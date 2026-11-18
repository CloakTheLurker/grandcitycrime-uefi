// taxi fares: pick someone up off the sidewalk, drive them to a real named building before the clock runs out
// and the gps line on the radar that shows the way

static u32 scale_col(u32 c, float k);
enum { J_NONE, J_PICKUP, J_RIDE };
static int job, job_t, job_fares, job_dest = -1, job_earned;
static float job_x, job_y, job_d;
static char job_msg[64];
static int job_msg_t, was_taxi;

// gps: shortest way along the roads, as a list of edges
static float *elen, *gdist;
static int *gprev, *route_e, route_n, gps_on, gps_t;
static u8 *gdone;
static float gps_x, gps_y;
// waypoint picked on the map, the gps follows it when there is no job
static int wp_on;
static float wp_x, wp_y;

static void say(const char *a, const char *b, int n) {
    int k = 0;
    for (; *a && k < 62; a++) job_msg[k++] = *a;
    if (b) for (; *b && k < 62; b++) job_msg[k++] = *b;
    if (n >= 0) {
        char d[12];
        int m = 0;
        do { d[m++] = (char)('0' + n % 10); n /= 10; } while (n && m < 11);
        while (m && k < 62) job_msg[k++] = d[--m];
    }
    job_msg[k] = 0;
    job_msg_t = 300;
}

static int nearest_node(float x, float y) {
    int best = 0;
    float bd = 1e18f;
    for (int i = 0; i < n_nodes; i++) {
        if (!nodes[i].n) continue;
        float dx = nodes[i].x - x, dy = nodes[i].y - y, d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static void gps_init(void) {
    elen = (float *)pool((usize)n_edges * 4);
    for (int i = 0; i < n_edges; i++) {
        Edge *e = &edges[i];
        float L = 0;
        for (int k = 0; k + 1 < e->n; k++) {
            float *p = &ep[(e->first + k) * 2];
            L += sqrtf_((p[2] - p[0]) * (p[2] - p[0]) + (p[3] - p[1]) * (p[3] - p[1]));
        }
        elen[i] = L + 0.5f;
    }
    gdist = (float *)pool((usize)n_nodes * 4);
    gprev = (int *)pool((usize)n_nodes * 4);
    gdone = (u8 *)pool((usize)n_nodes);
    route_e = (int *)pool((usize)n_nodes * 4);
}

static void plan_route(void) {
    int from = nearest_node(me_x, me_y), to = nearest_node(gps_x, gps_y);
    for (int i = 0; i < n_nodes; i++) { gdist[i] = 1e30f; gprev[i] = -1; gdone[i] = 0; }
    gdist[from] = 0;
    for (;;) {
        int u = -1;
        float bd = 1e29f;
        for (int i = 0; i < n_nodes; i++) if (!gdone[i] && gdist[i] < bd) { bd = gdist[i]; u = i; }
        if (u < 0 || u == to) break;
        gdone[u] = 1;
        Node *nd = &nodes[u];
        for (int k = 0; k < nd->n; k++) {
            int id = adj[nd->first + k];
            Edge *e = &edges[id];
            if ((e->flags & 1) && e->cls > 1) continue;
            int v = e->a == u ? e->b : e->a;
            if (oneway(e) && e->a != u && mine >= 0) continue;
            float d = gdist[u] + elen[id] * (e->cls <= 2 ? 0.85f : 1.0f);
            if (d < gdist[v]) { gdist[v] = d; gprev[v] = id; }
        }
    }
    route_n = 0;
    for (int v = to; v != from && gprev[v] >= 0 && route_n < n_nodes; ) {
        int id = gprev[v];
        route_e[route_n++] = id;
        v = edges[id].a == v ? edges[id].b : edges[id].a;
    }
}

static void gps_to(float x, float y) {
    gps_on = 1; gps_x = x; gps_y = y; gps_t = 0;
}

// a spot on a sidewalk beside a road, rmin to rmax from you
static int pick_spot(float rmin, float rmax, float *ox, float *oy) {
    for (int t = 0; t < 40; t++) {
        int id = edge_near(rmin, rmax);
        if (id < 0) continue;
        Edge *e = &edges[id];
        if (e->cls <= 1 || (e->flags & 1) || e->n < 2) continue;
        int k = rnd_in(e->n - 1);
        float *a = &ep[(e->first + k) * 2], *b = a + 2;
        float dx = b[0] - a[0], dy = b[1] - a[1], L = sqrtf_(dx * dx + dy * dy);
        if (L < 2) continue;
        float s = (rnd() & 1) ? 1.0f : -1.0f, off = e->w * 0.5f + 1.0f;
        float x = (a[0] + b[0]) / 2 - dy / L * off * s, y = (a[1] + b[1]) / 2 + dx / L * off * s;
        if (ground_kind(x, y) != 2 || near_solid(x, y, 0.5f)) continue;
        *ox = x; *oy = y;
        return 1;
    }
    return 0;
}

static void new_fare(void) {
    float x, y;
    if (!pick_spot(35, 90, &x, &y)) { job = J_NONE; say("NO FARES AROUND HERE", 0, -1); return; }
    job = J_PICKUP; job_x = x; job_y = y; job_t = 60 * 70;
    job_pi = PEOPLE - 1;
    Person *p = &people[job_pi];
    p->x = x; p->y = y; p->dead = 0; p->state = P_IDLE; p->act = 0; p->timer = 1 << 28;
    p->pose = PO_IDLE; p->hyaw = 0; p->buddy = -1; p->vx = p->vy = 0; p->spd = 0;
    p->shirt = shirts[rnd_in(8)]; p->look = rnd();
    face(p, me_x, me_y, 10);
    gps_to(x, y);
    say("PICK UP THE FARE", 0, -1);
}

static void end_job(const char *why) {
    if (job_pi >= 0 && people[job_pi].state == P_IDLE) people[job_pi].timer = 200;
    job = J_NONE; job_pi = -1; gps_on = 0;
    say(why, 0, -1);
}

static void update_job(void) {
    if (job_msg_t) job_msg_t--;
    int taxi = mine >= 0 && cars[mine].kind == K_TAXI && !cars[mine].wreck;
    if (taxi && !was_taxi && job == J_NONE) say("PRESS J FOR TAXI FARES", 0, -1);
    was_taxi = taxi;
    if (key_job) {
        key_job = 0;
        if (job != J_NONE) end_job("TAXI JOB OVER");
        else if (taxi) { job_fares = 0; job_earned = 0; new_fare(); }
    }
    if (job == J_NONE && wp_on) {
        if (!gps_on || gps_x != wp_x || gps_y != wp_y) gps_to(wp_x, wp_y);
        if (dist2_me(wp_x, wp_y) < 6 * 6) { wp_on = 0; gps_on = 0; say("YOU HAVE ARRIVED", 0, -1); }
    }
    if (gps_on && (gps_t++ % 45) == 0) plan_route();
#ifdef MOORTEST
    if (tick == 600 && mine < 0) {
        int best = -1; float bd = 1e9f;
        for (int i = BOAT0; i < CARS; i++) if (cars[i].mode == M_PARKED) { float d = dist2_me(cars[i].x, cars[i].y); if (d < bd) { bd = d; best = i; } }
        if (best >= 0) {
            Car *c = &cars[best];
            for (int k = 0; k < 16; k++) {
                float x = c->x + cosf_(k * TAU / 16) * 1.9f, y = c->y + sinf_(k * TAU / 16) * 1.9f;
                if (ground_kind(x, y) == 2 && !body_solid(x, y)) { foot_x = x; foot_y = y; foot_a = atan2f_(c->y - y, c->x - x); cam_yaw_s = foot_a; break; }
            }
        }
    }
#endif
#ifdef FARETEST
    if (job != J_NONE && mine >= 0 && (tick % 480) == 240) { Car *c = &cars[mine]; c->x = job_x + 1.2f; c->y = job_y + 0.5f; c->vx = c->vy = 0; }
#endif
    if (job == J_NONE) return;
    if (!taxi) { end_job("TAXI JOB OVER"); return; }
    if (caught) { end_job("TAXI JOB OVER"); return; }
    Car *c = &cars[mine];
    float dx = c->x - job_x, dy = c->y - job_y, d2 = dx * dx + dy * dy;
    int slow = speed2(c) < 0.0012f;
    if (--job_t <= 0) { end_job(job == J_PICKUP ? "THE FARE GAVE UP" : "TOO SLOW, THE FARE GOT OUT"); return; }
    if (job == J_PICKUP) {
        Person *p = &people[job_pi];
        if (p->dead || p->state != P_IDLE) { end_job("THE FARE RAN OFF"); return; }
        face(p, c->x, c->y, 0.1f);
        if (d2 < 3.2f * 3.2f && slow) {
            p->state = P_DOWN; p->dead = 30; p->x = p->y = -1000;
            // somewhere with a name, not too near and not too far
            int tries = 0;
            for (; tries < 300; tries++) {
                int b = rnd_in(n_blds);
                if (!bld_name[b] || blds[b].upper) continue;
                float nx, ny, wl;
                int w = street_wall(&blds[b], &nx, &ny, &wl);
                if (w < 0) continue;
                float *pp = &bp[(blds[b].first + w) * 2], *qq = &bp[(blds[b].first + (w + 1) % blds[b].n) * 2];
                float tx = (pp[0] + qq[0]) / 2 + nx * 1.6f, ty = (pp[1] + qq[1]) / 2 + ny * 1.6f;
                float ex = tx - c->x, ey = ty - c->y, dd = sqrtf_(ex * ex + ey * ey);
                if (dd < 110 || dd > 420 || solid(tx, ty)) continue;
                job_dest = b; job_x = tx; job_y = ty; job_d = dd;
                break;
            }
            if (tries == 300) { end_job("THE FARE CHANGED THEIR MIND"); return; }
            const u8 *nm = bld_name[job_dest];
            char nb[40];
            int n = mini(nm[-1], 39);
            for (int i = 0; i < n; i++) nb[i] = (char)nm[i];
            nb[n] = 0;
            say("TAKE ME TO ", nb, -1);
            job = J_RIDE; job_t = (int)(job_d * 13) + 900;
            gps_to(job_x, job_y);
        }
    } else if (job == J_RIDE) {
        if (d2 < 3.5f * 3.5f && slow) {
            int pay = 12 + (int)(job_d * 0.25f) + job_t / 120;
            money += pay; job_earned += pay; job_fares++;
            new_fare();
            say("FARE PAID $", 0, pay);
        }
    }
}

static void draw_job_marker(void) {
    if (job == J_NONE) return;
    float x = job_x, y = job_y;
    float dx = x - cam.x, dy = y - cam.y;
    if (dx * dx + dy * dy > fog_far * fog_far) return;
    // a see through glowing ring and column, like the old games
    float pulse = 0.75f + 0.25f * sinf_(tick * 0.12f), r = 0.9f, h = 2.4f;
    u32 col = job == J_PICKUP ? 0x3070ff : 0xffd030;
    Mat m = {t_white, scale_col(col, 0.32f * pulse), 1, 1, 0, 0, 1, 0};
    for (int k = 0; k < 12; k++) {
        float a0 = k * TAU / 12, a1 = (k + 1) * TAU / 12;
        V3 b0 = v3(x + cosf_(a0) * r, y + sinf_(a0) * r, 0.05f), b1 = v3(x + cosf_(a1) * r, y + sinf_(a1) * r, 0.05f);
        quad(b0, b1, add(b1, v3(0, 0, h)), add(b0, v3(0, 0, h)), 1, 1, &m);
    }
}
