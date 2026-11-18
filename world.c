// real miami baked by osm bake py
// map data (c) openstreetmap contributors
__asm__(".section .rdata,\"dr\"\n.p2align 4\n.globl mapz\nmapz:\n.incbin \"map.lz\"\n.text\n");
extern const u8 mapz[];
static const u8 *mapdata;

#define SEA_Z -0.35f
#define ROAD_Z 0.03f
#define CM 2
enum { G_SEA, G_LAND, G_PARK, G_SAND };
#define C_ROAD 4
#define C_BLD 8
#define C_BRIDGE 16
#define C_LOT 32
#define C_LOT2 64
#define C_TREE 128

static int WX, WY, GW, GH, CW, CH;
typedef struct { float x, y, a; u32 seed; int kind; } Beach;
#define MAXBEACH 2600
static Beach beach[MAXBEACH];
static int n_beach;
static int tick_now;
static u8 *cmap;

static inline int cell(float x, float y) {
    if (x < 0 || y < 0) return 0;
    int cx = (int)(x * CM), cy = (int)(y * CM);
    if (cx >= CW || cy >= CH) return 0;
    return cmap[cy * CW + cx];
}

static const u8 *rd;
static u16 rd16(void) { u16 v = rd[0] | rd[1] << 8; rd += 2; return v; }
static i32 rd32(void) { i32 v = rd[0] | rd[1] << 8 | rd[2] << 16 | rd[3] << 24; rd += 4; return v; }
static float rdc(void) { return rd16() / 16.0f - 64; }

typedef struct { float x, y; u32 first; u16 n; } Node;
typedef struct { u8 cls, flags; u16 a, b, n; u32 first; float w; } Edge;
static Node *nodes;
static Edge *edges;
static int n_nodes, n_edges;
static float *ep;
static float *rib;
static u16 *adj;
static u8 *nlit;
static float *nax, *nwid;

static int light_state(int node, int grp, int t) {
    int c = (t + node * 211) % 960;
    if (grp) c = (c + 480) % 960;
    return c < 400 ? 0 : c < 470 ? 1 : 2;
}

typedef struct {
    u32 first, tri_first;
    u16 n, tri_n;
    float h, minx, miny, maxx, maxy;
    u32 tint, neon, rtint;
    Tex *face;
    float u0, v0;
    u8 shops, roof, guess, use, upper, deco, sstyle, awn;
} Bld;
static Bld *blds;
static int n_blds;
static float *bp;
static u16 *btri;

typedef struct { u16 x, len; u8 g; } Run;
static Run *runs;
static u32 *row_first;
static u16 *row_n;

typedef struct { float x0, y0, x1, y1; } Wall;
static u8 *gtile;               // 4 by 4 unit tiles 2 bits per layer
static u8 *field[4];            // land park sand lot blurred 0 to 255
static int TW, TH;
#define TG 4
static Wall *walls;
static int n_walls;

// kind 0 fake palm 1 real palm 2 broad tree
typedef struct { float x, y; u32 seed; float h, r; int kind; } Palm;
#define MAXTREES 60000
static const u8 *scan_bits;
static int scan_bw;
static int scanned(float x, float y) {
    int k = (int)(y / 32) * scan_bw + (int)(x / 32);
    return x >= 0 && y >= 0 && (scan_bits[k >> 3] >> (k & 7) & 1);
}
typedef struct { float x, y, a; int kind; float tilt, tv, fa; } Pole;
static Pole *poles;
static int n_poles;
static void box_pole(int i, float *r) { r[0] = r[2] = poles[i].x; r[1] = r[3] = poles[i].y; }
static Palm *palms;
static int n_palms, n_roof_trees;

// 32 unit buckets so we only check whats near
#define BK 32
static int BW, BH;
typedef struct { u32 first, n; } Bucket;
static Bucket *bk[5];
static u32 *bl[5];
static u32 *stamp[5];
static u32 frame_no;

static void bucket_build(int kind, int count, void (*box_of)(int, float *)) {
    Bucket *b = (Bucket *)pool((usize)BW * BH * sizeof(Bucket));
    memset(b, 0, (usize)BW * BH * sizeof(Bucket));
    float r[4];
    u32 total = 0;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            u32 acc = 0;
            for (int i = 0; i < BW * BH; i++) { b[i].first = acc; acc += b[i].n; b[i].n = 0; }
            total = acc;
            bl[kind] = (u32 *)pool((usize)total * 4 + 4);
        }
        for (int i = 0; i < count; i++) {
            box_of(i, r);
            int x0 = maxi(0, (int)(r[0] / BK)), y0 = maxi(0, (int)(r[1] / BK)), x1 = mini(BW - 1, (int)(r[2] / BK)), y1 = mini(BH - 1, (int)(r[3] / BK));
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++) {
                    Bucket *k = &b[y * BW + x];
                    if (pass == 1) bl[kind][k->first + k->n] = (u32)i;
                    k->n++;
                }
        }
    }
    bk[kind] = b;
    stamp[kind] = (u32 *)pool((usize)count * 4 + 4);
    memset(stamp[kind], 0, (usize)count * 4 + 4);
}

static void box_bld(int i, float *r) { r[0] = blds[i].minx; r[1] = blds[i].miny; r[2] = blds[i].maxx; r[3] = blds[i].maxy; }
static void box_edge(int i, float *r) {
    Edge *e = &edges[i];
    r[0] = r[1] = 1e9f; r[2] = r[3] = -1e9f;
    for (u32 k = 0; k < e->n; k++) {
        float x = ep[(e->first + k) * 2], y = ep[(e->first + k) * 2 + 1];
        r[0] = minf(r[0], x - e->w); r[1] = minf(r[1], y - e->w); r[2] = maxf(r[2], x + e->w); r[3] = maxf(r[3], y + e->w);
    }
}
static void box_wall(int i, float *r) { r[0] = minf(walls[i].x0, walls[i].x1); r[1] = minf(walls[i].y0, walls[i].y1); r[2] = maxf(walls[i].x0, walls[i].x1); r[3] = maxf(walls[i].y0, walls[i].y1); }
static void box_palm(int i, float *r) { r[0] = r[2] = palms[i].x; r[1] = r[3] = palms[i].y; }

static void fill_cells_poly(float *pts, int n, int bit) {
    float y0 = 1e9f, y1 = -1e9f;
    for (int i = 0; i < n; i++) { y0 = minf(y0, pts[i * 2 + 1]); y1 = maxf(y1, pts[i * 2 + 1]); }
    int cy0 = maxi(0, (int)(y0 * CM)), cy1 = mini(CH - 1, (int)(y1 * CM));
    float xs[256];
    for (int cy = cy0; cy <= cy1; cy++) {
        float py = (cy + 0.5f) / CM;
        int k = 0;
        for (int i = 0; i < n && k < 256; i++) {
            float ax = pts[i * 2], ay = pts[i * 2 + 1], bx = pts[((i + 1) % n) * 2], by = pts[((i + 1) % n) * 2 + 1];
            if ((ay <= py && py < by) || (by <= py && py < ay)) xs[k++] = ax + (py - ay) / (by - ay) * (bx - ax);
        }
        for (int i = 1; i < k; i++) { float v = xs[i]; int j = i - 1; while (j >= 0 && xs[j] > v) { xs[j + 1] = xs[j]; j--; } xs[j + 1] = v; }
        for (int i = 0; i + 1 < k; i += 2) {
            int a = maxi(0, (int)(xs[i] * CM + 0.5f)), b = mini(CW - 1, (int)(xs[i + 1] * CM - 0.5f));
            for (int cx = a; cx <= b; cx++) cmap[cy * CW + cx] |= (u8)bit;
        }
    }
}

static void stamp_road(float ax, float ay, float bx, float by, float w, int bits) {
    float dx = bx - ax, dy = by - ay, L = sqrtf_(dx * dx + dy * dy);
    int steps = (int)(L * CM * 2) + 1;
    int r = (int)(w * 0.5f * CM + 0.5f);
    for (int s = 0; s <= steps; s++) {
        float t = (float)s / steps;
        int cx = (int)((ax + dx * t) * CM), cy = (int)((ay + dy * t) * CM);
        for (int oy = -r; oy <= r; oy++)
            for (int ox = -r; ox <= r; ox++) {
                if (ox * ox + oy * oy > r * r + r) continue;
                int x = cx + ox, y = cy + oy;
                if (x >= 0 && y >= 0 && x < CW && y < CH) cmap[y * CW + x] |= (u8)bits;
            }
    }
}

// ear clipping roofs cuz footprints are cursed shapes
static int ear_clip(float *p, int n, u16 *out) {
    int idx[512], m = n, k = 0;
    if (n > 512) return 0;
    for (int i = 0; i < n; i++) idx[i] = i;
    int guard = 0;
    while (m > 3 && guard++ < 4 * n) {
        int found = 0;
        for (int i = 0; i < m; i++) {
            int a = idx[(i + m - 1) % m], b = idx[i], c = idx[(i + 1) % m];
            float ax = p[a * 2], ay = p[a * 2 + 1], bx = p[b * 2], by = p[b * 2 + 1], cx = p[c * 2], cy = p[c * 2 + 1];
            float cr = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
            if (cr <= 0) continue;
            int bad = 0;
            for (int j = 0; j < m && !bad; j++) {
                int q = idx[j];
                if (q == a || q == b || q == c) continue;
                float px = p[q * 2], py = p[q * 2 + 1];
                float d1 = (bx - ax) * (py - ay) - (by - ay) * (px - ax);
                float d2 = (cx - bx) * (py - by) - (cy - by) * (px - bx);
                float d3 = (ax - cx) * (py - cy) - (ay - cy) * (px - cx);
                if (d1 >= 0 && d2 >= 0 && d3 >= 0) bad = 1;
            }
            if (bad) continue;
            out[k++] = (u16)a; out[k++] = (u16)b; out[k++] = (u16)c;
            for (int j = i; j < m - 1; j++) idx[j] = idx[j + 1];
            m--;
            found = 1;
            break;
        }
        if (!found) break;
    }
    if (m == 3) { out[k++] = (u16)idx[0]; out[k++] = (u16)idx[1]; out[k++] = (u16)idx[2]; }
    return k / 3;
}

static u32 pastels[] = {0xf7a8c4, 0x9fe3d0, 0xf4e3c1, 0xf0f0f4, 0xffc49a, 0xc8b3f0, 0xa8d8f7, 0xf6eaa0, 0xffb0a0, 0xb8f0c0, 0xe8d0ff, 0xfff0d8};
static u32 neons[] = {0xff3da0, 0x30e8e0, 0xb060ff, 0xffd040};

static void make_gstyle(void);
static void make_far(void);
static i32 o_names;
static void draw_bld_signs(int id);
static void load_world(void) {
    u32 msz;
    mapdata = unlzma(mapz, &msz);
    rd = mapdata + 4;
    WX = rd32(); WY = rd32(); GW = rd32(); GH = rd32();
    i32 o_ground = rd32(), o_nodes = rd32(), o_edges = rd32(), o_bld = rd32(), o_lot = rd32(), o_tree = rd32();
    o_names = rd32();
    scan_bits = mapdata + o_tree + 8;
    scan_bw = WX / 32 + 1;
    CW = WX * CM; CH = WY * CM;
    cmap = (u8 *)pool((usize)CW * CH);
    memset(cmap, 0, (usize)CW * CH);

    rd = mapdata + o_ground;
    u8 *prev = (u8 *)pool((usize)GW), *cur = (u8 *)pool((usize)GW);
    memset(prev, 0, (usize)GW);
    row_first = (u32 *)pool((usize)GH * 4);
    row_n = (u16 *)pool((usize)GH * 2);
    runs = (Run *)pool(400000 * sizeof(Run));
    walls = (Wall *)pool(300000 * sizeof(Wall));
    palms = (Palm *)pool(MAXTREES * sizeof(Palm));
    u32 nr = 0;
    n_walls = 0;
    n_palms = 0;
    for (int y = 0; y < GH; y++) {
        int n = rd16(), x = 0;
        row_first[y] = nr;
        row_n[y] = 0;
        for (int i = 0; i < n; i++) {
            u16 r = rd16();
            int v = r >> 14, len = r & 16383;
            for (int k = 0; k < len && x + k < GW; k++) cur[x + k] = (u8)v;
            if (v && nr < 400000) { runs[nr].x = (u16)x; runs[nr].len = (u16)len; runs[nr].g = (u8)v; nr++; row_n[y]++; }
            if (v == G_SAND && len > 4 && y % 5 == 0 && n_palms < 20000 && !scanned(x + 1.5f, y + 0.5f)) {
                Palm *pm = &palms[n_palms++];
                pm->x = x + 1.5f; pm->y = y + 0.5f; pm->seed = hash2(x, y); pm->h = 3.0f; pm->r = 1.4f; pm->kind = 0;
            }
            x += len;
        }
        for (int cy = y * CM; cy < (y + 1) * CM && cy < CH; cy++)
            for (int cx = 0; cx < CW; cx++) cmap[cy * CW + cx] = cur[cx / CM];
        for (int xx = 0; xx < GW; ) {
            int a = prev[xx] != 0, b = cur[xx] != 0;
            if (a == b || y == 0) { xx++; continue; }
            int s = xx;
            while (xx < GW && (prev[xx] != 0) == a && (cur[xx] != 0) == b) xx++;
            if (n_walls < 300000) {
                Wall *w = &walls[n_walls++];
                if (a) { w->x0 = (float)xx; w->x1 = (float)s; } else { w->x0 = (float)s; w->x1 = (float)xx; }
                w->y0 = w->y1 = (float)y;
            }
        }
        for (int xx = 1; xx < GW; xx++) {
            int a = cur[xx - 1] != 0, b = cur[xx] != 0;
            if (a == b || n_walls >= 300000) continue;
            Wall *w = &walls[n_walls++];
            w->x0 = w->x1 = (float)xx;
            if (a) { w->y0 = (float)y; w->y1 = (float)y + 1; } else { w->y0 = (float)y + 1; w->y1 = (float)y; }
        }
        u8 *t = prev; prev = cur; cur = t;
    }

    // lots 1 asphalt 2 concrete
    rd = mapdata + o_lot;
    for (int y = 0; y < GH; y++) {
        int n = rd16(), x = 0;
        for (int i = 0; i < n; i++) {
            u16 r = rd16();
            int v = r >> 14, len = r & 16383;
            if (v)
                for (int cy = y * CM; cy < (y + 1) * CM && cy < CH; cy++)
                    for (int cx = x * CM; cx < (x + len) * CM && cx < CW; cx++) cmap[cy * CW + cx] |= v == 1 ? C_LOT | C_LOT2 : C_LOT;
            x += len;
        }
    }

    rd = mapdata + o_nodes;
    n_nodes = rd32();
    nodes = (Node *)pool((usize)n_nodes * sizeof(Node));
    for (int i = 0; i < n_nodes; i++) { nodes[i].x = rdc(); nodes[i].y = rdc(); nodes[i].n = 0; }
    rd = mapdata + o_edges;
    n_edges = rd32();
    edges = (Edge *)pool((usize)n_edges * sizeof(Edge));
    ep = (float *)pool(40000 * 2 * 4);
    rib = (float *)pool(40000 * 5 * 4);
    u32 np = 0;
    for (int i = 0; i < n_edges; i++) {
        Edge *e = &edges[i];
        e->cls = rd[0]; e->flags = rd[1]; e->w = rd[2] / 16.0f; rd += 3;
        e->a = rd16(); e->b = rd16(); e->n = rd16();
        e->first = np;
        for (int k = 0; k < e->n; k++) { ep[np * 2] = rdc(); ep[np * 2 + 1] = rdc(); np++; }
        nodes[e->a].n++;
        nodes[e->b].n++;
        float v = 0;
        for (int k = 0; k < e->n; k++) {
            float *p = &ep[(e->first + k) * 2];
            float tx = 0, ty = 0;
            if (k > 0) { float dx = p[0] - p[-2], dy = p[1] - p[-1], l = sqrtf_(dx * dx + dy * dy) + 1e-6f; tx += dx / l; ty += dy / l; v += l; }
            if (k + 1 < e->n) { float dx = p[2] - p[0], dy = p[3] - p[1], l = sqrtf_(dx * dx + dy * dy) + 1e-6f; tx += dx / l; ty += dy / l; }
            float l = sqrtf_(tx * tx + ty * ty) + 1e-6f;
            tx /= l; ty /= l;
            float h = e->w * 0.5f;
            float *r = &rib[(e->first + k) * 5];
            r[0] = p[0] - ty * h; r[1] = p[1] + tx * h;
            r[2] = p[0] + ty * h; r[3] = p[1] - tx * h;
            r[4] = v / (e->w * 1.0f);
            if (k + 1 < e->n) stamp_road(p[0], p[1], p[2], p[3], e->w, C_ROAD | ((e->flags & 1) ? C_BRIDGE : 0));
        }
    }
    u32 acc = 0;
    for (int i = 0; i < n_nodes; i++) { nodes[i].first = acc; acc += nodes[i].n; nodes[i].n = 0; }
    adj = (u16 *)pool((usize)acc * 2 + 2);
    for (int i = 0; i < n_edges; i++) {
        Node *a = &nodes[edges[i].a], *b = &nodes[edges[i].b];
        adj[a->first + a->n++] = (u16)i;
        adj[b->first + b->n++] = (u16)i;
    }
    for (int i = 0; i < n_nodes; i++) {
        if (nodes[i].n != 2) continue;
        Edge *ea = &edges[adj[nodes[i].first]], *eb = &edges[adj[nodes[i].first + 1]];
        if (ea == eb || ea->n < 2 || eb->n < 2) continue;
        int ka = ea->b == i ? ea->n - 1 : 0, kb = eb->b == i ? eb->n - 1 : 0;
        float *pa = &ep[(ea->first + ka) * 2], *qa = &ep[(ea->first + (ka ? ka - 1 : 1)) * 2];
        float *pb = &ep[(eb->first + kb) * 2], *qb = &ep[(eb->first + (kb ? kb - 1 : 1)) * 2];
        float ix = pa[0] - qa[0], iy = pa[1] - qa[1], il = sqrtf_(ix * ix + iy * iy) + 1e-6f;
        float ox = qb[0] - pb[0], oy = qb[1] - pb[1], ol = sqrtf_(ox * ox + oy * oy) + 1e-6f;
        float tx = ix / il + ox / ol, ty = iy / il + oy / ol, tl = sqrtf_(tx * tx + ty * ty);
        if (tl < 0.5f) continue;
        tx /= tl; ty /= tl;
        for (int s2 = 0; s2 < 2; s2++) {
            Edge *e = s2 ? eb : ea;
            int k = s2 ? kb : ka;
            float sg = (k == 0) == (s2 == 0) ? -1.0f : 1.0f;
            float ux = tx * sg, uy = ty * sg, h = e->w * 0.5f;
            float *pp = &ep[(e->first + k) * 2], *r = &rib[(e->first + k) * 5];
            r[0] = pp[0] - uy * h; r[1] = pp[1] + ux * h;
            r[2] = pp[0] + uy * h; r[3] = pp[1] - ux * h;
        }
    }
    // palms on the big avenues peak miami
    for (int i = 0; i < n_edges && n_palms < 20000; i++) {
        Edge *e = &edges[i];
        if (e->cls > 3 || (e->flags & 1)) continue;
        for (int k = 0; k + 1 < e->n; k++) {
            float *p = &ep[(e->first + k) * 2];
            float dx = p[2] - p[0], dy = p[3] - p[1], L = sqrtf_(dx * dx + dy * dy);
            if (L < 1) continue;
            for (float t = 4; t < L - 2; t += 11) {
                for (int side = -1; side <= 1; side += 2) {
                    float x = p[0] + dx / L * t - dy / L * (e->w * 0.5f + 0.9f) * side;
                    float y = p[1] + dy / L * t + dx / L * (e->w * 0.5f + 0.9f) * side;
                    int c = cell(x, y);
                    if ((c & 3) == G_SEA || (c & C_ROAD) || n_palms >= 20000 || scanned(x, y)) continue;
                    Palm *pm = &palms[n_palms++];
                    pm->x = x; pm->y = y; pm->seed = hash2((int)x, (int)y); pm->h = 3.0f; pm->r = 1.4f; pm->kind = 0;
                }
            }
        }
    }

    // real trees from the scan not made up this time
    {
        rd = mapdata + o_tree;
        int nt = rd32(), nb = rd32();
        rd += nb;
        int ng = rd32();
        const u8 *g = rd, *at = rd + ng;
        u32 k = 0;
        for (int i = 0; i < nt && n_palms < MAXTREES; i++) {
            u32 gap = 0;
            for (int sh = 0;; sh += 7) { u8 b = *g++; gap |= (u32)(b & 127) << sh; if (b < 128) break; }
            k += gap;
            u32 h = hash2((int)k, 991);
            Palm *pm = &palms[n_palms++];
            pm->x = (float)(k % (u32)GW) + 0.2f + (h & 255) / 425.0f;
            pm->y = (float)(k / (u32)GW) + 0.2f + (h >> 8 & 255) / 425.0f;
            pm->seed = h;
            float m = (at[i] & 127) * 4.0f + ((h >> 16 & 255) / 255.0f - 0.5f) * 3.0f;
            m = maxf(m, 4.0f);
            pm->h = m / 2.8f;
            pm->kind = at[i] >> 7 ? 2 : 1;
            pm->r = pm->kind == 2 ? clampf(m * 0.42f, 2.5f, 8.0f) / 2.8f * (0.85f + (h >> 24 & 63) / 210.0f) : 1.4f;
        }
    }

    rd = mapdata + o_bld;
    n_blds = rd32();
    int n_pts = rd32();
    rd32();
    int n_bands = rd32();
    const u8 *s_band = rd;
    const u8 *s_pal = s_band + n_bands * 2;
    const u8 *s_hl = s_pal + 63 * 3, *s_hh = s_hl + n_blds, *s_fl = s_hh + n_blds, *s_rc = s_fl + n_blds, *s_cnt = s_rc + n_blds;
    const u8 *s_xl = s_cnt + n_blds, *s_xh = s_xl + n_blds, *s_yl = s_xh + n_blds;
    const i8 *s_dx = (const i8 *)(s_yl + n_blds), *s_dy = s_dx + (n_pts - n_blds);
    const u8 *s_esc = (const u8 *)(s_dy + (n_pts - n_blds));
    blds = (Bld *)pool((usize)n_blds * sizeof(Bld));
    bp = (float *)pool((usize)n_pts * 2 * 4);
    btri = (u16 *)pool((usize)n_pts * 3 * 2);
    u32 bn = 0, tn = 0;
    int band = -1, band_left = 0, px = 0;
    for (int i = 0; i < n_blds; i++) {
        Bld *b = &blds[i];
        while (!band_left) { band++; band_left = s_band[band * 2] | s_band[band * 2 + 1] << 8; px = 0; }
        band_left--;
        b->h = (s_hl[i] | s_hh[i] << 8) / 4.0f;
        b->guess = s_fl[i] >> 7;
        b->use = s_fl[i] & 15;
        b->upper = s_fl[i] >> 6 & 1;
        b->n = s_cnt[i];
        b->first = bn;
        int qx = px + (s_xl[i] | s_xh[i] << 8), qy = band << 8 | s_yl[i];
        px = qx;
        b->minx = b->miny = 1e9f; b->maxx = b->maxy = -1e9f;
        for (int k = 0; k < b->n; k++) {
            if (k) {
                int dx = *s_dx++, dy = *s_dy++;
                // minus 128 means the real step is stored in full
                if (dx == -128) { dx = (i16)(s_esc[0] | s_esc[1] << 8); dy = (i16)(s_esc[2] | s_esc[3] << 8); s_esc += 4; }
                qx += dx; qy += dy;
            }
            float x = qx / 16.0f - 64, y = qy / 16.0f - 64;
            bp[bn * 2] = x; bp[bn * 2 + 1] = y; bn++;
            b->minx = minf(b->minx, x); b->miny = minf(b->miny, y); b->maxx = maxf(b->maxx, x); b->maxy = maxf(b->maxy, y);
        }
        b->tri_first = tn;
        b->tri_n = (u16)ear_clip(&bp[b->first * 2], b->n, &btri[tn * 3]);
        tn += b->tri_n;
        u32 h = hash2(i, 77);
        if (b->guess) b->h += (h & 3) * 0.3f;
        float bcx = (b->minx + b->maxx) / 2;
        int beach = bcx > 2560, hh = (int)(h >> 3), tall = b->h > 14, mid = b->h > 6, style;
        static const u8 tall_s[6] = {2, 7, 5, 6, 2, 7}, mid_s[8] = {6, 9, 5, 0, 11, 6, 9, 3};
        static const u8 beach_s[8] = {1, 8, 0, 10, 4, 8, 1, 6}, low_s[8] = {4, 3, 9, 10, 12, 4, 9, 3};
        static const u8 by_use[10][3][4] = {
            {{0}},
            {{4, 10, 4, 3}, {4, 10, 4, 3}, {4, 10, 4, 3}},
            {{9, 4, 8, 3}, {9, 8, 6, 4}, {6, 9, 6, 8}},
            {{9, 8, 4, 10}, {6, 9, 8, 0}, {6, 2, 6, 0}},
            {{0, 5, 0, 9}, {0, 6, 0, 8}, {0, 6, 0, 2}},
            {{7, 5, 3, 5}, {7, 5, 2, 7}, {2, 7, 2, 7}},
            {{3, 4, 5, 1}, {5, 3, 6, 7}, {6, 2, 7, 6}},
            {{12, 12, 5, 3}, {12, 5, 12, 5}, {5, 7, 5, 7}},
            {{11, 5, 11, 11}, {11, 11, 11, 5}, {11, 11, 11, 5}},
            {{5, 10, 3, 5}, {5, 10, 3, 7}, {7, 5, 2, 5}}};
        static const u8 by_use_beach[10][4] = {{0}, {10, 4, 1, 4}, {1, 8, 10, 9}, {8, 1, 10, 9}, {1, 0, 8, 10}, {7, 1, 5, 8}, {1, 4, 1, 10}, {12, 5, 3, 12}, {11, 5, 11, 11}, {10, 5, 1, 10}};
        if (b->use && b->use < 10) style = beach && !mid ? by_use_beach[b->use][hh & 3] : by_use[b->use][tall ? 2 : mid][hh & 3];
        else if (tall) style = tall_s[hh % 6];
        else if (mid) style = beach ? beach_s[hh % 8] : mid_s[hh % 8];
        else style = beach ? beach_s[(hh >> 3) % 8] : low_s[hh % 8];
        Tex *faces[13] = {t_hotel, t_deco, t_glass, t_brick, t_stucco, t_concrete, t_condo, t_office, t_brow, t_apart, t_medi, t_garage, t_shed};
        b->face = faces[style % 13];
        static const u32 glass_t[4] = {0xd8e8f4, 0xc8f0e0, 0xe8e8ec, 0xd4dcf0};
        static const u32 neutral[6] = {0xf0ece6, 0xe4e2de, 0xd8d0c4, 0xf4f0e8, 0xccc8c4, 0xe8dcc8};
        static const u32 warm[6] = {0xf8f0e8, 0xf4e4d4, 0xe8d8c8, 0xfff4e4, 0xf0e0d0, 0xe0ccb4};
        if (style == 2 || style == 7) b->tint = glass_t[(h >> 8) & 3];
        else if (style == 5 || style == 6 || style == 11 || style == 12) b->tint = neutral[(h >> 8) % 6];
        else if (style == 3 || style == 10) b->tint = warm[(h >> 8) % 6];
        else b->tint = pastels[(h >> 8) % 12];
        b->u0 = (h >> 12 & 3) / 4.0f;
        b->v0 = (h >> 14 & 3) / 4.0f;
        int pick = h >> 16 & 3;
        if (b->use == 6) b->shops = b->h > 1.3f;
        else if (b->use == 4 || b->use == 5) b->shops = b->h > 2.5f && pick > 1;
        else if (b->use == 3) b->shops = mid && pick == 0;
        else if (b->use == 0) b->shops = b->h > 2.5f && pick != 0;
        else b->shops = 0;
        if (b->face == t_shed || b->face == t_garage || b->upper) b->shops = 0;
        {
            u32 sh = hash2(i, 9191);
            static const u8 by_use_s[10] = {0, 4, 2, 5, 3, 3, 0, 2, 3, 3};
            int sst = (sh & 3) == 0 ? by_use_s[b->use < 10 ? b->use : 0] : (int)((sh >> 2) % 6);
            if (b->face == t_brick && (sh & 1)) sst = 5;
            if (!b->shops && !b->upper && b->h > 6 && b->face != t_shed && b->face != t_garage) { b->shops = 1; sst = 3; }
            if (sst == 3 && b->h < 4) sst = (int)((sh >> 5) % 3);
            b->sstyle = (u8)sst;
            b->awn = (sst == 1 || ((sst == 0 || sst == 4) && (sh >> 8 & 1))) ? (u8)(1 + (sh >> 10 & 3)) : 0;
        }
        b->roof = b->h > 10 ? (h >> 18 & 3) : (h >> 18 & 3) != 0 ? 1 : 0;
        int glow = b->use == 0 || b->use == 4 || b->use == 6;
        b->neon = (beach && glow && !b->upper && b->h < 14 && (h >> 20 & 3) != 0) ? neons[h >> 22 & 3] : 0;
        static const u32 plain_roof[4] = {0xa8a4a8, 0xb8b4b0, 0x989ca0, 0xc4c0bc};
        const u8 *pc = s_pal + s_rc[i] * 3;
        b->rtint = s_rc[i] < 63 ? rgb(pc[0], pc[1], pc[2]) : plain_roof[h >> 24 & 3];
        fill_cells_poly(&bp[b->first * 2], b->n, C_BLD);
    }
    {
        static const u32 deco_t[8] = {0xffb4d2, 0x9ff0dc, 0xd2bcff, 0xffd6a8, 0xa8e4ff, 0xfff0a0, 0xffc0b8, 0xc4f4b4};
        static const u32 deco_n[6] = {0xff3da0, 0x30e8e0, 0xb060ff, 0x40ff80, 0x3a8cff, 0xff6040};
        for (int i = 0; i < n_blds; i++) {
            Bld *b = &blds[i];
            b->deco = 0;
            if (b->upper || b->h < 1.6f || b->h > 9.5f || (b->minx + b->maxx) / 2 < 2560 || b->maxy - b->miny > 22) continue;
            float cy = (b->miny + b->maxy) / 2;
            int road = 0, front = 0;
            for (float x = b->maxx + 0.4f; x < b->maxx + 16; x += 0.5f) {
                int c = cell(x, cy);
                if (c & C_BLD) break;
                if (c & C_ROAD) road = 1;
                else if ((c & 3) == G_PARK || (c & 3) == G_SAND) { front = road || x - b->maxx < 5; break; }
            }
            if (!front) continue;
            u32 h = hash2(i, 4040);
            b->deco = 1;
            b->face = t_deco;
            b->tint = deco_t[h & 7];
            b->neon = deco_n[(h >> 3) % 6];
            b->shops = b->h > 1.3f;
            b->sstyle = (h >> 6 & 1) ? 1 : (u8)((h >> 7) % 3 == 0 ? 0 : 2 + 2 * ((h >> 9) & 1));
            b->awn = (u8)(1 + (h >> 11 & 3));
        }
    }
    {
        int k = 0;
        for (int i = 0; i < n_palms; i++) {
            Palm *t = &palms[i];
            int in = 0;
            for (int q = 0; q < 5 && !in; q++) {
                float ox = q == 1 ? 0.3f : q == 2 ? -0.3f : 0, oy = q == 3 ? 0.3f : q == 4 ? -0.3f : 0;
                in = (cell(t->x + ox, t->y + oy) & C_BLD) != 0;
            }
            if (in) { n_roof_trees++; continue; }
            float rc = t->kind == 2 ? t->r : 1.25f, d = rc;
            for (int q = 0; q < 12; q++) {
                float ca = cosf_(q * (TAU / 12)), sa = sinf_(q * (TAU / 12));
                for (float rr = 0.4f; rr < d; rr += 0.2f)
                    if (cell(t->x + ca * rr, t->y + sa * rr) & C_BLD) { d = rr; break; }
            }
            if (d < rc) {
                if (d < (t->kind == 2 ? 0.75f : 0.95f)) { n_roof_trees++; continue; }
                if (t->kind == 2) t->r = d + 0.15f;
            }
            palms[k++] = *t;
        }
        n_palms = k;
    }
    // blur so edges curve bye bye minecraft stairs
    // weights 1 3 3 1 then cut at half
    {
        int FW = GW + 1, FH = GH + 1;
        u16 *hp = (u16 *)pool((usize)FW * GH * 2);
        static const int wt[4] = {1, 3, 3, 1};
        for (int layer = 0; layer < 4; layer++) {
            field[layer] = (u8 *)pool((usize)FW * FH);
            for (int y = 0; y < GH; y++)
                for (int x = 0; x < FW; x++) {
                    int s = 0;
                    for (int i = 0; i < 4; i++) {
                        int cx = x - 2 + i;
                        if (cx < 0 || cx >= GW) continue;
                        int c = cmap[(y * CM) * CW + cx * CM], g = c & 3;
                        int in = layer == 0 ? g != 0 : layer == 1 ? g == G_PARK : layer == 2 ? g == G_SAND : (c & C_LOT) != 0;
                        s += in * wt[i];
                    }
                    hp[y * FW + x] = (u16)s;
                }
            for (int y = 0; y < FH; y++)
                for (int x = 0; x < FW; x++) {
                    int s = 0;
                    for (int j = 0; j < 4; j++) {
                        int cy = y - 2 + j;
                        if (cy < 0 || cy >= GH) continue;
                        s += hp[cy * FW + x] * wt[j];
                    }
                    field[layer][y * FW + x] = (u8)mini(255, s * 4);
                }
        }
    }
    TW = GW / TG + 1; TH = GH / TG + 1;
    gtile = (u8 *)pool((usize)TW * TH);
    for (int ty = 0; ty < TH; ty++)
        for (int tx = 0; tx < TW; tx++) {
            int flags = 0;
            for (int layer = 0; layer < 4; layer++) {
                int lo = 255, hi = 0;
                for (int y = ty * TG; y <= ty * TG + TG && y <= GH; y++)
                    for (int x = tx * TG; x <= tx * TG + TG && x <= GW; x++) {
                        int v = field[layer][y * (GW + 1) + x];
                        lo = mini(lo, v); hi = maxi(hi, v);
                    }
                int st = hi < 128 ? 0 : lo >= 128 ? 1 : 2;
                flags |= st << (layer * 2);
            }
            gtile[ty * TW + tx] = (u8)flags;
        }
    poles = (Pole *)pool(20000 * sizeof(Pole));
    n_poles = 0;
    for (int i = 0; i < n_edges && n_poles < 19000; i++) {
        Edge *e = &edges[i];
        if (e->cls > 5 || (e->flags & 1)) continue;
        int side = (i & 1) ? 1 : -1;
        for (int k = 0; k + 1 < e->n; k++) {
            float *p = &ep[(e->first + k) * 2];
            float dx = p[2] - p[0], dy = p[3] - p[1], L = sqrtf_(dx * dx + dy * dy);
            if (L < 4) continue;
            for (float t = 6; t < L - 3; t += 15) {
                float ox = -dy / L * (e->w * 0.5f + 0.4f) * side, oy = dx / L * (e->w * 0.5f + 0.4f) * side;
                float x = p[0] + dx / L * t + ox, y = p[1] + dy / L * t + oy;
                int c = cell(x, y);
                if ((c & 3) == G_SEA || (c & (C_ROAD | C_BLD))) continue;
                poles[n_poles].x = x; poles[n_poles].y = y; poles[n_poles].tilt = poles[n_poles].tv = poles[n_poles].fa = 0;
                poles[n_poles].a = atan2f_(-oy, -ox);
                poles[n_poles].kind = 0;
                n_poles++;
                side = -side;
            }
        }
    }
    nlit = (u8 *)pool((usize)n_nodes + 4);
    nax = (float *)pool((usize)n_nodes * 4 + 4);
    nwid = (float *)pool((usize)n_nodes * 4 + 4);
    for (int i = 0; i < n_nodes; i++) {
        Node *nd = &nodes[i];
        nlit[i] = 0; nax[i] = 0; nwid[i] = 0;
        for (int k = 0; k < nd->n; k++) nwid[i] = maxf(nwid[i], edges[adj[nd->first + k]].w);
        if (!nd->n) continue;
        Edge *f = &edges[adj[nd->first]];
        if (f->n < 2) continue;
        float *p = &ep[f->first * 2], *q = p + 2;
        if (f->a != i) { p = &ep[(f->first + f->n - 1) * 2]; q = p - 2; }
        nax[i] = atan2f_(q[1] - p[1], q[0] - p[0]);
    }
    for (int i = 0; i < n_nodes && n_poles < 20000; i++) {
        Node *nd = &nodes[i];
        if (nd->n < 3) continue;
        float w = 0;
        int big = 0;
        for (int k = 0; k < nd->n; k++) { Edge *e = &edges[adj[nd->first + k]]; w = maxf(w, e->w); big |= e->cls <= 5; }
        if (!big) continue;
        for (int c = 0; c < 4; c++) {
            float a = c * (PI / 2) + PI / 4;
            float x = nd->x + cosf_(a) * (w * 0.5f + 0.6f) * 1.2f, y = nd->y + sinf_(a) * (w * 0.5f + 0.6f) * 1.2f;
            int cc = cell(x, y);
            if ((cc & 3) == G_SEA || (cc & (C_ROAD | C_BLD))) continue;
            poles[n_poles].x = x; poles[n_poles].y = y; poles[n_poles].tilt = poles[n_poles].tv = poles[n_poles].fa = 0;
            poles[n_poles].a = nax[i];
            poles[n_poles].kind = 1 + i;
            nlit[i] = 1;
            n_poles++;
            break;
        }
    }
    BW = WX / BK + 1; BH = WY / BK + 1;
    bucket_build(4, n_poles, box_pole);
    bucket_build(0, n_blds, box_bld);
    bucket_build(1, n_edges, box_edge);
    bucket_build(2, n_walls, box_wall);
    make_gstyle();
    bucket_build(3, n_palms, box_palm);
    make_far();
    n_beach = 0;
    for (int y = 30; y < WY - 30 && n_beach < MAXBEACH - 24; y += 40) {
        int xs = -1, xl = -1, skip = 0;
        for (int x = WX - 2; x > WX / 2; x--) {
            int g = cell(x + 0.5f, y + 0.5f);
            if (xs < 0) { if ((g & 3) == G_SAND) xs = x; else if ((g & 3) != G_SEA && ++skip > 6) break; continue; }
            if ((g & 3) != G_SAND || (g & (C_ROAD | C_BLD))) { xl = x + 1; break; }
        }
        if (xs < 0 || xl < 0 || xs - xl < 9) continue;
        int xa = -1, xb = -1;
        for (int x = xs + 6; x > xl - 6 && (xa < 0 || xb < 0); x--) {
            if (xa < 0 && (cell(x + 0.5f, y - 7.5f) & 3) == G_SAND) xa = x;
            if (xb < 0 && (cell(x + 0.5f, y + 8.5f) & 3) == G_SAND) xb = x;
        }
        float yaw = xa >= 0 && xb >= 0 ? atan2f_(-(float)(xb - xa), 16.0f) : 0;
        float bw = (float)(xs - xl);
        Beach *t = &beach[n_beach++];
        t->x = xl + bw * 0.62f; t->y = y + 0.5f; t->a = yaw; t->seed = hash2(y, 77); t->kind = 0;
        int placed = 0;
        for (int j = 0; j < 70 && placed < 22 && n_beach < MAXBEACH; j++) {
            u32 h = hash2(y * 31 + j, 5151);
            float uy = y + ((h & 255) / 255.0f - 0.5f) * 38, ux = xl + bw * (0.3f + 0.6f * ((h >> 8 & 255) / 255.0f));
            int g = cell(ux, uy);
            if ((g & 3) != G_SAND || (g & (C_TREE | C_ROAD | C_BLD))) continue;
            int ok = (ux - t->x) * (ux - t->x) + (uy - t->y) * (uy - t->y) > 2.6f * 2.6f;
            for (int q = n_beach - placed; q < n_beach && ok; q++) if ((beach[q].x - ux) * (beach[q].x - ux) + (beach[q].y - uy) * (beach[q].y - uy) < 1.7f * 1.7f) ok = 0;
            if (!ok) continue;
            Beach *u = &beach[n_beach++];
            u->x = ux; u->y = uy; u->a = 0; u->seed = h >> 16 | (h & 0xff) << 16; u->kind = 1;
            placed++;
        }
    }
    for (int i = 0; i < n_beach; i++) {
        Beach *b = &beach[i];
        float ca = cosf_(b->a), sa = sinf_(b->a);
        for (int q = 0; q < (b->kind ? 1 : 4); q++) {
            float lx = b->kind ? 0 : (q & 1) ? 0.38f : -0.38f, ly = b->kind ? 0 : (q & 2) ? 0.38f : -0.38f;
            int cx = (int)((b->x + lx * ca - ly * sa) * CM), cy = (int)((b->y + lx * sa + ly * ca) * CM);
            if (cx >= 0 && cy >= 0 && cx < CW && cy < CH) cmap[cy * CW + cx] |= C_TREE;
        }
    }
    for (int i = 0; i < n_palms; i++) {
        int cx = (int)(palms[i].x * CM), cy = (int)(palms[i].y * CM);
        if (cx >= 0 && cy >= 0 && cx < CW && cy < CH && !(cmap[cy * CW + cx] & C_ROAD)) cmap[cy * CW + cx] |= C_TREE;
    }
}

static Mat mat(Tex *t, u32 tint) { Mat m = {t, tint, 0, 0}; return m; }

static void quad_uv(V3 a, V3 b, V3 c, V3 d, float u0, float v0, float u1, float v1, Mat *m) {
    tri(a, b, c, u0, v1, u1, v1, u1, v0, m);
    tri(a, c, d, u0, v1, u1, v0, u0, v0, m);
}

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

static void draw_bld(Bld *b) {
    float *p = &bp[b->first * 2];
    float zt = b->h, zs = b->shops ? 1.0f : 0.0f;
    int nf = maxi(1, (int)(zt - zs + 0.4f));
    float fh = (zt - zs) / nf;
    for (int i = 0; i < b->n; i++) {
        int j = (i + 1) % b->n;
        float ax = p[i * 2], ay = p[i * 2 + 1], cx = p[j * 2], cy = p[j * 2 + 1];
        float dx = cx - ax, dy = cy - ay, len = sqrtf_(dx * dx + dy * dy);
        if (len < 1e-4f) continue;
        int nb = (int)(len + 0.45f);
        float ua = b->u0 + (i & 3) * 0.25f, ub = nb ? ua + nb / 4.0f : ua + 0.008f;
        V3 nrm = v3(dy / len, -dx / len, 0);
        if ((cam.x - ax) * nrm.x + (cam.y - ay) * nrm.y <= 0) continue;
        u32 lt = light(b->tint, nrm);
        // darker at the wall base hits different
        float s0 = 0.62f, s1 = b->shops ? 0.92f : 1.0f, s2 = 1.04f;
        if (b->shops) {
            int nu = nb ? nb : 1, k0 = 0, unit = 0;
            u32 wh = hash2((int)(b - blds), i * 7 + 3);
            while (k0 < nu) {
                u32 uh = hash2((int)wh, unit);
                int k1 = mini(nu, k0 + 2 + (int)(uh & 1));
                if (nu - k1 == 1) k1 = nu;
                int st = b->sstyle;
                if (st != 3 && unit > 0) { static const u8 mixs[6] = {0, 2, 4, 5, 1, 0}; st = mixs[(uh >> 1) % 6]; }
                if (b->deco && (uh >> 4 & 3) != 0) st = 1;
                float t0 = (float)k0 / nu, t1 = (float)k1 / nu;
                float px0 = ax + dx * t0, py0 = ay + dy * t0, px1 = ax + dx * t1, py1 = ay + dy * t1;
                float u0 = nb ? ua + k0 / 4.0f + (uh >> 6 & 3) * 0.25f : ua, u1 = nb ? u0 + (k1 - k0) / 4.0f : ub;
                Mat sm = mat(t_shops[st], light(st == 1 ? b->tint : 0xf6f2ec, nrm));
                sm.smooth = 1;
                float sa0 = st == 3 ? 0.86f : s0;
                tri_full(v3(px0, py0, 0), v3(px1, py1, 0), v3(px1, py1, zs), u0, 1, u1, 1, u1, 0, sa0, sa0, s1, &sm);
                tri_full(v3(px0, py0, 0), v3(px1, py1, zs), v3(px0, py0, zs), u0, 1, u1, 0, u0, 0, sa0, s1, s1, &sm);
                int aw = st == 1 || ((st == 0 || st == 4) && (b->awn || (uh >> 9 & 1)));
                float seg = len * (t1 - t0);
                if (aw && seg > 0.8f) {
                    float vq = (float)((b->awn ? b->awn - 1 + unit : (int)(uh >> 10)) & 3) * 0.25f + 0.03f, vr = vq + 0.19f, ul = seg * 0.5f;
                    V3 o = scl(nrm, 0.42f), o2 = scl(nrm, 0.43f);
                    float tx = dx / len * 0.06f, ty = dy / len * 0.06f;
                    V3 w0 = v3(px0 + tx, py0 + ty, zs - 0.12f), w1 = v3(px1 - tx, py1 - ty, zs - 0.12f);
                    V3 f0 = add(v3(w0.x, w0.y, zs - 0.34f), o), f1 = add(v3(w1.x, w1.y, zs - 0.34f), o);
                    Mat am = mat(t_awn, light(0xffffff, norm(v3(nrm.x * 0.47f, nrm.y * 0.47f, 0.88f))));
                    am.two_sided = 1;
                    quad_uv(w0, w1, f1, f0, 0, vq, ul, vr, &am);
                    Mat av = mat(t_awn, light(0xffffff, nrm));
                    av.two_sided = 1;
                    quad_uv(f0, f1, add(v3(w1.x, w1.y, zs - 0.42f), o2), add(v3(w0.x, w0.y, zs - 0.42f), o2), 0, vq, ul, vr, &av);
                }
                k0 = k1;
                unit++;
            }
        }
        Mat up = mat(b->face, lt);
        up.smooth = 1;
        float va = b->v0 + nf / 4.0f, sb = b->shops ? s1 : s0;
        float smid = b->shops ? s1 : minf(1.0f, s0 + 0.38f * minf(1.0f, 3.0f / (zt - zs + 0.01f)));
        if (zt - zs > 3.5f) {
            float zm = zs + 3.0f, vm = va - 3.0f / fh / 4.0f;
            tri_full(v3(ax, ay, zs), v3(cx, cy, zs), v3(cx, cy, zm), ua, va, ub, va, ub, vm, sb, sb, 1.0f, &up);
            tri_full(v3(ax, ay, zs), v3(cx, cy, zm), v3(ax, ay, zm), ua, va, ub, vm, ua, vm, sb, 1.0f, 1.0f, &up);
            tri_full(v3(ax, ay, zm), v3(cx, cy, zm), v3(cx, cy, zt), ua, vm, ub, vm, ub, b->v0, 1.0f, 1.0f, s2, &up);
            tri_full(v3(ax, ay, zm), v3(cx, cy, zt), v3(ax, ay, zt), ua, vm, ub, b->v0, ua, b->v0, 1.0f, s2, s2, &up);
        } else {
            tri_full(v3(ax, ay, zs), v3(cx, cy, zs), v3(cx, cy, zt), ua, va, ub, va, ub, b->v0, sb, sb, smid, &up);
            tri_full(v3(ax, ay, zs), v3(cx, cy, zt), v3(ax, ay, zt), ua, va, ub, b->v0, ua, b->v0, sb, smid, smid, &up);
        }
#ifndef NOCORN
        if (zt < 30 && b->face != t_glass && b->face != t_office) {
            V3 o = scl(nrm, 0.06f);
            Mat cm = mat(t_curb, light(b->face == t_shed ? 0xa8acb0 : b->tint, nrm));
            float z0 = zt - 0.16f, z1 = zt + 0.1f;
            quad(add(v3(ax, ay, z0), o), add(v3(cx, cy, z0), o), add(v3(cx, cy, z1), o), add(v3(ax, ay, z1), o), 1, 1, &cm);
            Mat cu = mat(t_curb, light(0xb8b4b0, v3(0, 0, -1)));
            quad(v3(ax, ay, z0), v3(cx, cy, z0), add(v3(cx, cy, z0), o), add(v3(ax, ay, z0), o), 1, 1, &cu);
            if (cam.z > z1) {
                Mat ct = mat(t_curb, light(0xd8d4d0, v3(0, 0, 1)));
                quad(add(v3(ax, ay, z1), o), add(v3(cx, cy, z1), o), v3(cx, cy, z1), v3(ax, ay, z1), 1, 1, &ct);
            }
        }
#endif
        if (b->neon) {
            Mat nm = mat(neon_tex(b->neon), 0x808080);
            V3 o = scl(nrm, 0.05f);
            float nb = b->deco ? 0.17f : 0.1f;
            quad(add(v3(ax, ay, zt - nb), o), add(v3(cx, cy, zt - nb), o), add(v3(cx, cy, zt), o), add(v3(ax, ay, zt), o), 1, 1, &nm);
            if (b->shops) quad(add(v3(ax, ay, zs - 0.02f), o), add(v3(cx, cy, zs - 0.02f), o), add(v3(cx, cy, zs + 0.06f), o), add(v3(ax, ay, zs + 0.06f), o), 1, 1, &nm);
        }
        if (b->deco && len > 0.7f) {
            V3 o = scl(nrm, 0.13f);
            Mat lf = mat(t_white, light(0xfbf8f2, nrm)), lt2 = mat(t_white, light(0xfbf8f2, v3(0, 0, cam.z > 1 ? 1 : -1)));
            float tx = dx / len * 0.06f, ty = dy / len * 0.06f;
            V3 a0 = v3(ax + tx, ay + ty, 0), c0 = v3(cx - tx, cy - ty, 0);
            for (float z = zt - 0.28f * fh; z > zs + 0.45f * fh; z -= fh) {
                V3 a1 = v3(a0.x, a0.y, z), c1 = v3(c0.x, c0.y, z), a2 = v3(a0.x, a0.y, z - 0.05f), c2 = v3(c0.x, c0.y, z - 0.05f);
                quad(add(a2, o), add(c2, o), add(c1, o), add(a1, o), 1, 1, &lf);
                float zz = cam.z > z ? z : z - 0.05f;
                quad(v3(a0.x, a0.y, zz), v3(c0.x, c0.y, zz), add(v3(c0.x, c0.y, zz), o), add(v3(a0.x, a0.y, zz), o), 1, 1, &lt2);
            }
            if (b->neon && zt > 2) {
                Mat nm = mat(neon_tex(b->neon), 0x808080);
                V3 on = scl(nrm, 0.06f);
                float wx = dx / len * 0.11f, wy = dy / len * 0.11f;
                quad(add(v3(ax, ay, zs + 0.1f), on), add(v3(ax + wx, ay + wy, zs + 0.1f), on), add(v3(ax + wx, ay + wy, zt - 0.1f), on), add(v3(ax, ay, zt - 0.1f), on), 1, 1, &nm);
            }
        }
    }
    if (cam.z > zt) {
        Mat r = mat(t_roof, light(b->rtint, v3(0, 0, 1)));
        u16 *t = &btri[b->tri_first * 3];
        for (int i = 0; i < b->tri_n; i++) {
            float *a = &p[t[i * 3] * 2], *bb = &p[t[i * 3 + 1] * 2], *c = &p[t[i * 3 + 2] * 2];
            tri(v3(a[0], a[1], zt), v3(bb[0], bb[1], zt), v3(c[0], c[1], zt), a[0], a[1], bb[0], bb[1], c[0], c[1], &r);
        }
    }
    // roof bits so tall ones dont look bald
    if (b->roof) {
        float cx = (b->minx + b->maxx) / 2, cy = (b->miny + b->maxy) / 2;
        if (b->roof == 1) {
            float w = minf(b->maxx - b->minx, b->maxy - b->miny);
            box(v3(cx - 0.3f, cy - 0.25f, zt), v3(cx + 0.3f, cy + 0.25f, zt + 0.35f), t_curb, 0xc0c0c8, 0);
            if (w > 3) box(v3(cx + 0.6f, cy - 0.2f, zt), v3(cx + 1.0f, cy + 0.2f, zt + 0.28f), t_curb, 0xb0b0b8, 0);
            if (w > 5) box(v3(cx - 1.3f, cy + 0.4f, zt), v3(cx - 0.8f, cy + 0.9f, zt + 0.5f), t_curb, 0xa8a8b0, 0);
        }
        else if (b->roof == 2) {
            box(v3(cx - 0.04f, cy - 0.04f, zt), v3(cx + 0.04f, cy + 0.04f, zt + 3.0f), t_curb, 0xd0d0d0, 0);
            box(v3(cx - 0.08f, cy - 0.08f, zt + 3.0f), v3(cx + 0.08f, cy + 0.08f, zt + 3.16f), t_red, 0xffffff, 0);
        } else box(v3(cx - 0.6f, cy - 0.6f, zt), v3(cx + 0.6f, cy + 0.6f, zt + 0.9f), b->face, b->tint, 0);
    }
}

static void rib_at(float *r0, float *r1, float t, float f, float z, V3 *o) {
    float lx = r0[0] + (r1[0] - r0[0]) * t, ly = r0[1] + (r1[1] - r0[1]) * t;
    float qx = r0[2] + (r1[2] - r0[2]) * t, qy = r0[3] + (r1[3] - r0[3]) * t;
    *o = v3(lx + (qx - lx) * f, ly + (qy - ly) * f, z);
}

static void decal(float *r0, float *r1, float t0, float t1, float f0, float f1, Mat *m) {
    V3 a, b, c, d;
    float z = ROAD_Z + 0.004f;
    rib_at(r0, r1, t0, f0, z, &a); rib_at(r0, r1, t0, f1, z, &b);
    rib_at(r0, r1, t1, f1, z, &c); rib_at(r0, r1, t1, f0, z, &d);
    quad(a, b, c, d, 1, 1, m);
}

static int edge_at(Edge *e, float s, float *t) {
    for (int k = 0; k + 1 < e->n; k++) {
        float *r0 = &rib[(e->first + k) * 5], *r1 = r0 + 5;
        float s0 = r0[4] * e->w, s1 = r1[4] * e->w;
        if (s <= s1 || k + 2 == e->n) { *t = (s - s0) / maxf(s1 - s0, 1e-4f); return k; }
    }
    *t = 0;
    return 0;
}

static void crossing(Edge *e, int end, Mat *wm) {
    int node = end ? e->b : e->a;
    if (nodes[node].n < 3 || (e->flags & 1) || e->cls <= 1) return;
    float L = rib[(e->first + e->n - 1) * 5 + 4] * e->w, c0 = nwid[node] * 0.5f + 0.15f, c1 = c0 + 0.85f;
    if (c1 + 0.5f > L * 0.5f) return;
    float w = e->w;
    for (int pass = 0; pass < 2; pass++) {
        float a0 = pass ? c1 + 0.22f : c0, a1 = pass ? c1 + 0.31f : c1;
        if (pass && !nlit[node]) break;
        float sa = end ? L - a1 : a0, sb = end ? L - a0 : a1, t0, t1;
        int k = edge_at(e, (sa + sb) * 0.5f, &t0);
        float *r0 = &rib[(e->first + k) * 5], *r1 = r0 + 5;
        float s0 = r0[4] * w, s1 = r1[4] * w, dl = maxf(s1 - s0, 1e-4f);
        t0 = (sa - s0) / dl; t1 = (sb - s0) / dl;
        if (pass) {
            int ow = (e->flags & 2) || e->cls == 0;
            if (ow && !end) break;
            float f0 = ow ? 0.04f : end ? 0.5f : 0.04f, f1 = ow ? 0.96f : end ? 0.96f : 0.5f;
            decal(r0, r1, t0, t1, f0, f1, wm);
        } else {
            int n = (int)((w - 0.2f) / 0.3f);
            float m = (w - n * 0.3f) * 0.5f / w;
            for (int q = 0; q < n; q++) {
                float f0 = m + q * 0.3f / w, f1 = f0 + 0.16f / w;
                decal(r0, r1, t0, t1, f0, f1, wm);
            }
        }
    }
}

static void curb_run(V3 e0, V3 e1, float ox, float oy, Mat *face, Mat *top, Mat *back) {
    float cz = 0.085f, tw = 0.11f;
    V3 t0 = v3(e0.x + ox * tw, e0.y + oy * tw, cz), t1 = v3(e1.x + ox * tw, e1.y + oy * tw, cz);
    quad(v3(e0.x, e0.y, ROAD_Z - 0.01f), v3(e1.x, e1.y, ROAD_Z - 0.01f), v3(e1.x, e1.y, cz), v3(e0.x, e0.y, cz), 1, 1, face);
    quad(v3(e0.x, e0.y, cz), v3(e1.x, e1.y, cz), t1, t0, 1, 1, top);
    quad(t0, t1, v3(t1.x, t1.y, -0.01f), v3(t0.x, t0.y, -0.01f), 1, 1, back);
}

static void draw_edge(Edge *e) {
    int oneway = e->flags & 2 || e->cls == 0 || e->cls >= 8;
    Mat m = mat(oneway ? t_road1 : t_road, light(0xffffff, v3(0, 0, 1)));
    m.filt = 1;
    float w = e->w, uw = w / 4.0f;
    Mat yl = mat(t_white, light(0xf0c038, v3(0, 0, 1))), wl = mat(t_white, light(0xf4f4f0, v3(0, 0, 1)));
    Mat cf = mat(t_curb, light(0xc4c0b8, v3(0, 0, 1))), ct = mat(t_curb, light(0xe0dcd4, v3(0, 0, 1))), cb = mat(t_curb, light(0xb8b4ac, v3(0, 0, 1)));
    cf.two_sided = ct.two_sided = cb.two_sided = 1;
    int park = (e->flags & 4) != 0, two = !oneway;
    float el = park ? 0.82f : 0.13f, dw = two ? w * 0.5f - el : w - 2 * el;
    int nl = maxi(1, (int)(dw / 1.15f + 0.3f));
    int marks = w > 1.5f && !(e->cls >= 8);
    float Ltot = rib[(e->first + e->n - 1) * 5 + 4] * w;
    float ms0 = nodes[e->a].n >= 3 ? nwid[e->a] * 0.5f + 1.35f : 0, ms1 = Ltot - (nodes[e->b].n >= 3 ? nwid[e->b] * 0.5f + 1.35f : 0);
    for (int k = 0; k + 1 < e->n; k++) {
        float *r0 = &rib[(e->first + k) * 5], *r1 = &rib[(e->first + k + 1) * 5];
        float len = (r1[4] - r0[4]) * w;
        int pieces = (int)(len / 6) + 1;
        for (int s = 0; s < pieces; s++) {
            float t0 = (float)s / pieces, t1 = (float)(s + 1) / pieces;
            float l0x = r0[0] + (r1[0] - r0[0]) * t0, l0y = r0[1] + (r1[1] - r0[1]) * t0, l1x = r0[0] + (r1[0] - r0[0]) * t1, l1y = r0[1] + (r1[1] - r0[1]) * t1;
            float q0x = r0[2] + (r1[2] - r0[2]) * t0, q0y = r0[3] + (r1[3] - r0[3]) * t0, q1x = r0[2] + (r1[2] - r0[2]) * t1, q1y = r0[3] + (r1[3] - r0[3]) * t1;
            float va = (r0[4] + (r1[4] - r0[4]) * t0) * uw, vb = (r0[4] + (r1[4] - r0[4]) * t1) * uw;
            V3 a = v3(l0x, l0y, ROAD_Z), b = v3(q0x, q0y, ROAD_Z), c = v3(q1x, q1y, ROAD_Z), d = v3(l1x, l1y, ROAD_Z);
            tri(a, b, c, 0, va, uw, va, uw, vb, &m);
            tri(a, c, d, 0, va, uw, vb, 0, vb, &m);
            float mx_ = (l0x + q1x) * 0.5f - cam.x, my_ = (l0y + q1y) * 0.5f - cam.y, dd = mx_ * mx_ + my_ * my_;
            if (dd > 60 * 60) continue;
            float psa = (r0[4] + (r1[4] - r0[4]) * t0) * w, psb = (r0[4] + (r1[4] - r0[4]) * t1) * w;
            if (marks && psb > ms0 && psa < ms1) {
                float ot0 = t0, ot1 = t1;
                if (psa < ms0) t0 = ot0 + (ot1 - ot0) * (ms0 - psa) / maxf(psb - psa, 1e-4f);
                if (psb > ms1) t1 = ot0 + (ot1 - ot0) * (ms1 - psa) / maxf(psb - psa, 1e-4f);
                float hw = 0.018f / w;
                if (two) {
                    float g = 0.04f / w;
                    decal(r0, r1, t0, t1, 0.5f - g - hw, 0.5f - g + hw, &yl);
                    decal(r0, r1, t0, t1, 0.5f + g - hw, 0.5f + g + hw, &yl);
                }
                if (w > 2.2f) {
                    decal(r0, r1, t0, t1, el / w - hw, el / w + hw, &wl);
                    decal(r0, r1, t0, t1, 1 - el / w - hw, 1 - el / w + hw, &wl);
                }
                if (nl > 1 && dd < 45 * 45) {
                    float sa = (r0[4] + (r1[4] - r0[4]) * t0) * w, sb = (r0[4] + (r1[4] - r0[4]) * t1) * w, per = 2.6f, dash = 0.95f;
                    for (int side = 0; side < (two ? 2 : 1); side++)
                        for (int q = 1; q < nl; q++) {
                            float off = el + q * dw / nl, f = side ? 1 - off / w : (two ? off / w : off / w);
                            for (float ds = floori(sa / per) * per; ds < sb; ds += per) {
                                float d0 = maxf(ds, sa), d1 = minf(ds + dash, sb);
                                if (d1 <= d0) continue;
                                float u0 = t0 + (t1 - t0) * (d0 - sa) / maxf(sb - sa, 1e-4f), u1 = t0 + (t1 - t0) * (d1 - sa) / maxf(sb - sa, 1e-4f);
                                decal(r0, r1, u0, u1, f - hw, f + hw, &wl);
                            }
                        }
                }
            }
            t0 = (float)s / pieces; t1 = (float)(s + 1) / pieces;
            if (!(e->flags & 1) && e->cls > 1 && dd < 50 * 50) {
                for (int side = 0; side < 2; side++) {
                    float ex0 = side ? q0x : l0x, ey0 = side ? q0y : l0y, ex1 = side ? q1x : l1x, ey1 = side ? q1y : l1y;
                    float ox = side ? q0x - l0x : l0x - q0x, oy = side ? q0y - l0y : l0y - q0y, ol = sqrtf_(ox * ox + oy * oy) + 1e-6f;
                    ox /= ol; oy /= ol;
                    int nc = maxi(1, (int)(len / pieces / 1.2f) + 1), run = -1;
                    for (int q = 0; q <= nc; q++) {
                        int ok = 0;
                        if (q < nc) {
                            float tm = (q + 0.5f) / nc, px = ex0 + (ex1 - ex0) * tm + ox * 0.9f, py = ey0 + (ey1 - ey0) * tm + oy * 0.9f;
                            int cc = cell(px, py);
                            ok = !(cc & C_ROAD) && (cc & 3) != G_SEA;
                        }
                        if (ok && run < 0) run = q;
                        if (!ok && run >= 0) {
                            float ta = (float)run / nc, tb = (float)q / nc;
                            curb_run(v3(ex0 + (ex1 - ex0) * ta, ey0 + (ey1 - ey0) * ta, 0), v3(ex0 + (ex1 - ex0) * tb, ey0 + (ey1 - ey0) * tb, 0), ox, oy, &cf, &ct, &cb);
                            run = -1;
                        }
                    }
                }
            }
        }
        if (e->flags & 1) {
            float *p0 = &ep[(e->first + k) * 2], *p1 = &ep[(e->first + k + 1) * 2];
            float dx = p1[0] - p0[0], dy = p1[1] - p0[1], L = sqrtf_(dx * dx + dy * dy) + 1e-6f;
            V3 nl = v3(-dy / L, dx / L, 0), nr = v3(dy / L, -dx / L, 0);
            Mat wall = mat(t_curb, light(0xf0ece8, nl)), wall2 = mat(t_curb, light(0xf0ece8, nr));
            V3 la = v3(r0[0], r0[1], 0), ld = v3(r1[0], r1[1], 0), ra = v3(r0[2], r0[3], 0), rc = v3(r1[2], r1[3], 0);
            quad(v3(ld.x, ld.y, -0.35f), v3(la.x, la.y, -0.35f), v3(la.x, la.y, 0.3f), v3(ld.x, ld.y, 0.3f), 1, 1, &wall);
            quad(v3(ra.x, ra.y, -0.35f), v3(rc.x, rc.y, -0.35f), v3(rc.x, rc.y, 0.3f), v3(ra.x, ra.y, 0.3f), 1, 1, &wall2);
            Mat in1 = mat(t_curb, light(0xe0dcd8, nr)), in2 = mat(t_curb, light(0xe0dcd8, nl));
            quad(v3(la.x, la.y, ROAD_Z), v3(ld.x, ld.y, ROAD_Z), v3(ld.x, ld.y, 0.3f), v3(la.x, la.y, 0.3f), 1, 1, &in1);
            quad(v3(rc.x, rc.y, ROAD_Z), v3(ra.x, ra.y, ROAD_Z), v3(ra.x, ra.y, 0.3f), v3(rc.x, rc.y, 0.3f), 1, 1, &in2);
            Mat top = mat(t_curb, light(0xf8f4f0, v3(0, 0, 1)));
            quad(v3(la.x, la.y, 0.3f), v3(ld.x, ld.y, 0.3f), add(v3(ld.x, ld.y, 0.3f), scl(nl, 0.15f)), add(v3(la.x, la.y, 0.3f), scl(nl, 0.15f)), 1, 1, &top);
        }
    }
    float ex = e->n > 0 ? ep[e->first * 2] - cam.x : 0, ey = e->n > 0 ? ep[e->first * 2 + 1] - cam.y : 0;
    float fx = ep[(e->first + e->n - 1) * 2] - cam.x, fy = ep[(e->first + e->n - 1) * 2 + 1] - cam.y;
    if (ex * ex + ey * ey < 55 * 55) crossing(e, 0, &wl);
    if (fx * fx + fy * fy < 55 * 55) crossing(e, 1, &wl);
}

static float sea_t;

static void draw_sea(void) {
    Mat m = mat(t_sea, light(0xffffff, v3(0, 0, 1)));
    float s = 4.0f, o = sea_t;
    float R = fog_far * 1.75f + 20;
    int i0 = (int)((cam.x - R) / 40) - 1, i1 = (int)((cam.x + R) / 40) + 1, j0 = (int)((cam.y - R) / 40) - 1, j1 = (int)((cam.y + R) / 40) + 1;
    for (int j = j0; j <= j1; j++)
        for (int i = i0; i <= i1; i++) {
            float x0 = i * 40.0f, y0 = j * 40.0f, x1 = x0 + 40, y1 = y0 + 40;
            V3 d = sub(v3(x0 + 20, y0 + 20, 0), cam);
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

// marching squares inside means over half
static u8 *gstyle;

static float wear(float x, float y) {
    float n = 0, amp = 1, tot = 0, sc = 1 / 7.0f;
    for (int o = 0; o < 2; o++) {
        float fx = x * sc, fy = y * sc;
        int ix = floori(fx), iy = floori(fy);
        float tx = fx - ix, ty = fy - iy;
        tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty);
        float a = (hash2(ix, iy + o * 977) & 255) / 255.0f, b = (hash2(ix + 1, iy + o * 977) & 255) / 255.0f;
        float c = (hash2(ix, iy + 1 + o * 977) & 255) / 255.0f, d = (hash2(ix + 1, iy + 1 + o * 977) & 255) / 255.0f;
        n += (a + (b - a) * tx + (c - a) * ty + (a - b - c + d) * tx * ty) * amp;
        tot += amp; amp *= 0.5f; sc *= 2.6f;
    }
    n /= tot;
    return 0.8f + 0.34f * n;
}

static void make_gstyle(void) {
    gstyle = (u8 *)pool((usize)TW * TH);
    for (int ty = 0; ty < TH; ty++)
        for (int tx = 0; tx < TW; tx++) {
            float cx = tx * TG + TG * 0.5f, cy = ty * TG + TG * 0.5f;
            int bx = (int)(cx / BK), by = (int)(cy / BK), best = -1;
            float bd = 12 * 12;
            if (bx >= 0 && by >= 0 && bx < BW && by < BH) {
                Bucket *k = &bk[0][by * BW + bx];
                for (u32 i = 0; i < k->n; i++) {
                    Bld *b = &blds[bl[0][k->first + i]];
                    float qx = clampf(cx, b->minx, b->maxx) - cx, qy = clampf(cy, b->miny, b->maxy) - cy, d = qx * qx + qy * qy;
                    if (d < bd) { bd = d; best = (int)bl[0][k->first + i]; }
                }
            }
            u32 h = hash2(tx >> 3, (ty >> 3) * 977 + 4242);
            (void)best;
            int beachy = cx > 2560, town = cx < 1500 && cy > 600, r = (int)(h % 20);
            u8 g;
            if (beachy) g = r < 7 ? 0 : r < 12 ? 1 : r < 15 ? 3 : r < 17 ? 4 : 5;
            else if (town) g = r < 9 ? 0 : r < 14 ? 2 : r < 17 ? 7 : 4;
            else g = r < 12 ? 0 : r < 16 ? 2 : r < 18 ? 6 : 3;
            gstyle[ty * TW + tx] = g;
            int dark = 0, any = 0;
            for (int y = ty * TG * CM; y < (ty + 1) * TG * CM && y < CH; y++)
                for (int x = tx * TG * CM; x < (tx + 1) * TG * CM && x < CW; x++) {
                    int c = cmap[y * CW + x];
                    any += (c & C_LOT) != 0;
                    dark += (c & C_LOT2) != 0;
                }
            if (any && dark * 2 >= any) gstyle[ty * TW + tx] |= 8;
        }
}

static void ground_tri(float ax, float ay, float bx, float by, float cx, float cy, float z, float sc, Mat *m) {
    tri_full(v3(ax, ay, z), v3(bx, by, z), v3(cx, cy, z), ax * sc, ay * sc, bx * sc, by * sc, cx * sc, cy * sc, wear(ax, ay), wear(bx, by), wear(cx, cy), m);
}

static void ms_cell(int x, int y, u8 *f, float z, Mat *m, float sc, int walls) {
    int FW = GW + 1;
    int v[4] = {f[y * FW + x], f[y * FW + x + 1], f[(y + 1) * FW + x + 1], f[(y + 1) * FW + x]};
    float px[4] = {(float)x, (float)x + 1, (float)x + 1, (float)x}, py[4] = {(float)y, (float)y, (float)y + 1, (float)y + 1};
    float ox[8], oy[8];
    int cr[8], n = 0;
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) & 3;
        int a = v[i] >= 128, b = v[j] >= 128;
        if (a) { ox[n] = px[i]; oy[n] = py[i]; cr[n++] = 0; }
        if (a != b) {
            float t = (128.0f - v[i]) / (float)(v[j] - v[i]);
            ox[n] = px[i] + (px[j] - px[i]) * t; oy[n] = py[i] + (py[j] - py[i]) * t; cr[n++] = 1;
        }
    }
    if (n < 3) return;
    for (int k = 1; k + 1 < n; k++) ground_tri(ox[0], oy[0], ox[k], oy[k], ox[k + 1], oy[k + 1], z, sc, m);
    if (!walls) return;
    for (int k = 0; k < n; k++) {
        int j = (k + 1) % n;
        if (!cr[k] || !cr[j]) continue;
        float dx = ox[j] - ox[k], dy = oy[j] - oy[k], L = sqrtf_(dx * dx + dy * dy);
        if (L < 1e-4f) continue;
        V3 nrm = v3(dy / L, -dx / L, 0);
        if ((cam.x - ox[k]) * nrm.x + (cam.y - oy[k]) * nrm.y <= 0) continue;
        Mat wm = mat(t_curb, light(0xc8c0b8, nrm));
        quad(v3(ox[k], oy[k], SEA_Z), v3(ox[j], oy[j], SEA_Z), v3(ox[j], oy[j], z), v3(ox[k], oy[k], z), L, 1, &wm);
    }
}

static int visible(float x0, float y0, float x1, float y1) {
    float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, r = maxf(x1 - x0, y1 - y0) * 0.75f + 2;
    float dx = cx - cam.x, dy = cy - cam.y;
    if (dx * dx + dy * dy > (fog_far + r) * (fog_far + r)) return 0;
    float zc = dx * cam_f.x + dy * cam_f.y, xc = dx * cam_r.x + dy * cam_r.y;
    if (cam_f.z < -0.35f) return 1;
    if (zc < -r) return 0;
    if (absf(xc) - r > (zc + r) * (half_w / focal) * 1.25f + 2) return 0;
    return 1;
}

static void draw_beach(void) {
    float R2 = (fog_far * 0.85f) * (fog_far * 0.85f);
    for (int i = 0; i < n_beach; i++) {
        Beach *b = &beach[i];
        float dx = b->x - cam.x, dy = b->y - cam.y, d2 = dx * dx + dy * dy;
        if (d2 > (b->kind ? minf(R2, 50 * 50) : R2) || dx * cam_f.x + dy * cam_f.y < -3) continue;
        if (!visible(b->x - 1.5f, b->y - 1.5f, b->x + 1.5f, b->y + 1.5f)) continue;
        if (b->kind) draw_umbrella(v3(b->x, b->y, 0.004f), b->seed, d2 > 22 * 22);
        else draw_tower(v3(b->x, b->y, 0.004f), b->a, b->seed, d2 > 35 * 35);
    }
}

// far city blocks so the skyline never vanishes
typedef struct { float x[4], y[4], cx, cy, rad, h; u32 tint; } FarB;
static FarB *farb;
static int n_farb;
static void make_far(void) {
    farb = (FarB *)pool((usize)n_blds * sizeof(FarB));
    n_farb = 0;
    for (int i = 0; i < n_blds; i++) {
        Bld *b = &blds[i];
        if (b->h < 5) continue;
        float *p = &bp[b->first * 2], best = 1e18f, bu = 1, bv = 0, lo0 = 0, hi0 = 0, lo1 = 0, hi1 = 0;
        for (int e = 0; e < b->n; e++) {
            int f = (e + 1) % b->n;
            float dx = p[f * 2] - p[e * 2], dy = p[f * 2 + 1] - p[e * 2 + 1], l = sqrtf_(dx * dx + dy * dy);
            if (l < 0.3f) continue;
            float ux = dx / l, uy = dy / l, a0 = 1e9f, a1 = -1e9f, c0 = 1e9f, c1 = -1e9f;
            for (int k = 0; k < b->n; k++) {
                float u = p[k * 2] * ux + p[k * 2 + 1] * uy, v = -p[k * 2] * uy + p[k * 2 + 1] * ux;
                a0 = minf(a0, u); a1 = maxf(a1, u); c0 = minf(c0, v); c1 = maxf(c1, v);
            }
            float ar = (a1 - a0) * (c1 - c0);
            if (ar < best) { best = ar; bu = ux; bv = uy; lo0 = a0; hi0 = a1; lo1 = c0; hi1 = c1; }
        }
        if (best > 1e17f) continue;
        FarB *f = &farb[n_farb++];
        float us[4] = {lo0, hi0, hi0, lo0}, vs[4] = {lo1, lo1, hi1, hi1};
        for (int k = 0; k < 4; k++) { f->x[k] = us[k] * bu - vs[k] * bv; f->y[k] = us[k] * bv + vs[k] * bu; }
        f->cx = (f->x[0] + f->x[2]) / 2; f->cy = (f->y[0] + f->y[2]) / 2;
        f->rad = sqrtf_((hi0 - lo0) * (hi0 - lo0) + (hi1 - lo1) * (hi1 - lo1)) / 2;
        f->h = b->h; f->tint = b->tint;
    }
}
static void draw_far_city(void) {
    float lim = fog_far * 0.9f, sp = half_w / focal * 1.25f;
    u32 haze = fog_col >> 1 & 0x7f7f7f;
    float hl = sqrtf_(cam_f.x * cam_f.x + cam_f.y * cam_f.y) + 1e-6f, fx = cam_f.x / hl, fy = cam_f.y / hl;
    Mat m = {t_white, 0, 0, 1};
    for (int i = 0; i < n_farb; i++) {
        FarB *f = &farb[i];
        float dx = f->cx - cam.x, dy = f->cy - cam.y, d2 = dx * dx + dy * dy;
        if (d2 > 900.0f * 900.0f) continue;
        float d = sqrtf_(d2);
        if (d - f->rad < lim) continue;
        float zc = dx * fx + dy * fy, xc = dx * fy * -1 + dy * fx;
        if (zc < -f->rad || absf(xc) - f->rad > (zc + f->rad) * sp + 2) continue;
        float k = clampf(0.78f + (d - lim) * 0.00022f, 0.78f, 0.93f);
        for (int e = 0; e < 4; e++) {
            int g = (e + 1) & 3;
            float ex = f->x[g] - f->x[e], ey = f->y[g] - f->y[e], el = sqrtf_(ex * ex + ey * ey) + 1e-6f;
            V3 n = v3(ey / el, -ex / el, 0);
            float mx_ = (f->x[e] + f->x[g]) / 2 - f->cx, my_ = (f->y[e] + f->y[g]) / 2 - f->cy;
            if (n.x * mx_ + n.y * my_ < 0) n = scl(n, -1);
            float vx = (f->x[e] + f->x[g]) / 2 - cam.x, vy = (f->y[e] + f->y[g]) / 2 - cam.y;
            if (vx * n.x + vy * n.y >= 0) continue;
            m.tint = mix(light(f->tint, n), haze, (u32)(k * 256));
            Mat mm = m;
            mm.two_sided = 1;
            quad(v3(f->x[e], f->y[e], 0), v3(f->x[g], f->y[g], 0), v3(f->x[g], f->y[g], f->h), v3(f->x[e], f->y[e], f->h), 1, 1, &mm);
        }
    }
}

static void draw_world_map(void) {
    frame_no++;
    float R = fog_far + 5;
    int bx0 = maxi(0, (int)((cam.x - R) / BK)), bx1 = mini(BW - 1, (int)((cam.x + R) / BK));
    int by0 = maxi(0, (int)((cam.y - R) / BK)), by1 = mini(BH - 1, (int)((cam.y + R) / BK));
    for (int by = by0; by <= by1; by++)
        for (int bx = bx0; bx <= bx1; bx++) {
            Bucket *k = &bk[1][by * BW + bx];
            for (u32 i = 0; i < k->n; i++) {
                u32 id = bl[1][k->first + i];
                if (stamp[1][id] == frame_no) continue;
                stamp[1][id] = frame_no;
                float r[4];
                box_edge((int)id, r);
                if (visible(r[0], r[1], r[2], r[3])) draw_edge(&edges[id]);
            }
        }
    for (int by = by0; by <= by1; by++)
        for (int bx = bx0; bx <= bx1; bx++) {
            Bucket *k = &bk[0][by * BW + bx];
            for (u32 i = 0; i < k->n; i++) {
                u32 id = bl[0][k->first + i];
                if (stamp[0][id] == frame_no) continue;
                stamp[0][id] = frame_no;
                Bld *b = &blds[id];
                if (visible(b->minx, b->miny, b->maxx, b->maxy)) { draw_bld(b); draw_bld_signs((int)id); }
            }
        }
    u32 glt = light(0xffffff, v3(0, 0, 1));
    Mat gm[4] = {mat(t_walk, glt), mat(t_grass, glt), mat(t_sand, glt), mat(t_lot, glt)};
    Mat lots[2] = {mat(t_lot, glt), mat(t_lot2, glt)};
    lots[0].smooth = lots[1].smooth = 1;
    lots[0].filt = lots[1].filt = 1;
    Mat walks[8] = {mat(t_walk, glt), mat(t_walk2, glt), mat(t_walk3, glt), mat(t_walk4, glt), mat(t_walk5, glt), mat(t_walk6, glt), mat(t_walk7, glt), mat(t_walk8, glt)};
    for (int i = 0; i < 8; i++) { walks[i].smooth = 1; walks[i].filt = 1; }
    gm[1].filt = gm[2].filt = gm[3].filt = 1;
    gm[1].smooth = gm[2].smooth = 1;
    float gsc[4] = {0.25f, 0.14f, 0.3f, 0.15625f}, gz[4] = {0, 0.004f, 0.004f, 0.004f};
    int tx0 = maxi(0, (int)((cam.x - R) / TG)), tx1 = mini(TW - 1, (int)((cam.x + R) / TG));
    int ty0 = maxi(0, (int)((cam.y - R) / TG)), ty1 = mini(TH - 1, (int)((cam.y + R) / TG));
    for (int ty = ty0; ty <= ty1; ty++)
        for (int tx = tx0; tx <= tx1; tx++) {
            int fl = gtile[ty * TW + tx];
            if (!fl) continue;
#ifdef NOGROUND
            continue;
#endif
            float x0 = (float)(tx * TG), y0 = (float)(ty * TG);
            if (!visible(x0, y0, x0 + TG, y0 + TG)) continue;
            int covered = ((fl >> 2) & 3) == 1 || ((fl >> 4) & 3) == 1 || ((fl >> 6) & 3) == 1;
            int gs = gstyle[ty * TW + tx];
            gm[0] = walks[gs & 7];
            gm[3] = lots[gs >> 3 & 1];
            for (int layer = 0; layer < 4; layer++) {
                int st = (fl >> (layer * 2)) & 3;
                if (!st || (layer == 0 && covered && st == 1)) continue;
                float sc = gsc[layer], z = gz[layer];
                if (st == 1) {
                    ground_tri(x0, y0, x0 + TG, y0, x0 + TG, y0 + TG, z, sc, &gm[layer]);
                    ground_tri(x0, y0, x0 + TG, y0 + TG, x0, y0 + TG, z, sc, &gm[layer]);
                    continue;
                }
                u8 *f = field[layer];
                int FW = GW + 1;
                for (int y = ty * TG; y < ty * TG + TG && y < GH; y++) {
                    int run0 = -1;
                    for (int x = tx * TG; x <= tx * TG + TG; x++) {
                        int in = x < tx * TG + TG && x < GW && f[y * FW + x] >= 128 && f[y * FW + x + 1] >= 128 && f[(y + 1) * FW + x] >= 128 && f[(y + 1) * FW + x + 1] >= 128;
                        if (in) { if (run0 < 0) run0 = x; continue; }
                        if (run0 >= 0) {
                            float a = (float)run0, b = (float)x;
                            ground_tri(a, (float)y, b, (float)y, b, y + 1.0f, z, sc, &gm[layer]);
                            ground_tri(a, (float)y, b, y + 1.0f, a, y + 1.0f, z, sc, &gm[layer]);
                            run0 = -1;
                        }
                        if (x < tx * TG + TG && x < GW) ms_cell(x, y, f, z, &gm[layer], sc, layer == 0);
                    }
                }
            }
        }
    float pr2 = (fog_far * 0.8f) * (fog_far * 0.8f);
    for (int by = by0; by <= by1; by++)
        for (int bx = bx0; bx <= bx1; bx++) {
            Bucket *k = &bk[3][by * BW + bx];
            for (u32 i = 0; i < k->n; i++) {
                Palm *p = &palms[bl[3][k->first + i]];
                float dx = p->x - cam.x, dy = p->y - cam.y, d2 = dx * dx + dy * dy;
                if (d2 > pr2 || dx * cam_f.x + dy * cam_f.y < -3 - p->r) continue;
                if (!visible(p->x - p->r, p->y - p->r, p->x + p->r, p->y + p->r)) continue;
                // camera inside a crown is just a green screen lol
                if (d2 < (p->r + 0.6f) * (p->r + 0.6f) && cam.z > p->h * 0.25f) continue;
                int lod = d2 > 30 * 30;
                if (p->kind == 2) draw_broad(v3(p->x, p->y, 0), p->seed, p->h, p->r, lod);
                else draw_palm(v3(p->x, p->y, 0), p->seed, p->h, lod);
            }
        }
}

// shadows are walls swept away from the sun
static void bld_shadow(Bld *b, float dx, float dy, Mat *m) {
    float *p = &bp[b->first * 2];
    float ox = dx * b->h, oy = dy * b->h, z = ROAD_Z + 0.005f;
    for (int i = 0; i < b->n; i++) {
        int j = (i + 1) % b->n;
        V3 a = v3(p[i * 2], p[i * 2 + 1], z), c = v3(p[j * 2], p[j * 2 + 1], z);
        quad(a, c, v3(c.x + ox, c.y + oy, z), v3(a.x + ox, a.y + oy, z), 1, 1, m);
    }
    u16 *t = &btri[b->tri_first * 3];
    for (int i = 0; i < b->tri_n; i++) {
        float *a = &p[t[i * 3] * 2], *bb = &p[t[i * 3 + 1] * 2], *c = &p[t[i * 3 + 2] * 2];
        tri(v3(a[0] + ox, a[1] + oy, z), v3(bb[0] + ox, bb[1] + oy, z), v3(c[0] + ox, c[1] + oy, z), 0, 0, 1, 0, 1, 1, m);
    }
}

static void draw_shadows(void) {
    if (sun_vis < 0.3f || sun_dir.z < 0.02f) return;
    float sz = maxf(sun_dir.z, 0.3f);
    float dx = -sun_dir.x / sz, dy = -sun_dir.y / sz;
    Mat m = {t_white, 0, 1, 1, 0, 1};
    float R = fog_far * 0.85f;
    int bx0 = maxi(0, (int)((cam.x - R) / BK) - 1), bx1 = mini(BW - 1, (int)((cam.x + R) / BK) + 1);
    int by0 = maxi(0, (int)((cam.y - R) / BK) - 1), by1 = mini(BH - 1, (int)((cam.y + R) / BK) + 1);
    frame_no++;
    for (int by = by0; by <= by1; by++)
        for (int bx = bx0; bx <= bx1; bx++) {
            Bucket *k = &bk[0][by * BW + bx];
            for (u32 i = 0; i < k->n; i++) {
                u32 id = bl[0][k->first + i];
                if (stamp[0][id] == frame_no) continue;
                stamp[0][id] = frame_no;
                Bld *b = &blds[id];
                float ex = dx * b->h, ey = dy * b->h;
                if (!visible(minf(b->minx, b->minx + ex), minf(b->miny, b->miny + ey), maxf(b->maxx, b->maxx + ex), maxf(b->maxy, b->maxy + ey))) continue;
                bld_shadow(b, dx, dy, &m);
            }
        }
    float tr2 = minf(R, 55.0f) * minf(R, 55.0f), z = ROAD_Z + 0.005f;
    float dl = sqrtf_(dx * dx + dy * dy) + 1e-6f, ux = dx / dl, uy = dy / dl, el = 1 / sz;
    for (int by = by0; by <= by1; by++)
        for (int bx = bx0; bx <= bx1; bx++) {
            Bucket *k = &bk[3][by * BW + bx];
            for (u32 i = 0; i < k->n; i++) {
                Palm *p = &palms[bl[3][k->first + i]];
                float ddx = p->x - cam.x, ddy = p->y - cam.y;
                if (ddx * ddx + ddy * ddy > tr2) continue;
                float zc = p->kind == 2 ? p->h * 0.66f : p->h, rr = p->kind == 2 ? p->r * 0.92f : 0.9f * clampf(0.75f + p->h * 0.08f, 0.8f, 1.3f);
                float cx = p->x + dx * zc, cy = p->y + dy * zc;
                float re = rr * el;
                if (!visible(minf(cx, p->x) - re, minf(cy, p->y) - re, maxf(cx, p->x) + re, maxf(cy, p->y) + re)) continue;
                V3 c0 = v3(cx, cy, z), pv = v3(cx + ux * rr * el, cy + uy * rr * el, z);
                for (int s = 1; s <= 8; s++) {
                    float a = s * (TAU / 8);
                    float w = rr * (0.8f + 0.2f * (hash2((int)p->seed & 255, s) & 255) / 255.0f);
                    float pa = cosf_(a) * w * el, pb = sinf_(a) * w;
                    V3 nv = v3(cx + ux * pa - uy * pb, cy + uy * pa + ux * pb, z);
                    tri(c0, pv, nv, 0, 0, 1, 0, 1, 1, &m);
                    pv = nv;
                }
                float tw = p->kind == 2 ? 0.11f : 0.07f, tl = maxf(0, dl * zc - rr * el * 0.7f);
                quad(v3(p->x - uy * tw, p->y + ux * tw, z), v3(p->x + uy * tw, p->y - ux * tw, z),
                     v3(p->x + ux * tl + uy * tw, p->y + uy * tl - ux * tw, z), v3(p->x + ux * tl - uy * tw, p->y + uy * tl + ux * tw, z), 1, 1, &m);
            }
        }
    for (int i = 0; i < n_beach; i++) {
        Beach *b = &beach[i];
        float ddx = b->x - cam.x, ddy = b->y - cam.y;
        if (ddx * ddx + ddy * ddy > tr2) continue;
        float hz = b->kind ? 0.68f : 0.85f, cx = b->x + dx * hz, cy = b->y + dy * hz, rr = b->kind ? 0.55f : 0.5f;
        if (!visible(minf(cx, b->x) - rr * el, minf(cy, b->y) - rr * el, maxf(cx, b->x) + rr * el, maxf(cy, b->y) + rr * el)) continue;
        if (b->kind) {
            V3 c0 = v3(cx, cy, z), pv = v3(cx + ux * rr * el, cy + uy * rr * el, z);
            for (int s = 1; s <= 10; s++) {
                float a = s * (TAU / 10), pa = cosf_(a) * rr * el, pb = sinf_(a) * rr;
                V3 nv = v3(cx + ux * pa - uy * pb, cy + uy * pa + ux * pb, z);
                tri(c0, pv, nv, 0, 0, 1, 0, 1, 1, &m);
                pv = nv;
            }
        } else {
            float ca = cosf_(b->a), sa = sinf_(b->a);
            V3 q[4];
            for (int k = 0; k < 4; k++) {
                float lx = (k == 1 || k == 2) ? 0.42f : -0.52f, ly = k >= 2 ? 0.52f : -0.52f;
                q[k] = v3(cx + lx * ca - ly * sa, cy + lx * sa + ly * ca, z);
            }
            quad(q[0], q[1], q[2], q[3], 1, 1, &m);
        }
    }
}

static void draw_poles(void) {
    float R2 = (fog_far * 0.7f) * (fog_far * 0.7f);
    int bx0 = maxi(0, (int)((cam.x - fog_far) / BK)), bx1 = mini(BW - 1, (int)((cam.x + fog_far) / BK));
    int by0 = maxi(0, (int)((cam.y - fog_far) / BK)), by1 = mini(BH - 1, (int)((cam.y + fog_far) / BK));
    for (int by = by0; by <= by1; by++)
        for (int bx = bx0; bx <= bx1; bx++) {
            Bucket *k = &bk[4][by * BW + bx];
            for (u32 i = 0; i < k->n; i++) {
                Pole *p = &poles[bl[4][k->first + i]];
                float dx = p->x - cam.x, dy = p->y - cam.y;
                if (dx * dx + dy * dy > R2 || dx * cam_f.x + dy * cam_f.y < -2) continue;
                if (p->tilt > 0) {
                    place(v3(p->x, p->y, 0), p->fa);
                    m_pc = cosf_(p->tilt); m_ps = sinf_(p->tilt);
                    m_qc = cosf_(p->a - p->fa); m_qs = sinf_(p->a - p->fa);
                } else place(v3(p->x, p->y, 0), p->a);
                if (p->kind == 0) {
                    mbox(v3(-0.04f, -0.04f, 0), v3(0.04f, 0.04f, 2.6f), t_white, 0x8a8a96);
                    mbox(v3(0, -0.03f, 2.52f), v3(0.75f, 0.03f, 2.58f), t_white, 0x8a8a96);
                    mbox(v3(0.62f, -0.08f, 2.44f), v3(0.86f, 0.08f, 2.54f), t_dark, 0x707078);
                    Mat gl = {neon_tex(0xffe0a0), 0x808080, 1, 0, 0, 0};
                    quad(mx(v3(0.64f, -0.06f, 2.435f)), mx(v3(0.84f, -0.06f, 2.435f)), mx(v3(0.84f, 0.06f, 2.435f)), mx(v3(0.64f, 0.06f, 2.435f)), 1, 1, &gl);
                } else {
                    mbox(v3(-0.05f, -0.05f, 0), v3(0.05f, 0.05f, 2.0f), t_white, 0x6a6a74);
                    mbox(v3(-0.09f, -0.09f, 1.55f), v3(0.09f, 0.09f, 2.05f), t_dark, 0x303034);
                    u32 cols[3] = {0xff2a2a, 0xffc020, 0x30ff60};
                    for (int g = 0; g < 2; g++) {
                        int st = light_state(p->kind - 1, g, tick_now);
                        for (int l = 0; l < 3; l++) {
                            int on = l == (st == 0 ? 2 : st == 1 ? 1 : 0);
                            Mat lm = {on ? neon_tex(cols[l]) : t_dark, on ? 0x808080 : light(cols[l] >> 2 & 0x3f3f3f, v3(1, 0, 0)), 1, 0, 0, 0};
                            float z0 = 1.92f - l * 0.14f;
                            for (int sd = -1; sd <= 1; sd += 2) {
                                float o = sd * 0.092f;
                                if (!g) quad(mx(v3(o, -0.05f, z0 - 0.1f)), mx(v3(o, 0.05f, z0 - 0.1f)), mx(v3(o, 0.05f, z0)), mx(v3(o, -0.05f, z0)), 1, 1, &lm);
                                else quad(mx(v3(-0.05f, o, z0 - 0.1f)), mx(v3(0.05f, o, z0 - 0.1f)), mx(v3(0.05f, o, z0)), mx(v3(-0.05f, o, z0)), 1, 1, &lm);
                            }
                        }
                    }
                }
            }
        }
}

static int sea_ok;
static int solid_walls(float x, float y) {
    int c = cell(x, y);
    if (x < 1 || y < 1 || x > WX - 1 || y > WY - 1) return 1;
    if (c & C_BLD) return 1;
    if ((c & 3) == G_SEA && !(c & C_ROAD) && !sea_ok) return 1;
    return 0;
}
static int solid(float x, float y) {
    int c = cell(x, y);
    if (x < 1 || y < 1 || x > WX - 1 || y > WY - 1) return 1;
    if (c & (C_BLD | C_TREE)) return 1;
    if ((c & 3) == G_SEA && !(c & C_ROAD) && !sea_ok) return 1;
    return 0;
}

static float ground_z(float x, float y) { return cell(x, y) & C_ROAD ? ROAD_Z : 0; }

// 0 sea 1 road 2 sidewalk 3 building 4 sand 5 grass
static int ground_kind(float x, float y) {
    int c = cell(x, y);
    if (c & C_BLD) return 3;
    if (c & C_ROAD) return 1;
    int g = c & 3;
    return g == G_SEA ? 0 : g == G_PARK ? 5 : g == G_SAND ? 4 : 2;
}
