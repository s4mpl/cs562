#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <virtio.h>
#include <debug.h>
#include <util.h>
#include <kmalloc.h>
#include <mmu.h>
#include <block.h>
#include <pci.h>

List *g_block_devices = NULL;
BlockDevice *g_bdev = NULL;

BlockDevice *block_init(VirtioDevice *vdev){
    //get capability
    unsigned char capes_next = vdev->pcidev->ecam->common.capes_pointer;
    unsigned long cap_addr = (unsigned long)vdev->pcidev->ecam+capes_next;
    VirtioCapability* v_cap = VIRTIO_CAP(cap_addr);
    
    // get config cape
    while(capes_next != 0){
        v_cap = VIRTIO_CAP((uint64_t)vdev->pcidev->ecam + v_cap->next);
        if(v_cap->type == VIRTIO_PCI_CAP_DEVICE_CFG ){
            break;
        }

        capes_next = v_cap->next;
    }
    if (!v_cap){
        //debugf("block_init: v_cap is not set for device\n");
        return NULL;
    }

    //make list if null
    if(!g_block_devices) g_block_devices = list_new();

    uint8_t bar_num = v_cap->bar;
    uint32_t *bar_addr = vdev->pcidev->ecam->type0.bar + bar_num;
    uint64_t MMIO_addr = ((((*bar_addr >> 1) & 0b11) == 0b10) ? (*(uint64_t *)bar_addr) & ~0xf : (*(uint32_t *)bar_addr)) & ~0xf;
    uint64_t block_cfg_adr = MMIO_addr + v_cap->offset;
    virtio_blk_config* blk_mmio_cfg = (virtio_blk_config*) block_cfg_adr;

    //make block device and append to list
    BlockDevice* new_block_device = (BlockDevice*)kzalloc(sizeof(BlockDevice));
    new_block_device->cfg = blk_mmio_cfg;
    new_block_device->vdev = vdev;
    list_add_ptr(g_block_devices, new_block_device);

    //debugf("block_init: Capacity:0x%x \n", blk_mmio_cfg->capacity);
    //debugf("block_init: size_max: 0x%x\n", blk_mmio_cfg->size_max);
    //debugf("block_init: seg_max: 0x%x\n", blk_mmio_cfg->seg_max);
    //geometry
    //debugf("block_init: cylinders: 0x%x\n", blk_mmio_cfg->geometry.cylinders);
    //debugf("block_init: heads: 0x%x\n", blk_mmio_cfg->geometry.heads);
    //debugf("block_init: sectors: 0x%x\n", blk_mmio_cfg->geometry.sectors);
    //topology
    //debugf("block_init: physical_block_exp: 0x%x\n", blk_mmio_cfg->topology.physical_block_exp);
    //debugf("block_init: alignment_offset: 0x%x\n", blk_mmio_cfg->topology.alignment_offset);
    //debugf("block_init: min_io_size: 0x%x\n", blk_mmio_cfg->topology.min_io_size);
    //debugf("block_init: opt_io_size: 0x%x\n", blk_mmio_cfg->topology.opt_io_size);
   

    //block size (Virtio_BLK_F_SIZE)
    //debugf("block_init: BlK size: 0x%x\n", blk_mmio_cfg->blk_size);

    return new_block_device;
};

static bool block_read(BlockDevice *bdev, uint64_t buf_addr, uint32_t byte_num, uint32_t size) {
    uint64_t header_phys, data_phys, status_phys;
    uint16_t at_idx, q_size;
    BlockRequest request;

    //check if device enabled
    if((bdev->vdev->common_cfg->device_status & VIRTIO_F_DRIVER_OK) == 0x0){
        return false;
    }

    //translate for device b/c devices deal in physical
    header_phys = mmu_translate_ptr_to_u64(kernel_mmu_table, &request);
    data_phys = mmu_translate(kernel_mmu_table, buf_addr);
    status_phys = mmu_translate_ptr_to_u64(kernel_mmu_table, &(request.status));

    // debugf("block_read: header 0x%08x, data 0x%08x, status 0x%08x\n", header_phys, data_phys, status_phys);

    request.sector = byte_num / SECTOR_SIZE; // really bdev->cfg->blk_size
    request.type = VIRTIO_BLK_T_IN;
    request.status = 0b111;
    
    //get index
    at_idx = bdev->vdev->driver_idx;

    //get q size for modding
    q_size = bdev->vdev->common_cfg->queue_size;

    //lock device before we start filling the descriptor
    mutex_spinlock(&bdev->vdev->lock);
    // debugf("block_read: locked!\n");

    //descriptor array
    VirtioDescriptor* descriptors = (VirtioDescriptor*) bdev->vdev->common_cfg->queue_desc;

    //fill header descriptor fields
    VirtioDescriptor* head_descr = &descriptors[at_idx];
    head_descr->addr = header_phys;
    head_descr->len = 16;   // this is hard coded b\c the struct math doesn't work b\c of aligning
    head_descr->flags = VIRTQ_DESC_F_NEXT;
    head_descr->next = (at_idx + 1) % q_size;

    //fill data descriptor fields
    VirtioDescriptor* data_descr = &descriptors[(at_idx + 1) % q_size];
    data_descr->addr = data_phys;
    data_descr->len = ALIGN_UP_POT((byte_num % SECTOR_SIZE) + size, SECTOR_SIZE);
    data_descr->flags = VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE;
    data_descr->next = (at_idx + 2) % q_size;

    //fill status descriptor fields
    VirtioDescriptor* status_descriptor = &descriptors[(at_idx + 2) % q_size];
    status_descriptor->addr = status_phys;
    status_descriptor->len = sizeof(uint8_t);
    status_descriptor->flags = VIRTQ_DESC_F_WRITE;
    status_descriptor->next = 0;
    
    //set descriptor in driver ring
    bdev->vdev->driver->ring[bdev->vdev->driver->idx % q_size] = at_idx;
    bdev->vdev->driver->idx += 1;
    bdev->vdev->driver_idx = (bdev->vdev->driver_idx + 1) % q_size;

    //unlock mutex
    mutex_unlock(&bdev->vdev->lock);
    // debugf("block_read: unlocked!\n");

    //notify
    virtio_notify(bdev->vdev, bdev->vdev->common_cfg->queue_select);
    debugf("block_read: notifying register at address 0x%08x\n", bdev->vdev->notify);
    *(bdev->vdev->notify) = 0; // doesn't matter what we read here, I guess--Marz reads 0
    while(bdev->vdev->isr->queue_interrupt != 0){
        // debugf("While loop ... waiting ... \n");
    }
    // debugf("made it!!!!!!\n");

    //grab numbers
    while(bdev->vdev->device_idx != bdev->vdev->device->idx){
        VirtioDescriptor* descr = (VirtioDescriptor*) bdev->vdev->common_cfg->queue_desc + bdev->vdev->device_idx;
        // debugf("at idx %d\n", at_idx);
        // debugf("descr length: 0x%x idx1: %d idx2: %d\n", descr->len,bdev->vdev->device_idx, bdev->vdev->device->idx);
        bdev->vdev->device_idx += 1;
    }

    debugf("block_read: status %u\n", request.status);
    return (request.status == VIRTIO_BLK_S_OK);
};


static bool block_write(BlockDevice *bdev, uint64_t buf_addr, uint32_t byte_num, uint32_t size) {
    uint64_t header_phys, data_phys, status_phys;
    uint16_t at_idx, q_size;
    BlockRequest request;

    //check if device enabled
    if((bdev->vdev->common_cfg->device_status & VIRTIO_F_DRIVER_OK) == 0x0){
        return false;
    }

    //translate for device b/c devices deal in physical
    header_phys = mmu_translate_ptr_to_u64(kernel_mmu_table, &request);
    data_phys = mmu_translate(kernel_mmu_table, buf_addr);
    status_phys = mmu_translate_ptr_to_u64(kernel_mmu_table, &(request.status));

    // debugf("block_write: header 0x%08x, data 0x%08x, status 0x%08x\n", header_phys, data_phys, status_phys);

    request.sector = byte_num / SECTOR_SIZE; // really bdev->cfg->blk_size
    request.type = VIRTIO_BLK_T_OUT;
    request.status = 0b111;

    //get index
    at_idx = bdev->vdev->driver_idx;

    //get q size for modding
    q_size = bdev->vdev->common_cfg->queue_size;

    //lock device before we start filling the descriptor
    mutex_spinlock(&bdev->vdev->lock);
    // debugf("block_write: locked!\n");

    //descriptor array
    VirtioDescriptor* descriptors = (VirtioDescriptor*) bdev->vdev->common_cfg->queue_desc;

    //fill header descriptor fields
    VirtioDescriptor* head_descr = &descriptors[at_idx];
    head_descr->addr = header_phys;
    head_descr->len = 16;   // this is hard coded b\c the struct math doesn't work b\c of aligning
    head_descr->flags = VIRTQ_DESC_F_NEXT;
    head_descr->next = (at_idx + 1) % q_size;

    //fill data descriptor fields
    VirtioDescriptor* data_descr = &descriptors[(at_idx + 1) % q_size];
    data_descr->addr = data_phys;
    data_descr->len = ALIGN_UP_POT((byte_num % SECTOR_SIZE) + size, SECTOR_SIZE);
    data_descr->flags = VIRTQ_DESC_F_NEXT;
    data_descr->next = (at_idx + 2) % q_size;

    //fill status descriptor fields
    VirtioDescriptor* status_descriptor = &descriptors[(at_idx + 2) % q_size];
    status_descriptor->addr = status_phys;
    status_descriptor->len = sizeof(uint8_t);
    status_descriptor->flags = VIRTQ_DESC_F_WRITE;
    status_descriptor->next = 0;
    
    //set descriptor in driver ring
    bdev->vdev->driver->ring[bdev->vdev->driver->idx % q_size] = at_idx;
    bdev->vdev->driver->idx += 1;
    bdev->vdev->driver_idx = (bdev->vdev->driver_idx + 1) % q_size;

    //unlock mutex
    mutex_unlock(&bdev->vdev->lock);
    // debugf("block_write: unlocked!\n");

    //notify
    virtio_notify(bdev->vdev, bdev->vdev->common_cfg->queue_select);
    debugf("block_write: notifying register at address 0x%08x\n", bdev->vdev->notify);
    *(bdev->vdev->notify) = 0; // doesn't matter what we write here, I guess--Marz writes 0
    while(bdev->vdev->isr->queue_interrupt != 0){
        // debugf("While loop ... waiting ... \n");
    }
    // debugf("made it!!!!!!\n");

    //grab numbers
    while(bdev->vdev->device_idx != bdev->vdev->device->idx){
        VirtioDescriptor* descr = (VirtioDescriptor*) bdev->vdev->common_cfg->queue_desc + bdev->vdev->device_idx;
        // debugf("at idx %d\n", at_idx);
        // debugf("descr length: 0x%x idx1: %d idx2: %d\n", descr->len,bdev->vdev->device_idx, bdev->vdev->device->idx);
        bdev->vdev->device_idx += 1;
    }

    debugf("block_write: status %u\n", request.status);
    return (request.status == VIRTIO_BLK_S_OK);
};

bool block_read_wrapper(BlockDevice *bdev, uint64_t buf_addr, uint32_t byte_num, uint32_t size) {
    void* temp_buffer = kzalloc(ALIGN_UP_POT((uint64_t)size, SECTOR_SIZE));
    if(!block_read(bdev, (uint64_t)temp_buffer, byte_num, ALIGN_UP_POT((byte_num % SECTOR_SIZE) + size, SECTOR_SIZE)))
    {
        kfree(temp_buffer);
        return false;
    } 

    //write only the specified amount into temp buffer from the buf_addr
    debugf("read wrapper: %x %x size %d\n", (temp_buffer), buf_addr, size);
    for(uint32_t i = 0; i < size; i++){
        ((char*)buf_addr)[i] = ((char*)temp_buffer)[(byte_num % SECTOR_SIZE) + i];
    }

    kfree(temp_buffer);

    return true;
}

bool block_write_wrapper(BlockDevice *bdev, uint64_t buf_addr, uint32_t byte_num, uint32_t size) {
    void* temp_buffer = kzalloc(ALIGN_UP_POT((uint64_t)size, SECTOR_SIZE));
    if(!block_read(bdev, (uint64_t)temp_buffer, byte_num, ALIGN_UP_POT((byte_num % SECTOR_SIZE) + size, SECTOR_SIZE)))
    {
        debugf("Failure to read!\n");
        kfree(temp_buffer);
        return false;
    }
    
    //write only the specified amount into temp buffer from the buf_addr
    for(uint32_t i = 0; i < size; i++){
        ((char*)temp_buffer)[(byte_num % SECTOR_SIZE) + i] = ((char*)buf_addr)[i];
    }
    
    bool status = block_write(bdev, (uint64_t)temp_buffer, byte_num, ALIGN_UP_POT((byte_num % SECTOR_SIZE) + size, SECTOR_SIZE));
    kfree(temp_buffer);
    return status;
}

bool block_flush(BlockDevice *bdev){
    uint64_t header_phys, status_phys;
    uint16_t at_idx, q_size;
    BlockRequest request;

    //check if device enabled
    if((bdev->vdev->common_cfg->device_status & VIRTIO_F_DRIVER_OK) == 0x0){
        return false;
    }

    //translate for device b/c devices deal in physical
    header_phys = mmu_translate_ptr_to_u64(kernel_mmu_table, &request);
    status_phys = mmu_translate_ptr_to_u64(kernel_mmu_table, &(request.status));

    ////debugf("block_read: header 0x%08x, status 0x%08x\n", header_phys, status_phys);

    request.sector = 0; // according to spec, set to 0 for flush
    request.type = VIRTIO_BLK_T_FLUSH;
    request.status = 0b111;
    
    //get index
    at_idx = bdev->vdev->driver_idx;

    //get q size for modding
    q_size = bdev->vdev->common_cfg->queue_size;

    //lock device before we start filling the descriptor
    mutex_spinlock(&bdev->vdev->lock);
    ////debugf("block_read: locked!\n");

    //descriptor array
    VirtioDescriptor* descriptors = (VirtioDescriptor*) bdev->vdev->common_cfg->queue_desc;

    //fill header descriptor fields
    VirtioDescriptor* head_descr = &descriptors[at_idx];
    head_descr->addr = header_phys;
    head_descr->len = 16;   // this is hard coded b\c the struct math doesn't work b\c of aligning
    head_descr->flags = VIRTQ_DESC_F_NEXT;
    head_descr->next = (at_idx + 1) % q_size;

    //fill status descriptor fields
    VirtioDescriptor* status_descriptor = &descriptors[(at_idx + 1) % q_size];
    status_descriptor->addr = status_phys;
    status_descriptor->len = sizeof(uint8_t);
    status_descriptor->flags = VIRTQ_DESC_F_WRITE;
    status_descriptor->next = 0;
    
    //set descriptor in driver ring
    bdev->vdev->driver->ring[bdev->vdev->driver->idx % q_size] = at_idx;
    bdev->vdev->driver->idx += 1;
    bdev->vdev->driver_idx = (bdev->vdev->driver_idx + 1) % q_size;

    //unlock mutex
    mutex_unlock(&bdev->vdev->lock);
    ////debugf("block_read: unlocked!\n");

    //notify
    virtio_notify(bdev->vdev, bdev->vdev->common_cfg->queue_select);
    ////debugf("block_read: notifying register at address 0x%08x\n", bdev->vdev->notify);
    *(bdev->vdev->notify) = 0; // doesn't matter what we read here, I guess--Marz reads 0
    while(bdev->vdev->isr->queue_interrupt != 0){
        ////debugf("While loop ... waiting ... \n");
    }
    ////debugf("made it!!!!!!\n");

    //grab numbers
    while(bdev->vdev->device_idx != bdev->vdev->device->idx){
        VirtioDescriptor* descr = (VirtioDescriptor*) bdev->vdev->common_cfg->queue_desc + bdev->vdev->device_idx;
        ////debugf("at idx %d\n", at_idx);
        ////debugf("descr length: 0x%x idx1: %d idx2: %d\n", descr->len,bdev->vdev->device_idx, bdev->vdev->device->idx);
        bdev->vdev->device_idx += 1;
    }

    debugf("block_flush: status %u\n", request.status);
    return (request.status == VIRTIO_BLK_S_OK);
};

// void  discard_block(vdev, ecam){
// };


// void  write_zero_block(vdev, ecam){
// };