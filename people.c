__asm__(".section .rdata,\"dr\"\n.p2align 4\n.globl pplz\npplz:\n.incbin \"people.lz\"\n.text\n");
extern const u8 pplz[];

#define NB 19
enum { B_PEL, B_SPI, B_CHE, B_NEC, B_HEA, B_UAL, B_LAL, B_HAL, B_FIL, B_UAR, B_LAR, B_HAR, B_FIR, B_THL, B_CAL, B_FOL, B_THR, B_CAR, B_FOR };
static const int hpar[NB] = {-1, 0, 1, 2, 3, 2, 5, 6, 7, 2, 9, 10, 11, 0, 13, 14, 0, 16, 17};

typedef struct { float x, y, z, nx, ny, nz, u, v, w; u8 b0, b1; } HV;
typedef struct { int nv, nt; HV *v; u16 *t; } HLod;
#define NVAR 8
typedef struct { Tex *tex[NVAR]; V3 j[NB]; HLod lod[3]; float adl, adr, fixl[9], fixr[9]; V3 kl, kr; u32 far[NVAR][3]; float tall; } Human;
#define NHUM 9
static Human humans[NHUM];
static V3 *hv_pos;
static float *hv_lum;

static float rdf(const u8 *p) { union { u32 i; float f; } u; u.i = p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; return u.f; }

static void axis_rot(float *m, V3 a, float t) {
    float c = cosf_(t), s = sinf_(t), k = 1 - c;
    m[0] = c + a.x * a.x * k; m[1] = a.x * a.y * k - a.z * s; m[2] = a.x * a.z * k + a.y * s;
    m[3] = a.y * a.x * k + a.z * s; m[4] = c + a.y * a.y * k; m[5] = a.y * a.z * k - a.x * s;
    m[6] = a.z * a.x * k - a.y * s; m[7] = a.z * a.y * k + a.x * s; m[8] = c + a.z * a.z * k;
}

static void straighten(float *m, V3 up, V3 fo) {
    V3 u = norm(up), f = norm(fo), n = cross(f, u);
    float s = sqrtf_(dot(n, n)), c = dot(u, f);
    if (s < 1e-4f) { axis_rot(m, v3(0, 0, 1), 0); return; }
    axis_rot(m, scl(n, 1 / s), atan2f_(s, c));
}

static const u32 shirt_cols[8] = {0xc83a3a, 0xe8e4dc, 0x26262a, 0x3a8a4a, 0xe0b030, 0xd860a0, 0x8a8a92, 0x6a3ac8};
static const u32 pant_cols[4] = {0x2a3e66, 0x222226, 0xb09a70, 0x5a5a64};

static void recolour(Tex *dst, Tex *src, int h, int vv) {
    u32 sc = shirt_cols[(h * 3 + vv * 5) & 7], pc = pant_cols[(h + vv) & 3];
    for (int i = 0; i < 256 * 256; i++) {
        u32 c = src->lv[0][i];
        int x = i & 255, y = i >> 8;
        dst->lv[0][i] = c;
        if (x < 128 || y >= 64 || (c >> 24) == 1) continue;
        int r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
        int mx_ = maxi(r, maxi(g, b)), mn_ = mini(r, mini(g, b));
        if (mx_ < 20 || b < r + 25 || (mx_ - mn_) * 100 < mx_ * 22) continue;
        float l = (r * 0.3f + g * 0.59f + b * 0.11f) / 120.0f;
        u32 t = y < 30 ? sc : pc;
        dst->lv[0][i] = rgb((int)((t >> 16 & 255) * l), (int)((t >> 8 & 255) * l), (int)((t & 255) * l));
    }
}

static void load_people(void) {
    u32 sz;
    u8 *b = unlzma(pplz, &sz);
    const u8 *p = b + 8;
    hv_pos = (V3 *)pool(4096 * sizeof(V3));
    hv_lum = (float *)pool(4096 * 4);
    for (int h = 0; h < NHUM; h++) {
        Human *H = &humans[h];
        const u8 *pal = p; p += 768;
        const u8 *idx = p; p += 256 * 128;
        Tex *t = new_tex(8);
        u32 *px = t->lv[0];
        for (int y = 0; y < 256; y++)
            for (int x = 0; x < 256; x++) {
                int i = idx[(y & 127) * 256 + x];
                px[y * 256 + x] = i ? rgb(pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]) : 0x01000000u;
            }
        for (int pass = 0; pass < 4; pass++)
            for (int y = 0; y < 256; y++)
                for (int x = 0; x < 256; x++) {
                    u32 c = px[y * 256 + x];
                    if (c != 0x01000000u) continue;
                    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
                    for (int k = 0; k < 4; k++) {
                        u32 n = px[((y + dy[k]) & 255) * 256 + ((x + dx[k]) & 255)];
                        if ((n >> 24) == 0) { px[y * 256 + x] = 0x01000000u | (n & 0xffffff) | 0x02000000u; break; }
                    }
                }
        for (int i = 0; i < 256 * 256; i++) if ((px[i] >> 24) >= 1) px[i] = 0x01000000u | (px[i] & 0xffffff);
        for (int vv = 0; vv < NVAR; vv++) {
            Tex *tv = vv ? new_tex(8) : t;
            if (vv) recolour(tv, t, h, vv);
            make_mips(tv);
            for (int l = 1; l < tv->levels; l++) {
                int n = 1 << (2 * tv->bits[l]);
                for (int i = 0; i < n; i++) tv->lv[l][i] &= 0xffffff;
            }
            H->tex[vv] = tv;
        }
        for (int k = 0; k < NB; k++) { H->j[k] = v3(rdf(p), rdf(p + 4), rdf(p + 8)); p += 12; }
        for (int l = 0; l < 3; l++) {
            HLod *L = &H->lod[l];
            L->nv = p[0] | p[1] << 8; L->nt = p[2] | p[3] << 8; p += 4;
            L->v = (HV *)pool((usize)L->nv * sizeof(HV));
            L->t = (u16 *)pool((usize)L->nt * 6);
            for (int i = 0; i < L->nv; i++) {
                HV *v = &L->v[i];
                v->x = (short)(p[0] | p[1] << 8) / 16384.0f;
                v->y = (short)(p[2] | p[3] << 8) / 16384.0f;
                v->z = (short)(p[4] | p[5] << 8) / 16384.0f;
                v->u = (p[6] + 0.5f) / 256.0f;
                v->v = (p[7] + 0.5f) / 512.0f;
                v->b0 = p[8]; v->b1 = p[9]; v->w = p[10] / 255.0f;
                v->nx = v->ny = v->nz = 0;
                p += 11;
            }
            for (int i = 0; i < L->nt * 3; i++) { L->t[i] = (u16)(p[0] | p[1] << 8); p += 2; }
            for (int i = 0; i < L->nt; i++) {
                HV *a = &L->v[L->t[i * 3] & 0x7fff], *bb = &L->v[L->t[i * 3 + 1]], *c = &L->v[L->t[i * 3 + 2]];
                V3 n = cross(v3(bb->x - a->x, bb->y - a->y, bb->z - a->z), v3(c->x - a->x, c->y - a->y, c->z - a->z));
                HV *vs[3] = {a, bb, c};
                for (int k = 0; k < 3; k++) { vs[k]->nx += n.x; vs[k]->ny += n.y; vs[k]->nz += n.z; }
            }
            for (int i = 0; i < L->nv; i++) {
                HV *v = &L->v[i];
                V3 n = norm(v3(v->nx, v->ny, v->nz));
                v->nx = n.x; v->ny = n.y; v->nz = n.z;
            }
        }
        {
            HLod *L = &H->lod[2];
            float zt = 0;
            for (int i = 0; i < L->nv; i++) zt = maxf(zt, L->v[i].z);
            H->tall = zt;
            for (int vv = 0; vv < NVAR; vv++) {
                u32 sum[3][4] = {{0}};
                for (int i = 0; i < L->nv; i++) {
                    HV *v = &L->v[i];
                    float f = v->z / zt;
                    int band = f < 0.46f ? 0 : f < 0.8f ? 1 : f > 0.86f ? 2 : -1;
                    if (band < 0) continue;
                    u32 c = H->tex[vv]->lv[0][((int)(v->v * 256) & 255) * 256 + ((int)(v->u * 256) & 255)];
                    sum[band][0] += c >> 16 & 255; sum[band][1] += c >> 8 & 255; sum[band][2] += c & 255; sum[band][3]++;
                }
                for (int b = 0; b < 3; b++) {
                    u32 n = sum[b][3] ? sum[b][3] : 1;
                    H->far[vv][b] = rgb((int)(sum[b][0] / n), (int)(sum[b][1] / n), (int)(sum[b][2] / n));
                }
            }
        }
        V3 al = sub(H->j[B_LAL], H->j[B_UAL]), ar = sub(H->j[B_LAR], H->j[B_UAR]);
        H->adl = atan2f_(al.y, -al.z);
        H->adr = atan2f_(-ar.y, -ar.z);
        // rest pose elbows bend 50 deg so undo that first
        straighten(H->fixl, al, sub(H->j[B_HAL], H->j[B_LAL]));
        straighten(H->fixr, ar, sub(H->j[B_HAR], H->j[B_LAR]));
        H->kl = norm(cross(norm(al), v3(1, 0, 0)));
        H->kr = norm(cross(norm(ar), v3(1, 0, 0)));
    }
}

typedef struct { float a[NB][3]; V3 root; } Pose;

static void rot3(float *m, float ax, float ay, float az) {
    float cx = cosf_(ax), sx = sinf_(ax), cy = cosf_(ay), sy = sinf_(ay), cz = cosf_(az), sz = sinf_(az);
    m[0] = cz * cy; m[1] = cz * sy * sx - sz * cx; m[2] = cz * sy * cx + sz * sx;
    m[3] = sz * cy; m[4] = sz * sy * sx + cz * cx; m[5] = sz * sy * cx - cz * sx;
    m[6] = -sy;     m[7] = cy * sx;                m[8] = cy * cx;
}

static void mul3(float *o, const float *a, const float *b) {
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) o[r * 3 + c] = a[r * 3] * b[c] + a[r * 3 + 1] * b[3 + c] + a[r * 3 + 2] * b[6 + c];
}

static V3 app3(const float *m, V3 v) { return v3(m[0] * v.x + m[1] * v.y + m[2] * v.z, m[3] * v.x + m[4] * v.y + m[5] * v.z, m[6] * v.x + m[7] * v.y + m[8] * v.z); }

static void pose_build(Human *H, Pose *P, int pose, float ph, float hyaw, int t) {
    int aimrun = pose == PO_AIMRUN;
    if (aimrun) pose = PO_RUN;
    for (int b = 0; b < NB; b++) P->a[b][0] = P->a[b][1] = P->a[b][2] = 0;
    P->root = v3(0, 0, 0);
    float s = sinf_(ph), c = cosf_(ph), br = sinf_(t * 0.05f);
    float AL = -H->adl * 0.9f, AR = H->adr * 0.9f;
    float (*a)[3] = P->a;
    a[B_UAL][0] = AL; a[B_UAR][0] = AR;
    a[B_LAL][1] = -0.15f; a[B_LAR][1] = -0.15f;
    a[B_FIL][1] = -0.35f; a[B_FIR][1] = -0.35f;
    a[B_CHE][1] = 0.012f * br;
    if (pose == PO_WALK || pose == PO_RUN) {
        int run = pose == PO_RUN;
        float th = run ? 0.75f : 0.42f, ar = run ? 0.75f : 0.32f;
        a[B_THL][1] = -th * s; a[B_THR][1] = th * s;
        a[B_CAL][1] = (run ? 0.3f : 0.08f) + (run ? 1.4f : 0.65f) * maxf(0, c);
        a[B_CAR][1] = (run ? 0.3f : 0.08f) + (run ? 1.4f : 0.65f) * maxf(0, -c);
        if (run) {
            float kl = maxf(0, cosf_(ph - 0.7f)), kr = maxf(0, -cosf_(ph - 0.7f));
            a[B_THL][1] = 0.12f - 0.78f * s - 0.18f * kl; a[B_THR][1] = 0.12f + 0.78f * s - 0.18f * kr;
            a[B_CAL][1] = 0.22f + 1.75f * kl * kl; a[B_CAR][1] = 0.22f + 1.75f * kr * kr;
        }
        a[B_FOL][1] = -(a[B_THL][1] + a[B_CAL][1]) * 0.55f;
        a[B_FOR][1] = -(a[B_THR][1] + a[B_CAR][1]) * 0.55f;
        a[B_UAL][1] = ar * s; a[B_UAR][1] = -ar * s;
        a[B_LAL][1] = -(run ? 1.5f : 0.25f) - 0.2f * maxf(0, -s);
        a[B_LAR][1] = -(run ? 1.5f : 0.25f) - 0.2f * maxf(0, s);
        a[B_PEL][2] = 0.07f * s; a[B_CHE][2] = -0.11f * s;
        a[B_SPI][1] = run ? 0.18f : 0.03f;
        P->root.z = run ? 0.016f * absf(sinf_(ph)) - 0.022f : -0.006f * cosf_(2 * ph);
        if (run) {
            a[B_FIL][1] = a[B_FIR][1] = -1.2f;
            a[B_UAL][1] = 0.5f * s + 0.2f; a[B_UAR][1] = -0.5f * s + 0.2f;
            a[B_LAL][1] = -1.2f - 0.3f * maxf(0, -s); a[B_LAR][1] = -1.2f - 0.3f * maxf(0, s);
            a[B_SPI][1] = 0.26f;
            a[B_THL][1] *= 0.85f; a[B_THR][1] *= 0.85f;
        }
    } else if (pose == PO_PHONE) {
        a[B_UAR][0] = AR * 0.55f; a[B_UAR][1] = -0.45f; a[B_UAR][2] = 0.35f;
        a[B_LAR][1] = -2.35f;
        a[B_FIR][1] = -0.9f;
        a[B_HEA][0] = -0.12f;
        a[B_UAL][1] = 0.05f * br;
    } else if (pose == PO_TEXT) {
        a[B_UAL][1] = a[B_UAR][1] = -0.25f;
        a[B_UAL][0] = AL * 0.6f; a[B_UAR][0] = AR * 0.6f;
        a[B_LAL][1] = a[B_LAR][1] = -1.35f;
        a[B_LAL][2] = -0.35f; a[B_LAR][2] = 0.35f;
        a[B_HEA][1] = 0.45f; a[B_NEC][1] = 0.15f;
    } else if (pose == PO_CHAT) {
        a[B_UAR][1] = -0.45f - 0.22f * sinf_(ph * 3);
        a[B_LAR][1] = -1.0f - 0.35f * sinf_(ph * 2.2f);
        a[B_UAL][1] = -0.15f; a[B_LAL][1] = -0.5f;
        a[B_HEA][2] = 0.1f * sinf_(ph * 1.3f);
    } else if (pose == PO_HANDSUP) {
        a[B_UAL][0] = AL * 0.2f; a[B_UAR][0] = AR * 0.2f;
        a[B_UAL][1] = a[B_UAR][1] = -2.7f;
        a[B_LAL][1] = a[B_LAR][1] = -0.5f;
        a[B_FIL][1] = a[B_FIR][1] = 0.1f;
        a[B_SPI][1] = -0.05f;
    } else if (pose == PO_COWER) {
        a[B_THL][1] = a[B_THR][1] = -1.65f;
        a[B_CAL][1] = a[B_CAR][1] = 2.3f;
        a[B_FOL][1] = a[B_FOR][1] = -0.55f;
        a[B_SPI][1] = 0.55f; a[B_CHE][1] = 0.25f; a[B_HEA][1] = 0.35f;
        a[B_UAL][1] = a[B_UAR][1] = -2.4f;
        a[B_LAL][1] = a[B_LAR][1] = -2.1f;
        P->root.z = -0.17f + 0.004f * sinf_(ph * 3); P->root.x = -0.04f;
    } else if (pose == PO_ANGRY) {
        a[B_UAR][1] = -0.95f + 0.35f * sinf_(ph * 3); a[B_LAR][1] = -0.9f;
        a[B_UAL][1] = -0.3f; a[B_LAL][1] = -0.6f;
        a[B_SPI][1] = 0.1f; a[B_FIR][1] = -0.2f;
        a[B_HEA][1] = 0.05f * sinf_(ph * 6);
    } else if (pose == PO_PUNCH) {
        a[B_UAR][0] = AR * 0.25f; a[B_UAR][1] = -1.55f; a[B_LAR][1] = -0.1f;
        a[B_UAL][1] = -1.0f; a[B_LAL][1] = -1.9f;
        a[B_CHE][2] = 0.35f; a[B_FIL][1] = a[B_FIR][1] = -1.5f;
        a[B_THL][1] = -0.3f; a[B_THR][1] = 0.2f;
    } else if (pose == PO_SHOCK) {
        a[B_UAL][1] = a[B_UAR][1] = -0.9f;
        a[B_LAL][1] = a[B_LAR][1] = -1.9f;
        a[B_SPI][1] = -0.12f; a[B_HEA][1] = -0.1f;
    } else if (pose == PO_AIM) {
        a[B_UAR][0] = AR; a[B_UAR][1] = -1.5f; a[B_UAR][2] = 0.22f; a[B_LAR][1] = -0.05f;
        a[B_UAL][0] = AL; a[B_UAL][1] = -1.35f; a[B_UAL][2] = -0.6f; a[B_LAL][1] = -0.45f;
        a[B_FIR][1] = -1.3f; a[B_CHE][2] = 0.12f;
    } else if (pose == PO_DEAD) {
        a[B_PEL][1] = -1.5f;
        a[B_UAL][0] = AL * 0.3f; a[B_UAR][0] = AR * 0.3f;
        a[B_LAL][1] = -0.3f; a[B_LAR][1] = -0.6f;
        a[B_THL][0] = 0.12f; a[B_THR][0] = -0.08f; a[B_CAL][1] = 0.3f;
        a[B_HEA][2] = 0.4f;
        P->root = v3(0.0f, 0, 0.05f - H->j[B_PEL].z);
    } else {
        a[B_UAL][1] = 0.04f * br; a[B_UAR][1] = -0.04f * br;
        a[B_PEL][1] = 0.01f * br;
    }
    if (aimrun) {
        a[B_UAR][0] = AR; a[B_UAR][1] = -1.5f; a[B_UAR][2] = 0.22f; a[B_LAR][1] = -0.05f;
        a[B_UAL][0] = AL; a[B_UAL][1] = -1.35f; a[B_UAL][2] = -0.6f; a[B_LAL][1] = -0.45f;
        a[B_FIR][1] = -1.3f; a[B_CHE][2] = 0.12f;
        a[B_PEL][2] *= 0.4f; a[B_SPI][1] = 0.1f;
    }
    a[B_HEA][2] += hyaw * 0.7f;
    a[B_NEC][2] += hyaw * 0.3f;
}

static void draw_human(u32 which, V3 at, float yaw, int pose, float ph, float hyaw, float dist, int gun) {
    Human *H = &humans[which % NHUM];
    Tex *tx = H->tex[(which >> 8) % NVAR];
    HLod *L = &H->lod[dist < 6 ? 0 : dist < 14 ? 1 : 2];
    Pose P;
    pose_build(H, &P, pose, ph, hyaw, tick_now);
    float R[NB][9];
    V3 T[NB];
    for (int b = 0; b < NB; b++) {
        float l[9];
        if (b == B_LAL || b == B_LAR) {
            float e[9];
            axis_rot(e, b == B_LAL ? H->kl : H->kr, -P.a[b][1]);
            mul3(l, e, b == B_LAL ? H->fixl : H->fixr);
        } else rot3(l, P.a[b][0], P.a[b][1], P.a[b][2]);
        if (b == 0) { for (int k = 0; k < 9; k++) R[0][k] = l[k]; T[0] = add(H->j[0], P.root); }
        else {
            int q = hpar[b];
            mul3(R[b], R[q], l);
            T[b] = add(T[q], app3(R[q], sub(H->j[b], H->j[q])));
        }
    }
    place(at, yaw);
    for (int i = 0; i < L->nv; i++) {
        HV *v = &L->v[i];
        V3 p = v3(v->x, v->y, v->z);
        // each skin point follows up to 2 bones
        V3 a = add(T[v->b0], app3(R[v->b0], sub(p, H->j[v->b0])));
        if (v->w < 0.999f) {
            V3 bb = add(T[v->b1], app3(R[v->b1], sub(p, H->j[v->b1])));
            a = add(scl(a, v->w), scl(bb, 1 - v->w));
        }
        hv_pos[i] = mx(a);
        hv_lum[i] = lumf(mn(app3(R[v->b0], v3(v->nx, v->ny, v->nz))));
    }
    u32 tint = base_tint(0xffffff);
    Mat m1 = {tx, tint, 0, 0, 1, 0, 0, 0, 1}, m2 = {tx, tint, 1, 0, 1, 0, 0, 0, 1};
    for (int i = 0; i < L->nt; i++) {
        u16 *t = &L->t[i * 3];
        int ia = t[0] & 0x7fff, ib = t[1], ic = t[2];
        HV *va = &L->v[ia], *vb = &L->v[ib], *vc = &L->v[ic];
        tri_full(hv_pos[ia], hv_pos[ib], hv_pos[ic], va->u, va->v, vb->u, vb->v, vc->u, vc->v, hv_lum[ia], hv_lum[ib], hv_lum[ic], (t[0] & 0x8000) ? &m2 : &m1);
    }
    if (gun) {
        V3 h = T[B_HAR], f = T[B_FIR];
        V3 d = norm(sub(f, h));
        V3 g0 = add(f, scl(d, 0.005f)), g1 = add(g0, scl(d, 0.07f));
        Mat gm = {t_dark, 0x383838, 0, 0, 0};
        V3 up = v3(0, 0, 0.012f), sd = scl(norm(cross(d, v3(0, 0, 1))), 0.01f);
        quad(mx(add(g0, up)), mx(add(g1, up)), mx(sub(g1, up)), mx(sub(g0, up)), 1, 1, &gm);
        quad(mx(add(g0, sd)), mx(add(g1, sd)), mx(sub(g1, sd)), mx(sub(g0, sd)), 1, 1, &gm);
    }
    if (pose == PO_DEAD) {
        Mat bl = {t_pool, 0x4a060c, 1, 0, 0, 0, 2, 235};
        float r = 0.34f;
        quad(v3(at.x - r, at.y - r, at.z + 0.016f), v3(at.x + r, at.y - r, at.z + 0.016f), v3(at.x + r, at.y + r, at.z + 0.016f), v3(at.x - r, at.y + r, at.z + 0.016f), 1, 1, &bl);
    }
}

// far people are flat cutouts nobody can tell lol
static void draw_far_human(u32 which, V3 at, float yaw, int pose, float ph) {
    Human *H = &humans[which % NHUM];
    u32 *col = H->far[(which >> 8) % NVAR];
    float t = H->tall, sw = (pose == PO_WALK || pose == PO_RUN) ? sinf_(ph) * t * (pose == PO_RUN ? 0.2f : 0.12f) : 0;
    V3 r = v3(cam_r.x, cam_r.y, 0);
    float rl = sqrtf_(r.x * r.x + r.y * r.y) + 1e-6f;
    r = scl(r, 1 / rl);
    V3 fw = v3(cosf_(yaw), sinf_(yaw), 0);
    float side = dot(fw, r);
    V3 nrm = v3(-cam_f.x, -cam_f.y, 0.3f);
    Mat leg = {t_white, light(col[0], norm(nrm)), 1, 0}, body = {t_white, light(col[1], norm(nrm)), 1, 0}, head = {t_white, light(col[2], norm(nrm)), 1, 0};
    if (pose == PO_DEAD) {
        V3 a = add(at, scl(r, -t * 0.5f)), b = add(at, scl(r, t * 0.5f)), up = v3(0, 0, t * 0.08f);
        quad(a, b, add(b, up), add(a, up), 1, 1, &body);
        return;
    }
    float hip = t * 0.47f, sh = t * 0.82f, hw = t * 0.1f;
    for (int k = -1; k <= 1; k += 2) {
        float s0 = k * hw * 0.5f, foot = s0 + k * sw * side;
        V3 a = add(at, scl(r, foot - hw * 0.42f)), b = add(at, scl(r, foot + hw * 0.42f));
        V3 c = add(at, add(scl(r, s0 + hw * 0.5f), v3(0, 0, hip))), d = add(at, add(scl(r, s0 - hw * 0.5f), v3(0, 0, hip)));
        quad(a, b, c, d, 1, 1, &leg);
    }
    V3 a = add(at, add(scl(r, -hw * 1.1f), v3(0, 0, hip))), b = add(at, add(scl(r, hw * 1.1f), v3(0, 0, hip)));
    V3 c = add(at, add(scl(r, hw * 1.25f), v3(0, 0, sh))), d = add(at, add(scl(r, -hw * 1.25f), v3(0, 0, sh)));
    quad(a, b, c, d, 1, 1, &body);
    a = add(at, add(scl(r, -hw * 0.45f), v3(0, 0, sh))); b = add(at, add(scl(r, hw * 0.45f), v3(0, 0, sh)));
    c = add(at, add(scl(r, hw * 0.45f), v3(0, 0, t))); d = add(at, add(scl(r, -hw * 0.45f), v3(0, 0, t)));
    quad(a, b, c, d, 1, 1, &head);
}
