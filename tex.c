// every texture is made here at startup, nothing loaded

static Tex *t_road1, *t_cloud;
static Tex *t_white, *t_road, *t_walk, *t_curb, *t_sand, *t_sea, *t_roof;
static Tex *t_brick, *t_stucco, *t_concrete, *t_grass, *t_container;
static Tex *neon_cache[8];
static u32 neon_key[8];
static Tex *t_shops[6], *t_awn, *t_bark, *t_leaf, *t_carglass, *t_tyre, *t_crown;

static float noise2(int x, int y, int wrap) {
    return (hash2(x & (wrap - 1), y & (wrap - 1)) & 1023) / 1023.0f;
}

// smooth value noise that tiles every wrap cells
static float vnoise(float x, float y, int wrap) {
    int ix = floori(x), iy = floori(y);
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    float a = noise2(ix, iy, wrap), b = noise2(ix + 1, iy, wrap);
    float c = noise2(ix, iy + 1, wrap), d = noise2(ix + 1, iy + 1, wrap);
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy;
}

static float fbm(float x, float y, int wrap) {
    float s = 0, amp = 0.5f;
    for (int i = 0; i < 4; i++) {
        s += vnoise(x, y, wrap) * amp;
        x *= 2; y *= 2; wrap *= 2; amp *= 0.5f;
    }
    return s;
}

static u32 shade(u32 c, float k) {
    return rgb((int)((c >> 16 & 255) * k), (int)((c >> 8 & 255) * k), (int)((c & 255) * k));
}

#define PX(t, x, y) (t)->lv[0][(y) << (t)->bits[0] | (x)]

static void asphalt(Tex *t, u32 base) {
    int s = 1 << t->bits[0];
    for (int y = 0; y < s; y++)
        for (int x = 0; x < s; x++) {
            float big = fbm(x / 64.0f, y / 64.0f, s / 64), mid = fbm(x / 16.0f, y / 16.0f, s / 16), agg = noise2(x, y, s);
            u32 c = shade(base, 0.8f + big * 0.2f + (mid - 0.5f) * 0.12f + agg * 0.12f);
            if (agg > 0.975f) c = shade(c, 1.28f);
            else if (agg < 0.02f) c = shade(c, 0.7f);
            int bx = x >> 6, by = y >> 6, lx = x & 63, ly = y & 63;
            u32 h = hash2(bx * 7 + 3, by * 13 + 1);
            if ((h & 7) < 2) {
                int x0 = 6 + (h >> 4 & 15), y0 = 6 + (h >> 8 & 15), x1 = x0 + 18 + (h >> 12 & 15), y1 = y0 + 14 + (h >> 16 & 15);
                if (lx >= x0 && lx <= x1 && ly >= y0 && ly <= y1) {
                    c = shade(c, (h >> 20 & 1) ? 0.86f : 1.1f);
                    if (lx == x0 || lx == x1 || ly == y0 || ly == y1) c = shade(0x2c2c30, 0.9f + agg * 0.2f);
                }
            }
            float mask = fbm(x / 32.0f + 5, y / 32.0f, s / 32);
            if (mask > 0.62f) {
                float cr = vnoise(x / 14.0f, y / 14.0f, s / 14 > 0 ? 16 : 1);
                if (absf(cr - 0.5f) < 0.008f) c = mix(c, 0x26262a, 150);
            }
            PX(t, x, y) = c;
        }
}

static void make_textures(void) {
    t_white = new_tex(2);
    for (int i = 0; i < 16; i++) t_white->lv[0][i] = 0xffffff & 0xf0f0f0;
    make_mips(t_white);

    t_road = new_tex(8);
    asphalt(t_road, 0x4a4b52);
    make_mips(t_road);
    t_road1 = new_tex(8);
    asphalt(t_road1, 0x5a5b60);
    make_mips(t_road1);

    // sidewalk tiles one per half unit
    t_walk = new_tex(6);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            float n = noise2(x, y, 64) * 0.1f + fbm(x / 8.0f, y / 8.0f, 8) * 0.12f;
            u32 c = shade(0xd2c6c0, 0.86f + n);
            if ((x & 31) == 0 || (y & 31) == 0) c = shade(c, 0.78f);
            PX(t_walk, x, y) = c;
        }
    make_mips(t_walk);

    t_curb = new_tex(4);
    for (int i = 0; i < 256; i++) t_curb->lv[0][i] = shade(0xe2dad6, 0.9f + noise2(i, 0, 256) * 0.1f);
    make_mips(t_curb);

    t_sand = new_tex(7);
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++) {
            float n = fbm(x / 16.0f, y / 16.0f, 8);
            float ripple = sinf_((x + n * 30) * 0.35f) * 0.05f;
            PX(t_sand, x, y) = shade(0xf2d9a2, 0.86f + n * 0.18f + ripple + noise2(x, y, 128) * 0.06f);
        }
    make_mips(t_sand);

    t_sea = new_tex(7);
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++) {
            float n = fbm(x / 16.0f, y / 16.0f, 8);
            float w = sinf_((y + n * 24) * 0.4f);
            u32 c = mix(0x1f8fa8, 0x46d4d0, (u32)(n * 200));
            if (w > 0.93f) c = mix(c, 0xe8fbff, 140);
            PX(t_sea, x, y) = c;
        }
    make_mips(t_sea);

    t_roof = new_tex(6);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++)
            PX(t_roof, x, y) = shade(0xf4f2f0, 0.84f + noise2(x, y, 64) * 0.16f);
    make_mips(t_roof);

    // facades are 4 bays by 4 floors so lit windows dont repeat every window
    // wall is near white so the building tint shows
    t_hotel = new_tex(8);
    t_deco = new_tex(8);
    t_glass = new_tex(8);
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) {
            int bx = x >> 6, by = y >> 6, lx = x & 63, ly = y & 63;
            int lit = (hash2(bx * 7 + 1, by * 13 + 5) & 3) == 0;
            float wn = noise2(x, y, 256) * 0.05f;
            // hotel: one tall window per bay with a sill and a balcony rail
            u32 c = shade(0xf4f0ec, 0.93f + wn);
            if (lx >= 16 && lx < 48 && ly >= 12 && ly < 50) {
                float g = (ly - 12) / 38.0f;
                c = lit ? (0xff000000u | mix(0xffd27a, 0xffb050, (u32)(g * 120))) : mix(0x6a7aa8, 0x2c3458, (u32)(g * 256));
                if (lx == 31 || lx == 32) c = shade(0xe8e4e0, 0.8f);
            }
            if (ly >= 50 && ly < 54 && lx >= 12 && lx < 52) c = shade(0xffffff, 0.8f);
            if (ly >= 56 && ly < 58) c = shade(0xf4f0ec, 0.82f);
            PX(t_hotel, x, y) = c;

            // deco: long ribbon windows and a coloured band
            c = shade(0xf6f2ee, 0.94f + wn);
            if (ly >= 20 && ly < 42 && lx != 0 && lx != 63) {
                int lit2 = (hash2(bx + 31, by * 3 + 7) % 5) == 0;
                float g = (ly - 20) / 22.0f;
                c = lit2 ? (0xff000000u | 0xffcf80) : mix(0x7ab0c8, 0x2a4a64, (u32)(g * 256));
            }
            if (ly >= 46 && ly < 52) c = 0x40b0b8 | 0;
            if (ly >= 52 && ly < 54) c = shade(0xffffff, 0.75f);
            PX(t_deco, x, y) = c;

            // glass tower: panels with frames, sky reflected, a few lit
            float g = (y & 63) / 64.0f;
            int lit3 = (hash2(bx * 5 + 2, by * 11 + 3) % 11) == 0;
            c = lit3 ? (0xff000000u | 0xe8c890) : mix(0x8fb8d8, 0x2a5070, (u32)((g * 0.6f + fbm(x / 32.0f, y / 64.0f, 8) * 0.5f) * 256));
            if (lx < 3 || ly < 3 || (lx >= 31 && lx < 33) || (ly >= 31 && ly < 33)) c = 0x2a3038;
            PX(t_glass, x, y) = c;
        }
    make_mips(t_hotel);
    make_mips(t_deco);
    make_mips(t_glass);

    // ground floor shops, one bay per 64 across
    for (int st = 0; st < 6; st++) {
        Tex *t = t_shops[st] = new_tex(8);
        for (int y = 0; y < 256; y++)
            for (int x = 0; x < 256; x++) {
                int bx = x >> 6, lx = x & 63;
                float ly = y / 4.0f;
                u32 hb = hash2(bx * 17 + st * 101, 31337);
                int lit = (hb >> 4 & 3) != 0;
                float nz = noise2(x, y, 256) * 0.05f;
                u32 c;
                if (st == 0) {
                    static const u32 fas[4] = {0x1c2230, 0x14141a, 0x1e3a2c, 0x4a1a24};
                    c = fas[bx];
                    if (ly >= 3 && ly < 7 && lx >= 12 && lx < 52 && ((lx / 3 + (int)ly) % 3) != 0) c = 0xff000000u | 0xfff0d8;
                    if (ly >= 9 && ly < 58 && lx >= 2 && lx < 62) {
                        float g = (ly - 9) / 49.0f;
                        c = mix(0xa8c4d8, 0x2c3c4c, (u32)(g * 256));
                        if (((lx + (int)ly) % 23) < 3) c = mix(c, 0xffffff, 70);
                        if (lit) {
                            c = 0xff000000u | mix(0xfff2dc, 0xd8b48c, (u32)(g * 256));
                            if (((lx + (int)ly) % 23) < 3) c = 0xff000000u | 0xfffaf0;
                            for (int k = 0; k < 2; k++) {
                                float mx_ = k ? 47.5f : 14.5f, ddx = lx - mx_;
                                u32 cl = (hb >> (k * 8 + 8)) & 1 ? 0x2a2a30 : shade(0xc04060 + ((hb >> (k * 5)) & 0x3f6f9f), 0.8f);
                                if ((ddx * ddx + (ly - 27) * (ly - 27)) < 4.2f) c = 0x3a3036;
                                else if (ly > 29.5f && ly < 46 && absf(ddx) < (ly < 33 ? 3.2f : 2.4f + (ly - 33) * 0.12f)) c = cl;
                                else if (ly >= 46 && ly < 55 && absf(ddx) < 1.6f && (absf(ddx) > 0.6f || ly < 48)) c = 0x2a2a30;
                            }
                            if (ly >= 54) c = 0xff000000u | 0xb89870;
                        }
                        if ((bx & 1) == 0 && lx >= 25 && lx < 39 && ly >= 18) c = mix(0x405060, 0x1a2028, (u32)(g * 200));
                        if (lx == 2 || lx == 61 || lx == 25 || lx == 38 || ly < 10.5f || ((bx & 1) && lx == 32)) c = 0x2a2a30;
                    }
                    if (ly >= 58) c = 0x34343a;
                } else if (st == 1) {
                    static const u32 awn[4] = {0x2a7a5a, 0xa83040, 0x1e4a8a, 0xd08a20};
                    c = shade(0xead8bc, 0.92f + nz);
                    if (ly < 9 + (((lx & 7) < 4) ? 1.5f : 0)) c = shade(awn[bx], 0.95f + nz);
                    for (int w = 0; w < 2; w++) {
                        float cxw = w ? 45.5f : 18.5f, dxw = lx - cxw, top = 22, ar = 10.5f;
                        int in = absf(dxw) < ar && ly < 60 && (ly > top || (ly > top - ar && dxw * dxw + (ly - top) * (ly - top) < ar * ar));
                        if (in) {
                            float g = (ly - 12) / 48.0f;
                            c = lit ? (0xff000000u | mix(0xffd49a, 0xa85a28, (u32)(g * 256))) : mix(0x40342c, 0x18120e, (u32)(g * 256));
                            if (ly > 46 && ly < 50 && ((lx >> 2) & 1)) c = 0x241810;
                            if (ly > 50 && ((lx & 7) == 3)) c = 0x241810;
                        }
                    }
                    if (ly >= 60) c = shade(0xc8b8a0, 0.9f);
                } else if (st == 2) {
                    static const u32 fas[4] = {0xd83a30, 0x2a6ab0, 0xe0b020, 0x3a9a50};
                    c = shade(0xd8d4cc, 0.9f + nz);
                    if (ly < 9) {
                        c = fas[bx];
                        if (ly >= 2.5f && ly < 6.5f && lx >= 10 && lx < 54 && ((lx / 2 + (int)(ly * 2)) % 4) == 0) c = 0xff000000u | 0xffffff;
                    }
                    if (ly >= 11 && lx >= 4 && lx < 60) {
                        if ((hb & 3) == 0) {
                            c = shade(0x9ca0a8, ((y >> 1) & 1) ? 0.95f : 0.78f);
                            if ((hash2(x >> 2, y >> 3) & 31) == 0) c = shade(0xc04080 + (hb & 0x3f3f), 0.8f);
                        } else {
                            float g = (ly - 11) / 53.0f;
                            c = 0xff000000u | mix(0xf0f8ff, 0xb8c8d8, (u32)(g * 256));
                            u32 q = hash2(lx >> 1, (int)ly) & 3;
                            if (((int)ly % 9) == 0 && ly > 20) c = 0x606870;
                            else if (ly > 20 && ((int)ly % 9) > 5) c = 0xff000000u | (q == 0 ? 0xe85040 : q == 1 ? 0x40a0e0 : 0xf0e0a0);
                            if (lx >= 40 && lx < 54 && ly >= 18) c = mix(0x506070, 0x202830, (u32)(g * 200));
                        }
                        if (lx == 4 || lx == 59) c = 0x50545a;
                    }
                } else if (st == 3) {
                    c = shade(0xdcd4c8, 0.9f + nz * 2);
                    if (((int)ly % 8) == 0 || ((lx + ((int)ly / 8) * 8) % 16) == 0) c = shade(0xc4bcb0, 0.88f);
                    if (bx == 1 || bx == 2) {
                        if (lx >= 10 && lx < 54 && ly >= 12) {
                            float g = (ly - 12) / 52.0f;
                            c = 0xff000000u | mix(0xfff0d0, 0xc8a070, (u32)(g * 256));
                            if (lx == 10 || lx == 53 || lx == 31 || lx == 32 || ly < 13.5f || absf(ly - 30) < 0.6f) c = 0xc8a050;
                        }
                        if (ly >= 5 && ly < 9 && lx >= 6 && lx < 58) c = 0x2a2a34;
                    } else if (lx >= 22 && lx < 42 && ly >= 14 && ly < 54) {
                        c = mix(0x8098b0, 0x283444, (u32)((ly - 14) * 6));
                        if (lx == 22 || lx == 41 || ly < 15) c = 0x3a3a40;
                    }
                } else if (st == 4) {
                    static const u32 fas[4] = {0x1e8a4a, 0xc02828, 0x2858a8, 0x7a2a8a};
                    c = shade(0xf0ece4, 0.92f + nz);
                    if (ly < 10) {
                        c = fas[bx];
                        if (bx == 0 && lx >= 4 && lx < 12 && ((ly >= 3.5f && ly < 6.5f) || (lx >= 6.5f && lx < 9.5f && ly >= 2 && ly < 8))) c = 0xff000000u | 0x80ff90;
                        if (ly >= 3 && ly < 7 && lx >= 16 && lx < 56 && ((lx + (int)ly) % 4) < 2) c = 0xff000000u | 0xffffff;
                    }
                    if (ly >= 12 && ly < 60 && lx >= 3 && lx < 61) {
                        float g = (ly - 12) / 48.0f;
                        c = 0xff000000u | mix(0xf4fbff, 0xc8d8e0, (u32)(g * 256));
                        int px = (lx - 3) / 14, py = (int)(ly - 14) / 16;
                        u32 ph = hash2(px + bx * 7, py + 3);
                        float qx = (lx - 3) - px * 14, qy = (ly - 14) - py * 16;
                        if ((ph & 3) != 0 && qx > 1 && qx < 12 && qy > 1 && qy < 13 && ly < 46) c = shade(0xe04848 + (ph & 0x5f7fff), 0.9f);
                        if (lx >= 24 && lx < 40 && ly >= 20) c = mix(0x8090a0, 0x303840, (u32)(g * 200));
                        if (lx == 3 || lx == 60 || lx == 24 || lx == 39) c = 0x404448;
                    }
                    if (ly >= 60) c = 0x585c60;
                } else {
                    int row = y >> 3, off = (row & 1) * 8;
                    u32 bc = mix(0xa85638, 0x7a3624, hash2((x + off) >> 4, row) & 255);
                    if ((y & 7) == 0 || ((x + off) & 15) == 0) bc = 0xb8aca0;
                    c = shade(bc, 0.9f + nz * 2);
                    static const u32 fas[4] = {0x1e3a2a, 0x4a1c22, 0x1e2a44, 0x2a2a2a};
                    if (ly >= 4 && ly < 11 && lx >= 4 && lx < 60) {
                        c = fas[bx];
                        if (ly >= 6 && ly < 9 && lx >= 14 && lx < 50 && ((lx / 2 + (int)ly) % 3) == 0) c = 0xff000000u | 0xf0c860;
                    }
                    if (ly >= 14 && ly < 58 && lx >= 6 && lx < 58) {
                        float g = (ly - 14) / 44.0f;
                        c = lit ? (0xff000000u | mix(0xffe0b0, 0xc08850, (u32)(g * 256))) : mix(0x6a7a8a, 0x1a2028, (u32)(g * 256));
                        if (lit && lx < 40) {
                            if (((int)ly - 18) % 9 == 0 && ly < 44) c = 0x5a3a24;
                            else if (((int)ly - 18) % 9 > 5 && ly < 44 && (hash2(lx >> 1, (int)ly + bx * 5) & 3) != 0) c = 0xff000000u | shade(0xe06040 + (hash2(lx >> 1, (int)ly) & 0x3f8fbf), 0.85f);
                            if (ly >= 46 && lx > 9 && lx < 36) c = (ly < 47.5f) ? 0x8a6a4a : 0x4a3424;
                        }
                        if (!lit && ((lx + (int)ly) % 19) < 2) c = mix(c, 0xffffff, 60);
                        if (lx >= 40 && lx < 52 && ly >= 22) c = 0x2a1a14;
                        if (lx == 6 || lx == 57 || lx == 40 || lx == 51 || ly < 15 || ly >= 57) c = 0x141414;
                    }
                }
                PX(t, x, y) = c;
            }
        make_mips(t);
    }
    t_awn = new_tex(8);
    {
        static const u32 ac[4] = {0xd83848, 0x1a9a9a, 0xe8a828, 0x2a3a8a};
        for (int y = 0; y < 256; y++)
            for (int x = 0; x < 256; x++) {
                u32 c = ((x >> 4) & 1) ? 0xf8f6f0 : ac[y >> 6];
                PX(t_awn, x, y) = shade(c, 0.94f + noise2(x, y, 256) * 0.06f);
            }
        make_mips(t_awn);
    }

    // brick: staggered bricks, white framed windows
    t_brick = new_tex(8);
    t_stucco = new_tex(8);
    t_concrete = new_tex(8);
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) {
            int bx = x >> 6, by = y >> 6, lx = x & 63, ly = y & 63;
            int row = y >> 2, off = (row & 1) * 4;
            u32 bc = mix(0xb0583a, 0x8a3c2a, hash2((x + off) >> 3, row) & 255);
            if ((y & 3) == 0 || ((x + off) & 7) == 0) bc = 0xc8bcb0;
            u32 c = shade(bc, 0.9f + noise2(x, y, 256) * 0.12f);
            int lit = (hash2(bx * 3 + 9, by * 5 + 1) % 5) == 0;
            if (lx >= 14 && lx < 50 && ly >= 14 && ly < 50) {
                if (lx < 17 || lx >= 47 || ly < 17 || ly >= 47 || lx == 31 || lx == 32 || ly == 30) c = 0xf0ece8;
                else c = lit ? (0xff000000u | 0xffd890) : mix(0x7a8cb0, 0x26304a, (u32)((ly - 14) * 7));
            }
            PX(t_brick, x, y) = c;

            // stucco: warm wall, shutters each side, terracotta band per floor
            c = shade(0xf8eee0, 0.92f + fbm(x / 16.0f, y / 16.0f, 16) * 0.12f);
            int lit2 = (hash2(bx * 7 + 3, by * 3 + 8) % 4) == 0;
            u32 shut = (hash2(bx, 99) & 1) ? 0x3a8a6a : 0x3a6aa8;
            if (ly >= 16 && ly < 48) {
                if (lx >= 22 && lx < 42) c = lit2 ? (0xff000000u | 0xffc880) : mix(0x5a6a88, 0x202838, (u32)((ly - 16) * 8));
                else if ((lx >= 12 && lx < 22) || (lx >= 42 && lx < 52)) c = ((ly & 3) == 0) ? shade(shut, 0.7f) : shut;
            }
            if (ly >= 58) c = shade(0xc8643a, 0.9f + noise2(x, y, 256) * 0.1f);
            PX(t_stucco, x, y) = c;

            // concrete: grey slabs and long dark window strips
            c = shade(0xe0dcd8, 0.88f + noise2(x >> 1, y >> 1, 128) * 0.1f);
            if ((x & 31) == 0) c = shade(c, 0.85f);
            int lit3 = (hash2(x >> 4, by * 13 + 2) % 7) == 0;
            if (ly >= 22 && ly < 46) c = lit3 ? (0xff000000u | 0xfff0c0) : mix(0x4a5868, 0x1a2028, (u32)((ly - 22) * 10));
            if (ly >= 22 && ly < 46 && (x & 15) == 0) c = 0x2a2e34;
            PX(t_concrete, x, y) = c;
        }
    make_mips(t_brick);
    make_mips(t_stucco);
    make_mips(t_concrete);

    t_grass = new_tex(7);
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++) {
            float n = fbm(x / 16.0f, y / 16.0f, 8);
            PX(t_grass, x, y) = shade(mix(0x4a9a3a, 0x7ab84a, (u32)(n * 256)), 0.85f + noise2(x, y, 128) * 0.2f);
        }
    make_mips(t_grass);

    // container side, ribs going up, white so the paint shows
    t_container = new_tex(6);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            u32 c = shade(0xf0f0f0, (x & 3) < 2 ? 0.95f : 0.78f);
            if (y < 2 || y > 61) c = shade(c, 0.7f);
            PX(t_container, x, y) = shade(c, 0.95f + noise2(x, y, 64) * 0.08f);
        }
    make_mips(t_container);

    // clouds, how thick they are in the low byte
    t_cloud = new_tex(8);
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) {
            float n = fbm(x / 40.0f, y / 22.0f, 8) * 0.8f + fbm(x / 9.0f, y / 6.0f, 32) * 0.3f;
            float a = clampf((n - 0.48f) * 3.2f, 0, 1);
            PX(t_cloud, x, y) = (u32)(a * 255);
        }

    t_bark = new_tex(5);
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            PX(t_bark, x, y) = shade(0x8a6040, 0.75f + ((y & 7) < 2 ? -0.15f : 0) + noise2(x, y, 32) * 0.25f);
    make_mips(t_bark);

    t_leaf = new_tex(5);
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            u32 c = shade(0x3aa04a, 0.7f + noise2(x, y, 32) * 0.3f);
            if (x >= 15 && x <= 16) c = 0x6aa040;
            else if (((x + y) & 3) == 0) c = shade(c, 0.8f);
            PX(t_leaf, x, y) = c;
        }
    make_mips(t_leaf);

    // leafy clumps for the round trees, gaps darker
    t_crown = new_tex(6);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            float n = fbm(x / 8.0f, y / 8.0f, 8), m = noise2(x, y, 64);
            u32 c = shade(0xc8e0b0, 0.62f + n * 0.5f + m * 0.16f);
            if (n < 0.38f) c = shade(c, 0.62f);
            PX(t_crown, x, y) = c;
        }
    make_mips(t_crown);

    t_carglass = new_tex(5);
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            u32 c = mix(0x8ab0d0, 0x1a2234, (u32)(y * 8));
            if (absf((float)(x - y * 0.6f - 6)) < 2) c = mix(c, 0xffffff, 90);
            PX(t_carglass, x, y) = c;
        }
    make_mips(t_carglass);

    t_tyre = new_tex(4);
    for (int i = 0; i < 256; i++) t_tyre->lv[0][i] = shade(0x26262c, 0.8f + noise2(i, 3, 256) * 0.3f);
    make_mips(t_tyre);
}

static Tex *t_condo, *t_office, *t_brow, *t_apart, *t_medi, *t_garage, *t_shed;
static Tex *t_walk2, *t_walk3;

static u32 glass_px(int lit, float g, u32 lo, u32 hi, u32 litc) {
    return lit ? (0xff000000u | litc) : mix(hi, lo, (u32)(clampf(g, 0, 1) * 256));
}

static void make_facades(void) {
    t_condo = new_tex(8); t_office = new_tex(8); t_brow = new_tex(8);
    t_apart = new_tex(8); t_medi = new_tex(8); t_garage = new_tex(8); t_shed = new_tex(8);
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) {
            int bx = x >> 6, by = y >> 6, lx = x & 63, ly = y & 63;
            float wn = noise2(x, y, 256) * 0.05f;
            u32 c;

            c = shade(0xf2f0ee, 0.92f + wn);
            if (ly >= 8 && ly < 50) {
                int lit = (hash2(x >> 5, by * 7 + 1) % 6) == 0;
                c = glass_px(lit, (ly - 8) / 42.0f + fbm(x / 32.0f, y / 32.0f, 8) * 0.3f, 0x1c3246, 0x7aa4c4, 0xffdca0);
                if ((x & 31) == 0) c = 0x3a4048;
            }
            if (ly >= 50 && ly < 58) c = shade(0xfafafa, 0.9f + wn);
            if (ly >= 58 && ly < 60) c = shade(0xfafafa, 0.62f);
            if (ly >= 36 && ly < 50) {
                u32 rail = mix(c, 0xb8d8e8, 90);
                c = (ly == 36 || ly == 37) ? 0xe8eef2 : rail;
            }
            PX(t_condo, x, y) = c;

            int lit2 = (hash2(x >> 3, y >> 4) % 13) == 0;
            float gg = fbm(x / 32.0f, y / 32.0f, 8) * 0.6f + (y & 63) / 160.0f;
            c = glass_px(lit2, gg, 0x2a2018, 0x8a7a64, 0xf0d8a8);
            if ((x & 15) == 0 || (x & 15) == 1) c = 0x2a2624;
            if (ly >= 54 && ly < 64) c = shade(0x4a423c, 0.9f + wn);
            if (ly == 54) c = 0x1a1816;
            PX(t_office, x, y) = c;

            c = shade(0xf6f2ec, 0.94f + wn);
            int lit3 = (hash2(bx * 9 + 4, by * 5 + 2) % 4) == 0;
            if (lx >= 8 && lx < 56 && ly >= 22 && ly < 48) {
                c = glass_px(lit3, (ly - 22) / 26.0f, 0x2a4a5a, 0x8ac0cc, 0xffd090);
                if (lx == 31 || lx == 32 || ly == 34) c = 0xe8e4de;
            }
            if (ly >= 16 && ly < 20 && lx >= 4 && lx < 60) c = shade(0xffffff, 0.98f);
            if (ly >= 20 && ly < 23 && lx >= 6 && lx < 58) c = shade(0xf6f2ec, 0.55f);
            if ((bx & 1) && lx >= 60) c = shade(0xffffff, 0.86f);
            if ((bx & 1) && lx >= 58 && lx < 60) c = shade(0xf6f2ec, 0.6f);
            PX(t_brow, x, y) = c;

            c = shade(0xeee8e0, 0.9f + wn + fbm(x / 16.0f, y / 16.0f, 16) * 0.08f);
            int lit4 = (hash2(bx * 13 + 3, by * 3 + 9) % 5) == 0;
            int wa = lx >= 8 && lx < 26, wb = lx >= 38 && lx < 56;
            if ((wa || wb) && ly >= 14 && ly < 42) {
                c = glass_px(lit4 && wa, (ly - 14) / 28.0f, 0x283040, 0x7888a4, 0xffcc88);
                if (lx == 16 || lx == 17 || lx == 46 || lx == 47 || ly == 27) c = 0xf2f0ec;
                if ((ly & 3) == 0 && ((bx + by) & 1)) c = shade(c, 0.85f);
            }
            if ((wa || wb) && (ly == 13 || ly == 42)) c = 0xf8f6f2;
            if (wb && ly >= 44 && ly < 52 && lx >= 40 && lx < 52 && (hash2(bx, by * 17) & 1)) c = shade(0xd8d8d4, (ly & 1) ? 0.8f : 0.95f);
            if (wa && ly >= 44 && ly < 47) c = shade(0x6a6a70, 0.9f);
            PX(t_apart, x, y) = c;

            c = shade(0xf4e6d0, 0.9f + fbm(x / 16.0f, y / 16.0f, 16) * 0.14f);
            int lit5 = (hash2(bx * 5 + 7, by * 11 + 1) % 4) == 0;
            float ax = (lx - 32) / 14.0f, ay = (ly - 22) / 14.0f;
            int arch = lx >= 18 && lx < 46 && ((ly >= 22 && ly < 52) || (ly < 22 && ax * ax + ay * ay < 1));
            if (arch) {
                c = glass_px(lit5, (ly - 10) / 42.0f, 0x2a2a3a, 0x6a7088, 0xffc070);
                if (lx == 31 || lx == 32) c = 0x4a3020;
                if (ly >= 22 && ly < 24) c = 0x4a3020;
            }
            if (ly >= 52 && ly < 56 && lx >= 14 && lx < 50) c = (lx & 3) == 0 ? 0x1a1a1a : shade(0x2a2a2a, 0.9f);
            if (ly >= 56 && ly < 58 && lx >= 14 && lx < 50) c = 0x1a1a1a;
            if (ly >= 60) c = shade((x + (y & 4)) & 4 ? 0xb8562e : 0x9a4422, 0.92f + wn);
            PX(t_medi, x, y) = c;

            c = shade(0xd8d4ce, 0.88f + noise2(x >> 1, y >> 1, 128) * 0.12f);
            if (ly >= 18 && ly < 52) {
                float d = (ly - 18) / 34.0f;
                c = mix(0x1c1e22, 0x34363c, (u32)(d * 256));
                if (ly >= 18 && ly < 21) c = 0x101214;
                if (ly < 24 && ((lx + bx * 21) % 32) < 6) c = 0xff000000u | 0xfff4d8;
                if ((x & 63) >= 28 && (x & 63) < 36) c = shade(0xd0ccc6, 0.8f);
                if (ly >= 40 && ((x * 7 + by * 13) % 90) < 30) c = mix(c, (x * 7 + by * 13) % 90 < 15 ? 0x8a2a2a : 0x2a4a7a, 120);
            }
            if (ly >= 52 && ly < 54) c = shade(c, 0.7f);
            PX(t_garage, x, y) = c;

            c = shade(0xd0d4d8, ((x & 7) < 4 ? 0.95f : 0.8f) + noise2(x, y >> 2, 256) * 0.08f);
            if (fbm(x / 32.0f, y / 32.0f, 8) > 0.68f) c = mix(c, 0x8a6a4a, 70);
            if (ly >= 4 && ly < 12 && lx >= 6 && lx < 58 && (by & 1)) c = glass_px(0, (ly - 4) / 8.0f, 0x30383c, 0x80909a, 0);
            PX(t_shed, x, y) = c;
        }
    make_mips(t_condo); make_mips(t_office); make_mips(t_brow);
    make_mips(t_apart); make_mips(t_medi); make_mips(t_garage); make_mips(t_shed);

    t_walk = new_tex(8); t_walk2 = new_tex(8); t_walk3 = new_tex(8);
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) {
            float big = fbm(x / 64.0f, y / 64.0f, 4) * 0.12f;
            int tx = x >> 5, ty = y >> 5;
            float tv = (hash2(tx, ty) & 255) / 255.0f * 0.08f;
            u32 c = shade(0xcfc6c0, 0.84f + tv + big + noise2(x, y, 256) * 0.06f);
            if ((x & 31) == 0 || (y & 31) == 0) c = shade(c, 0.72f);
            if ((x & 31) == 1 || (y & 31) == 1) c = shade(c, 0.9f);
            float st = fbm(x / 8.0f + 4, y / 8.0f, 32);
            if (st > 0.72f) c = shade(c, 0.88f - (st - 0.72f));
            if ((hash2(x >> 1, y >> 1) % 1400) == 0) c = shade(c, 0.78f);
            PX(t_walk, x, y) = c;

            int row = y >> 2, ox = (row & 1) * 4;
            u32 pv = mix(0xb87a66, 0x9c5e4c, hash2((x + ox) >> 3, row) & 255);
            if ((hash2((x + ox) >> 3, row * 3) & 15) == 0) pv = 0xc49a84;
            c = shade(pv, 0.88f + big + noise2(x, y, 256) * 0.06f);
            if ((y & 3) == 0 || ((x + ox) & 7) == 0) c = shade(0xa49a90, 0.88f);
            PX(t_walk2, x, y) = c;

            int sx = x >> 6, sy = y >> 6;
            u32 gr = mix(0xb4b2b4, 0x989a9e, hash2(sx * 5, sy * 3) & 255);
            float speck = noise2(x, y, 256);
            c = shade(gr, 0.86f + big + speck * 0.1f);
            if (speck > 0.93f) c = shade(c, 0.75f);
            if ((x & 63) == 0 || (y & 63) == 0) c = shade(c, 0.7f);
            PX(t_walk3, x, y) = c;
        }
    make_mips(t_walk); make_mips(t_walk2); make_mips(t_walk3);

    t_grass = new_tex(8);
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) {
            float n = fbm(x / 64.0f, y / 64.0f, 4), m = fbm(x / 8.0f, y / 8.0f, 32);
            u32 c = mix(0x3e8a32, 0x78b046, (u32)(n * 256));
            if (n > 0.66f) c = mix(c, 0xa8a456, (u32)((n - 0.66f) * 500));
            c = shade(c, 0.78f + m * 0.3f + noise2(x, y, 256) * 0.16f);
            if ((hash2(x, y) % 400) == 0) c = (hash2(y, x) & 1) ? 0xf0e070 : 0xf4f4f0;
            PX(t_grass, x, y) = c;
        }
    make_mips(t_grass);
}


static Tex *t_walk4, *t_walk5, *t_walk6, *t_walk7, *t_walk8, *t_lot, *t_lot2;

static void make_paving(void) {
    t_walk4 = new_tex(8); t_walk5 = new_tex(8); t_walk6 = new_tex(8); t_walk7 = new_tex(8); t_walk8 = new_tex(8);
    static const u32 chips[5] = {0x8a6a4a, 0x4a5a6a, 0xb04a3a, 0x2a2a2a, 0xd0c0a0};
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) {
            float big = fbm(x / 64.0f, y / 64.0f, 4) * 0.1f;
            int sx = x >> 6, sy = y >> 6;
            int kx = x >> 5, ky = y >> 5;
            u32 kt = hash2(kx * 5 + 1, ky * 9 + 2);
            u32 c = shade(mix(0xe8dcc4, 0xd4c4a8, kt & 255), 0.9f + big + noise2(x, y, 256) * 0.04f);
            float pit = fbm(x / 6.0f + kx, y / 6.0f, 43);
            if (pit > 0.7f) c = shade(c, 0.86f - (pit - 0.7f) * 0.6f);
            if ((hash2(x, y * 3) % 500) == 0) c = shade(c, 0.7f);
            if ((x & 31) == 0 || (y & 31) == 0) c = 0xb4aa98;
            (void)sx; (void)sy; (void)chips;
            PX(t_walk4, x, y) = c;

            float hx = x / 10.6667f, hy = y / 9.142857f;
            int row = (int)hy;
            float ox = (row & 1) ? 0.5f : 0;
            int col = (int)(hx + ox);
            float best = 1e9f, second = 1e9f;
            int bid = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int r = row + dy, cc = col + dx;
                    float cx = (cc - ((r & 1) ? 0.5f : 0) + 0.5f) * 10.6667f, cy = (r + 0.5f) * 9.142857f;
                    float ddx = x - cx, ddy = y - cy, d = ddx * ddx + ddy * ddy;
                    if (d < best) { second = best; best = d; bid = ((cc + 24) % 24) * 31 + ((r + 28) % 28) * 17; }
                    else if (d < second) second = d;
                }
            float edge = sqrtf_(second) - sqrtf_(best);
            u32 hc = hash2(bid, 77);
            c = shade(0xb0aeaa, 0.84f + (hc & 255) / 1800.0f + big + noise2(x, y, 256) * 0.05f);
            if (edge < 0.8f) c = shade(0x7a7874, 0.95f);
            PX(t_walk5, x, y) = c;

            int bx = x >> 5, by = y >> 5, lx = x & 31, ly = y & 31;
            int horiz = (bx + by) & 1;
            int brick = horiz ? (ly >> 4) : (lx >> 4);
            u32 bc = mix(0xa84a32, 0x7a3626, hash2(bx * 2 + brick, by * 3 + brick) & 255);
            c = shade(bc, 0.88f + big + noise2(x, y, 256) * 0.08f);
            if ((horiz ? (ly & 15) == 0 : (lx & 15) == 0) || lx == 0 || ly == 0) c = 0x8a8078;
            PX(t_walk6, x, y) = c;

            float sp = noise2(x * 7, y * 5, 256);
            c = shade(0xbab2a4, 0.84f + big + sp * 0.14f);
            if (sp > 0.86f) c = shade(0x6a6258, 0.95f);
            else if (sp < 0.08f) c = shade(0xe8e0d0, 0.95f);
            if ((x & 127) == 0 || (y & 127) == 0) c = 0x5e5850;
            PX(t_walk7, x, y) = c;

            int ex = x & 127, ey = y & 127;
            u32 sl = shade(0xd2d0cc, 0.88f + big + (hash2(x >> 7, y >> 7) & 255) / 3000.0f + noise2(x >> 1, y >> 1, 128) * 0.05f);
            if (ex < 10 || ey < 10) sl = shade(0x6e6c6a, 0.92f + noise2(x, y, 256) * 0.08f);
            if (ex == 0 || ey == 0 || ex == 10 || ey == 10) sl = 0x3e3c3a;
            PX(t_walk8, x, y) = sl;
        }
    make_mips(t_walk4); make_mips(t_walk5); make_mips(t_walk6); make_mips(t_walk7); make_mips(t_walk8);

    // parking lots, a row of stalls top and bottom and the aisle between, 6.4 units across
    t_lot = new_tex(8); t_lot2 = new_tex(8);
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) {
            float big = fbm(x / 64.0f, y / 64.0f, 4), n = noise2(x, y, 256);
            int stall = y < 77 || y >= 179, sy = y < 77 ? y : 255 - y;
            int col = (x * 7) >> 8, sx = (x * 7) & 255;
            u32 hs = hash2(col, y < 128);
            float oil = fbm(x / 10.0f + col * 3.1f, y / 10.0f, 25);
            int line = stall && sx < 14 && sy < 74;
            if (sy < 2) line = 1;
            int stop = stall && sy > 9 && sy < 15 && sx > 60 && sx < 196 && (hs & 3) != 0;
            float wornl = fbm(x / 6.0f, y / 6.0f, 42);
            for (int k = 0; k < 2; k++) {
                u32 c = k ? shade(0x5c5a64, 0.78f + big * 0.4f + n * 0.12f) : shade(0xbcb6aa, 0.84f + big * 0.2f + n * 0.08f);
                if ((hash2(x >> 1, y >> 1) % 200) == 0) c = shade(c, 0.8f);
                if (stall && sy > 18 && sy < 66 && sx > 40 && sx < 216 && oil > 0.62f) c = shade(c, k ? 0.8f : 0.72f);
                if (line && wornl > 0.22f) c = mix(c, 0xeceae2, (u32)(150 + wornl * 100));
                if (stop) c = shade(k ? 0xa8a6a0 : 0xd8d4cc, 0.9f + n * 0.1f);
                if (!stall && (hash2(x >> 3, y >> 3) % 90) == 0) c = shade(c, 0.88f);
                PX(k ? t_lot2 : t_lot, x, y) = c;
            }
        }
    make_mips(t_lot); make_mips(t_lot2);
}

// glowing tube in one colour, made the first time its asked for
static Tex *neon_tex(u32 c) {
    for (int i = 0; i < 8; i++) if (neon_cache[i] && neon_key[i] == c) return neon_cache[i];
    for (int i = 0; i < 8; i++)
        if (!neon_cache[i]) {
            Tex *t = new_tex(2);
            for (int k = 0; k < 16; k++) t->lv[0][k] = 0xff000000u | c;
            make_mips(t);
            neon_cache[i] = t;
            neon_key[i] = c;
            return t;
        }
    return neon_cache[0];
}
