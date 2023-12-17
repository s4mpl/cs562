#ifndef _BLOCK_H
#define _BLOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <virtio.h>

typedef struct virtio_blk_config {
   uint64_t capacity;
   uint32_t size_max;
   uint32_t seg_max;
   struct virtio_blk_geometry {
      uint16_t cylinders;
      uint8_t heads;
      uint8_t sectors;
   } geometry;
   uint32_t blk_size;
   struct virtio_blk_topology {
      uint8_t physical_block_exp;
      uint8_t alignment_offset;
      uint16_t min_io_size;
      uint32_t opt_io_size;
   } topology;
   uint8_t writeback;
   uint8_t unused0[3];
   uint32_t max_discard_sectors;
   uint32_t max_discard_seg;
   uint32_t discard_sector_alignment;
   uint32_t max_write_zeroes_sectors;
   uint32_t max_write_zeroes_seg;
   uint8_t write_zeroes_may_unmap;
   uint8_t unused1[3];
}virtio_blk_config;

typedef struct BlockDevice {
   virtio_blk_config* cfg;
   VirtioDevice* vdev;
} BlockDevice;

#define SECTOR_SIZE                 512

#define VIRTIO_BLK_T_IN             0
#define VIRTIO_BLK_T_OUT            1
#define VIRTIO_BLK_T_FLUSH          4
#define VIRTIO_BLK_T_DISCARD        11
#define VIRTIO_BLK_T_WRITE_ZEROES   13
#define VIRTIO_BLK_S_OK             0
#define VIRTIO_BLK_S_IOERR          1
#define VIRTIO_BLK_S_UNSUPP         2

typedef struct virtio_blk_req {
   // First descriptor (header).
   uint32_t type; // IN / OUT
   uint32_t reserved;
   uint64_t sector; // start sector
   // Third descriptor (status).
   uint8_t status;
} BlockRequest;

extern List *g_block_devices; /* Global list for block devices. */
extern BlockDevice *g_bdev;

/**
 * @brief init function called from virtio on finding block device
*/
BlockDevice *block_init(VirtioDevice *vdev);
bool block_write_wrapper(BlockDevice *bdev, uint64_t buf_addr, uint32_t byte_num, uint32_t size);
bool block_read_wrapper(BlockDevice *bdev, uint64_t buf_addr, uint32_t byte_num, uint32_t size);
bool block_flush(BlockDevice *bdev);

#endif