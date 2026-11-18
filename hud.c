// hud: money, stars, radar, busted and wasted
// 5 by 7 font, one byte per row, low 5 bits
static const char glyph_names[] = "0123456789$BUSTEDWA:";
static const u8 glyphs[][7] = {
    {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14}, {14, 17, 1, 2, 4, 8, 31}, {30, 1, 1, 14, 1, 1, 30},
    {2, 6, 10, 18, 31, 2, 2}, {31, 16, 30, 1, 1, 17, 14}, {6, 8, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8},
    {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 2, 12}, {4, 15, 20, 14, 5, 30, 4},
    {30, 17, 17, 30, 17, 17, 30}, {17, 17, 17, 17, 17, 17, 14}, {15, 16, 16, 14, 1, 1, 30}, {31, 4, 4, 4, 4, 4, 4},
    {31, 16, 16, 30, 16, 16, 31}, {28, 18, 17, 17, 17, 18, 28}, {17, 17, 17, 21, 21, 21, 10}, {14, 17, 17, 31, 17, 17, 17}, {0, 12, 12, 0, 12, 12, 0}};

static void rect(int x0, int y0, int x1, int y1, u32 c) {
    x0 = maxi(x0, 0); y0 = maxi(y0, 0); x1 = mini(x1, W); y1 = mini(y1, H);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) fb[y * W + x] = c;
}

static int glyph_index(char ch) {
    for (int i = 0; glyph_names[i]; i++)
        if (glyph_names[i] == ch) return i;
    return -1;
}

// outline first then the letters on top
static void text(const char *s, int x, int y, int px, u32 col) {
    for (int pass = 0; pass < 2; pass++) {
        int cx = x;
        for (const char *p = s; *p; p++, cx += px * 6) {
            int g = glyph_index(*p), f = g < 0 ? font_at(*p) : -1;
            if (g < 0 && f < 0) continue;
            const u8 *rows = g >= 0 ? glyphs[g] : font5[f];
            for (int r = 0; r < 7; r++)
                for (int c = 0; c < 5; c++)
                    if (rows[r] >> (4 - c) & 1) {
                        int X = cx + c * px, Y = y + r * px;
                        if (pass == 0) rect(X - px / 2 - 1, Y - px / 2 - 1, X + px + px / 2 + 1, Y + px + px / 2 + 1, 0x0c0a10);
                        else rect(X, Y, X + px, Y + px, col);
                    }
        }
    }
}

static void fill_tri2(float ax, float ay, float bx, float by, float cx, float cy, u32 col) {
    int x0 = maxi(0, (int)minf(ax, minf(bx, cx))), x1 = mini(W - 1, (int)maxf(ax, maxf(bx, cx)) + 1);
    int y0 = maxi(0, (int)minf(ay, minf(by, cy))), y1 = mini(H - 1, (int)maxf(ay, maxf(by, cy)) + 1);
    float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    if (area == 0) return;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float e0 = (bx - ax) * (py - ay) - (by - ay) * (px - ax);
            float e1 = (cx - bx) * (py - by) - (cy - by) * (px - bx);
            float e2 = (ax - cx) * (py - cy) - (ay - cy) * (px - cx);
            if ((area > 0 && e0 >= 0 && e1 >= 0 && e2 >= 0) || (area < 0 && e0 <= 0 && e1 <= 0 && e2 <= 0)) fb[y * W + x] = col;
        }
}

static void star(float cx, float cy, float R, u32 col) {
    for (int k = 0; k < 5; k++) {
        float a0 = -PI / 2 + k * TAU / 5, a1 = a0 + TAU / 10, a2 = a0 + TAU / 5;
        float ox = cx + cosf_(a0) * R, oy = cy + sinf_(a0) * R;
        float ix = cx + cosf_(a1) * R * 0.42f, iy = cy + sinf_(a1) * R * 0.42f;
        float nx = cx + cosf_(a2) * R, ny = cy + sinf_(a2) * R;
        fill_tri2(cx, cy, ox, oy, ix, iy, col);
        fill_tri2(cx, cy, ix, iy, nx, ny, col);
    }
}

// round radar bottom left turned so up is where we face
static void radar(void) {
    int R = H / 8, cx = H / 20 + R, cy = H - H / 20 - R;
    float sc = 0.5f;
    float fx = cosf_(cam_yaw_s + look_yaw), fy = sinf_(cam_yaw_s + look_yaw);
    int flash = wanted && (tick >> 4 & 1);
    for (int y = -R - 3; y <= R + 3; y++)
        for (int x = -R - 3; x <= R + 3; x++) {
            int d2 = x * x + y * y;
            if (d2 > (R + 3) * (R + 3)) continue;
            u32 *p = &fb[(cy + y) * W + cx + x];
            if (d2 > R * R) { *p = flash ? ((tick >> 3 & 1) ? 0xff2030 : 0x3050ff) : 0x101014; continue; }
            float wx = me_x + sc * (-y * fx + x * fy), wy = me_y + sc * (-y * fy - x * fx);
            static const u32 kinds[6] = {0x2a6a9a, 0x8a8a92, 0x5a5a66, 0x3a3a48, 0xc8b07a, 0x4a8a4a};
            u32 c = kinds[ground_kind(wx, wy)];
            *p = mix(*p, c, 215);
        }
    // free boats so you can find one
    for (int i = BOAT0; i < CARS; i++) {
        Car *c = &cars[i];
        if (c->mode != M_PARKED || c->wreck) continue;
        float dx = c->x - me_x, dy = c->y - me_y;
        float ux = (dx * fy - dy * fx) / sc, uy = -(dx * fx + dy * fy) / sc;
        if (ux * ux + uy * uy > (R - 4) * (R - 4)) continue;
        rect(cx + (int)ux - 3, cy + (int)uy - 2, cx + (int)ux + 4, cy + (int)uy + 3, 0x101014);
        rect(cx + (int)ux - 2, cy + (int)uy - 1, cx + (int)ux + 3, cy + (int)uy + 2, 0xf0f8ff);
    }
    // cops as blinking dots
    for (int i = 1; i < 5; i++) {
        Car *c = &cars[i];
        if (c->mode != M_COP) continue;
        float dx = c->x - me_x, dy = c->y - me_y;
        float ux = (dx * fy - dy * fx) / sc, uy = -(dx * fx + dy * fy) / sc;
        float l = sqrtf_(ux * ux + uy * uy);
        if (l > R - 4) { ux *= (R - 4) / l; uy *= (R - 4) / l; }
        u32 col = (tick >> 3 & 1) ? 0xff3040 : 0x4060ff;
        rect(cx + (int)ux - 3, cy + (int)uy - 3, cx + (int)ux + 4, cy + (int)uy + 4, col);
    }
    // gps line along the roads and the target at the edge when its far
    if (gps_on) {
        for (int i = 0; i < route_n; i++) {
            Edge *e = &edges[route_e[i]];
            for (int k = 0; k + 1 < e->n; k++) {
                float *a = &ep[(e->first + k) * 2], *b = a + 2;
                float L = sqrtf_((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]));
                for (float t = 0; t < L; t += 0.8f) {
                    float dx = a[0] + (b[0] - a[0]) * t / L - me_x, dy = a[1] + (b[1] - a[1]) * t / L - me_y;
                    if (dx * dx + dy * dy > (R * sc) * (R * sc)) continue;
                    float ux = (dx * fy - dy * fx) / sc, uy = -(dx * fx + dy * fy) / sc;
                    if (ux * ux + uy * uy > (R - 3) * (R - 3)) continue;
                    rect(cx + (int)ux - 2, cy + (int)uy - 2, cx + (int)ux + 2, cy + (int)uy + 2, 0xc050ff);
                }
            }
        }
        float dx = gps_x - me_x, dy = gps_y - me_y;
        float ux = (dx * fy - dy * fx) / sc, uy = -(dx * fx + dy * fy) / sc;
        float l = sqrtf_(ux * ux + uy * uy);
        if (l > R - 5) { ux *= (R - 5) / l; uy *= (R - 5) / l; }
        u32 col = job == J_PICKUP ? 0x4080ff : 0xffd030;
        rect(cx + (int)ux - 5, cy + (int)uy - 5, cx + (int)ux + 6, cy + (int)uy + 6, 0x101014);
        rect(cx + (int)ux - 4, cy + (int)uy - 4, cx + (int)ux + 5, cy + (int)uy + 5, col);
    }
    float s = R * 0.09f;
    fill_tri2(cx, cy - s * 1.6f, cx - s, cy + s, cx + s, cy + s, 0xffffff);
}

static int num_text(char *b, int n) {
    char d[12];
    int m = 0, k = 0;
    if (n < 0) n = 0;
    do { d[m++] = (char)('0' + n % 10); n /= 10; } while (n);
    while (m) b[k++] = d[--m];
    b[k] = 0;
    return k;
}

static void draw_hud(void) {
    int px = H / 160 + 1;
    char buf[16];
    buf[0] = '$';
    int m = money < 0 ? 0 : money;
    for (int i = 8; i >= 1; i--) { buf[i] = '0' + m % 10; m /= 10; }
    buf[9] = 0;
    int tw = 9 * px * 6, top = px * 5 + px * 10;
    {
        int hh = (int)tod, mm = (int)((tod - hh) * 60);
        char ck[6] = {(char)('0' + hh / 10), (char)('0' + hh % 10), ':', (char)('0' + mm / 10), (char)('0' + mm % 10), 0};
        text(ck, W - 5 * px * 6 - px * 6, px * 5, px, 0xf0f0f4);
    }
    text(buf, W - tw - px * 6, top, px, 0x6fd36a);
    {
        int x0 = W - tw - px * 6, x1 = W - px * 6 - px, y = top + px * 9, h = px * 2 + 1;
        rect(x0 - 2, y - 2, x1 + 2, y + h + 2, 0x0c0a10);
        rect(x0, y, x1, y + h, 0x5a1414);
        u32 hc = my_hp < 25 && (tick >> 4 & 1) ? 0xff6a6a : 0xd83030;
        rect(x0, y, x0 + (int)((x1 - x0) * my_hp / 100), y + h, hc);
        if (my_armour > 0) {
            y += h + px + 2;
            rect(x0 - 2, y - 2, x1 + 2, y + h + 2, 0x0c0a10);
            rect(x0, y, x1, y + h, 0x3a3a44);
            rect(x0, y, x0 + (int)((x1 - x0) * my_armour / 100), y + h, 0xe8e8f0);
        }
        top = y + h - px * 2;
    }
    float R = px * 4.5f;
    // stars only while the police want you, and a few seconds after
    static int star_show;
    if (wanted > 0) star_show = 300; else if (star_show > 0) star_show--;
    for (int i = 0; i < 5 && star_show > 0; i++) {
        float sx = W - px * 8 - (4 - i) * R * 2.3f - R, sy = top + px * 7 + px * 4 + R;
        star(sx, sy, R + px * 0.8f, 0x0c0a10);
        // steady while they can see you, slow flash while they are searching
        u32 col = i < wanted ? (seen ? 0xffd23f : ((tick >> 5 & 1) ? 0x8a7a40 : 0xffd23f)) : 0x55555f;
        star(sx, sy, R, col);
    }
    radar();
    // weapon and ammo under the stars
    {
        static const char *wn[3] = {"FISTS", "PISTOL ", "SMG "};
        char b[24];
        int k = 0;
        for (const char *q = wn[weapon]; *q; q++) b[k++] = *q;
        if (weapon) k += num_text(b + k, ammo[weapon]);
        b[k] = 0;
        if (mine < 0) text(b, W - k * px * 6 - px * 6, top + px * 23, px, 0xf0e8d0);
    }
    // job lines in the middle at the bottom, the clock and fares on the right
    if (job_msg_t) {
        int n = 0;
        while (job_msg[n]) n++;
        int ps = px > 2 ? px - 1 : px;
        text(job_msg, (int)half_w - n * ps * 3, H - H / 6, ps, 0xffe080);
    }
    if (job != J_NONE) {
        char b[32];
        int k = 0, sec = job_t / 60;
        const char *l = "TIME ";
        for (; *l; l++) b[k++] = *l;
        k += num_text(b + k, sec / 60);
        b[k++] = ':'; b[k++] = (char)('0' + sec % 60 / 10); b[k++] = (char)('0' + sec % 10); b[k] = 0;
        text(b, W - k * px * 6 - px * 6, top + px * 33, px, sec < 15 && (tick >> 4 & 1) ? 0xff6060 : 0xffffff);
        k = 0;
        l = "FARES ";
        for (; *l; l++) b[k++] = *l;
        k += num_text(b + k, job_fares);
        b[k] = 0;
        text(b, W - k * px * 6 - px * 6, top + px * 43, px, 0xffffff);
    }
#ifdef FPS
    {
        extern int frames;
        static int last_tick, last_frames, fps10;
        if (tick - last_tick >= 120) { fps10 = (frames - last_frames) * 10 / 2; last_tick = tick; last_frames = frames; }
        char b[8];
        int v = fps10;
#ifdef BOATDBG
        v = 0; for (int i = BOAT0; i < CARS; i++) if (cars[i].mode) v += cars[i].mode == M_PARKED ? 1 : 10;
#endif
        for (int i = 3; i >= 0; i--) { b[i] = '0' + v % 10; v /= 10; }
        b[4] = ' '; b[5] = '0' + nwork % 10; b[6] = 0;
        text(b, 20, 20, 4, 0xffffff);
    }
#endif
#ifdef PIXDBG
    for (int k = 0; k < 16; k++) {
        float v = dbg_tri[k];
        int iv = (int)(absf(v) * 100);
        char b[12];
        for (int i = 7; i >= 0; i--) { b[i] = '0' + iv % 10; iv /= 10; }
        b[8] = 0;
        text(b, 20 + (k % 5) * 200, 200 + (k / 5) * 30, 3, v < 0 ? 0xff4040 : 0xffffff);
    }
    rect(1098, 58, 1103, 63, 0xff00ff);
#endif
    if (caught && caught < 180) {
        // grey it out like the real one
        for (int i = 0; i < W * H; i++) {
            u32 c = fb[i];
            u32 g = ((c >> 16 & 255) * 77 + (c >> 8 & 255) * 150 + (c & 255) * 29) >> 8;
            g = g * 3 / 4;
            fb[i] = g << 16 | g << 8 | g;
        }
        int big = H / 40;
        const char *w = caught_how ? "WASTED" : "BUSTED";
        text(w, (int)half_w - 6 * big * 6 / 2, (int)half_h - big * 4, big, caught_how ? 0xd82828 : 0x8fd8ff);
    }
#ifdef TELE
    if (mine >= 0) {
        Car *c = &cars[mine];
        char t[32];
        int kmh = (int)(absf(fwd_speed(c)) * 60 * 2.8f * 3.6f), sl = (int)(absf(wrapf(atan2f_(c->vy, c->vx) - c->a)) * 57.3f);
        if (speed2(c) < 0.0004f) sl = 0;
        int n = 0;
        t[n++] = '0' + kmh / 100 % 10; t[n++] = '0' + kmh / 10 % 10; t[n++] = '0' + kmh % 10; t[n++] = ' ';
        t[n++] = '0' + sl / 100 % 10; t[n++] = '0' + sl / 10 % 10; t[n++] = '0' + sl % 10; t[n] = 0;
        text(t, px * 6, px * 5, px, 0xffffff);
    }
#endif
}
