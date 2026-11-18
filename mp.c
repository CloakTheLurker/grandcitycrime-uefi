typedef struct MpServices {
    u64 (EFIAPI *count)(struct MpServices *, usize *, usize *);
    void *info;
    u64 (EFIAPI *all)(struct MpServices *, void (EFIAPI *)(void *), u8, void *, usize, void *, usize **);
    void *this_ap, *switch_bsp, *enable, *who;
} MpServices;

#define MAXW 8
static volatile int job_seq, job_left, ap_ids, ap_used;
static void (*volatile job_fn)(int, int);
static int nwork = 1;

static void EFIAPI ap_entry(void *arg) {
    (void)arg;
    int id = __sync_add_and_fetch(&ap_ids, 1);
    int seen = job_seq;
    for (;;) {
        // helpers chill here till the job number changes
        while (job_seq == seen) __asm__ volatile("pause" ::: "memory");
        seen = job_seq;
        if (id >= ap_used + 1) continue;
        job_fn(id, nwork);
        __sync_fetch_and_sub(&job_left, 1);
    }
}

static void run_job(void (*fn)(int, int)) {
    if (nwork == 1) { fn(0, 1); return; }
    job_fn = fn;
    job_left = ap_used;
    __sync_synchronize();
    job_seq++;
    fn(0, nwork);
    while (job_left) __asm__ volatile("pause" ::: "memory");
    __sync_synchronize();
}

static void mp_start(void) {
    // mp services guid also from the spec
    Guid g = {0x3fdda605, 0xa76e, 0x4f46, {0xad, 0x29, 0x12, 0xf4, 0x53, 0x1b, 0x3d, 0x08}};
    MpServices *mp = 0;
    if (bs->locate_proto(&g, 0, (void **)&mp) || !mp) return;
    usize total = 0, on = 0;
    if (mp->count(mp, &total, &on) || on < 2) return;
    void *ev = 0;
    if (((u64 (EFIAPI *)(u32, u64, void *, void *, void **))bs->create_event)(0, 0, 0, 0, &ev) || !ev) return;
    ap_used = 1 << 30;
    job_seq = 0;
    if (mp->all(mp, ap_entry, 0, ev, 0, 0, 0)) return;
    for (int t = 0; t < 300 && ap_ids < (int)on - 1; t++) bs->stall(1000);
    int got = ap_ids;
    ap_used = mini(got, MAXW - 1);
    nwork = ap_used + 1;
    __sync_synchronize();
}
