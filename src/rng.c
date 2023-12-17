#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <rng.h>
#include <mmu.h>
#include <virtio.h>
#include <debug.h>
#include <util.h>
#include <sbi.h>
#include <kmalloc.h>

struct VirtioDevice *rng_device;

/*rng init function
    shouldn't be anything here but maybe we'll discover something we need to init
*/
void rng_init(struct VirtioDevice *viodev){
    //debugf("rng_init: successfully initialized\n");
    rng_device = viodev;
}

void rng_finish(){
   //debugf("rng_finish: made it!\n"); 
}

/*rng request function
    fills buffer with rng 
*/
bool rng_fill(void *buffer, uint16_t size){
    uint64_t buffer_phys;
    uint16_t at_idx;
    uint16_t q_size;

    //check if device enabled
    if((rng_device->common_cfg->device_status & VIRTIO_F_DRIVER_OK) == 0x0){
        return false;
    }

    //translate for device b/c devices deal in physical
    buffer_phys = mmu_translate(kernel_mmu_table, (uint64_t)buffer);

    //get index
    at_idx = rng_device->driver_idx;

    //get q size for modding
    q_size = rng_device->common_cfg->queue_size;

    //lock device before we start filling the descriptor
    mutex_spinlock(&rng_device->lock);
    // debugf("rng_fill: locked!\n");

    //fill descriptor fields
    VirtioDescriptor* curr_descr = (VirtioDescriptor*) rng_device->common_cfg->queue_desc + at_idx;
    curr_descr->addr = buffer_phys;
    curr_descr->len = size;
    curr_descr->flags = VIRTQ_DESC_F_WRITE;
    curr_descr->next = 0;
    
    //set descriptor in driver ring
    rng_device->driver->ring[rng_device->driver->idx % q_size] = at_idx;
    rng_device->driver->idx += 1;
    rng_device->driver_idx = (rng_device->driver_idx + 1) % q_size;

    //make job here to rep this rng job
    /* Commenting this out for now so we won't run out of memory.
    struct rng_job* job = (struct rng_job*) kzalloc(sizeof(struct rng_job*));
    job->id = at_idx;
    job->buffer = buffer;
    job->size = size;
    job->complete = false;
    job->uuid = sbi_rtc_get_time();
    map_set_int(rng_device->jobs, job->id, (MapValue)job);
    */

    //unlock mutex
    mutex_unlock(&rng_device->lock);
    // debugf("rng_fill: unlocked!\n");

    //notify
    // virtio_notify(rng_device, rng_device->common_cfg->queue_select);
    // debugf("rng_fill: notifying register at address 0x%08x\n", rng_device->notify);
    *(rng_device->notify) = 1; // doesn't matter what we write here, I guess--Marz writes 0
    while(rng_device->isr->queue_interrupt != 0){
        // debugf("waiting ... \n");
    }
    // debugf("made it!!!!!!\n");

    //grab numbers
    while(rng_device->device_idx != rng_device->device->idx){
        // uint32_t at_idx = rng_device->device->ring[rng_device->device->idx].id;
        VirtioDescriptor* descr = (VirtioDescriptor*) rng_device->common_cfg->queue_desc + rng_device->device_idx;
        // debugf("at idx %d\n", at_idx);
        // debugf("descr length: 0x%x idx1: %d idx2: %d\n", descr->len, rng_device->device_idx, rng_device->device->idx);
        rng_device->device_idx += 1;
    }

    return true;
}