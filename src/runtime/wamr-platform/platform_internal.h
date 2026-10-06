#ifndef _PLATFORM_INTERNAL_H
#define _PLATFORM_INTERNAL_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Single-threaded. The thread types are here only to let this compile */
#define BH_APPLET_PRESERVED_STACK_SIZE (2 * BH_KB)
#define BH_THREAD_DEFAULT_PRIORITY 0

typedef int korp_thread;
typedef korp_thread *korp_tid;
typedef int korp_mutex;
typedef int korp_sem;
typedef int korp_cond;
typedef int korp_rwlock;

#define os_printf printf
#define os_vprintf vprintf

typedef int os_file_handle;
typedef void *os_dir_stream;
typedef int os_raw_file_handle;
typedef int os_poll_file_handle;
typedef unsigned int os_nfds_t;
typedef int os_timespec;

static inline os_file_handle os_get_invalid_handle(void) {
    return -1;
}
static inline int os_getpagesize(void) {
    return 4096;
}

#endif
