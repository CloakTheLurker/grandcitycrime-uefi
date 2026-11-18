#define KEY_GAS 1
#define KEY_BRAKE 2
#define KEY_RIGHT 4
#define KEY_LEFT 8
#define KEY_HAND 16

enum { M_NONE, M_PARKED, M_TRAFFIC, M_COP, M_MINE };
enum { K_SEDAN, K_TAXI, K_VAN, K_SPORT, K_BOAT };
enum { P_WALK, P_WAIT, P_DODGE, P_FLEE, P_DOWN, P_FREEZE, P_IDLE, P_FOLLOW, P_COWER, P_HANDS, P_ANGRY, P_FIGHT, P_WAITX };
enum { PO_WALK, PO_RUN, PO_IDLE, PO_PHONE, PO_TEXT, PO_CHAT, PO_HANDSUP, PO_COWER, PO_ANGRY, PO_PUNCH, PO_DEAD, PO_AIM, PO_SHOCK, PO_AIMRUN };

typedef struct {
    float x, y, a, vx, vy;
    float r, steer, steer_in, gas, brk, roll, pitch;
    u32 paint;
    int cop, kind, keys, mode, timer, rev;
    int edge, dir, k, nedge, ndir;
    float hp, sink;
    int wreck, burn, gun;
    int stuck, pass;
    float shift, shift_to;
} Car;

typedef struct {
    float x, y, a, vx, vy, sp, off;
    u32 shirt, look;
    int dead, state, timer, edge, dir, k, side;
    float phase;
    int pose, act, buddy, punch;
    float hyaw, spd;
} Person;

#define TRAF0 5
#define PARK0 21
#define BOAT0 35
#define CARS 41
#define PEOPLE 96
#define AXLE 0.55f
#define YAW_I 0.32f
static Car cars[CARS];
static Person people[PEOPLE];
static int hold[5], enter_tap, fire_held, fire_tap;
static int mouse_dx, mouse_dy, mouse_left, mouse_right, mwheel, mouse_idle = 999, key_map, mouse_wheel_ok;
static float look_yaw, look_pitch;
static int mine = 0, tick;
static float me_x, me_y, me_a, me_vx, me_vy;
static float foot_x, foot_y, foot_a;
static int walking, wanted, money, caught, caught_how, bust, heat;
static float seen_x, seen_y;
static int seen;
static int key_job, key_wpn, job_pi = -1;
// weapons 0 fists 1 pistol 2 smg
static int weapon = 1, ammo[3] = {0, 60, 0}, punch_t;
static float shot_spread;
static void update_job(void);
static void gps_init(void);
static void say(const char *a, const char *b, int n);
static float cam_yaw_s;
static float my_hp = 100, my_armour = 0;
static int fire_cd, aim_t, shake, hit_cd;
static float foot_sink;
static float foot_phase;

enum { FX_SMOKE, FX_BLACK, FX_FIRE, FX_FLASH, FX_SPARK, FX_SPLASH };
typedef struct { float x, y, z, vx, vy, vz, size, grow; int life, max, type; } Fx;
#define FXN 360
static Fx fx[FXN];
static int fx_next;
typedef struct { float x0, y0, z0, x1, y1, z1; int life; } Tracer;
static Tracer tracers[16];
static int tr_next;
typedef struct { float x, y; int kind, on; } Pickup;
#define PICKS 6
static Pickup picks[PICKS];

static void emit(int type, float x, float y, float z, float vx, float vy, float vz, float size, float grow, int life) {
    Fx *f = &fx[fx_next++ % FXN];
    f->type = type; f->x = x; f->y = y; f->z = z; f->vx = vx; f->vy = vy; f->vz = vz;
    f->size = size; f->grow = grow; f->life = f->max = life;
}

static void tracer(float x0, float y0, float z0, float x1, float y1, float z1) {
    Tracer *t = &tracers[tr_next++ & 15];
    t->x0 = x0; t->y0 = y0; t->z0 = z0; t->x1 = x1; t->y1 = y1; t->z1 = z1; t->life = 4;
}

static void hurt_me(float d) {
    if (caught) return;
    my_armour -= d;
    if (my_armour < 0) { my_hp += my_armour; my_armour = 0; }
    if (my_hp <= 0) { my_hp = 0; caught = 200; caught_how = 1; }
}

static void damage_car_by(float *hp, int wreck, float d) {
    if (wreck || d < 4) return;
    *hp -= d;
}
#define damage_car(c, d) damage_car_by(&(c)->hp, (c)->wreck, (d) * (mine >= 0 && (c) == &cars[mine] ? 0.6f : 1.0f))
static float start_x, start_y, start_a;

static u32 paints[] = {0xff4f6a, 0x7a5cff, 0xf4f4f4, 0x3ad16a, 0xff7a2f, 0x40c8e0, 0x2a2a38, 0xe84ab0, 0x9aa4b4, 0x1a3a7a};
static u32 shirts[] = {0xff4f9a, 0x2fd0e0, 0xffd23f, 0xffffff, 0x7a5cff, 0xff7a2f, 0x3ad16a, 0xe83a3a};

static const float k_power[4] = {0.0007f, 0.0007f, 0.00052f, 0.00098f};
static const float k_top[4] = {0.27f, 0.27f, 0.21f, 0.35f};
static const float k_grip[4] = {0.0019f, 0.0019f, 0.0015f, 0.0024f};
static const float k_mass[5] = {1.0f, 1.0f, 1.5f, 0.9f, 2.0f};

static float *edge_pt(Edge *e, int dir, int k) { return &ep[(e->first + (dir ? e->n - 1 - k : k)) * 2]; }
static float speed2(Car *c) { return c->vx * c->vx + c->vy * c->vy; }
static float fwd_speed(Car *c) { return c->vx * cosf_(c->a) + c->vy * sinf_(c->a); }
static float dist2_me(float x, float y) { return (x - me_x) * (x - me_x) + (y - me_y) * (y - me_y); }

static int in_view(float x, float y) {
    float dx = x - cam.x, dy = y - cam.y, d2 = dx * dx + dy * dy;
    if (d2 > (fog_far + 6) * (fog_far + 6)) return 0;
    if (d2 < 9) return 1;
    float h = sqrtf_(cam_f.x * cam_f.x + cam_f.y * cam_f.y) + 1e-6f;
    float f = (dx * cam_f.x + dy * cam_f.y) / h, s = dx * cam_r.x + dy * cam_r.y;
    return f > -2 && absf(s) < f * 1.05f + 2.5f;
}

// grip peaks near 7 deg then drops a bit
static float tyre(float slip) {
    float s = slip / 0.13f, m = absf(s);
    if (m <= 1) return s * (1.5f - 0.5f * s * s);
    return (s < 0 ? -1 : 1) * (1 - 0.15f * minf(m - 1, 1));
}

static int corner_hit(float x, float y, float a, float *hx, float *hy) {
    float ca = cosf_(a), sa = sinf_(a);
    static const float cx[6] = {0.86f, 0.86f, -0.86f, -0.86f, 0.0f, 0.0f}, cy[6] = {0.42f, -0.42f, 0.42f, -0.42f, 0.44f, -0.44f};
    for (int i = 0; i < 6; i++) {
        float px = x + cx[i] * ca - cy[i] * sa, py = y + cx[i] * sa + cy[i] * ca;
        if (solid(px, py)) { *hx = px; *hy = py; return 1; }
    }
    return 0;
}

static void wall_normal(float px, float py, float vx, float vy, float *nx, float *ny) {
    for (float e = 0.3f; e < 1.3f; e += 0.45f) {
        float gx = (float)(solid(px - e, py) - solid(px + e, py)), gy = (float)(solid(px, py - e) - solid(px, py + e));
        float l = sqrtf_(gx * gx + gy * gy);
        if (l > 0) { *nx = gx / l; *ny = gy / l; return; }
    }
    float l = sqrtf_(vx * vx + vy * vy) + 1e-6f;
    *nx = -vx / l; *ny = -vy / l;
}

static void hit_wall(Car *c, float px, float py) {
    float nx, ny;
    wall_normal(px, py, c->vx, c->vy, &nx, &ny);
    float rx = px - c->x, ry = py - c->y;
    float pvx = c->vx - c->r * ry, pvy = c->vy + c->r * rx;
    float vn = pvx * nx + pvy * ny;
    if (vn < 0) {
        float rn = rx * ny - ry * nx;
        float j = -1.3f * vn / (1 + rn * rn / YAW_I);
        if (j > 0.025f) damage_car(c, j * 1300);
        c->vx += j * nx; c->vy += j * ny; c->r += rn * j / YAW_I;
        float tx = -ny, ty = nx, vt = pvx * tx + pvy * ty, rt = rx * ty - ry * tx;
        float jt = clampf(-vt / (1 + rt * rt / YAW_I), -0.35f * j, 0.35f * j);
        c->vx += jt * tx; c->vy += jt * ty; c->r += rt * jt / YAW_I;
    }
    c->x += nx * 0.03f; c->y += ny * 0.03f;
}

static int fallen[64], n_fallen;

static void hit_poles(Car *c) {
    float ca = cosf_(c->a), sa = sinf_(c->a), s2 = speed2(c);
    int bx0 = maxi(0, (int)((c->x - 1) / BK)), bx1 = mini(BW - 1, (int)((c->x + 1) / BK));
    int by0 = maxi(0, (int)((c->y - 1) / BK)), by1 = mini(BH - 1, (int)((c->y + 1) / BK));
    for (int by = by0; by <= by1; by++)
        for (int bx = bx0; bx <= bx1; bx++) {
            Bucket *k = &bk[4][by * BW + bx];
            for (u32 i = 0; i < k->n; i++) {
                int id = (int)bl[4][k->first + i];
                Pole *p = &poles[id];
                if (p->tilt > 1.2f) continue;
                float dx = p->x - c->x, dy = p->y - c->y;
                float t = clampf(dx * ca + dy * sa, -0.85f, 0.85f);
                float qx = c->x + ca * t - p->x, qy = c->y + sa * t - p->y, d = sqrtf_(qx * qx + qy * qy);
                if (d >= 0.48f) continue;
                if (s2 > 0.0009f && p->tilt == 0) {
                    p->fa = atan2f_(c->vy, c->vx);
                    p->tv = 0.015f + sqrtf_(s2) * 0.25f;
                    p->tilt = 0.001f;
                    c->vx *= 0.85f; c->vy *= 0.85f; damage_car(c, 12); c->r += (t > 0 ? 1 : -1) * 0.01f * (qx * sa - qy * ca > 0 ? 1 : -1);
                    if (n_fallen < 64) fallen[n_fallen++] = id;
                } else if (p->tilt == 0 && d > 1e-4f) {
                    float nx = qx / d, ny = qy / d, o = 0.48f - d;
                    c->x += nx * o; c->y += ny * o;
                    float vn = c->vx * nx + c->vy * ny;
                    if (vn < 0) { c->vx -= vn * nx * 1.3f; c->vy -= vn * ny * 1.3f; }
                }
            }
        }
}

static void update_poles(void) {
    for (int i = 0; i < n_fallen; i++) {
        Pole *p = &poles[fallen[i]];
        if (p->tilt < 1.5f) {
            p->tv += 0.003f * sinf_(p->tilt) + 0.0006f;
            p->tilt = minf(p->tilt + p->tv, 1.5f);
        }
        if (dist2_me(p->x, p->y) > 110 * 110 && !in_view(p->x, p->y)) {
            p->tilt = 0; p->tv = 0;
            fallen[i--] = fallen[--n_fallen];
        }
    }
}

static void drive_(Car *c);
static void drive_boat(Car *c);
static void drive(Car *c) {
    if (c->kind == K_BOAT) { drive_boat(c); return; }
    sea_ok = c == &cars[mine] && mine >= 0;
    drive_(c);
    sea_ok = 0;
}

static void drive_(Car *c) {
    int kd = c->kind;
    float pw = k_power[kd], top = k_top[kd], grip = k_grip[kd];
    if (c->cop) { pw *= 1.25f; top = 0.31f; grip *= 1.12f; }
    float ca = cosf_(c->a), sa = sinf_(c->a);
    float u = c->vx * ca + c->vy * sa;
    float v = c->vy * ca - c->vx * sa;
    float au = absf(u);
    float want = c->steer_in * 0.6f / (1 + au * 8);
    float rate = c->steer_in == 0 ? 0.06f : 0.035f;
    c->steer += clampf(want - c->steer, -rate, rate);
    float fx = 0, brake = 0;
    if (c->gas > 0) fx += u < -0.005f ? 0.0018f * c->gas : pw * c->gas * maxf(0, 1 - (u / top) * (u / top));
    if (c->brk > 0) {
        if (u > 0.005f) brake = 0.0018f * c->brk;
        else if (c->rev && u > -0.08f) fx -= 0.0006f * c->brk;
        else if (!c->rev) { brake = 0; if (au < 0.012f) u = 0; else fx -= (u < 0 ? -0.0018f : 0.0018f) * c->brk; }
    }
    fx -= brake;
    fx -= u * au * 0.0012f + (u > 0 ? 0.00006f : u < 0 ? -0.00006f : 0);
    if (c->gas == 0 && u > 0) fx -= 0.00022f;
    if (c->gas == 0 && c->brk == 0 && au < 0.0008f) { u = 0; fx = 0; }
    float g = grip * 0.5f, gr = g;
    // handbrake leaves 30 pct back grip thats the drift
    if (c->keys & KEY_HAND) { gr *= 0.3f; if (u > 0.003f) fx -= 0.0008f; }
    float push = maxf(fx, 0) / (gr * 2.4f), stop = brake * 0.6f / (g * 2.4f);
    float avr = sqrtf_(maxf(0, 1 - minf(1, push + stop) * minf(1, push + stop)));
    float avf = sqrtf_(maxf(0, 1 - minf(1, stop) * minf(1, stop)));
    float den = maxf(au, 0.03f);
    float af = atan2f_(v + AXLE * c->r - u * c->steer, den);
    float ar = atan2f_(v - AXLE * c->r, den);
    float Ff = -tyre(af) * g * avf, Fr = -tyre(ar) * gr * avr;
    float cs = cosf_(c->steer), ss = sinf_(c->steer);
    float ay = Ff * cs + Fr, ax = fx - Ff * ss;
    float nu = u + ax + c->r * v;
    if (u > 0 && nu < 0 && c->gas == 0 && !(c->rev && c->brk > 0)) nu = 0;
    float nv = v + ay - c->r * u;
    c->r += (AXLE * Ff * cs - AXLE * Fr) / YAW_I;
    float k = clampf(au / 0.05f, 0, 1);
    c->r = c->r * k + nu * ss / (2 * AXLE) * (1 - k);
    nv *= 0.5f + 0.5f * k;
    c->r *= 0.995f;
    u = nu; v = nv;
    c->roll += (clampf(ay * 40, -0.075f, 0.075f) - c->roll) * 0.12f;
    c->pitch += (clampf(ax * 30, -0.055f, 0.03f) - c->pitch) * 0.12f;
    float na = c->a + c->r;
    float nc = cosf_(na), ns = sinf_(na);
    c->vx = u * nc - v * ns;
    c->vy = u * ns + v * nc;
    float hx, hy;
    if (corner_hit(c->x + c->vx, c->y + c->vy, na, &hx, &hy)) {
        hit_wall(c, hx, hy);
        return;
    }
    c->x += c->vx; c->y += c->vy; c->a = wrapf(na);
    hit_poles(c);
}

static void speed_to(Car *c, float vt) {
    float e = vt - fwd_speed(c);
    c->gas = clampf(e * 30, 0, 1);
    c->brk = clampf(-e * 25, 0, 1);
    if (vt < 0.004f) { c->gas = 0; c->brk = maxf(c->brk, 0.6f); }
}

static float steer_to(Car *c, float want) {
    float d = wrapf(want - c->a);
    float u = fwd_speed(c);
    c->steer_in = clampf(d * (2.0f + u * 10), -1, 1);
    return d;
}

static int edge_near(float rmin, float rmax) {
    for (int t = 0; t < 40; t++) {
        float ox = (rndf() * 2 - 1) * rmax, oy = (rndf() * 2 - 1) * rmax;
        int bx = (int)((me_x + ox) / BK), by = (int)((me_y + oy) / BK);
        if (bx < 0 || by < 0 || bx >= BW || by >= BH) continue;
        Bucket *k = &bk[1][by * BW + bx];
        if (!k->n) continue;
        int id = (int)bl[1][k->first + rnd_in((int)k->n)];
        Edge *e = &edges[id];
        float *p = &ep[(e->first + e->n / 2) * 2];
        float d2 = dist2_me(p[0], p[1]);
        if (d2 < rmin * rmin || d2 > rmax * rmax) continue;
        return id;
    }
    return -1;
}

static int oneway(Edge *e) { return (e->flags & 2) || e->cls == 0; }
static float lane_off(Edge *e) { return oneway(e) ? 0 : (e->w - ((e->flags & 4) ? 1.64f : 0)) * 0.25f; }

static void waypoint(Car *c, float *tx, float *ty) {
    Edge *e = &edges[c->edge];
    int k = c->k < e->n ? c->k : e->n - 1;
    float *p = edge_pt(e, c->dir, k), *q = edge_pt(e, c->dir, k > 0 ? k - 1 : 0);
    float dx = p[0] - q[0], dy = p[1] - q[1], L = sqrtf_(dx * dx + dy * dy) + 1e-6f;
    float lane = lane_off(e) + c->shift;
    *tx = p[0] + dy / L * lane;
    *ty = p[1] - dx / L * lane;
}

static void pick_next(Car *c, int chase) {
    Edge *e = &edges[c->edge];
    int node = c->dir ? e->a : e->b;
    Node *nd = &nodes[node];
    int best = -1;
    float bd = 1e18f;
    for (int i = 0; i < nd->n; i++) {
        int id = adj[nd->first + i];
        Edge *f = &edges[id];
        if (id == c->edge || f->n < 2) continue;
        if (oneway(f) && f->a != node) continue;
        float score;
        if (chase) {
            int far = f->a == node ? f->b : f->a;
            float qx = nodes[far].x - seen_x, qy = nodes[far].y - seen_y;
            score = qx * qx + qy * qy + rndf() * (seen ? 0 : 400);
        } else score = rndf() * (1 + f->cls * 0.12f);
        if (score < bd) { bd = score; best = id; }
    }
    if (best < 0) {
        if (oneway(e)) { c->nedge = -1; return; }
        c->nedge = c->edge; c->ndir = c->dir ^ 1;
        return;
    }
    c->nedge = best;
    c->ndir = edges[best].a == node ? 0 : 1;
}

static void follow_road(Car *c, int chase) {
    float tx, ty;
    waypoint(c, &tx, &ty);
    float dx = tx - c->x, dy = ty - c->y;
    if (dx * dx + dy * dy < 2.0f) {
        c->k++;
        if (c->k >= edges[c->edge].n) {
            if (chase) pick_next(c, 1);
            if (c->nedge < 0) { c->mode = M_NONE; return; }
            c->edge = c->nedge; c->dir = c->ndir; c->k = 1;
            pick_next(c, chase);
        }
        waypoint(c, &tx, &ty);
    }
    steer_to(c, atan2f_(ty - c->y, tx - c->x));
}

static void reset_car(Car *c) {
    c->vx = c->vy = c->r = c->steer = c->steer_in = c->gas = c->brk = c->roll = c->pitch = 0;
    c->keys = c->timer = c->rev = c->cop = 0;
    c->stuck = c->pass = 0; c->shift = c->shift_to = 0;
    c->hp = 1000; c->wreck = c->burn = 0; c->gun = 60; c->sink = 0;
}

static void place_on_edge(Car *c, int id, int dir) {
    Edge *e = &edges[id];
    reset_car(c);
    c->edge = id; c->dir = dir; c->k = 1;
    float *p = edge_pt(e, dir, 0), *q = edge_pt(e, dir, 1);
    float dx = q[0] - p[0], dy = q[1] - p[1], L = sqrtf_(dx * dx + dy * dy) + 1e-6f;
    float lane = lane_off(e);
    float t = minf(L * 0.5f, 0.5f + rndf() * L * 0.6f);
    c->x = p[0] + dx / L * t + dy / L * lane;
    c->y = p[1] + dy / L * t - dx / L * lane;
    c->a = atan2f_(dy, dx);
    pick_next(c, 0);
}

static int crowded(Car *self, float x, float y, float r) {
    for (int i = 0; i < CARS; i++) {
        Car *o = &cars[i];
        if (o == self || !o->mode) continue;
        if ((o->x - x) * (o->x - x) + (o->y - y) * (o->y - y) < r * r) return 1;
    }
    return 0;
}

static int pick_kind(void) {
    int r = rnd_in(100);
    return r < 45 ? K_SEDAN : r < 65 ? K_TAXI : r < 80 ? K_VAN : K_SPORT;
}

static void paint_car(Car *c) {
    c->kind = pick_kind();
    c->paint = c->kind == K_TAXI ? 0xffc21a : paints[rnd_in(10)];
}

static const float cls_keep[13] = {1, 1, 1, 0.85f, 0.7f, 0.35f, 0.3f, 0.2f, 0.6f, 0.6f, 0.6f, 0.5f, 0.5f};

static void spawn_traffic(Car *c) {
    c->mode = M_NONE;
    for (int t = 0; t < 4; t++) {
        int id = edge_near(16, 90);
        if (id < 0 || edges[id].n < 2) continue;
        Edge *e = &edges[id];
        if (rndf() > cls_keep[e->cls]) continue;
        place_on_edge(c, id, oneway(e) ? 0 : rnd_in(2));
        if (solid(c->x, c->y) || in_view(c->x, c->y) || dist2_me(c->x, c->y) < 14 * 14 || crowded(c, c->x, c->y, 3)) continue;
        float hx, hy;
        if (corner_hit(c->x, c->y, c->a, &hx, &hy)) continue;
        c->mode = M_TRAFFIC;
        paint_car(c);
        return;
    }
}

static int near_pole(float x, float y, float r) {
    int bx = (int)(x / BK), by = (int)(y / BK);
    if (bx < 0 || by < 0 || bx >= BW || by >= BH) return 0;
    Bucket *k = &bk[4][by * BW + bx];
    for (u32 i = 0; i < k->n; i++) {
        Pole *p = &poles[bl[4][k->first + i]];
        if ((p->x - x) * (p->x - x) + (p->y - y) * (p->y - y) < r * r) return 1;
    }
    return 0;
}

static void spawn_parked(Car *c) {
    c->mode = M_NONE;
    for (int t = 0; t < 6; t++) {
        int id = edge_near(10, 75);
        if (id < 0) continue;
        Edge *e = &edges[id];
        if (e->cls < 3 || e->cls > 7 || (e->flags & 1) || e->n < 2) continue;
        int k = rnd_in(e->n - 1);
        float *a = &ep[(e->first + k) * 2], *b = a + 2;
        float dx = b[0] - a[0], dy = b[1] - a[1], L = sqrtf_(dx * dx + dy * dy);
        if (L < 4.5f) continue;
        float ux = dx / L, uy = dy / L, side = (rnd() & 1) ? 1.0f : -1.0f;
        float s = 1.6f + rndf() * (L - 3.2f), off = (e->flags & 4) ? e->w * 0.5f - 0.42f : e->w * 0.5f + 0.48f;
        float x = a[0] + ux * s + uy * off * side, y = a[1] + uy * s - ux * off * side;
        if (ground_kind(x, y) != 2 || in_view(x, y) || dist2_me(x, y) < 10 * 10) continue;
        float ang = side > 0 ? atan2f_(uy, ux) : atan2f_(-uy, -ux), ca = cosf_(ang), sa = sinf_(ang);
        int ok = 1;
        for (int q = 0; q < 4 && ok; q++) {
            float px = x + ((q & 1) ? 0.9f : -0.9f) * ca - ((q & 2) ? 0.46f : -0.46f) * sa;
            float py = y + ((q & 1) ? 0.9f : -0.9f) * sa + ((q & 2) ? 0.46f : -0.46f) * ca;
            int g = ground_kind(px, py);
            if ((g != 1 && g != 2) || solid(px, py)) ok = 0;
        }
        if (!ok || near_pole(x, y, 1.4f) || crowded(c, x, y, 2.4f)) continue;
        reset_car(c);
        c->x = x; c->y = y; c->a = ang;
        c->mode = M_PARKED;
        paint_car(c);
        return;
    }
}

static float last_angle(Edge *e, int dir) {
    float *p = edge_pt(e, dir, e->n - 1), *q = edge_pt(e, dir, e->n - 2);
    return atan2f_(p[1] - q[1], p[0] - q[0]);
}

static float traffic_speed(Car *c, float cruise) {
    Edge *e = &edges[c->edge];
    int k = mini(c->k, e->n - 1);
    float *p = edge_pt(e, c->dir, k), *q = edge_pt(e, c->dir, k > 0 ? k - 1 : 0);
    float a0 = atan2f_(p[1] - q[1], p[0] - q[0]), a1;
    if (k < e->n - 1) { float *r = edge_pt(e, c->dir, k + 1); a1 = atan2f_(r[1] - p[1], r[0] - p[0]); }
    else if (c->nedge >= 0) {
        Edge *f = &edges[c->nedge];
        float *r0 = edge_pt(f, c->ndir, 0), *r1 = edge_pt(f, c->ndir, 1);
        a1 = atan2f_(r1[1] - r0[1], r1[0] - r0[0]);
    } else a1 = a0 + PI;
    float dist = sqrtf_((p[0] - c->x) * (p[0] - c->x) + (p[1] - c->y) * (p[1] - c->y));
    float turn = absf(wrapf(a1 - a0));
    float vb = cruise * (1 - 0.65f * clampf(turn / 1.4f, 0, 1));
    float vt = minf(cruise, sqrtf_(vb * vb + 2 * 0.0011f * maxf(0, dist - 1.5f)));
    int node = c->dir ? e->a : e->b;
    if (nlit[node]) {
        float *n = edge_pt(e, c->dir, e->n - 1);
        float dn = sqrtf_((n[0] - c->x) * (n[0] - c->x) + (n[1] - c->y) * (n[1] - c->y));
        if (dn < 30) {
            int grp = absf(cosf_(last_angle(e, c->dir) - nax[node])) > 0.7071f ? 0 : 1;
            int st = light_state(node, grp, tick);
            float stop = dn - (nwid[node] * 0.5f + 1.2f);
            if ((st == 2 || (st == 1 && stop > 2.0f)) && stop > -0.4f)
                vt = minf(vt, sqrtf_(2 * 0.0011f * maxf(0, stop)));
        }
    }
    return vt;
}

static int gap_still;
static float gap_speed(Car *c) {
    float ca = cosf_(c->a), sa = sinf_(c->a), best = 1e9f, ou = 0;
    int still = 0;
    for (int i = 0; i <= CARS; i++) {
        float ox, oy, ov = 0;
        if (i == CARS) {
            if (mine >= 0) break;
            ox = foot_x; oy = foot_y;
        } else {
            Car *o = &cars[i];
            if (o == c || !o->mode || o->kind == K_BOAT) continue;
            ox = o->x; oy = o->y; ov = o->vx * ca + o->vy * sa;
        }
        float dx = ox - c->x, dy = oy - c->y;
        float f = dx * ca + dy * sa, s = dy * ca - dx * sa;
        if (f > 0.3f && f < 16 && absf(s) < 0.8f && f < best) {
            best = f; ou = ov;
            still = i == CARS || (cars[i].mode == M_PARKED || cars[i].mode == M_MINE || cars[i].wreck || speed2(&cars[i]) < 0.0004f);
        }
    }
    for (int i = 0; i < PEOPLE; i++) {
        Person *p = &people[i];
        if (p->dead && p->state != P_DOWN) continue;
        float dx = p->x - c->x, dy = p->y - c->y;
        float f = dx * ca + dy * sa, s = dy * ca - dx * sa;
        if (f > 0.3f && f < 9 && absf(s) < 0.75f && f < best) { best = f; ou = 0; still = p->state == P_DOWN; }
    }
    gap_still = still;
    if (best > 1e8f) return 1e9f;
    float g = best - 2.1f;
    if (g <= 0) return 0;
    return maxf(0, ou) * 0.9f + sqrtf_(2 * 0.0012f * g);
}

static void ai_traffic(Car *c) {
    float d2 = dist2_me(c->x, c->y);
    if (d2 > 95 * 95 || (d2 > 60 * 60 && !in_view(c->x, c->y))) { spawn_traffic(c); return; }
    if (speed2(c) < 0.0004f) { if (++c->timer > 900 && !in_view(c->x, c->y)) { spawn_traffic(c); return; } }
    else c->timer = 0;
    c->keys = 0; c->rev = 0;
    int cl = edges[c->edge].cls;
    float cruise = cl <= 2 ? 0.17f : cl <= 4 ? 0.14f : 0.10f;
    follow_road(c, 0);
    if (!c->mode) return;
    float ts = traffic_speed(c, cruise), gs = gap_speed(c);
    if (!c->pass && gs < 0.02f && gap_still && ts > 0.05f) c->stuck++;
    else if (c->stuck > 0) c->stuck -= 2;
    if (c->stuck > 150 && !c->pass) {
        Edge *e = &edges[c->edge];
        float ca = cosf_(c->a), sa = sinf_(c->a);
        float side = oneway(e) ? (e->w > 2.6f ? 1.0f : 0) : -(lane_off(e) * 2);
        int clear = side != 0;
        for (int i = 0; i < CARS && clear; i++) {
            Car *o = &cars[i];
            if (o == c || !o->mode || o->kind == K_BOAT) continue;
            float dx = o->x - c->x, dy = o->y - c->y, f = dx * ca + dy * sa, s = dy * ca - dx * sa;
            if (f > -1 && f < 26 && absf(s - (-side)) < 1.0f && o->mode != M_PARKED) clear = 0;
        }
        float px = c->x + ca * 3 + sa * side, py = c->y + sa * 3 - ca * side;
        if (clear && !solid(px, py)) { c->pass = 320; c->shift_to = side; c->stuck = 0; }
        else c->stuck = 100;
    }
    if (c->pass && --c->pass == 0) c->shift_to = 0;
    c->shift += (c->shift_to - c->shift) * 0.04f;
    float vt = minf(ts, gs);
    if (c->pass) vt = minf(minf(ts, 0.08f), gap_still ? 1e9f : gs);
    float tx, ty;
    waypoint(c, &tx, &ty);
    if (absf(wrapf(atan2f_(ty - c->y, tx - c->x) - c->a)) > 0.8f) vt = minf(vt, 0.05f);
    speed_to(c, vt);
}

static void spawn_cop(Car *c) {
    int id = edge_near(20, 50);
    if (id < 0 || edges[id].n < 2) return;
    place_on_edge(c, id, oneway(&edges[id]) ? 0 : rnd_in(2));
    float hx, hy;
    if (solid(c->x, c->y) || in_view(c->x, c->y) || crowded(c, c->x, c->y, 3) || corner_hit(c->x, c->y, c->a, &hx, &hy)) { c->mode = M_NONE; return; }
    c->cop = 1;
    c->kind = K_SEDAN;
    c->mode = M_COP;
    c->paint = 0xf2f2f6;
}

static void ai_cop(Car *c) {
    c->keys = 0;
    if (c->timer < 0) { c->timer++; c->gas = 0; c->brk = 1; c->rev = 1; c->steer_in = -1; return; }
    c->rev = 0;
    float d2 = dist2_me(c->x, c->y);
    if (d2 < 22 * 22 && seen) {
        float d = sqrtf_(d2), cs = maxf(sqrtf_(speed2(c)), 0.08f);
        float t = clampf(d / cs, 0, 50);
        float ax = me_x + me_vx * t, ay = me_y + me_vy * t;
        float da = steer_to(c, atan2f_(ay - c->y, ax - c->x));
        speed_to(c, absf(da) > 1.4f ? 0.06f : 0.3f);
        if (d < 2.2f && mine >= 0) speed_to(c, sqrtf_(me_vx * me_vx + me_vy * me_vy));
    } else {
        follow_road(c, 1);
        if (!c->mode) return;
        speed_to(c, 0.24f);
    }
    if (speed2(c) < 0.0004f) { if (++c->timer > 60) c->timer = -45; }
    else c->timer = 0;
}

static int nearest_edge(float x, float y, int *kk) {
    int bx0 = (int)(x / BK), by0 = (int)(y / BK), best = -1;
    float bd = 1e18f;
    for (int by = by0 - 1; by <= by0 + 1; by++)
        for (int bx = bx0 - 1; bx <= bx0 + 1; bx++) {
            if (bx < 0 || by < 0 || bx >= BW || by >= BH) continue;
            Bucket *k = &bk[1][by * BW + bx];
            for (u32 i = 0; i < k->n; i++) {
                int id = (int)bl[1][k->first + i];
                Edge *e = &edges[id];
                if (e->cls <= 1 || (e->flags & 1) || e->n < 2) continue;
                for (int j = 0; j < e->n; j++) {
                    float *p = &ep[(e->first + j) * 2];
                    float d = (p[0] - x) * (p[0] - x) + (p[1] - y) * (p[1] - y);
                    if (d < bd) { bd = d; best = id; *kk = j; }
                }
            }
        }
    return best;
}

// ring check so nobody sinks into walls
static const float ring8[8][2] = {{1, 0}, {0.7071f, 0.7071f}, {0, 1}, {-0.7071f, 0.7071f}, {-1, 0}, {-0.7071f, -0.7071f}, {0, -1}, {0.7071f, -0.7071f}};
static int near_solid(float x, float y, float r) {
    if (solid(x, y)) return 1;
    for (int k = 0; k < 8; k++) if (solid_walls(x + ring8[k][0] * r, y + ring8[k][1] * r)) return 1;
    return 0;
}
static int body_solid(float x, float y) { return near_solid(x, y, 0.28f); }

static int walk_target(Person *p, float *tx, float *ty) {
    Edge *e = &edges[p->edge];
    int k = mini(p->k, e->n - 1);
    float *q = edge_pt(e, p->dir, k), *o;
    float dx, dy;
    if (k > 0) { o = edge_pt(e, p->dir, k - 1); dx = q[0] - o[0]; dy = q[1] - o[1]; }
    else { o = edge_pt(e, p->dir, 1); dx = o[0] - q[0]; dy = o[1] - q[1]; }
    float L = sqrtf_(dx * dx + dy * dy) + 1e-6f;
    float offs[4] = {e->w * 0.5f + p->off, e->w * 0.5f + 1.0f, e->w * 0.5f + 0.75f, e->w * 0.5f + 0.55f};
    for (int i = 0; i < 4; i++) {
        float x = q[0] + dy / L * offs[i] * p->side, y = q[1] - dx / L * offs[i] * p->side;
        int g = ground_kind(x, y);
        if ((g == 2 || g == 4 || g == 5) && !near_solid(x, y, 0.45f)) { *tx = x; *ty = y; return 1; }
    }
    return 0;
}

static void next_walk(Person *p) {
    Edge *e = &edges[p->edge];
    int node = p->dir ? e->a : e->b;
    Node *nd = &nodes[node];
    int cand[16], n = 0;
    for (int i = 0; i < nd->n && n < 16; i++) {
        int id = adj[nd->first + i];
        Edge *f = &edges[id];
        if (id == p->edge || f->cls <= 1 || (f->flags & 1) || f->n < 2) continue;
        cand[n++] = id;
    }
    if (!n) { p->dir ^= 1; p->side = -p->side; p->k = 1; return; }
    int id = cand[rnd_in(n)];
    p->edge = id;
    p->dir = edges[id].a == node ? 0 : 1;
    p->k = 0;
}

static void attach_person(Person *p) {
    int k = 0, id = nearest_edge(p->x, p->y, &k);
    if (id < 0) { p->dead = 30; p->state = P_DOWN; p->x = p->y = -1000; return; }
    p->edge = id; p->dir = rnd_in(2); p->k = p->dir ? edges[id].n - 1 - k : k;
    p->side = (rnd() & 1) ? 1 : -1;
    p->state = P_WALK;
}

static Person *spare_person(Person *not) {
    for (int i = 0; i < PEOPLE; i++) {
        Person *q = &people[i];
        if (q != not && q->dead && q->dead < 600 && q->state == P_DOWN) return q;
    }
    for (int i = 0; i < PEOPLE; i++) {
        Person *q = &people[i];
        if (q != not && !q->dead && q->state == P_WALK && dist2_me(q->x, q->y) > 40 * 40 && !in_view(q->x, q->y)) return q;
    }
    return 0;
}

static void start_idle(Person *p) {
    int r = rnd_in(100);
    p->state = P_IDLE;
    p->act = r < 45 ? 1 : r < 80 ? 2 : 4;
    p->timer = 500 + rnd_in(1400);
    p->phase = 0;
}

static void decorate_spawn(Person *p) {
    p->pose = PO_WALK; p->act = 0; p->hyaw = 0; p->buddy = -1;
    int r = rnd_in(100);
    if (r < 20) { start_idle(p); p->a = rndf() * TAU; return; }
    if (r < 30) {
        Person *q = spare_person(p);
        if (!q) return;
        start_idle(p); p->act = 3;
        float fa = rndf() * TAU;
        p->a = fa;
        *q = *p;
        q->x = p->x + cosf_(fa) * 0.75f; q->y = p->y + sinf_(fa) * 0.75f;
        if (body_solid(q->x, q->y)) { q->x = p->x; q->y = p->y; q->state = P_DOWN; q->dead = 30; q->x = -1000; return; }
        q->a = fa + PI; q->look = rnd(); q->shirt = shirts[rnd_in(8)]; q->dead = 0;
        return;
    }
    if (r < 45) {
        Person *q = spare_person(p);
        if (!q) return;
        *q = *p;
        q->look = rnd(); q->shirt = shirts[rnd_in(8)]; q->dead = 0;
        q->state = P_FOLLOW; q->buddy = (int)(p - people);
        q->x = p->x - sinf_(p->a) * 0.5f; q->y = p->y + cosf_(p->a) * 0.5f;
        if (body_solid(q->x, q->y)) { q->state = P_DOWN; q->dead = 30; q->x = -1000; }
    }
}

static int crossing_clear(float mx, float my, float dx, float dy, Person *p) {
    for (int i = 0; i < CARS; i++) {
        Car *c = &cars[i];
        if (!c->mode || c->mode == M_PARKED) continue;
        float s2 = speed2(c);
        float rx = mx - c->x, ry = my - c->y, d2 = rx * rx + ry * ry;
        if (d2 > 12 * 12) continue;
        if (d2 < 2.2f * 2.2f) return 0;
        if (s2 < 0.0004f) continue;
        if (rx * c->vx + ry * c->vy > 0) return 0;
    }
    Edge *e = &edges[p->edge];
    int nds[2] = {e->a, e->b};
    for (int k = 0; k < 2; k++) {
        int n = nds[k];
        if (!nlit[n]) continue;
        float qx = nodes[n].x - mx, qy = nodes[n].y - my;
        if (qx * qx + qy * qy > 9 * 9) continue;
        float ang = atan2f_(dy, dx);
        int g = absf(cosf_(ang - nax[n])) > 0.7071f ? 0 : 1;
        if (light_state(n, 1 - g, tick) != 2) return 0;
    }
    return 1;
}

static int path_crosses_road(Person *p, float tx, float ty) {
    float mx = (p->x + tx) * 0.5f, my = (p->y + ty) * 0.5f;
    return (cell(mx, my) & C_ROAD) || (cell((p->x * 3 + tx) * 0.25f, (p->y * 3 + ty) * 0.25f) & C_ROAD) || (cell((p->x + tx * 3) * 0.25f, (p->y + ty * 3) * 0.25f) & C_ROAD);
}

static void gun_panic(float x, float y) {
    for (int i = 0; i < PEOPLE; i++) {
        Person *q = &people[i];
        if (q->dead || q->state == P_FIGHT) continue;
        float dx = q->x - x, dy = q->y - y, d = dx * dx + dy * dy;
        if (d > 15 * 15) continue;
        d = sqrtf_(d) + 1e-3f;
        float facing = absf(wrapf(atan2f_(-dy, -dx) - q->a));
        int r = rnd_in(100);
        if (q->state == P_COWER || q->state == P_HANDS) { q->timer = maxi(q->timer, 120); continue; }
        if (d < 7 && facing < 1.2f && r < 30) { q->state = P_HANDS; q->timer = 200 + rnd_in(120); q->a = atan2f_(-dy, -dx); }
        else if (r < 60) { q->state = P_COWER; q->timer = 160 + rnd_in(200); }
        else { q->state = P_FLEE; q->timer = 220 + rnd_in(160); q->vx = dx / d; q->vy = dy / d; q->spd = 0; }
    }
}

static void look_at_me(Person *p) {
    float dx = me_x - p->x, dy = me_y - p->y, d2 = dx * dx + dy * dy;
    float t = 0;
    if (d2 < 6 * 6 && !caught) t = clampf(wrapf(atan2f_(dy, dx) - p->a), -1.1f, 1.1f);
    p->hyaw += (t - p->hyaw) * 0.08f;
}

static void face(Person *p, float x, float y, float k) {
    p->a += clampf(wrapf(atan2f_(y - p->y, x - p->x) - p->a), -k, k);
}

static void spawn_person(Person *p) {
    // half spawn up the street so it isnt empty
    int ahead = rnd_in(2);
    for (int t = 0; t < 8; t++) {
        int id = ahead ? edge_near(40, 72) : edge_near(6, 45);
        if (id < 0) continue;
        Edge *e = &edges[id];
        if ((e->flags & 1) || e->cls <= 1 || e->n < 2) continue;
        p->edge = id; p->dir = rnd_in(2); p->k = rnd_in(e->n);
        p->side = (rnd() & 1) ? 1 : -1;
        p->off = 1.15f + rndf() * 0.5f;
        float x, y;
        if (!walk_target(p, &x, &y)) continue;
        if (in_view(x, y) && (!ahead || dist2_me(x, y) < 40 * 40)) continue;
        if (ahead && !in_view(x, y)) continue;
        p->x = x; p->y = y; p->a = rndf() * TAU;
        p->k++;
        if (p->k >= e->n) next_walk(p);
        p->sp = 0.0085f + rndf() * 0.003f;
        p->shirt = shirts[rnd_in(8)];
        p->look = rnd();
        p->dead = 0; p->state = P_WALK; p->timer = 0;
        p->vx = p->vy = 0; p->spd = 0;
        p->phase = rndf() * TAU;
        decorate_spawn(p);
        return;
    }
    p->dead = 30; p->state = P_DOWN;
    p->x = p->y = -1000;
}

static void panic(float x, float y, float r) {
    for (int i = 0; i < PEOPLE; i++) {
        Person *q = &people[i];
        if (q->dead) continue;
        float dx = q->x - x, dy = q->y - y, d = dx * dx + dy * dy;
        if (d > r * r) continue;
        d = sqrtf_(d) + 1e-3f;
        q->state = P_FLEE; q->timer = 180 + rnd_in(140);
        q->vx = dx / d; q->vy = dy / d; q->spd = 0;
    }
}

static void knock(Person *p, Car *c, int by_me) {
    p->state = P_DOWN; p->dead = 900;
    p->vx = c->vx * 0.85f; p->vy = c->vy * 0.85f;
    if (speed2(c) > 0.0001f) p->a = atan2f_(c->vy, c->vx);
    c->vx *= 0.92f; c->vy *= 0.92f;
    if (by_me) {
        if (wanted < 5) wanted++;
        heat = 0;
        money += 10 + rnd_in(90);
        panic(p->x, p->y, 14);
    }
}

static void move_free(Person *p, float damp) {
    float nx = p->x + p->vx, ny = p->y + p->vy;
    if (!body_solid(nx, p->y)) p->x = nx; else p->vx = -p->vx * 0.3f;
    if (!body_solid(p->x, ny)) p->y = ny; else p->vy = -p->vy * 0.3f;
    p->vx *= damp; p->vy *= damp;
}

static void flee_steer(Person *p) {
    float l = sqrtf_(p->vx * p->vx + p->vy * p->vy) + 1e-6f;
    float ax = p->x + p->vx / l * 1.2f, ay = p->y + p->vy / l * 1.2f;
    int g = ground_kind(ax, ay);
    if (g == 0 || g == 3) { float t = p->vx; p->vx = -p->vy; p->vy = t; }
}

static void run_move(Person *p, float target, float acc) {
    float l = sqrtf_(p->vx * p->vx + p->vy * p->vy) + 1e-6f, dx = p->vx / l, dy = p->vy / l;
    p->spd += clampf(target - p->spd, -acc, acc);
    float nx = p->x + dx * p->spd, ny = p->y + dy * p->spd;
    if (!body_solid(nx, ny)) { p->x = nx; p->y = ny; }
    else { p->vx = -dy; p->vy = dx; p->spd *= 0.5f; }
    p->a += clampf(wrapf(atan2f_(dy, dx) - p->a), -0.14f, 0.14f);
    p->phase += p->spd * 4.9f;
}

static void watch_cars(Person *p, int look) {
    for (int i = 0; i < CARS; i++) {
        Car *c = &cars[i];
        if (!c->mode) continue;
        float s2 = speed2(c);
        float dx = p->x - c->x, dy = p->y - c->y;
        if (dx * dx + dy * dy < 0.5f && s2 > 0.0016f) { knock(p, c, i == mine); return; }
        if (!look || s2 < 0.0036f || dx * dx + dy * dy > 15 * 15) continue;
        float t = (dx * c->vx + dy * c->vy) / s2;
        if (t < 0 || t > 34) continue;
        float cx = dx - c->vx * t, cy = dy - c->vy * t;
        if (cx * cx + cy * cy > 1.0f) continue;
        if (rnd_in(100) < 40) { p->state = P_FREEZE; p->timer = 70; p->phase = 0; return; }
        float sv = sqrtf_(s2);
        float px = -c->vy / sv, py = c->vx / sv;
        if (px * dx + py * dy < 0) { px = -px; py = -py; }
        p->vx = px; p->vy = py;
        p->state = P_DODGE; p->timer = 55; p->act = 6 + rnd_in(9); p->spd = 0;
        p->a += clampf(wrapf(atan2f_(-dy, -dx) - p->a), -0.6f, 0.6f);
        return;
    }
}

static void avoid_cars(Person *p) {
    for (int i = 0; i < CARS; i++) {
        Car *c = &cars[i];
        if (!c->mode) continue;
        float dx = p->x - c->x, dy = p->y - c->y, d2 = dx * dx + dy * dy;
        if (d2 > 1.1f * 1.1f || d2 < 1e-4f) continue;
        float d = sqrtf_(d2), o = (1.1f - d) * 0.25f;
        float nx = p->x + dx / d * o, ny = p->y + dy / d * o;
        if (!body_solid(nx, ny)) { p->x = nx; p->y = ny; }
    }
}

static void move_people(void) {
    for (int i = 0; i < PEOPLE; i++) {
        Person *p = &people[i];
        if (p->dead) {
            p->pose = PO_DEAD;
            if (p->state == P_DOWN && p->x > -999) move_free(p, 0.9f);
            if (--p->dead == 0 || (p->dead < 600 && !in_view(p->x, p->y))) spawn_person(p);
            continue;
        }
        float d2 = dist2_me(p->x, p->y);
        if (i != job_pi && (d2 > 95 * 95 || (d2 > 55 * 55 && !in_view(p->x, p->y)))) { spawn_person(p); continue; }
        int calm = p->state == P_WALK || p->state == P_WAIT || p->state == P_IDLE || p->state == P_FOLLOW || p->state == P_WAITX;
        if (p->state != P_DOWN) watch_cars(p, calm);
        if (p->state == P_DOWN) continue;
        if (calm && mine < 0 && !caught) {
            float dx = p->x - foot_x, dy = p->y - foot_y, dd = dx * dx + dy * dy;
            if (dd < 0.34f * 0.34f && walking) {
                dd = sqrtf_(dd) + 1e-3f;
                float nx = p->x + dx / dd * 0.12f, ny = p->y + dy / dd * 0.12f;
                if (!body_solid(nx, ny)) { p->x = nx; p->y = ny; }
                p->state = P_ANGRY; p->timer = 90;
                calm = 0;
            }
        }
        if (calm) look_at_me(p); else p->hyaw *= 0.9f;
        if (p->state == P_WALK) {
            p->pose = PO_WALK;
            float tx, ty;
            if (!walk_target(p, &tx, &ty)) {
                if (++p->k >= edges[p->edge].n) next_walk(p);
                continue;
            }
            float dx = tx - p->x, dy = ty - p->y, d = sqrtf_(dx * dx + dy * dy);
            if (d < 0.35f) {
                if (++p->k >= edges[p->edge].n) next_walk(p);
                if (rnd_in(100) < 3) { p->state = P_WAIT; p->timer = 60 + rnd_in(200); }
                else if (rnd_in(100) < 2) start_idle(p);
                continue;
            }
            if (path_crosses_road(p, tx, ty) && !crossing_clear((p->x + tx) * 0.5f, (p->y + ty) * 0.5f, dx, dy, p)) {
                p->state = P_WAITX; p->timer = 900;
                face(p, tx, ty, 0.15f);
                continue;
            }
            float want = atan2f_(dy, dx);
            p->a += clampf(wrapf(want - p->a), -0.12f, 0.12f);
            float sp = p->sp * (absf(wrapf(want - p->a)) > 1.2f ? 0.3f : 1);
            float nx = p->x + cosf_(p->a) * sp, ny = p->y + sinf_(p->a) * sp;
            if (!body_solid(nx, ny)) { p->x = nx; p->y = ny; }
            else if (++p->k >= edges[p->edge].n) next_walk(p);
            p->phase += sp * 11.6f;
            avoid_cars(p);
        } else if (p->state == P_WAITX) {
            p->pose = PO_IDLE;
            float tx, ty;
            if ((tick + i) % 8 == 0 && walk_target(p, &tx, &ty)) {
                if (--p->timer < 0 || crossing_clear((p->x + tx) * 0.5f, (p->y + ty) * 0.5f, tx - p->x, ty - p->y, p)) p->state = P_WALK;
                else p->timer -= 7;
            }
        } else if (p->state == P_WAIT) {
            p->pose = PO_IDLE;
            if (--p->timer <= 0) p->state = P_WALK;
        } else if (p->state == P_IDLE) {
            p->pose = p->act == 1 ? PO_PHONE : p->act == 2 ? PO_TEXT : p->act == 3 ? PO_CHAT : PO_IDLE;
            p->phase += 0.02f;
            if (--p->timer <= 0) { p->state = P_WALK; p->act = 0; }
        } else if (p->state == P_FOLLOW) {
            Person *l = p->buddy >= 0 ? &people[p->buddy] : 0;
            if (!l || l->dead || (l->state != P_WALK && l->state != P_WAIT && l->state != P_WAITX)) { attach_person(p); continue; }
            float tx = l->x - sinf_(l->a) * 0.5f, ty = l->y + cosf_(l->a) * 0.5f;
            float dx = tx - p->x, dy = ty - p->y, d = sqrtf_(dx * dx + dy * dy);
            if (d > 6) { attach_person(p); continue; }
            if (d > 0.08f) {
                float sp = minf(d * 0.2f, l->sp * 1.4f);
                float nx = p->x + dx / d * sp, ny = p->y + dy / d * sp;
                if (!body_solid(nx, ny)) { p->x = nx; p->y = ny; }
                p->phase += sp * 11.6f;
                p->pose = PO_WALK;
            } else p->pose = l->state == P_WALK ? PO_WALK : PO_IDLE;
            if (l->state == P_WALK) p->phase = l->phase + 0.4f;
            p->a += clampf(wrapf(l->a - p->a), -0.1f, 0.1f);
            avoid_cars(p);
        } else if (p->state == P_FREEZE) {
#ifndef POSETEST
            p->pose = PO_SHOCK;
#endif
            p->phase = 0;
            if (--p->timer <= 0) attach_person(p);
        } else if (p->state == P_DODGE) {
            if (p->act > 0) { p->act--; p->pose = PO_SHOCK; continue; }
            p->pose = PO_RUN;
            run_move(p, p->timer > 18 ? 0.036f : 0.012f, 0.004f);
            if (--p->timer <= 0) attach_person(p);
        } else if (p->state == P_FLEE) {
            p->pose = PO_RUN;
            flee_steer(p);
            run_move(p, 0.026f + (p->look >> 3 & 7) * 0.0012f, 0.0011f);
            avoid_cars(p);
            if (--p->timer <= 0) attach_person(p);
        } else if (p->state == P_COWER) {
            p->pose = PO_COWER;
            p->phase += 0.3f;
            if (--p->timer <= 0) { p->state = P_FLEE; p->timer = 200; float dx = p->x - me_x, dy = p->y - me_y, d = sqrtf_(dx * dx + dy * dy) + 1e-3f; p->vx = dx / d; p->vy = dy / d; p->spd = 0; }
        } else if (p->state == P_HANDS) {
            p->pose = PO_HANDSUP;
            face(p, me_x, me_y, 0.1f);
            p->phase += 0.05f;
            if (--p->timer <= 0) { p->state = P_FLEE; p->timer = 220; float dx = p->x - me_x, dy = p->y - me_y, d = sqrtf_(dx * dx + dy * dy) + 1e-3f; p->vx = dx / d; p->vy = dy / d; p->spd = 0; }
        } else if (p->state == P_ANGRY) {
            p->pose = PO_ANGRY;
            face(p, me_x, me_y, 0.15f);
            p->phase += 0.25f;
            if (--p->timer <= 0) attach_person(p);
        } else if (p->state == P_FIGHT) {
            if (mine >= 0) { p->state = P_FLEE; p->timer = 200; continue; }
            float dx = foot_x - p->x, dy = foot_y - p->y, d = sqrtf_(dx * dx + dy * dy) + 1e-3f;
            face(p, foot_x, foot_y, 0.2f);
            if (p->punch > 0) { p->punch--; p->pose = PO_PUNCH; if (p->punch == 6 && d < 0.75f) { hurt_me(4); shake = maxi(shake, 4); } }
            else if (d > 0.55f) {
                float sp = 0.028f, nx = p->x + dx / d * sp, ny = p->y + dy / d * sp;
                if (!body_solid(nx, ny)) { p->x = nx; p->y = ny; }
                p->phase += sp * 4.9f;
                p->pose = PO_RUN;
            } else { p->pose = PO_ANGRY; p->phase += 0.2f; if (rnd_in(100) < 4) p->punch = 18; }
            if (--p->timer <= 0) { p->state = P_FLEE; p->timer = 240; p->vx = -dx / d; p->vy = -dy / d; p->spd = 0.02f; }
        }
    }
}

static void push_car(Car *c, float dx, float dy) {
    float hx, hy;
    if (!corner_hit(c->x + dx, c->y + dy, c->a, &hx, &hy)) { c->x += dx; c->y += dy; }
    else if (!corner_hit(c->x + dx * 0.5f, c->y + dy * 0.5f, c->a, &hx, &hy)) { c->x += dx * 0.5f; c->y += dy * 0.5f; }
}

static void bump_pair(Car *a, Car *b) {
    const float HX = 0.9f, HY = 0.46f;
    float ax[2][2] = {{cosf_(a->a), sinf_(a->a)}, {-sinf_(a->a), cosf_(a->a)}};
    float bx[2][2] = {{cosf_(b->a), sinf_(b->a)}, {-sinf_(b->a), cosf_(b->a)}};
    float dx = b->x - a->x, dy = b->y - a->y, best = 1e9f, nx = 0, ny = 0;
    for (int i = 0; i < 4; i++) {
        float *n = i < 2 ? ax[i] : bx[i - 2];
        float ra = HX * absf(ax[0][0] * n[0] + ax[0][1] * n[1]) + HY * absf(ax[1][0] * n[0] + ax[1][1] * n[1]);
        float rb = HX * absf(bx[0][0] * n[0] + bx[0][1] * n[1]) + HY * absf(bx[1][0] * n[0] + bx[1][1] * n[1]);
        float d = dx * n[0] + dy * n[1], o = ra + rb - absf(d);
        if (o <= 0) return;
        if (o < best) { best = o; nx = d < 0 ? -n[0] : n[0]; ny = d < 0 ? -n[1] : n[1]; }
    }
    float ma = k_mass[a->kind], mb = k_mass[b->kind];
    float hx, hy;
    int fa = !corner_hit(a->x - nx * best, a->y - ny * best, a->a, &hx, &hy);
    int fb = !corner_hit(b->x + nx * best, b->y + ny * best, b->a, &hx, &hy);
    float ka = fa && fb ? mb / (ma + mb) : fa ? 1.0f : 0.0f, kb = fa && fb ? ma / (ma + mb) : fb ? 1.0f : 0.0f;
    if (ka > 0) push_car(a, -nx * best * ka, -ny * best * ka);
    if (kb > 0) push_car(b, nx * best * kb, ny * best * kb);
    float px = 0, py = 0, bd = 1e9f;
    for (int q = 0; q < 4; q++) {
        float sx = (q & 1) ? HX : -HX, sy = (q & 2) ? HY : -HY;
        float cx = b->x + bx[0][0] * sx + bx[1][0] * sy, cy = b->y + bx[0][1] * sx + bx[1][1] * sy;
        float t = (cx - a->x) * nx + (cy - a->y) * ny;
        if (t < bd) { bd = t; px = cx; py = cy; }
    }
    float rax = px - a->x, ray = py - a->y, rbx = px - b->x, rby = py - b->y;
    float vax = a->vx - a->r * ray, vay = a->vy + a->r * rax;
    float vbx = b->vx - b->r * rby, vby = b->vy + b->r * rbx;
    float vn = (vbx - vax) * nx + (vby - vay) * ny;
    if (vn >= 0) return;
    float rna = rax * ny - ray * nx, rnb = rbx * ny - rby * nx;
    float jn = -1.3f * vn / (1 / ma + 1 / mb + rna * rna / (YAW_I * ma) + rnb * rnb / (YAW_I * mb));
    if (jn > 0.02f) { damage_car(a, jn * 1100 / ma); damage_car(b, jn * 1100 / mb); }
    a->vx -= nx * jn / ma; a->vy -= ny * jn / ma; a->r -= rna * jn / (YAW_I * ma);
    b->vx += nx * jn / mb; b->vy += ny * jn / mb; b->r += rnb * jn / (YAW_I * mb);
}

static void bump_cars(void) {
    for (int i = 0; i < CARS; i++) {
        Car *a = &cars[i];
        if (!a->mode) continue;
        for (int j = i + 1; j < CARS; j++) {
            Car *b = &cars[j];
            if (!b->mode) continue;
            float dx = b->x - a->x, dy = b->y - a->y;
            if (dx * dx + dy * dy < 4.2f && a->kind != K_BOAT && b->kind != K_BOAT) bump_pair(a, b);
        }
    }
}


static int boat_land(float x, float y) {
    if (x < 1 || y < 1 || x > WX - 1 || y > WY - 1) return 1;
    return (cell(x, y) & 3) != G_SEA;
}
static int boat_hit(float x, float y, float a) {
    float ca = cosf_(a), sa = sinf_(a);
    static const float bx[7] = {1.18f, 0.6f, 0.6f, -0.4f, -0.4f, -1.12f, -1.12f}, by[7] = {0, 0.42f, -0.42f, 0.5f, -0.5f, 0.46f, -0.46f};
    for (int i = 0; i < 7; i++)
        if (boat_land(x + bx[i] * ca - by[i] * sa, y + bx[i] * sa + by[i] * ca)) return 1;
    return 0;
}

static void drive_boat(Car *c) {
    float ca = cosf_(c->a), sa = sinf_(c->a);
    float u = c->vx * ca + c->vy * sa, v = c->vy * ca - c->vx * sa, top = 0.30f;
    if (c->gas > 0) u += 0.0011f * c->gas * maxf(0, 1 - (u / top) * (u / top));
    if (c->brk > 0) u -= (u > 0.01f ? 0.0017f : 0.0006f) * c->brk;
    if (u < -0.08f) u = -0.08f;
    u -= u * absf(u) * 0.0045f + u * 0.0028f;
    v *= 0.9f;
    float bite = clampf(absf(u) / 0.1f, 0.12f, 1.0f) * (u < 0 ? -1.0f : 1.0f);
    c->steer += (c->steer_in * 0.5f - c->steer) * 0.12f;
    c->r += (c->steer * 0.075f * bite - c->r) * 0.07f;
    c->pitch += (clampf(u * 0.28f, 0, 0.075f) + (c->gas > 0 && u < 0.18f ? 0.02f : 0) - c->pitch) * 0.05f;
    c->roll += (clampf(c->r * 2.2f, -0.14f, 0.14f) - c->roll) * 0.08f;
    float na = c->a + c->r, nc = cosf_(na), ns = sinf_(na);
    c->vx = u * nc - v * ns;
    c->vy = u * ns + v * nc;
    if (boat_hit(c->x + c->vx, c->y + c->vy, na)) {
        float sp = sqrtf_(speed2(c));
        if (sp > 0.07f) damage_car(c, sp * 700);
        c->vx *= -0.25f; c->vy *= -0.25f; c->r *= -0.3f;
        if (boat_hit(c->x + c->vx, c->y + c->vy, c->a)) c->vx = c->vy = 0;
        else { c->x += c->vx; c->y += c->vy; }
        return;
    }
    c->x += c->vx; c->y += c->vy; c->a = wrapf(na);
    if (absf(u) > 0.05f && (tick & 1) && dist2_me(c->x, c->y) < 60 * 60) {
        float sx = c->x - nc * 1.0f, sy = c->y - ns * 1.0f, k = absf(u) * 3;
        for (int s = -1; s <= 1; s += 2)
            emit(FX_SPLASH, sx - ns * 0.45f * s, sy + nc * 0.45f * s, SEA_Z + 0.08f, -ns * 0.02f * s * k - c->vx * 0.2f, nc * 0.02f * s * k - c->vy * 0.2f,
                 0.03f + 0.03f * k, 0.12f + 0.1f * k, 0.008f, 30 + rnd_in(20));
    }
}

static int water_clear(float x, float y, float a, float L) {
    for (float t = 2; t <= L; t += 1.0f)
        if (boat_land(x + cosf_(a) * t, y + sinf_(a) * t)) return 0;
    return 1;
}
static void spawn_boat(Car *c, int cruise);
static void ai_boat(Car *c, int i) {
    float d2 = dist2_me(c->x, c->y);
    if (d2 > 110 * 110 || (d2 > 70 * 70 && !in_view(c->x, c->y))) { spawn_boat(c, 1); return; }
    static const float tries[5] = {0, 0.5f, -0.5f, 1.1f, -1.1f};
    int pick = -1;
    for (int k = 0; k < 5 && pick < 0; k++)
        if (water_clear(c->x, c->y, c->a + tries[k] * ((i & 1) ? 1 : -1), k ? 7 : 10)) pick = k;
    c->rev = 0; c->keys = 0;
    if (pick < 0) { c->gas = 0; c->brk = 1; c->steer_in = 1; return; }
    float t = tries[pick] * ((i & 1) ? 1 : -1);
    c->steer_in = t > 0 ? 1 : t < 0 ? -1 : 0;
    c->gas = pick == 0 ? 0.55f : 0.3f; c->brk = 0;
}

static const u32 boat_cols[6] = {0xf4f4f0, 0x1a5aa8, 0xd82828, 0x101418, 0xe8c020, 0x1e9a8a};
static void spawn_boat(Car *c, int cruise) {
    c->mode = M_NONE;
    for (int t = 0; t < 40; t++) {
        float a = rndf() * TAU, r = (cruise ? 35 : 20) + rndf() * 60;
        float x = me_x + cosf_(a) * r, y = me_y + sinf_(a) * r;
        if (boat_land(x, y) || (in_view(x, y) && r < 50)) continue;
        float nx = 0, ny = 0;
        int land = 0, near = 0;
        for (int k = 0; k < 12; k++) {
            float ka = k * TAU / 12, dx = cosf_(ka), dy = sinf_(ka);
            float lx = x + dx * (cruise ? 5.0f : 1.7f), ly = y + dy * (cruise ? 5.0f : 1.7f);
            if (boat_land(lx, ly)) { land++; nx += dx; ny += dy; if (!cruise && (cell(lx, ly) & 3) == G_SAND) near += 9; }
            if (boat_land(x + dx * 0.8f, y + dy * 0.8f)) near++;
        }
        if (cruise ? land > 0 : (land == 0 || near > 0)) continue;
        float h = cruise ? rndf() * TAU : atan2f_(ny, nx) + PI / 2;
        if (boat_hit(x, y, h)) continue;
        int n = 0;
        for (int i = 0; i < CARS; i++) if (&cars[i] != c && cars[i].mode && dist2_me(cars[i].x, cars[i].y) >= 0 && (cars[i].x - x) * (cars[i].x - x) + (cars[i].y - y) * (cars[i].y - y) < 9) n++;
        if (n) continue;
        reset_car(c);
        c->kind = K_BOAT; c->cop = 0; c->x = x; c->y = y; c->a = h;
        c->paint = boat_cols[rnd_in(6)];
        c->mode = cruise ? M_TRAFFIC : M_PARKED;
        return;
    }
}

static void reset_game(void);
// wasted wakes you at the hospital busted at the cops
static void respawn(void) {
    int how = caught_how;
    int fee = how ? 100 + money / 10 : 200 + money / 8;
    if (fee > money) fee = money < 0 ? 0 : money;
    reset_game();
    money += money / 4;
    money -= fee;
    float sx = how ? 2312 : 236, sy = how ? 772 : 797;
    foot_x = sx; foot_y = sy;
    for (int r = 0; r < 40; r++) {
        int done = 0;
        for (int k = 0; k < 16 && !done; k++) {
            float x = sx + cosf_(k * TAU / 16) * r, y = sy + sinf_(k * TAU / 16) * r;
            if (ground_kind(x, y) == 2 && !near_solid(x, y, 0.45f)) { foot_x = x; foot_y = y; done = 1; }
        }
        if (done) break;
    }
    cars[0].mode = M_NONE;
    mine = -1;
    foot_a = cam_yaw_s; look_yaw = 0;
    if (!how) { ammo[1] = ammo[2] = 0; weapon = 0; }
    else if (!ammo[1] && !ammo[2]) { ammo[1] = 30; weapon = 1; }
    say(how ? "HOSPITAL BILL $" : "BAIL $", 0, fee);
}

static void reset_game(void) {
    wanted = bust = heat = 0;
    my_hp = 100; my_armour = 0; shake = 0; aim_t = 0; foot_sink = 0;
    money -= money / 4;
    for (int i = 1; i < TRAF0; i++) cars[i].mode = M_NONE;
    Car *c = &cars[0];
    reset_car(c);
    c->x = start_x; c->y = start_y; c->a = start_a;
    c->mode = M_MINE;
    c->kind = K_SPORT;
    c->paint = 0x1ec8d0;
    mine = 0;
    cam_yaw_s = start_a;
}

static int clear_line(float x0, float y0, float x1, float y1) {
    float dx = x1 - x0, dy = y1 - y0, L = sqrtf_(dx * dx + dy * dy);
    int n = (int)(L / 0.6f);
    for (int i = 1; i < n; i++) {
        float t = (float)i / n;
        if (cell(x0 + dx * t, y0 + dy * t) & C_BLD) return 0;
    }
    return 1;
}

static void explode(Car *c, int idx) {
    c->wreck = 1; c->burn = 900; c->hp = 0;
    c->paint = 0x2a2624;
    c->vx += (rndf() - 0.5f) * 0.06f; c->vy += (rndf() - 0.5f) * 0.06f; c->r += (rndf() - 0.5f) * 0.1f;
    if (c->mode == M_TRAFFIC || c->mode == M_COP) c->mode = M_PARKED;
    if (c->cop && wanted < 5) { wanted++; heat = 0; }
    emit(FX_FLASH, c->x, c->y, 0.9f, 0, 0, 0, 2.2f, 0.35f, 14);
    for (int i = 0; i < 18; i++) {
        float a = rndf() * TAU, sp = 0.01f + rndf() * 0.05f;
        emit(FX_FIRE, c->x + cosf_(a) * 0.3f, c->y + sinf_(a) * 0.3f, 0.4f + rndf() * 0.5f, cosf_(a) * sp, sinf_(a) * sp, 0.02f + rndf() * 0.05f, 0.6f + rndf() * 0.6f, 0.025f, 25 + rnd_in(25));
    }
    for (int i = 0; i < 16; i++) {
        float a = rndf() * TAU, sp = rndf() * 0.02f;
        emit(FX_BLACK, c->x + cosf_(a) * 0.5f, c->y + sinf_(a) * 0.5f, 0.8f + rndf(), cosf_(a) * sp, sinf_(a) * sp, 0.015f + rndf() * 0.02f, 0.7f + rndf() * 0.4f, 0.012f, 110 + rnd_in(100));
    }
    for (int i = 0; i < 14; i++) {
        float a = rndf() * TAU, sp = 0.05f + rndf() * 0.1f;
        emit(FX_SPARK, c->x, c->y, 0.6f, cosf_(a) * sp, sinf_(a) * sp, 0.04f + rndf() * 0.12f, 0.07f, 0, 30 + rnd_in(30));
    }
    float R = 4.5f;
    for (int i = 0; i < PEOPLE; i++) {
        Person *p = &people[i];
        if (p->dead) continue;
        float dx = p->x - c->x, dy = p->y - c->y, d = sqrtf_(dx * dx + dy * dy) + 1e-3f;
        if (d > R) continue;
        p->state = P_DOWN; p->dead = 900;
        p->vx = dx / d * 0.25f * (1 - d / R); p->vy = dy / d * 0.25f * (1 - d / R);
        p->a = atan2f_(dy, dx);
    }
    panic(c->x, c->y, 20);
    for (int j = 0; j < CARS; j++) {
        Car *o = &cars[j];
        if (o == c || !o->mode) continue;
        float dx = o->x - c->x, dy = o->y - c->y, d = sqrtf_(dx * dx + dy * dy) + 1e-3f;
        if (d > R) continue;
        float k = 1 - d / R;
        damage_car(o, 700 * k);
        o->vx += dx / d * 0.12f * k / k_mass[o->kind]; o->vy += dy / d * 0.12f * k / k_mass[o->kind];
        o->r += (rndf() - 0.5f) * 0.12f * k;
    }
    if (idx == mine) hurt_me(1000);
    else if (mine < 0) {
        float d = sqrtf_(dist2_me(c->x, c->y));
        if (d < R) {
            hurt_me(30 + 90 * (1 - d / R));
            float nx = foot_x + (foot_x - c->x) / (d + 0.01f) * 0.6f * (1 - d / R), ny = foot_y + (foot_y - c->y) / (d + 0.01f) * 0.6f * (1 - d / R);
            if (!body_solid(nx, ny)) { foot_x = nx; foot_y = ny; }
        }
    }
    float dc = sqrtf_((c->x - cam.x) * (c->x - cam.x) + (c->y - cam.y) * (c->y - cam.y));
    shake = maxi(shake, (int)(36 * clampf(1 - dc / 45, 0, 1)));
}

static void car_state(Car *c, int i) {
    float ca = cosf_(c->a), sa = sinf_(c->a);
    float hx = c->x + ca * 0.62f, hy = c->y + sa * 0.62f;
    if (c->wreck) {
        if (c->burn > 0) {
            c->burn--;
            if ((tick & 7) == 0) emit(FX_BLACK, c->x + (rndf() - 0.5f) * 0.6f, c->y + (rndf() - 0.5f) * 0.6f, 0.7f, 0, 0, 0.02f, 0.3f, 0.01f, 130);
            if (c->burn > 400 && (tick & 3) == 0) emit(FX_FIRE, c->x + (rndf() - 0.5f) * 1.2f, c->y + (rndf() - 0.5f) * 0.6f, 0.45f, 0, 0, 0.02f, 0.35f, 0.01f, 22);
        }
        return;
    }
    if (c->hp < 120) {
        c->hp -= 0.45f;
        if (tick & 1) emit(FX_FIRE, hx + (rndf() - 0.5f) * 0.4f, hy + (rndf() - 0.5f) * 0.4f, 0.5f, c->vx * 0.5f, c->vy * 0.5f, 0.02f + rndf() * 0.02f, 0.3f, 0.012f, 20 + rnd_in(10));
        if ((tick & 3) == 0) emit(FX_BLACK, hx, hy, 0.7f, c->vx * 0.3f, c->vy * 0.3f, 0.02f, 0.2f, 0.009f, 90);
    } else if (c->hp < 250) {
        if ((tick & 3) == 0) emit(FX_BLACK, hx, hy, 0.55f, c->vx * 0.3f, c->vy * 0.3f, 0.016f, 0.16f, 0.008f, 80);
    } else if (c->hp < 450) {
        if ((tick % 6) == 0) emit(FX_SMOKE, hx, hy, 0.55f, c->vx * 0.3f, c->vy * 0.3f, 0.014f, 0.14f, 0.007f, 70);
    }
    if (c->hp <= 0) explode(c, i);
}

static void update_fx(void) {
    for (int i = 0; i < FXN; i++) {
        Fx *f = &fx[i];
        if (f->life <= 0) continue;
        f->life--;
        f->x += f->vx; f->y += f->vy; f->z += f->vz;
        f->size += f->grow;
        if (f->type == FX_SPLASH) { f->vz -= 0.005f; if (f->z < SEA_Z) f->life = 0; }
        else if (f->type == FX_SPARK) { f->vz -= 0.006f; if (f->z < 0.05f) { f->z = 0.05f; f->vz *= -0.3f; f->vx *= 0.6f; f->vy *= 0.6f; } }
        else { f->vx *= 0.97f; f->vy *= 0.97f; if (f->type != FX_FLASH) f->vz *= 0.985f; }
    }
    for (int i = 0; i < 16; i++) if (tracers[i].life > 0) tracers[i].life--;
}

static void shoot(void) {
    float fa = foot_a + shot_spread, ca = cosf_(fa), sa = sinf_(fa);
    float gx = foot_x + ca * 0.3f + sa * 0.17f, gy = foot_y + sa * 0.3f - ca * 0.17f, gz = 0.6f;
    int tp = -1, tc = -1;
    float best = 1e9f;
    for (int i = 0; i < PEOPLE; i++) {
        Person *p = &people[i];
        if (p->dead) continue;
        float dx = p->x - foot_x, dy = p->y - foot_y, d = sqrtf_(dx * dx + dy * dy);
        if (d > 28 || d < 0.2f) continue;
        float ang = absf(wrapf(atan2f_(dy, dx) - fa));
        if (ang > 0.35f) continue;
        float sc = d * (1 + ang * 4);
        if (sc < best && clear_line(foot_x, foot_y, p->x, p->y)) { best = sc; tp = i; tc = -1; }
    }
    for (int i = 0; i < CARS; i++) {
        Car *c = &cars[i];
        if (!c->mode || c->wreck) continue;
        float dx = c->x - foot_x, dy = c->y - foot_y, d = sqrtf_(dx * dx + dy * dy);
        if (d > 28 || d < 0.3f) continue;
        float ang = absf(wrapf(atan2f_(dy, dx) - fa));
        if (ang > 0.3f) continue;
        float sc = d * (1 + ang * 4) + 1;
        if (sc < best && clear_line(foot_x, foot_y, c->x, c->y)) { best = sc; tc = i; tp = -1; }
    }
    float ex, ey, ez = 0.55f;
    if (tp >= 0) {
        Person *p = &people[tp];
        ex = p->x; ey = p->y;
        float dx = p->x - foot_x, dy = p->y - foot_y, d = sqrtf_(dx * dx + dy * dy) + 1e-3f;
        p->state = P_DOWN; p->dead = 900;
        p->vx = dx / d * 0.05f; p->vy = dy / d * 0.05f; p->a = atan2f_(dy, dx);
        if (wanted < 2) wanted++;
        heat = 0;
    } else if (tc >= 0) {
        Car *c = &cars[tc];
        ex = c->x; ey = c->y; ez = 0.45f;
        damage_car(c, 60);
        emit(FX_SPARK, ex, ey, 0.5f, (rndf() - 0.5f) * 0.05f, (rndf() - 0.5f) * 0.05f, 0.05f, 0.05f, 0, 15);
        if (c->cop) { if (wanted < 3) wanted++; heat = 0; }
    } else {
        float t = 0;
        ex = gx; ey = gy;
        while (t < 30) { t += 0.4f; ex = gx + ca * t; ey = gy + sa * t; if (solid(ex, ey)) break; }
        emit(FX_SPARK, ex, ey, ez, -ca * 0.03f, -sa * 0.03f, 0.04f, 0.05f, 0, 12);
    }
    tracer(gx, gy, gz, ex, ey, ez);
    emit(FX_FLASH, gx + ca * 0.12f, gy + sa * 0.12f, gz, 0, 0, 0, 0.22f, 0, 3);
    gun_panic(foot_x, foot_y);
    aim_t = 40;
}

static void punch(void) {
    punch_t = 18;
    for (int i = 0; i < PEOPLE; i++) {
        Person *p = &people[i];
        if (p->dead) continue;
        float dx = p->x - foot_x, dy = p->y - foot_y, d = sqrtf_(dx * dx + dy * dy);
        if (d > 0.95f || absf(wrapf(atan2f_(dy, dx) - foot_a)) > 0.8f) continue;
        d += 1e-3f;
        p->state = P_DOWN; p->dead = 900; p->vx = dx / d * 0.035f; p->vy = dy / d * 0.035f; p->a = atan2f_(-dy, -dx);
        panic(p->x, p->y, 6);
        for (int k = 1; k < TRAF0; k++)
            if (cars[k].mode == M_COP && dist2_me(cars[k].x, cars[k].y) < 25 * 25 && wanted < 1) { wanted = 1; heat = 0; }
        return;
    }
}

static void cop_fire(Car *c) {
    float dx = me_x - c->x, dy = me_y - c->y, d = sqrtf_(dx * dx + dy * dy) + 1e-3f;
    float ca = cosf_(c->a), sa = sinf_(c->a);
    float gx = c->x + sa * 0.45f, gy = c->y - ca * 0.45f;
    float chance = 0.5f - (me_vx * me_vx + me_vy * me_vy > 0.01f ? 0.25f : 0) - d * 0.01f;
    float ex = me_x, ey = me_y, ez = 0.5f;
    if (rndf() < chance) {
        if (mine >= 0) { damage_car(&cars[mine], 14); if (rnd_in(10) == 0) hurt_me(4); }
        else hurt_me(7);
    } else { ex += (rndf() - 0.5f) * 2.5f; ey += (rndf() - 0.5f) * 2.5f; ez = 0.2f + rndf(); }
    tracer(gx, gy, 0.6f, ex, ey, ez);
    emit(FX_FLASH, gx + dx / d * 0.2f, gy + dy / d * 0.2f, 0.62f, 0, 0, 0, 0.2f, 0, 3);
}

static void spawn_pick(Pickup *k) {
    for (int t = 0; t < 10; t++) {
        float a = rndf() * TAU, r = 15 + rndf() * 40;
        float x = me_x + cosf_(a) * r, y = me_y + sinf_(a) * r;
        if (ground_kind(x, y) != 2 || in_view(x, y) || near_pole(x, y, 1.0f)) continue;
        k->x = x; k->y = y; k->on = 1; k->kind = rnd_in(3);
        return;
    }
    k->on = 0;
}

static void update_picks(void) {
    for (int i = 0; i < PICKS; i++) {
        Pickup *k = &picks[i];
        if (!k->on) { if ((tick & 127) == i * 7) spawn_pick(k); continue; }
        float d2 = dist2_me(k->x, k->y);
        if (d2 > 95 * 95 && !in_view(k->x, k->y)) { k->on = 0; continue; }
        if (d2 < 1.1f) {
            if (k->kind == 0 && my_hp < 100) { my_hp = minf(100, my_hp + 40); k->on = 0; }
            if (k->kind == 1 && my_armour < 100) { my_armour = minf(100, my_armour + 50); k->on = 0; }
            if (k->kind == 2) { ammo[1] += 24; ammo[2] += 90; if (weapon == 0) weapon = 2; k->on = 0; say("SMG AND AMMO", 0, -1); }
        }
    }
}

static void police(void) {
    int want = mini(wanted, 4), out = 0, cuffed = 0, spotted = 0;
    // cant see you so they search your last spot
    if (wanted && !seen) want = maxi(1, want - 1);
    heat++;
    for (int i = 1; i < TRAF0; i++) {
        Car *c = &cars[i];
        if (!c->mode) {
            if (out < want && (tick & 15) == 0) { spawn_cop(c); if (c->mode) out++; }
            continue;
        }
        float d2 = dist2_me(c->x, c->y);
        if (c->mode != M_COP) { if (d2 > 3600 && c->mode != M_MINE && !in_view(c->x, c->y)) c->mode = M_NONE; continue; }
        if (!wanted) { c->mode = M_PARKED; c->gas = 0; c->brk = 1; c->steer_in = 0; continue; }
        out++;
        if (d2 > 4900 && !in_view(c->x, c->y)) { c->mode = M_NONE; continue; }
        if (d2 < 10 * 10 || (d2 < 32 * 32 && ((tick + i) & 7) == 0 && clear_line(c->x, c->y, me_x, me_y)) || (d2 < 32 * 32 && seen && ((tick + i) & 7) != 0)) spotted = 1;
        if (d2 < 2.56f && me_vx * me_vx + me_vy * me_vy < 0.0012f) cuffed = 1;
        if (wanted >= 3 && d2 < 18 * 18 && c->timer >= 0 && --c->gun <= 0) {
            c->gun = 35 + rnd_in(45);
            if (clear_line(c->x, c->y, me_x, me_y)) cop_fire(c);
        }
    }
    bust = cuffed ? bust + 1 : 0;
    if (bust >= 150) { caught = 200; caught_how = 0; return; }
    // remember where they last saw you
    if (spotted || !wanted) { seen = 1; seen_x = me_x; seen_y = me_y; heat = 0; }
    else seen = 0;
    // 10 s plus 4 s a star then they give up
    if (wanted && heat >= 600 + wanted * 240) { heat = 0; wanted = 0; }
    if (hit_cd) hit_cd--;
    if (mine < 0 && !hit_cd)
        for (int i = 0; i < CARS; i++) {
            Car *c = &cars[i];
            if (!c->mode || dist2_me(c->x, c->y) > 0.5f || speed2(c) < 0.0016f) continue;
            float l = sqrtf_(speed2(c));
            hurt_me(l * 650);
            float nx = foot_x + c->vx / l * 0.8f, ny = foot_y + c->vy / l * 0.8f;
            if (!body_solid(nx, ny)) { foot_x = nx; foot_y = ny; }
            hit_cd = 40;
            break;
        }
}

static Person *free_person(void) {
    for (int i = 0; i < PEOPLE; i++) if (people[i].dead && people[i].dead < 600) return &people[i];
    for (int i = 0; i < PEOPLE; i++) if (!in_view(people[i].x, people[i].y)) return &people[i];
    return &people[tick % PEOPLE];
}

static void enter_exit(void) {
    if (mine >= 0) {
        Car *c = &cars[mine];
        if (speed2(c) > 0.003f) return;
        if (c->kind == K_BOAT) {
            float bx = 0, by = 0, bd = 1e9f;
            for (int r = 1; r <= 7; r++)
                for (int k = 0; k < 16; k++) {
                    float x = c->x + cosf_(k * TAU / 16) * r * 0.5f, y = c->y + sinf_(k * TAU / 16) * r * 0.5f;
                    int g = ground_kind(x, y);
                    if (g == 0 || g == 3 || body_solid(x, y)) continue;
                    float d = r * 0.5f;
                    if (d < bd) { bd = d; bx = x; by = y; }
                }
            if (bd > 9) { say("GET CLOSER TO LAND", 0, -1); return; }
            c->mode = M_PARKED;
            c->keys = 0; c->gas = 0; c->brk = 1; c->steer_in = 0;
            foot_x = bx; foot_y = by; foot_a = c->a;
            mine = -1;
            return;
        }
        c->mode = M_PARKED;
        c->keys = 0; c->gas = 0; c->brk = 1; c->steer_in = 0;
        float sx = -sinf_(c->a), sy = cosf_(c->a);
        foot_x = c->x + sx; foot_y = c->y + sy;
        if (body_solid(foot_x, foot_y)) { foot_x = c->x - sx; foot_y = c->y - sy; }
        foot_a = c->a;
        mine = -1;
        return;
    }
    int best = -1;
    float bd = 4;
    for (int i = 0; i < CARS; i++) {
        if (!cars[i].mode || cars[i].wreck) continue;
        float d = dist2_me(cars[i].x, cars[i].y) - (cars[i].kind == K_BOAT ? 1.5f : 0);
        if (d < bd) { bd = d; best = i; }
    }
    if (best < 0) return;
    Car *c = &cars[best];
    if (c->mode != M_PARKED && c->kind != K_BOAT) {
        Person *p = free_person();
        float sx = -sinf_(c->a), sy = cosf_(c->a);
        p->x = c->x + sx * 0.7f; p->y = c->y + sy * 0.7f;
        if (body_solid(p->x, p->y)) { p->x = c->x - sx * 0.7f; p->y = c->y - sy * 0.7f; }
        p->dead = 0; p->state = rnd_in(100) < (c->cop ? 70 : 35) ? P_FIGHT : P_FLEE; p->timer = p->state == P_FIGHT ? 420 : 260; p->punch = 0; p->hyaw = 0; p->buddy = -1;
        p->vx = (p->x - c->x) * 0.08f + cosf_(c->a) * 0.03f; p->vy = (p->y - c->y) * 0.08f + sinf_(c->a) * 0.03f; p->spd = 0.01f;
        p->shirt = c->cop ? 0x2a3a6a : shirts[rnd_in(8)];
        p->look = c->cop ? (rnd() & ~(1u | 7u << 11 | 3u << 14)) | 1u << 11 | 1u << 14 : rnd();
        p->sp = 0.016f; p->off = 1.3f;
        if (c->cop && wanted < 5) { wanted++; heat = 0; }
    }
    c->mode = M_MINE;
    c->keys = 0; c->gas = c->brk = 0;
    c->a = cam_yaw_s + wrapf(c->a - cam_yaw_s);
    mine = best;
}

static void splash(float x, float y, int n, float k) {
    for (int i = 0; i < n; i++) {
        float a = rndf() * TAU, sp = rndf() * 0.04f * k;
        emit(FX_SPLASH, x + cosf_(a) * 0.3f, y + sinf_(a) * 0.3f, SEA_Z + 0.1f, cosf_(a) * sp, sinf_(a) * sp, (0.04f + rndf() * 0.06f) * k, 0.12f + rndf() * 0.1f, 0.006f, 40 + rnd_in(30));
    }
}

static void water(void) {
    if (caught) return;
    if (mine >= 0) {
        Car *c = &cars[mine];
        if (c->kind == K_BOAT) return;
        if (ground_kind(c->x, c->y) != 0) return;
        if (c->sink == 0) splash(c->x, c->y, 40, 1 + sqrtf_(speed2(c)) * 6);
        c->sink += 0.008f;
        c->vx *= 0.93f; c->vy *= 0.93f; c->r *= 0.9f;
        if ((tick & 3) == 0) splash(c->x, c->y, 2, 0.4f);
        if (c->sink > 0.9f) { my_hp = 0; caught = 200; caught_how = 1; }
        return;
    }
    if (ground_kind(foot_x, foot_y) != 0) { foot_sink = 0; return; }
    if (foot_sink == 0) splash(foot_x, foot_y, 20, 0.8f);
    foot_sink += 0.006f;
    if ((tick & 7) == 0) splash(foot_x, foot_y, 2, 0.3f);
    if (foot_sink > 0.8f) { my_hp = 0; caught = 200; caught_how = 1; }
}

static void walk_me(void) {
    if (hold[3]) foot_a += 0.05f;
    if (hold[2]) foot_a -= 0.05f;
    float sp = hold[0] ? 0.031f : hold[1] ? -0.012f : 0;
    if (foot_sink > 0) sp *= 0.3f;
    walking = sp != 0;
    foot_phase += absf(sp) * (sp > 0.02f ? 4.9f : 11.6f);
    float nx = foot_x + cosf_(foot_a) * sp, ny = foot_y + sinf_(foot_a) * sp;
    sea_ok = 1;
    if (!body_solid(nx, foot_y)) foot_x = nx;
    if (!body_solid(foot_x, ny)) foot_y = ny;
    sea_ok = 0;
}

static void sync_me(void) {
    if (mine >= 0) { Car *c = &cars[mine]; me_x = c->x; me_y = c->y; me_a = c->a; me_vx = c->vx; me_vy = c->vy; }
    else { me_x = foot_x; me_y = foot_y; me_a = foot_a; me_vx = me_vy = 0; }
}

static void mouse_look(void) {
    if (mouse_dx || mouse_dy) mouse_idle = 0; else mouse_idle++;
    look_yaw = wrapf(look_yaw - mouse_dx * 0.0042f);
    look_pitch = clampf(look_pitch - mouse_dy * 0.003f, -0.35f, 0.65f);
    int moving = mine >= 0 ? speed2(&cars[mine]) > 0.0004f : walking;
    if (mouse_idle > 90 && moving) { look_yaw *= 0.94f; look_pitch *= 0.97f; }
    mouse_dx = mouse_dy = 0;
}

static void step(void) {
    for (int i = 0; i < 5; i++) if (hold[i]) hold[i]--;
    tick++;
    mouse_look();
#ifdef BURNTEST
    if (tick == 900) {
        Car *c = &cars[PARK0];
        reset_car(c);
        c->x = me_x + cosf_(me_a) * 7; c->y = me_y + sinf_(me_a) * 7; c->a = me_a + 0.4f;
        c->mode = M_PARKED; c->kind = K_SEDAN; c->paint = 0xff4f6a; c->hp = 300;
    }
    if (tick == 1080) cars[PARK0].hp = 119;
#endif
    sea_t += 0.0015f;
    tod += 1.0f / 3600;
    if (tod >= 24) tod -= 24;
    if (caught) { enter_tap = 0; if (--caught == 0) respawn(); }
    else {
        if (enter_tap) { enter_tap = 0; enter_exit(); }
        if (mine >= 0) {
            int k = 0;
            for (int i = 0; i < 5; i++) if (hold[i]) k |= 1 << i;
            Car *c = &cars[mine];
            c->keys = k;
            c->gas = (k & KEY_GAS) ? 1 : 0;
            c->brk = (k & KEY_BRAKE) ? 1 : 0;
            c->steer_in = ((k & KEY_LEFT) ? 1.0f : 0) - ((k & KEY_RIGHT) ? 1.0f : 0);
            c->rev = 1;
            if (c->wreck) { c->gas = c->brk = 0; c->steer_in = 0; }
        } else {
            walk_me();
            if (fire_cd) fire_cd--;
            if (fire_tap) fire_tap--;
            if (key_wpn) { int w = key_wpn - 1; if (w == 0 || ammo[w] > 0) weapon = w; key_wpn = 0; }
            if ((fire_held || fire_tap || mouse_left) && !fire_cd) {
                if (weapon == 0) { punch(); fire_cd = 26; }
                else if (ammo[weapon] > 0) {
                    shot_spread = weapon == 2 ? (rndf() - 0.5f) * 0.14f : 0;
                    shoot(); ammo[weapon]--; fire_cd = weapon == 2 ? 5 : 14;
                    if (!ammo[weapon]) weapon = ammo[1] ? 1 : 0;
                } else weapon = 0;
            }
        }
    }
    if (aim_t) aim_t--;
    if (punch_t) punch_t--;
    update_job();
    if (shake) shake--;
    for (int i = 0; i < CARS; i++) {
        Car *c = &cars[i];
        if (!c->mode) {
            if (i >= TRAF0 && (tick & 31) == (i & 31)) { if (i >= BOAT0) spawn_boat(c, i >= BOAT0 + 3); else if (i < PARK0) spawn_traffic(c); else spawn_parked(c); }
            continue;
        }
        if (c->mode == M_PARKED && i >= TRAF0) {
            float d2 = dist2_me(c->x, c->y);
            if (d2 > 95 * 95 || (d2 > 65 * 65 && !in_view(c->x, c->y))) { if (i >= BOAT0) spawn_boat(c, 0); else if (i < PARK0) spawn_traffic(c); else spawn_parked(c); continue; }
        }
        if (c->mode == M_TRAFFIC) { if (c->kind == K_BOAT) ai_boat(c, i); else ai_traffic(c); }
        if (c->mode == M_COP) ai_cop(c);
        if (c->mode == M_PARKED) { c->gas = 0; c->brk = 1; c->steer_in = 0; c->keys = 0; c->rev = 0; }
        if (c->mode) { drive(c); car_state(c, i); }
    }
    bump_cars();
    update_poles();
    update_fx();
    update_picks();
    water();
    sync_me();
    police();
    move_people();
    float target = me_a;
    if (mine >= 0) {
        Car *c = &cars[mine];
        if (speed2(c) > 0.0036f && fwd_speed(c) > 0) target = me_a + clampf(wrapf(atan2f_(c->vy, c->vx) - me_a), -0.9f, 0.9f) * 0.5f;
    }
    cam_yaw_s += wrapf(target - cam_yaw_s) * 0.08f;
}

static void find_start(void) {
    float tx = 2667, ty = 733, bd = 1e18f;
    int be = 0, bk_ = 0;
    for (int i = 0; i < n_edges; i++) {
        Edge *e = &edges[i];
        if (e->flags & 1 || e->cls < 3 || e->n < 2 || oneway(e)) continue;
        for (int k = 0; k + 1 < e->n; k++) {
            float *p = &ep[(e->first + k) * 2];
            float dx = p[2] - p[0], dy = p[3] - p[1];
            if (absf(dy) < absf(dx) * 2) continue;
            float d = (p[0] - tx) * (p[0] - tx) + (p[1] - ty) * (p[1] - ty);
            if (d < bd) { bd = d; be = i; bk_ = k; }
        }
    }
    Edge *e = &edges[be];
    float *p = &ep[(e->first + bk_) * 2];
    float dx = p[2] - p[0], dy = p[3] - p[1], L = sqrtf_(dx * dx + dy * dy) + 1e-6f;
    if (dy < 0) { dx = -dx; dy = -dy; }
    float lane = lane_off(e);
    start_x = p[0] + dy / L * lane;
    start_y = p[1] - dx / L * lane;
    start_a = atan2f_(dy, dx);
}

static void init_game(void) {
    load_world();
    make_signs();
    gps_init();
    find_start();
    reset_game();
    money = 0;
    sync_me();
    seed = 0x2545f491;
    for (int i = TRAF0; i < PARK0; i++) spawn_traffic(&cars[i]);
    for (int i = PARK0; i < CARS; i++) spawn_parked(&cars[i]);
    for (int i = 0; i < PEOPLE; i++) spawn_person(&people[i]);
#ifdef MOORTEST
    cars[0].mode = M_NONE; mine = -1; foot_x = MOORTEST; foot_y = MOORY; foot_a = 0.6f; cam_yaw_s = 0.6f; sync_me();
#endif
#ifdef BOATTEST
    for (int t = 0; t < 50 && !cars[BOAT0 + 3].mode; t++) spawn_boat(&cars[BOAT0 + 3], 1);
    if (cars[BOAT0 + 3].mode) { cars[0].mode = M_NONE; mine = BOAT0 + 3; cars[mine].mode = M_MINE; cam_yaw_s = cars[mine].a; sync_me(); }
#endif
#ifdef FARETEST
    cars[0].kind = K_TAXI; cars[0].paint = 0xf2c418;
#endif
#ifdef STARS
    wanted = STARS; seen_x = me_x; seen_y = me_y;
#endif
#ifdef HOUR
    tod = HOUR;
#endif
#ifdef POLETEST
    {
        int best = -1; float bd = 1e9f;
        for (int i = 0; i < n_poles; i++) {
            if (poles[i].kind) continue;
            float d = (poles[i].x - start_x) * (poles[i].x - start_x) + (poles[i].y - start_y) * (poles[i].y - start_y);
            if (d < bd) { bd = d; best = i; }
        }
        Car *c = &cars[0];
        c->x = poles[best].x - cosf_(start_a) * 7; c->y = poles[best].y - sinf_(start_a) * 7;
        sync_me();
    }
#endif
#ifdef WATERTEST
    {
        float x = start_x;
        while (x < start_x + 200 && ground_kind(x, start_y) != 0) x += 0.5f;
        Car *c = &cars[0];
        c->x = x - 7; c->y = start_y; c->a = 0;
        cam_yaw_s = 0;
        sync_me();
    }
#endif
#ifdef PORTRAIT
    {
        Car *c = &cars[0];
        c->x = start_x - cosf_(start_a) * 6; c->y = start_y - sinf_(start_a) * 6;
        mine = -1; c->mode = M_PARKED;
        foot_x = start_x; foot_y = start_y; foot_a = start_a + PI;
        for (int i = 0; i < 5; i++) {
            Person *p = &people[i];
            float o = (i - 2) * 0.45f + (i >= 2 ? 0.45f : 0);
            if (i == 2) o = -0.45f * 3;
            p->x = start_x - sinf_(start_a) * o; p->y = start_y + cosf_(start_a) * o;
            p->a = start_a + PI; p->state = P_WAIT; p->timer = 100000; p->dead = 0; p->phase = 0;
            p->look = (u32)(i * 2654435761u) ^ 0x5bd1e995u;
#ifdef POSETEST
            { static const int ps[5] = {PO_HANDSUP, PO_PHONE, PO_TEXT, PO_COWER, PO_PUNCH}; p->state = P_FREEZE; p->timer = 100000; p->look = (u32)i; p->pose = ps[i];
#ifdef ONEPOSE
              p->pose = ONEPOSE;
#endif
#ifdef SOLO
              p->x += 30; 
#endif
            }
#endif
            p->shirt = shirts[i];
        }
        sync_me();
    }
#endif
#ifdef CROWDTEST
    {
        Car *c = &cars[0];
        mine = -1; c->mode = M_PARKED;
        c->x = start_x - cosf_(start_a) * 4; c->y = start_y - sinf_(start_a) * 4;
        foot_x = start_x; foot_y = start_y; foot_a = start_a;
        for (int i = 0; i < 8; i++) {
            Person *p = &people[i];
            float f = 2.5f + (i % 4) * 0.9f, sd = (i < 4 ? -0.7f : 0.7f) + (i & 1) * 0.3f;
            p->x = start_x + cosf_(start_a) * f - sinf_(start_a) * sd;
            p->y = start_y + sinf_(start_a) * f + cosf_(start_a) * sd;
            p->a = start_a + PI + (i - 4) * 0.3f; p->dead = 0;
            p->look = rnd();
            if (i < 3) { p->state = P_IDLE; p->act = i + 1; p->timer = 100000; }
            else { p->state = P_WAIT; p->timer = 100000; }
        }
        sync_me();
    }
#endif
#ifdef PEDTEST
    for (int i = 0; i < 6; i++) {
        Person *p = &people[i];
        float d = 9 + (i >> 1) * 3.0f, sd = (i & 1) ? 0.3f : -0.5f;
        p->x = start_x + cosf_(start_a) * d - sinf_(start_a) * sd;
        p->y = start_y + sinf_(start_a) * d + cosf_(start_a) * sd;
        p->state = P_WAIT; p->timer = 100000; p->dead = 0;
    }
#endif
}
