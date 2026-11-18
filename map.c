// full screen map on m: the game waits, scroll zooms round the cursor, drag moves it, a click sets a waypoint

static int map_open, map_drag, map_moved, map_was_left, map_was_right;
static float map_cx, map_cy, map_s = 0.6f, cur_x, cur_y;

static void map_toggle(void) {
    map_open = !map_open;
    mouse_dx = mouse_dy = mwheel = 0;
    if (map_open) { map_cx = me_x; map_cy = me_y; cur_x = W * 0.5f; cur_y = H * 0.5f; map_was_left = mouse_left; map_was_right = mouse_right; }
}

static void map_job(int w, int nw) {
    static const u32 base[4] = {0x1d4a74, 0x585c63, 0x3e7a3c, 0xcdb67e};
    for (int y = 0; y < H; y++) {
        if (nw > 1 && ((y >> 3) % nw) != w) { y |= 7; continue; }
        u32 *row = fb + y * W;
        float wy = map_cy - (y - H * 0.5f) * map_s;
        for (int x = 0; x < W; x++) {
            float wx = map_cx + (x - W * 0.5f) * map_s;
            if (wx < 0 || wy < 0 || wx >= WX || wy >= WY) { row[x] = base[0]; continue; }
            int c = cell(wx, wy);
            u32 col = base[c & 3];
            if (c & C_LOT) col = 0x4c4f56;
            if (c & C_ROAD) col = (c & C_BRIDGE) ? 0xb4b0a8 : 0xa4a8b0;
            if (c & C_BLD) col = 0x33363d;
            row[x] = col;
        }
    }
}

static void to_screen(float wx, float wy, float *sx, float *sy) {
    *sx = W * 0.5f + (wx - map_cx) / map_s;
    *sy = H * 0.5f - (wy - map_cy) / map_s;
}

static void map_dot(float wx, float wy, int r, u32 col) {
    float sx, sy;
    to_screen(wx, wy, &sx, &sy);
    if (sx < -r || sy < -r || sx > W + r || sy > H + r) return;
    rect((int)sx - r - 1, (int)sy - r - 1, (int)sx + r + 2, (int)sy + r + 2, 0x0c0a10);
    rect((int)sx - r, (int)sy - r, (int)sx + r + 1, (int)sy + r + 1, col);
}

static void map_line(float ax, float ay, float bx, float by, int r, u32 col) {
    float x0, y0, x1, y1;
    to_screen(ax, ay, &x0, &y0);
    to_screen(bx, by, &x1, &y1);
    float dx = x1 - x0, dy = y1 - y0, L = sqrtf_(dx * dx + dy * dy);
    int n = (int)L + 1;
    for (int i = 0; i <= n; i++) {
        float x = x0 + dx * i / n, y = y0 + dy * i / n;
        if (x < 0 || y < 0 || x >= W || y >= H) continue;
        rect((int)x - r, (int)y - r, (int)x + r + 1, (int)y + r + 1, col);
    }
}

static int tlen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static void map_input(void) {
    // wheel: zoom keeping the spot under the cursor where it is
    if (mwheel) {
        float wx = map_cx + (cur_x - W * 0.5f) * map_s, wy = map_cy - (cur_y - H * 0.5f) * map_s;
        for (int i = 0; i < (mwheel < 0 ? -mwheel : mwheel); i++) map_s *= mwheel < 0 ? 0.8f : 1.25f;
        map_s = clampf(map_s, 0.06f, 3.0f);
        map_cx = wx - (cur_x - W * 0.5f) * map_s; map_cy = wy + (cur_y - H * 0.5f) * map_s;
        mwheel = 0;
    }
    if (mouse_left) {
        if (!map_was_left) { map_drag = 1; map_moved = 0; }
        map_cx -= mouse_dx * map_s; map_cy -= mouse_dy * map_s;
        map_moved += (mouse_dx < 0 ? -mouse_dx : mouse_dx) + (mouse_dy < 0 ? -mouse_dy : mouse_dy);
    } else {
        cur_x = clampf(cur_x + mouse_dx, 0, W - 1); cur_y = clampf(cur_y - mouse_dy, 0, H - 1);
        // a click that didnt drag sets the waypoint where the cursor is
        if (map_was_left && map_drag && map_moved < 6) {
            wp_x = map_cx + (cur_x - W * 0.5f) * map_s; wp_y = map_cy - (cur_y - H * 0.5f) * map_s;
            wp_on = 1; gps_on = 0;
        }
        map_drag = 0;
    }
    if (mouse_right && !map_was_right) { wp_on = 0; if (job == J_NONE) gps_on = 0; }
    map_was_left = mouse_left; map_was_right = mouse_right;
    mouse_dx = mouse_dy = 0;
    // arrow keys pan too
    float k = 8 * map_s;
    if (hold[0]) map_cy += k;
    if (hold[1]) map_cy -= k;
    if (hold[2]) map_cx += k;
    if (hold[3]) map_cx -= k;
    map_cx = clampf(map_cx, 0, WX); map_cy = clampf(map_cy, 0, WY);
}

static void map_frame(void) {
    map_input();
    run_job(map_job);
    // names when close enough to read them, one per patch of screen so they dont pile up
    if (map_s < 0.4f && bld_name) {
        static u8 used[64 * 48];
        for (int i = 0; i < 64 * 48; i++) used[i] = 0;
        int ps = map_s < 0.15f ? 2 : 1;
        for (int i = 0; i < n_blds; i++) {
            if (!bld_name[i]) continue;
            Bld *b = &blds[i];
            float sx, sy;
            to_screen((b->minx + b->maxx) / 2, (b->miny + b->maxy) / 2, &sx, &sy);
            if (sx < 20 || sy < 20 || sx > W - 20 || sy > H - 20) continue;
            int gx = (int)sx * 64 / W, gy = (int)sy * 48 / H;
            if (used[gy * 64 + gx]) continue;
            int n = mini(bld_name[i][-1], 30);
            char t[32];
            for (int k = 0; k < n; k++) t[k] = (char)bld_name[i][k];
            t[n] = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -2; dx <= 2; dx++)
                    if (gx + dx >= 0 && gx + dx < 64 && gy + dy >= 0 && gy + dy < 48) used[(gy + dy) * 64 + gx + dx] = 1;
            text(t, (int)sx - n * ps * 3, (int)sy - ps * 3, ps, 0xf0ecd8);
        }
    }
    // gps route
    if (gps_on)
        for (int i = 0; i < route_n; i++) {
            Edge *e = &edges[route_e[i]];
            for (int k = 0; k + 1 < e->n; k++) {
                float *a = &ep[(e->first + k) * 2];
                map_line(a[0], a[1], a[2], a[3], 2, 0xc050ff);
            }
        }
    for (int i = 0; i < PICKS; i++)
        if (picks[i].on) map_dot(picks[i].x, picks[i].y, 3, picks[i].kind == 2 ? 0xff9a20 : picks[i].kind ? 0x3a9cff : 0x3cff6a);
    for (int i = BOAT0; i < CARS; i++)
        if (cars[i].mode == M_PARKED && !cars[i].wreck) map_dot(cars[i].x, cars[i].y, 3, 0xf0f8ff);
    for (int i = 1; i < TRAF0; i++)
        if (cars[i].mode == M_COP) map_dot(cars[i].x, cars[i].y, 4, (tick_now >> 3 & 1) ? 0xff3040 : 0x4060ff);
    if (job != J_NONE) map_dot(job_x, job_y, 6, job == J_PICKUP ? 0x4080ff : 0xffd030);
    if (wp_on) map_dot(wp_x, wp_y, 6, 0xd040ff);
    // you, as an arrow pointing the way you face
    {
        float sx, sy;
        to_screen(me_x, me_y, &sx, &sy);
        float a = me_a, r = 11, c = cosf_(a), s = sinf_(a);
        float tx = sx + c * r, ty = sy - s * r, lx = sx + cosf_(a + 2.5f) * r * 0.75f, ly = sy - sinf_(a + 2.5f) * r * 0.75f;
        float qx = sx + cosf_(a - 2.5f) * r * 0.75f, qy = sy - sinf_(a - 2.5f) * r * 0.75f;
        fill_tri2(tx, ty, lx, ly, qx, qy, 0x0c0a10);
        fill_tri2(tx + (sx - tx) * 0.15f, ty + (sy - ty) * 0.15f, lx + (sx - lx) * 0.2f, ly + (sy - ly) * 0.2f, qx + (sx - qx) * 0.2f, qy + (sy - qy) * 0.2f, 0xffffff);
    }
    // cursor
    rect((int)cur_x - 12, (int)cur_y - 1, (int)cur_x + 13, (int)cur_y + 2, 0x0c0a10);
    rect((int)cur_x - 1, (int)cur_y - 12, (int)cur_x + 2, (int)cur_y + 13, 0x0c0a10);
    rect((int)cur_x - 11, (int)cur_y, (int)cur_x + 12, (int)cur_y + 1, 0xffffff);
    rect((int)cur_x, (int)cur_y - 11, (int)cur_x + 1, (int)cur_y + 12, 0xffffff);
    int px = H / 160 + 1;
    text("MIAMI", px * 6, px * 5, px + 1, 0xf0f0f4);
    const char *help = "SCROLL ZOOM   DRAG MOVE   CLICK WAYPOINT   RIGHT CLICK CLEAR   M CLOSE";
    int ps = mini(px, (W - 40) / (tlen(help) * 6));
    if (ps < 1) ps = 1;
    text(help, (W - tlen(help) * ps * 6) / 2, H - ps * 14, ps, 0xd8d8e0);
#ifdef WHEELDBG
    { char b[16]; int v = mouse_wheel_ok * 1000 + (int)(map_s * 100); int k = num_text(b, v); (void)k; text(b, px * 6, px * 20, px, 0xffff00); }
#endif
}
