#include <csr.h>
#include <errno.h>
#include <sbi.h>
#include <stdint.h>
#include <util.h>
#include <process.h>
#include <sched.h>
#include <uaccess.h>
#include <gpu.h>
#include <page.h>
#include <input.h>

#define XREG(x)             (scratch[XREG_##x])
#define SYSCALL_RETURN_TYPE void
#define SYSCALL_PARAM_LIST  int hart, uint64_t epc, int64_t *scratch
#define SYSCALL(t)          static SYSCALL_RETURN_TYPE syscall_##t(SYSCALL_PARAM_LIST)
#define SYSCALL_PTR(t)      syscall_##t
#define SYSCALL_EXEC(x)     SYSCALLS[(x)](hart, epc, scratch)
#define SYSCALL_ENTER() \
    (void)hart;         \
    (void)epc;          \
    (void)scratch

SYSCALL(exit)
{
    debugf("SYSCALL EXIT\n");
    SYSCALL_ENTER();
    // Kill the current process on this HART and schedule the next
    // one.
    Process *p = get_proc_on_hart(hart);
    process_free(p);
    sched_remove(p);
    sched_invoke(hart);
}

SYSCALL(putchar)
{
    SYSCALL_ENTER();
    sbi_putchar(XREG(A0));
}

SYSCALL(getchar)
{
    SYSCALL_ENTER();
    XREG(A0) = sbi_getchar();
}

SYSCALL(yield)
{
    SYSCALL_ENTER();
    sched_invoke(hart);
}

SYSCALL(sleep)
{
    // printf("SYSCALL SLEEP\n");
    SYSCALL_ENTER();
    // Sleep the process. VIRT_TIMER_FREQ is 10MHz, divided by 1000, we get 10KHz
    //     p->sleep_until = sbi_get_time() + XREG(A0) * VIRT_TIMER_FREQ / 1000;
    //     p->state = PS_SLEEPING;
    Process *p = get_proc_on_hart(hart);

    if(p == NULL){
        debugf("SLEEP PROBLEM\n");
    } 
    else{
        p->sleep_until = sbi_get_time();
        p->sleep_until += XREG(A0) * VIRT_TIMER_FREQ / 1000;
        p->state = PS_SLEEPING;
    }
    
    sched_invoke(hart);
}

SYSCALL(get_keyboard)
{
    SYSCALL_ENTER();
    XREG(A0) = get_keyboard();
}

SYSCALL(get_cursor)
{
    SYSCALL_ENTER();
    Process *p = get_proc_on_hart(hart);

    coord_pair coord = get_cursor();
    copy_to(XREG(A0), p->ptable, &coord, sizeof(coord_pair));
}

SYSCALL(get_gpu)
{
    SYSCALL_ENTER();
    Process *p = get_proc_on_hart(hart);

    void *vaddr = g_gdev;

    // Map the entire GPU!
    mmu_map(p->ptable,
            vaddr, 
            mmu_translate(kernel_mmu_table, vaddr), 
            MMU_LEVEL_4K, 
            PB_USER | PB_READ);

    vaddr = g_gdev->framebuffer;
    // Map the framebuffer for the user :-)
    mmu_map_range(p->ptable,
                  vaddr,
                  vaddr + (g_gdev->width * g_gdev->height * sizeof(PixelRGBA)),
                  mmu_translate(kernel_mmu_table, vaddr), 
                  MMU_LEVEL_4K, 
                  PB_USER | PB_READ | PB_WRITE);

    XREG(A0) = g_gdev;
}

SYSCALL(malloc)
{
    SYSCALL_ENTER();
    Process *p = get_proc_on_hart(hart);

    int amt = ALIGN_UP_POT(XREG(A0), PAGE_SIZE);

    void *heap_page = page_znalloc(amt / PAGE_SIZE);
    for(uint32_t i = 0; i < amt; i += PAGE_SIZE) {
        list_add_ptr(p->rcb.heap_pages, heap_page + i);

        mmu_map(p->ptable, heap_page + i, heap_page + i, MMU_LEVEL_4K, PB_USER | PB_READ | PB_WRITE);
    }

    XREG(A0) = heap_page;

    // char *vaddr_start = kzalloc(amt);
    // char *vaddr_cur = vaddr_start;
    // char *vaddr_end = vaddr_start + amt;

    // uint32_t num_mapped = 0;
    // while(vaddr_cur < vaddr_end) {
    //     debugf("mapping page %p\n", vaddr_cur);

    //     if(mmu_map(p->ptable,
    //                vaddr_cur, 
    //                mmu_translate(kernel_mmu_table, vaddr_cur), 
    //                MMU_LEVEL_4K, 
    //                PB_USER | PB_READ | PB_WRITE)) {
    //         num_mapped++;
    //     }

    //     vaddr_cur += PAGE_SIZE;
    // }

    // debugf("mapped %u pages in syscall malloc\n", num_mapped);

    // XREG(A0) = vaddr_start;
}

SYSCALL(free)
{
    SYSCALL_ENTER();
    Process *p = get_proc_on_hart(hart);

    kfree(mmu_translate(p->ptable, XREG(A0)));
}

SYSCALL(fstat)
{
    SYSCALL_ENTER();

}

SYSCALL(open)
{
    SYSCALL_ENTER();
    char buf[60];
    Process *p = get_proc_on_hart(hart);

    // debugf("file_path ptr: %p\n", XREG(A0));

    copy_from(buf, p->ptable, XREG(A0), 60);
    char *file_path = buf;

    // debugf("file_path translated: %p\n", file_path);

    int flags = XREG(A1);
    uint32_t mode = XREG(A2);
    XREG(A0) = open(file_path, flags, mode);
    debugf("exit syscall open\n");
}

SYSCALL(close)
{
    SYSCALL_ENTER();
    XREG(A0) = close(XREG(A0));
}

SYSCALL(read)
{
    SYSCALL_ENTER();
    int fd = XREG(A0);
    size_t count = XREG(A2);
    Process *p = get_proc_on_hart(hart);

    char *buf = kzalloc(count);
    debugf("syscall read\n");
    read(fd, buf, count);
    XREG(A0) = copy_to(XREG(A1), p->ptable, buf, count);
}

SYSCALL(write)
{
    SYSCALL_ENTER();
    int fd = XREG(A0);
    size_t count = XREG(A2);
    Process *p = get_proc_on_hart(hart);

    char *buf = kzalloc(count);
    copy_from(buf, p->ptable, XREG(A1), count);
    debugf("syscall write\n");
    XREG(A0) = write(fd, buf, count);
}

SYSCALL(gpu_redraw)
{
    SYSCALL_ENTER();
    GpuDevice *gdev = XREG(A0);
    gpu_redraw(&(Rectangle){ 0, 0, gdev->width, gdev->height }, gdev);
}

/**
    SYS_EXIT = 0,
    SYS_PUTCHAR,
    SYS_GETCHAR,
    SYS_YIELD,
    SYS_SLEEP,
    SYS_OLD_GET_EVENTS,
    SYS_GET_GPU,
    SYS_MALLOC,
    SYS_FREE,
    SYS_FSTAT,
    SYS_OPEN,
    SYS_CLOSE,
    SYS_READ,
    SYS_WRITE,
    SYS_GPU_FLUSH,
    SYS_STAT,
    SYS_SEEK,
    SYS_SBRK
*/
// These syscall numbers MUST match the user/libc numbers!
static SYSCALL_RETURN_TYPE (*const SYSCALLS[])(SYSCALL_PARAM_LIST) = {
    SYSCALL_PTR(exit),          /* 0 */
    SYSCALL_PTR(putchar),       /* 1 */
    SYSCALL_PTR(getchar),       /* 2 */
    SYSCALL_PTR(yield),         /* 3 */
    SYSCALL_PTR(sleep),         /* 4 */
    SYSCALL_PTR(get_keyboard),  /* 5 */
    SYSCALL_PTR(get_cursor),    /* 6 */
    SYSCALL_PTR(get_gpu),       /* 7 */
    SYSCALL_PTR(malloc),        /* 8 */
    SYSCALL_PTR(free),          /* 9 */
    SYSCALL_PTR(fstat),         /* 10 */
    SYSCALL_PTR(open),          /* 11 */
    SYSCALL_PTR(close),         /* 12 */
    SYSCALL_PTR(read),          /* 13 */
    SYSCALL_PTR(write),         /* 14 */
    SYSCALL_PTR(gpu_redraw),    /* 15 */
};

static const int NUM_SYSCALLS = sizeof(SYSCALLS) / sizeof(SYSCALLS[0]);

// We get here from the trap.c if this is an ECALL from U-MODE
void syscall_handle(int hart, uint64_t epc, int64_t *scratch)
{
    // Sched invoke will save sepc, so we want it to resume
    // 4 bytes ahead, which will be the next instruction.
    CSR_WRITE("sepc", epc + 4);

    if (XREG(A7) >= NUM_SYSCALLS || SYSCALLS[XREG(A7)] == NULL) {
        // Invalid syscall
        XREG(A0) = -EINVAL;
    }
    else {
        SYSCALL_EXEC(XREG(A7));
    }
}
