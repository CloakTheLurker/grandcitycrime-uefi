// the world, 40 by 32 blocks of 8 units
// mainland west, a bay with islands and causeways, a beach island east, sea round it all
// every land block has a road on its west and south side and a raised plot with buildings
#define BXN 40
#define BYN 32
#define WX (BXN * 8)
#define WY (BYN * 8)
#define PLOT0 2.625f
#define PLOT1 8.375f
#define CURB 0.1f
#define SEA_Z -0.35f

enum { T_WATER, T_CITY, T_BEACH, T_PORT, T_PARK, T_BRIDGE };
static u8 tiles[BYN][BXN];
static int tile_at(int bx, int by) {
    if (bx < 0 || by < 0 || bx >= BXN || by >= BYN) return T_WATER;
    return tiles[by][bx];
}
static const int bridge_rows[3] = {12, 18, 26};
#define ISLE_X0 30
#define ISLE_X1 34
#define DOWNTOWN_X 13.0f
#define DOWNTOWN_Y 18.0f

// where the mainland ends on each row, a bit ragged
static int coast(int by) {
    if (by >= 3 && by <= 8) return 19;
    return 17 + (int)(hash2(by, 31) % 3);
}

static void plan_tiles(void) {
    for (int by = 0; by < BYN; by++)
        for (int bx = 0; bx < BXN; bx++) {
            int t = T_WATER;
            if (by >= 1 && by <= BYN - 2 && bx < coast(by)) t = T_CITY;
            if (by >= 3 && by <= 8 && bx >= 15 && bx < 19) t = T_PORT;
            if (by >= 3 && by <= BYN - 4 && bx >= ISLE_X0 && bx < ISLE_X1) t = T_CITY;
            if (by >= 3 && by <= BYN - 4 && bx == ISLE_X1) t = T_BEACH;
            tiles[by][bx] = (u8)t;
        }
    // parks, one big one and a few small
    for (int by = 21; by <= 22; by++)
        for (int bx = 5; bx <= 6; bx++) tiles[by][bx] = T_PARK;
    for (int by = 1; by < BYN - 1; by++)
        for (int bx = 0; bx < BXN; bx++) {
            float dx = bx - DOWNTOWN_X, dy = by - DOWNTOWN_Y;
            if (tiles[by][bx] == T_CITY && dx * dx + dy * dy > 20 && hash2(bx, by * 3 + 1) % 100 < 6) tiles[by][bx] = T_PARK;
        }
    // causeways over the bay
    for (int i = 0; i < 3; i++)
        for (int bx = 0; bx < BXN; bx++)
            if (tiles[bridge_rows[i]][bx] == T_WATER && bx > 10 && bx < ISLE_X0) tiles[bridge_rows[i]][bx] = T_BRIDGE;
}

typedef struct {
    float x0, y0, x1, y1, h;          // base, h is the top of it
    float tx0, ty0, tx1, ty1, th;     // tower on top, th 0 means none
    u32 tint, tint2;
    Tex *face, *face2;
    float u0, v0;                     // where in the facade it starts so lit windows dont line up
    int roof, shops, shape, tshape;
    u32 dome, neon;
} Building;
#define MAX_B 8
typedef struct { Building b[MAX_B]; int n; } Block;
static Block blocks[BYN][BXN];

static u32 pastels[] = {0xf7a8c4, 0x9fe3d0, 0xf4e3c1, 0xf0f0f4, 0xffc49a, 0xc8b3f0, 0xa8d8f7, 0xf6eaa0, 0xffb0a0, 0xb8f0c0, 0xe8d0ff, 0xfff0d8};
static u32 earthy[] = {0xffffff, 0xf0e0d0, 0xe0d0c0, 0xfff4e8, 0xd8c8c0};
static u32 greys[] = {0xe8e8ec, 0xd0d4dc, 0xc8c0b8, 0xf0ece4};

static u32 city_r;
static u32 dome_cols[];
static Mat mat(Tex *t, u32 tint) { Mat m = {t, tint, 0, 0}; return m; }
static float r01(void) {
    city_r ^= city_r << 13; city_r ^= city_r >> 17; city_r ^= city_r << 5;
    return (city_r & 0xffffff) / 16777216.0f;
}
static int rint_(int n) { return (int)(r01() * n); }

// cut the block into lots, each split leaves a little alley
static int cut_lots(float x0, float y0, float x1, float y1, int depth, float out[][4], int n) {
    float w = x1 - x0, d = y1 - y0;
    int can_x = w > 2.6f, can_y = d > 2.6f;
    if (n < MAX_B - 1 && depth > 0 && (can_x || can_y) && r01() < 0.8f) {
        int along_x = can_x && (!can_y || (w > d ? r01() < 0.75f : r01() < 0.25f));
        float t = 0.32f + r01() * 0.36f, gap = r01() < 0.4f ? 0.08f : 0.15f + r01() * 0.35f;
        if (along_x) {
            float m = x0 + w * t;
            n = cut_lots(x0, y0, m - gap / 2, y1, depth - 1, out, n);
            return cut_lots(m + gap / 2, y0, x1, y1, depth - 1, out, n);
        }
        float m = y0 + d * t;
        n = cut_lots(x0, y0, x1, m - gap / 2, depth - 1, out, n);
        return cut_lots(x0, m + gap / 2, x1, y1, depth - 1, out, n);
    }
    if (n >= MAX_B) return n;
    out[n][0] = x0; out[n][1] = y0; out[n][2] = x1; out[n][3] = y1;
    return n + 1;
}

static void plan_port(int bx, int by);
static void plan_islands(void);
static u32 neons[] = {0xff3da0, 0x30e8e0, 0xb060ff, 0xffd040};

static void plan_city(void) {
    plan_tiles();
    for (int by = 0; by < BYN; by++)
        for (int bx = 0; bx < BXN; bx++) {
            Block *k = &blocks[by][bx];
            k->n = 0;
            if (tiles[by][bx] == T_PORT) { city_r = hash2(bx, by) | 1; plan_port(bx, by); continue; }
            if (tiles[by][bx] != T_CITY) continue;
            city_r = hash2(bx * 3 + 11, by * 7 + 5) | 1;
            for (int i = 0; i < 3; i++) r01();
            float X = bx * 8.0f, Y = by * 8.0f;
            float lots[MAX_B][4];
            int n = cut_lots(X + 3.0f, Y + 3.0f, X + 8.0f, Y + 8.0f, 1 + rint_(3), lots, 0);
            // near the beach keep it low and pastel like ocean drive hotels
            int beachy = bx >= ISLE_X0;
            float dx = bx - DOWNTOWN_X, dy = by - DOWNTOWN_Y;
            float downtown = clampf(1.0f - (dx * dx + dy * dy) / 26.0f, 0, 1);
            for (int i = 0; i < n; i++) {
                if (n > 2 && r01() < 0.1f) continue;   // empty lot, little plaza
                Building *b = &k->b[k->n++];
                float x0 = lots[i][0], y0 = lots[i][1], x1 = lots[i][2], y1 = lots[i][3];
                // pushed back from the sidewalk a random bit on each side
                float in[4];
                for (int j = 0; j < 4; j++) in[j] = r01() < 0.5f ? 0 : r01() * 0.45f;
                if (x1 - x0 - in[0] - in[2] < 1.2f) in[0] = in[2] = 0;
                if (y1 - y0 - in[1] - in[3] < 1.2f) in[1] = in[3] = 0;
                b->x0 = x0 + in[0]; b->y0 = y0 + in[1]; b->x1 = x1 - in[2]; b->y1 = y1 - in[3];
                int style;
                if (beachy) { int s3[3] = {0, 1, 4}; style = s3[rint_(3)]; }
                else if (r01() < downtown * 0.7f) style = r01() < 0.6f ? 2 : 5;
                else style = rint_(6);
                Tex *faces[6] = {t_hotel, t_deco, t_glass, t_brick, t_stucco, t_concrete};
                b->face = faces[style];
                int floors;
                switch (style) {
                case 2: floors = 7 + rint_(10) + (int)(downtown * 14); break;
                case 5: floors = 4 + rint_(9) + (int)(downtown * 8); break;
                case 3: floors = 2 + rint_(7); break;
                default: floors = 2 + rint_(8);
                }
                if (beachy) floors = 2 + rint_(5);
                b->shops = r01() < 0.7f;
                b->h = CURB + (b->shops ? 1.0f : 0) + floors;
                b->tint = style == 2 ? (r01() < 0.5f ? 0xd8e8f4 : 0xc8f0e0) : style == 3 ? earthy[rint_(5)] : style == 5 ? greys[rint_(4)] : pastels[rint_(12)];
                b->u0 = (float)rint_(4) / 4;
                b->v0 = (float)rint_(4) / 4;
                // stepped tower on the bigger ones
                b->th = 0;
                float bw = b->x1 - b->x0, bd = b->y1 - b->y0;
                if (floors >= 6 && bw > 1.6f && bd > 1.6f && r01() < 0.4f) {
                    float s = 0.2f + r01() * 0.25f;
                    b->tx0 = b->x0 + bw * s * r01() * 2; b->tx1 = b->x1 - bw * s * r01() * 2;
                    b->ty0 = b->y0 + bd * s * r01() * 2; b->ty1 = b->y1 - bd * s * r01() * 2;
                    if (b->tx1 - b->tx0 < 0.8f) { b->tx0 = b->x0 + bw * 0.25f; b->tx1 = b->x1 - bw * 0.25f; }
                    if (b->ty1 - b->ty0 < 0.8f) { b->ty0 = b->y0 + bd * 0.25f; b->ty1 = b->y1 - bd * 0.25f; }
                    b->th = b->h + 2 + rint_(style == 2 ? 12 : 7);
                    b->face2 = r01() < 0.6f ? b->face : t_glass;
                    b->tint2 = b->face2 == t_glass ? 0xd8e8f4 : b->tint;
                }
                // round towers and round corners, not everything is a box
                float sq = bw / bd;
                b->shape = 0;
                float pr = r01();
                if (sq > 0.6f && sq < 1.7f && bw > 1.5f && pr < (style == 2 ? 0.35f : 0.22f)) b->shape = 1;
                else if (pr < (beachy || style == 0 || style == 1 ? 0.75f : 0.35f)) b->shape = 2;
                b->tshape = b->th ? (r01() < 0.45f ? 1 : r01() < 0.5f ? 2 : 0) : 0;
                b->roof = rint_(4);
                int top_round = b->th ? b->tshape == 1 : b->shape == 1;
                if (top_round && r01() < 0.6f) b->roof = 4;
                else if (style == 2 && r01() < 0.25f) b->roof = 5;
                else if (r01() < 0.08f) b->roof = 4;
                b->dome = dome_cols[rint_(5)];
                // neon trim on the beach hotels, its miami after all
                b->neon = beachy && r01() < 0.65f ? neons[rint_(4)] : 0;
            }
        }
    plan_islands();
}

static void quad_uv(V3 a, V3 b, V3 c, V3 d, float u0, float v0, float u1, float v1, Mat *m) {
    tri(a, b, c, u0, v1, u1, v1, u1, v0, m);
    tri(a, c, d, u0, v1, u1, v0, u0, v0, m);
}

// outline of a building seen from above, counter clockwise
// shape 0 box, 1 round, 2 box with round corners
#define MAXO 32
static int outline(int shape, float x0, float y0, float x1, float y1, float px[], float py[]) {
    float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, rx = (x1 - x0) / 2, ry = (y1 - y0) / 2;
    int n = 0;
    if (shape == 1) {
        int segs = 20;
        for (int i = 0; i < segs; i++) {
            float a = i * TAU / segs;
            px[n] = cx + rx * cosf_(a);
            py[n++] = cy + ry * sinf_(a);
        }
        return n;
    }
    if (shape == 2) {
        float r = minf(minf(rx, ry) * 0.9f, 0.7f);
        float ccx[4] = {x1 - r, x1 - r, x0 + r, x0 + r}, ccy[4] = {y0 + r, y1 - r, y1 - r, y0 + r};
        for (int c = 0; c < 4; c++)
            for (int i = 0; i <= 4; i++) {
                float a = -PI / 2 + c * PI / 2 + i * (PI / 8);
                px[n] = ccx[c] + r * cosf_(a);
                py[n++] = ccy[c] + r * sinf_(a);
            }
        return n;
    }
    px[0] = x0; py[0] = y0; px[1] = x1; py[1] = y0; px[2] = x1; py[2] = y1; px[3] = x0; py[3] = y1;
    return 4;
}

// walls round any outline, shop floor at the bottom if asked, ledge and roof on top
static Tex *neon_tex(u32 c);
static void draw_tier(int shape, float x0, float y0, float x1, float y1, float zb, float zt, Tex *face, u32 tint, float u0, float v0, int shops, u32 neon) {
    float px[MAXO], py[MAXO];
    int n = outline(shape, x0, y0, x1, y1, px, py);
    float zs = shops ? zb + 1.0f : zb;
    float ustart = u0;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        float dx = px[j] - px[i], dy = py[j] - py[i];
        float len = sqrtf_(dx * dx + dy * dy);
        if (len < 1e-4f) continue;
        float ua = ustart, ub = ustart + len / 4.0f;
        ustart = ub;
        V3 nrm = v3(dy / len, -dx / len, 0);
        V3 a = v3(px[i], py[i], 0), c = v3(px[j], py[j], 0);
        V3 mid = scl(add(a, c), 0.5f);
        if (dot(sub(cam, mid), nrm) <= 0) continue;
        u32 lt = light(tint, nrm);
        if (shops) {
            Mat shop = mat(t_shop, lt);
            V3 a0 = a, c0 = c, a1 = a, c1 = c;
            a0.z = c0.z = zb; a1.z = c1.z = zs;
            quad_uv(a0, c0, c1, a1, ua, 0, ub, 1, &shop);
        }
        Mat up = mat(face, lt);
        V3 a1 = a, c1 = c, a2 = a, c2 = c;
        a1.z = c1.z = zs; a2.z = c2.z = zt;
        quad_uv(a1, c1, c2, a2, ua, v0, ub, v0 + (zt - zs) / 4.0f, &up);
        // ledge sticks out a bit, round ones dont need the end overhang
        float e = 0.08f, ov = shape ? 0.0f : e;
        V3 along = scl(v3(dx, dy, 0), 1 / len);
        V3 la = sub(add(a, scl(nrm, e)), scl(along, ov)), lc = add(add(c, scl(nrm, e)), scl(along, ov));
        la.z = lc.z = zt - 0.12f;
        V3 la2 = la, lc2 = lc;
        la2.z = lc2.z = zt + 0.03f;
        Mat lm = neon ? mat(neon_tex(neon), 0x808080) : mat(t_curb, light(tint, nrm));
        quad(la, lc, lc2, la2, 1, 1, &lm);
        if (neon && shops) {
            // second neon line over the shops
            V3 sa = la, sc = lc, sa2 = la, sc2 = lc;
            sa.z = sc.z = zs - 0.02f; sa2.z = sc2.z = zs + 0.06f;
            quad(sa, sc, sc2, sa2, 1, 1, &lm);
        }
    }
    if (cam.z > zt) {
        Mat r = mat(t_roof, light(0xffffff, v3(0, 0, 1)));
        float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, z = zt + 0.03f;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            tri(v3(cx, cy, z), v3(px[i], py[i], z), v3(px[j], py[j], z), cx, cy, px[i], py[i], px[j], py[j], &r);
        }
    }
}

// part of a sphere, lat from phi0 to phi1 in radians, squashed by rx ry rz
static void ball(V3 c, float rx, float ry, float rz, float phi0, float phi1, int rings, int segs, Tex *t, u32 tint) {
    for (int k = 0; k < rings; k++) {
        float f0 = phi0 + (phi1 - phi0) * k / rings, f1 = phi0 + (phi1 - phi0) * (k + 1) / rings;
        float c0 = cosf_(f0), s0 = sinf_(f0), c1 = cosf_(f1), s1 = sinf_(f1);
        for (int i = 0; i < segs; i++) {
            float t0 = i * TAU / segs, t1 = (i + 1) * TAU / segs;
            float ct0 = cosf_(t0), st0 = sinf_(t0), ct1 = cosf_(t1), st1 = sinf_(t1);
            V3 a = v3(c.x + rx * c0 * ct0, c.y + ry * c0 * st0, c.z + rz * s0);
            V3 b = v3(c.x + rx * c0 * ct1, c.y + ry * c0 * st1, c.z + rz * s0);
            V3 d = v3(c.x + rx * c1 * ct0, c.y + ry * c1 * st0, c.z + rz * s1);
            V3 e = v3(c.x + rx * c1 * ct1, c.y + ry * c1 * st1, c.z + rz * s1);
            float fm = (f0 + f1) / 2, tm = (t0 + t1) / 2;
            V3 n = norm(v3(cosf_(fm) * cosf_(tm) / rx, cosf_(fm) * sinf_(tm) / ry, sinf_(fm) / rz));
            Mat m = mat(t, light(tint, n));
            quad(a, b, e, d, 1, 1, &m);
        }
    }
}

static u32 dome_cols[] = {0x6ac0a8, 0xe8c060, 0xf4f4f4, 0xf7a8c4, 0x7ab0e8};

static void box(V3 lo, V3 hi, Tex *t, u32 tint, int bottom);

static void roof_stuff(Building *b) {
    float x0 = b->th ? b->tx0 : b->x0, y0 = b->th ? b->ty0 : b->y0, x1 = b->th ? b->tx1 : b->x1, y1 = b->th ? b->ty1 : b->y1;
    float z = (b->th ? b->th : b->h) + 0.03f;
    float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    int shp = b->th ? b->tshape : b->shape;
    if (shp) { float kx = (x1 - x0) * 0.15f, ky = (y1 - y0) * 0.15f; x0 += kx; x1 -= kx; y0 += ky; y1 -= ky; }
    float rx = (x1 - x0) / 2, ry = (y1 - y0) / 2;
    switch (b->roof) {
    case 4: // dome
        if (shp == 1) { rx = (x1 - x0) / 2 / 0.7f * 0.92f; ry = (y1 - y0) / 2 / 0.7f * 0.92f; }
        else { rx *= 0.8f; ry *= 0.8f; }
        ball(v3(cx, cy, z), rx, ry, minf(rx, ry) * 1.1f, 0, PI / 2, 5, 16, t_white, b->dome);
        box(v3(cx - 0.05f, cy - 0.05f, z + minf(rx, ry) * 1.1f - 0.05f), v3(cx + 0.05f, cy + 0.05f, z + minf(rx, ry) * 1.1f + 0.5f), t_curb, 0xe0d0a0, 0);
        break;
    case 5: // spire with a ball on top
        box(v3(cx - 0.1f, cy - 0.1f, z), v3(cx + 0.1f, cy + 0.1f, z + 1.6f), t_curb, 0xe8e8f0, 0);
        ball(v3(cx, cy, z + 1.95f), 0.38f, 0.38f, 0.38f, -PI / 2, PI / 2, 7, 14, t_white, b->dome);
        break;
    case 1: // ac units
        box(v3(x0 + 0.2f, y0 + 0.2f, z), v3(x0 + 0.6f, y0 + 0.5f, z + 0.3f), t_curb, 0xc0c0c8, 0);
        box(v3(x1 - 0.7f, y1 - 0.6f, z), v3(x1 - 0.25f, y1 - 0.25f, z + 0.35f), t_curb, 0xc0c0c8, 0);
        break;
    case 2: // water tower on legs
        box(v3(cx - 0.32f, cy - 0.32f, z), v3(cx - 0.26f, cy - 0.26f, z + 0.6f), t_dark, 0xffffff, 0);
        box(v3(cx + 0.26f, cy + 0.26f, z), v3(cx + 0.32f, cy + 0.32f, z + 0.6f), t_dark, 0xffffff, 0);
        box(v3(cx - 0.32f, cy + 0.26f, z), v3(cx - 0.26f, cy + 0.32f, z + 0.6f), t_dark, 0xffffff, 0);
        box(v3(cx + 0.26f, cy - 0.32f, z), v3(cx + 0.32f, cy - 0.26f, z + 0.6f), t_dark, 0xffffff, 0);
        box(v3(cx - 0.4f, cy - 0.4f, z + 0.6f), v3(cx + 0.4f, cy + 0.4f, z + 1.4f), t_bark, 0xffffff, 1);
        break;
    case 3: // antenna with a red light
        box(v3(cx - 0.04f, cy - 0.04f, z), v3(cx + 0.04f, cy + 0.04f, z + 2.2f), t_curb, 0xd0d0d0, 0);
        box(v3(cx - 0.07f, cy - 0.07f, z + 2.2f), v3(cx + 0.07f, cy + 0.07f, z + 2.34f), t_red, 0xffffff, 0);
        break;
    }
}

static void draw_building(Building *b) {
    draw_tier(b->shape, b->x0, b->y0, b->x1, b->y1, CURB, b->h, b->face, b->tint, b->u0, b->v0, b->shops, b->neon);
    if (b->th) draw_tier(b->tshape, b->tx0, b->ty0, b->tx1, b->ty1, b->h, b->th, b->face2, b->tint2, b->v0, b->u0, 0, b->neon);
    roof_stuff(b);
}

// box with all 5 visible sides, used for curbs, penthouses, models
static void box(V3 lo, V3 hi, Tex *t, u32 tint, int bottom) {
    V3 n[6] = {v3(0, -1, 0), v3(1, 0, 0), v3(0, 1, 0), v3(-1, 0, 0), v3(0, 0, 1), v3(0, 0, -1)};
    float x0 = lo.x, y0 = lo.y, z0 = lo.z, x1 = hi.x, y1 = hi.y, z1 = hi.z;
    V3 q[6][4] = {
        {v3(x0, y0, z0), v3(x1, y0, z0), v3(x1, y0, z1), v3(x0, y0, z1)},
        {v3(x1, y0, z0), v3(x1, y1, z0), v3(x1, y1, z1), v3(x1, y0, z1)},
        {v3(x1, y1, z0), v3(x0, y1, z0), v3(x0, y1, z1), v3(x1, y1, z1)},
        {v3(x0, y1, z0), v3(x0, y0, z0), v3(x0, y0, z1), v3(x0, y1, z1)},
        {v3(x0, y0, z1), v3(x1, y0, z1), v3(x1, y1, z1), v3(x0, y1, z1)},
        {v3(x0, y1, z0), v3(x1, y1, z0), v3(x1, y0, z0), v3(x0, y0, z0)}};
    for (int s = 0; s < (bottom ? 6 : 5); s++) {
        Mat m = mat(t, light(tint, n[s]));
        quad(q[s][0], q[s][1], q[s][2], q[s][3], 1, 1, &m);
    }
}

// shipping containers, stacked in the port
typedef struct { float x0, y0, x1, y1, z1; u32 col; } Crate;
#define MAX_CRATES 600
static Crate crates[MAX_CRATES];
static int n_crates;
static u16 crate_from[BYN][BXN], crate_n[BYN][BXN];

static u32 crate_cols[] = {0xd0402a, 0x2a70b0, 0xe0a020, 0x2a9060, 0xe8e8e8, 0x8a3a8a, 0xc06030};

static void plan_port(int bx, int by) {
    float X = bx * 8.0f, Y = by * 8.0f;
    crate_from[by][bx] = (u16)n_crates;
    // rows of containers running east west, leave lanes between
    for (int row = 0; row < 3; row++) {
        float y0 = Y + 3.2f + row * 1.7f;
        float x = X + 3.1f;
        while (x < X + 7.6f && n_crates < MAX_CRATES) {
            float len = r01() < 0.5f ? 1.2f : 2.3f;
            if (x + len > X + 7.9f) break;
            int stack = 1 + rint_(3);
            if (r01() < 0.15f) { x += len + 0.2f; continue; }
            for (int s = 0; s < stack && n_crates < MAX_CRATES; s++) {
                Crate *c = &crates[n_crates++];
                c->x0 = x; c->x1 = x + len; c->y0 = y0; c->y1 = y0 + 0.95f;
                c->z1 = 0.05f + (s + 1) * 0.95f;
                c->col = crate_cols[rint_(7)];
            }
            x += len + 0.12f;
        }
    }
    crate_n[by][bx] = (u16)(n_crates - crate_from[by][bx]);
}

// round islands out in the bay, with a couple of big houses each
typedef struct { float x, y, r; Building b[3]; int n; } Island;
#define ISLANDS 3
static Island isles[ISLANDS] = {{196, 146, 13}, {227, 146, 8.5f}, {206, 97.5f, 7.5f}};

static void plan_islands(void) {
    for (int i = 0; i < ISLANDS; i++) {
        Island *s = &isles[i];
        city_r = hash2(i, 4242) | 1;
        s->n = 0;
        // houses sit off the causeway, north and south of it
        for (int k = 0; k < 3; k++) {
            float a = k * 2.1f + 0.7f + i;
            float d = s->r * 0.55f;
            float cx = s->x + cosf_(a) * d, cy = s->y + sinf_(a) * d;
            if (absf(cy - 146.0f) < 3.2f) cy += cy > 146 ? 2.5f : -2.5f;
            if (absf(cy - 98.0f) < 3.2f && i == 2) cy += cy > 98 ? 2.5f : -2.5f;
            Building *b = &s->b[s->n++];
            float w = 1.6f + r01() * 1.2f, h = 1.4f + r01() * 1.0f;
            b->x0 = cx - w / 2; b->x1 = cx + w / 2; b->y0 = cy - h / 2; b->y1 = cy + h / 2;
            b->h = 0.1f + 2 + rint_(2);
            b->face = r01() < 0.5f ? t_stucco : t_glass;
            b->tint = b->face == t_glass ? 0xe0f0f8 : 0xfff8f0;
            b->shops = 0; b->th = 0; b->u0 = 0; b->v0 = 0;
            b->shape = r01() < 0.6f ? 2 : 0;
            b->tshape = 0;
            b->roof = r01() < 0.3f ? 4 : 0;
            b->dome = 0xf4f4f4;
            b->neon = 0;
        }
    }
}

static int in_island(float x, float y, float pad) {
    for (int i = 0; i < ISLANDS; i++) {
        float dx = x - isles[i].x, dy = y - isles[i].y, r = isles[i].r + pad;
        if (dx * dx + dy * dy < r * r) return i + 1;
    }
    return 0;
}

static void draw_island(Island *s) {
    int segs = 28;
    Mat g = mat(t_grass, light(0xffffff, v3(0, 0, 1)));
    Mat sd = mat(t_sand, light(0xffffff, v3(0, 0, 1)));
    for (int i = 0; i < segs; i++) {
        float a0 = i * TAU / segs, a1 = (i + 1) * TAU / segs;
        float c0 = cosf_(a0), s0 = sinf_(a0), c1 = cosf_(a1), s1 = sinf_(a1);
        V3 p0 = v3(s->x + c0 * s->r, s->y + s0 * s->r, -0.02f), p1 = v3(s->x + c1 * s->r, s->y + s1 * s->r, -0.02f);
        tri(v3(s->x, s->y, -0.02f), p0, p1, s->x / 2, s->y / 2, p0.x / 2, p0.y / 2, p1.x / 2, p1.y / 2, &g);
        float R = s->r + 2.2f;
        V3 q0 = v3(s->x + c0 * R, s->y + s0 * R, SEA_Z + 0.05f), q1 = v3(s->x + c1 * R, s->y + s1 * R, SEA_Z + 0.05f);
        quad(p0, q0, q1, p1, 1, 1, &sd);
    }
    for (int k = 0; k < s->n; k++) {
        Building *b = &s->b[k];
        draw_building(b);
        // pool next to the house
        Mat pool_m = {t_sea, 0x9090a0, 0, 0};
        float px = b->x1 + 0.3f, py = (b->y0 + b->y1) / 2;
        quad(v3(px, py - 0.5f, 0.0f), v3(px + 1.0f, py - 0.5f, 0.0f), v3(px + 1.0f, py + 0.5f, 0.0f), v3(px, py + 0.5f, 0.0f), 1, 1, &pool_m);
    }
    for (int k = 0; k < 6; k++) {
        float a = k * TAU / 6 + 0.4f;
        float r = s->r * 0.85f;
        V3 p = v3(s->x + cosf_(a) * r, s->y + sinf_(a) * r, -0.02f);
        if (absf(p.y - 146.0f) < 1.8f || absf(p.y - 98.0f) < 1.8f) continue;
        draw_palm(p, hash2(k, (int)s->x));
    }
}

static void crane(float x, float y) {
    u32 c = 0xe8b830;
    box(v3(x - 0.15f, y - 1.6f, 0), v3(x + 0.15f, y - 1.3f, 6), t_curb, c, 0);
    box(v3(x - 0.15f, y + 1.3f, 0), v3(x + 0.15f, y + 1.6f, 6), t_curb, c, 0);
    box(v3(x + 3.0f, y - 1.6f, 0), v3(x + 3.3f, y - 1.3f, 6), t_curb, c, 0);
    box(v3(x + 3.0f, y + 1.3f, 0), v3(x + 3.3f, y + 1.6f, 6), t_curb, c, 0);
    // top beam sticks out over the water
    box(v3(x - 0.5f, y - 1.6f, 6), v3(x + 9.0f, y + 1.6f, 6.6f), t_curb, c, 1);
    box(v3(x + 1.0f, y - 0.6f, 6.6f), v3(x + 2.6f, y + 0.6f, 7.4f), t_curb, 0xf0f0f0, 0);
    box(v3(x + 6.0f, y - 0.05f, 3.0f), v3(x + 6.1f, y + 0.05f, 6.0f), t_dark, 0xffffff, 0);
}

static Mat road_m, cross_m, walk_m;

// the strips of road every land block has on its west and south side
static void block_roads(float X, float Y, int ew) {
    float r0 = 0.375f, r1 = PLOT0;
    quad(v3(X + r0, Y + r0, 0), v3(X + r1, Y + r0, 0), v3(X + r1, Y + r1, 0), v3(X + r0, Y + r1, 0), 1, 1, &cross_m);
    quad(v3(X + r0, Y + PLOT0, 0), v3(X + r1, Y + PLOT0, 0), v3(X + r1, Y + PLOT1, 0), v3(X + r0, Y + PLOT1, 0), 1, (PLOT1 - PLOT0) / 2.25f, &road_m);
    if (ew)
        quad(v3(X + PLOT0, Y + r1, 0), v3(X + PLOT0, Y + r0, 0), v3(X + PLOT1, Y + r0, 0), v3(X + PLOT1, Y + r1, 0), 1, (PLOT1 - PLOT0) / 2.25f, &road_m);
}

static int has_plot(int t) { return t == T_CITY || t == T_PARK || t == T_PORT; }

static void draw_block(int bx, int by) {
    float X = bx * 8.0f, Y = by * 8.0f;
    int t = tile_at(bx, by);
    if (t == T_WATER) return;
    if (t == T_BRIDGE) {
        // deck on pillars with a low wall each side
        float y0 = Y + 0.375f, y1 = Y + PLOT0;
        quad(v3(X, y1, 0), v3(X, y0, 0), v3(X + 8, y0, 0), v3(X + 8, y1, 0), 1, 8 / 2.25f, &road_m);
        box(v3(X, y0 - 0.25f, -0.3f), v3(X + 8, y0, 0.28f), t_curb, 0xf0ece8, 0);
        box(v3(X, y1, -0.3f), v3(X + 8, y1 + 0.25f, 0.28f), t_curb, 0xf0ece8, 0);
        Mat side = mat(t_curb, light(0xc8c4c0, v3(0, -1, 0)));
        quad(v3(X, y0, -0.3f), v3(X + 8, y0, -0.3f), v3(X + 8, y0, 0), v3(X, y0, 0), 1, 1, &side);
        box(v3(X + 3.6f, y0 + 0.4f, SEA_Z - 0.5f), v3(X + 4.4f, y1 - 0.4f, -0.3f), t_curb, 0xb0aca8, 0);
        return;
    }
    if (t == T_BEACH) {
        block_roads(X, Y, 0);
        box(v3(X + PLOT0, Y, 0), v3(X + 3.0f, Y + 8, CURB), t_walk, 0xffffff, 0);
        Mat sand = mat(t_sand, light(0xffffff, v3(0, 0, 1)));
        quad(v3(X + 3.0f, Y, CURB), v3(X + 7.0f, Y, SEA_Z + 0.05f), v3(X + 7.0f, Y + 8, SEA_Z + 0.05f), v3(X + 3.0f, Y + 8, CURB), 1, 2, &sand);
        if (!has_plot(tile_at(bx, by - 1)) && tile_at(bx, by - 1) != T_BEACH)
            box(v3(X, Y - 0.1f, SEA_Z), v3(X + 7, Y + 0.375f, CURB), t_walk, 0xffffff, 0);
        // palms down the beach every few cells
        for (int i = 0; i < 2; i++) draw_palm(v3(X + 4.3f + (hash2(bx, by * 2 + i) & 7) * 0.08f, Y + 1.5f + i * 4, 0.05f), hash2(bx * 9 + i, by));
        return;
    }
    // land block
    block_roads(X, Y, 1);
    int west = tile_at(bx - 1, by), south = tile_at(bx, by - 1), north = tile_at(bx, by + 1), east = tile_at(bx + 1, by);
    // edge with no neighbour plot gets a sidewalk strip that is also the sea wall
    if (!has_plot(west)) box(v3(X - 0.1f, Y, SEA_Z), v3(X + 0.375f, Y + 8, CURB), t_walk, 0xffffff, 0);
    if (!has_plot(south) && south != T_BEACH) box(v3(X, Y - 0.1f, SEA_Z), v3(X + 8, Y + 0.375f, CURB), t_walk, 0xffffff, 0);
    float p0x = X + PLOT0, p1x = X + PLOT1, p0y = Y + PLOT0, p1y = Y + PLOT1;
    float bot_e = (east == T_WATER || east == T_BRIDGE) ? SEA_Z : 0, bot_n = (north == T_WATER || north == T_BRIDGE) ? SEA_Z : 0;
    if (t == T_PORT) {
        // yard is flat asphalt, no curb
        Mat yard = mat(t_road, light(0xd8d8d8, v3(0, 0, 1)));
        quad(v3(p0x, p0y, 0.01f), v3(p1x, p0y, 0.01f), v3(p1x, p1y, 0.01f), v3(p0x, p1y, 0.01f), 0.02f, 0.02f, &yard);
        if (bot_e < 0) { Mat w = mat(t_curb, light(0xb0b0b0, v3(1, 0, 0))); quad(v3(p1x, p0y, SEA_Z), v3(p1x, p1y, SEA_Z), v3(p1x, p1y, 0.01f), v3(p1x, p0y, 0.01f), 8, 1, &w); }
        if (bot_n < 0) { Mat w = mat(t_curb, light(0xb0b0b0, v3(0, 1, 0))); quad(v3(p1x, p1y, SEA_Z), v3(p0x, p1y, SEA_Z), v3(p0x, p1y, 0.01f), v3(p1x, p1y, 0.01f), 8, 1, &w); }
        for (int i = crate_from[by][bx]; i < crate_from[by][bx] + crate_n[by][bx]; i++) {
            Crate *c = &crates[i];
            box(v3(c->x0, c->y0, c->z1 - 0.95f), v3(c->x1, c->y1, c->z1), t_container, c->col, 0);
        }
        if (east == T_WATER && (by & 1)) crane(X + 6.6f, Y + 5.0f);
        return;
    }
    Mat top = t == T_PARK ? mat(t_grass, light(0xffffff, v3(0, 0, 1))) : walk_m;
    quad(v3(p0x, p0y, CURB), v3(p1x, p0y, CURB), v3(p1x, p1y, CURB), v3(p0x, p1y, CURB), (p1x - p0x) * 2, (p1y - p0y) * 2, &top);
    Mat cs = mat(t_curb, light(0xffffff, v3(0, -1, 0))), cw = mat(t_curb, light(0xffffff, v3(-1, 0, 0)));
    Mat cn = mat(t_curb, light(0xffffff, v3(0, 1, 0))), ce = mat(t_curb, light(0xffffff, v3(1, 0, 0)));
    quad(v3(p0x, p0y, 0), v3(p1x, p0y, 0), v3(p1x, p0y, CURB), v3(p0x, p0y, CURB), 8, 1, &cs);
    quad(v3(p1x, p0y, bot_e), v3(p1x, p1y, bot_e), v3(p1x, p1y, CURB), v3(p1x, p0y, CURB), 8, 1, &ce);
    quad(v3(p1x, p1y, bot_n), v3(p0x, p1y, bot_n), v3(p0x, p1y, CURB), v3(p1x, p1y, CURB), 8, 1, &cn);
    quad(v3(p0x, p1y, 0), v3(p0x, p0y, 0), v3(p0x, p0y, CURB), v3(p0x, p1y, CURB), 8, 1, &cw);
    if (t == T_PARK) {
        // paths cross the park, palms in the corners
        Mat path = mat(t_walk, light(0xffffff, v3(0, 0, 1)));
        float mx = X + 5.5f, my = Y + 5.5f;
        quad(v3(mx - 0.3f, p0y, CURB + 0.01f), v3(mx + 0.3f, p0y, CURB + 0.01f), v3(mx + 0.3f, p1y, CURB + 0.01f), v3(mx - 0.3f, p1y, CURB + 0.01f), 1, 8, &path);
        quad(v3(p0x, my - 0.3f, CURB + 0.01f), v3(p1x, my - 0.3f, CURB + 0.01f), v3(p1x, my + 0.3f, CURB + 0.01f), v3(p0x, my + 0.3f, CURB + 0.01f), 8, 1, &path);
        for (int i = 0; i < 4; i++) draw_palm(v3(X + 4.0f + (i & 1) * 3.0f, Y + 4.0f + (i >> 1) * 3.0f, CURB), hash2(bx * 4 + i, by));
        return;
    }
    Block *k = &blocks[by][bx];
    for (int i = 0; i < k->n; i++) draw_building(&k->b[i]);
}

static float sea_t;

static void draw_sea(void) {
    Mat m = mat(t_sea, 0x808080);
    float s = 4.0f, o = sea_t;
    for (int j = -2; j < WY / 40 + 3; j++)
        for (int i = -2; i < WX / 40 + 3; i++) {
            float x0 = i * 40.0f, y0 = j * 40.0f, x1 = x0 + 40, y1 = y0 + 40;
            V3 d = sub(v3((x0 + x1) / 2, (y0 + y1) / 2, 0), cam);
            if (d.x * d.x + d.y * d.y > (fog_far + 30) * (fog_far + 30)) continue;
            if (dot(d, cam_f) < -30) continue;
            CV v[3];
            to_cam(v3(x0, y0, SEA_Z), &v[0], x0 / s + o, y0 / s);
            to_cam(v3(x1, y0, SEA_Z), &v[1], x1 / s + o, y0 / s);
            to_cam(v3(x1, y1, SEA_Z), &v[2], x1 / s + o, y1 / s);
            clip_draw(v, &m);
            to_cam(v3(x0, y0, SEA_Z), &v[0], x0 / s + o, y0 / s);
            to_cam(v3(x1, y1, SEA_Z), &v[1], x1 / s + o, y1 / s);
            to_cam(v3(x0, y1, SEA_Z), &v[2], x0 / s + o, y1 / s);
            clip_draw(v, &m);
        }
}

typedef struct { float d; int bx, by; } Vis;
static Vis vis[BXN * BYN];

static void draw_city(void) {
    road_m = mat(t_road, light(0xffffff, v3(0, 0, 1)));
    cross_m = mat(t_cross, road_m.tint);
    walk_m = mat(t_walk, light(0xffffff, v3(0, 0, 1)));
    int n = 0;
    float tanx = half_w / focal;
    int cbx = (int)(cam.x / 8), cby = (int)(cam.y / 8), rr = (int)(fog_far / 8) + 2;
    for (int by = maxi(0, cby - rr); by < mini(BYN, cby + rr + 1); by++)
        for (int bx = maxi(0, cbx - rr); bx < mini(BXN, cbx + rr + 1); bx++) {
            if (tile_at(bx, by) == T_WATER) continue;
            V3 d = sub(v3(bx * 8 + 4.5f, by * 8 + 4.5f, 0), cam);
            float dd = d.x * d.x + d.y * d.y;
            if (dd > (fog_far + 10) * (fog_far + 10)) continue;
            float zc = d.x * cam_f.x + d.y * cam_f.y;
            float xc = dot(d, cam_r);
            if (zc < -7) continue;
            if (absf(xc) - 7 > (zc + 7) * tanx * 1.2f) continue;
            vis[n].d = dd;
            vis[n].bx = bx;
            vis[n].by = by;
            n++;
        }
    // near first so the depth test throws away hidden pixels early
    for (int i = 1; i < n; i++) {
        Vis v = vis[i];
        int j = i - 1;
        while (j >= 0 && vis[j].d > v.d) { vis[j + 1] = vis[j]; j--; }
        vis[j + 1] = v;
    }
    for (int i = 0; i < n; i++) draw_block(vis[i].bx, vis[i].by);
    for (int i = 0; i < ISLANDS; i++) {
        V3 d = sub(v3(isles[i].x, isles[i].y, 0), cam);
        if (d.x * d.x + d.y * d.y > (fog_far + 15) * (fog_far + 15) || dot(d, cam_f) < -15) continue;
        draw_island(&isles[i]);
    }
}

static int in_building(Building *b, float x, float y) {
    if (x < b->x0 || x > b->x1 || y < b->y0 || y > b->y1) return 0;
    if (b->shape == 1) {
        float u = (x - (b->x0 + b->x1) / 2) / ((b->x1 - b->x0) / 2), v = (y - (b->y0 + b->y1) / 2) / ((b->y1 - b->y0) / 2);
        if (u * u + v * v > 1) return 0;
    }
    return 1;
}

// what the cars bump into
static int solid(float x, float y) {
    if (x < 0 || y < 0 || x >= WX || y >= WY) return 1;
    int bx = (int)(x / 8), by = (int)(y / 8);
    int t = tile_at(bx, by);
    float lx = x - bx * 8, ly = y - by * 8;
    if (t == T_WATER || t == T_BRIDGE) {
        int isl = in_island(x, y, 0.3f);
        if (isl) {
            Island *s = &isles[isl - 1];
            for (int k = 0; k < s->n; k++)
                if (in_building(&s->b[k], x, y)) return 1;
            return 0;
        }
        if (t == T_BRIDGE && ly > 0.45f && ly < PLOT0 - 0.07f) return 0;
        // the edge of a land block next door hangs over a little
        if (lx < 0.375f && has_plot(tile_at(bx - 1, by))) return 0;
        if (ly < 0.375f && has_plot(tile_at(bx, by - 1))) return 0;
        return 1;
    }
    if (t == T_BEACH) return lx > 6.0f;
    if (t == T_PORT) {
        for (int i = crate_from[by][bx]; i < crate_from[by][bx] + crate_n[by][bx]; i++) {
            Crate *c = &crates[i];
            if (x >= c->x0 && x <= c->x1 && y >= c->y0 && y <= c->y1) return 1;
        }
        return 0;
    }
    Block *k = &blocks[by][bx];
    for (int i = 0; i < k->n; i++)
        if (in_building(&k->b[i], x, y)) return 1;
    return 0;
}

// what is on the ground here, for the radar
// 0 sea 1 road 2 sidewalk 3 building 4 sand 5 grass
static int ground_kind(float x, float y) {
    if (x < 0 || y < 0 || x >= WX || y >= WY) return 0;
    int bx = (int)(x / 8), by = (int)(y / 8);
    int t = tile_at(bx, by);
    float lx = x - bx * 8, ly = y - by * 8;
    if (t == T_WATER || t == T_BRIDGE) {
        if (in_island(x, y, 0)) return solid(x, y) ? 3 : 5;
        if (t == T_BRIDGE && ly > 0.375f && ly < PLOT0) return 1;
        if ((lx < 0.375f && has_plot(tile_at(bx - 1, by))) || (ly < 0.375f && has_plot(tile_at(bx, by - 1)))) return 2;
        return 0;
    }
    int road = (lx >= 0.375f && lx < PLOT0) || (ly >= 0.375f && ly < PLOT0);
    if (t == T_BEACH) return road && lx < PLOT0 ? 1 : lx < 3.0f ? 2 : lx < 7.0f ? 4 : 0;
    if (road) return 1;
    if (t == T_PARK) return 5;
    return solid(x, y) ? 3 : 2;
}
