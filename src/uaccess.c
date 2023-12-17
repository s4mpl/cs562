#include <uaccess.h>
#include <util.h>
#include <mmu.h>
#include <page.h>

unsigned long copy_from(void *dst, 
                        const struct page_table *from_table, 
                        const void *from, 
                        unsigned long size)
{
    /*
    steps:
    1. translate from address using from_table and mmu_translate
    2. dst address is already translated (physical identity map)?
    3. iterate over page sized bytes and copy 
        3.5 may need a check to see if bytes is less than a page
            or if there's uneven page division
    4. increment from addr pointer to get next page table entry
    5. repeat from step 1 until either fault of bytes all allocated
    */

    unsigned long bytes_copied = 0;
    unsigned long required_perms = 0x12; //user and read access

    for (const void* from_itr = from; bytes_copied < size;){
        // //debugf("copy_from: Iterating...\n");
        
        //check if we have access to this entry
        if(!mmu_access_ok(from_table, (uint64_t)from_itr, required_perms)) break;

        //get phys addr
        uint64_t phys_addr = mmu_translate(from_table, (uint64_t)from_itr);
        // //debugf("copy_from: phys_addr is 0x%08x\n", phys_addr);

        //check for fault
        if (phys_addr == MMU_TRANSLATE_PAGE_FAULT){
            break;
        }

        //calculate how many bytes to copy
        int bytes_to_copy = MIN((ALIGN_UP_POT(phys_addr + 1, PAGE_SIZE) - phys_addr), (size - bytes_copied));
        memcpy(dst, (void *)phys_addr, bytes_to_copy);
        // //debugf("copy_from: bytes_to_copy is %d\n", bytes_to_copy);

        //increment to move past what we copied
        dst += bytes_to_copy;
        from_itr += bytes_to_copy;
        bytes_copied += bytes_to_copy;
    }

    return bytes_copied;
}

unsigned long copy_to(void *to, 
                      const struct page_table *to_table, 
                      const void *src, 
                      unsigned long size)
{
    unsigned long bytes_copied = 0;
    unsigned long required_perms = 0x14; // need user and write bits set in PTE

    for(const void *to_itr = to; bytes_copied < size;) {
        // //debugf("copy_to: Iterating...\n");
        
        /* Check if we have proper access to this entry. */
        if(!mmu_access_ok(to_table, (uint64_t)to_itr, required_perms)) break;

        /* Translate the current PTE with the provided page table. */
        uint64_t to_paddr = mmu_translate(to_table, (uint64_t)to_itr);
        if(to_paddr == MMU_TRANSLATE_PAGE_FAULT) break;
        // //debugf("copy_to: to_paddr is 0x%08x\n", to_paddr);

        /* Calculate how many bytes to copy (up to the next page-aligned address or until size is reached).
         * Add 1 so that if already page-aligned, it will attempt to copy `PAGE_SIZE` bytes instead of 0.
        */
        int bytes_to_copy = MIN((ALIGN_UP_POT(to_paddr + 1, PAGE_SIZE) - to_paddr), (size - bytes_copied));
        memcpy((void *)to_paddr, src, bytes_to_copy);
        // //debugf("copy_to: bytes_to_copy is %d\n", bytes_to_copy);

        /* Move pointers to where to start copying "contiguously" next. */
        src += bytes_to_copy;
        to_itr += bytes_to_copy;
        bytes_copied += bytes_to_copy;
    }

    return bytes_copied;
}