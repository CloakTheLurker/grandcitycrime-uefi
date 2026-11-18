// names on buildings, shop signs, posters and billboards

static const char font_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789&'-.";
static const u8 font5[][7] = {
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},
    {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}, {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}, {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}, {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}, {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
    {0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D}, {0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}};
static int font_at(char c) {
    for (int i = 0; font_chars[i]; i++) if (font_chars[i] == c) return i;
    return -1;
}

#define CLEAR 0x01000000u
#define GLOW 0xff000000u

// letters scaled up with rounded corners so they dont look like blocks, into a box of the texture
static int text_w(int n, int sc) { return n * 6 * sc - sc; }
static void put_text(Tex *t, int x0, int y0, const char *s, int n, int sc, u32 col, u32 edge) {
    int S = 1 << t->bits[0];
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < n; i++) {
            int g = font_at(s[i]);
            if (g < 0) continue;
            for (int r = 0; r < 7; r++)
                for (int c = 0; c < 5; c++) {
                    if (!(font5[g][r] >> (4 - c) & 1)) continue;
                    int bx = x0 + (i * 6 + c) * sc, by = y0 + r * sc;
                    int o = pass ? 0 : (sc > 2 ? 1 : 0);
                    for (int y = by - o; y < by + sc + o; y++)
                        for (int x = bx - o; x < bx + sc + o; x++) {
                            if (x < 0 || y < 0 || x >= S || y >= S) continue;
                            if (pass == 0 && !edge) continue;
                            u32 *p = &t->lv[0][y * S + x];
                            *p = pass ? col : (*p == CLEAR || (*p >> 24) == 1 ? edge : *p);
                        }
                }
        }
}

// mips that keep clear texels clear and glowing ones glowing, colour from the solid ones
static void sign_mips(Tex *t) {
    int b = t->bits[0];
    t->levels = 1;
    while (b > 2 && t->levels < 8) {
        int s = 1 << b, h = s >> 1;
        u32 *src = t->lv[t->levels - 1], *dst = pool((usize)h * h * 4);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < h; x++) {
                u32 p[4] = {src[(2 * y) * s + 2 * x], src[(2 * y) * s + 2 * x + 1], src[(2 * y + 1) * s + 2 * x], src[(2 * y + 1) * s + 2 * x + 1]};
                u32 r = 0, g = 0, bl = 0, n = 0, clear = 0, glow = 0;
                for (int i = 0; i < 4; i++) {
                    if ((p[i] >> 24) == 1) { clear++; continue; }
                    glow += (p[i] >> 24) == 0xff;
                    r += p[i] >> 16 & 255; g += p[i] >> 8 & 255; bl += p[i] & 255; n++;
                }
                if (clear >= 3 || !n) { dst[y * h + x] = CLEAR | (n ? (r / n) << 16 | (g / n) << 8 | bl / n : 0); continue; }
                dst[y * h + x] = (glow * 2 >= n ? GLOW : 0) | (r / n) << 16 | (g / n) << 8 | bl / n;
            }
        t->lv[t->levels] = dst;
        t->bits[t->levels] = b - 1;
        t->levels++;
        b--;
    }
}

static const char *shop_names[] = {
    "CAFE CUBANO", "LA VENTANITA", "OCEAN DELI", "PALM LAUNDRY", "BAYSIDE PHARMACY", "SUNRISE BAKERY", "CORAL CELL", "TACO LOCO",
    "PIZZA 305", "BEACH WEAR", "LUCKY MART", "BODEGA LUZ", "GYM 305", "ISLAND TATTOO", "SALSA CLUB", "PAWN & GOLD",
    "NAILS & SPA", "BARBER SHOP", "LIQUORS", "FARMACIA", "MERCADO", "SUSHI BAR", "JUICE BAR", "CHECKS CASHED",
    "DINER", "BIKE RENTAL", "SURF SHOP", "PHOTO LAB", "SHOES", "OPTICA", "BOUTIQUE", "CERVEZA FRIA",
    "MOJITO BAR", "TOBACCO", "FLORES", "ARCADE", "VIDEO", "TOYS", "GELATO", "PIZZERIA",
    "HOTEL CORAL", "SEA BREEZE", "THE PALMS", "HOTEL MARINA", "CASA AZUL", "OCEAN VIEW", "STARLITE", "HOTEL FLAMINGO",
    "LAW OFFICES", "DENTAL CARE", "BAY REALTY", "INSURANCE", "MEDICAL CENTER", "TRAVEL AGENCY", "ACCOUNTING", "CREDIT UNION",
    "THE CORAL APTS", "BAY VILLAS", "PALM COURT", "SUNSET HOUSE", "CASA DEL MAR", "THE TROPICS", "LAGOON APTS", "VILLA ROSA"};
#define NSHOP 64
static const char *ads[][2] = {
    {"SUNSET FM", "98.3"}, {"COCO LOCO", "RUM"}, {"BAY BANK", "LOANS"}, {"FLY", "AIR CARIBE"}, {"MANGO", "SODA"}, {"305", "LOTTO"},
    {"ISLA", "TOURS"}, {"PULSE", "GYM"}, {"OCEAN", "TV 7"}, {"SALSA", "NIGHTS"}, {"CAFE", "CUBANO"}, {"BEACH", "PARTY"}};
#define NADS 12
static const u32 sign_bg[8] = {0x1a2a6a, 0xb02020, 0x10603a, 0x202024, 0xe8e0d0, 0x6a1a6a, 0xd08a10, 0x0a5a7a};
static const u32 neon_c[6] = {0xff4fb0, 0x40e8ff, 0xffe040, 0x60ff80, 0xff8040, 0xb070ff};

typedef struct { float ax, ay, bx, by, z0, z1, u1, v0, v1, nx, ny; Tex *t; u8 kind, glow; } Sign;
enum { S_NAME_HIGH, S_NAME_LOW, S_SHOP, S_POSTER, S_BOARD };
static Sign *signs;
static int n_signs;
static int *sign_first;
static u8 *sign_n;
static Tex *strip_tex;
static const u8 **bld_name;
static int strip_used = 8;

// a 256 wide strip of 32 rows in an atlas of 8, returns the row its in
static Tex *new_strip(int *row) {
    if (strip_used == 8) {
        strip_tex = new_tex(8);
        for (int i = 0; i < 256 * 256; i++) strip_tex->lv[0][i] = CLEAR;
        strip_used = 0;
    }
    *row = strip_used++;
    return strip_tex;
}

// 1 when the outline goes anticlockwise, then (dy, -dx) of a wall points out
static float outline_ccw(Bld *b) {
    float *p = &bp[b->first * 2], a = 0;
    for (int i = 0; i < b->n; i++) {
        int j = (i + 1) % b->n;
        a += p[i * 2] * p[j * 2 + 1] - p[j * 2] * p[i * 2 + 1];
    }
    return a > 0 ? 1.0f : -1.0f;
}

// wall i of a building: outward normal, length, and whether a road is in front of it
static int wall_road(Bld *b, int i, float *nx, float *ny, float *len) {
    float *p = &bp[b->first * 2], cx = (b->minx + b->maxx) / 2, cy = (b->miny + b->maxy) / 2;
    int j = (i + 1) % b->n;
    float dx = p[j * 2] - p[i * 2], dy = p[j * 2 + 1] - p[i * 2 + 1], l = sqrtf_(dx * dx + dy * dy);
    if (l < 1e-3f) return 0;
    (void)cx; (void)cy;
    float ux = dy / l, uy = -dx / l, mx_ = (p[i * 2] + p[j * 2]) / 2, my_ = (p[i * 2 + 1] + p[j * 2 + 1]) / 2;
    if (outline_ccw(b) < 0) { ux = -ux; uy = -uy; }
    *nx = ux; *ny = uy; *len = l;
    if (cell(mx_ + ux * 1.0f, my_ + uy * 1.0f) & C_BLD) return 0;
    for (float o = 1.0f; o <= 4.5f; o += 0.7f) if (cell(mx_ + ux * o, my_ + uy * o) & C_ROAD) return 1;
    return 0;
}

// the wall that faces a road, longest wins, n is outward
static int street_wall(Bld *b, float *nx, float *ny, float *len) {
    float *p = &bp[b->first * 2], cx = (b->minx + b->maxx) / 2, cy = (b->miny + b->maxy) / 2, best = -1;
    int bi = -1;
    for (int i = 0; i < b->n; i++) {
        int j = (i + 1) % b->n;
        float dx = p[j * 2] - p[i * 2], dy = p[j * 2 + 1] - p[i * 2 + 1], l = sqrtf_(dx * dx + dy * dy);
        if (l < 1.2f) continue;
        float ux = dy / l, uy = -dx / l, mx_ = (p[i * 2] + p[j * 2]) / 2, my_ = (p[i * 2 + 1] + p[j * 2 + 1]) / 2;
        if (outline_ccw(b) < 0) { ux = -ux; uy = -uy; }
        (void)cx; (void)cy;
        float sc = l;
        int road = 0;
        for (float o = 1.0f; o <= 4.0f && !road; o += 1.0f) road = (cell(mx_ + ux * o, my_ + uy * o) & C_ROAD) != 0;
        if (!road) sc *= 0.25f;
        if (cell(mx_ + ux * 1.0f, my_ + uy * 1.0f) & C_BLD) sc *= 0.05f;
        if (sc > best) { best = sc; bi = i; *nx = ux; *ny = uy; *len = l; }
    }
    return bi;
}

static void add_sign(Bld *b, int wall, float nx, float ny, float wl, float w, float z0, float z1, Tex *t, float u1, float v0, float v1, int kind, int glow, float at) {
    if (n_signs >= 16000 || w > wl * 0.95f) return;
    float *p = &bp[b->first * 2];
    int j = (wall + 1) % b->n;
    float ax = p[wall * 2], ay = p[wall * 2 + 1], dx = (p[j * 2] - ax) / wl, dy = (p[j * 2 + 1] - ay) / wl;
    // which way along the wall reads left to right from outside
    if (dx * -ny + dy * nx < 0) { ax = p[j * 2]; ay = p[j * 2 + 1]; dx = -dx; dy = -dy; }
    float c = wl * at, o = kind == S_BOARD ? 0 : 0.04f;
    Sign *s = &signs[n_signs++];
    s->ax = ax + dx * (c - w / 2) + nx * o; s->ay = ay + dy * (c - w / 2) + ny * o;
    s->bx = ax + dx * (c + w / 2) + nx * o; s->by = ay + dy * (c + w / 2) + ny * o;
    s->z0 = z0; s->z1 = z1; s->t = t; s->u1 = u1; s->v0 = v0; s->v1 = v1; s->nx = nx; s->ny = ny; s->kind = (u8)kind; s->glow = (u8)glow;
}

// a word on a strip: board behind it or just letters, glowing or not
static Tex *word_strip(const char *s, int n, u32 fg, u32 bg, int board, int glow, float *u1, float *v0, float *v1) {
    int row;
    Tex *t = new_strip(&row);
    int sc = mini(3, (248 - 8) / (n * 6));
    if (sc < 1) sc = 1;
    int w = text_w(n, sc) + 12, y0 = row * 32;
    if (board)
        for (int y = y0 + 2; y < y0 + 30; y++)
            for (int x = 0; x < w; x++) {
                int edge = y < y0 + 4 || y >= y0 + 28 || x < 2 || x >= w - 2;
                t->lv[0][y * 256 + x] = edge ? shade(bg, 0.6f) : shade(bg, 0.92f + (y - y0) * 0.006f);
            }
    put_text(t, 6, y0 + 16 - 7 * sc / 2, s, n, sc, (glow ? GLOW : 0) | fg, board ? 0 : shade(fg, 0.35f));
    *u1 = w / 256.0f; *v0 = (y0 + 2) / 256.0f; *v1 = (y0 + 30) / 256.0f;
    return t;
}

static Tex *poster_tex[3];
static void make_posters(void) {
    for (int a = 0; a < 3; a++) {
        Tex *t = poster_tex[a] = new_tex(8);
        for (int q = 0; q < 4; q++) {
            int k = a * 4 + q, ox = (q & 1) * 128, oy = (q >> 1) * 128;
            u32 c0 = sign_bg[k % 8], c1 = neon_c[(k * 5) % 6];
            for (int y = 0; y < 128; y++)
                for (int x = 0; x < 128; x++) {
                    u32 c = mix(c0, shade(c0, 0.55f), (u32)(y * 2));
                    float dx = x - 92.0f, dy = y - 38.0f;
                    if ((k % 3 == 0 && dx * dx + dy * dy < 26 * 26) || (k % 3 == 1 && ((x + y * 2) / 14) % 2 == 0 && y > 70) ||
                        (k % 3 == 2 && x > 10 && x < 118 && y > 56 && y < 64))
                        c = shade(c1, 0.9f);
                    if (x < 3 || y < 3 || x > 124 || y > 124) c = 0xf0ece4;
                    t->lv[0][(oy + y) * 256 + ox + x] = c;
                }
            const char *w0 = ads[k][0], *w1 = ads[k][1];
            int n0 = 0, n1 = 0;
            while (w0[n0]) n0++;
            while (w1[n1]) n1++;
            int s0 = mini(4, 116 / (n0 * 6)), s1 = mini(3, 116 / (n1 * 6));
            put_text(t, ox + 8, oy + 12, w0, n0, s0 < 1 ? 1 : s0, 0xffffff, 0x101010);
            put_text(t, ox + 8, oy + 128 - 14 - 7 * s1, w1, n1, s1 < 1 ? 1 : s1, shade(c1, 1.1f), 0x101010);
        }
        sign_mips(t);
    }
}

static void make_signs(void) {
    signs = (Sign *)pool(16000 * sizeof(Sign));
    sign_first = (int *)pool((usize)n_blds * 4);
    sign_n = (u8 *)pool((usize)n_blds);
    make_posters();
    // names from the map
    const u8 *r = mapdata + o_names;
    int nn = r[0] | r[1] << 8 | r[2] << 16 | r[3] << 24, idx = 0;
    r += 4;
    for (int i = 0; i < n_blds; i++) { sign_first[i] = 0; sign_n[i] = 0; }
    const u8 **nm = (const u8 **)pool((usize)n_blds * sizeof(u8 *));
    for (int i = 0; i < n_blds; i++) nm[i] = 0;
    bld_name = nm;
    for (int i = 0; i < nn; i++) {
        idx += r[0] | r[1] << 8;
        if (idx < n_blds) nm[idx] = r + 3;
        r += 3 + r[2];
    }
    Tex *last_t = 0;
    for (int i = 0; i < n_blds; i++) {
        Bld *b = &blds[i];
        sign_first[i] = n_signs;
        u32 h = hash2(i, 5150);
        float nx, ny, wl;
        int wall = street_wall(b, &nx, &ny, &wl);
        if (wall < 0) continue;
        float u1, v0, v1;
        if (nm[i]) {
            int n = nm[i][-1];
            const char *s = (const char *)nm[i];
            int tall = b->h >= 8;
            u32 col = tall ? ((h & 3) == 0 ? 0xf0d080 : (h & 3) == 1 ? 0x9ad8ff : 0xffffff) : neon_c[h % 6];
            Tex *t = word_strip(s, n, col, 0, 0, 1, &u1, &v0, &v1);
            float aspect = u1 * 256 / 28.0f;
            float hh = tall ? clampf(b->h * 0.035f, 0.35f, 1.3f) : 0.32f;
            if (hh * aspect > wl * 0.85f) hh = wl * 0.85f / aspect;
            float z1 = tall ? b->h - hh * 0.5f : minf(b->h - 0.12f, (b->shops ? 1.12f : 1.25f) + hh);
            add_sign(b, wall, nx, ny, wl, hh * aspect, z1 - hh, z1, t, u1, v0, v1, tall ? S_NAME_HIGH : S_NAME_LOW, 1, 0.5f);
            last_t = t;
        } else if (!b->upper && b->h < 14 && (b->shops || b->use == 6 || b->use == 4 || b->use == 5 || ((b->use == 2 || b->use == 3) && (h >> 5) % 3 == 0) || (b->use == 0 && b->h < 6 && (h >> 5) % 5 < 2))) {
          int placed = 0;
          for (int w = 0; w < b->n && placed < 4; w++) {
            float wnx, wny, wwl;
            if (!wall_road(b, w, &wnx, &wny, &wwl) || wwl < 1.6f) continue;
            placed++;
            // one strip per shop name, the same shop looks the same wherever it is
            static Tex *shop_t[NSHOP];
            static float shop_uv[NSHOP][3];
            int group = b->shops || b->use == 6 || b->use == 0 ? 0 : b->use == 4 ? 1 : b->use == 5 ? 2 : 3;
            int k = group == 0 ? hash2(i, w * 31 + 7) % 40 : 40 + group * 8 - 8 + hash2(i, 3) % 8, neon = group == 1 || (group == 0 && (k & 3) == 0);
            if (group && placed > 1) break;
            if (!shop_t[k]) {
                const char *s = shop_names[k];
                int n = 0;
                while (s[n]) n++;
                u32 hk = hash2(k, 77);
                u32 bg = sign_bg[(hk >> 4) % 8], fg = neon ? neon_c[(hk >> 12) % 6] : ((bg & 0xff) + (bg >> 8 & 0xff) + (bg >> 16) > 400 ? 0x202020 : 0xf8f4e8);
                shop_t[k] = word_strip(s, n, fg, neon ? 0x101014 : bg, 1, neon, &shop_uv[k][0], &shop_uv[k][1], &shop_uv[k][2]);
            }
            Tex *t = shop_t[k];
            u1 = shop_uv[k][0]; v0 = shop_uv[k][1]; v1 = shop_uv[k][2];
            float aspect = u1 * 256 / 28.0f, hh = 0.28f;
            if (hh * aspect > wwl * 0.8f) hh = wwl * 0.8f / aspect;
            float zs = group ? minf(b->h - hh - 0.15f, group == 3 ? 1.15f : 1.6f) : 1.03f;
            if (zs < 0.5f) break;
            add_sign(b, w, wnx, wny, wwl, hh * aspect, zs, zs + hh, t, u1, v0, v1, S_SHOP, neon, 0.5f);
            last_t = t;
          }
        } else if (!b->upper && b->h > 1.2f && b->h < 9 && (h >> 20) % 4 == 0 && wl > 2.0f) {
            int k = (h >> 3) % NADS;
            float pw = 0.62f, ph = 0.62f;
            Tex *t = poster_tex[k / 4];
            int q = k % 4;
            float u0 = (q & 1) * 0.5f, vv = (q >> 1) * 0.5f;
            int before = n_signs;
            add_sign(b, wall, nx, ny, wl, pw, 0.18f, 0.18f + ph, t, u0 + 0.5f, vv, vv + 0.5f, S_POSTER, 0, 0.25f + (h >> 24 & 7) * 0.07f);
            if (n_signs > before) signs[n_signs - 1].u1 = u0;
        }
        // billboards up on low wide roofs
        if (!b->upper && b->h > 1.4f && b->h < 6 && (b->maxx - b->minx) > 5 && (b->maxy - b->miny) > 5 && (h >> 26) % 5 == 0) {
            int k = (h >> 9) % NADS, q = k % 4;
            float u0 = (q & 1) * 0.5f, vv = (q >> 1) * 0.5f, bw = minf(3.2f, wl * 0.7f);
            int before = n_signs;
            add_sign(b, wall, nx, ny, wl, bw, b->h + 0.55f, b->h + 0.55f + bw * 0.5f, poster_tex[k / 4], u0 + 0.5f, vv, vv + 0.5f, S_BOARD, 0, 0.5f);
            if (n_signs > before) signs[n_signs - 1].u1 = u0;
        }
        sign_n[i] = (u8)mini(255, n_signs - sign_first[i]);
    }
    (void)last_t;
    for (int i = 0; i < n_signs; i++)
        if (signs[i].t->levels == 0) sign_mips(signs[i].t);
}

static void draw_bld_signs(int id) {
    int n = sign_n[id];
    if (!n) return;
    for (int k = 0; k < n; k++) {
        Sign *s = &signs[sign_first[id] + k];
        float mx_ = (s->ax + s->bx) / 2 - cam.x, my_ = (s->ay + s->by) / 2 - cam.y, d2 = mx_ * mx_ + my_ * my_;
        if (s->kind != S_NAME_HIGH && d2 > 60 * 60) continue;
        if (mx_ * s->nx + my_ * s->ny > 0 && s->kind != S_BOARD) continue;
        V3 nrm = v3(s->nx, s->ny, 0);
        Mat m = {s->t, light(0xf0f0f0, nrm), s->kind == S_BOARD, 0, 0, 0, 0, 0, 1};
        float u0 = s->kind == S_POSTER || s->kind == S_BOARD ? s->u1 : 0, u1 = s->kind == S_POSTER || s->kind == S_BOARD ? s->u1 + 0.5f : s->u1;
        V3 a = v3(s->ax, s->ay, s->z0), b = v3(s->bx, s->by, s->z0), c = v3(s->bx, s->by, s->z1), d = v3(s->ax, s->ay, s->z1);
        tri(a, b, c, u0, s->v1, u1, s->v1, u1, s->v0, &m);
        tri(a, c, d, u0, s->v1, u1, s->v0, u0, s->v0, &m);
        if (s->kind == S_BOARD) {
            // two legs and a back
            float lx = (s->bx - s->ax) * 0.2f, ly = (s->by - s->ay) * 0.2f;
            for (int g = 0; g < 2; g++) {
                float px = g ? s->bx - lx : s->ax + lx, py = g ? s->by - ly : s->ay + ly;
                place(v3(px, py, s->z0 - 0.56f), 0);
                mbox(v3(-0.04f, -0.04f, 0), v3(0.04f, 0.04f, 0.56f), t_white, 0x707078);
            }
            Mat bk = {t_white, light(0x505058, scl(nrm, -1)), 0, 0};
            V3 o = v3(-s->nx * 0.05f, -s->ny * 0.05f, 0);
            quad(add(b, o), add(a, o), add(d, o), add(c, o), 1, 1, &bk);
        }
    }
}

