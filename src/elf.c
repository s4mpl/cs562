#include <elf.h>
#include <util.h>
#include <process.h>
#include <page.h>
#include <uaccess.h>

/**
 * @brief Load an ELF file into a process and update its fields.
 *
 * @param p The process to load the ELF into.
 * @param elf A buffer of the ELF file. This must be canonical!
 * @return true The ELF file was read and the process updated.
 * @return false The ELF file is not valid or isn't targeting our machine.
 */
bool elf_load(struct process *p, const void *elf) {
  // Read the ELF header (EH).
  const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)(elf + 0);

  // Error check.
  if(!elf_valid(ehdr)) return false;
  
  // Go to the program header table (PH) by seeking to EH.phoff.
  Elf64_Phdr *phdr = (Elf64_Phdr *)(elf + ehdr->e_phoff);

  //debugf("sscratch vaddr 0x%08x to paddr 0x%08x\n", p->frame.sscratch, mmu_translate(p->ptable, p->frame.sscratch));
  uint8_t p_flags = 0; // Don't reset any flags between iterations (cumulative)?
  // Loop EH.phnum times seeking sizeof(PH) for each iteration.
  for(uint32_t i = 0; i < ehdr->e_phnum; i += 1) {
    // Check PH.p_type against PT_LOAD (1).
    if(phdr[i].p_type == PT_LOAD) {
      // printf("  %u is PT_LOAD: ", i);
      
      // Check PH.p_memsz to see if it is non-zero.
      if(phdr[i].p_memsz == 0) continue;
      
      // Set mapping permissions to PH.p_flags (PF_X, PF_W, PF_R).
      // Be careful when mapping! Program addresses may overlap pages! Therefore, you might reset a PF_X, PF_W, or PF_R. Flags should be cumulative instead.
      if(phdr[i].p_flags & PF_R) {
        // printf("R");
        p_flags |= PB_READ;
      }
      if(phdr[i].p_flags & PF_W) {
        // printf("W");
        p_flags |= PB_WRITE;
      }
      if(phdr[i].p_flags & PF_X) {
        // printf("X");
        p_flags |= PB_EXECUTE;
      }
      // printf("\n");
      printf("    --> Map %lu bytes.\n      : data in file offset %lu\n      : base address 0x%08lx.\n", phdr[i].p_memsz, phdr[i].p_offset, phdr[i].p_vaddr);
      
      void *image_page; // = kzalloc(phdr[i].p_memsz);

      // Copy bytes starting at PH.p_offset into process image.
      // copy_to(image_page, p->ptable, elf + phdr[i].p_offset, phdr[i].p_memsz);
      for(uint32_t j = 0; j < phdr[i].p_memsz; j += PAGE_SIZE) {
        // Allocate full, mappable, pages for each 4,096-byte chunk.
        image_page = page_zalloc();
        memcpy(image_page, elf + phdr[i].p_offset + j, MIN(PAGE_SIZE, phdr[i].p_memsz - j));
        list_add_ptr(p->rcb.image_pages, image_page);

        // Map virtual addresses to PH.p_vaddr. (PH.p_paddr must be ignored).
        debugf("mapping vaddr 0x%08x to paddr 0x%08x\n", phdr[i].p_vaddr + j, image_page);
        mmu_map(p->ptable, phdr[i].p_vaddr + j, image_page, MMU_LEVEL_4K, PB_USER | p_flags);
      }
    }
  }

  // Set the process image's program counter to EH.e_entry (virtual memory address).
  p->frame.sepc = ehdr->e_entry;
  // p->frame.xregs[1] = ehdr->e_entry;
  debugf("e_entry (sepc) address 0x%08x\n", ehdr->e_entry);
  debugf("user translated sepc memory address 0x%08x\n", mmu_translate(p->ptable, ehdr->e_entry));
  mmu_map(kernel_mmu_table, ehdr->e_entry, mmu_translate(p->ptable, ehdr->e_entry), MMU_LEVEL_4K, PB_READ | PB_EXECUTE); // don't need this
  debugf("kernel translated sepc memory address 0x%08x\n", mmu_translate(kernel_mmu_table, ehdr->e_entry));

  debugf("Successfully loaded ELF file!\n");
  return true;
}

bool elf_valid(const Elf64_Ehdr *ehdr) {
  // Check EH.ident[0:3] (magic) against '\x7fELF'.
  // Check EH.e_machine against EM_RISCV (243).
  // Check EH.e_type against ET_EXEC (2).
  return !memcmp(ehdr->e_ident, ELFMAG, SELFMAG) &&
         ehdr->e_type    == ET_EXEC              &&
         ehdr->e_machine == EM_RISCV             &&
         ehdr->e_version == EV_CURRENT;
}