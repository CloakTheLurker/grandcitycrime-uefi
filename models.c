// models are boxes and prisms in their own space, x forward y left z up
static V3 m_o;
static float m_c = 1, m_s = 0;      // yaw
static float m_pc = 1, m_ps = 0;    // tipped over, for people lying down
static float j_c = 1, j_s = 0;      // a joint swinging round y
static V3 j_p;
static float m_rc = 1, m_rs = 0, m_qc = 1, m_qs = 0, m_scale = 1;

static Tex *t_head, *t_tail, *t_red, *t_blue, *t_red_dim, *t_blue_dim, *t_dark, *t_taxi;

static void glow_tex(Tex **t, u32 c, int glow) {
    *t = new_tex(2);
    for (int i = 0; i < 16; i++) (*t)->lv[0][i] = (glow ? 0xff000000u : 0) | c;
    make_mips(*t);
}

static void make_model_tex(void) {
    glow_tex(&t_head, 0xfff4d0, 1);
    glow_tex(&t_tail, 0xff2a3a, 1);
    glow_tex(&t_red, 0xff2030, 1);
    glow_tex(&t_blue, 0x3050ff, 1);
    glow_tex(&t_red_dim, 0x601018, 0);
    glow_tex(&t_blue_dim, 0x182060, 0);
    glow_tex(&t_dark, 0x303036, 0);
    glow_tex(&t_taxi, 0xffe27a, 1);
}

static void place(V3 o, float yaw) {
    m_o = o;
    m_c = cosf_(yaw);
    m_s = sinf_(yaw);
    m_pc = 1; m_ps = 0;
    m_rc = 1; m_rs = 0;
    m_qc = 1; m_qs = 0;
    m_scale = 1;
    j_c = 1; j_s = 0;
}

static V3 joint(V3 l) {
    float x = l.x - j_p.x, z = l.z - j_p.z;
    return v3(j_p.x + x * j_c + z * j_s, l.y, j_p.z - x * j_s + z * j_c);
}

static V3 mx(V3 l) {
    l = joint(l);
    l.x *= m_scale; l.y *= m_scale; l.z *= m_scale;
    float qx = l.x * m_qc - l.y * m_qs;
    l.y = l.x * m_qs + l.y * m_qc; l.x = qx;
    float x = l.x * m_pc - l.z * m_ps, z = l.x * m_ps + l.z * m_pc;
    float y = l.y * m_rc - z * m_rs;
    z = l.y * m_rs + z * m_rc;
    return v3(m_o.x + x * m_c - y * m_s, m_o.y + x * m_s + y * m_c, m_o.z + z);
}

static V3 mn(V3 n) {
    float x = n.x * j_c + n.z * j_s, z = -n.x * j_s + n.z * j_c;
    float qy = x * m_qs + n.y * m_qc;
    x = x * m_qc - n.y * m_qs;
    float x2 = x * m_pc - z * m_ps, z2 = x * m_ps + z * m_pc;
    float y2 = qy * m_rc - z2 * m_rs;
    z2 = n.y * m_rs + z2 * m_rc;
    return v3(x2 * m_c - y2 * m_s, x2 * m_s + y2 * m_c, z2);
}

static void mquad(V3 a, V3 b, V3 c, V3 d, V3 n, Tex *t, u32 tint, int two) {
    Mat m = {t, light(tint, mn(n)), two, 0};
    quad(mx(a), mx(b), mx(c), mx(d), 1, 1, &m);
}

static void mbox(V3 lo, V3 hi, Tex *t, u32 tint) {
    float x0 = lo.x, y0 = lo.y, z0 = lo.z, x1 = hi.x, y1 = hi.y, z1 = hi.z;
    mquad(v3(x0, y0, z0), v3(x1, y0, z0), v3(x1, y0, z1), v3(x0, y0, z1), v3(0, -1, 0), t, tint, 0);
    mquad(v3(x1, y0, z0), v3(x1, y1, z0), v3(x1, y1, z1), v3(x1, y0, z1), v3(1, 0, 0), t, tint, 0);
    mquad(v3(x1, y1, z0), v3(x0, y1, z0), v3(x0, y1, z1), v3(x1, y1, z1), v3(0, 1, 0), t, tint, 0);
    mquad(v3(x0, y1, z0), v3(x0, y0, z0), v3(x0, y0, z1), v3(x0, y1, z1), v3(-1, 0, 0), t, tint, 0);
    mquad(v3(x0, y0, z1), v3(x1, y0, z1), v3(x1, y1, z1), v3(x0, y1, z1), v3(0, 0, 1), t, tint, 0);
    mquad(v3(x0, y1, z0), v3(x1, y1, z0), v3(x1, y0, z0), v3(x0, y0, z0), v3(0, 0, -1), t, tint, 0);
}

// wheel is an 8 sided prism along y
static void wheel(float x, float y, float r, float w) {
    V3 rim[8];
    for (int i = 0; i < 8; i++) {
        float a = i * (TAU / 8) + TAU / 16;
        rim[i] = v3(x + cosf_(a) * r, 0, r + sinf_(a) * r);
    }
    float ya = y - w / 2, yb = y + w / 2;
    for (int i = 0; i < 8; i++) {
        V3 p = rim[i], q = rim[(i + 1) % 8];
        V3 n = v3(cosf_(i * (TAU / 8) + TAU / 8), 0, sinf_(i * (TAU / 8) + TAU / 8));
        mquad(v3(q.x, ya, q.z), v3(p.x, ya, p.z), v3(p.x, yb, p.z), v3(q.x, yb, q.z), scl(n, 1), t_tyre, 0xffffff, 0);
    }
    // hub caps both sides
    for (int side = 0; side < 2; side++) {
        float yy = side ? yb : ya;
        for (int i = 0; i < 8; i++) {
            V3 p = v3(rim[i].x, yy, rim[i].z), q = v3(rim[(i + 1) % 8].x, yy, rim[(i + 1) % 8].z), c = v3(x, yy, r);
            V3 a = side ? p : q, b = side ? q : p;
            Mat m = {i & 1 ? t_dark : t_tyre, light(0xd0d0d0, mn(v3(0, side ? 1 : -1, 0))), 0, 0};
            tri(mx(c), mx(a), mx(b), 0, 0, 1, 0, 1, 1, &m);
        }
    }
}

// tint for smooth surfaces, the light comes per corner instead
static u32 base_tint(u32 c) {
    int r = (int)((c >> 17 & 127) * cast_r), g = (int)((c >> 9 & 127) * cast_g), b = (int)((c >> 1 & 127) * cast_b);
    return rgb(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
}

// R rings of K points in model space, C is each ring's middle so we know which way is out
// normals worked out from the neighbours so round things shade round
#define LMAX 24
static void loft(V3 *P, V3 *C, int R, int K, Tex *t, u32 tint) {
    static V3 N[LMAX * LMAX];
    for (int r = 0; r < R; r++)
        for (int i = 0; i < K; i++) {
            V3 a = P[r * K + (i + 1) % K], b = P[r * K + (i + K - 1) % K];
            V3 c = P[mini(r + 1, R - 1) * K + i], d = P[maxi(r - 1, 0) * K + i];
            V3 n = norm(cross(sub(a, b), sub(c, d)));
            if (dot(n, sub(P[r * K + i], C[r])) < 0) n = scl(n, -1);
            N[r * K + i] = mn(n);
        }
    Mat m = {t, base_tint(tint), 0, 0, 1};
    for (int r = 0; r + 1 < R; r++)
        for (int i = 0; i < K; i++) {
            int j = (i + 1) % K;
            int ia = r * K + i, ib = r * K + j, ic = (r + 1) * K + j, id = (r + 1) * K + i;
            V3 a = P[ia], b = P[ib], c = P[ic], d = P[id];
            V3 f = cross(sub(b, a), sub(c, a));
            V3 mid = scl(add(a, c), 0.5f);
            if (dot(f, sub(mid, C[r])) < 0) { int tmp = ib; ib = id; id = tmp; V3 tv = b; b = d; d = tv; }
            V3 A = mx(a), B = mx(b), Cc = mx(c), D = mx(d);
            float la = lumf(N[ia]), lb = lumf(N[ib]), lc = lumf(N[ic]), ld = lumf(N[id]);
            tri_s(A, B, Cc, la, lb, lc, &m);
            tri_s(A, Cc, D, la, lc, ld, &m);
        }
}

// flat cap over a ring, facing away from 'in'
static void cap(V3 *P, int K, V3 c, V3 out, Tex *t, u32 tint) {
    Mat m = {t, light(tint, mn(out)), 0, 0, 0};
    for (int i = 0; i < K; i++) {
        V3 a = c, b = P[i], d = P[(i + 1) % K];
        V3 f = cross(sub(b, a), sub(d, a));
        if (dot(f, out) < 0) { V3 tv = b; b = d; d = tv; }
        tri(mx(a), mx(b), mx(d), 0, 0, 1, 0, 1, 1, &m);
    }
}

// rounded rectangle ring across x, half width w, middle height zc, half height h, corner r
static void rrect_ring(V3 *out, float x, float w, float zc, float h, float r) {
    float cy[4] = {w - r, -(w - r), -(w - r), w - r}, cz[4] = {zc + h - r, zc + h - r, zc - h + r, zc - h + r};
    int n = 0;
    for (int c = 0; c < 4; c++)
        for (int k = 0; k < 4; k++) {
            float a = c * (PI / 2) + k * (PI / 6);
            out[n++] = v3(x, cy[c] + cosf_(a) * r, cz[c] + sinf_(a) * r);
        }
}

static void sphere(V3 c, float r, float rz, float lat0, Tex *t, u32 tint) {
    V3 P[7 * 10], C[7];
    int R = 7, K = 10;
    for (int i = 0; i < R; i++) {
        float la = lat0 + (PI / 2 - lat0) * i / (R - 1);
        if (i == R - 1) la = PI / 2 - 0.01f;
        C[i] = v3(c.x, c.y, c.z + sinf_(la) * rz * 0.5f);
        for (int k = 0; k < K; k++) {
            float lo = k * TAU / K;
            P[i * K + k] = v3(c.x + cosf_(la) * cosf_(lo) * r, c.y + cosf_(la) * sinf_(lo) * r, c.z + sinf_(la) * rz);
        }
    }
    for (int i = 0; i < R; i++) C[i] = c;
    loft(P, C, R, K, t, tint);
}

// round limb from a to b
static void limb(V3 a, V3 b, float ra, float rb, Tex *t, u32 tint) {
    V3 d = norm(sub(b, a));
    V3 u = absf(d.z) < 0.9f ? norm(cross(d, v3(0, 0, 1))) : norm(cross(d, v3(1, 0, 0)));
    V3 w = cross(d, u);
    V3 P[2 * 8], C[2] = {a, b};
    for (int k = 0; k < 8; k++) {
        float an = k * TAU / 8;
        V3 o = add(scl(u, cosf_(an)), scl(w, sinf_(an)));
        P[k] = add(a, scl(o, ra));
        P[8 + k] = add(b, scl(o, rb));
    }
    loft(P, C, 2, 8, t, tint);
    cap(&P[8], 8, b, d, t, tint);
}

// wheel along y, 12 sides, smooth tyre and a lighter hub
static void wheel2(float x, float y, float r, float w) {
    V3 P[2 * 12], C[2] = {v3(x, y - w / 2, r), v3(x, y + w / 2, r)};
    for (int k = 0; k < 12; k++) {
        float a = k * TAU / 12;
        P[k] = v3(x + cosf_(a) * r, y - w / 2, r + sinf_(a) * r);
        P[12 + k] = v3(x + cosf_(a) * r, y + w / 2, r + sinf_(a) * r);
    }
    loft(P, C, 2, 12, t_tyre, 0xffffff);
    float out = y < 0 ? -1.0f : 1.0f;
    V3 *rim = y < 0 ? P : &P[12];
    V3 hub[12];
    for (int k = 0; k < 12; k++) hub[k] = v3(x + (rim[k].x - x) * 0.6f, rim[k].y + out * 0.002f, r + (rim[k].z - r) * 0.6f);
    cap(rim, 12, v3(x, rim[0].y, r), v3(0, out, 0), t_dark, 0xffffff);
    cap(hub, 12, v3(x, hub[0].y, r), v3(0, out, 0), t_white, 0xc8c8d0);
}

static const float cab_sedan[4][5] = {{-0.56f, 0.40f, 0.37f, 0.44f, 0.47f}, {-0.38f, 0.40f, 0.32f, 0.44f, 0.73f}, {0.12f, 0.40f, 0.32f, 0.44f, 0.74f}, {0.44f, 0.40f, 0.37f, 0.44f, 0.47f}};
static const float cab_sport[4][5] = {{-0.62f, 0.40f, 0.36f, 0.38f, 0.42f}, {-0.36f, 0.40f, 0.30f, 0.38f, 0.61f}, {0.04f, 0.40f, 0.30f, 0.38f, 0.62f}, {0.42f, 0.40f, 0.37f, 0.38f, 0.40f}};
static const float cab_van[4][5] = {{-0.06f, 0.41f, 0.37f, 0.52f, 1.04f}, {0.10f, 0.41f, 0.36f, 0.52f, 1.05f}, {0.34f, 0.41f, 0.36f, 0.52f, 0.92f}, {0.58f, 0.41f, 0.38f, 0.52f, 0.56f}};

// speedboat: deep v hull, white deck, console with a windscreen, seats, outboard on the back
static void draw_boat(V3 at, float yaw, u32 paint, float roll, float pitch, int wreck) {
    place(at, yaw);
    m_pc = cosf_(pitch); m_ps = sinf_(pitch);
    m_rc = cosf_(roll); m_rs = sinf_(roll);
    static const float hs[6][3] = {{-1.12f, 0.46f, 0.30f}, {-0.6f, 0.5f, 0.32f}, {0.1f, 0.5f, 0.30f}, {0.6f, 0.42f, 0.2f}, {0.95f, 0.25f, 0.08f}, {1.2f, 0.03f, 0.01f}};
    V3 P[6 * 6], C[6];
    for (int i = 0; i < 6; i++) {
        float x = hs[i][0], hw = hs[i][1], cw = hs[i][2], keel = -0.07f + maxf(0, x - 0.2f) * 0.22f, deck = 0.30f + maxf(0, x) * 0.07f;
        V3 *q = &P[i * 6];
        q[0] = v3(x, hw, deck); q[1] = v3(x, cw, 0.05f); q[2] = v3(x, 0, keel);
        q[3] = v3(x, -cw, 0.05f); q[4] = v3(x, -hw, deck); q[5] = v3(x, 0, deck + 0.01f);
        C[i] = v3(x, 0, 0.14f);
    }
    u32 hull = wreck ? 0x303030 : paint;
    loft(P, C, 6, 6, t_white, hull);
    cap(&P[0], 6, C[0], v3(-1, 0, 0), t_white, hull);
    // deck and a stripe along the side
    for (int i = 0; i + 1 < 5; i++) {
        float x0 = hs[i][0], x1 = hs[i + 1][0], w0 = hs[i][1] - 0.05f, w1 = hs[i + 1][1] - 0.05f, z0 = 0.315f + maxf(0, x0) * 0.07f, z1 = 0.315f + maxf(0, x1) * 0.07f;
        mquad(v3(x0, -w0, z0), v3(x1, -w1, z1), v3(x1, w1, z1), v3(x0, w0, z0), v3(0, 0, 1), t_white, wreck ? 0x404040 : 0xf2efe6, 0);
    }
    mbox(v3(-0.05f, -0.2f, 0.31f), v3(0.2f, 0.2f, 0.62f), t_white, 0xe8e8ec);
    mquad(v3(0.2f, -0.24f, 0.62f), v3(0.36f, -0.24f, 0.44f), v3(0.36f, 0.24f, 0.44f), v3(0.2f, 0.24f, 0.62f), norm(v3(0.75f, 0, 0.66f)), t_carglass, 0xffffff, 1);
    mbox(v3(-0.62f, -0.36f, 0.31f), v3(-0.36f, 0.36f, 0.46f), t_white, 0xd8ccb0);
    mbox(v3(-0.66f, -0.36f, 0.31f), v3(-0.6f, 0.36f, 0.66f), t_white, 0xd8ccb0);
    mbox(v3(-1.26f, -0.08f, 0.3f), v3(-1.1f, 0.08f, 0.5f), t_white, 0x9a9ca4);
    mbox(v3(-1.22f, -0.035f, -0.1f), v3(-1.15f, 0.035f, 0.3f), t_white, 0x6a6c74);
}

static void draw_car(V3 at, float yaw, u32 paint, int cop, int kind, float steer, float roll, float pitch, int wreck, int tick) {
    place(at, yaw);
    m_pc = cosf_(pitch); m_ps = sinf_(pitch);
    m_rc = cosf_(roll); m_rs = sinf_(roll);
    float zs = kind == 3 ? 0.86f : kind == 2 ? 1.15f : 1.0f;
    static const float st[7][5] = {
        {-0.87f, 0.37f, 0.31f, 0.10f, 0.07f}, {-0.82f, 0.43f, 0.31f, 0.15f, 0.10f}, {-0.60f, 0.44f, 0.31f, 0.16f, 0.10f},
        {0.30f, 0.44f, 0.30f, 0.15f, 0.10f}, {0.62f, 0.43f, 0.27f, 0.12f, 0.09f}, {0.82f, 0.41f, 0.24f, 0.09f, 0.07f}, {0.88f, 0.35f, 0.23f, 0.06f, 0.05f}};
    V3 P[7 * 16], C[7];
    for (int i = 0; i < 7; i++) {
        float x = kind == 3 ? st[i][0] * 1.03f : st[i][0];
        rrect_ring(&P[i * 16], x, st[i][1], st[i][2] * zs, st[i][3] * zs, st[i][4]);
        C[i] = v3(x, 0, st[i][2] * zs);
    }
    loft(P, C, 7, 16, t_white, paint);
    cap(&P[0], 16, C[0], v3(-1, 0, 0), t_white, paint);
    cap(&P[6 * 16], 16, C[6], v3(1, 0, 0), t_white, paint);
    if (cop) {
        mbox(v3(-0.42f, -0.448f, 0.2f), v3(0.36f, -0.44f, 0.38f), t_dark, 0x707078);
        mbox(v3(-0.42f, 0.44f, 0.2f), v3(0.36f, 0.448f, 0.38f), t_dark, 0x707078);
    }
    const float (*cb)[5] = kind == 3 ? cab_sport : kind == 2 ? cab_van : cab_sedan;
    V3 Q[4 * 6], D[4];
    for (int i = 0; i < 4; i++) {
        float x = cb[i][0], bw = cb[i][1], tw = cb[i][2], z0 = cb[i][3], z1 = cb[i][4];
        V3 *q = &Q[i * 6];
        q[0] = v3(x, bw, z0); q[1] = v3(x, tw + 0.02f, z1 - 0.04f); q[2] = v3(x, tw - 0.04f, z1);
        q[3] = v3(x, -tw + 0.04f, z1); q[4] = v3(x, -tw - 0.02f, z1 - 0.04f); q[5] = v3(x, -bw, z0);
        D[i] = v3(x, 0, z0);
    }
    loft(Q, D, 4, 6, t_carglass, 0xffffff);
    if (kind == 2) {
        mbox(v3(-0.9f, -0.43f, 0.5f), v3(-0.05f, 0.43f, 1.07f), t_white, paint);
        mquad(v3(-0.05f, -0.33f, 1.052f), v3(0.11f, -0.33f, 1.052f), v3(0.11f, 0.33f, 1.052f), v3(-0.05f, 0.33f, 1.052f), v3(0, 0, 1), t_white, paint, 0);
    } else if (kind == 3) {
        mquad(v3(-0.32f, -0.27f, 0.612f), v3(0.0f, -0.27f, 0.612f), v3(0.0f, 0.27f, 0.612f), v3(-0.32f, 0.27f, 0.612f), v3(0, 0, 1), t_white, paint, 0);
        mbox(v3(-0.9f, -0.36f, 0.4f), v3(-0.84f, 0.36f, 0.43f), t_dark, 0x505058);
    } else
        mquad(v3(-0.34f, -0.29f, 0.742f), v3(0.08f, -0.29f, 0.742f), v3(0.08f, 0.29f, 0.742f), v3(-0.34f, 0.29f, 0.742f), v3(0, 0, 1), t_white, paint, 0);
    if (kind == 1) {
        mbox(v3(-0.2f, -0.13f, 0.742f), v3(-0.04f, 0.13f, 0.84f), t_taxi, 0xffffff);
        mbox(v3(-0.06f, -0.4f, 0.36f), v3(0.04f, 0.4f, 0.4f), t_dark, 0x202024);
    }
    float hz0 = 0.27f * zs, hz1 = 0.33f * zs, tz0 = 0.3f * zs, tz1 = 0.36f * zs;
    Tex *th = wreck ? t_dark : t_head, *tt = wreck ? t_dark : t_tail;
    mquad(v3(0.885f, -0.33f, hz0), v3(0.885f, -0.18f, hz0), v3(0.885f, -0.18f, hz1), v3(0.885f, -0.33f, hz1), v3(1, 0, 0), th, 0xffffff, 0);
    mquad(v3(0.885f, 0.18f, hz0), v3(0.885f, 0.33f, hz0), v3(0.885f, 0.33f, hz1), v3(0.885f, 0.18f, hz1), v3(1, 0, 0), th, 0xffffff, 0);
    mquad(v3(-0.875f, 0.33f, tz0), v3(-0.875f, 0.17f, tz0), v3(-0.875f, 0.17f, tz1), v3(-0.875f, 0.33f, tz1), v3(-1, 0, 0), tt, 0xffffff, 0);
    mquad(v3(-0.875f, -0.17f, tz0), v3(-0.875f, -0.33f, tz0), v3(-0.875f, -0.33f, tz1), v3(-0.875f, -0.17f, tz1), v3(-1, 0, 0), tt, 0xffffff, 0);
    if (cop) {
        int ph = (tick >> 3) & 1;
        mbox(v3(-0.16f, -0.26f, 0.742f), v3(-0.02f, 0, 0.81f), ph ? t_red : t_red_dim, 0xffffff);
        mbox(v3(-0.16f, 0, 0.742f), v3(-0.02f, 0.26f, 0.81f), ph ? t_blue_dim : t_blue, 0xffffff);
    }
    float wr = kind == 2 ? 0.18f : 0.17f, ww = kind == 3 ? 0.17f : 0.14f;
    m_pc = 1; m_ps = 0; m_rc = 1; m_rs = 0;
    wheel2(-0.55f, -0.40f, wr, ww);
    wheel2(-0.55f, 0.40f, wr, ww);
    V3 fw[2] = {mx(v3(0.55f, -0.40f, 0)), mx(v3(0.55f, 0.40f, 0))};
    for (int s = 0; s < 2; s++) {
        place(fw[s], yaw + steer);
        wheel2(0, s ? 0.0001f : -0.0001f, wr, ww);
    }
    place(at, yaw);
    // soft dark patch right under the body, darkens the ground instead of covering it
    Mat sh = {t_white, 0, 1, 1, 0, 1};
    V3 s0 = mx(v3(-0.8f, -0.4f, 0)), s1 = mx(v3(0.82f, -0.4f, 0)), s2 = mx(v3(0.82f, 0.4f, 0)), s3 = mx(v3(-0.8f, 0.4f, 0));
    s0.z = s1.z = s2.z = s3.z = at.z + 0.012f;
    quad(s0, s1, s2, s3, 1, 1, &sh);
}

// person about 0.9 tall, round limbs, phase swings arms and legs
static const u32 skins[6] = {0xf1c9a5, 0xe0ac84, 0xc68a5e, 0x9a6440, 0x6e4229, 0x4a2c1c};
static const u32 hairs[6] = {0x161210, 0x2e1c10, 0x5a3820, 0xc8a050, 0x8a8a8e, 0x7a3418};
static const u32 legs_c[6] = {0x2a3a5a, 0x1c1c22, 0xb0986a, 0xe4e0d8, 0x5a5a64, 0x3a4a2a};
static const u32 shoes_c[4] = {0xf0f0f0, 0x18181a, 0x5a3a24, 0xc83030};

static void sphere_n(V3 c, float r, float rz, float lat0, int R, int K, Tex *t, u32 tint) {
    V3 P[8 * 12], C[8];
    for (int i = 0; i < R; i++) {
        float la = lat0 + (PI / 2 - lat0) * i / (R - 1);
        if (i == R - 1) la = PI / 2 - 0.01f;
        for (int k = 0; k < K; k++) {
            float lo = k * TAU / K;
            P[i * K + k] = v3(c.x + cosf_(la) * cosf_(lo) * r, c.y + cosf_(la) * sinf_(lo) * r, c.z + sinf_(la) * rz);
        }
        C[i] = c;
    }
    loft(P, C, R, K, t, tint);
}

static void limb_n(V3 a, V3 b, float ra, float rb, int K, Tex *t, u32 tint) {
    V3 d = norm(sub(b, a));
    V3 u = absf(d.z) < 0.9f ? norm(cross(d, v3(0, 0, 1))) : norm(cross(d, v3(1, 0, 0)));
    V3 w = cross(d, u);
    V3 P[2 * 8], C[2] = {a, b};
    for (int k = 0; k < K; k++) {
        float an = k * TAU / K;
        V3 o = add(scl(u, cosf_(an)), scl(w, sinf_(an)));
        P[k] = add(a, scl(o, ra));
        P[K + k] = add(b, scl(o, rb));
    }
    loft(P, C, 2, K, t, tint);
    cap(&P[K], K, b, d, t, tint);
}

static V3 swing(V3 o, float ang, float len, float out) {
    return v3(o.x + sinf_(ang) * len, o.y + out, o.z - cosf_(ang) * len);
}

static void hand(V3 w, V3 dir, float side, u32 skin, int lod) {
    V3 palm = add(w, scl(dir, 0.085f));
    if (lod < 2) { sphere_n(add(w, scl(dir, 0.05f)), 0.04f, 0.05f, -PI / 2, 4, 6, t_white, skin); return; }
    limb_n(w, palm, 0.03f, 0.034f, 6, t_white, skin);
    V3 fw = norm(add(dir, v3(0.45f, 0, 0)));
    V3 across = cross(dir, v3(1, 0, 0));
    if (dot(across, across) < 0.01f) across = v3(0, 1, 0);
    across = norm(across);
    if (across.y < 0) across = scl(across, -1);
    for (int f = 0; f < 4; f++) {
        float o = (f - 1.5f) * 0.018f;
        V3 b0 = add(palm, scl(across, o));
        float len = f == 0 || f == 3 ? 0.05f : 0.06f;
        V3 b1 = add(b0, scl(dir, len * 0.55f));
        V3 b2 = add(b1, scl(fw, len * 0.5f));
        limb_n(b0, b1, 0.0095f, 0.0085f, 4, t_white, skin);
        limb_n(b1, b2, 0.0085f, 0.0075f, 4, t_white, skin);
    }
    V3 t0 = add(add(w, scl(dir, 0.04f)), scl(across, -side * 0.03f));
    V3 t1 = add(t0, add(scl(dir, 0.035f), v3(0.03f, -side * 0.012f, 0)));
    limb_n(t0, t1, 0.011f, 0.009f, 4, t_white, skin);
}

static void draw_person(V3 at, float yaw, u32 shirt, u32 look, float phase, int dead, int aim, float dist) {
    place(at, yaw);
    m_scale = 1.0f / 2.8f;
    if (dead) { m_pc = 0; m_ps = -1; m_o.z += 0.04f; }
    int lod = dist < 5 ? 2 : dist < 15 ? 1 : 0;
    int female = look & 1;
    u32 skin = skins[(look >> 1) % 6];
    u32 hair = hairs[(look >> 4) % 6];
    if (((look >> 4) % 6) == 4 && !(look & 64)) hair = hairs[0];
    int style = (look >> 8) & 3;
    u32 pants = legs_c[(look >> 11) % 6];
    int shorts = ((look >> 14) & 3) == 0, skirt = female && ((look >> 16) & 1);
    int sleeves = (look >> 17) & 1;
    u32 shoe = shoes_c[(look >> 18) & 3];
    int hat = !female && ((look >> 20) & 7) == 0;
    float sw = sinf_(phase) * 0.45f;
    float hipw = female ? 0.095f : 0.1f;
    for (int s = -1; s <= 1; s += 2) {
        float a1 = s * sw, bend = maxf(0, -s * sinf_(phase + 0.6f)) * 0.7f + 0.05f;
        V3 hip = v3(0, s * hipw, 0.9f);
        V3 knee = swing(hip, a1, 0.44f, 0);
        V3 ank = swing(knee, a1 - bend, 0.42f, 0);
        int K = lod ? 8 : 6;
        if (!skirt) limb_n(hip, knee, 0.085f, 0.062f, K, t_white, pants);
        else limb_n(hip, knee, 0.075f, 0.058f, K, t_white, skin);
        limb_n(knee, ank, 0.058f, 0.042f, K, t_white, shorts || skirt ? skin : pants);
        if (lod > 1) sphere_n(knee, 0.062f, 0.062f, -PI / 2, 4, 8, t_white, shorts || skirt ? skin : pants);
        if (shorts && !skirt) limb_n(hip, swing(hip, a1, 0.3f, 0), 0.092f, 0.075f, K, t_white, pants);
        V3 f0 = v3(ank.x - 0.05f, ank.y - 0.045f, maxf(ank.z - 0.06f, 0.0f)), f1 = v3(ank.x + 0.17f, ank.y + 0.045f, maxf(ank.z + 0.02f, 0.07f));
        mbox(f0, f1, t_white, shoe);
    }
    float waist = female ? 0.135f : 0.15f, hipr = female ? 0.18f : 0.165f, chest = female ? 0.17f : 0.2f;
    float tz[6][3] = {{0.84f, hipr, 0.11f}, {0.98f, hipr * 0.95f, 0.105f}, {1.08f, waist, 0.1f}, {1.25f, chest, 0.12f}, {1.4f, chest + 0.02f, 0.11f}, {1.47f, 0.11f, 0.075f}};
    int K = lod > 1 ? 12 : lod ? 10 : 8;
    V3 P[6 * 12], C[6];
    for (int i = 0; i < 6; i++) {
        for (int k = 0; k < K; k++) {
            float a = k * TAU / K;
            P[i * K + k] = v3(cosf_(a) * tz[i][2], sinf_(a) * tz[i][1], tz[i][0]);
        }
        C[i] = v3(0, 0, tz[i][0]);
    }
    loft(P, C, 2, K, t_white, skirt ? shirt : pants);
    loft(&P[K], &C[1], 5, K, t_white, shirt);
    cap(&P[5 * K], K, C[5], v3(0, 0, 1), t_white, shirt);
    if (skirt) {
        V3 Q[2 * 12], D[2] = {v3(0, 0, 0.98f), v3(0, 0, 0.55f)};
        for (int k = 0; k < K; k++) {
            float a = k * TAU / K;
            Q[k] = v3(cosf_(a) * 0.12f, sinf_(a) * hipr, 0.98f);
            Q[K + k] = v3(cosf_(a) * 0.2f, sinf_(a) * 0.27f, 0.55f);
        }
        loft(Q, D, 2, K, t_white, pants);
    }
    for (int s = -1; s <= 1; s += 2) {
        float a1 = -s * sw * 0.8f, a2 = 0.25f + maxf(0, s * sinf_(phase)) * 0.3f;
        float out = s * 0.035f;
        if (aim && s < 0) { a1 = 1.5f; a2 = 0.05f; out = 0.06f; }
        V3 sh = v3(0, s * (chest + 0.03f), 1.42f);
        V3 el = swing(sh, a1, 0.29f, out);
        V3 wr = swing(el, a1 + a2, 0.26f, out * 0.4f);
        int Ka = lod ? 8 : 6;
        if (sleeves) { limb_n(sh, el, 0.058f, 0.048f, Ka, t_white, shirt); limb_n(el, wr, 0.046f, 0.036f, Ka, t_white, shirt); }
        else {
            V3 mid = add(sh, scl(sub(el, sh), 0.45f));
            limb_n(sh, mid, 0.062f, 0.058f, Ka, t_white, shirt);
            limb_n(mid, el, 0.05f, 0.044f, Ka, t_white, skin);
            limb_n(el, wr, 0.044f, 0.033f, Ka, t_white, skin);
        }
        if (lod > 1) {
            sphere_n(sh, 0.066f, 0.066f, -PI / 2, 5, 8, t_white, shirt);
            sphere_n(el, 0.048f, 0.048f, -PI / 2, 4, 8, t_white, sleeves ? shirt : skin);
        }
        V3 dir = norm(sub(wr, el));
        hand(wr, dir, (float)s, skin, lod);
        if (aim && s < 0) {
            V3 g0 = add(wr, scl(dir, 0.06f)), g1 = add(wr, scl(dir, 0.22f));
            limb_n(g0, g1, 0.022f, 0.022f, 4, t_dark, 0x38383c);
            limb_n(add(g0, v3(0, 0, -0.01f)), add(g0, v3(-0.01f, 0, -0.1f)), 0.018f, 0.018f, 4, t_dark, 0x38383c);
        }
    }
    limb_n(v3(0, 0, 1.45f), v3(0.005f, 0, 1.57f), 0.05f, 0.048f, lod ? 8 : 6, t_white, skin);
    V3 hc = v3(0.01f, 0, 1.665f);
    if (lod > 1) sphere_n(hc, 0.1f, 0.118f, -PI / 2, 8, 12, t_white, skin);
    else if (lod) sphere_n(hc, 0.1f, 0.118f, -PI / 2, 6, 10, t_white, skin);
    else sphere_n(hc, 0.1f, 0.118f, -PI / 2, 5, 8, t_white, skin);
    if (lod) {
        for (int s = -1; s <= 1; s += 2) {
            if (lod > 1) sphere_n(v3(0.0f, s * 0.098f, 1.66f), 0.02f, 0.032f, -PI / 2, 4, 6, t_white, skin);
            if (lod > 1) {
                sphere_n(v3(0.082f, s * 0.036f, 1.68f), 0.017f, 0.012f, -PI / 2, 4, 6, t_white, 0xf4f4f0);
                sphere_n(v3(0.094f, s * 0.036f, 1.68f), 0.0085f, 0.0085f, -PI / 2, 3, 6, t_dark, 0x201814);
            } else mbox(v3(0.092f, s * 0.036f - 0.012f, 1.672f), v3(0.1f, s * 0.036f + 0.012f, 1.686f), t_dark, 0x201814);
            mbox(v3(0.085f, s * 0.04f - 0.022f, 1.703f), v3(0.1f, s * 0.04f + 0.022f, 1.713f), t_white, hair);
        }
        if (lod > 1) limb_n(v3(0.098f, 0, 1.69f), v3(0.125f, 0, 1.64f), 0.011f, 0.019f, 5, t_white, skin);
        mbox(v3(0.088f, -0.026f, 1.608f), v3(0.1f, 0.026f, 1.62f), t_white, female ? 0xb8404a : 0x8a4a40);
        if (!female && ((look >> 23) & 3) == 0) sphere_n(v3(0.04f, 0, 1.6f), 0.075f, 0.06f, -PI / 2, 5, 8, t_white, hair);
    }
    if (hat) {
        sphere_n(v3(0.0f, 0, 1.7f), 0.108f, 0.08f, 0.05f, 5, 10, t_white, shirt ^ 0x606060);
        mbox(v3(0.04f, -0.08f, 1.715f), v3(0.19f, 0.08f, 1.725f), t_white, shirt ^ 0x606060);
    } else if (style == 1 && !female) {
    } else {
        sphere_n(v3(-0.008f, 0, 1.675f), 0.108f, 0.122f, 0.12f, lod ? 6 : 4, lod ? 12 : 8, t_white, hair);
        if (female || style == 3) {
            V3 Q[2 * 8], D[2] = {v3(-0.06f, 0, 1.7f), v3(-0.08f, 0, female ? 1.36f : 1.55f)};
            for (int k = 0; k < 8; k++) {
                float a = PI * 0.5f + k * PI / 7;
                Q[k] = v3(-0.02f + cosf_(a) * 0.1f, sinf_(a) * 0.112f, 1.7f);
                Q[8 + k] = v3(-0.05f + cosf_(a) * 0.08f, sinf_(a) * 0.13f, female ? 1.36f : 1.55f);
            }
            loft(Q, D, 2, 8, t_white, hair);
        }
    }
    m_scale = 1;
    if (dead) {
        Mat bl = {t_white, 0x6a1018, 0, 0, 0};
        float r = 0.3f;
        quad(v3(at.x - r, at.y - r, at.z + 0.016f), v3(at.x + r, at.y - r, at.z + 0.016f), v3(at.x + r, at.y + r, at.z + 0.016f), v3(at.x - r, at.y + r, at.z + 0.016f), 1, 1, &bl);
    }
}

// palm with a bendy trunk and droopy leaves, h units tall, lod 1 is the cheap one for far away
static void draw_palm(V3 at, u32 seed_, float h, int lod) {
    float k3 = h / 3.0f, ls = clampf(0.75f + h * 0.08f, 0.8f, 1.3f);
    float lean = (0.08f + (seed_ & 7) * 0.012f) * k3, dir = (seed_ >> 3 & 63) * (TAU / 64);
    float lx = cosf_(dir), ly = sinf_(dir);
    V3 prev = at;
    float r = 0.11f * ls;
    int segs = lod ? 2 : 7, sides = lod ? 5 : 8;
    Mat m = {t_bark, base_tint(0xffffff), 0, 0, 1};
    float vp = 0;
    for (int i = 1; i <= segs; i++) {
        float t = i / (float)segs;
        V3 p = v3(at.x + lx * lean * t * t * 6, at.y + ly * lean * t * t * 6, at.z + t * h);
        float r2 = (0.11f - t * 0.04f) * ls, vt = t * h * 2.2f;
        for (int k = 0; k < sides; k++) {
            float a0 = k * (TAU / sides), a1 = (k + 1) * (TAU / sides), u0 = k * 0.5f, u1 = (k + 1) * 0.5f;
            V3 n0 = v3(cosf_(a0), sinf_(a0), 0), n1 = v3(cosf_(a1), sinf_(a1), 0);
            float l0 = lumf(n0), l1 = lumf(n1);
            V3 b0 = v3(prev.x + n0.x * r, prev.y + n0.y * r, prev.z);
            V3 b1 = v3(prev.x + n1.x * r, prev.y + n1.y * r, prev.z);
            V3 c1 = v3(p.x + n1.x * r2, p.y + n1.y * r2, p.z);
            V3 c0 = v3(p.x + n0.x * r2, p.y + n0.y * r2, p.z);
            tri_full(b0, b1, c1, u0, vp, u1, vp, u1, vt, l0, l1, l1, &m);
            tri_full(b0, c1, c0, u0, vp, u1, vt, u0, vt, l0, l1, l0, &m);
        }
        prev = p;
        r = r2;
        vp = vt;
    }
    V3 top = prev;
    int nl = lod ? 6 : 9;
    for (int k = 0; k < nl; k++) {
        float a = k * (TAU / nl) + (seed_ & 15) * 0.1f;
        float ca = cosf_(a) * ls, sa = sinf_(a) * ls;
        V3 side = v3(-sinf_(a), cosf_(a), 0);
        // leaf goes up a bit then droops
        V3 pts[5] = {top,
                     add(top, v3(ca * 0.45f, sa * 0.45f, 0.18f * ls)),
                     add(top, v3(ca * 0.9f, sa * 0.9f, 0.12f * ls)),
                     add(top, v3(ca * 1.25f, sa * 1.25f, -0.15f * ls)),
                     add(top, v3(ca * 1.45f, sa * 1.45f, -0.55f * ls))};
        float wdt[5] = {0.03f, 0.22f, 0.24f, 0.16f, 0.02f};
        int step = lod ? 2 : 1;
        for (int i = 0; i + step <= 4; i += step) {
            float w0 = wdt[i] * ls, w1 = wdt[i + step] * ls;
            if (lod && i == 0) w0 = 0.18f * ls;
            V3 a0 = sub(pts[i], scl(side, w0)), a1 = add(pts[i], scl(side, w0));
            V3 b0 = sub(pts[i + step], scl(side, w1)), b1 = add(pts[i + step], scl(side, w1));
            V3 n = norm(cross(sub(b0, a0), side));
            if (n.z < 0) n = scl(n, -1);
            Mat m = {t_leaf, light(0xffffff, n), 1, 0};
            quad(a0, a1, b1, b0, 1, 1, &m);
        }
    }
}

static const u32 beach_cols[8] = {0xf28cb4, 0x4fd0c8, 0xf5d04a, 0xb79cf0, 0xf59a50, 0x8fe0a0, 0x5aa8f0, 0xf26a6a};

static void draw_tower(V3 at, float yaw, u32 seed, int lod) {
    place(at, yaw);
    u32 c1 = beach_cols[seed & 7], c2 = beach_cols[(seed >> 3) % 7 + ((seed & 7) == 7 ? 0 : 1)];
    if (c2 == c1) c2 = 0xf4f0e8;
    u32 wood = 0xe8e2d4;
    for (int q = 0; q < 4; q++) {
        float x = (q & 1) ? 0.36f : -0.4f, y = (q & 2) ? 0.36f : -0.4f;
        mbox(v3(x, y, 0), v3(x + 0.05f, y + 0.05f, 0.5f), t_white, wood);
    }
    mbox(v3(-0.5f, -0.5f, 0.5f), v3(0.62f, 0.5f, 0.55f), t_white, wood);
    mbox(v3(-0.42f, -0.42f, 0.55f), v3(0.3f, 0.42f, 1.15f), t_white, c1);
    mbox(v3(-0.43f, -0.43f, 0.74f), v3(0.31f, 0.43f, 0.8f), t_white, c2);
    mbox(v3(0.3f, -0.3f, 0.86f), v3(0.315f, 0.3f, 1.06f), t_dark, 0x2a3440);
    mbox(v3(-0.52f, -0.52f, 1.15f), v3(0.42f, 0.52f, 1.21f), t_white, c2);
    if (!lod) {
        mbox(v3(0.59f, -0.5f, 0.55f), v3(0.62f, 0.5f, 0.72f), t_white, wood);
        mbox(v3(0.3f, -0.5f, 0.69f), v3(0.62f, -0.47f, 0.72f), t_white, wood);
        mbox(v3(0.3f, 0.47f, 0.69f), v3(0.62f, 0.5f, 0.72f), t_white, wood);
        mquad(v3(-0.5f, 0.15f, 0.53f), v3(-0.5f, -0.15f, 0.53f), v3(-1.45f, -0.15f, 0.01f), v3(-1.45f, 0.15f, 0.01f), v3(0.48f, 0, 0.88f), t_white, wood, 1);
        mbox(v3(0.0f, 0.3f, 1.21f), v3(0.025f, 0.325f, 1.62f), t_white, 0xf0f0f0);
        mquad(v3(0.012f, 0.32f, 1.62f), v3(0.012f, 0.62f, 1.6f), v3(0.012f, 0.62f, 1.46f), v3(0.012f, 0.32f, 1.48f), v3(1, 0, 0), t_white, (seed >> 6 & 1) ? 0xf5d020 : 0xe83030, 1);
    }
}

static void draw_umbrella(V3 at, u32 seed, int lod) {
    place(at, (seed & 63) * (TAU / 64));
    u32 c1 = beach_cols[seed >> 6 & 7], c2 = (seed >> 9 & 1) ? 0xf8f6f0 : beach_cols[(seed >> 10) & 7];
    mbox(v3(-0.012f, -0.012f, 0), v3(0.012f, 0.012f, 0.74f), t_white, 0xf0f0f0);
    V3 top = v3(0, 0, 0.8f);
    int K = lod ? 6 : 10;
    for (int k = 0; k < K; k++) {
        float a0 = k * TAU / K, a1 = (k + 1) * TAU / K;
        V3 p0 = v3(cosf_(a0) * 0.55f, sinf_(a0) * 0.55f, 0.62f), p1 = v3(cosf_(a1) * 0.55f, sinf_(a1) * 0.55f, 0.62f);
        V3 n = norm(v3(cosf_((a0 + a1) / 2) * 0.18f, sinf_((a0 + a1) / 2) * 0.18f, 0.55f));
        Mat m = {t_white, light(k & 1 ? c2 : c1, mn(n)), 1, 0};
        tri(mx(top), mx(p0), mx(p1), 0, 0, 1, 0, 1, 1, &m);
    }
    if (lod) return;
    if (seed >> 13 & 1) {
        mbox(v3(0.15f, -0.12f, 0.06f), v3(0.75f, 0.12f, 0.09f), t_white, 0xf4f4f4);
        mquad(v3(0.6f, -0.12f, 0.09f), v3(0.82f, -0.12f, 0.26f), v3(0.82f, 0.12f, 0.26f), v3(0.6f, 0.12f, 0.09f), v3(-0.6f, 0, 0.8f), t_white, 0xf4f4f4, 1);
    } else
        mquad(v3(0.12f, -0.13f, 0.012f), v3(0.78f, -0.13f, 0.012f), v3(0.78f, 0.13f, 0.012f), v3(0.12f, 0.13f, 0.012f), v3(0, 0, 1), t_white, beach_cols[(seed >> 14) & 7], 0);
}

// one triangle of a round thing, turned so it faces away from the middle
static void out_tri(V3 a, V3 b, V3 c, float la, float lb, float lc, V3 mid, Mat *m) {
    V3 f = cross(sub(b, a), sub(c, a));
    if (dot(f, sub(scl(add(add(a, b), c), 1.0f / 3), mid)) < 0) tri_s(a, c, b, la, lc, lb, m);
    else tri_s(a, b, c, la, lb, lc, m);
}

// broad leaf tree, a lumpy round crown on a short trunk, h units tall and r wide
static const u32 crown_cols[6] = {0x4a7a3a, 0x3c6c34, 0x5a8a42, 0x447a4c, 0x68883c, 0x36603a};
static void draw_broad(V3 at, u32 seed, float h, float r, int lod) {
    float zb = h * (0.24f + (seed & 7) * 0.02f), hz = (h - zb) * 0.5f, zc = zb + hz * 0.9f;
    float lx = ((seed >> 3 & 15) - 7.5f) * 0.008f * h, ly = ((seed >> 7 & 15) - 7.5f) * 0.008f * h;
    V3 c = v3(at.x + lx, at.y + ly, at.z + zc);
    int ts = lod ? 3 : 5;
    float tr = minf(0.07f + h * 0.014f, 0.22f);
    Mat bm = {t_bark, base_tint(0xb0a090), 0, 0, 1};
    V3 tt = v3(at.x + lx * 0.7f, at.y + ly * 0.7f, at.z + zb + hz * 0.4f);
    for (int k = 0; k < ts; k++) {
        float a0 = k * TAU / ts, a1 = (k + 1) * TAU / ts;
        V3 n0 = v3(cosf_(a0), sinf_(a0), 0), n1 = v3(cosf_(a1), sinf_(a1), 0);
        V3 b0 = add(at, scl(n0, tr)), b1 = add(at, scl(n1, tr)), t0 = add(tt, scl(n0, tr * 0.6f)), t1 = add(tt, scl(n1, tr * 0.6f));
        float l0 = lumf(n0), l1 = lumf(n1);
        out_tri(b0, b1, t1, l0, l1, l1, v3(at.x, at.y, b0.z), &bm);
        out_tri(b0, t1, t0, l0, l1, l0, v3(tt.x, tt.y, t0.z), &bm);
    }
    Mat cm = {t_crown, base_tint(crown_cols[(seed >> 11) % 6]), 0, 0, 1};
    enum { KMAX = 8, RMAX = 3 };
    static const float rz[RMAX] = {-0.7f, -0.1f, 0.5f}, rr[RMAX] = {0.72f, 1.0f, 0.82f};
    static const float fz[2] = {-0.4f, 0.4f}, fr[2] = {0.95f, 0.85f};
    int K = lod ? 5 : 8, R = lod ? 2 : 3;
    V3 P[RMAX][KMAX];
    float L[RMAX][KMAX];
    for (int i = 0; i < R; i++)
        for (int k = 0; k < K; k++) {
            float a = (k + (i & 1) * 0.5f) * TAU / K + (seed >> 14 & 7) * 0.3f;
            u32 q = hash2((int)(seed & 0xffff) + k * 7, i * 13 + (int)(seed >> 16));
            float j = 0.82f + (q & 255) / 900.0f, zj = ((q >> 8 & 255) / 255.0f - 0.5f) * 0.25f;
            float ri = r * (lod ? fr[i] : rr[i]) * j, zi = hz * ((lod ? fz[i] : rz[i]) + zj);
            P[i][k] = v3(c.x + cosf_(a) * ri, c.y + sinf_(a) * ri, c.z + zi);
            L[i][k] = lumf(norm(v3(cosf_(a) * ri / (r * r), sinf_(a) * ri / (r * r), zi / (hz * hz))));
        }
    V3 top = v3(c.x, c.y, c.z + hz * 0.82f), bot = v3(c.x, c.y, c.z - hz * (lod ? 0.6f : 0.85f));
    float lt = lumf(v3(0, 0, 1)), lb = lumf(v3(0, 0, -1));
    for (int k = 0; k < K; k++) {
        int n = (k + 1) % K;
        for (int i = 0; i + 1 < R; i++) {
            out_tri(P[i][k], P[i][n], P[i + 1][n], L[i][k], L[i][n], L[i + 1][n], c, &cm);
            out_tri(P[i][k], P[i + 1][n], P[i + 1][k], L[i][k], L[i + 1][n], L[i + 1][k], c, &cm);
        }
        out_tri(P[R - 1][k], P[R - 1][n], top, L[R - 1][k], L[R - 1][n], lt, c, &cm);
        out_tri(P[0][n], P[0][k], bot, L[0][n], L[0][k], lb, c, &cm);
    }
}
