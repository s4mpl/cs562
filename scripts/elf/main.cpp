#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "elf.h"

using namespace std;

int main(int argc, char *argv[])
{
    int fd;
    char *elf;
    Elf64_Ehdr *ehdr;
    Elf64_Phdr *phdr;
    Elf64_Half i;
    off_t filelen;

    if (argc < 2) {
        printf("Usage: %s <file>\n", argv[0]);
        return -1;
    }

    if ((fd = open(argv[1], O_RDONLY, 0)) < 0) {
        perror("open");
        return -1;
    }

    filelen = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);

    elf = (char *)mmap(NULL, filelen, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (elf == MAP_FAILED) {
        perror("mmap");
        return -1;
    }

    ehdr = (Elf64_Ehdr *)(elf + 0);

    printf("ELF HEADER\n~~~~~~~~~~\n");
    printf("Magic = %02x %02x %02x %02x\n", ehdr->e_ident[0], ehdr->e_ident[1], ehdr->e_ident[2], ehdr->e_ident[3]);
    if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG)) {
        printf("ERROR: MAGIC ISN'T AN ELF.\n");
        munmap(elf, filelen);
        return -1;
    }

    if (ehdr->e_machine == EM_RISCV) {
        printf("Machine = %u (RISCV).\n", EM_RISCV);
    }
    if (ehdr->e_type == ET_EXEC) {
        printf("Type = %u (EXECUTABLE).\n", ET_EXEC);
    }
    printf("Entry point = 0x%08lx\n", ehdr->e_entry);
    phdr = (Elf64_Phdr *)(elf + ehdr->e_phoff);

    for (i = 0;i < ehdr->e_phnum;i+=1) {
        if (phdr[i].p_type == PT_LOAD) {
            printf("  %u is PT_LOAD: ", i);
            if (phdr[i].p_flags & PF_R) {
                printf("R");
            }
            if (phdr[i].p_flags & PF_W) {
                printf("W");
            }
            if (phdr[i].p_flags & PF_X) {
                printf("X");
            }
            printf("\n");
            printf("    --> Map %lu bytes.\n      : data in file offset %lu\n      : base address 0x%08lx.\n", phdr[i].p_memsz, phdr[i].p_offset, phdr[i].p_vaddr);
        }
    }

    munmap(elf, filelen);
    return 0;
}
