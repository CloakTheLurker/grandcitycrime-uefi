typedef struct { const u8 *in; u32 range, code; } Rc;

// each bit is a guess and the guess learns
static int rc_bit(Rc *rc, u16 *p) {
    u32 bound = (rc->range >> 11) * *p;
    int b;
    if (rc->code < bound) { *p += (u16)((2048 - *p) >> 5); rc->range = bound; b = 0; }
    else { *p -= (u16)(*p >> 5); rc->code -= bound; rc->range -= bound; b = 1; }
    if (rc->range < (1u << 24)) { rc->range <<= 8; rc->code = rc->code << 8 | *rc->in++; }
    return b;
}

static u32 rc_direct(Rc *rc, int n) {
    u32 res = 0;
    while (n--) {
        rc->range >>= 1;
        rc->code -= rc->range;
        u32 t = 0 - (rc->code >> 31);
        rc->code += rc->range & t;
        if (rc->range < (1u << 24)) { rc->range <<= 8; rc->code = rc->code << 8 | *rc->in++; }
        res = (res << 1) + t + 1;
    }
    return res;
}

static u32 rc_tree(Rc *rc, u16 *p, int bits) {
    u32 m = 1;
    for (int i = 0; i < bits; i++) m = (m << 1) + (u32)rc_bit(rc, &p[m]);
    return m - (1u << bits);
}

static u32 rc_rtree(Rc *rc, u16 *p, int bits) {
    u32 m = 1, sym = 0;
    for (int i = 0; i < bits; i++) {
        u32 b = (u32)rc_bit(rc, &p[m]);
        m = (m << 1) + b;
        sym |= b << i;
    }
    return sym;
}

typedef struct { u16 choice, choice2, low[16][8], mid[16][8], high[256]; } LenDec;

static u32 len_decode(Rc *rc, LenDec *d, u32 ps) {
    if (!rc_bit(rc, &d->choice)) return rc_tree(rc, d->low[ps], 3);
    if (!rc_bit(rc, &d->choice2)) return 8 + rc_tree(rc, d->mid[ps], 3);
    return 16 + rc_tree(rc, d->high, 8);
}

typedef struct {
    u16 is_match[192], is_rep[12], g0[12], g1[12], g2[12], rep0_long[192];
    u16 slot[4][64], pos[115], align[16];
    LenDec len, rep_len;
} LzState;

static void fill16(u16 *p, usize n) { while (n--) *p++ = 1024; }

static u8 *unlzma(const u8 *src, u32 *out_size) {
    u32 size = src[0] | src[1] << 8 | src[2] << 16 | (u32)src[3] << 24;
    const u8 *h = src + 4;
    int d = h[0], lc = d % 9, lp = (d / 9) % 5, pb = d / 45;
    u8 *out = (u8 *)pool(size + 16);
    LzState *s = (LzState *)pool(sizeof(LzState));
    fill16((u16 *)s, sizeof(LzState) / 2);
    usize nlit = (usize)0x300 << (lc + lp);
    u16 *lit = (u16 *)pool(nlit * 2);
    fill16(lit, nlit);
    Rc rc = {h + 13 + 1, 0xffffffffu, 0};
    for (int i = 0; i < 4; i++) rc.code = rc.code << 8 | *rc.in++;
    u32 state = 0, r0 = 0, r1 = 0, r2 = 0, r3 = 0, n = 0;
    u32 pbm = (1u << pb) - 1, lpm = (1u << lp) - 1;
    while (n < size) {
        u32 ps = n & pbm;
        if (!rc_bit(&rc, &s->is_match[(state << 4) + ps])) {
            u32 prev = n ? out[n - 1] : 0;
            u16 *p = &lit[0x300 * (((n & lpm) << lc) + (prev >> (8 - lc)))];
            u32 sym = 1;
            if (state >= 7) {
                u32 mb = out[n - r0 - 1];
                do {
                    u32 mbit = (mb >> 7) & 1;
                    mb <<= 1;
                    u32 b = (u32)rc_bit(&rc, &p[((1 + mbit) << 8) + sym]);
                    sym = (sym << 1) | b;
                    if (mbit != b) break;
                } while (sym < 0x100);
            }
            while (sym < 0x100) sym = (sym << 1) | (u32)rc_bit(&rc, &p[sym]);
            out[n++] = (u8)(sym - 0x100);
            state = state < 4 ? 0 : state < 10 ? state - 3 : state - 6;
            continue;
        }
        u32 len;
        if (rc_bit(&rc, &s->is_rep[state])) {
            if (!rc_bit(&rc, &s->g0[state])) {
                if (!rc_bit(&rc, &s->rep0_long[(state << 4) + ps])) {
                    state = state < 7 ? 9 : 11;
                    out[n] = out[n - r0 - 1];
                    n++;
                    continue;
                }
            } else {
                u32 dist;
                if (!rc_bit(&rc, &s->g1[state])) dist = r1;
                else {
                    if (!rc_bit(&rc, &s->g2[state])) dist = r2;
                    else { dist = r3; r3 = r2; }
                    r2 = r1;
                }
                r1 = r0; r0 = dist;
            }
            len = len_decode(&rc, &s->rep_len, ps);
            state = state < 7 ? 8 : 11;
        } else {
            r3 = r2; r2 = r1; r1 = r0;
            len = len_decode(&rc, &s->len, ps);
            state = state < 7 ? 7 : 10;
            u32 ls = len < 3 ? len : 3;
            u32 slot = rc_tree(&rc, s->slot[ls], 6);
            if (slot < 4) r0 = slot;
            else {
                int nd = (int)(slot >> 1) - 1;
                u32 dist = (2 | (slot & 1)) << nd;
                if (slot < 14) dist += rc_rtree(&rc, &s->pos[dist - slot], nd);
                else { dist += rc_direct(&rc, nd - 4) << 4; dist += rc_rtree(&rc, s->align, 4); }
                r0 = dist;
            }
            if (r0 == 0xffffffffu) break;
        }
        len += 2;
        while (len-- && n < size) { out[n] = out[n - r0 - 1]; n++; }
    }
    *out_size = size;
    return out;
}
