// todo sound card driver
// todo motorbikes maybe
// todo try it on a real pc not just qemu

#include "efi.h"
#include "base.c"

static BootServices *bs;
static u8 *arena;
static usize arena_left;
// arena first then beg the firmware for memory
static u32 *pool(usize bytes) {
    bytes = (bytes + 15) & ~(usize)15;
    if (arena && arena_left >= bytes) {
        void *p = arena;
        arena += bytes; arena_left -= bytes;
        return (u32 *)p;
    }
    void *p = 0;
    bs->alloc_pool(2, bytes, &p);
    return (u32 *)p;
}
// port io asm i never remember this syntax lol
static inline u8 inb(u16 port) { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static inline void outb(u16 port, u8 v) { __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port)); }
#include "mp.c"
static inline int abs_i(int x) { return x < 0 ? -x : x; }

// one big build no linker drama
#include "gfx.c"
#include "tex.c"
#include "models.c"
#include "lzma.c"
#include "world.c"
#include "signs.c"
#include "game.c"
#include "people.c"
#include "jobs.c"
#ifdef POSETEST
#define AIMHOLD || 1
#else
#define AIMHOLD
#endif
#include "hud.c"
#include "map.c"

static Gop *gop;
static TextIn *con_in;


static int raw_kbd;
static u8 kdown[256];
static int k_ext;

// mouse shares the keyboard chip ngl 3 byte packets
static u8 mpk[4];
static int mpn;
static void ps2_wait(void) { for (int i = 0; i < 100000 && (inb(0x64) & 2); i++) {} }
static void ps2_mouse_cmd(u8 c) {
    ps2_wait(); outb(0x64, 0xd4);
    ps2_wait(); outb(0x60, c);
    for (int i = 0; i < 100000; i++) if ((inb(0x64) & 0x21) == 0x21) { inb(0x60); break; }
}
// wheel unlock is 3 sample rates then it says id 3 lowkey cursed
static void mouse_on(void) {
    ps2_wait(); outb(0x64, 0xa8);
    ps2_mouse_cmd(0xf6);
    static const u8 rates[3] = {200, 100, 80};
    for (int i = 0; i < 3; i++) { ps2_mouse_cmd(0xf3); ps2_mouse_cmd(rates[i]); }
    ps2_wait(); outb(0x64, 0xd4);
    ps2_wait(); outb(0x60, 0xf2);
    u8 got[2] = {0, 0};
    int n = 0;
    for (int i = 0; i < 400000 && n < 2; i++) if ((inb(0x64) & 0x21) == 0x21) got[n++] = inb(0x60);
    mouse_wheel_ok = n == 2 && got[1] == 3;
    ps2_mouse_cmd(0xf4);
    while (inb(0x64) & 1) inb(0x60);
    mpn = 0;
}
static void mouse_byte(u8 b) {
    if (mpn == 0 && !(b & 8)) return;
    mpk[mpn++] = b;
    if (mpn < (mouse_wheel_ok ? 4 : 3)) return;
    mpn = 0;
    if (mpk[0] & 0xc0) return;
    int dx = mpk[1] - ((mpk[0] << 4) & 0x100), dy = mpk[2] - ((mpk[0] << 3) & 0x100);
    mouse_dx += dx; mouse_dy += dy;
    mouse_left = mpk[0] & 1;
    mouse_right = (mpk[0] >> 1) & 1;
    if (mouse_wheel_ok) mwheel += (int)((i8)(mpk[3] << 4) >> 4);
}

static void raw_keys(void) {
    for (int n = 0; n < 64; n++) {
        // port 64 says if a byte is waiting 60 is the byte
        u8 st = inb(0x64);
        if (!(st & 1)) break;
        u8 sc = inb(0x60);
        if (st & 0x20) { mouse_byte(sc); continue; }
        if (sc == 0xe0) { k_ext = 128; continue; }
        if (sc == 0xe1 || sc == 0xfa || sc == 0xfe) { k_ext = 0; continue; }
        // top bit set means the key got let go
        int code = (sc & 0x7f) | k_ext, down = !(sc & 0x80);
        k_ext = 0;
        if (down && !kdown[code] && (code == 0x1c || code == 0x21 || code == 0x9c)) enter_tap = 1;
        if (down && !kdown[code] && code == 0x24) key_job = 1;
        if (down && !kdown[code] && (code == 0x32 || (code == 0x01 && map_open))) key_map = 1;
        if (down && !kdown[code] && code >= 0x02 && code <= 0x04) key_wpn = code - 1;
        kdown[code] = (u8)down;
    }
    int map[5][3] = {{0xc8, 0x11, 0}, {0xd0, 0x1f, 0}, {0xcd, 0x20, 0}, {0xcb, 0x1e, 0}, {0x39, 0, 0}};
    for (int i = 0; i < 5; i++) {
        int d = 0;
        for (int j = 0; j < 3; j++) if (map[i][j] && kdown[map[i][j]]) d = 1;
        hold[i] = d ? 8 : 0;
    }
    fire_held = kdown[0x1d] || kdown[0x9d] || kdown[0x2d];
}

static void read_keys(void) {
    if (raw_kbd) { raw_keys(); return; }
    InputKey k;
    while (con_in->read_key(con_in, &k) == 0) {
        if (k.ch == 13 || k.ch == 'f' || k.ch == 'F') enter_tap = 1;
        else if (k.ch == ' ') hold[4] = 18;
        else if (k.ch == 'x' || k.ch == 'X') fire_tap = 18;
        else if (k.ch == 'j' || k.ch == 'J') key_job = 1;
        else if (k.ch == 'm' || k.ch == 'M') key_map = 1;
        else if (k.ch >= '1' && k.ch <= '3') key_wpn = k.ch - '0';
        else if (k.scan >= 1 && k.scan <= 4) hold[k.scan - 1] = 18;
    }
}

typedef struct { float r, g, b; } C3;
static const float sky_e[4] = {-0.12f, 0.0f, 0.12f, 0.45f};
static const C3 k_zen[4] = {{0.012f, 0.018f, 0.05f}, {0.10f, 0.13f, 0.32f}, {0.22f, 0.36f, 0.66f}, {0.16f, 0.40f, 0.85f}};
static const C3 k_near[4] = {{0.03f, 0.04f, 0.09f}, {0.95f, 0.40f, 0.18f}, {1.0f, 0.70f, 0.42f}, {0.80f, 0.86f, 0.96f}};
static const C3 k_far[4] = {{0.03f, 0.04f, 0.09f}, {0.32f, 0.30f, 0.48f}, {0.62f, 0.62f, 0.76f}, {0.68f, 0.78f, 0.93f}};
static const C3 k_sun[4] = {{1.0f, 0.30f, 0.10f}, {1.0f, 0.38f, 0.14f}, {1.0f, 0.66f, 0.36f}, {1.0f, 0.95f, 0.86f}};
static const C3 k_amb[4] = {{0.17f, 0.19f, 0.28f}, {0.34f, 0.30f, 0.42f}, {0.46f, 0.44f, 0.54f}, {0.46f, 0.50f, 0.60f}};
static const C3 k_cdark[4] = {{0.05f, 0.06f, 0.10f}, {0.30f, 0.22f, 0.32f}, {0.58f, 0.46f, 0.52f}, {0.78f, 0.80f, 0.86f}};
static const C3 k_clit[4] = {{0.11f, 0.12f, 0.17f}, {1.0f, 0.48f, 0.28f}, {1.0f, 0.80f, 0.58f}, {1.0f, 1.0f, 1.0f}};
static C3 c_zen, c_near, c_far, c_sun, c_cdark, c_clit;

static C3 lerp3(C3 a, C3 b, float t) { C3 r = {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; return r; }
static C3 keyed(const C3 *k, float e) {
    if (e <= sky_e[0]) return k[0];
    for (int i = 0; i < 3; i++)
        if (e <= sky_e[i + 1]) return lerp3(k[i], k[i + 1], (e - sky_e[i]) / (sky_e[i + 1] - sky_e[i]));
    return k[3];
}
static u32 pack3(C3 c) { return rgb((int)(c.r * 255), (int)(c.g * 255), (int)(c.b * 255)); }

static void set_time(void) {
    float t = tod;
    float az = -(t - 6) / 12 * PI, e = sinf_(PI * (t - 6) / 12) * 0.95f, ce = sqrtf_(1 - e * e);
    sun_true = v3(cosf_(az) * ce, sinf_(az) * ce, e);
    c_zen = keyed(k_zen, e); c_near = keyed(k_near, e); c_far = keyed(k_far, e); c_sun = keyed(k_sun, e);
    c_cdark = keyed(k_cdark, e); c_clit = keyed(k_clit, e);
    C3 amb = keyed(k_amb, e);
    sun_vis = clampf((e + 0.02f) / 0.1f, 0, 1);
    night_k = clampf(-(e - 0.04f) / 0.12f, 0, 1);
    float sk = sun_vis * (0.5f + 0.4f * (1 - clampf(e, 0, 1)));
    if (e > 0.0f) {
        sun_dir = norm(v3(sun_true.x, sun_true.y, maxf(e, 0.06f)));
        sun_r = c_sun.r * sk; sun_g = c_sun.g * sk; sun_b = c_sun.b * sk;
    } else {
        sun_dir = norm(v3(-sun_true.x, -sun_true.y, 0.5f));
        sun_r = 0.10f; sun_g = 0.12f; sun_b = 0.18f;
    }
    amb_r = amb.r; amb_g = amb.g; amb_b = amb.b;
    lum_amb = (amb.r + amb.g + amb.b) / 1.6f;
    lum_sun = (sun_r + sun_g + sun_b) / 1.9f;
    float m = (amb.r + amb.g + amb.b) / 3;
    cast_r = 0.5f + 0.5f * amb.r / m; cast_g = 0.5f + 0.5f * amb.g / m; cast_b = 0.5f + 0.5f * amb.b / m;
    glow_m = (u32)(150 + 106 * clampf(night_k + (1 - sun_vis), 0, 1));
}

static C3 haze_col(V3 d) {
    float w = (dot(d, sun_true) + 1) * 0.5f;
    w = w * w * w;
    C3 hor = lerp3(c_far, c_near, w * 0.7f);
    C3 low = {hor.r * 0.62f, hor.g * 0.64f, hor.b * 0.7f};
    return lerp3(hor, low, clampf(-d.z * 5, 0, 1) * 0.6f + 0.25f);
}

static C3 sky_col(V3 d) {
    float mu = dot(d, sun_true), w = (mu + 1) * 0.5f;
    w = w * w * w;
    C3 hor = lerp3(c_far, c_near, w);
    if (d.z <= 0) return haze_col(d);
    float t = minf(1, sqrtf_(d.z * 1.3f));
    C3 c = lerp3(hor, c_zen, t);
    float gv = clampf((sun_true.z + 0.1f) / 0.12f, 0, 1);
    if (mu > 0 && gv > 0) {
        float m2 = mu * mu, m8 = m2 * m2; m8 *= m8;
        float m64 = m8 * m8; m64 *= m64; m64 *= m64;
        float g = (m8 * 0.22f + m64 * 0.5f) * gv;
        c.r += c_sun.r * g; c.g += c_sun.g * g; c.b += c_sun.b * g;
        if (mu > 0.9989f) {
            float k = clampf((mu - 0.9989f) / 0.0004f, 0, 1) * gv;
            c.r += (1.4f) * k; c.g += (0.9f + c_sun.g) * k; c.b += (0.4f + c_sun.b) * k;
        }
    }
    if (night_k > 0) {
        V3 md = v3(-sun_true.x, -sun_true.y, -sun_true.z + 0.3f);
        md = norm(md);
        float mm = dot(d, md);
        if (mm > 0.9992f) { float k = clampf((mm - 0.9992f) / 0.0002f, 0, 1) * night_k; c.r += 0.75f * k; c.g += 0.76f * k; c.b += 0.72f * k; }
        else if (mm > 0.99f) { float k = (mm - 0.99f) * 8 * night_k; c.r += 0.05f * k; c.g += 0.06f * k; c.b += 0.09f * k; }
        if (d.z > 0.04f) {
            float q = 1 / (d.z + 0.35f);
            u32 h = hash2((int)(d.x * q * 260), (int)(d.y * q * 260));
            if ((h & 1023) < 3) { float k = night_k * (0.35f + (h >> 10 & 255) / 400.0f) * minf(1, d.z * 6); c.r += k; c.g += k; c.b += k * 1.05f; }
        }
    }
    return c;
}

static void sky_job(int w, int nw) {
    for (int y = 0; y < H; y++) {
        if (nw > 1 && ((y >> 3) % nw) != w) { y |= 7; continue; }
        u32 *r = fb + y * W;
        float *z = zb + y * W;
        float fy = (y + 0.5f - half_h) / focal;
        V3 d0 = sub(cam_f, scl(cam_u, fy));
        for (int x = 0; x < W; x += 2) {
            // sky only where nothing got drawn
            if (z[x] != 0 && (x + 1 >= W || z[x + 1] != 0)) continue;
            float fx = (x + 1.0f - half_w) / focal;
            V3 d = norm(add(d0, scl(cam_r, fx)));
            u32 c = d.z <= 0 ? fog_col : pack3(sky_col(d));
            if (d.z < 0.03f && d.z > 0) c = mix(fog_col, c, (u32)(clampf(d.z / 0.03f, 0, 1) * 256));
            if (z[x] == 0) r[x] = c;
            if (x + 1 < W && z[x + 1] == 0) r[x + 1] = c;
        }
    }
}

static void cloud_job(int w, int nw) {
    float hy = half_h + focal * (sinf_(-cam_pitch) / cosf_(cam_pitch));
    float hl = sqrtf_(sun_true.x * sun_true.x + sun_true.y * sun_true.y) + 1e-6f;
    float sx = sun_true.x / hl, sy = sun_true.y / hl;
    u32 dark = pack3(c_cdark), lite = pack3(c_clit);
    u32 sunny = pack3(lerp3(c_clit, c_sun, 0.4f));
    Tex *t = t_cloud;
    for (int y = 0; y < H && y < hy; y++) {
        if (nw > 1 && ((y >> 3) % nw) != w) { y |= 7; continue; }
        float fy = (y + 0.5f - half_h) / focal;
        V3 d0 = sub(cam_f, scl(cam_u, fy));
        float *zr = zb + y * W;
        for (int x = 0; x < W; x += 2) {
            if (zr[x] != 0 && (x + 1 >= W || zr[x + 1] != 0)) continue;
            float fx = (x + 1.0f - half_w) / focal;
            V3 d = add(d0, scl(cam_r, fx));
            if (d.z < 0.02f) continue;
            float tt = (90.0f - cam.z) / d.z;
            float px = (cam.x + d.x * tt) * 0.0042f, py = (cam.y + d.y * tt) * 0.0042f;
            int ix = (int)(px * 256 + 1048576.0f) & 255, iy = (int)(py * 256 + 1048576.0f) & 255;
            u32 a = t->lv[0][iy << 8 | ix] & 255;
            if (!a) continue;
            float fade = clampf((d.z - 0.02f) * 9, 0, 1);
            u32 a2 = t->lv[0][((iy - (int)(sy * 6)) & 255) << 8 | ((ix - (int)(sx * 6)) & 255)] & 255;
            int litk = (int)clampf(150 + (int)a2 - (int)a + 50, 0, 255);
            float dl = sqrtf_(dot(d, d));
            float l = (d.x * sun_true.x + d.y * sun_true.y + d.z * sun_true.z) / dl;
            u32 lit = mix(dark, lite, (u32)litk);
            if (l > 0 && sun_vis > 0) lit = mix(lit, sunny, (u32)(l * l * l * l * 200 * sun_vis));
            u32 k = (u32)(a * fade * 0.85f);
            if (zb[y * W + x] == 0) fb[y * W + x] = mix(fb[y * W + x], lit, k);
            if (x + 1 < W && zb[y * W + x + 1] == 0) fb[y * W + x + 1] = mix(fb[y * W + x + 1], lit, k);
        }
    }
}


static void make_light_tex(void) {
    t_pool = new_tex(6);
    t_beam = new_tex(6);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            float dx = (x - 31.5f) / 31.5f, dy = (y - 31.5f) / 31.5f, r = sqrtf_(dx * dx + dy * dy);
            float v = clampf(1 - r, 0, 1);
            v = v * v * (3 - 2 * v);
            int g = (int)(v * 255);
            t_pool->lv[0][y * 64 + x] = (u32)(g << 16 | g << 8 | g);
            float a = y / 63.0f, ed = clampf((1 - absf(dx)) / 0.45f, 0, 1);
            ed = ed * ed * (3 - 2 * ed);
            float rise = clampf(a / 0.18f, 0, 1), fall = 1 - a;
            float b = ed * (0.35f + 0.65f * rise * rise * (3 - 2 * rise)) * fall * sqrtf_(fall);
            g = (int)(clampf(b * 0.85f, 0, 1) * 255);
            t_beam->lv[0][y * 64 + x] = (u32)(g << 16 | g << 8 | g);
        }
    make_mips(t_pool);
    make_mips(t_beam);
}

static void ground_quad(V3 a, V3 b, V3 c, V3 d, Tex *t, u32 tint) {
    Mat m = {t, tint, 1, 1, 0, 0, 1};
    quad(a, b, c, d, 1, 1, &m);
}

static void draw_night_lights(void) {
    if (night_k < 0.05f) return;
    float nk = night_k;
    u32 lamp = rgb((int)(255 * nk), (int)(170 * nk), (int)(90 * nk));
    float R2 = (fog_far * 0.8f) * (fog_far * 0.8f);
    int bx0 = maxi(0, (int)((cam.x - fog_far) / BK)), bx1 = mini(BW - 1, (int)((cam.x + fog_far) / BK));
    int by0 = maxi(0, (int)((cam.y - fog_far) / BK)), by1 = mini(BH - 1, (int)((cam.y + fog_far) / BK));
    for (int by = by0; by <= by1; by++)
        for (int bx = bx0; bx <= bx1; bx++) {
            Bucket *k = &bk[4][by * BW + bx];
            for (u32 i = 0; i < k->n; i++) {
                Pole *p = &poles[bl[4][k->first + i]];
                if (p->kind || p->tilt > 0) continue;
                float lx = p->x + cosf_(p->a) * 0.75f, ly = p->y + sinf_(p->a) * 0.75f;
                float dx = lx - cam.x, dy = ly - cam.y;
                if (dx * dx + dy * dy > R2 || dx * cam_f.x + dy * cam_f.y < -4) continue;
                float r = 3.2f, z = 0.045f;
                ground_quad(v3(lx - r, ly - r, z), v3(lx + r, ly - r, z), v3(lx + r, ly + r, z), v3(lx - r, ly + r, z), t_pool, lamp);
            }
        }
    u32 head = rgb((int)(175 * nk), (int)(170 * nk), (int)(150 * nk));
    for (int i = 0; i < CARS; i++) {
        Car *c = &cars[i];
        if (!c->mode || c->wreck || (c->mode == M_PARKED && i != mine)) continue;
        float dx = c->x - cam.x, dy = c->y - cam.y;
        if (dx * dx + dy * dy > R2) continue;
        float ca = cosf_(c->a), sa = sinf_(c->a), z = 0.05f;
        float f0 = 0.85f, f1 = 8.5f, w0 = 0.45f, w1 = 2.4f;
        V3 a = v3(c->x + ca * f0 - sa * w0, c->y + sa * f0 + ca * w0, z), b = v3(c->x + ca * f0 + sa * w0, c->y + sa * f0 - ca * w0, z);
        V3 e = v3(c->x + ca * f1 + sa * w1, c->y + sa * f1 - ca * w1, z), f = v3(c->x + ca * f1 - sa * w1, c->y + sa * f1 + ca * w1, z);
        Mat m = {t_beam, head, 1, 1, 0, 0, 1};
        quad(e, f, a, b, 1, 1, &m);
    }
}

static void billboard(float x, float y, float z, float s, Mat *m) {
    V3 c = v3(x, y, z), r = scl(cam_r, s), u = scl(cam_u, s);
    quad(sub(sub(c, r), u), sub(add(c, r), u), add(add(c, r), u), add(sub(c, r), u), 1, 1, m);
}

static u32 scale_col(u32 c, float k) {
    return rgb((int)((c >> 16 & 255) * k), (int)((c >> 8 & 255) * k), (int)((c & 255) * k));
}

static void draw_fx(void) {
    float R2 = fog_far * fog_far;
    u32 grey = light(0xc8c8cc, v3(0, 0, 1)), soot = light(0x3a3a3e, v3(0, 0, 1));
    grey = rgb((grey >> 16 & 255) * 2, (grey >> 8 & 255) * 2, (grey & 255) * 2);
    soot = rgb((soot >> 16 & 255) * 2, (soot >> 8 & 255) * 2, (soot & 255) * 2);
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < FXN; i++) {
            Fx *f = &fx[i];
            if (f->life <= 0) continue;
            int smoky = f->type == FX_SMOKE || f->type == FX_BLACK || f->type == FX_SPLASH;
            if (smoky != (pass == 0)) continue;
            float dx = f->x - cam.x, dy = f->y - cam.y;
            if (dx * dx + dy * dy > R2) continue;
            float t = (float)f->life / f->max, in = minf(1, (f->max - f->life) / 6.0f);
            if (smoky) {
                u32 sc = 0;
                if (f->type == FX_SPLASH) { u32 l = light(0xf4fbff, v3(0, 0, 1)); sc = rgb((l >> 16 & 255) * 2, (l >> 8 & 255) * 2, (l & 255) * 2); }
                Mat m = {t_pool, f->type == FX_SMOKE ? grey : f->type == FX_SPLASH ? sc : soot, 1, 0, 0, 0, 2, (int)((f->type == FX_SMOKE ? 110 : f->type == FX_SPLASH ? 200 : 170) * t * in)};
                billboard(f->x, f->y, f->z, f->size, &m);
            } else {
                u32 col = f->type == FX_FIRE ? mix(0xff3a10, 0xffc850, (u32)(t * 256)) : f->type == FX_FLASH ? 0xfff0c8 : 0xffd080;
                Mat m = {t_pool, scale_col(col, f->type == FX_FIRE ? 0.4f + 0.6f * t : t), 1, 1, 0, 0, 1, 0};
                billboard(f->x, f->y, f->z, f->size, &m);
            }
        }
    for (int i = 0; i < 16; i++) {
        Tracer *t = &tracers[i];
        if (t->life <= 0) continue;
        V3 a = v3(t->x0, t->y0, t->z0), b = v3(t->x1, t->y1, t->z1);
        V3 sd = norm(cross(sub(b, a), sub(a, cam)));
        sd = scl(sd, 0.018f);
        Mat m = {t_white, scale_col(0xfff0a0, t->life / 4.0f), 1, 1, 0, 0, 1, 0};
        quad(sub(a, sd), add(a, sd), add(b, sd), sub(b, sd), 1, 1, &m);
    }
    for (int i = 0; i < PICKS; i++) {
        Pickup *k = &picks[i];
        if (!k->on) continue;
        float dx = k->x - cam.x, dy = k->y - cam.y;
        if (dx * dx + dy * dy > R2) continue;
        u32 col = k->kind == 2 ? 0xff9a20 : k->kind ? 0x3a9cff : 0x3cff6a;
        float bob = 0.5f + 0.08f * sinf_(tick * 0.07f);
        place(v3(k->x, k->y, bob), tick * 0.04f);
        mbox(v3(-0.16f, -0.16f, -0.16f), v3(0.16f, 0.16f, 0.16f), neon_tex(col), 0x808080);
        if (!k->kind) {
            mbox(v3(-0.17f, -0.05f, -0.12f), v3(0.17f, 0.05f, 0.12f), neon_tex(0xffffff), 0x808080);
            mbox(v3(-0.17f, -0.12f, -0.05f), v3(0.17f, 0.12f, 0.05f), neon_tex(0xffffff), 0x808080);
        }
        Mat g = {t_pool, scale_col(col, 0.45f), 1, 1, 0, 0, 1, 0};
        float r = 1.1f, z = 0.05f;
        quad(v3(k->x - r, k->y - r, z), v3(k->x + r, k->y - r, z), v3(k->x + r, k->y + r, z), v3(k->x - r, k->y + r, z), 1, 1, &g);
    }
}

static void clear_job(int w, int nw) {
    for (int y = 0; y < H; y++) {
        if (nw > 1 && ((y >> 3) % nw) != w) { y |= 7; continue; }
        float *z = zb + y * W;
        u8 *s = sbuf + y * W;
        for (int x = 0; x < W; x++) { z[x] = 0; s[x] = 0; }
    }
}

static u32 *vram_p;
static int vram_pitch;
static void copy_job(int w, int nw) {
    for (int y = 0; y < H; y++) {
        if (nw > 1 && ((y >> 3) % nw) != w) { y |= 7; continue; }
        u64 *d = (u64 *)(vram_p + (usize)y * vram_pitch), *sr = (u64 *)(fb + (usize)y * W);
        for (int x = 0; x < W / 2; x++) d[x] = sr[x];
    }
}

static void draw_world(void) {
    float back = mine >= 0 ? 4.4f : 2.3f, up = mine >= 0 ? 1.9f : 1.2f;
    float yaw = cam_yaw_s + look_yaw;
    up += sinf_(look_pitch) * back;
    back *= cosf_(look_pitch);
    float cy = cosf_(yaw), sy = sinf_(yaw);
    float gz = ground_z(me_x, me_y);
    if (mine >= 0 && cars[mine].sink > 0) gz = SEA_Z;
    else if (mine < 0 && foot_sink > 0) gz = SEA_Z;
    V3 target = v3(me_x, me_y, gz + (mine >= 0 ? 0.6f : 0.55f));
    V3 pos;
    float want = back;
    for (;;) {
        if (!(cell(me_x - cy * want, me_y - sy * want) & C_BLD) || want < 0.8f) break;
        want -= 0.25f;
    }
    static float cam_back = -1;
    if (cam_back < 0 || want < cam_back) cam_back = want;
    else cam_back += (want - cam_back) * 0.06f;
    up = maxf(up - (back - cam_back) * 0.2f, 0.9f);
    pos = v3(me_x - cy * cam_back, me_y - sy * cam_back, gz + up);
    if (shake) { float k = shake * 0.006f; pos.x += (rndf() - 0.5f) * k; pos.y += (rndf() - 0.5f) * k; pos.z += (rndf() - 0.5f) * k; }
    V3 d = sub(target, pos);
    float pitch = atan2f_(d.z, sqrtf_(d.x * d.x + d.y * d.y));
    set_camera(pos, yaw, pitch);
#ifdef PORTRAIT
    {
        float fa = foot_a;
#ifdef PORTSIDE
        fa += PORTSIDE;
#endif
        set_camera(v3(foot_x + cosf_(fa) * PORTRAIT, foot_y + sinf_(fa) * PORTRAIT, 0.58f), fa + PI, -0.05f);
    }
#endif
#ifdef SKYCAM
    fog_near = SKYFOG; fog_far = SKYFOG * 3;
#ifndef SKYPITCH
#define SKYPITCH -0.42f
#endif
    set_camera(v3(SKYCAM), SKYYAW, SKYPITCH);
#endif
    set_time();
    run_job(clear_job);
    queue_on = 1;
    tick_now = tick;
    fog_col = pack3(haze_col(norm(v3(cam_f.x, cam_f.y, 0.03f))));
    draw_far_city();
    draw_world_map();
#ifndef NOPOLES
    draw_poles();
#endif
    draw_beach();
    draw_sea();
    float far2 = fog_far * fog_far;
    for (int i = 0; i < PEOPLE; i++) {
        Person *p = &people[i];
        V3 at = v3(p->x, p->y, ground_z(p->x, p->y));
        V3 dd = sub(at, cam);
        if (dot(dd, dd) > far2 || dot(dd, cam_f) < -1) continue;
        float pd = sqrtf_(dot(dd, dd));
#ifndef NOPEOPLE
        if (pd < 48) draw_human(p->look, at, p->a, p->dead ? PO_DEAD : p->pose, p->phase, p->hyaw, pd, 0);
        else draw_far_human(p->look, at, p->a, p->dead ? PO_DEAD : p->pose, p->phase);
#endif
    }
    static float foot_z;
    foot_z += ((foot_sink > 0 ? SEA_Z + 0.05f - foot_sink * 0.9f : ground_z(foot_x, foot_y)) - foot_z) * (foot_sink > 0 ? 1.0f : 0.2f);
    if (mine < 0) draw_human(0, v3(foot_x, foot_y, foot_z), foot_a, caught && caught_how ? PO_DEAD : punch_t ? PO_PUNCH : aim_t AIMHOLD ? (walking ? PO_AIMRUN : PO_AIM) : walking ? (hold[0] ? PO_RUN : PO_WALK) : PO_IDLE, foot_phase, 0, 3, aim_t > 0 && weapon > 0);
    for (int i = 0; i < CARS; i++) {
        Car *c = &cars[i];
        if (!c->mode) continue;
        if (c->kind == K_BOAT) {
            float bob = 0.035f * sinf_(tick * 0.045f + i * 1.7f);
            V3 at = v3(c->x, c->y, SEA_Z + 0.03f + bob);
            V3 dd = sub(at, cam);
            if (dot(dd, dd) > far2 || dot(dd, cam_f) < -2) continue;
            float rl = c->roll + 0.03f * sinf_(tick * 0.031f + i), pt = c->pitch + 0.02f * sinf_(tick * 0.04f + i * 2.3f);
            draw_boat(at, c->a, c->paint, rl, pt, c->wreck);
            if (c->mode == M_MINE || c->mode == M_TRAFFIC) {
                place(at, c->a);
                m_pc = cosf_(pt); m_ps = sinf_(pt); m_rc = cosf_(rl); m_rs = sinf_(rl);
                V3 st = mx(v3(-0.22f, 0, 0.31f));
                draw_human(c->mode == M_MINE ? 0 : (u32)(i * 2654435761u), st, c->a, PO_IDLE, 0, 0, sqrtf_(dot(dd, dd)), 0);
            }
            continue;
        }
        V3 at = v3(c->x, c->y, c->sink > 0 ? SEA_Z + 0.1f - c->sink : ground_z(c->x, c->y));
        V3 dd = sub(at, cam);
        if (dot(dd, dd) > far2 || dot(dd, cam_f) < -2) continue;
        draw_car(at, c->a, c->paint, c->cop, c->kind, c->steer, c->roll, c->pitch, c->wreck, tick);
    }
    draw_job_marker();
#ifndef NOSHADOWS
    draw_shadows();
#endif
    draw_night_lights();
    draw_fx();
    flush_queue();
    queue_on = 0;
    run_job(sky_job);
#ifndef NOCLOUDS
    run_job(cloud_job);
#endif
}

static u64 clock(void) {
    u32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (u64)hi << 32 | lo;
}

int frames;
EFIAPI u64 efi_main(void *image, SystemTable *st) {
    (void)image;
    bs = st->boot;
    con_in = st->con_in;
    bs->watchdog(0, 0, 0, 0);
    // gop guid from the uefi spec nobody remembers this
    Guid gop_guid = {0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}};
    bs->locate_proto(&gop_guid, 0, (void **)&gop);
    W = (int)gop->mode->info->width;
    H = (int)gop->mode->info->height;
    half_w = W * 0.5f;
    half_h = H * 0.5f;
    focal = half_w / 0.9f;
    fb = pool((usize)W * H * 4);
    zb = (float *)pool((usize)W * H * 4);
    sbuf = (u8 *)pool((usize)W * H);
    arena_left = (usize)48 << 20;
    arena = (u8 *)pool(arena_left);
    if (!arena) arena_left = 0;
    make_textures();
    make_facades();
    make_paving();
    make_model_tex();
    load_people();
    make_light_tex();
    init_game();
    qt = (QT *)pool((usize)QCAP * sizeof(QT));
#ifndef ONECORE
    mp_start();
#endif
    u64 t0 = clock();
    // time a tenth of a second then a sixth of it is one step
    bs->stall(100000);
    u64 per_step = (clock() - t0) / 6;
    u32 *vram = 0;
    int pitch = (int)gop->mode->info->pitch;
    if (gop->mode->fb_base && gop->mode->info->format == 1 && pitch >= W) vram = (u32 *)gop->mode->fb_base;
#ifndef NORAW
    if (vram && arena && inb(0x64) != 0xff) {
        while (inb(0x64) & 1) inb(0x60);
        mouse_on();
#ifndef NOTPL
        // tpl 16 blocks the firmware key timer 31 froze everything lol
        ((u64 (EFIAPI *)(u64))bs->raise_tpl)(16);
#endif
        raw_kbd = 1;
    }
#endif

    u64 last = clock();
    for (;;) {
        read_keys();
        if (key_map) { key_map = 0; map_toggle(); }
        if (map_open) {
            map_frame();
            if (raw_kbd) { vram_p = vram; vram_pitch = pitch; run_job(copy_job); }
            else gop->blt(gop, fb, 2, 0, 0, 0, 0, (usize)W, (usize)H, 0);
            last = clock();
            continue;
        }
        u64 now = clock();
        int n = (int)((now - last) / per_step);
        if (!n) continue;
        last += (u64)n * per_step;
        if (n > 4) n = 4;
        while (n--) step();
        draw_world();
#ifndef NOAA
        smooth_edges();
#endif
        draw_hud();
        if (raw_kbd) {
            vram_p = vram; vram_pitch = pitch;
            run_job(copy_job);
        } else gop->blt(gop, fb, 2, 0, 0, 0, 0, (usize)W, (usize)H, 0);
#ifdef FPS
        frames++;
#endif
    }
}
