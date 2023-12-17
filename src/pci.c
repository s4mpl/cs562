#include <pci.h>
#include <virtio.h>
#include <debug.h>
#include <vector.h>
#include <util.h>
#include <kmalloc.h>
#include <rng.h>

static void setup_pci_bridge(volatile struct pci_ecam *ecam, uint16_t bus)
{
    /* Clear memory space bit (and I/O) to turn off responding to memory requests while setting up the bridge. */
    ecam->command_reg &= ~0x3;

    // tally for secondary bus num
    static uint8_t sbn                = 1;

    //calculate address start and end for this bridge's responses
    uint64_t address_start            = PCI_MMIO_ADDRESS_START | ((uint64_t)sbn << 20);
    uint64_t address_end              = address_start + ((1 << 20) - 1);

    //setup the bus numbers
    ecam->type1.primary_bus_no        = bus;
    ecam->type1.secondary_bus_no      = sbn;
    ecam->type1.subordinate_bus_no    = sbn;

    //setup memory and command reg
    ecam->type1.memory_base           = address_start >> 16;
    ecam->type1.memory_limit          = address_end >> 16;
    ecam->type1.prefetch_memory_base  = address_start >> 16;
    ecam->type1.prefetch_memory_limit = address_end >> 16;
    ecam->command_reg                 = COMMAND_REG_MMIO;
    
    //debugf("setup_pci_bridge: setting up bridge with sbn %u on bus %u\n", sbn, bus);
    //debugf("-- vendor id 0x%x, device id 0x%x, start address 0x%08x\n", ecam->vendor_id, ecam->device_id, address_start);

    //increment our running tally for secondary bus number
    sbn += 1;
}

static void setup_pci_device(volatile struct pci_ecam *ecam){
    /* Clear memory space bit (and I/O) to turn off responding to memory requests while setting up the BARs. */
    ecam->command_reg &= ~0x3;
    
    /*
     * Start the address range at the corresponding bus/bridge start address and use the device number to separate them.
     * Use only 4 bits of the bus number since we can only have 16 busses. Use all 5 bits of the device number.
    */
    uint64_t current_addr = PCI_MMIO_ADDRESS_START | ((uint64_t)ecam & (0xf << 20)) | ((uint64_t)ecam & (0x1f << 15));
    uint64_t bar_size;

    //debugf("setup_pci_device: current_addr starting at 0x%08x\n", current_addr);

    // go through bars 0-5 (or 1- 6)
    for (uint8_t bar_num = 0; bar_num < 6; bar_num++) {
        volatile uint32_t *bar_addr = ecam->type0.bar + bar_num;

        /* Check if bar is in use so we can determine the address space if so. */
        *bar_addr = -1U;
        if(*bar_addr == 0) {
            continue;
        }
        //debugf("setup_pci_device: written value is 0x%08x\n", *((uint32_t *)(ecam->type0.bar + bar_num)));

        //check if 64 bit bar
        if (((*bar_addr >> 1) & 0b11) == 0b10){
            //debugf("setup_pci_device: BAR%d 64-bit, vendor id 0x%x, device id 0x%x\n", bar_num, ecam->vendor_id, ecam->device_id);

            volatile uint64_t *bar_addr64 = (volatile uint64_t *)bar_addr;
            bar_num += 1;     //add 1 to bar counter b\c we need to use two bars here

            //calculate bar size and iterate current_addr
            bar_size = -(*bar_addr & ~0xF);
            current_addr = ALIGN_UP_POT((uint64_t)current_addr, bar_size); // ALIGN UP!!!!! OR ELSE THE BAR CLEARS IT
            *bar_addr64 = (uint64_t)current_addr;
            current_addr += bar_size;

            //debugf("-- BAR is on: current address was 0x%08x with size %lu\n", current_addr - bar_size, bar_size);
            //debugf("-- value is 0x%016lx\n", *((uint64_t *)(ecam->type0.bar + bar_num - 1)));
        }
        else{
            //debugf("setup_pci_device: BAR%d 32-bit, vendor id 0x%x, device id 0x%x\n", bar_num, ecam->vendor_id, ecam->device_id);
            
            //do all the same, but in 32 bit b/c 32-bit bar
            //NOTE: do not increment bar_num b/c only one bar used in 32-bit
            //calculate bar size and iterate current_addr
            bar_size = -(*bar_addr & ~0xF);
            *bar_addr = (uint32_t)current_addr;
            current_addr += bar_size;

            //debugf("-- BAR is on: current address was 0x%08x with size %lu\n", current_addr - bar_size, bar_size);
            //debugf("-- value is 0x%08x\n", *((uint32_t *)(ecam->type0.bar + bar_num)));
        }
    }

    ecam->command_reg |= COMMAND_REG_MMIO;
}

static volatile struct VirtioPciIsrCap* get_isr_from_device(struct pci_ecam* ecam){
    //iterate over capes to find ISR
    if (0 != (ecam->status_reg & (1 << 4))) {
        unsigned char capes_next = ecam->common.capes_pointer;
        while (capes_next != 0) {
            unsigned long cap_addr = (unsigned long)ecam + capes_next;
            struct pci_cape *cap = (struct pci_cape *)cap_addr;

            // check for vendor-specific
            switch (cap->id) {
                case 0x09:{
                    //check for a type 3 for ISR
                    struct VirtioCapability* virtio_cap = (struct VirtioCapability*) cap;
                    if(virtio_cap->type == 0x03){
                        //get config based on bar num and offset
                        uint64_t *bar_addr = (uint64_t *)(ecam->type0.bar + virtio_cap->bar);
                        return (struct VirtioPciIsrCap *)(bar_addr + virtio_cap->offset);
                    }

                } 
                break;
                default:
                break;
            }
            
            //"increment" our next pointer
            capes_next = cap->next;
        }                
    }

    return NULL;
}

static volatile struct pci_ecam* pci_get_ecam(uint8_t bus, uint8_t device, uint8_t function, uint16_t reg){
    // create 64-bit address that can be returned
    // need to mask to make sure we're only getting the correct bits for each field
    uint64_t bus64 = bus & 0xff;
    uint64_t device64 = device & 0x1f;
    uint64_t function64 = function & 0x7;
    uint64_t reg64 = reg & 0x3ff;

    // mask the bits into one address to get the final ecam
    return (struct pci_ecam*) (PCI_ECAM_ADDRESS_START | 
                                (bus64 << 20) | 
                                (device64 << 15) | 
                                (function64 << 12) | 
                                (reg64 << 2));
}

//function to iterate over capes copied and modified from notes
//testing to see if at least 5 vendor-specific capes per virtio device
static void print_capes(struct pci_ecam* ecam){
    int count_09 = 0;
    // printf("Haiiiiii ;3 <3333\n"); // -Logan
    // Make sure there are capabilities (bit 4 of the status register).
    if (0 != (ecam->status_reg & (1 << 4))) {
        unsigned char capes_next = ecam->common.capes_pointer;
        while (capes_next != 0) {
            unsigned long cap_addr = (unsigned long)ecam + capes_next;
            struct pci_cape *cap = (struct pci_cape *)cap_addr;
            switch (cap->id) {
                case 0x09: /* Vendor Specific */
                {
                    count_09 += 1;
                }
                break;
                case 0x10: /* PCI-express */
                {
                }
                break;
                default:
                    //debugf("print_capes: Unknown capability ID 0x%02x (next: 0x%02x)\n", cap->id, cap->next);
                break;
            }
            capes_next = cap->next;
        }
    }
                    
    //debugf("print_capes: num 0x09 is %d for vend_id %x\n", count_09, ecam->vendor_id);

}

void pci_init(void)
{
    int bus;
    int device;
    int max_bus = 16;
    int max_device = 32;

    /* Initialize the global VirtIO devices list. */
    g_virtio_devices = list_new();

    // Initialize and enumerate all PCI bridges and devices.
    for (bus = 0; bus < max_bus; bus++) {
        for (device = 0; device < max_device; device++) {  // enumerate through bus and device
            if(bus == 0 && device == 0) continue;
            
            //calc ecam addr
            struct pci_ecam *ecam = (struct pci_ecam *)pci_get_ecam(bus, device, 0, 0);
            // //debugf("pci_init: ecam addr %x\n", ecam);

            // get bus # and device number, 27-20 and 15-19
            if (ecam->vendor_id == 0xFFFF){
                // //debugf("pci_init: not connected\n");
                continue;
            }

            // //debugf("pci_init: connected\n");
            // device is present
            if (ecam->header_type == 0x00) {
                setup_pci_device(ecam);
            }
            else if (ecam->header_type == 0x01) {
                setup_pci_bridge(ecam, bus);
            }

            if(ecam->vendor_id == VIRTIO_PCI_VENDOR_ID) {
                /* Make a new PciDevice struct for the VirtioDevice and add it to the global list */
                PciDevice *new_pci_dev = (PciDevice *)kmalloc(sizeof(PciDevice));
                new_pci_dev->ecam = ecam;
                new_pci_dev->irq = 32 + (bus + device) % 4;

                VirtioDevice *new_virtio_dev = (VirtioDevice *)kzalloc(sizeof(VirtioDevice));
                new_virtio_dev->pcidev = new_pci_dev;
                // new_virtio_dev->isr = get_isr_from_device(ecam); // this should instead be done upon virtio_init()

                list_add_ptr(g_virtio_devices, new_virtio_dev);
            }
        }
    }

    
    // //debugf("pci_init: vectorsize %d\n", vector_capacity(device_vector));

    //test device struct
    // struct PciDevice* curr_device;
    // uint64_t curr_val;
    // for(int i = 0; i < vector_size(device_vector); i++){
    //     if(!vector_get(device_vector, i, &curr_val)){
    //         //debugf("pci_init: couldn't grab indx %d\n", bus * 32 + device);
    //         continue;
    //     }
    //     //debugf("pci_init: curr device addr: %x\n at bus %d device %d\n", curr_val, bus, device);
    //     curr_device = (struct PciDevice*)curr_val;
    //     //debugf("pci_init: testing vendor id %x header %x\n", curr_device->ecam->vendor_id, curr_device->ecam->header_type);
    // }

    //test our dispatch function
    //pci_dispatch_irq(34);
}

// This should forward all virtio devices to the virtio drivers.
void pci_dispatch_irq(int irq)
{
    // An IRQ came from the PLIC, but recall PCI devices
    // share IRQs. So, you need to check the ISR register
    // of potential virtio devices.
    ListElem *curr_elem;
    VirtioDevice *curr_dev;
    list_for_each(g_virtio_devices, curr_elem) {
        curr_dev = (VirtioDevice *)list_elem_value_ptr(curr_elem);
        if(curr_dev->pcidev->irq == irq) {
            //check interrupt
            if(curr_dev->isr == NULL){
                continue;
            }
            if(curr_dev->isr->queue_interrupt == 0x1){
                if(curr_dev->pcidev->ecam->device_id == 0x1044){
                    rng_finish();
                }
                //debugf("pci_dispatch_irq: INTERRUPT FOUND\n");
            }
            else{
                //debugf("pci_dispatch_irq: NO INTERRUPT\n");
            }
        }
    } 
}
