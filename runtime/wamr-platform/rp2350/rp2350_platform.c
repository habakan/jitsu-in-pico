#include "platform_api_vmcore.h"
#include "platform_api_extension.h"
#ifdef PICO_BUILD
#include "pico/time.h"
#else
static uint64 time_us_64(void) { return 0; }
#endif

int bh_platform_init(void) { return 0; }
void bh_platform_destroy(void) {}

/* 確保はすべて WAMR のプールアロケータ経由にする */
void *os_malloc(unsigned size) { (void)size; return NULL; }
void *os_realloc(void *ptr, unsigned size) { (void)ptr; (void)size; return NULL; }
void os_free(void *ptr) { (void)ptr; }

uint64 os_time_get_boot_us(void) { return time_us_64(); }
uint64 os_time_thread_cputime_us(void) { return time_us_64(); }

korp_tid os_self_thread(void) { return (korp_tid)1; }
uint8 *os_thread_get_stack_boundary(void) { return NULL; }
void os_thread_jit_write_protect_np(bool enabled) { (void)enabled; }

int os_mutex_init(korp_mutex *m) { (void)m; return BHT_OK; }
int os_mutex_destroy(korp_mutex *m) { (void)m; return BHT_OK; }
int os_mutex_lock(korp_mutex *m) { (void)m; return BHT_OK; }
int os_mutex_unlock(korp_mutex *m) { (void)m; return BHT_OK; }

void *os_mmap(void *hint, size_t size, int prot, int flags, os_file_handle file)
{
    void *addr;
    (void)hint; (void)prot; (void)flags; (void)file;
    if (size >= UINT32_MAX || !(addr = BH_MALLOC((uint32)size)))
        return NULL;
    memset(addr, 0, size);
    return addr;
}
void os_munmap(void *addr, size_t size) { (void)size; BH_FREE(addr); }
int os_mprotect(void *addr, size_t size, int prot) { (void)addr; (void)size; (void)prot; return 0; }
void *os_mremap(void *old_addr, size_t old_size, size_t new_size)
{
    return os_mremap_slow(old_addr, old_size, new_size);
}
void os_dcache_flush(void) {}
void os_icache_flush(void *start, size_t len) { (void)start; (void)len; }
os_raw_file_handle os_invalid_raw_handle(void) { return -1; }
