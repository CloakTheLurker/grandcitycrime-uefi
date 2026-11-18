// triangles in pixels out no gpu allowed fr
// x east y north z up

typedef struct { float x, y, z; } V3;
static inline V3 v3(float x, float y, float z) { V3 r = {x, y, z}; return r; }
static inline V3 add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline V3 sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline V3 scl(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline V3 norm(V3 a) { float l = sqrtf_(dot(a, a)); return l > 0 ? scl(a, 1 / l) : a; }

static int W, H;
static u32 *fb;
static float *zb;
static float focal, half_w, half_h;

static V3 cam, cam_f, cam_r, cam_u;
static float cam_yaw, cam_pitch;
static V3 sun_dir, sun_true;
static float tod = 17.0f;
static float amb_r = 0.5f, amb_g = 0.5f, amb_b = 0.6f, sun_r = 0.85f, sun_g = 0.65f, sun_b = 0.4f;
static float lum_amb = 1, lum_sun = 1, cast_r = 1, cast_g = 1, cast_b = 1, night_k = 0, sun_vis = 1;
static u32 glow_m = 256;
static u32 fog_col = 0xff9a6a;
static float fog_near = 25, fog_far = 85;

static void set_camera(V3 pos, float yaw, float pitch) {
    cam = pos;
    cam_yaw = yaw;
    cam_pitch = pitch;
    float cp = cosf_(pitch), sp = sinf_(pitch);
    float cy = cosf_(yaw), sy = sinf_(yaw);
    cam_f = v3(cy * cp, sy * cp, sp);
    cam_r = v3(sy, -cy, 0);
    cam_u = cross(cam_r, cam_f);
}

// top byte ff means it glows so skip the shading
typedef struct {
    u32 *lv[8];
    int bits[8];
    int levels;
} Tex;

static u32 *pool(usize bytes);

static void make_mips(Tex *t) {
    int b = t->bits[0];
    t->levels = 1;
    while (b > 2 && t->levels < 8) {
        int s = 1 << b, h = s >> 1;
        u32 *src = t->lv[t->levels - 1], *dst = pool((usize)h * h * 4);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < h; x++) {
                u32 p[4] = {src[(2 * y) * s + 2 * x], src[(2 * y) * s + 2 * x + 1], src[(2 * y + 1) * s + 2 * x], src[(2 * y + 1) * s + 2 * x + 1]};
                u32 r = 0, g = 0, bl = 0, glow = 0;
                for (int i = 0; i < 4; i++) {
                    r += p[i] >> 16 & 255;
                    g += p[i] >> 8 & 255;
                    bl += p[i] & 255;
                    glow += p[i] >> 31;
                }
                dst[y * h + x] = (glow >= 2 ? 0xff000000u : 0) | (r / 4) << 16 | (g / 4) << 8 | bl / 4;
            }
        t->lv[t->levels] = dst;
        t->bits[t->levels] = b - 1;
        t->levels++;
        b--;
    }
}

static Tex *new_tex(int bits) {
    Tex *t = (Tex *)pool(sizeof(Tex));
    memset(t, 0, sizeof(Tex));
    t->bits[0] = bits;
    t->lv[0] = pool((usize)4 << (2 * bits));
    return t;
}

typedef struct {
    Tex *tex;
    u32 tint;       // 128 means 1x
    int two_sided;
    int no_fog;
    int smooth;
    int shade;      // darken once per pixel or it goes double dark
    int add;
    int alpha;
    int cut;
    int filt;       // softer mip so the paving stops crawling
} Mat;
static u8 *sbuf;

typedef struct { float x, y, z, u, v, s; } CV;   // camera space s is the light
typedef struct { float x, y, iw, uw, vw, s; } SV; // screen space

static int tris_drawn;
static Tex *t_glass, *t_hotel, *t_deco;
static Tex *t_pool, *t_beam;
#ifdef PIXDBG
float dbg_tri[16];
static CV *dbg_cur;
#endif

static void raster(SV *a, SV *b, SV *c, Mat *m, float tpp_k, int wid, int nw) {
#ifdef NORASTER
    return;
#endif
    float area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    if (area == 0) return;
    if (area > 0) {
        if (!m->two_sided) return;
        SV *t = b; b = c; c = t;
        area = -area;
    }
    float ia = 1.0f / area;
    float d1x = b->x - a->x, d1y = b->y - a->y, d2x = c->x - a->x, d2y = c->y - a->y;
#define PLANE(f, dx, dy) \
    float dx = ((b->f - a->f) * d2y - (c->f - a->f) * d1y) * ia; \
    float dy = ((c->f - a->f) * d1x - (b->f - a->f) * d2x) * ia;
    PLANE(iw, wdx, wdy)
    PLANE(uw, udx, udy)
    PLANE(vw, vdx, vdy)
    PLANE(s, sdx, sdy)
#undef PLANE
    int y0 = maxi(0, (int)(minf(a->y, minf(b->y, c->y)) + 0.5f));
    int y1 = mini(H - 1, (int)(maxf(a->y, maxf(b->y, c->y)) - 0.5f));
    if (y0 > y1) return;
    tris_drawn++;
    // inside means all 3 edges below zero
    SV *e[3][2] = {{a, b}, {b, c}, {c, a}};
    float ex[3], ey[3], ec[3];
    for (int i = 0; i < 3; i++) {
        SV *p = e[i][0], *q = e[i][1];
        ex[i] = -(q->y - p->y);
        ey[i] = (q->x - p->x);
        ec[i] = -(ex[i] * p->x + ey[i] * p->y);
    }
    Tex *t = m->tex;
    u32 tr = m->tint >> 16 & 255, tg = m->tint >> 8 & 255, tb = m->tint & 255;
    float fog_span = 256.0f / (fog_far - fog_near);
    for (int y = y0; y <= y1; y++) {
        // 8 row strips each core takes its own
        if (nw > 1 && ((y >> 3) % nw) != wid) { y |= 7; continue; }
        float py = y + 0.5f;
        float xl = 0, xr = (float)W;
        int empty = 0;
        for (int i = 0; i < 3; i++) {
            // solve each edge for x on this row
            float k = ey[i] * py + ec[i];
            if (ex[i] > 0) xr = minf(xr, -k / ex[i]);
            else if (ex[i] < 0) xl = maxf(xl, -k / ex[i]);
            else if (k > 0) empty = 1;
        }
        if (empty) continue;
        int x0 = maxi(0, (int)(xl + 0.5f - 0.0001f) + ((xl + 0.5f) > (int)(xl + 0.5f) ? 0 : 0));
        x0 = maxi(0, (int)(xl - 0.5f + 0.9999f));
        int x1 = mini(W - 1, (int)(xr - 0.5f + 0.9999f) - 1);
        if (x0 > x1) continue;
        float px = x0 + 0.5f;
        float iw = a->iw + wdx * (px - a->x) + wdy * (py - a->y);
        float uw = a->uw + udx * (px - a->x) + udy * (py - a->y);
        float vw = a->vw + vdx * (px - a->x) + vdy * (py - a->y);
        float sl = a->s + sdx * (px - a->x) + sdy * (py - a->y);
        u32 *row = fb + y * W;
        float *zr = zb + y * W;
        u8 *sr = sbuf + y * W;
        for (int x = x0; x <= x1; x++, iw += wdx, uw += udx, vw += vdx, sl += sdx) {
            if (m->shade) {
                // tiny slack so the shadow beats the ground under it
                if (iw * 1.0008f <= zr[x] || sr[x]) continue;
                sr[x] = 1;
                u32 c = row[x];
                row[x] = ((c >> 16 & 255) * 140 >> 8) << 16 | ((c >> 8 & 255) * 148 >> 8) << 8 | ((c & 255) * 182 >> 8);
                continue;
            }
            if (m->add) {
                if (iw * 1.0008f <= zr[x]) continue;
                float z = 1.0f / iw;
                int sb = t->bits[0], sz = 1 << sb;
                float fu = uw * z * sz - 0.5f + 1048576.0f, fv = vw * z * sz - 0.5f + 1048576.0f;
                int iu = (int)fu, iv = (int)fv;
                u32 ax = (u32)((fu - iu) * 256), ay = (u32)((fv - iv) * 256);
                int x0_ = iu & (sz - 1), x1_ = (iu + 1) & (sz - 1), y0_ = iv & (sz - 1), y1_ = (iv + 1) & (sz - 1);
                u32 *tl = t->lv[0];
                u32 p = mix(mix(tl[y0_ << sb | x0_], tl[y0_ << sb | x1_], ax), mix(tl[y1_ << sb | x0_], tl[y1_ << sb | x1_], ax), ay);
                u32 k = 256;
                if (z > fog_near) { int f = (int)((z - fog_near) * fog_span); k = f > 256 ? 0 : 256 - f; }
                if (m->add == 2) {
                    u32 a = (((p >> 16 & 255) * (u32)m->alpha) >> 8) * k >> 8;
                    if (a) row[x] = mix(row[x], m->tint, a > 256 ? 256 : a);
                    continue;
                }
                u32 c = row[x];
                u32 r = (c >> 16 & 255) + (((p >> 16 & 255) * tr * k) >> 15), g = (c >> 8 & 255) + (((p >> 8 & 255) * tg * k) >> 15), bl = (c & 255) + (((p & 255) * tb * k) >> 15);
                row[x] = (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (bl > 255 ? 255 : bl);
                continue;
            }
            if (iw <= zr[x]) continue;
            float z = 1.0f / iw;
            float u = uw * z, v = vw * z;
            // float exponent bits are a free log2 lol
            float tpp = tpp_k * z;
            union { float f; u32 i; } bits = {m->filt ? tpp * 2.0f : tpp};
            int lv = (int)(bits.i >> 23 & 255) - 127;
            lv = lv < 0 ? 0 : lv >= t->levels ? t->levels - 1 : lv;
            int sb = t->bits[lv];
            int sz = 1 << sb;
            u32 p;
            if (lv == 0 && tpp < 0.8f) {
                // up close blend 4 texels so no minecraft blocks
                float fu = u * sz - 0.5f + 1048576.0f, fv = v * sz - 0.5f + 1048576.0f;
                int iu = (int)fu, iv = (int)fv;
                u32 ax = (u32)((fu - iu) * 256), ay = (u32)((fv - iv) * 256);
                int x0_ = iu & (sz - 1), x1_ = (iu + 1) & (sz - 1), y0_ = iv & (sz - 1), y1_ = (iv + 1) & (sz - 1);
                u32 *tl = t->lv[lv];
                u32 p00 = tl[y0_ << sb | x0_], p10 = tl[y0_ << sb | x1_], p01 = tl[y1_ << sb | x0_], p11 = tl[y1_ << sb | x1_];
                p = mix(mix(p00, p10, ax), mix(p01, p11, ax), ay) | (p00 & 0xff000000u);
            } else {
                int tx = (int)(u * sz + 1048576.0f) & (sz - 1);
                int ty = (int)(v * sz + 1048576.0f) & (sz - 1);
                p = t->lv[lv][ty << sb | tx];
            }
            if (m->cut && (p >> 24) == 1) continue;
            zr[x] = iw;
            u32 col;
            if ((p >> 24) == 0xff) {
                col = p & 0xffffff;
                if (glow_m < 256) {
                    u32 r = (col >> 16 & 255) * tr >> 7, g = (col >> 8 & 255) * tg >> 7, bl = (col & 255) * tb >> 7;
                    col = mix((r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (bl > 255 ? 255 : bl), col, glow_m);
                }
            }
            else {
                u32 k = 128;
                if (m->smooth) { int kk = (int)(sl * 128); k = kk < 0 ? 0 : kk > 400 ? 400 : (u32)kk; }
                u32 r = (p >> 16 & 255) * tr * k >> 14, g = (p >> 8 & 255) * tg * k >> 14, bl = (p & 255) * tb * k >> 14;
                col = (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (bl > 255 ? 255 : bl);
            }
            if (!m->no_fog && z > fog_near) {
                int f = (int)((z - fog_near) * fog_span);
                col = mix(col, fog_col, f > 256 ? 256 : f);
            }
            row[x] = col;
#ifdef PIXDBG
            if (x == 1100 && y == 60 && dbg_cur) {
                for (int k = 0; k < 3; k++) { dbg_tri[k * 5] = dbg_cur[k].x; dbg_tri[k * 5 + 1] = dbg_cur[k].y; dbg_tri[k * 5 + 2] = dbg_cur[k].z; dbg_tri[k * 5 + 3] = dbg_cur[k].u; dbg_tri[k * 5 + 4] = dbg_cur[k].v; }
                dbg_tri[15] = (float)(t == t_glass) + 2 * (t == t_hotel) + 4 * (t == t_deco);
            }
#endif
        }
    }
}

#define NEAR_Z 0.08f

static float tpp_at_one(CV *v, Tex *t) {
    V3 p0 = v3(v[0].x, v[0].y, v[0].z), p1 = v3(v[1].x, v[1].y, v[1].z), p2 = v3(v[2].x, v[2].y, v[2].z);
    V3 n = cross(sub(p1, p0), sub(p2, p0));
    float world_area = sqrtf_(dot(n, n));
    float uv_area = absf((v[1].u - v[0].u) * (v[2].v - v[0].v) - (v[2].u - v[0].u) * (v[1].v - v[0].v));
    if (world_area < 1e-6f) return 1;
    float sz = (float)(1 << t->bits[0]);
    float tpu = sqrtf_(uv_area / world_area) * sz;
    V3 c = norm(scl(add(add(p0, p1), p2), 1.0f / 3));
    float facing = absf(dot(norm(n), c));
    facing = facing < 0.25f ? 0.25f : facing;
    return tpu / focal / facing;
}

static void project(CV *c, SV *s) {
    float iw = 1.0f / c->z;
    s->x = half_w + focal * c->x * iw;
    s->y = half_h - focal * c->y * iw;
    s->iw = iw;
    s->uw = c->u * iw;
    s->vw = c->v * iw;
    s->s = c->s;
}

static inline void to_cam(V3 p, CV *c, float u, float v) {
    V3 d = sub(p, cam);
    c->x = dot(d, cam_r);
    c->y = dot(d, cam_u);
    c->z = dot(d, cam_f);
    c->u = u;
    c->v = v;
    c->s = 1;
}

typedef struct { SV a, b, c; Mat m; float k; } QT;
#define QCAP 70000
static QT *qt;
static int nqt, queue_on;

static void raster_job(int w, int n) {
    for (int i = 0; i < nqt; i++) raster(&qt[i].a, &qt[i].b, &qt[i].c, &qt[i].m, qt[i].k, w, n);
}

static void flush_queue(void) {
    if (nqt) run_job(raster_job);
    nqt = 0;
}

static void clip_draw(CV *in, Mat *m) {
    if (in[0].z < NEAR_Z && in[1].z < NEAR_Z && in[2].z < NEAR_Z) return;
#ifdef PIXDBG
    dbg_cur = in;
#endif
    float tk = tpp_at_one(in, m->tex);
    CV out[4];
    int n = 0;
    for (int i = 0; i < 3; i++) {
        CV *p = &in[i], *q = &in[(i + 1) % 3];
        int pi = p->z >= NEAR_Z, qi = q->z >= NEAR_Z;
        if (pi) out[n++] = *p;
        if (pi != qi) {
            float t = (NEAR_Z - p->z) / (q->z - p->z);
            CV r = {p->x + (q->x - p->x) * t, p->y + (q->y - p->y) * t, NEAR_Z, p->u + (q->u - p->u) * t, p->v + (q->v - p->v) * t, p->s + (q->s - p->s) * t};
            out[n++] = r;
        }
    }
    SV s[4];
    for (int i = 0; i < n; i++) project(&out[i], &s[i]);
    for (int i = 1; i + 1 < n; i++) {
        if (queue_on) {
            if (nqt == QCAP) flush_queue();
            QT *q = &qt[nqt++];
            q->a = s[0]; q->b = s[i]; q->c = s[i + 1]; q->m = *m; q->k = tk;
        } else raster(&s[0], &s[i], &s[i + 1], m, tk, 0, 1);
    }
}

static void tri(V3 a, V3 b, V3 c, float ua, float va, float ub, float vb, float uc, float vc, Mat *m) {
    CV v[3];
    to_cam(a, &v[0], ua, va);
    to_cam(b, &v[1], ub, vb);
    to_cam(c, &v[2], uc, vc);
    clip_draw(v, m);
}

static void tri_full(V3 a, V3 b, V3 c, float ua, float va, float ub, float vb, float uc, float vc, float sa, float sb, float sc, Mat *m) {
    CV v[3];
    to_cam(a, &v[0], ua, va);
    to_cam(b, &v[1], ub, vb);
    to_cam(c, &v[2], uc, vc);
    v[0].s = sa; v[1].s = sb; v[2].s = sc;
    clip_draw(v, m);
}

static void tri_s(V3 a, V3 b, V3 c, float sa, float sb, float sc, Mat *m) {
    CV v[3];
    to_cam(a, &v[0], 0, 0);
    to_cam(b, &v[1], 1, 0);
    to_cam(c, &v[2], 1, 1);
    v[0].s = sa; v[1].s = sb; v[2].s = sc;
    clip_draw(v, m);
}

static float lumf(V3 n) {
    float d = dot(n, sun_dir);
    return (0.55f + 0.1f * n.z) * lum_amb + 0.6f * (d > 0 ? d : 0) * lum_sun;
}

static void quad(V3 a, V3 b, V3 c, V3 d, float us, float vs, Mat *m) {
    tri(a, b, c, 0, vs, us, vs, us, 0, m);
    tri(a, c, d, 0, vs, us, 0, 0, 0, m);
}

// blend edges that jump cheap aa but it slaps
static u32 *aa_buf;
static void aa_job(int w, int nw) {
    for (int y = 0; y < H; y++) {
        if (nw > 1 && ((y >> 3) % nw) != w) { y |= 7; continue; }
        u32 *src = fb + y * W, *dst = aa_buf + y * W;
        if (y == 0 || y == H - 1) { for (int x = 0; x < W; x++) dst[x] = src[x]; continue; }
        dst[0] = src[0]; dst[W - 1] = src[W - 1];
        for (int x = 1; x < W - 1; x++) {
            u32 c = src[x], n = src[x - W], s = src[x + W], e = src[x + 1], w = src[x - 1];
#define LUMA(p) (((p) >> 16 & 255) * 2 + ((p) >> 8 & 255) * 5 + ((p) & 255))
            int lc = LUMA(c), ln = LUMA(n), ls = LUMA(s), le = LUMA(e), lw = LUMA(w);
            int mx = maxi(lc, maxi(maxi(ln, ls), maxi(le, lw))), mn = mini(lc, mini(mini(ln, ls), mini(le, lw)));
            if (mx - mn < 160) { dst[x] = c; continue; }
            u32 rb = ((c & 0xff00ff) * 4 + (n & 0xff00ff) + (s & 0xff00ff) + (e & 0xff00ff) + (w & 0xff00ff)) >> 3 & 0xff00ff;
            u32 g = ((c & 0xff00) * 4 + (n & 0xff00) + (s & 0xff00) + (e & 0xff00) + (w & 0xff00)) >> 3 & 0xff00;
            dst[x] = rb | g;
#undef LUMA
        }
    }
}

static void smooth_edges(void) {
    if (!aa_buf) aa_buf = pool((usize)W * H * 4);
    run_job(aa_job);
    u32 *t = fb; fb = aa_buf; aa_buf = t;
}

// golden hour on the lit side blue in the shade
static u32 light(u32 base, V3 n) {
    float d = dot(n, sun_dir);
    d = d > 0 ? d : 0;
    float up = 0.5f + 0.5f * n.z;
    float kr = amb_r * (1 + 0.12f * up) + sun_r * d, kg = amb_g * (1 + 0.15f * up) + sun_g * d, kb = amb_b * (1 + 0.2f * up) + sun_b * d;
    int r = (int)((base >> 16 & 255) * kr * 128 / 255);
    int g = (int)((base >> 8 & 255) * kg * 128 / 255);
    int b = (int)((base & 255) * kb * 128 / 255);
    return rgb(r, g, b);
}
