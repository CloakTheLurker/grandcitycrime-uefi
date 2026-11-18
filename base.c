// no libc so the stuff the compiler expects lives here
int _fltused = 0;

void *memset(void *d, int c, usize n) {
    u8 *p = d;
    while (n--) *p++ = (u8)c;
    return d;
}

void *memcpy(void *d, const void *s, usize n) {
    u8 *p = d;
    const u8 *q = s;
    while (n--) *p++ = *q++;
    return d;
}

static inline float sqrtf_(float x) { return __builtin_sqrtf(x); }
static inline float absf(float x) { return x < 0 ? -x : x; }
static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float maxf(float a, float b) { return a > b ? a : b; }
static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline int mini(int a, int b) { return a < b ? a : b; }
static inline int maxi(int a, int b) { return a > b ? a : b; }
static inline int floori(float x) { int i = (int)x; return i > x ? i - 1 : i; }

#define PI 3.14159265f
#define TAU 6.2831853f

// wrap into -pi..pi
static float wrapf(float a) {
    float k = a * (1.0f / TAU);
    int n = (int)(k + (k < 0 ? -0.5f : 0.5f));
    return a - n * TAU;
}

// small sine good enough for games, error under 0.001
static float sinf_(float x) {
    x = wrapf(x);
    float y = 1.2732395f * x - 0.4052847f * x * absf(x);
    return 0.225f * (y * absf(y) - y) + y;
}
static float cosf_(float x) { return sinf_(x + PI * 0.5f); }

static float atan2f_(float y, float x) {
    float ax = absf(x), ay = absf(y);
    float mx = maxf(ax, ay), mn = minf(ax, ay);
    if (mx == 0) return 0;
    float a = mn / mx, s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) r = 1.57079637f - r;
    if (x < 0) r = PI - r;
    if (y < 0) r = -r;
    return r;
}

static u32 seed = 0x2545f491;
static u32 rnd(void) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}
static int rnd_in(int n) { return (int)(rnd() % (u32)n); }
static float rndf(void) { return (rnd() & 0xffffff) / 16777216.0f; }

static u32 hash2(int x, int y) {
    u32 h = (u32)x * 374761393u + (u32)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static inline u32 rgb(int r, int g, int b) {
    return (u32)(clampf(r, 0, 255)) << 16 | (u32)(clampf(g, 0, 255)) << 8 | (u32)(clampf(b, 0, 255));
}

// mix two colours, t 0 to 256
static inline u32 mix(u32 a, u32 b, u32 t) {
    u32 rb = ((a & 0xff00ff) * (256 - t) + (b & 0xff00ff) * t) >> 8 & 0xff00ff;
    u32 g = ((a & 0x00ff00) * (256 - t) + (b & 0x00ff00) * t) >> 8 & 0x00ff00;
    return rb | g;
}
