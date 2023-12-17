#include <page.h>
#include <lock.h>
#include <stddef.h>
#include <util.h>
#include <symbols.h>
#include <debug.h>

// #define DEBUG_PAGE

// Do NOT hold the lock any longer than you have to!
Mutex page_lock;

/* 
 * Write your static functions here.
*/

/**
 * @brief Tests the two bookkeeping bits for the specified page number.
 *
 * @param page_idx The number of pages from the start of the heap.
 * @return int The state of the bits (0, 2, or 3).
 */
static int bk_test(int page_idx) {
    /* Return 0, 2, or 3 based on the state of the bits. */
    uint64_t heap_start = sym_start(heap);

    uint64_t byte_num = page_idx / 4;
    uint64_t bit_num = (page_idx % 4) * 2;
    
    return ((*((uint8_t *)(heap_start + byte_num))) >> bit_num) & 0x3;
}

/**
 * @brief Sets the "taken" or both of the bookkeeping bits for the specified page number.
 *
 * @param page_idx The number of pages from the start of the heap.
 * @param last Whether the last bit should also be set.
 */
static void bk_set(int page_idx, bool last) {
    /* Set bits according to `last` param. ASSUMES THE LAST BIT IS ALREADY CLEARED! */
    uint64_t heap_start = sym_start(heap);

    uint64_t byte_num = page_idx / 4;
    uint64_t bit_num = (page_idx % 4) * 2;

    *((uint8_t *)(heap_start + byte_num)) |= (0x2 + ((last == 0) ? 0 : 1)) << bit_num;
}

/**
 * @brief Clears the two bookkeeping bits for the specified page number.
 *
 * @param page_idx The number of pages from the start of the heap.
 */
static void bk_clear(int page_idx) {
    /* Set both bits to 0. */
    uint64_t heap_start = sym_start(heap);

    uint64_t byte_num = page_idx / 4;
    uint64_t bit_num = (page_idx % 4) * 2;

    *((uint8_t *)(heap_start + byte_num)) &= ~(0x3 << bit_num);
}

/**
 * @brief Maps a page address to its page number/index.
 *
 * @param page_addr The address of the beginning of a page.
 * @return int The number of pages from the start of the heap.
 */
static int map_page_to_idx(uint64_t page_addr) {
    uint64_t heap_start = sym_start(heap);
    uint64_t heap_end = sym_end(heap);

    /* This page address is invalid if it is outside of the heap or not page-aligned. */
    if(page_addr >= heap_end || (page_addr & 0xfff) != 0) return -1;

    return (page_addr - heap_start) / PAGE_SIZE;
}

void page_init(void)
{
    /* Initialize the page system. */
    uint64_t heap_start = sym_start(heap);
    uint64_t heap_end = sym_end(heap);
    uint64_t num_pages = (heap_end - heap_start) / PAGE_SIZE;
    uint64_t bk_size = ALIGN_UP_POT(num_pages / 4, PAGE_SIZE);
    // uint64_t page_start = heap_start + bk_size;
    uint64_t num_pages_bk = bk_size / PAGE_SIZE;
    // uint64_t num_pages_actual = num_pages - num_pages_bk;

    mutex_spinlock(&page_lock);

    //debugf("page_init: Heap start at 0x%lx.\n", heap_start);
    //debugf("page_init: Heap end at 0x%lx.\n", heap_end);
    //debugf("page_init: Heap size is %lu bytes.\n", heap_end - heap_start);
    //debugf("page_init: Init %lu pages using %lu bookkeeping bytes.\n", num_pages, bk_size);
    // //debugf("page_init: First allocatable page at 0x%lx.\n", page_start);
    // //debugf("page_init: Init %lu actual pages.\n", num_pages_actual);
    
    /* Initialize page allocator by zeroing the bookkeeping bytes for
     * `num_pages` pages (plus the alignment) and setting the "taken" bits for
     * pages already associated with bookkeeping.
    */
    memset((void *)heap_start, 0, bk_size);
    for(uint64_t i = 0; i < num_pages_bk; i++) {
        bk_set(i, true); // set "last" so page_free can free the first allocatable page
    }

    mutex_unlock(&page_lock);

    /* Test functions here. */
    // page_nalloc(5);
    // void *p = page_nalloc(2);
    // page_nalloc(1);
    // page_nalloc(2);
    // page_nalloc(7);
    // page_free(p + PAGE_SIZE);
    // page_free(p + 2*PAGE_SIZE);
    // page_free(p);
    // page_nalloc(1);
    // page_nalloc(99999);
    // //debugf("page_init: Pages taken: %d.\n", page_count_taken());
    // //debugf("page_init: Pages free: %d.\n", page_count_free());
     
    // for(int i = 0; i < 20; i++) {
    //     //debugf("page_init: Value at 0x%lx: 0x%x\n", heap_start + i, *((uint8_t *)(heap_start + i)));
    //     //debugf("page_init: bk_test at %d: %d\n", i, bk_test(i));
    // }
}

void *page_nalloc(int n)
{
    if (n <= 0) {
        return NULL;
    }

    uint64_t heap_start = sym_start(heap);
    uint64_t heap_end = sym_end(heap);
    uint64_t num_pages = (heap_end - heap_start) / PAGE_SIZE;

    int start_indx = -1;
    int end_indx = -1;
    int pages_free = 0;

    mutex_spinlock(&page_lock);

    /* Linearly search for n consecutive free pages. */
    for(uint64_t i = 0; i < num_pages; i++) {    
        /* If >= 2, it's taken. Start over. */
        if(bk_test(i) >= 2) {
            start_indx = -1;
            end_indx = -1;
            continue;
        }

        /* Otherwise, check if start index is not set and save it. */
        if(start_indx == -1) {
            start_indx = i;
        }

        /* Increment pages free. If enough pages, set end index and break. */
        pages_free++;
        if(pages_free == n) {
            end_indx = i;
            break; 
        }
    }

    /* If end index is still not set, n consecutive free pages weren't found--return NULL. */
    if(end_indx == -1) {
        mutex_unlock(&page_lock);
        logf(LOG_ERROR, "page_nalloc: No free chunk of %d pages found!\n", n);
        return NULL;
    }

    // //debugf("page_nalloc: start %d, end %d\n", start_indx, end_indx);

    /* Otherwise, allocate n pages by setting taken and last bits accordingly
     * and returning the page address based on start index.
    */
    for(int i = start_indx; i < end_indx; i++) {
        bk_set(i, false);
    }
    bk_set(end_indx, true);

    mutex_unlock(&page_lock);

    return (void *)(heap_start + start_indx * PAGE_SIZE);
}

void *page_znalloc(int n)
{
    void *mem;
    if (n <= 0 || (mem = page_nalloc(n)) == NULL) {
        return NULL;
    }
    return memset(mem, 0, n * PAGE_SIZE);
}

void page_free(void *p)
{
    if (p == NULL) {
        return;
    }

    /* Free the page */
    uint64_t heap_start = sym_start(heap);
    uint64_t heap_end = sym_end(heap);
    uint64_t num_pages = (heap_end - heap_start) / PAGE_SIZE;

    uint64_t page_addr = (uint64_t) p;
    int page_idx = map_page_to_idx(page_addr);
    int end_idx = page_idx;

    // //debugf("page_free: page_idx %d\n", page_idx);

    if(page_idx < 0 || bk_test(page_idx - 1) == 2){
        logf(LOG_ERROR, "page_free: Invalid page free!\n");
        return;
        //If we have an invalid page return without doing anything
        //quick check to stop us from possibly reading from a memory location we shouldnt from
        /* Also ensure this address is the beginning of an allocated chunk of pages
         * by checking if the previous bit is not JUST "taken."
        */
    }

    while(bk_test(end_idx) != 3 && (uint64_t)end_idx < num_pages - 1) {
        //Since we get 0, 2 or 3 from bk_test where 0 is free and 2 is taken but not the last page
        //3 means last page and taken
        end_idx++;
    }

    mutex_spinlock(&page_lock);

    //clear BK bytes 
    for(int i = page_idx; i <= end_idx; i++){
        bk_clear(i);
    }

    mutex_unlock(&page_lock);
}

int page_count_free(void)
{
    int num_pages_free = 0;

    /* Count free pages in the bookkeeping area */

    /* Don't just take total pages and subtract taken. The point
     * of these is to detect anomalies. You are making an assumption
     * if you take total pages and subtract taken pages from it.
    */
    uint64_t heap_start = sym_start(heap);
    uint64_t heap_end = sym_end(heap);
    uint64_t num_pages = (heap_end - heap_start) / PAGE_SIZE;

    mutex_spinlock(&page_lock);

    for(uint64_t i = 0; i < num_pages; i++) {
        if(bk_test(i) < 2) num_pages_free++;
    }

    mutex_unlock(&page_lock);

    return num_pages_free;
}

int page_count_taken(void)
{
    int num_pages_taken = 0;

    /* Count taken pages in the bookkeeping area */

    /* Don't just take total pages and subtract free. The point
     * of these is to detect anomalies. You are making an assumption
     * if you take total pages and subtract free pages from it.
    */
    uint64_t heap_start = sym_start(heap);
    uint64_t heap_end = sym_end(heap);
    uint64_t num_pages = (heap_end - heap_start) / PAGE_SIZE;

    mutex_spinlock(&page_lock);

    for(uint64_t i = 0; i < num_pages; i++) {
        if(bk_test(i) >= 2) num_pages_taken++;
    }

    mutex_unlock(&page_lock);

    return num_pages_taken;
}
