#include <compiler.h>
#include <config.h>
#include <csr.h>
#include <kmalloc.h>
#include <list.h>
#include <lock.h>
#include <sbi.h>  // sbi_xxx()
#include <symbols.h>
#include <util.h>  // strcmp
#include <debug.h>
#include <mmu.h>
#include <page.h>
#include <uaccess.h>
#include <pci.h>
#include <rng.h>
#include <input.h>
#include <input-event-codes.h>
#include <block.h>
#include <gpu.h>
#include <minix3.h>
#include <process.h>
#include <sched.h>
#include <drawing.h>
#include <stat.h>
#include <elf.h>

// Global MMU table for the kernel. This is used throughout
// the kernel.
// Defined in src/include/mmu.h
struct page_table *kernel_mmu_table;

static void init_systems(void)
{
    void plic_init(void);
#ifdef USE_PLIC
    plic_init();
#endif
    void page_init(void);
    // page_init();
#ifdef USE_HEAP
    void heap_init(void);
    void sched_init(void);
    void *kmalloc(uint64_t size);
    void *kcalloc(uint64_t elem, uint64_t size);
    void kfree(void *ptr);
    void util_connect_galloc(void *(*malloc)(uint64_t size),
                             void *(*calloc)(uint64_t elem, uint64_t size),
                             void (*free)(void *ptr));
    util_connect_galloc(kmalloc, kcalloc, kfree);
    heap_init();
#endif
#ifdef USE_PCI
    pci_init();
#endif
#ifdef USE_VIRTIO
    virtio_init();
#endif
}

static const char *hart_status_values[] = {"NOT PRESENT", "STOPPED", "STARTING", "RUNNING"};
#ifdef RUN_INTERNAL_CONSOLE
static void console(void);
#endif

void main(unsigned int hart)
{
    // Initialize the page allocator
    // Allocate and zero the kernel's page table.
    page_init();

    // Kind of neat to see our memory mappings to ensure they make sense.
    logf(LOG_INFO, "[[ MEMORY MAPPINGS ]]\n");
    logf(LOG_INFO, "  [TEXT]  : 0x%08lx -> 0x%08lx\n", sym_start(text), sym_end(text));
    logf(LOG_INFO, "  [BSS]   : 0x%08lx -> 0x%08lx\n", sym_start(bss), sym_end(bss));
    logf(LOG_INFO, "  [RODATA]: 0x%08lx -> 0x%08lx\n", sym_start(rodata), sym_end(rodata));
    logf(LOG_INFO, "  [DATA]  : 0x%08lx -> 0x%08lx\n", sym_start(data), sym_end(data));
    logf(LOG_INFO, "  [STACK] : 0x%08lx -> 0x%08lx\n", sym_start(stack), sym_end(stack));
    logf(LOG_INFO, "  [HEAP]  : 0x%08lx -> 0x%08lx\n", sym_start(heap), sym_end(heap));

    logf(LOG_INFO, "[[ HART MAPPINGS ]]\n");
    for (unsigned int i = 0; i < MAX_ALLOWABLE_HARTS; i++) {
        if (i == hart) {
            logf(LOG_INFO, "  [HART#%d]: %s (this HART).\n", i, hart_status_values[sbi_hart_get_status(i)]);
        }
        else {
            logf(LOG_INFO, "  [HART#%d]: %s.\n", i, hart_status_values[sbi_hart_get_status(i)]);
        }
    }

    struct page_table *pt    = mmu_table_create();
    kernel_mmu_table = pt;
    // Map memory segments for our kernel
    mmu_map_range(pt, sym_start(text), sym_end(heap), sym_start(text), MMU_LEVEL_1G,
                  PB_READ | PB_WRITE | PB_EXECUTE);
    // PLIC
    mmu_map_range(pt, 0x0C000000, 0x0C2FFFFF, 0x0C000000, MMU_LEVEL_2M, PB_READ | PB_WRITE);
    // PCIe ECAM
    mmu_map_range(pt, 0x30000000, 0x30FFFFFF, 0x30000000, MMU_LEVEL_2M, PB_READ | PB_WRITE);
    // PCIe MMIO
    mmu_map_range(pt, 0x40000000, 0x4FFFFFFF, 0x40000000, MMU_LEVEL_2M, PB_READ | PB_WRITE);

#ifdef USE_MMU
    // TODO: turn on the MMU when you've written the src/mmu.c functions
    CSR_WRITE("satp", SATP_KERNEL); 
    SFENCE_ALL();
#endif

    // MMU is turned on here.

    // Initialize all submodules here, including PCI, VirtIO, Heap, etc.
    // Many will require the MMU, so write those functions first.
    init_systems();

    // Now that all submodules are initialized, you need to schedule the init process
    // and the idle processes for each HART.
    logf(LOG_INFO, "Congratulations! You made it to the OS! Going back to sleep.\n");
    logf(LOG_INFO, 
        "The logf function in the OS uses sbi_putchar(), so this means ECALLs from S-mode are "
        "working!\n");
    logf(LOG_INFO, 
        "If you don't remember, type CTRL-a, followed by x to exit. Make sure your CAPS LOCK is "
        "off.\n");

    /* Testing Zone */
   
    // //debugf("TEST ZONE\n");
    // //debugf("main: kmalloc translated from 0x1c0ffee000 to %x\n", mmu_translate(pt, 0x1c0ffee000)); //test mapped kmalloc space
    // //debugf("main: kmalloc translated from 0x1c0ffee001 to %x\n", mmu_translate(pt, 0x1c0ffee001)); //test mapped kmalloc space
    // //debugf("main: kmalloc translated from 0x1c0ffef000 to %x\n", mmu_translate(pt, 0x1c0ffef000)); //test mapped kmalloc space
    
    // //allocate user page and map with user | read | write
    // void* vaddr = 0x100000d000UL;
    // void* user_page = page_alloc(); //test copy_From
    // //debugf("main: alloced user page %x\n", user_page);
    // struct page_table* user_pt = mmu_table_create();
    // //debugf("main: mapping func %d\n", mmu_map(user_pt, vaddr, user_page, MMU_LEVEL_4K, PB_USER | PB_READ | PB_WRITE));
    // //debugf("main: translate from %lx to %x\n", vaddr, mmu_translate(user_pt, vaddr));

    // //set value in kernel space and copy to virt addr
    // void* phys_addr = 0x80029000;
    // *(unsigned long*) phys_addr = 0xdeadbeef;   
    // //debugf("main: bytes copied to %d\n", copy_to(vaddr, user_pt, 0x80029000, PAGE_SIZE));
    // //debugf("main: made it\n");
    // //debugf("main: value at %x %x\n", mmu_translate(user_pt, vaddr), *(long*)(mmu_translate(user_pt, vaddr)));

    // //set value in user space and copy from it to kernel space
    // *(unsigned long*) mmu_translate(user_pt, vaddr) = 0xabcdeff1;
    // //debugf("main: val before at %x is %x\n", 0x80029000, *(unsigned long*)0x80029000);
    // //debugf("main: bytes copied from %d\n", copy_from(0x80029000, user_pt, vaddr, PAGE_SIZE));
    // //debugf("main: val at %x is %x\n", 0x80029000, *(unsigned long*)0x80029000);

    // //try two pages
    // void* vaddr2 = 0x10000e0000UL;
    // void* user_page2 = page_nalloc(2);
    // struct page_table* user_pt_2 = mmu_table_create();
    // mmu_map_range(user_pt_2, vaddr2, vaddr2 + PAGE_SIZE*2, user_page2, MMU_LEVEL_4K, PB_USER | PB_WRITE | PB_READ);
    // //debugf("2 pages starting at %08x\n", user_page2);
    // //debugf("table starting at %08x\n", user_pt_2);
    
    // //set two vals
    // *(unsigned long*) mmu_translate(user_pt_2, vaddr2 + 16) = 0xb00beeee;
    // *(unsigned long*) mmu_translate(user_pt_2, vaddr2 + PAGE_SIZE) = 0xd000000d;
    // //debugf("main: vals before at page bounds at addr %lx are %x and %x\n", vaddr2, *(unsigned long*)0x80029000, *(unsigned long*)(0x8002a000 - 16));
    // //debugf("main: bytes copied from %d\n", copy_from(0x80029000, user_pt_2, vaddr2 + 16, PAGE_SIZE * 2));
    // //debugf("main: vals at page bounds at addr %lx are %x and %x\n", vaddr2, *(unsigned long*)0x80029000, *(unsigned long*)(0x8002a000 - 16));
    // //debugf("main: addrs are %x and %x\n", mmu_translate(user_pt_2, vaddr2), mmu_translate(user_pt_2, vaddr2 + PAGE_SIZE));

    // *(unsigned long*) mmu_translate(user_pt_2, vaddr2 + PAGE_SIZE + 100) = 0xfaceface0UL;
    // //debugf("main: val before at %x is %x\n", mmu_translate(user_pt_2, vaddr2), *(unsigned long*)mmu_translate(user_pt_2, vaddr2));
    // //debugf("main: bytes copied to %d\n", copy_to(vaddr2, user_pt_2, mmu_translate(user_pt_2, vaddr2 + PAGE_SIZE + 100), 16));
    // //debugf("main: val at %x is %lx\n", mmu_translate(user_pt_2, vaddr2), *(unsigned long*)mmu_translate(user_pt_2, vaddr2));
    // //debugf("main: addrs are %x and %x\n", mmu_translate(user_pt_2, vaddr2), mmu_translate(user_pt_2, vaddr2 + PAGE_SIZE + 100));
    
    // TEST RNG
    // char bytes[5] = {0};
    // rng_fill(bytes, sizeof(bytes));
    // //debugf("%02x %02x %02x %02x %02x\n", bytes[0], bytes[1], bytes[2], bytes[3], bytes[4]);
    // rng_fill(bytes, sizeof(bytes));
    // // WFI();
    // //debugf("%02x %02x %02x %02x %02x\n", bytes[0], bytes[1], bytes[2], bytes[3], bytes[4]);

    // for(int i = 0; i < 99999; i++) {
    //     rng_fill(bytes, sizeof(bytes));
    //     //debugf("%d\n", i);
    // }

    // TEST BLOCK
    // uint8_t *data = kzalloc(1024);
    // data[0] = 0xf0;
    // data[1] = 0xab;
    // data[2] = 0xcd;
    // if(block_write_wrapper(g_bdev, (uint64_t)data, 511, 3)) //debugf("main: disk write successful!\n");
    // if(block_read_wrapper(g_bdev, (uint64_t)data, 509, 6)) //debugf("main: disk read successful!\n");
    // // block_read(bdev, (uint64_t)data, 517, 3);
    // if(block_flush(g_bdev)) //debugf("main: disk flush successful!\n");

    // if(block_read(g_bdev, (uint64_t)data, 0, 1024)) //debugf("main: disk read successful!\n");
    // //debugf("main: data bytes from block ops %x %x %x %x %x %x\n", data[0], data[1], data[2], data[3], data[4], data[5]);

    
    // test minix3
    // while(1){ debugf("t\n");}
    minix3_init_system(g_block_devices);
    // int fd_test;
    // char test_path[256] = "/home/cosc562/test_0.txt\0";
    // char test_buf[10000];
    // fd_test = open(test_path, O_CREAT | O_RDWR, S_FMT(S_IFREG));
    // for(int i = 0; i < 1024; i++){
    //     test_buf[i] = 'a';
    // }
    // test_buf[9999] = '\0';
    // debugf("buf: \n%s\n", test_buf);
    // int bytes_written = write(fd_test, test_buf, 9999);

    // char test_path[256] = "/home/cosc562/test_0.txt\0";
    // char test_path[256] = "gulliver.txt\0";
    // cacheEntry* test_entry = minix3_search_cache_wrapper(test_path);

    // debugf("Searching cache for: %s\n", test_path);
    // if(test_entry != NULL){
    //     debugf("Path %s found in cache.\n", test_path);
    // }else{
    //     debugf("Path %s not found in cache.\n", test_path);
    // }
    // debugf("num of block devices: %d\n", list_size(g_block_devices));

    // debugf("test_path mode: %x\n", test_entry->cached_inode.mode);

    // char test_path[256] = "/home/cosc562/test_0.txt\0";
    // char gulliver[15] = "/gulliver.txt\0";
    
    // int file_desc_test = open(test_path, O_CREAT | O_RDWR, S_FMT(S_IFREG));
    // int file_desc_gul = open(gulliver, O_RDWR, S_FMT(S_IFREG));
    // // debugf("file_desc: %d\n", file_desc_test);
    // debugf("file_desc: %d\n", file_desc_gul);

    // char gulliver_buf[10046];
    // gulliver_buf[10045] = '\0';
    // int gull_read = read(file_desc_gul, gulliver_buf, 10045);
    // debugf("gull_read: %d\n", gull_read);
    // debugf("gull_file:\n%s\n", gulliver_buf);

    // int gull_write = write(file_desc_test, gulliver_buf, 10045);
    // debugf("gull_write: %d\n", gull_write);
    // int test_closed = close(file_desc_test);
    // file_desc_test = open(test_path, O_RDWR, S_FMT(S_IFREG));
    // debugf("file_desc_test: %d\n", file_desc_test);
    // gull_read = read(file_desc_test, gulliver_buf, 10045);
    
    // debugf("New_file:\n%s\n", gulliver_buf);
    // debugf("gull_write: %d\n", gull_write);
    // debugf("gull_read: %d\n", gull_read);

    // int written_size = 0;
    // for(int i =0; i < 181; i++){
    //     written_size = write(file_desc, &test_buf, 12);
    //     debugf("Written_size: %d\n", written_size);
    // }


    


    // char larger_test_buf[2049];
    // for(int i =0; i < 2048; i++){
    //     larger_test_buf[i] = 'a';
    // }
    // written_size = write(file_desc, &larger_test_buf, 2048);
    // debugf("Written_size: %d\n", written_size);

    // int file_closed = close(file_desc);
    // debugf("file_closed: %d\n", file_closed);


    // file_desc = open(test_path, O_RDWR, S_FMT(S_IFREG));
    // debugf("file_desc: %d\n", file_desc);

    // char read_buf[4220 + 1];
    // read_buf[4221] = '\0';
    // int read_bytes = read(file_desc, read_buf, 4220);
    // debugf("read_bytes: %d\n", read_bytes);
    // // read_buf[1024] = '\0';
    // debugf("read_buf:\n%s\n", read_buf);

    // debugf("file_desc: %d\n", file_desc);
    // int test_size = 3 * 1024;
    // char test[test_size+1];   // = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Integer at commodo sapien. Vestibulum scelerisque nunc non mauris vestibulum, vel vehicula leo tincidunt. Donec sit amet consectetur turpis. Praesent vestibulum sapien a magna facilisis venenatis. Sed cursus tellus ac eros facilisis, in tincidunt tellus hendrerit. Sed vestibulum, nunc sit amet varius sollicitudin, odio nisl volutpat mi, in auctor felis sapien eget libero. Aliquam efficitur metus nec diam suscipit congue. Sed ac nunc in quam hendrerit suscipit. Nullam nec dui eu leo malesuada consectetur. Cras a justo at enim venenatis vestibulum. Fusce a ultrices turpis. Vivamus fermentum justo a arcu hendrerit, eu iaculis ipsum dignissim. Duis vehicula lorem ut sapien viverra, at lacinia ante fermentum. Maecenas eget elit vel urna efficitur tincidunt. Vivamus eu euismod elit. Etiam malesuada arcu quis turpis feugiat, in interdum libero cursus. Fusce ac elit nec odio malesuada malesuada vel vel nulla. Morbi ut risus nec arcu dignissim euismod ac ut eros. In hac habitasse platea dictumst.";
    // test[test_size] = '\0';
    // for(int i = 0; i < 1024; i++){
    //     test[i] = 'a';
    // }
    // for(int i = 1024; i < 2048; i++){
    //     test[i] = 'b';
    // }
    // for(int i = 2048; i < 2048 + 1024; i++){
    //     test[i] = 'c';
    // }

    // debugf("test:\n%s\n", test);
    // int size;
    // for(int i = 0; i < 10000; i++){
    //     size = write(file_desc, &test, test_size);
    //     debugf("size written: %d\n", size);
    // }
    // size = write(file_desc, test, 1024);
    // 
    // cacheEntry* test_entry = minix3_search_cache_wrapper(test_path);
    // debugf("Searching cache for: %s\n", test_path);
    // if(test_entry != NULL){
    //     debugf("Path %s found in cache.\n", test_path);
    // }else{
    //     debugf("Path %s not found in cache.\n", test_path);
    // }
    // debugf("file descriptor: %d\n", file_desc);
    // minix3_parse_path(test_path);
    // minix3_get_parent_path(test_path);
    
    //Here we can test the system calls now that the cache system has been set up

    // // TEST GPU / INPUT
    PixelRGBA c1 = { 0, 0, 0, 255 }, c2 = { 255, 255, 255, 255 }, c3 = { 0, 0, 0, 255 };
    PixelRGBA z1 = { 255, 100, 50, 255 };
    PixelRGBA z2 = { 50, 0, 255, 255 };
    PixelRGBA z3 = { 50, 255, 255, 255 };
    PixelRGBA z4 = { 0, 255, 50, 255 };
    PixelRGBA z5 = { 0, 0, 0, 255 };
    Rectangle r1 = { 0, 0, g_gdev->width, g_gdev->height };
    Rectangle r2 = { 100, 100, g_gdev->width - 200, g_gdev->height - 200 };
    uint64_t t = 0;
    bool start = false;
    signed char key;

    fb_fill_rect(g_gdev->width, g_gdev->height, g_gdev->framebuffer, &r1, &z5);
    fb_fill_string(" mongOS", 2, 2, g_gdev, g_gdev->width, g_gdev->height, g_gdev->framebuffer, &z5, &z1, 8, 4);
    ppm_read_fb("/images/Among-Us-Red-Crewmate_ppm.ppm", g_gdev, 130, 140);
    fb_stroke_rect(g_gdev->width, g_gdev->height, g_gdev->framebuffer, &r2, &z1, 10);
    fb_stroke_string("cuhraaaaazyyy", 20, 40, g_gdev, g_gdev->width, g_gdev->height, g_gdev->framebuffer, &z3, 1, 1);

    while(!start) {
        fb_fill_string("press any key to continue...", 9, 7, g_gdev, g_gdev->width, g_gdev->height, g_gdev->framebuffer, &c1, &c2, 2, 2);
        rng_fill(&c1, 3);
        rng_fill(&c2, 3);

        // input_handle(g_keydev);

        // //Here, get every event from the ring buffer, and figure out how to print it to the screen!

        // //This would test our keyboard :)

        // // debugf("g_keydev->ring_buffer: 0x%x\n", g_keydev->ring_buffer);

        // uint32_t event_ring_size = ring_size(g_keydev->ring_buffer);
        // // debugf("Ring size: %d\n", event_ring_size);
        // uint32_t current_event_idx = 0;
        // InputEvent curr_event;
        // uint16_t last_character = 0;
        // uint16_t current_character = 0;
        // for(current_event_idx = 0; current_event_idx < event_ring_size; current_event_idx++){
            
        //     // printf("nextEvent:\n");
        //     uint64_t raw_event = input_ring_buffer_pop(g_keydev);

        //     curr_event.type = (uint16_t)(raw_event & 0xFFFF);  // Extract the lower 16 bits of after_pop as the type
            
        //     if(curr_event.type == (uint16_t)0){
        //         // debugf("Skipping sync\n");
        //         continue;
        //     }

        //     curr_event.code = (uint16_t)((raw_event >> 16) & 0xFFFF); // Extract the next 16 bits as the code
            
        //     current_character = curr_event.code;
            
        //     curr_event.value = (uint32_t)((raw_event >> 32) & 0xFFFFFFFF); // Extract the upper 32 bits as the value
        //     // debugf("n\n");
        //     if(curr_event.value == key_pressed){
        //         last_character = curr_event.value;
        //     }
        //     else if(curr_event.value == key_released){  
        //         printf("%c", keys[curr_event.code]);
        //         fb_fill_char(keys[curr_event.code], 5, 5, g_gdev->width, g_gdev->height, g_gdev->framebuffer, &c2, &c1, 3, 3);
        //         gpu_redraw(&r1, g_gdev); // Will redraw it jankily sometime before updating the frame again with the new colors. This can go elsewhere later.
        //     }
        // }
        
        key = get_keyboard();
        while(key != 0) {
            if(key < 0) {
                start = true;
                ppm_read_fb("/images/1000002127.ppm", g_gdev, 105, 105);
                fb_fill_string("   starting user process!   ", 9, 7, g_gdev, g_gdev->width, g_gdev->height, g_gdev->framebuffer, &z1, &z5, 2, 2);
            }
            key = get_keyboard();
        }

        gpu_redraw(&r1, g_gdev);
    }

    /******************
     * TEST PROCESSES
    */

    /*******
     *  TEST LOADING ELF PROC FROM DSK
     * 
    */
    int elf_size = 19488; // 19320 = console.elf
    int elf_fd = open("/user/user_drawing.elf", O_RDONLY, 666);
    char *elf_buf = kzalloc(elf_size);
    read(elf_fd, elf_buf, elf_size);

    for(uint32_t i = 0; i < elf_size; i++) {
        debugf("%u: %02x\n", i, elf_buf[i]);
    }

    sched_init();
    // bool test_bool = true;    
    // while(test_bool) continue;
    // sched_invoke(0);

    // drawing_proc();

    /* Testing Zone */


    // Below is just a little shell that demonstrates the sbi_getchar and
    // how the console works.

    // This is defined above main()
#ifdef RUN_INTERNAL_CONSOLE
    console();
#else
    extern uint32_t *elfcon;
    Process *con = process_new(PM_USER);
    // if (!elf_load(con, elfcon)) {
    if (!elf_load(con, elf_buf)) {
        logf(LOG_INFO, "PANIC: Could not load init.\n");
        WFI_LOOP();
    }
    debugf("main: epc %x\n", mmu_translate(con->ptable, con->frame.sepc));
    sched_add(con);
    con->state = PS_RUNNING;
    sched_invoke(0);
#endif
}

#ifdef RUN_INTERNAL_CONSOLE
ATTR_NORET static void console(void)
{
    const int BUFFER_SIZE = 56;
    int at                = 0;
    char input[BUFFER_SIZE];
    logf(LOG_TEXT, "> ");
    do {
        char c;
        // Recall that sbi_getchar() will return -1, 0xff, 255
        // if the receiver is empty.
        if ((c = sbi_getchar()) != 0xff) {
            if (c == '\r' || c == '\n') {
                if (at > 0) {
                    input[at] = '\0';
                    if (!strcmp(input, "quit")) {
                        logf(LOG_TEXT, "\nShutting down...\n\n");
                        sbi_poweroff();
                    }
                    else if (!strcmp(input, "fatal")) {
                        logf(LOG_TEXT, "\n");
                        fatalf("Testing fatal error @ %lu.\nHanging HART...\n", sbi_rtc_get_time());
                        logf(LOG_ERROR, "If I get here, fatal didn't work :'(.\n");
                    }
                    else if (!strcmp(input, "heap")) {
                        logf(LOG_TEXT, "\n");
                        void heap_print_stats(void);
                        heap_print_stats();
                    }
                    else {
                        logf(LOG_TEXT, "\nUnknown command '%s'\n", input);
                    }
                    at = 0;
                }
                logf(LOG_TEXT, "\n> ");
            }
            else if (c == 127) {
                // BACKSPACE
                if (at > 0) {
                    logf(LOG_TEXT, "\b \b");
                    at -= 1;
                }
            }
            else if (c == 0x1B) {
                // Escape sequence
                char esc1 = sbi_getchar();
                char esc2 = sbi_getchar();
                if (esc1 == 0x5B) {
                    switch (esc2) {
                        case 0x41:
                            logf(LOG_INFO, "UP\n");
                            break;
                        case 0x42:
                            logf(LOG_INFO, "DOWN\n");
                            break;
                        case 0x43:
                            logf(LOG_INFO, "RIGHT\n");
                            break;
                        case 0x44:
                            logf(LOG_INFO, "LEFT\n");
                            break;
                    }
                }
            }
            else {
                if (at < (BUFFER_SIZE - 1)) {
                    input[at++] = c;
                    logf(LOG_TEXT, "%c", c);
                }
            }
        }
        else {
            // We can WFI here since interrupts are enabled
            // for the UART.
            WFI();
        }
    } while (1);
}
#endif
