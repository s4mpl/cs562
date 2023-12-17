#include <minix3.h>
#include <stat.h>
#include <util.h>
#include <path.h>
#include <sbi.h> 

static FileSystem fs;

char** g_cache_path = NULL;
uint64_t g_path_size = 0;
bool g_inode_found = false;

char* g_parent_cache_path_new = NULL;
char** g_parent_cache_path = NULL;
uint64_t g_parent_path_size = 0;

//Intro some locks here I think!

bool y_failed = false;

bool double_allocated = false;

/* THESE FUNCTIONS ASSUME 1-INDEXED ELEMENT NUMBERS. NEED TO MAKE SURE ZONE NUMBERS ARE PASSED IN PROPERLY! */
static bool bitmap_taken(uint64_t bitmap_addr, uint32_t elem_num) {
    elem_num -= 1;
    uint64_t block_offset = elem_num >> 3;
    uint8_t bit_offset = elem_num % 8;
    uint64_t block_check_addr = bitmap_addr + block_offset;
    return *((uint8_t *)block_check_addr) & (1 << bit_offset);
}

static void bitmap_set(uint64_t bitmap_addr, uint32_t elem_num) {
    elem_num -= 1;
    uint64_t block_offset = elem_num >> 3;
    uint8_t bit_offset = elem_num % 8;
    uint64_t block_check_addr = bitmap_addr + block_offset;
    *((uint8_t *)block_check_addr) |= (1 << bit_offset);
}

static void bitmap_clear(uint64_t bitmap_addr, uint32_t elem_num) {
    elem_num -= 1;
    uint64_t block_offset = elem_num >> 3;
    uint8_t bit_offset = elem_num % 8;
    uint64_t block_check_addr = bitmap_addr + block_offset;
    *((uint8_t *)block_check_addr) &= ~(1 << bit_offset);
}

static int find_next_inode(){
    int inode = 0;
    for(int i = 1; i < fs.num_inodes; i++){
        if(bitmap_taken((uint64_t)fs.imap_allocd, i) == false){
            bitmap_set((uint64_t)fs.imap_allocd, i);
            
            block_write_wrapper(g_bdev, fs.imap_allocd, fs.imap_blocks_start, fs.block_size * fs.sb->imap_blocks);
            inode = i;
            break;
        }
    }
    return inode;
}

static uint32_t find_next_datazone(){
    uint32_t zone = 0;
    for(uint32_t i = 0; i < fs.num_zones; i++){
        if(bitmap_taken((uint64_t)fs.zmap_allocd, i + 1) == false){
            bitmap_set((uint64_t)fs.zmap_allocd, i + 1);
            block_write_wrapper(g_bdev, fs.zmap_allocd, fs.zmap_blocks_start, fs.block_size * fs.sb->zmap_blocks);
            zone = (uint32_t)i;
            break;
        }
    }
    if(zone == 0){
        debugf("No more data zones are free!\n");
        while(1){
            
        }
    }
    return zone;
}

void minix3_init_system(List* block_devs){
    ListElem *curr_elem;
    BlockDevice *curr_dev;
    list_for_each(block_devs, curr_elem){
        curr_dev = (BlockDevice*) list_elem_value_ptr(curr_elem);
        minix3_init_dev(curr_dev);
    }
}

static bool inode_get(BlockDevice* bdev, Inode* inode, uint32_t inode_num){
    // //debugf("getting inode number %d\n", inode_num);

	if(inode_num == 0){
		return false;
	}

	uint32_t byte_offset = fs.inodes_start + (inode_num-1) * sizeof(Inode);
    // //debugf("inode 0x%08x, byte offset %d\n", inode, byte_offset);
    // //debugf("READ WRAPPER INODE_GET\n");
    return block_read_wrapper(bdev, (uint64_t)inode, byte_offset, sizeof(Inode));
}

uint64_t file_count = 0;

static uint32_t minix3_setup_cache(BlockDevice* bdev, uint32_t inode_num, Map* current_map){

    debugf("Test\n");
    uint32_t return_num_elements_added = 0;
    char parent_dir[3] = "..\0";
    char current_dir[2] = ".\0";
    char poem_file[9] = "poem.txt\0";
    char test_dir[8] = "cosc562\0";

    Inode curr_inode;
    inode_get(bdev, &curr_inode, inode_num);

    uint32_t num_dir_entries = curr_inode.size / DIR_ENTRY_SIZE;
    Dirent *curr_dir_entry;
    uint32_t data_zone = 0;
    uint32_t num_dir_entries_processed = 0;
    Inode next_dir_inode;
    char directory_block[fs.block_size];
    
    for(uint32_t curr_data_zone = 0; curr_data_zone < ZONE_INDIR; curr_data_zone++){
        //Break out once we reached the number of directory entries!
        if(num_dir_entries == num_dir_entries_processed){
            // //debugf("Done with dir\n");
            break;
        }
    
        if(curr_inode.zones[curr_data_zone] == 0){
            continue;
        }
        else{
            data_zone = curr_inode.zones[curr_data_zone] * fs.block_size;
        }

        bool test = block_read_wrapper(g_bdev, (uint64_t)directory_block, data_zone, fs.block_size);
        if(test == false){
            debugf("Epic failure\n");
            break;
        }
        
        for(uint32_t curr_dir_entry_num = 0; curr_dir_entry_num < fs.block_size / DIR_ENTRY_SIZE; curr_dir_entry_num++){
            //Get next directory entry

            if(num_dir_entries_processed == num_dir_entries) break;
            debugf("A\n");
            curr_dir_entry = (Dirent *)(&(directory_block[DIR_ENTRY_SIZE * curr_dir_entry_num]));
            num_dir_entries_processed++;
            uint32_t curr_dir_entry_inode_num = curr_dir_entry->inode_num;
            if(curr_dir_entry->inode_num != 0){
                
                inode_get(g_bdev, &next_dir_inode, curr_dir_entry_inode_num);

                if(S_ISDIR(next_dir_inode.mode)){

                    if(strcmp(current_dir, curr_dir_entry->name) == 0 || strcmp(parent_dir, curr_dir_entry->name) == 0){
                        //Pass this because we don't need to cache the parent directory or the current directory
                    }
                    else{
                        
                        return_num_elements_added++;
                        cacheEntry* new_cache_entry = (cacheEntry*)kmalloc(sizeof(cacheEntry));
                        new_cache_entry->inode_num = curr_dir_entry_inode_num;
                        // Map* temp = map_new_with_slots(FS_NUM_CACHE_ENTRIES);
                        new_cache_entry->num_entries = 0;
                        new_cache_entry->directory_map = map_new_with_slots(FS_NUM_CACHE_ENTRIES);
                        memcpy((void*)&(new_cache_entry->cached_inode), (const void*)&next_dir_inode, sizeof(Inode));
                        char* cache_entry_name = (char*)kmalloc(DIR_ENTRY_NAME_SIZE);
                        strncpy(cache_entry_name, curr_dir_entry->name, 60);
                        map_set(current_map, cache_entry_name, (MapValue)new_cache_entry);
                        uint32_t num_elements = 0;
                        debugf("Dir: %s\n", curr_dir_entry->name);
                        num_elements = minix3_setup_cache(bdev, curr_dir_entry_inode_num, new_cache_entry->directory_map);
                        debugf("Dir: %s\n", curr_dir_entry->name);
                        List* map_keys = map_get_keys(current_map);
                        new_cache_entry->num_entries = num_elements;
                    }
                }
                else{
                    debugf("FILE: %s\n", curr_dir_entry->name);
                    return_num_elements_added++;
                    file_count++;
                    cacheEntry* new_cache_entry = (cacheEntry*)kmalloc(sizeof(cacheEntry));
                    new_cache_entry->directory_map = NULL;
                    new_cache_entry->num_entries = 0;
                    memcpy((void*)&(new_cache_entry->cached_inode), (const void*)&next_dir_inode, sizeof(Inode));
                    char* cache_entry_name = (char*)kmalloc(DIR_ENTRY_NAME_SIZE);
                    strncpy(cache_entry_name, curr_dir_entry->name, 60);
                    map_set(current_map, cache_entry_name, (uint64_t)new_cache_entry);
                }
            }
        }
    }

return return_num_elements_added;
}

// Inode* minix3_search_cache(Map* current_directory_map, uint16_t directory_level){

//     uint32_t current_dir_list_size = map_size(current_directory_map);
//     List* map_keys = map_get_keys(current_directory_map);

//     if(list_size(map_keys) == 0){
//         debugf("Failure to get Map!\n");
//         return;
//     }

//     ListElem *e;
//     cacheEntry* current_cache_entry;

//     MapValue map_val;

//     Inode current_inode;
    
//     char tab_level[directory_level+1];
//     tab_level[directory_level] = '\0';
//     for(uint16_t i = 0; i < directory_level; i++){
//         tab_level[i] = '\t';
//     }

//     char* current_key = NULL;
//     list_for_each_ascending(map_keys, e){

//         current_key = (char*)list_elem_value(e);
//         debugf("%scurrent_key: %s\n", tab_level, current_key);

//         bool test = map_get(current_directory_map, current_key, &map_val);
//         current_cache_entry = (cacheEntry*)(map_val);
//         if(S_ISDIR(current_cache_entry->cached_inode.mode)){
//             directory_level += 1;
//             minix3_search_cache(current_cache_entry->directory_map, directory_level);
//             directory_level -= 1;
//         }else{
//             // debugf("File: %s\n", current_key);
//         }
//     }
// }

cacheEntry* minix3_search_cache(Map* current_directory_map, uint16_t directory_level){

    if(current_directory_map == NULL){
        debugf("Failure at level %d\n", directory_level);
    }

    uint32_t current_dir_list_size = map_size(current_directory_map);

    debugf("Map size: %d\n", current_dir_list_size);
    
    List* map_keys = map_get_keys(current_directory_map);
    // List* map_keys;
    debugf("test\n");
    debugf("Directory level: %d\n", directory_level);

    if(list_size(map_keys) == 0){
        debugf("Failure to get Map!\n");
        return NULL;
    }

    ListElem *e;
    cacheEntry* current_cache_entry;
    cacheEntry* return_cache_entry;
    MapValue map_val;

    Inode current_inode;
    
    char tab_level[directory_level+1];
    tab_level[directory_level] = '\0';
    for(uint16_t i = 0; i < directory_level; i++){
        tab_level[i] = '\t';
    }

    char* current_key = NULL;
    list_for_each_ascending(map_keys, e){

        current_key = (char*)list_elem_value(e);
        //debugf("%scurrent_key: %s\n", tab_level, current_key);

        map_get(current_directory_map, current_key, &map_val);
        current_cache_entry = (cacheEntry*)(map_val);

        uint64_t path_match = strcmp(current_key, g_cache_path[directory_level]);
        
        debugf("strcmp(%s, %s), dir_level = %d, g_path_size = %d\n", current_key, g_cache_path[directory_level], directory_level, g_path_size);

        //Key does not match current level in cache_path
        if (path_match != 0){
            continue;
        }//If we reached the end of the path, return the cache entry at that point
        else if (directory_level == (g_path_size - 1)){
            debugf("%sFound file: %s\n", tab_level, current_key);
            g_inode_found = true;
            return current_cache_entry;
        }//Otherwise, continue traversing further down the cache
        else{
            if(S_ISDIR(current_cache_entry->cached_inode.mode)){

                directory_level += 1;
                // debugf("%sNext_dir: %s\n", tab_level, current_key);
                debugf("\t\t\t\t\t\t\t\t\t\tMap: %x, key: %s\n", current_cache_entry->directory_map, current_key);
                return_cache_entry = minix3_search_cache(current_cache_entry->directory_map, directory_level);
                directory_level -= 1;
                if(g_inode_found == true){
                    return return_cache_entry;
                }
            }

        }
    }
    return NULL;
}

void minix3_get_parent_path(char* path){

    List* test_path_split = path_split(path);
    
    uint64_t path_size = list_size(test_path_split);
    g_parent_path_size = path_size - 1;
    ListElem *e;
    char* current_path_element = NULL;
    g_parent_cache_path = (char**)kmalloc(sizeof(char*) * g_parent_path_size);
    int i = 0;
    list_for_each_ascending(test_path_split, e){
        current_path_element = (char*)list_elem_value(e);
        g_parent_cache_path[i] = strdup((const char*)current_path_element);
        i++;
        if(i == path_size - 1) break;
    }

    path_split_free(test_path_split);

    for(int i = 0; i < g_parent_path_size; i++){
        debugf("PATH: %s\n", g_parent_cache_path[i]);
    }

    g_parent_cache_path_new = (char*)kmalloc(60);
    
    for(int i = 0; i < 60; i++){
        g_parent_cache_path_new[i] = '\0';
    }

    int totalLength = 0;
    for (int i = 0; i < g_parent_path_size; i++) {
        totalLength += strlen(g_parent_cache_path[i]) + 1; // +1 for the '/' or null terminator
    }

    // Start combining strings
    g_parent_cache_path_new[0] = '/'; // Start with an empty string
    int currentPosition = 1; // Track the current position in the combined string

    for (int i = 0; i < g_parent_path_size; i++) {
        strcpy(g_parent_cache_path_new + currentPosition, g_parent_cache_path[i]);
        currentPosition += strlen(g_parent_cache_path[i]);

        if (i < g_parent_path_size - 1) {
            g_parent_cache_path_new[currentPosition] = '/';
            currentPosition++;
        }
    }
    g_parent_cache_path_new[currentPosition] = '\0';

    debugf("Full string: %s\n", g_parent_cache_path_new);

}

void minix3_parse_path(char* path_to_search){

    List* test_path_split = path_split(path_to_search);
    uint64_t path_size = list_size(test_path_split);

    g_path_size = path_size;
    ListElem *e;
    char* current_path_element = NULL;
    g_cache_path = (char**)kmalloc(sizeof(char*) * g_path_size);
    int i = 0;
    list_for_each_ascending(test_path_split, e){
        current_path_element = (char*)list_elem_value(e);
        g_cache_path[i] = strdup((const char*)current_path_element);
        i++;
    }

    path_split_free(test_path_split);

    for(int i = 0; i < g_path_size; i++){
        debugf("PATH: %s\n", g_cache_path[i]);
    }

}

cacheEntry* minix3_search_cache_wrapper(char* path_to_search){

    cacheEntry* return_cache_entry;

    minix3_parse_path(path_to_search);
    debugf("Path parsed\n");
    g_inode_found = false;
    return_cache_entry = minix3_search_cache(fs.dirent_cache, 0);
    debugf("Found: %d\n", g_inode_found);

    g_inode_found = false;

    for(int i = 0; i < g_path_size; i++){
        kfree(g_cache_path[i]);
    }
    kfree(g_cache_path);

    return return_cache_entry;
}

static void iterate_over_dirent_block(BlockDevice* b_dev, uint32_t zone_offset){
    struct minix3_dir_entry dirent;
    for(int j = 0; j < (fs.block_size / sizeof(struct minix3_dir_entry)); j++){
        uint64_t dirent_offset = zone_offset * fs.block_size + j * sizeof(struct minix3_dir_entry);
        // //debugf("dirents_get: offset 0x%x size 0x%x\n", dirent_offset, sizeof(struct minix3_dir_entry));
        // if(!block_read_wrapper(b_dev, &dirent, dirent_offset, sizeof(struct minix3_dir_entry))) debugf("problem\n");
        // //debugf("dirents_get: dirent addr 0x%x inode_num %d name %s\n", dirent_offset, dirent.inode_num, dirent.name);
    }
}

static uint16_t dirents_get(BlockDevice* b_dev, struct minix3_inode* inode, struct minix3_dir_entry* dirent){
    uint32_t num_pointers = fs.block_size / 4;

    for(int i = 0; i < 10; i++){
        if(inode->zones[i] == 0){
            continue;
        }

        // //debugf("dirents_get: zone ptr %d val 0x%x\n", i, inode->zones[i]);
        if(i < ZONE_INDIR){
            // //debugf("dirents_get: direct pointer !\n");
            iterate_over_dirent_block(b_dev, inode->zones[i]);
        }
        else if(i == ZONE_INDIR){
            // //debugf("dirents_get: singly-indirect !\n");
            uint32_t* spointer = inode->zones[i];
            for(int j = 0; j < num_pointers; j++){
                if(spointer[j] == 0){
                    continue;
                }
                iterate_over_dirent_block(b_dev, spointer[j]);                
            }
        }
        else if(i == ZONE_DINDIR){
            // //debugf("dirents_get: DOUBLy-indirect !\n");
            uint32_t* dpointer = inode->zones[i];
            uint32_t* spointer;
            for(int j = 0; j < num_pointers; j++){
                if(dpointer[j] == 0){
                    continue;
                }
                spointer = dpointer[j];
                for(int k = 0; k < num_pointers; k++){                    
                    iterate_over_dirent_block(b_dev, spointer[k]);                
                }
            }
        }
        else if(i == ZONE_TINDIR){
            // //debugf("dirents_get: triply-indirect !\n");
            uint32_t* tpointer = inode->zones[i];
            uint32_t* dpointer;
            uint32_t* spointer;
            for(int j = 0; j < num_pointers; j++){
                if(tpointer[j] == 0){
                    continue;
                }
                dpointer = tpointer[j];
                for(int k = 0; k < num_pointers; k++){
                    spointer = dpointer[k];                    
                    for(int l = 0; l < num_pointers; l++){
                        iterate_over_dirent_block(b_dev, spointer[l]);
                    }
                }       
            }
        }
    }
    // //debugf("finished loop\n");
}


static bool get_next_zone(fileDescriptorEntry *fd_entry){
    Inode *inode = fd_entry->file_inode;
    uint32_t num_pointers = fs.block_size / 4;
    int i, j, k, l;
    bool block_return;
    debugf("fd_entry->zone_indices[0]: %d\n", fd_entry->zone_indices[0]);
    
    if(fd_entry->zone_indices[0] < ZONE_INDIR){
        debugf("get_next_zone: direct pointer !\n");
        for(i = fd_entry->zone_indices[0] + 1; i < ZONE_INDIR; i++){
            
            fd_entry->zone_indices[0] = i;
            fd_entry->fp_block_offset = inode->zones[i];
            if(inode->zones[i] == 0) continue;
            else return true;
        }
        fd_entry->zone_indices[0] = ZONE_INDIR;
        fd_entry->zone_indices[1] = -1;
    }
    debugf("fd_entry->zone_indices[0]: %d\n", fd_entry->zone_indices[0]);
    if(fd_entry->zone_indices[0] == ZONE_INDIR && fd_entry->file_inode->zones[ZONE_INDIR] != 0){
        debugf("get_next_zone: singly-indirect !\n");
        i = fd_entry->zone_indices[0];
        uint32_t* spointer = (uint32_t*)kmalloc(fs.block_size);
        block_return = block_read_wrapper(g_bdev, (uint64_t)spointer, inode->zones[i] * fs.block_size, fs.block_size);
        if(!block_return){
            debugf("failure at A\n");
            while(1);
        }
        for(j = fd_entry->zone_indices[1] + 1; j < num_pointers; j++){
            fd_entry->zone_indices[1] = j;
            if(spointer[j] == 0){
                continue;
            }
            else{
                fd_entry->fp_block_offset = spointer[j];
                debugf("pointer: %d\n", spointer[j]);
                debugf("j: %d\n", j);
                kfree(spointer);
                return true;
            }
        }
        
        //free allocd mem, and go to Double indirect zones!
        kfree(spointer);
        fd_entry->zone_indices[0] = ZONE_DINDIR;
        fd_entry->zone_indices[1] = 0;
        fd_entry->zone_indices[2] = -1;
        // if (j == num_pointers){
        //     for(i = fd_entry->zone_indices[0] + 1; i < 10; i++){
        //         // debugf("dir pointer %d\n", i);
        //         fd_entry->zone_indices[0] = i;
        //         if(inode->zones[i] != 0){
        //             break;
        //         }
        //     }
        //     fd_entry->zone_indices[1] = 0;
        // }
    }
    
    if(fd_entry->zone_indices[0] == ZONE_DINDIR && fd_entry->file_inode->zones[ZONE_DINDIR] != 0){
        // debugf("get_next_zone: DOUBLy-indirect !\n");
        i = fd_entry->zone_indices[0];
        uint32_t* dpointer = kmalloc(fs.block_size);
        uint32_t* spointer = kmalloc(fs.block_size);
        block_return = block_read_wrapper(g_bdev, (uint64_t)dpointer, inode->zones[i] * fs.block_size, fs.block_size);
        if(block_return == false){
            debugf("Failure at B\n");
            while(1);
        }
        
        for(j = fd_entry->zone_indices[1]; j < num_pointers; j++){
            fd_entry->zone_indices[1] = j;
            // debugf("j: %d\n", j);
            if(dpointer[j] == 0){ continue;}
            
            if(!block_read_wrapper(g_bdev, (uint64_t)spointer, dpointer[j] * fs.block_size, fs.block_size)){
                debugf("REally Fuck You\n");
                while(1);
            }
            // debugf("j: %d\n", j);
            for(k = (uint32_t)(fd_entry->zone_indices[2] + 1); k < num_pointers; k++){
                // debugf("doubly j %d k %d\n", j, k);
                if(spointer[k] == 0) continue;
                else{
                    fd_entry->zone_indices[2] = k;
                    fd_entry->fp_block_offset = spointer[k];
                    debugf("pointer: %d\n", spointer[k]);
                    kfree(dpointer);
                    kfree(spointer);
                    
                    return true;
                }
            }

            // Reset the third index for the next round of the second index
            fd_entry->zone_indices[2] = (uint32_t)-1;
        }

        //If we reach down here, then we must be going to the triple indirect pointer level!
        kfree(dpointer);
        kfree(spointer);

        if(j == num_pointers){
            fd_entry->zone_indices[0] = ZONE_TINDIR; // Move to the next type of pointer
            fd_entry->zone_indices[1] = 0;  // Reset the second index
            fd_entry->zone_indices[2] = 0;  // Reset the third index
            fd_entry->zone_indices[3] = -1;
        }
        
    }
    

    if(fd_entry->zone_indices[0] == ZONE_TINDIR && fd_entry->file_inode->zones[ZONE_TINDIR] != 0){
        debugf("get_next_zone: triply-indirect!: %d\n", j);


        while(1);
        i = fd_entry->zone_indices[0];
        uint32_t* tpointer = kmalloc(fs.block_size);
        uint32_t* dpointer = kmalloc(fs.block_size);
        uint32_t* spointer = kmalloc(fs.block_size);
        block_return = block_read_wrapper(g_bdev, (uint64_t)tpointer, inode->zones[i] * fs.block_size, fs.block_size);
        if(block_return == false){
            debugf("Failure at o\n");
            while(1);
        }
        for(j = (uint32_t)fd_entry->zone_indices[1]; j < num_pointers; j++){
            fd_entry->zone_indices[1] = j;
            if(tpointer[j] == 0){
                continue;
            }

            if(!block_read_wrapper(g_bdev, (uint64_t)dpointer, tpointer[j] * fs.block_size, fs.block_size));
            for(k = (uint32_t)fd_entry->zone_indices[2]; k < num_pointers; k++){
                fd_entry->zone_indices[2] = k;
                if(dpointer[k] == 0){
                    continue;
                }

                if(!block_read_wrapper(g_bdev, (uint64_t)spointer, dpointer[k] * fs.block_size, fs.block_size));
                
                for(l = (uint32_t)(fd_entry->zone_indices[3] + 1); l < num_pointers; l++){     
                    // debugf("triply j %d k %d l %d\n", j, k, l);
                    fd_entry->zone_indices[3] = l;
                    if(spointer[l] == 0){
                        continue;
                    }
                    else{
                        kfree(tpointer);
                        kfree(dpointer);
                        kfree(spointer);
                        fd_entry->fp_block_offset = spointer[l];
                        return true;
                    }
                }

                if(l == num_pointers){
                    fd_entry->zone_indices[3] = -1;
                }
            }
            if(k == num_pointers){
                fd_entry->zone_indices[2] = 0;
            }
        }

        kfree(tpointer);
        kfree(dpointer);
        kfree(spointer);

        if(j == num_pointers){
            fd_entry->zone_indices[0] += 1;
            fd_entry->zone_indices[1] = 0;
            fd_entry->zone_indices[2] = 0;
        }
    }

    return false;
}

bool get_next_zone_wrapper(fileDescriptorEntry* fd_entry){

    uint32_t zone_indices_temp[4];
    zone_indices_temp[0] = fd_entry->zone_indices[0];
    zone_indices_temp[1] = fd_entry->zone_indices[1];
    zone_indices_temp[2] = fd_entry->zone_indices[2];
    zone_indices_temp[3] = fd_entry->zone_indices[3];
    uint64_t temp_block_offset =  fd_entry->fp_block_offset;
    if(get_next_zone(fd_entry) == false){
        fd_entry->zone_indices[0] = zone_indices_temp[0];
        fd_entry->zone_indices[1] = zone_indices_temp[1];
        fd_entry->zone_indices[2] = zone_indices_temp[2];
        fd_entry->zone_indices[3] = zone_indices_temp[3];
        fd_entry->fp_block_offset = temp_block_offset;
        return false;
    }
return true;
}

bool alloc_new_zone(fileDescriptorEntry* fd_entry){

    bool next_zone_alloc = false;

    //Now we need to search for a free zone to map this to!

    uint32_t datazone_num = find_next_datazone();
    
    //No free datazone available. Can not expand file!
    if(datazone_num == 0){
        while(1);
        return false;    
    }
    bool block_written;
    uint32_t datazone_ptr = fs.new_zone_pointer_add + datazone_num;
    int indirect_datazone_num = 0;
    int dindirect_data_zone_num = 0;
    int tindirect_data_zone_num = 0;
    
    debugf("Current Zone [0]: %d\n", fd_entry->zone_indices[0]);
    debugf("Current zone ptr: %d\n", fd_entry->file_inode->zones[fd_entry->zone_indices[0]]);
    
    //Set the next direct zone. 
    if(fd_entry->zone_indices[0] < ZONE_INDIR - 1){
        debugf("Setting next direct zone!\n");
        fd_entry->file_inode->zones[fd_entry->zone_indices[0] + 1] = datazone_ptr;
        fd_entry->zone_indices[0] += 1;
        fd_entry->fp_block_offset = datazone_ptr;
        next_zone_alloc = true;
    }

    //Otherwise, we need to go to single indirect!
    if((fd_entry->zone_indices[0] <= ZONE_INDIR) && (next_zone_alloc == false)){
        
        //Need to set up single indirect zones!
        if(fd_entry->zone_indices[0] == ZONE_INDIR - 1){
            debugf("Allocating space for single indirect zones!\n");
            uint32_t* indirect_zone = (uint32_t*)kmalloc(fs.block_size);
            //First grab new datazone
            indirect_datazone_num = find_next_datazone();
            if(indirect_datazone_num == 0){ 
                kfree(indirect_zone);
                return false;
            }
            debugf("Found zone for single pointers!\n");

            uint32_t indirect_datazone_ptr = indirect_datazone_num + fs.new_zone_pointer_add;
            
            for(int i = 0; i < NUM_POINTERS; i++) indirect_zone[i] = 0;
            indirect_zone[0] = datazone_ptr;

            // Write the new datazone address into the indirect_datazone
            block_written = block_write_wrapper(g_bdev, (uint64_t)indirect_zone, zone_adr(indirect_datazone_ptr), 1024);
            if(block_written == false){
                debugf("G\n");
                while(1);
            }
            fd_entry->file_inode->zones[ZONE_INDIR] = indirect_datazone_ptr;
            fd_entry->zone_indices[0] = ZONE_INDIR;
            fd_entry->zone_indices[1] = 0;
            fd_entry->zone_indices[2] = -1;
            fd_entry->zone_indices[3] = -1;
            fd_entry->fp_block_offset = datazone_ptr;
            next_zone_alloc = true;
            kfree(indirect_zone);
            // block_flush(g_bdev);
        }
        //Don't need to allocate single indirect, just set the next zone array
        else if(fd_entry->zone_indices[0] == ZONE_INDIR){
            debugf("Going to next indirect datazone pointer\n");
            //Last pointer on indirect zones means we must go to double indirect
            if(fd_entry->zone_indices[1] == (NUM_POINTERS - 1)){
                fd_entry->zone_indices[0] = ZONE_DINDIR;
                fd_entry->zone_indices[1] = 0;
                fd_entry->zone_indices[2] = 0;
                fd_entry->zone_indices[3] = -1;
                debugf("Last single indirect block taken, need to go to double indirect\n");
            }
            //Set the next pointer for single indirect
            else{
                debugf("Setting next single indirect pointer!\n");
                // uint32_t pointer_zone[NUM_POINTERS];
                uint32_t* pointer_zone = (uint32_t*)kmalloc(fs.block_size);
                //Read single indirect pointer block, update it, and write it back
                block_written = block_read_wrapper(g_bdev, (uint64_t)pointer_zone, zone_adr(fd_entry->file_inode->zones[ZONE_INDIR]), fs.block_size);
                if(block_written == false){
                    debugf("G\n");
                    while(1);
                }

                pointer_zone[fd_entry->zone_indices[1] + 1] = datazone_ptr;

                block_written = block_write_wrapper(g_bdev, (uint64_t)pointer_zone, zone_adr(fd_entry->file_inode->zones[ZONE_INDIR]), fs.block_size);
                if(block_written == false){
                    debugf("fd_entry->file_inode->zones[ZONE_INDIR]: %d\n", fd_entry->file_inode->zones[ZONE_INDIR]);
                    debugf("Failure to write!\n");
                    while(1);
                }
                fd_entry->zone_indices[1] += 1;
                fd_entry->fp_block_offset = datazone_ptr;
                next_zone_alloc = true;
                kfree(pointer_zone);
                debugf("Next single allocated!\n");
                // block_flush(g_bdev);
            }
        }
    }

    //Go into double indirect if needed!
    if(fd_entry->zone_indices[0] == ZONE_DINDIR && (next_zone_alloc == false)){
        
        //Allocate double indirect zones
        if(fd_entry->file_inode->zones[ZONE_DINDIR] == 0){
            int s_direct = find_next_datazone();
            if(s_direct == 0) return false;
            int d_direct = find_next_datazone();
            if(d_direct == 0) return false;

            uint32_t s_direct_ptr = s_direct + fs.new_zone_pointer_add;
            uint32_t d_direct_ptr = d_direct + fs.new_zone_pointer_add;

            uint32_t* s_pointers = (uint32_t*)kmalloc(fs.block_size);
            uint32_t* d_pointers = (uint32_t*)kmalloc(fs.block_size);
            
            for(int i = 0; i < NUM_POINTERS; i++){
                s_pointers[i] = 0;
                d_pointers[i] = 0;
            }

            s_pointers[0] = d_direct_ptr;
            d_pointers[0] = datazone_ptr;

            block_written =  block_write_wrapper(g_bdev, (uint64_t)s_pointers, zone_adr(s_direct_ptr), fs.block_size);

            if(block_written == false){
                debugf("t\n");
                while(1);
            }

            block_written = block_write_wrapper(g_bdev, (uint64_t)d_pointers, zone_adr(d_direct_ptr), fs.block_size);
            if(block_written == false){
                debugf("e\n");
                while(1);
            }

            fd_entry->zone_indices[1] = 0;
            fd_entry->zone_indices[2] = 0;
            fd_entry->zone_indices[3] = -1;
            fd_entry->file_inode->zones[ZONE_DINDIR] = s_direct_ptr;
            fd_entry->fp_block_offset = datazone_ptr;
            next_zone_alloc = true;
            kfree(s_pointers);
            kfree(d_pointers);
            // block_flush(g_bdev);
        }
        //We have single indirects that we can use, but the current double one is filled, go to the next single indirect
        else if((fd_entry->zone_indices[2] == NUM_POINTERS - 1) && (fd_entry->zone_indices[1] < NUM_POINTERS - 1)){
            debugf("Allocating a new double direct\n");
            debugf("zones[0]: %d\n", fd_entry->zone_indices[0]);
            debugf("zones[1]: %d\n", fd_entry->zone_indices[1]);
            debugf("zones[2]: %d\n", fd_entry->zone_indices[2]);
            debugf("zones[3]: %d\n", fd_entry->zone_indices[3]);

            int d_direct = find_next_datazone();

            if(d_direct == 0) return false;

            uint32_t s_pointers_ptr = fd_entry->file_inode->zones[ZONE_DINDIR];
            uint32_t d_direct_ptr = (d_direct + fs.new_zone_pointer_add);

            uint32_t* s_pointers = (uint32_t*)kmalloc(fs.block_size);
            uint32_t* d_pointers = (uint32_t*)kmalloc(fs.block_size);

            //update the s_pointers to include the new d_pointer zone
            block_written = block_read_wrapper(g_bdev, (uint64_t)s_pointers, zone_adr(s_pointers_ptr), fs.block_size);
            if(block_written == false){
                debugf("r\n");
                while(1);
            }
            s_pointers[fd_entry->zone_indices[1] + 1] = (uint32_t)d_direct_ptr;
            block_written = block_write_wrapper(g_bdev, (uint64_t)s_pointers, zone_adr(s_pointers_ptr), fs.block_size);
            if(block_written == false){
                debugf("x\n");
                while(1);
            }
            
            for(int i = 0; i < NUM_POINTERS; i++){
                d_pointers[i] = 0;
            }

            d_pointers[0] = datazone_ptr;

            block_written = block_write_wrapper(g_bdev, (uint64_t)d_pointers, zone_adr(d_direct_ptr), fs.block_size);
            if(block_written == false){
                debugf("j\n");
                while(1);
            }

            fd_entry->zone_indices[1] += 1;
            fd_entry->zone_indices[2] = 0;
            fd_entry->zone_indices[3] = -1;
            fd_entry->fp_block_offset = datazone_ptr;
            next_zone_alloc = true;
            kfree(s_pointers);
            kfree(d_pointers);
            // block_flush(g_bdev);
        }//Otherwise we have that ALL the single indirects are taken, go to the triple indirects
        else if((fd_entry->zone_indices[2] == (NUM_POINTERS - 1)) && (fd_entry->zone_indices[1] == (NUM_POINTERS - 1))){
            fd_entry->zone_indices[0] = ZONE_TINDIR;
        }//Here we have that the double indirect we are on isn't filled. Set the next pointer
        else{
            uint32_t* s_pointers = (uint32_t*)kmalloc(fs.block_size);
            uint32_t* d_pointers = (uint32_t*)kmalloc(fs.block_size);

            uint32_t s_pointers_ptr = fd_entry->file_inode->zones[ZONE_DINDIR];
            uint32_t d_pointers_ptr;
            //read the full single pointers array first, grab the current zone ptr for double pointers, then read
            //then update the next double pointer to be the next datazone!
            block_written = block_read_wrapper(g_bdev, (uint64_t)s_pointers, zone_adr(s_pointers_ptr), fs.block_size);
            if(block_written == false){
                debugf("s_pointers_ptr: %d\n", s_pointers_ptr);
                debugf("bgfdgdfgsgdfsgsdf\n");
                while(1);
            }
            d_pointers_ptr = s_pointers[fd_entry->zone_indices[1]];
            block_written = block_read_wrapper(g_bdev, (uint64_t)d_pointers, zone_adr(d_pointers_ptr), fs.block_size);
            if(block_written == false){
                debugf("q\n");
                while(1);
            }
            d_pointers[fd_entry->zone_indices[2] + 1] = datazone_ptr;
            block_written = block_write_wrapper(g_bdev, (uint64_t)d_pointers, zone_adr(d_pointers_ptr), fs.block_size);
            if(block_written == false){
                y_failed = true;
                block_written = block_write_wrapper(g_bdev, (uint64_t)d_pointers, zone_adr(d_pointers_ptr), fs.block_size);
                if(block_written == false){
                    debugf("y\n");
                    while(1);
                }
                debugf("y failed first time\n");
                while(1);
            }

            fd_entry->zone_indices[2] += 1;
            // if(fd_entry->zone_indices[2] == 256){
            //     debugf("fuck\n");
            //     while(1);
            // }
            fd_entry->fp_block_offset = datazone_ptr;
            next_zone_alloc = true;
            kfree(s_pointers);
            kfree(d_pointers);
            // block_flush(g_bdev);
        }
    }

    if(fd_entry->zone_indices[0] == ZONE_TINDIR && next_zone_alloc == false){
        //Allocate new triple indirect zones
        if(fd_entry->file_inode->zones[ZONE_TINDIR] == 0){
            int s_direct = find_next_datazone();
            if(s_direct == 0) return false;
            int d_direct = find_next_datazone();
            if(d_direct == 0) return false;
            int t_direct = find_next_datazone();
            if(t_direct == 0) return false;

            uint32_t s_direct_ptr = s_direct + fs.new_zone_pointer_add;
            uint32_t d_direct_ptr = d_direct + fs.new_zone_pointer_add;
            uint32_t t_direct_ptr = t_direct + fs.new_zone_pointer_add;

            uint32_t s_pointers[NUM_POINTERS];
            uint32_t d_pointers[NUM_POINTERS];
            uint32_t t_pointers[NUM_POINTERS];

            for(int i = 0; i < NUM_POINTERS; i++){
                t_pointers[i] = s_pointers[i] = d_pointers[i] = 0;
            }
            
            s_pointers[0] = d_direct_ptr;
            d_pointers[0] = t_direct_ptr;
            t_pointers[0] = datazone_ptr;

            block_write_wrapper(g_bdev, &s_pointers, zone_adr(s_direct_ptr), fs.block_size);
            block_write_wrapper(g_bdev, &d_pointers, zone_adr(d_direct_ptr), fs.block_size);
            block_write_wrapper(g_bdev, &t_pointers, zone_adr(t_direct_ptr), fs.block_size);
            
            fd_entry->zone_indices[0] = ZONE_TINDIR;
            fd_entry->zone_indices[1] = 0;
            fd_entry->zone_indices[2] = 0;
            fd_entry->zone_indices[3] = 0;
            fd_entry->file_inode->zones[ZONE_TINDIR] = s_direct_ptr;
            fd_entry->fp_block_offset = datazone_ptr;
            next_zone_alloc = true;        
        }//Triple indirect is not full, just go to the next zone!
        else if(fd_entry->zone_indices[3] != NUM_POINTERS - 1){
            uint32_t s_pointers[NUM_POINTERS];
            uint32_t d_pointers[NUM_POINTERS];
            uint32_t t_pointers[NUM_POINTERS];

            uint32_t s_pointers_ptr = fd_entry->file_inode->zones[ZONE_DINDIR];
            uint32_t d_pointers_ptr;
            uint32_t t_pointers_ptr;

            block_read_wrapper(g_bdev, &s_pointers, zone_adr(s_pointers_ptr), fs.block_size);
            d_pointers_ptr = s_pointers[fd_entry->zone_indices[2]];
            block_read_wrapper(g_bdev, &d_pointers, zone_adr(d_pointers_ptr), fs.block_size);
            d_pointers_ptr = s_pointers[fd_entry->zone_indices[3]];
            block_read_wrapper(g_bdev, &t_pointers, zone_adr(t_pointers_ptr), fs.block_size);

            t_pointers[fd_entry->zone_indices[3] + 1] = datazone_ptr;

            block_write_wrapper(g_bdev, &t_pointers, zone_adr(t_pointers_ptr), fs.block_size);

            fd_entry->zone_indices[3] += 1;
            fd_entry->fp_block_offset = datazone_ptr;
            next_zone_alloc = true;    
        }//Triple has no data zones available, go to the next double, and allocate a new triple. 
        else if(fd_entry->zone_indices[3] == (NUM_POINTERS - 1) && fd_entry->zone_indices[2] < (NUM_POINTERS - 1)){
            
            int t_direct = find_next_datazone();
            if(t_direct == 0) return false;

            uint32_t s_pointers[NUM_POINTERS];
            uint32_t d_pointers[NUM_POINTERS];
            uint32_t t_pointers[NUM_POINTERS];

            uint32_t s_pointers_ptr = fd_entry->file_inode->zones[ZONE_DINDIR];
            uint32_t d_pointers_ptr;
            uint32_t t_pointers_ptr = t_direct + fs.new_zone_pointer_add;

            block_read_wrapper(g_bdev, &s_pointers, zone_adr(s_pointers_ptr), fs.block_size);
            d_pointers_ptr = fs.zones_start + (s_pointers[fd_entry->zone_indices[2]] * fs.block_size);
            block_read_wrapper(g_bdev, &d_pointers, zone_adr(d_pointers_ptr), fs.block_size);

            d_pointers[fd_entry->zone_indices[2] + 1] = t_pointers_ptr;
            
            block_write_wrapper(g_bdev, &d_pointers, zone_adr(d_pointers_ptr), fs.block_size);
            for(int i = 0; i < NUM_POINTERS; i++) t_pointers[i] = 0;

            t_pointers[0] = datazone_ptr;

            block_write_wrapper(g_bdev, &t_pointers, zone_adr(t_pointers_ptr), fs.block_size);

            fd_entry->zone_indices[2] += 1;
            fd_entry->zone_indices[2] = 0;
            fd_entry->fp_block_offset = datazone_ptr;
            next_zone_alloc = true;
        }//No more double indirect pointers, alloc new double and triple
        else if(fd_entry->zone_indices[3] == (NUM_POINTERS - 1) && fd_entry->zone_indices[2] == (NUM_POINTERS - 1) && fd_entry->zone_indices[1] < (NUM_POINTERS - 1) ){
            
            int d_direct = find_next_datazone();
            if(d_direct == 0) return false;
            int t_direct = find_next_datazone();
            if(t_direct == 0) return false;

            uint32_t s_pointers[NUM_POINTERS];
            uint32_t d_pointers[NUM_POINTERS];
            uint32_t t_pointers[NUM_POINTERS];

            uint32_t s_pointers_ptr = fd_entry->file_inode->zones[ZONE_DINDIR];
            uint32_t d_pointers_ptr = fs.new_zone_pointer_add + d_direct;
            uint32_t t_pointers_ptr = fs.new_zone_pointer_add + t_direct;

            block_read_wrapper(g_bdev, &s_pointers, zone_adr(s_pointers_ptr), fs.block_size);
            
            s_pointers[fd_entry->zone_indices[1] + 1] = d_pointers_ptr;
            block_write_wrapper(g_bdev, &s_pointers, zone_adr(s_pointers_ptr), fs.block_size);

            for(int i = 0; i < NUM_POINTERS; i++){
                d_pointers[i] = t_pointers[i] = 0;
            }
            d_pointers[0] = d_pointers_ptr;
            t_pointers[0] = datazone_ptr;
            
            block_write_wrapper(g_bdev, &d_pointers, zone_adr(d_pointers_ptr), fs.block_size);
            block_write_wrapper(g_bdev, &t_pointers, zone_adr(t_pointers_ptr), fs.block_size);

            fd_entry->zone_indices[1] += 1;
            fd_entry->zone_indices[2] = 0;
            fd_entry->zone_indices[3] = 0;
            next_zone_alloc = true;
        }
        else{
            return false;
        }
    }
fd_entry->fp_block_offset = datazone_ptr;
return next_zone_alloc;
}

void minix3_init_dev(BlockDevice* bdev){
    //read super block
    superblock *sb = (superblock *)kmalloc(32);
    if(!block_read_wrapper(bdev, sb, 1024, 32)){
        debugf("minix3_init_dev: problem reading superblock\n");
        return;
    }

    //print some superblock stuff
    //debugf("minix3_init_dev: sb num inodes %d\n", sb->num_inodes);
    //debugf("minix3_init_dev: sb pad0 %d\n", sb->pad0);
    //debugf("minix3_init_dev: sb imap_blocks %d\n", sb->imap_blocks);
    //debugf("minix3_init_dev: sb zmap_blocks %d\n", sb->zmap_blocks);
    //debugf("minix3_init_dev: sb first_data_zone %d\n", sb->first_data_zone);
    //debugf("minix3_init_dev: sb log_zone_size %d\n", sb->log_zone_size);
    //debugf("minix3_init_dev: sb max_size %d\n", sb->max_size);
    //debugf("minix3_init_dev: sb num_zones %d\n", sb->num_zones);
    //debugf("minix3_init_dev: sb magic 0x%x\n", sb->magic);
    //debugf("minix3_init_dev: sb pad2 %d\n", sb->pad2);
    //debugf("minix3_init_dev: sb pad3 %d\n", sb->pad3);
    //debugf("minix3_init_dev: sb disk_version %d\n", sb->disk_version);
    //debugf("minix3_init_dev: sb pad4 %d\n", sb->pad4);

    if(sb->magic != MINIX3_MAGIC) {
        debugf("minix3_init_dev: block device not Minix 3\n");
        return;
    }
    fs.num_inodes = sb->num_inodes;
    fs.num_zones = sb->num_zones;
    fs.bd = bdev;
    fs.ecam_device_id = bdev->vdev->pcidev->ecam->device_id;
    fs.sb = sb;
    fs.block_size = 1024 << sb->log_zone_size;

    fs.imap_allocd = (uint8_t *)kmalloc(fs.block_size * sb->imap_blocks);
    fs.imap_blocks_start = 1024 * 2; // boot block and super block always 1024 bytes
    fs.zmap_allocd = (uint8_t *)kmalloc(fs.block_size * sb->zmap_blocks);
    fs.zmap_blocks_start = fs.imap_blocks_start + (fs.block_size * sb->imap_blocks);
    debugf("\n\n\n\n\n\ntest\n");
    fs.inodes_start = fs.zmap_blocks_start + (fs.block_size * sb->zmap_blocks);
    fs.zones_start = fs.block_size * sb->first_data_zone;
    fs.new_zone_pointer_add = sb->first_data_zone;
    debugf("num of zones: %d\n", sb->first_data_zone);
    debugf("num of zones: %d\n", fs.new_zone_pointer_add);
    // while(1);
    fs.dirent_cache = map_new_with_slots(FS_NUM_CACHE_ENTRIES);
    if(fs.dirent_cache == NULL){
        while(1){}
    }
    
//     int get_next = find_next_datazone();
//     debugf("get_next: %d\n", get_next);
// while(1);
    debugf("Map address: 0x%x\n", fs.dirent_cache);

    if(fs.inodes_start + (sizeof(Inode) * sb->num_inodes) != fs.zones_start) {
        debugf("minix3_init_dev: super block first_data_zone %d does not match calculatedd %d\n", fs.zones_start, fs.inodes_start + (sizeof(Inode) * sb->num_inodes));
        return;
    }
    debugf("minix3_init_dev: super block first_data_zone matches calculated!\n");

    //debugf("Test A\n");
    if(!block_read_wrapper(bdev, fs.imap_allocd, fs.imap_blocks_start, fs.block_size * sb->imap_blocks)){
        debugf("minix3_init_dev: problem reading imap blocks\n");
        return;
    }
   
    debugf("Test B\n");
    for(int i = 1; i < sb->num_inodes; i++){
        if(bitmap_taken((uint64_t)fs.imap_allocd, i)){
            // debugf("minix3_init_dev: inode %d taken\n", i);
            
            struct minix3_inode curr_inode;
            debugf("Getting inode: %d\n", i);

            inode_get(bdev, &curr_inode, i);
            
            // debugf("inode mode %o\n", curr_inode.mode);
            if (S_ISDIR(curr_inode.mode)){
                // debugf("inode is a dir\n");

                struct minix3_dir_entry curr_dirent;
                uint16_t placeholder = dirents_get(bdev, &curr_inode, &curr_dirent);
            }
        }
    }
    
    debugf("Test B done!\n");
    //  while(1){
        
    // }
    // test get_next_zone
    // fileDescriptorEntry temp_descr;
    // temp_descr.file_inode = kmalloc(sizeof(Inode));
    // for(int i = 1; i < fs.sb->num_inodes; i++){
    //     if(!bitmap_taken(fs.imap_allocd, i)) continue;
    //     inode_get(bdev, temp_descr.file_inode, i);
    //     temp_descr.zone_indices[0] = 0;
    //     temp_descr.zone_indices[1] = -1;
    //     temp_descr.zone_indices[2] = -1;
    //     temp_descr.zone_indices[3] = -1;
    //     while(get_next_zone(&temp_descr)){
    //         //debugf("inode %d, zone indices %d %d %d %d\n", i, temp_descr.zone_indices[0],temp_descr.zone_indices[1], temp_descr.zone_indices[2], temp_descr.zone_indices[3]);
    //     }
    // }

    // while(true);

    if(!block_read_wrapper(bdev, fs.zmap_allocd, fs.zmap_blocks_start, fs.block_size * sb->zmap_blocks)){
        //debugf("minix3_init_dev: problem reading zmap blocks\n");
        return;
    }

    // int get_next = find_next_datazone();
    // debugf("get_next: %d\n", get_next);
    // while(1);
    // for(int i = 0; i < sb->num_zones; i++){
    //     if(bitmap_taken((uint64_t)fs.zmap_allocd, i)){
    //         //debugf("minix3_init_dev: zone %d taken\n", i);
    //     }
    // }

    // need to cache all inodes and all directory entries (see .h)

    char* data_block = (char*)kmalloc(fs.block_size);

    // for(int i = 1; i < sb->num_inodes; i++){
    //     if(bitmap_taken((uint64_t)fs.imap_allocd, i)){
    //         ////debugf("minix3_init_dev: inode %d taken\n", i);
    //         Dirent *curr_dir_entry;
    //         struct minix3_inode curr_inode;
    //         inode_get(bdev, &curr_inode, i);
    //         ////debugf("inode mode %o\n", curr_inode.mode);
    //         if (inode_get_type(&curr_inode) == S_IFDIR){
    //             ////debugf("inode is a dir\n");
    //             uint32_t num_dir_entries = curr_inode.size / sizeof(Dirent);
    //             //debugf("Number of entries: %d\n", num_dir_entries);
    //             ////debugf("Zone_1: 0x%x\n", curr_inode.zones[0]);
    //             uint32_t zone_start = (curr_inode.zones[0] * fs.block_size);
    //             block_read_wrapper(bdev, data_block, zone_start, fs.block_size);
                
    //             for(uint32_t curr_entry = 0; curr_entry < num_dir_entries; curr_entry++){
    //                 curr_dir_entry = (Dirent *)(data_block + (64 * curr_entry));
    //                 // //debugf("Directory Name: %s\n", curr_dir_entry->name);
    //                 // //debugf("Directory inode: %d\n", curr_dir_entry->inode_num);

    //                 if(curr_dir_entry->inode_num == 1){
    //                     //debugf("Directory Name: %s\n", curr_dir_entry->name);
    //                     //debugf("Directory inode: %d\n", curr_dir_entry->inode_num);
    //                 }

    //             }
    //         }
    //     }
    // }

    
    // struct minix3_inode root_inode;
    // inode_get(bdev, &root_inode, 1);
    // if (inode_get_type(&root_inode) == S_IFDIR){
    //     //debugf("Root inode is a directory!\n");
    //     //debugf("Root directory size: %d\n", root_inode.size / 64);
    //     //debugf("Zones to consider: %x\n", (root_inode.size / 64) / 16);
    // }

    // debugf("cache_entry size: %d\n", sizeof(cacheEntry));

    debugf("Cache_setup\n");
    minix3_setup_cache(bdev, 1, fs.dirent_cache);
    debugf("File count: %d\n", file_count);
    // minix3_search_cache(fs.dirent_cache, 0);
}

// int     stat   (const char *path, struct stat *stat){}

uint32_t find_entry_for_parent(Inode parent_inode){

    uint64_t num_dir_entries = parent_inode.size / 64;
    uint64_t max_num_dir_entires = 1024 / 64;
    uint32_t left_over = num_dir_entries % 16;

    debugf("parent_inode.size: %d\n", parent_inode.size);
    debugf("num_dir_entries: %d\n", num_dir_entries);
    debugf("left_over: %d\n", left_over);
    // while(1){

    // }

    uint64_t num_of_zones_to_traverse = parent_inode.size / 1024;

    uint64_t directory_blocks_traversed = 0;

    uint32_t return_adr = 0;

    for(int i = 0; i < NUM_ZONES; i++){

        if(parent_inode.zones[i] == 0){
            continue;
        }
        if(i < ZONE_INDIR){
            if(directory_blocks_traversed == num_of_zones_to_traverse){
                if(left_over == 0){
                    //Do something here to get a new zone
                }
                debugf("find_entry: %d\n", i);
                Dirent test_dir;
                // block_read_wrapper(g_bdev, &test_dir, (parent_inode.zones[i] * fs.block_size) + ((left_over) * 64), 64);
                // block_read_wrapper(g_bdev, &test_dir, (parent_inode.zones[i] * fs.block_size) + (64 * left_over), 64);
                // debugf("Last dir entry name: %s\n", test_dir.name);
                // while(1){

                // }
                return_adr = (parent_inode.zones[i] * fs.block_size) + ((left_over) * 64);
                break;
            }
        }
        else if(i == ZONE_INDIR){

        }else if(i == ZONE_DINDIR){

        }else if(i == ZONE_TINDIR){

        }

    }
return return_adr;
}

int open(const char *path, int flags, mini_mode_t mode){
    
    bool block_write_success = false;

    mutex_spinlock(&fs.fd_table.lock);
    // find an empty descriptor
    fileDescriptorEntry* curr_fd_entry;
    Inode* curr_inode;

    int file_entry_number = -1;

    for(int i = 0; i < MAX_FD_ENTRIES; i++){
        curr_fd_entry = &(fs.fd_table.entry[i]);
        if(curr_fd_entry->taken == 0){
            curr_fd_entry->taken = 1;
            file_entry_number = i;
            break;
        }
    }
    
    debugf("enter open\n");

    cacheEntry* curr_cache_entry = minix3_search_cache_wrapper(path);

    if((flags & O_CREAT) && curr_cache_entry == NULL){

        debugf("Enter creating new file!\n");
        minix3_parse_path(path);
        minix3_get_parent_path(path);
        
        cacheEntry* parent_cache_entry = minix3_search_cache_wrapper(g_parent_cache_path_new);
        Inode parent_inode = parent_cache_entry->cached_inode;
        if(parent_cache_entry != NULL){
            debugf("Parent found!\n");
        }

        //First find a free inode, and a free datazone to use
        uint32_t inode_num = -1;
        uint32_t datazone_num = -1;
        
        inode_num = find_next_inode();
        datazone_num = find_next_datazone();

        debugf("Inode: %d\n", inode_num);
        debugf("data: %d\n", datazone_num);
        if(inode_num == 0 || datazone_num == 0) return -1;
        //Check the mode shit. Probably am not dealing with it correctly!
        Inode* new_inode = (Inode*)kmalloc(64); 
        new_inode->mode = mode;
        new_inode->nlinks = 1;
        new_inode->uid;
        new_inode->gid;
        new_inode->size = 0;
        new_inode->atime = sbi_get_time();
        new_inode->mtime = sbi_get_time();
        new_inode->ctime = sbi_get_time();

        for(int i = 0; i < 10; i++) new_inode->zones[i] = 0;

        new_inode->zones[0] = fs.new_zone_pointer_add + datazone_num;
        Dirent new_directory_entry;
        new_directory_entry.inode_num = inode_num;
        strcpy(&(new_directory_entry.name), path_file_name(path));

        uint32_t new_dir_entry_adr = find_entry_for_parent(parent_inode);

        //Write: Directory entry to the parent!

        Dirent test_dir;
        block_write_success = block_read_wrapper(g_bdev, &test_dir, new_dir_entry_adr, 64);
        if(!block_write_success){
            debugf("Fail at A\n");
        }
        debugf("test_dir.name: %s\n", test_dir.name);

        block_write_success = block_write_wrapper(g_bdev, &new_directory_entry, new_dir_entry_adr, 64);
        if(!block_write_success){
            debugf("Fail at A\n");
        }
        block_write_success = block_read_wrapper(g_bdev, &test_dir, new_dir_entry_adr, 64);
        if(!block_write_success){
            debugf("Fail at A\n");
        }
        debugf("test_dir.name: %s\n", test_dir.name);
        // while(1){

        // }
        debugf("parent_cache_entry->size: %d\n", parent_cache_entry->cached_inode.size);
        parent_cache_entry->cached_inode.size += 64;
        debugf("parent_cache_entry->size: %d\n", parent_cache_entry->cached_inode.size);
        // while(1){

        // }
        //Write to the block: Updated parent cache entry

        //Write: Updated parent Inode!
        Inode test_inode;
        block_write_success = block_read_wrapper(g_bdev, &test_inode, fs.inodes_start + (64 * (parent_cache_entry->inode_num - 1)), 64);
        if(!block_write_success){
            debugf("Fail at A\n");
        }
        debugf("test_inode.size: %d\n", test_inode.size);

        block_write_success = block_write_wrapper(g_bdev, &(parent_cache_entry->cached_inode), fs.inodes_start + (64 * (parent_cache_entry->inode_num - 1)), 64);
        if(!block_write_success){
            debugf("Fail at A\n");
        }
        block_write_success = block_read_wrapper(g_bdev, &test_inode, fs.inodes_start + (64 * (parent_cache_entry->inode_num - 1)), 64);
        if(!block_write_success){
            debugf("Fail at A\n");
        }
        debugf("test_inode.size: %d\n", test_inode.size);
        
        //Now we need to write the new inode to the block

        // Inode test_inode;
        // block_read_wrapper(g_bdev, &test_inode, fs.inodes_start + (64 * (inode_num - 1)), 64);
        // debugf("inode_num: %d\n", inode_num);
        // debugf("test_inode->size: %d\n", test_inode.size);
        // debugf("test_inode->mode: %d\n", test_inode.mode);
        // debugf("test_inode->zones[0]: %d\n", test_inode.zones[0]);
        // debugf("sizeof(Inode): %d\n", sizeof(Inode));
        // while(1){

        // }

        Inode test;

        block_write_success = block_write_wrapper(g_bdev, new_inode, fs.inodes_start + (64 * (inode_num - 1)), 64);
        if(!block_write_success){
            debugf("Fail at A\n");
        }
        block_write_success = block_read_wrapper(g_bdev, &test, fs.inodes_start + (64 * (inode_num - 1)), 64);
        if(!block_write_success){
            debugf("Fail at A\n");
        }
        // debugf("test.size: %d\n", test.size);
        //write to the file system that an inode was taken, and a zmap was taken
        block_write_success = block_write_wrapper(g_bdev, fs.imap_allocd, fs.imap_blocks_start, fs.block_size * fs.sb->imap_blocks);
        if(!block_write_success){
            debugf("Fail at A\n");
        }
        //update the file system to reflect new zblock was taken!
        block_write_success = block_write_wrapper(g_bdev, fs.zmap_allocd, fs.zmap_blocks_start, fs.block_size * fs.sb->zmap_blocks);
        if(!block_write_success){
            debugf("Fail at A\n");
        }

        // Dirent test_new;
        // block_read_wrapper(g_bdev, &test_new, (parent_inode.zones[0] * 1024) + (64 * 3), 64);
        // debugf("test_new.name: %s\n", test_new.name);
        // while(1){

        // }

        //Insert new file into cache. Will have to test this out!
        cacheEntry* new_cache_entry = (cacheEntry*)kmalloc(sizeof(cacheEntry));

        new_cache_entry->cached_inode = *(new_inode);
        new_cache_entry->inode_num = inode_num;
        new_cache_entry->num_entries = 0;
        
        char* new_cache_key = strdup(path_file_name(path));
        map_set(parent_cache_entry->directory_map, new_cache_key, (MapValue)new_cache_entry); 
        kfree(new_cache_key);
        curr_cache_entry = minix3_search_cache_wrapper(path);
        curr_fd_entry->file_pointer = curr_fd_entry->fp_block_offset;
        curr_fd_entry->total_read = 0;

        debugf("curr_cache_entry->inode_num: %d\n", curr_cache_entry->inode_num);
        debugf("curr_cache_entry->inode->size: %d\n", curr_cache_entry->cached_inode.size);
        


        // while(1){

        // }
        // debugf("Leaving file creation\n");
        // block_flush(g_bdev);
    }

    if(curr_cache_entry == NULL){
        debugf("fuck u\n");
        return -1;
    }
    curr_fd_entry->cache_entry = curr_cache_entry;
    //Set up zones!
    curr_fd_entry->zone_indices[0] = 0;
    curr_fd_entry->zone_indices[1] = -1;
    curr_fd_entry->zone_indices[2] = -1;
    curr_fd_entry->zone_indices[3] = -1;

    //Get inode
    curr_fd_entry->file_inode = &(curr_cache_entry->cached_inode);
    
    //Find the first legit datazone!
    if(curr_fd_entry->file_inode->zones[0] != 0){
        curr_fd_entry->fp_block_offset = curr_fd_entry->file_inode->zones[0];
    }else{
        debugf("Find first zone!\n");
        bool test = get_next_zone_wrapper(curr_fd_entry);
        if(test == false){
            debugf("Failure to get next zone!\n");
            
            return -1;
        }
    }
    //Set variables. These will change in either O_APPEND, or O_TRUNCATE, Or not get changed at all!
    curr_fd_entry->fp_byte_offset = 0;
    curr_fd_entry->file_pointer = curr_fd_entry->fp_block_offset * 1024;
    curr_fd_entry->total_read = 0;
    //Update variables so that we are at the end of the file!
    if(flags & O_APPEND){
        uint32_t number_of_zones = curr_fd_entry->file_inode->size / 1024;
        uint32_t left_over = curr_fd_entry->file_inode->size % 1024;
        uint32_t i = 0;

        for(i = 0; i < number_of_zones; i++){
            bool test = get_next_zone_wrapper(curr_fd_entry);
            if(test == false){
                debugf("FAILURE: %d\n", i);
                debugf("zones[0]: %d\n", curr_fd_entry->zone_indices[0]);
                debugf("zones[1]: %d\n", curr_fd_entry->zone_indices[1]);
                debugf("zones[2]: %d\n", curr_fd_entry->zone_indices[2]);
                debugf("zones[3]: %d\n", curr_fd_entry->zone_indices[3]);
                return -1;
            }
        }
        curr_fd_entry->fp_byte_offset = curr_fd_entry->file_inode->size % 1024;
        curr_fd_entry->file_pointer = (curr_fd_entry->fp_block_offset * 1024) + curr_fd_entry->fp_byte_offset;
        curr_fd_entry->total_read = curr_fd_entry->file_inode->size;
    }

    // truncate set size to zero. I think we should keep the zones as is. 
    if(flags & O_TRUNC){
        curr_fd_entry->file_inode->size = 0;
        curr_fd_entry->fp_byte_offset = 0;
        curr_fd_entry->file_pointer = curr_fd_entry->fp_block_offset * 1024;
        curr_fd_entry->total_read = 0;
    }
    
    if(flags & O_RDONLY){
        if(flags & O_WRONLY){
            return -1;
        }
        curr_fd_entry->read_enabled = true;
        curr_fd_entry->write_enabled = false;
    }
    if(flags & O_WRONLY){
        if(flags & O_RDONLY){
            return -1;
        }
        curr_fd_entry->write_enabled = true;
        curr_fd_entry->read_enabled = false;
    }
    // Inode test_inode;
    block_write_success = block_write_wrapper(g_bdev, (curr_fd_entry->file_inode), fs.inodes_start + (64 * (curr_fd_entry->cache_entry->inode_num - 1)), 64);
    
    if(!block_write_success){
            debugf("Fail at A\n");
        }// block_read_wrapper(g_bdev, &(test_inode), fs.inodes_start + (64 * (curr_fd_entry->cache_entry->inode_num - 1)), 64);
    // debugf("test_inode.size: %d\n", test_inode.size);
    // while(1){
        
    // }
    mutex_unlock(&fs.fd_table.lock);

    debugf("exit open\n");
    return file_entry_number;
}

//I think close is good for now. Need to review this with el team. :)
int close(int fd){

    fileDescriptorEntry* entry = &(fs.fd_table.entry[fd]);
    
    int return_code = 0;
    mutex_spinlock(&entry->lock);
    
    // check if taken
    if(entry->taken != 1){
        mutex_unlock(&entry->lock);
        return 0;
    }

    //Flush the inode to the file system. Do this incase the zones have changed at all!
    debugf("zones on close:\n");
    debugf("Zones[0] = %d\n", entry->file_inode->zones[0]);
    debugf("Zones[1] = %d\n", entry->file_inode->zones[1]);
    debugf("Zones[2] = %d\n", entry->file_inode->zones[2]);
    debugf("Zones[3] = %d\n", entry->file_inode->zones[3]);
    debugf("Zones[4] = %d\n", entry->file_inode->zones[4]);
    debugf("Zones[5] = %d\n", entry->file_inode->zones[5]);
    debugf("Zones[6] = %d\n", entry->file_inode->zones[6]);
    debugf("Zones[7] = %d\n", entry->file_inode->zones[7]);
    debugf("Zones[8] = %d\n", entry->file_inode->zones[8]);
    debugf("Zones[9] = %d\n", entry->file_inode->zones[9]);


    debugf("zone_indices[0]: %d\n", entry->zone_indices[0]);
    debugf("zone_indices[1]: %d\n", entry->zone_indices[1]);
    debugf("zone_indices[2]: %d\n", entry->zone_indices[2]);
    debugf("zone_indices[3]: %d\n", entry->zone_indices[3]);

    block_write_wrapper(g_bdev, (entry->file_inode), fs.inodes_start + (64 * (entry->cache_entry->inode_num - 1)), 64);
    entry->cache_entry = NULL;
    entry->file_pointer = 0;
    entry->fp_byte_offset = 0;
    entry->fp_block_offset = 0;
    entry->read_enabled = false;
    entry->total_read = 0;
    entry->write_enabled = false;
    entry->zone_indices[0] = 0;
    entry->zone_indices[1] = -1;
    entry->zone_indices[2] = -1;
    entry->zone_indices[3] = -1;
    entry->file_inode = NULL;
    entry->file_pointer = NULL;
    entry->taken = 0;
    // block_flush(g_bdev);
    mutex_unlock(&entry->lock);

    return return_code;
}

ssize_t read   (int fd, void *buf, size_t count){
    debugf("called read\n");

    //checking if you have a shit file descriptor 
    if (fd < 0 || fd >= MAX_FD_ENTRIES) {
        return -EBADF; // Bad file descriptor
    }

    //setting the file descriptor
    fileDescriptorEntry * entry = &fs.fd_table.entry[fd];

    //checking if the file descriptor is shit and somehow made it past the first shit detector
    if (!entry->taken) {
        return -EBADF; // File descriptor is not in use
    }

    //permissions. doing something wreong here. no worky. me comment out
    // if (!(entry.mode & READ_PERMISSION)) {
    //     return -EACCES; // No read permission
    // }

    //some random variables
    uint64_t read_bytes = 0;
    uint32_t max_read = 0;
    uint32_t curr_read = 0;
    char read_buf[BLOCK_SIZE];
    uint32_t pointer_buf[BLOCK_SIZE/sizeof(uint32_t)];
    bool worked;

    char *buf_idx;

    debugf("File Size: %i\n", entry->file_inode->size);
    debugf("Total Read Bytes: %i\n", entry->total_read);

    //checking if the user's dumb fucking ass put in a value that goes outside the bounds of the file.
    if ((count+entry->total_read) > entry->file_inode->size) {
        count = entry->file_inode->size - entry->total_read;
    }
    debugf("file_size: %d\n",entry->file_inode->size);
    debugf("total_read: %d\n", entry->total_read);
    debugf("read_bytes %i count %u\n", read_bytes, count);

    while(read_bytes < count){
        debugf("read_bytes: %d\n",read_bytes);
        //determining how much of the current block we can/need to read
        max_read = BLOCK_SIZE - entry->fp_byte_offset;
        if ((count-read_bytes) > max_read) {
            curr_read = max_read;
        }
        else {
            curr_read = count-read_bytes;
        }

        //hacking (reading data from) into the (a memory address in the) mainframe (block device)
        // worked = block_read_wrapper(fs.bd, read_buf, entry->file_pointer, curr_read);
        worked = block_read_wrapper(fs.bd, read_buf, entry->file_pointer, curr_read);
        //putting the read data in the buffer
        for (int i = read_bytes; i < curr_read+read_bytes; i++) {
            buf_idx = (char *)buf;
            *(buf_idx + i)  = read_buf[i-read_bytes];
        }
        // buf_idx+=1;
        // *buf_idx  = '\0';
        // DON'T NULL TERMINATE FOR THE USER!!!

        //shits broke
        if(!worked) {
            debugf("shits broke: read %u bytes\n", read_bytes);
            return read_bytes;
        }

        //updating how many bytes we read here in case we break while changing blocks
        read_bytes += curr_read; 

        //change blocks if we read all of the current one and we still need to read more
        if (curr_read == max_read){
            //using magic function to get the next block indices 
            worked = get_next_zone_wrapper(entry);
            // entry->fp_byte_offset = 0;
            //means something is broken. mission abort
            if (!worked){
                debugf("zone[0]: %d\n", entry->zone_indices[0]);
                debugf("zone[1]: %d\n", entry->zone_indices[1]);
                debugf("zone[2]: %d\n", entry->zone_indices[2]);
                debugf("zone[3]: %d\n", entry->zone_indices[3]);
                debugf("Failed to get next zone\n");
                while(1);
                break;
            }
            //direct zones
            if (entry->zone_indices[1] == -1) {
                entry->fp_block_offset = entry->file_inode->zones[entry->zone_indices[0]];
            }

            //single indirect zones
            else if (entry->zone_indices[2] == -1) {
                uint32_t spot = entry->file_inode->zones[7] * 1024;
                worked = block_read_wrapper(fs.bd, pointer_buf, spot, BLOCK_SIZE);
                if (!worked) {
                    debugf("WTFFFGDDDD\n");
                    while(1);
                    return read_bytes;
                }
                entry->fp_block_offset = pointer_buf[entry->zone_indices[1]];
            }

            else if (entry->zone_indices[3] == -1) {
                uint32_t spot = entry->file_inode->zones[8] * 1024;
                worked = block_read_wrapper(fs.bd, pointer_buf, spot, BLOCK_SIZE);
                if (!worked) {
                    debugf("WTFFFGDDDD\n");
                    while(1);
                    return read_bytes;
                }

                spot = pointer_buf[entry->zone_indices[1]] * 1024;
                worked = block_read_wrapper(fs.bd, pointer_buf, spot, BLOCK_SIZE);
                if (!worked) {
                    debugf("WTFFFGDDDD 2\n");
                    while(1);
                    return read_bytes;
                }

                entry->fp_block_offset = pointer_buf[entry->zone_indices[2]];
            }
            else{
                uint32_t spot = entry->file_inode->zones[9] * 1024;
                worked = block_read_wrapper(fs.bd, pointer_buf, spot, BLOCK_SIZE);
                if (!worked) {
                    debugf("WTFFFGDDDD\n");
                    while(1);
                    return read_bytes;
                }

                spot = pointer_buf[entry->zone_indices[1]] * 1024;
                worked = block_read_wrapper(fs.bd, pointer_buf, spot, BLOCK_SIZE);
                if (!worked) {
                    debugf("WTFFFGDDDD 2\n");
                    while(1);
                    return read_bytes;
                }

                spot = pointer_buf[entry->zone_indices[2]] * 1024;
                worked = block_read_wrapper(fs.bd, pointer_buf, spot, BLOCK_SIZE);
                if (!worked) {
                    debugf("WTFFFGDDDD 3\n");
                    while(1);
                    return read_bytes;
                }

                entry->fp_block_offset = pointer_buf[entry->zone_indices[3]];
            }

            
            entry->file_pointer = entry->fp_block_offset*BLOCK_SIZE;
            entry->fp_byte_offset = 0;

        }
        else {
            entry->file_pointer += curr_read;
            entry->fp_byte_offset += curr_read;
        }
    }
    
    entry->total_read += read_bytes;
    return read_bytes;
    
}

ssize_t write(int fd, const void *buf, size_t count){

    fileDescriptorEntry* fd_entry = &fs.fd_table.entry[fd];
    // debugf("test\n");
    mutex_spinlock(&fd_entry->lock);
    
    if(fs.fd_table.entry[fd].taken == 0){
        mutex_unlock(&fd_entry->lock);
        return -1;
    }

    if(buf == NULL){
        mutex_unlock(&fd_entry->lock);
        return -1;
    }

    if(count == 0){
        mutex_unlock(&fd_entry->lock);
        return -1;
    }

    //Tells you how many bytes are remaining in the current data zone!
    uint32_t bytes_remaining_current_zone = (1024 - fd_entry->fp_byte_offset);
    debugf("Bytes_remaining_current_zone: %d\n", bytes_remaining_current_zone);
    uint32_t count_remaining = count - bytes_remaining_current_zone;
    uint32_t number_of_full_data_blocks = count_remaining / 1024;
    uint32_t bytes_in_last_zone = count_remaining % 1024;

    bool allocated_zone_found;
    bool new_zone_allocated;
    bool zone_written_to;
    char* temp_buffer = NULL;

    uint32_t num_bytes_to_write = 0;
    uint32_t num_bytes_left_to_write = count;
    uint32_t num_bytes_written = 0;
    

    //First figure out if where we are in the current file is at the end of a zone
    //If it is, we need to go to the next zone first.
    //If there are no more allocated zones, allocate a new zone!
    if(bytes_remaining_current_zone == 0){
        debugf("\n\n\n\n\nMust get to next zone!\n");
        allocated_zone_found = get_next_zone_wrapper(fd_entry);
        if(allocated_zone_found == false){
            new_zone_allocated = alloc_new_zone(fd_entry);
            if(new_zone_allocated == false){
                mutex_unlock(&fd_entry->lock);
                return 0;
            }
            block_write_wrapper(g_bdev, (fd_entry->file_inode), fs.inodes_start + ((fd_entry->cache_entry->inode_num - 1) * 64), 64);
        }
        fd_entry->file_pointer = fd_entry->fp_block_offset * 1024;
    }//Write count remaing bytes to the current zone because it has space to do so
    else{
        debugf("\n\n\n\nCurrent zone has space to write to, %d\n", bytes_remaining_current_zone);

        if(count <= bytes_remaining_current_zone) num_bytes_to_write = count;
        else num_bytes_to_write = bytes_remaining_current_zone;

        block_write_wrapper(g_bdev, buf, fd_entry->fp_byte_offset + (fd_entry->fp_block_offset * fs.block_size), num_bytes_to_write);
        num_bytes_written += num_bytes_to_write;
        num_bytes_left_to_write -= num_bytes_to_write;

        fd_entry->fp_byte_offset += num_bytes_to_write;
        fd_entry->total_read += num_bytes_to_write;

        //Update size and flush to file system if needed
        if(fd_entry->total_read > fd_entry->file_inode->size){
            fd_entry->file_inode->size = fd_entry->total_read;
            block_write_wrapper(g_bdev, (fd_entry->file_inode), fs.inodes_start + ((fd_entry->cache_entry->inode_num - 1) * 64), 64);
        }

        fd_entry->file_pointer += num_bytes_to_write;
        //If we wrote to the end of this block, then find the next block to write to, allocate if needed!
        if(num_bytes_to_write == bytes_remaining_current_zone){
            allocated_zone_found = get_next_zone_wrapper(fd_entry);
            if(allocated_zone_found == false){
                new_zone_allocated = alloc_new_zone(fd_entry);
                if(new_zone_allocated == false){
                    mutex_unlock(&fd_entry->lock);
                    return num_bytes_written;
                }
                //Flush the inode since we had to allocate, incase we we needed to update zones!
                block_write_wrapper(g_bdev, (fd_entry->file_inode), fs.inodes_start + ((fd_entry->cache_entry->inode_num - 1) * 64), 64);
                fd_entry->fp_byte_offset = 0;
                fd_entry->file_pointer = fd_entry->fp_block_offset * 1024;
            }
        }
        else{
            mutex_unlock(&fd_entry->lock);
            return count;
        }
    }

    //Once we get down here, we should have the case where the current zone was written to, and there was enough space so we returned,
    // OR we need to continue writing bytes. In either case, we can start writing out the rest of the bytes in full zone chunks until
    //  We hit the end. 

    debugf("Entering full zone writing!\n");

    //This section will write out full bytes until we hit the case that there won't be a full zone of bytes to write
    num_bytes_to_write = 1024;
    temp_buffer = (char*)kmalloc(num_bytes_to_write);
    debugf("num_bytes_left_to_write: %d\n", num_bytes_left_to_write);
    while(num_bytes_left_to_write >= 1024){
        debugf("num_bytes_left_to_write: %d\n", num_bytes_left_to_write);

        debugf("Full zone loop\n");
        debugf("num_bytes_left_write: %d\n", num_bytes_left_to_write);
        char test[num_bytes_to_write + 1];
        test[num_bytes_to_write] = '\0';

        fd_entry->fp_byte_offset = 0;
        debugf("Writing a full zone\n");
        num_bytes_left_to_write -= num_bytes_to_write;
        //memcpy(temp_buffer, buf + num_bytes_written, 1024);
        memcpy(&test, buf + num_bytes_written, 1024);
        debugf("num_bytes_written: %d\n", num_bytes_written);
        //debugf("From user buffer:\n%s\n", test);
        zone_written_to = block_write_wrapper(g_bdev, (uint64_t)(buf + num_bytes_written), fd_entry->fp_block_offset * fs.block_size, 1024);
        
        if(zone_written_to == false){
            debugf("Full zone was not written to!\n");
            mutex_unlock(&fd_entry->lock);
            kfree(temp_buffer);
            return num_bytes_written;
        }

        fd_entry->total_read += 1024;
        if(fd_entry->total_read > fd_entry->file_inode->size){
            fd_entry->file_inode->size = fd_entry->total_read;
            block_write_wrapper(g_bdev, (fd_entry->file_inode), fs.inodes_start + ((fd_entry->cache_entry->inode_num - 1) * 64), 64);
            // Inode test_inode;

            // block_read_wrapper(g_bdev, &test_inode, fs.inodes_start + ((fd_entry->cache_entry->inode_num - 1) * 64), 64);
            // debugf("inode num: %d\n", fd_entry->cache_entry->inode_num);
            // debugf("file size: %d\n", test_inode.size);
            // debugf("zone[1]: %d\n", test_inode.zones[1]);
            // while(1){

            // }
        }

        num_bytes_written += 1024;
        //block_read_wrapper(g_bdev, &test, fd_entry->fp_byte_offset + (fd_entry->fp_block_offset * fs.block_size), num_bytes_to_write);
        //debugf("Read:\n%s\n", test);
        //Go to the next zone, allocate one if needed!
        allocated_zone_found = get_next_zone_wrapper(fd_entry);
        if(allocated_zone_found == false){
            debugf("Allocating a new zone!\n");
            debugf("fd_entry->block_ptr: %x\n", fd_entry->fp_block_offset);
            new_zone_allocated = alloc_new_zone(fd_entry);
            if(new_zone_allocated == false){
                mutex_unlock(&fd_entry->lock);
                return num_bytes_written;
            }
            block_write_wrapper(g_bdev, (fd_entry->file_inode), fs.inodes_start + ((fd_entry->cache_entry->inode_num - 1) * 64), 64);
        }
        fd_entry->file_pointer = fd_entry->fp_block_offset * 1024;
        debugf("fd_entry->block_ptr: %x\n", fd_entry->fp_block_offset);
        fd_entry->fp_byte_offset = 0;
    }

    //Once we have exhausted full blocks, then we can move onto writing the remaining bytes to the last zone
    debugf("Exiting full block write section!\n");
    kfree(temp_buffer);

    //Check if we are done writing.
    if(num_bytes_left_to_write == 0){
        debugf("Return after full zone\n");
        mutex_unlock(&fd_entry->lock);
        block_write_wrapper(g_bdev, (fd_entry->file_inode), fs.inodes_start + ((fd_entry->cache_entry->inode_num - 1) * 64), 64);
        return num_bytes_written;
    }

    //write the remaining bytes to the file!
    zone_written_to = block_write_wrapper(g_bdev, buf + num_bytes_written, fd_entry->fp_block_offset * fs.block_size, num_bytes_left_to_write);

    if(zone_written_to == false){
        mutex_unlock(&fd_entry->lock);
        return num_bytes_written;
    }
    fd_entry->file_pointer += num_bytes_left_to_write;
    num_bytes_written += num_bytes_left_to_write;
    fd_entry->fp_byte_offset = num_bytes_left_to_write;
    fd_entry->total_read += num_bytes_left_to_write;
    
    if(fd_entry->total_read > fd_entry->file_inode->size){
        fd_entry->file_inode->size = fd_entry->total_read;
    }
    
    // debugf("byte_offset: %d\n", fd_entry->fp_byte_offset);
    debugf("Zones[0]: %d\n", fd_entry->zone_indices[0]);
    debugf("Zones[1]: %d\n", fd_entry->zone_indices[1]);
    debugf("Zones[2]: %d\n", fd_entry->zone_indices[2]);
    debugf("Zones[3]: %d\n", fd_entry->zone_indices[3]);

    block_write_wrapper(g_bdev, (fd_entry->file_inode), fs.inodes_start + ((fd_entry->cache_entry->inode_num - 1) * 64), 64);

    mutex_unlock(&fd_entry->lock);
    return num_bytes_written;

}

off_t   lseek  (int fd, off_t offset, int whence){
    //checking if you have a shit file descriptor 
    if (fd < 0 || fd >= MAX_FD_ENTRIES) {
        return -EBADF; // Bad file descriptor
    }

    //setting the file descriptor
    fileDescriptorEntry * entry = &fs.fd_table.entry[fd];

    //checking if the file descriptor is shit and somehow made it past the first shit detector
    if (!entry->taken) {
        return -EBADF; // File descriptor is not in use
    }

    uint64_t ending_block = 0;
    uint64_t ending_byte = 0;
    uint64_t skipped = 0;
    uint64_t starting_block = 0;
    uint64_t starting_byte = 0;
    uint32_t pointer_buf[BLOCK_SIZE/sizeof(uint32_t)];

    bool worked;

    if (whence == SEEK_CUR) {
        offset = offset+entry->total_read;
        whence = SEEK_SET;  
    }

    if (whence == SEEK_END) {
        offset += entry->file_inode->size;
        whence = SEEK_SET;
    }

    if (whence == SEEK_SET) {
        entry->fp_byte_offset = 0;
        entry->fp_block_offset = entry->file_inode->zones[0];
        entry->zone_indices[0] = 0;
        entry->zone_indices[1] = -1;
        entry->zone_indices[2] = -1;
        entry->zone_indices[3] = -1;
        entry->file_pointer = entry->fp_block_offset * BLOCK_SIZE;
        entry->total_read = 0;

        ending_block = offset/BLOCK_SIZE;
        ending_byte = offset%BLOCK_SIZE;
    }

    if (ending_block < 0 || ending_byte < 0) {
        return -EINVAL;
    }

    for(int i = starting_block; i < ending_block; i++) {
        worked = get_next_zone_wrapper(entry);
        //means something is broken. mission abort
        if (!worked) break;

        //direct zones
        if (entry->zone_indices[1] == -1) {
            entry->fp_block_offset = entry->file_inode->zones[entry->zone_indices[0]];
        }

        //single indirect zones
        else if (entry->zone_indices[2] == -1) {
            uint32_t spot = entry->file_inode->zones[7] * 1024;
            worked = block_read_wrapper(fs.bd, pointer_buf, spot, BLOCK_SIZE);
            if (!worked) {
                debugf("WTFFFGDDDD\n");
                return 20000;
            }
            entry->fp_block_offset = pointer_buf[entry->zone_indices[1]];
        }

        else if (entry->zone_indices[3] == -1) {
            uint32_t spot = entry->file_inode->zones[8] * 1024;
            worked = block_read_wrapper(fs.bd, pointer_buf, spot, BLOCK_SIZE);
            if (!worked) {
                debugf("WTFFFGDDDD\n");
                return 20000;
            }

            spot = pointer_buf[entry->zone_indices[1]] * 1024;
            worked = block_read_wrapper(fs.bd, pointer_buf, spot, BLOCK_SIZE);
            if (!worked) {
                debugf("WTFFFGDDDD 2\n");
                return 20000;
            }

            entry->fp_block_offset = pointer_buf[entry->zone_indices[2]];
        }
        else{
            uint32_t spot = entry->file_inode->zones[9] * 1024;
            worked = block_read_wrapper(fs.bd, pointer_buf, spot, BLOCK_SIZE);
            if (!worked) {
                debugf("WTFFFGDDDD\n");
                return 20000;
            }

            spot = pointer_buf[entry->zone_indices[1]] * 1024;
            worked = block_read_wrapper(fs.bd, pointer_buf, spot, BLOCK_SIZE);
            if (!worked) {
                debugf("WTFFFGDDDD 2\n");
                return 20000;
            }

            spot = pointer_buf[entry->zone_indices[2]] * 1024;
            worked = block_read_wrapper(fs.bd, pointer_buf, spot, BLOCK_SIZE);
            if (!worked) {
                debugf("WTFFFGDDDD 3\n");
                return -20000;
            }

            entry->fp_block_offset = pointer_buf[entry->zone_indices[3]];
        }

            
        skipped+=BLOCK_SIZE;
    }

    entry->file_pointer = entry->fp_block_offset*BLOCK_SIZE + ending_byte;
    entry->fp_byte_offset = ending_byte;
    entry->total_read += ending_byte;
    entry->total_read += skipped;
    skipped += ending_byte;

    return skipped; 

}



int unlink (const char *path){
    // dirent *entry = find_dirent(path);
    // if (entry == NULL){
    //     return -1;
    // }
    // int num_dir_entries =uint32_t num_dir_entries = curr_inode.size / DIR_ENTRY_SIZE;
    // inode *file_inode = dirent->entry-> inode_num;
    // if (inode_get_type(file_inode) == S_IFDIR) {
    //     //debugf("inode unlink: Entry is a directory")
    //     return -1; // Is a directory not a file
    // }
    // if (inode_get_type(file_inode) == S_IFREG){
    //     debug_f("inode unlike: have file");

    // }
    // file_inode->nlinks--;
    // inode_put_in_cache(file_inode); //whatever we are calliung this for cache when its done

    // //if links are 0 we need to remove it
    // if(new_node->nlinks ==0){
    //    bitmap_clear(block_bitmap, block_number);
    // }



    // //Essentially free's up the file space for this path. 

    // //Error checking:
    // //  Make sure it is a file we are unlinking, OR, it can also be a hardlink to a directory!

    // remove_dirent(entry); //remove directory entry
    // return 0;

}

int chmod(const char *path, mini_mode_t mode){
    cacheEntry* entry = minix3_search_cache_wrapper(path);

    if (entry == NULL){
        return -ENOENT; // No such file or directory
    }

    //check if the user has permissions (owner) (think this might be something to do in our user level mapping for this function)
    //might not be that important we will have to see.

    //preserves file type but rewrites permission bits. 
    entry->cached_inode.mode = (entry->cached_inode.mode & ~0777) | (mode & 0777);

    uint32_t byte_offset = fs.inodes_start + (entry->inode_num-1)*sizeof(Inode);

    //write the change to disk for persistence.
    bool check = block_write_wrapper(fs.bd, (uint64_t)&entry->cached_inode, byte_offset, sizeof(Inode));

    if (check == false){
        return -EIO; // IO Error - Data was not able to write. 
    }

    return 0;
}

int minix3_find_next_free_inode(BlockDevice* bdev){

    //Read the inode bitmap
    char* inode_bitmap = (char*)kmalloc(fs.sb->num_inodes);

    block_read_wrapper(bdev, (uint64_t)inode_bitmap, fs.imap_blocks_start, fs.sb->num_inodes);

    int i = 0;
    int inode_num = 0;
    for(i = 1; i < fs.sb->num_inodes; i++){
        if(inode_bitmap[i] == 0){
            inode_num = i;
            break;
        }
    }

    kfree(inode_bitmap);

    return inode_num;
}

int minix3_find_next_free_zone(BlockDevice* bdev){

    //Read the inode bitmap
    char* zone_bitmap = (char*)kmalloc(fs.sb->zmap_blocks);

    block_read_wrapper(bdev, (uint64_t)zone_bitmap, fs.zmap_blocks_start, fs.sb->zmap_blocks);

    int i = 0;
    int zone_num = 0;
    for(i = 1; i < fs.sb->num_inodes; i++){
        if(zone_bitmap[i] == 0){
            zone_num = i;
            break;
        }
    }
    
    kfree(zone_bitmap);

    return zone_num;
}

int mkdir(const char *path, mini_mode_t mode){
    

    if (path == NULL || *path == '\0' || mode == NULL){
        return -1;
    }

    //Check if directory exists already. If so, return -1

    cacheEntry* dir_entry_search = minix3_search_cache_wrapper(path);

    if(dir_entry_search != NULL && S_ISDIR(dir_entry_search->cached_inode.mode)){
        return -1;
    }

    //Assuming we have the cache set up!

    // Go through the cache based on the path given.
    //What we should get back from the cache is the parent inode

    //mkdir needs to do the following:

    //We need to first check if the directory already exists! If it does, then we return -1

    //If the directory doesn't exist, then we find a free inode, this inode will be for this directory

    //Once we get a free inode, we need to mark that inode as taken, and also set 
    //  The inode information in the inode zone. 

    //We then need to add a directory entry into the parent directory, which is going to just
    //  be a direcotry struct. we set the inode of that entry to be the new inode
    //  We also have to set the name of the entry to be the last part of the path that
    //  was handed to us

    //We also need to store the parent directory inode
    //  The parent inode will have to be stored as a directory entry of the new directory
    //  The other directory entry that will need to be stored is the new directory
    //  We do this because every directory has at least the . and the .. directory, which is the 
    //  Parent directory and the current directory respectively

}


int rmdir  (const char *path){

    // if (path == NULL || *path = '\0'){
    //     return -1;
    //     //check if path is empty
    // }
    // //check if path is root or current directory

    // if(strcmp(path,"/") ==0 || strcmp(path,".") ==0){
    //     return -1
    // }

    //  inode *dir_inode = find_inode_by_path(fs, path); //or however we are getting it from the cache


    //  if (dir_inode == NULL){
    //     return -1;
    //  }

    //  if((dir_inode ->mode != S_IFDIR)){
    //     return -=1
    //  }



    
    //All this really does is set the indode as free.

    //We first need to error check that the path exists. 
    //If the path exists, BUT the directory is NOT empty, we need to return -1
    //  Skip the first two entries since that is literally just the . and .. part

    //Once we confirm we can remove the directory, we need to go to the parent and remove
    // The entry for the current directory

    //I wonder if we should move all the directoy entries up one once we remove the child directory
    //  from the parent...? --> Can talk about this!

    //Once the parent directory entry is removed, we then free the current inode. which just
    //  sets the imap entry free, and we also need to mark all the zones as free as well!

}
int chdir  (const char *path){
    // dirent *entry = find_dirent(path);
    // if (path == NULL || *path == '\0') {
    //     return -1;
    // }

    //  inode *dir_inode = find_inode_by_path(fs, resolved_path); //again find_inode by path from cache? based on what brandan said

    // if (dir_node == NULL){
    //     return -1;
    // }
    // if (dir_inode->mode != S_IFDIR){
    //     return -1;
    // } 





    //So pretty much we just set the current working dir to be the path that
    // Was passed in

    // We first need to error check if the path exists, AND, that the path is a pointing to
    // A directory. If not, then we return -1

    //Once error checking is done, we set the CWD of the current proccess
    // to be the path that was specified

}
// int     getcwd (char *buf, size_t bufsize){}
// int     mknod  (const char *path, mini_mode_t mode, uint64_t dev){}


//sleep
//This would be really easy to implement. We already basically have a function to do it in sched.c
//remember, sleep_until() in sched.c