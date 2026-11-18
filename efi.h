typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef signed char i8;
typedef short i16;
typedef int i32;
typedef long long i64;
typedef u64 usize;

#define EFIAPI __attribute__((ms_abi))

typedef struct { u32 a; u16 b, c; u8 d[8]; } Guid;

typedef struct {
    u64 sig; u32 rev, size, crc, res;
} TableHeader;

typedef struct { u16 scan; u16 ch; } InputKey;

typedef struct TextIn {
    u64 (EFIAPI *reset)(struct TextIn *, u8);
    u64 (EFIAPI *read_key)(struct TextIn *, InputKey *);
    void *wait;
} TextIn;

typedef struct {
    u32 version, width, height, format;
    u32 masks[4];
    u32 pitch;
} GopInfo;

typedef struct {
    u32 max_mode, mode;
    GopInfo *info;
    u64 info_size;
    u64 fb_base, fb_size;
} GopMode;

typedef struct Gop {
    u64 (EFIAPI *query)(struct Gop *, u32, u64 *, GopInfo **);
    u64 (EFIAPI *set_mode)(struct Gop *, u32);
    u64 (EFIAPI *blt)(struct Gop *, u32 *, u32, usize, usize, usize, usize, usize, usize, usize);
    GopMode *mode;
} Gop;

typedef struct {
    TableHeader hdr;
    void *raise_tpl, *restore_tpl;
    void *alloc_pages, *free_pages, *mem_map;
    u64 (EFIAPI *alloc_pool)(u32, usize, void **);
    void *free_pool;
    void *create_event, *set_timer, *wait_event, *signal_event, *close_event, *check_event;
    void *install_if, *reinstall_if, *uninstall_if, *handle_proto, *reserved, *register_notify, *locate_handle, *locate_path, *install_config;
    void *load_image, *start_image, *exit, *unload_image, *exit_boot;
    void *next_count;
    u64 (EFIAPI *stall)(usize);
    u64 (EFIAPI *watchdog)(usize, u64, usize, u16 *);
    void *connect, *disconnect, *open_proto, *close_proto, *open_info;
    void *protos_per_handle, *locate_handle_buf;
    u64 (EFIAPI *locate_proto)(Guid *, void *, void **);
} BootServices;

typedef struct {
    TableHeader hdr;
    u16 *vendor; u32 rev;
    void *in_handle; TextIn *con_in;
    void *out_handle; void *con_out;
    void *err_handle; void *std_err;
    void *runtime;
    BootServices *boot;
} SystemTable;
