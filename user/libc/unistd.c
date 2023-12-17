#include "unistd.h"

enum syscall_nos {
    SYS_EXIT = 0,
    SYS_PUTCHAR,
    SYS_GETCHAR,
    SYS_YIELD,
    SYS_SLEEP,
    SYS_GET_KEYBOARD,
    SYS_GET_CURSOR,
    SYS_GET_GPU,
    SYS_MALLOC,
    SYS_FREE,
    SYS_FSTAT,
    SYS_OPEN,
    SYS_CLOSE,
    SYS_READ,
    SYS_WRITE,
    SYS_GPU_REDRAW,
    SYS_SEEK,
    SYS_UNLINK,
    SYS_CHMOD,
    SYS_MKDIR,
    SYS_RMDIR,
    SYS_CHDIR,
    SYS_GETCWD,
    SYS_MKNOD,
    SYS_FORK,
    SYS_EXEC,
    SYS_WAIT,
    SYS_KILL,
};

void exit(void)
{
    __asm__ volatile("mv a7, %0\necall" :: "r"(SYS_EXIT) : "a7");
}

void yield(void)
{
    __asm__ volatile("mv a7, %0\necall" : : "r"(SYS_YIELD) : "a7");
}

void sleep(int secs)
{
    __asm__ volatile("mv a7, %0\nmv a0, %1\necall" : : "r"(SYS_SLEEP), "r"(secs) : "a0", "a7");
}

signed char get_keyboard(void)
{
    signed char ret;
    __asm__ volatile("mv a7, %1\necall\nmv %0, a0\n"
                     : "=r"(ret)
                     : "r"(SYS_GET_KEYBOARD)
                     : "a7");
    return ret;
}

void get_cursor(void *coord_pair)
{
    __asm__ volatile("mv a7, %0\nmv a0, %1\necall" : : "r"(SYS_GET_CURSOR), "r"(coord_pair) : "a0", "a7");
}

void *get_gpu(void)
{
    void *ret;
    __asm__ volatile("mv a7, %1\necall\nmv %0, a0\n"
                     : "=r"(ret)
                     : "r"(SYS_GET_GPU)
                     : "a7");
    return ret;
}

void *malloc(int amt)
{
    void *ret;
    __asm__ volatile("mv a7, %1\nmv a0, %2\necall\nmv %0, a0\n"
                     : "=r"(ret)
                     : "r"(SYS_MALLOC), "r"(amt)
                     : "a0", "a7");
    return ret;
}

void free(void *ptr)
{
    __asm__ volatile("mv a7, %0\nmv a0, %1\necall" : : "r"(SYS_FREE), "r"(ptr) : "a0", "a7");
}

int fstat(const char *path, struct stat *stat)
{
    int ret;
    __asm__ volatile("mv a7, %1\nmv a0, %2\nmv a1, %3\necall\nmv %0, a0"
                     : "=r"(ret)
                     : "r"(SYS_FSTAT), "r"(path), "r"(stat)
                     : "a0", "a1", "a7");
    return ret;
}

int open(const char *path, int flags, mode_t mode)
{
    int ret;
    __asm__ volatile("mv a7, %1\nmv a0, %2\nmv a1, %3\nmv a2, %4\necall\nmv %0,a0"
                     : "=r"(ret)
                     : "r"(SYS_OPEN), "r"(path), "r"(flags), "r"(mode)
                     : "a0", "a1", "a2", "a7");
    return ret;
}

int close(int fd)
{
    int ret;
    __asm__ volatile("mv a7, %1\nmv a0, %2\n\necall\nmv %0, a0"
                     : "=r"(ret)
                     : "r"(SYS_CLOSE), "r"(fd)
                     : "a0", "a7");
    return ret;
}

ssize_t read(int fd, void *buf, size_t count)
{
    ssize_t ret;
    __asm__ volatile("mv a7, %1\nmv a0, %2\nmv a1, %3\nmv a2, %4\necall\nmv %0,a0"
                     : "=r"(ret)
                     : "r"(SYS_READ), "r"(fd), "r"(buf), "r"(count)
                     : "a0", "a1", "a2", "a7");
    return ret;
}

ssize_t write(int fd, const void *buf, size_t count)
{
    ssize_t ret;
    __asm__ volatile("mv a7, %1\nmv a0, %2\nmv a1, %3\nmv a2, %4\necall\nmv %0,a0"
                     : "=r"(ret)
                     : "r"(SYS_WRITE), "r"(fd), "r"(buf), "r"(count)
                     : "a0", "a1", "a2", "a7");
    return ret;
}

void gpu_redraw(void *gdev)
{
    __asm__ volatile("mv a7, %0\nmv a0, %1\necall" : : "r"(SYS_GPU_REDRAW), "r"(gdev) : "a0", "a7");
}

off_t lseek(int fd, off_t offset, int whence)
{
    off_t ret;
    __asm__ volatile("mv a7, %1\nmv a0, %2\nmv a1, %3\nmv a2, %4\necall\nmv %0,a0"
                     : "=r"(ret)
                     : "r"(SYS_SEEK), "r"(fd), "r"(offset), "r"(whence)
                     : "a0", "a1", "a2", "a7");
    return ret;
}

int unlink(const char *path)
{
    int ret;
    __asm__ volatile("mv a7, %1\nmv a0, %2\n\necall\nmv %0, a0"
                     : "=r"(ret)
                     : "r"(SYS_UNLINK), "r"(path)
                     : "a0", "a7");
    return ret;
}

int chmod(const char *path, mode_t mode)
{
    int ret;
    __asm__ volatile("mv a7, %1\nmv a0, %2\nmv a1, %3\necall\nmv %0,a0"
                     : "=r"(ret)
                     : "r"(SYS_CHMOD), "r"(path), "r"(mode)
                     : "a0", "a1", "a7");
    return ret;
}

int mkdir(const char *path, mode_t mode)
{
    int ret;
    __asm__ volatile("mv a7, %1\nmv a0, %2\nmv a1, %3\necall\nmv %0,a0"
                     : "=r"(ret)
                     : "r"(SYS_MKDIR), "r"(path), "r"(mode)
                     : "a0", "a1", "a7");
    return ret;
}

int rmdir(const char *path)
{
    int ret;
    __asm__ volatile("mv a7, %1\nmv a0, %2\n\necall\nmv %0, a0"
                     : "=r"(ret)
                     : "r"(SYS_RMDIR), "r"(path)
                     : "a0", "a7");
    return ret;
}

int chdir(const char *path)
{
    int ret;
    __asm__ volatile("mv a7, %1\nmv a0, %2\n\necall\nmv %0, a0"
                     : "=r"(ret)
                     : "r"(SYS_CHDIR), "r"(path)
                     : "a0", "a7");
    return ret;
}

size_t getcwd(char *buf, size_t bufsize)
{
    int ret;
    __asm__ volatile("mv a7, %1\nmv a0, %2\nmv a1, %3\necall\nmv %0,a0"
                     : "=r"(ret)
                     : "r"(SYS_GETCWD), "r"(buf), "r"(bufsize)
                     : "a0", "a1", "a7");
    return ret;
}

int mknod(const char *path, mode_t mode, dev_t dev)
{
    int ret;
    __asm__ volatile("mv a7, %1\nmv a0, %2\nmv a1, %3\nmv a2, %4\necall\nmv %0,a0"
                     : "=r"(ret)
                     : "r"(SYS_MKNOD), "r"(path), "r"(mode), "r"(dev)
                     : "a0", "a1", "a2", "a7");
    return ret;
}

int fork(void)
{
    int ret;
    __asm__ volatile("mv a7, %1\necall\nmv %0, a0" :"=r"(ret) : "r"(SYS_FORK) : "a0", "a7");
    return ret;
}

int exec(const char *path, const char *argv[])
{
    int ret;
    __asm__ volatile("mv a7, %1\nmv a0, %2\nmv a1, %3\necall\nmv %0, a0" : "=r"(ret) : "r"(SYS_EXEC), "r"(path), "r"(argv) : "a0", "a1", "a7");
    return ret;
}

int wait(int pid)
{
    int ret;
    __asm__ volatile("mv a7, %1\nmv a0, %2\necall\nmv %0, a0" : "=r"(ret) : "r"(SYS_WAIT), "r"(pid) : "a0", "a7");
    return ret;
}

int kill(int pid)
{
    int ret;
    __asm__ volatile("mv a7, %1\nmv a0, %2\necall\nmv %0, a0" : "=r"(ret) : "r"(SYS_KILL), "r"(pid) : "a0", "a7");
    return ret;
}
