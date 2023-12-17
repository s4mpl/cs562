#include <compiler.h>
#include <config.h>
#include <lock.h>
#include <mmu.h>
#include <page.h>
#include <util.h>

#define PTE_PPN0_BIT 10
#define PTE_PPN1_BIT 19
#define PTE_PPN2_BIT 28

#define ADDR_0_BIT   12
#define ADDR_1_BIT   21
#define ADDR_2_BIT   30

struct page_table *mmu_table_create(void)
{
    return page_zalloc();
}

bool mmu_map(struct page_table *tab, uint64_t vaddr, uint64_t paddr, uint8_t lvl, uint64_t bits)
{
    const uint64_t vpn[] = {(vaddr >> ADDR_0_BIT) & 0x1FF, (vaddr >> ADDR_1_BIT) & 0x1FF,
                            (vaddr >> ADDR_2_BIT) & 0x1FF};
    const uint64_t ppn[] = {(paddr >> ADDR_0_BIT) & 0x1FF, (paddr >> ADDR_1_BIT) & 0x1FF,
                            (paddr >> ADDR_2_BIT) & 0x3FFFFFF};

    //Root table can not be null
    //lvl must be between [0, max_number_page_table_levels - 1]
    //At least one of the bits must be set to 1
    if (tab == NULL || lvl > MMU_LEVEL_1G || (bits & 0xE) == 0) return false;

    // //debugf("mmu_map: Mapping vaddr 0x%08x to paddr 0x%08x at level %d with permission bits 0x%08x. Given table 0x%08x.\n", vaddr, paddr, lvl, bits, (uint64_t)tab);

    uint64_t entry = 0;
    
    for(uint8_t currLevel = MMU_LEVEL_1G; currLevel > lvl; currLevel--){
        entry = tab->entries[vpn[currLevel]];
        // //debugf("mmu_map: At level %d. Checking entry %d.\n", currLevel, vpn[currLevel]);

        if((entry & (PB_VALID))){//Page entry already exists. Go to that table
            uint64_t next_table = (entry >> 10) << 12;
            tab = (struct page_table                   *                    )                        next_table;
            // //debugf("mmu_map: Walking to existing lower-level page table 0x%08x.\n", (uint64_t)tab);
        }
        else{
            // //debugf("mmu_map: Attempting to allocate lower-level page table...\n");
            struct page_table *new_table = mmu_table_create();
            //Check if mem was allocated
            if(new_table == NULL) return false;

            uint64_t brandan_sucks = ((uint64_t)new_table) >> 2;
            entry = brandan_sucks | PB_VALID;
            tab->entries[vpn[currLevel]] = entry; // Actually put the entry into the current page table before walking down!!!
            tab = new_table;

            // //debugf("mmu_map: New lower-level page table 0x%08x successfully allocated.\n", (uint64_t)tab);
        }
    }

    //Set appropriate ppns
    if (lvl == 2 && lvl != 1 && lvl != 0)       entry = (ppn[2] << PTE_PPN2_BIT);
    else if (lvl != 2 && lvl ==1 && lvl != 0)   entry = (ppn[2] << PTE_PPN2_BIT) | (ppn[1] << PTE_PPN1_BIT);
    else if(lvl != 2 && lvl != 1 && lvl == 0)   entry = (ppn[2] << PTE_PPN2_BIT) | (ppn[1] << PTE_PPN1_BIT) | (ppn[0] << PTE_PPN0_BIT);
    
    //set bits and valid 
    entry = entry | bits | PB_VALID; // PB_* macros are already shifted!!! Don't shift by 1 to make space for the valid bit.
    tab->entries[vpn[lvl]] = entry;

    // //debugf("mmu_map: Successfully created mapping in table 0x%08x. Entry is 0x%08x at index %d.\n", (uint64_t)tab, entry, vpn[lvl]);
    
    return true;
}

//The mmu_free function needs to recursively free all of the entries of a given table. 
//Recall that each table could be a branch, which means that the memory address stored in the entry is a page you allocated. 
//All of these pages need to be freed.
void mmu_free(struct page_table *tab)
{
    if (tab == NULL) { //No page table, no function worky :). 
        return;
    }

    uint64_t pte;
    int i; 

    for(i = 0; i < PAGE_SIZE/8; i+=1) { //go through each entry in the table
        pte = tab->entries[i];
        if (pte & PB_VALID) { //if its valid, It has something stored
            if (!(pte & 0xE)){ //check if branch
                pte = (pte << 2) & ~0xFFFUL;
                mmu_free((struct page_table *)pte); //if its a branch, you have to recursively go to the next lowest level.
            }
            tab->entries[i] = 0; //clear the entry. 
        }

    }

    page_free(tab); //give the table back after its cleared. 
    
}

//The mmu_translate function will translate a virtual address to a physical address given a table.
//This will be helpful getting the physical address when worrying about hardware drivers, etc.s
uint64_t mmu_translate(const struct page_table *tab, uint64_t vaddr) {
    uint64_t vpn[] = { // split the vpn up to simplify access
        (vaddr >> ADDR_0_BIT) & 0x1FF, 
        (vaddr >> ADDR_1_BIT) & 0x1FF,
        (vaddr >> ADDR_2_BIT) & 0x1FF
    };

    // Can't translate without a table.
    if (tab == NULL) {
        return MMU_TRANSLATE_PAGE_FAULT;
    }

    // L2 
    uint64_t pte = tab->entries[vpn[2]];
    if(!(pte & PB_VALID)){ //Check if it is valid
        return MMU_TRANSLATE_PAGE_FAULT;
    }
    if (pte & 0xE) { //Check if its a leaf
        uint64_t ppn[] = {(pte >> PTE_PPN0_BIT) & 0x1FF, (pte >> PTE_PPN1_BIT) & 0x1FF, (pte >> PTE_PPN2_BIT) & 0x3FFFFFF};
        return (ppn[2] << ADDR_2_BIT) | (vaddr & 0x3FFFFFFF); 
    }

    //L1
    tab = (struct page_table *)((pte << 2) & ~0xFFFUL);
    pte = tab->entries[vpn[1]];
    if(!(pte & PB_VALID)){ //Check if it is valid
        return MMU_TRANSLATE_PAGE_FAULT;
    }
    if (pte & 0xE) { //Check if its a leaf
        uint64_t ppn[] = {(pte >> PTE_PPN0_BIT) & 0x1FF, (pte >> PTE_PPN1_BIT) & 0x1FF, (pte >> PTE_PPN2_BIT) & 0x3FFFFFF};
        return (ppn[2] << ADDR_2_BIT) | (ppn[1] << ADDR_1_BIT) | (vaddr & 0x1FFFFF);
    }

    //L0
    tab = (struct page_table *)((pte << 2) & ~0xFFFUL);
    pte = tab->entries[vpn[0]];
    if(!(pte & PB_VALID)){ //Check if it is valid
        return MMU_TRANSLATE_PAGE_FAULT;
    }
    if (pte & 0xE) { //Check if its a leaf
        uint64_t ppn[] = {(pte >> PTE_PPN0_BIT) & 0x1FF, (pte >> PTE_PPN1_BIT) & 0x1FF, (pte >> PTE_PPN2_BIT) & 0x3FFFFFF};
        return ((ppn[2] << ADDR_2_BIT) | (ppn[1] << ADDR_1_BIT) | (ppn[0] << ADDR_0_BIT) | (vaddr & 0xFFF));
    }
    return MMU_TRANSLATE_PAGE_FAULT; //If we get here, somethings fu- messed up!
}

bool mmu_access_ok(const struct page_table *tab, unsigned long vaddr, unsigned long required_perms)
{
    uint64_t vpn[] = {(vaddr >> ADDR_0_BIT) & 0x1FF, (vaddr >> ADDR_1_BIT) & 0x1FF,
                      (vaddr >> ADDR_2_BIT) & 0x1FF};

    // Can't translate without a table.
    if (tab == NULL){
        return false;
    }

    for (int i = MMU_LEVEL_1G; i >= MMU_LEVEL_4K; i--){
        uint64_t tab_entry = tab->entries[vpn[i]];

        // page is not valid
        if ((tab_entry & PB_VALID) == 0){
            return false;
        }
        // page is a leaf node if one of RWX is set, return whether the entry meets permission requirements (by masking)
        else if ((tab_entry & 0xe) != 0){
            return (tab_entry & required_perms) == required_perms;
        }
        else{
            // left shift 2 to preserve PPNs before zeroing out last 12 bits for page alignment
            tab = (struct page_table*)((tab_entry << 2) & ~0xfffUL);
        }
    }

    return false;
}

uint64_t mmu_map_range(struct page_table *tab, 
                       uint64_t start_virt, 
                       uint64_t end_virt, 
                       uint64_t start_phys,
                       uint8_t lvl, 
                       uint64_t bits)
{
    start_virt            = ALIGN_DOWN_POT(start_virt, PAGE_SIZE_AT_LVL(lvl));
    end_virt              = ALIGN_UP_POT(end_virt, PAGE_SIZE_AT_LVL(lvl));
    uint64_t num_bytes    = end_virt - start_virt;
    uint64_t pages_mapped = 0;

    // //debugf("mmu_map_range: Attempting to map %ld bytes at page level %d...\n", num_bytes, lvl);

    uint64_t i;
    for (i = 0; i < num_bytes; i += PAGE_SIZE_AT_LVL(lvl)) {
        if (!mmu_map(tab, start_virt + i, start_phys + i, lvl, bits)) {
            break;
        }
        pages_mapped += 1;
    }

    // //debugf("mmu_map_range: Successfully allocated %ld pages!\n\n", pages_mapped);

    return pages_mapped;
}
