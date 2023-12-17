#include <virtio.h>
#include <pci.h>
#include <debug.h>
#include <util.h>
#include <mmu.h>
#include <rng.h>
#include <csr.h>
#include <map.h>
#include <gpu.h>
#include <block.h>
#include <input.h>
#include <kmalloc.h>

List *g_virtio_devices;

/*
    Debug function that prints all the capabilites given an ecam
*/
static void virtio_print_debug(struct pci_ecam *ecam){
    
    unsigned char capes_next = ecam->common.capes_pointer;
    //Go through cap list --> Linked list
    while(capes_next != 0){
        unsigned long cap_addr = (unsigned long)ecam+capes_next;
        struct pci_cape* cap = (struct pci_cape*)cap_addr;
        VirtioCapability* v_cap = VIRTIO_CAP(cap_addr);
        if(cap->id == VIRTIO_CAP_VENDOR && v_cap->type <= 3){    
            //debugf("vndr: 0x%x\n", v_cap->id);
            //debugf("next: 0x%x\n", v_cap->next);
            //debugf("len: 0x%x\n", v_cap->len);
            //debugf("type: 0x%x\n", v_cap->type);
            //debugf("bar: 0x%x\n", v_cap->bar);
            //debugf("offset: 0x%x\n", v_cap->offset);
            //debugf("length: 0x%x\n", v_cap->length);

            uint64_t MMIO_addr = ecam->type0.bar[v_cap->bar] & ~0xF;
            uint32_t offset =  v_cap->offset;
            //debugf("0x%x\n", MMIO_addr + offset);
        }
        capes_next = cap->next;
    }
}

/*
    Functions that will deal with the capabilities, I don't know if there are any params
    we should be passing in besides the cap
*/
static void virtio_init_common(VirtioDevice* viodev, VirtioCapability* v_cap, struct pci_ecam* ecam){
    uint8_t bar_num = v_cap->bar;
    uint32_t *bar_addr = ecam->type0.bar + bar_num;
    uint64_t MMIO_addr = ((((*bar_addr >> 1) & 0b11) == 0b10) ? (*(uint64_t *)bar_addr) & ~0xf : (*(uint32_t *)bar_addr)) & ~0xf;
    uint32_t offset =  v_cap->offset;

    VirtioPciCommonCfg* comm_config = (VirtioPciCommonCfg*)(MMIO_addr + offset);
    viodev->common_cfg = comm_config;
    
    //Set the first three status flags
    // //debugf("virtio_init_common: Device status init: 0x%x\n", comm_config->device_status);
    comm_config->device_status = VIRTIO_F_RESET;
    // //debugf("virtio_init_common: Device status (reset): 0x%x\n", comm_config->device_status);
    comm_config->device_status = VIRTIO_F_ACKNOWLEDGE;
    // //debugf("virtio_init_common: Device status (ack): 0x%x\n", comm_config->device_status);
    comm_config->device_status |= VIRTIO_F_DRIVER;
    // //debugf("virtio_init_common: Device status (driver): 0x%x\n", comm_config->device_status);
    // Feature select is an index into some array that selects a feature
    // Feature is a bit set, where each bit represents a flag.
    // //debugf("virtio_init_common: Device features (init): 0x%x\n", comm_config->device_feature);
    // //debugf("virtio_init_common: Device features select (init): 0x%x\n", comm_config->device_feature_select);
    // //debugf("virtio_init_common: Driver features (init): 0x%x\n", comm_config->driver_feature);
    // //debugf("virtio_init_common: Driver features select (init): 0x%x\n", comm_config->driver_feature_select);

    //I am not exactly sure how this part works tbh. 
    //I just set the driver feature stuff to whatever the device has.
    //I feel this is way wrong, but until I figure it out, I'm going to keep this here. 
    comm_config->driver_feature_select = comm_config->device_feature_select;
    comm_config->driver_feature = comm_config->device_feature;

    // if (ecam->device_id == 0x1050){
    //     //entropy device has no feature bits defined
    //     comm_config->driver_feature = 0;
    //     //write bits that driver knows. check device to see if we still good
    //     comm_config->device_feature |= 0xf000000;//comm_config->driver_feature;
        
    // }

    //Not sure if we need a check in here to see if it stuck or not...?

    // //debugf("virtio_init_common: Device features (set): 0x%x\n", comm_config->device_feature);
    // //debugf("virtio_init_common: Device features select (set): 0x%x\n", comm_config->device_feature_select);
    // //debugf("virtio_init_common: Driver features (set): 0x%x\n", comm_config->driver_feature);
    // //debugf("virtio_init_common: Driver features select (set): 0x%x\n", comm_config->driver_feature_select);
    
    comm_config->device_status |= VIRTIO_F_FEATURES_OK;

    // //debugf("virtio_init_common: Device status (feature): 0x%x\n", comm_config->device_status);

    //set desc. tables, and both rings for each queue
    uint16_t queue_num = 0;
    uint64_t desc_table;
    uint64_t driver_ring;
    uint64_t device_ring;

    //select first queue, ignore the rest for now. 
    //INPUT and GPU have two queues, but we won't be using those (maybe later?)
    comm_config->queue_select = 0;
    
    //get current queue size
    uint16_t qsize = comm_config->queue_size;
    //debugf("virtio_init_common: Q (0x%x) Q_size: 0x%x Q_notify_offset: 0x%x\n", queue_num, qsize, comm_config->queue_notify_off);

    //zalloc tables and rings for each virtq
    desc_table =  (uint64_t)kzalloc(VIRTIO_DESCRIPTOR_TABLE_BYTES(qsize));
    driver_ring = (uint64_t)kzalloc(VIRTIO_DRIVER_TABLE_BYTES(qsize));
    device_ring = (uint64_t)kzalloc(VIRTIO_DEVICE_TABLE_BYTES(qsize));
    // //debugf("virtio_init_common: T\n");

    //Set pointer to table and rings. We speak virtual memory since we have the MMU
    viodev->desc = (struct VirtioDescriptor *)desc_table;
    viodev->driver = (struct VirtioDriverRing *)driver_ring;
    viodev->device = (struct VirtioDeviceRing *)device_ring;

    //Need to set physical address in the common_cfg since the device speaks physical memory
    viodev->common_cfg->queue_desc = (uint64_t)mmu_translate_ptr(kernel_mmu_table, desc_table);
    viodev->common_cfg->queue_driver = (uint64_t)mmu_translate_ptr(kernel_mmu_table, driver_ring);
    viodev->common_cfg->queue_device = (uint64_t)mmu_translate_ptr(kernel_mmu_table, device_ring);
    
    // //debugf("virtio_init_common: queue_desc 0x%x\n", viodev->common_cfg->queue_desc);
    // //debugf("virtio_init_common: queue_driver 0x%x\n", viodev->common_cfg->queue_driver);
    // //debugf("virtio_init_common: queue_device 0x%x\n", viodev->common_cfg->queue_device);


    //Once we alloc'd, and set tables and rings. Set queue enable for the queue
    // //debugf("virtio_init_common: queue enable (init): 0x%x\n", viodev->common_cfg->queue_enable);
    viodev->common_cfg->queue_enable = VIRTIO_QUEUE_ENABLE;
    // //debugf("virtio_init_common: queue enable (set): 0x%x\n", viodev->common_cfg->queue_enable);
}

//set notify in the VirtioDevice
//set nofifymult in VirtioDevice
//get VirtioPciNotifyCap and set cap to VirtioCapability  
//get VirtioPciNotifyCap from ecam
static void virtio_init_notify(VirtioDevice* viodev, VirtioCapability* v_cap, struct pci_ecam* ecam){
    uint8_t bar_num = v_cap->bar;
    
    uint32_t *bar_addr = ecam->type0.bar + bar_num;
    uint64_t MMIO_addr = ((((*bar_addr >> 1) & 0b11) == 0b10) ? (*(uint64_t *)bar_addr) & ~0xf : (*(uint32_t *)bar_addr)) & ~0xf;
    uint32_t offset =  v_cap->offset;

    //save notify pointer, notify mult, and cap
    viodev->notify = (char*)(MMIO_addr + offset);
    viodev->notifymult = ((VirtioPciNotifyCap*) v_cap)->notify_off_multiplier;
    viodev->notify_cap = (VirtioPciNotifyCap*) v_cap;
    // //debugf("virtio_init_notify: 0x%x 0x%x\n", viodev->notify, viodev->notifymult);
}

void virtio_notify(VirtioDevice *viodev, uint16_t which_queue){
    // get BAR address
    uint8_t bar_num = viodev->notify_cap->cap.bar;
    uint32_t *bar_addr = viodev->pcidev->ecam->type0.bar + bar_num;
    uint64_t MMIO_addr = ((((*bar_addr >> 1) & 0b11) == 0b10) ? (*(uint64_t *)bar_addr) : (*(uint32_t *)bar_addr)) & ~0xf;
    uint64_t queue_addr = MMIO_addr + (uint64_t)BAR_NOTIFY_CAP(viodev->notify_cap->cap.offset,
                                                               viodev->common_cfg->queue_notify_off,
                                                               viodev->notify_cap->notify_off_multiplier);

    //write queue number to this addr to notify
    // //debugf("virtio_notify: %x %x %x\n", queue_addr, MMIO_addr, bar_num);
    // //debugf("virtio_notify: notify values %x %x %x\n", viodev->notify_cap->cap.offset, viodev->common_cfg->queue_notify_off, viodev->notify_cap->notify_off_multiplier);
    *(uint16_t *)queue_addr = which_queue;
    debugf("notify!\n");
}

static void virtio_init_isr(VirtioDevice* viodev, VirtioCapability* v_cap, struct pci_ecam* ecam){
    uint8_t bar_num = v_cap->bar;
    uint32_t *bar_addr = ecam->type0.bar + bar_num;
    uint64_t MMIO_addr = ((((*bar_addr >> 1) & 0b11) == 0b10) ? (*(uint64_t *)bar_addr) : (*(uint32_t *)bar_addr)) & ~0xf;
    uint32_t offset =  v_cap->offset;

    viodev->isr = (VirtioPciIsrCap*) (MMIO_addr + offset);
}

static void virtio_set_dev_config(VirtioDevice* viodev, VirtioCapability* v_cap, struct pci_ecam* ecam){

    uint8_t bar_num = v_cap->bar;
    uint32_t *bar_addr = ecam->type0.bar + bar_num;
    uint64_t MMIO_addr = ((((*bar_addr >> 1) & 0b11) == 0b10) ? (*(uint64_t *)bar_addr) : (*(uint32_t *)bar_addr)) & ~0xf;
    uint32_t offset =  v_cap->offset;

    viodev->pcidev->dev_config = (uint64_t)(MMIO_addr + offset);

}

// static void virtio_isr(VirtioDevice* v_dev){
   
//     if(v_dev->isr->queue_interrupt){ //check if queue interrupt
//         //debugf("virtio_isr: Queue interrupt\n");
//         //probably need to add handling
//         while(v_dev->device_idx != v_dev->device->idx){
//             VirtioDeviceRingElem* ring_elem = &(v_dev->device->ring[v_dev->device->idx]);
//             VirtioDescriptor* descr = &(v_dev->desc[ring_elem->id]);
//             //debugf("virtio_isr: %x %d %x %x\n", descr->addr, descr->len, descr->flags, descr->next);
//         }
//     }

//     if(v_dev->isr->device_cfg_interrupt== 1){
//         //debugf("Device configuratin change interrupt\n");
//         //add handling

//     }
// }

/*
    Functions that will deal with the setup of the device, calls above functions
*/

static void virtio_device_init(VirtioDevice* viodev, struct pci_ecam* ecam){
    
    unsigned char capes_next = ecam->common.capes_pointer;
    struct VirtioCapability* base_cap = NULL;
    
    while(capes_next != 0){
        unsigned long cap_addr = (unsigned long)ecam + capes_next;
        base_cap = (struct VirtioCapability*)cap_addr;
        if(base_cap->id == VIRTIO_CAP_VENDOR && base_cap->type <= 4){
            switch(base_cap->type){
                case VIRTIO_PCI_CAP_COMMON_CFG:
                    virtio_init_common(viodev, base_cap, ecam);
                    break;
                case VIRTIO_PCI_CAP_NOTIFY_CFG:
                    virtio_init_notify(viodev, base_cap, ecam);
                    break;
                case VIRTIO_PCI_CAP_ISR_CFG:
                    virtio_init_isr(viodev, base_cap, ecam);
                    break;
                case VIRTIO_PCI_CAP_DEVICE_CFG:
                    virtio_set_dev_config(viodev, base_cap, ecam);
                    break;
            }
        }
        capes_next = base_cap->next;
    }

    //We should have a check in here to see if all the registers were enumerated...

    //make a map for our jobs
    viodev->jobs = map_new_with_slots(100);

    //This is where we should set the device to be live after we set up all the cap

    viodev->common_cfg->device_status |= VIRTIO_F_DRIVER_OK;
    viodev->ready = true;
    //debugf("VIO_DEVICE_READY: 0x%x\n", viodev->common_cfg->device_status);
}

void virtio_init() {
    //debugf("virtio_init: %x, size: %i\n", g_virtio_devices, list_size(g_virtio_devices));

    ListElem *curr_elem;
    VirtioDevice *curr_dev;
    list_for_each(g_virtio_devices, curr_elem) {
        curr_dev = (VirtioDevice *)list_elem_value_ptr(curr_elem);
        virtio_device_init(curr_dev, curr_dev->pcidev->ecam);
        //check for device ids
        // See "Device Types": https://docs.oasis-open.org/virtio/virtio/v1.1/virtio-v1.1.pdf
        if(curr_dev->pcidev->ecam->device_id == VIRTIO_PCI_DEVICE_ID(VIRTIO_PCI_DEVICE_BLOCK)) {
            g_bdev = block_init(curr_dev);
        } else if(curr_dev->pcidev->ecam->device_id == VIRTIO_PCI_DEVICE_ID(VIRTIO_PCI_DEVICE_ENTROPY)) {
            rng_init(curr_dev);
        } else if(curr_dev->pcidev->ecam->device_id == VIRTIO_PCI_DEVICE_ID(VIRTIO_PCI_DEVICE_GPU)) {
            GpuDevice *gdev = gpu_init(curr_dev);
            gpu_setup(gdev);
            g_gdev = gdev;
        } else if(curr_dev->pcidev->ecam->device_id == VIRTIO_PCI_DEVICE_ID(VIRTIO_PCI_DEVICE_INPUT)) {
            input_init(curr_dev);
        }
    }

    // input_test();
}