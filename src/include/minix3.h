#include <stddef.h>
#include <stdint.h>
#include <list.h>
#include <map.h>
#include <block.h>
#include <debug.h>
#include <kmalloc.h>
#include <lock.h>
#include <stat.h>
#include <errno.h> 

#define MINIX3_MAGIC 0x4D5A

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2 

#define BLOCK_SIZE 1024 // Block size in bytes
#define PTRS_PER_BLOCK (BLOCK_SIZE / sizeof(uint32_t)) // Number of pointers per block

typedef unsigned long  size_t;
typedef signed long    ssize_t;
typedef unsigned short mini_mode_t;


typedef struct minix3_superblock {
  uint32_t num_inodes;
  uint16_t pad0;
  uint16_t imap_blocks;
  uint16_t zmap_blocks;
  uint16_t first_data_zone;
  uint16_t log_zone_size;
  uint16_t pad1;
  uint32_t max_size;
  uint32_t num_zones;
  uint16_t magic;
  uint16_t pad2;
  uint16_t pad3;
  uint8_t  disk_version;
  uint8_t  pad4;
} superblock;

#define NUM_ZONES 10    // 10 zone pointers
#define ZONE_INDIR 7    // Indirect zone is index 7
#define ZONE_DINDIR 8   // Doubly-indirect zone is index 8
#define ZONE_TINDIR 9   // Triply-indirect zone is index 9

#define NUM_POINTERS (1024 / 4)

#define zone_adr(x) (x * 1024)

#define DIR_ENTRY_SIZE 64

#define FS_NUM_CACHE_ENTRIES 1

typedef struct minix3_inode {
  uint16_t mode;
  uint16_t nlinks;
  uint16_t uid;
  uint16_t gid;
  uint32_t size;
  uint32_t atime;
  uint32_t mtime;
  uint32_t ctime;
  uint32_t zones[NUM_ZONES];
} Inode;

#define DIR_ENTRY_NAME_SIZE 60
typedef struct minix3_dir_entry {
  uint32_t inode_num;
  char name[DIR_ENTRY_NAME_SIZE];
} Dirent;

typedef struct cacheEntry {
  Inode cached_inode;
  Map* directory_map;
  uint32_t inode_num;
  uint32_t num_entries;
} cacheEntry;

void minix3_init_system(List* block_devs);
void minix3_init_dev(BlockDevice* bdev);

#define MAX_FD_ENTRIES 256
typedef struct fileDescriptorEntry {
  Inode *file_inode;
  uint64_t fp_byte_offset;  // byte offset into the data block itself (in the zone)
  uint64_t fp_block_offset; // byte offset into the block device (of the zone)
  uint32_t zone_indices[4]; // indices into each level of zones pointers. Not sure what this is?
  uint8_t taken;
  int flags;
  bool read_enabled;
  bool write_enabled;
  mini_mode_t mode;
  char* file_pointer;//Where you are in the entire file system
  // uint64_t buf_size;
  uint64_t total_read; //Where you are in the actual file
  cacheEntry* cache_entry;
  Mutex lock;
} fileDescriptorEntry;

typedef struct fileDescriptorTable {
  fileDescriptorEntry entry[MAX_FD_ENTRIES];
  Mutex lock;
} fileDescriptorTable;

typedef struct FileSystem {
  superblock *sb;
  BlockDevice *bd; 
  uint16_t ecam_device_id;
  uint32_t block_size;
  uint64_t imap_blocks_start;
  uint64_t zmap_blocks_start;
  uint64_t inodes_start;
  uint64_t zones_start;
  uint8_t *imap_allocd; // for buffered reads/writes to the disk
  uint8_t *zmap_allocd; // for buffered reads/writes to the disk
  Inode *inodes;
  uint64_t num_inodes;
  uint64_t num_zones;
  uint32_t new_zone_pointer_add;
  // Map of dirents keyed on path (relative or absolute?)? how to make it recursive?
  Map* dirent_cache;
  fileDescriptorTable fd_table;
} FileSystem;



int     stat   (const char *path, struct m_stat *stat);
int     open   (const char *path, int flags, mini_mode_t mode);
int     close  (int fd);
ssize_t read   (int fd, void *buf, size_t count);
ssize_t write  (int fd, const void *buf, size_t count);
off_t   lseek  (int fd, off_t offset, int whence);
int     unlink (const char *path);
int     chmod  (const char *path, mini_mode_t mode);
int     mkdir  (const char *path, mini_mode_t mode);
int     rmdir  (const char *path);
int     chdir  (const char *path);
int     getcwd (char *buf, size_t bufsize);
int     mknod  (const char *path, mini_mode_t mode, uint64_t dev);



#define O_RDONLY        00000000
#define O_WRONLY        00000001
#define O_RDWR          00000002
#define O_CREAT         00000100        
#define O_EXCL          00000200        
#define O_TRUNC         00001000
#define O_APPEND        00002000