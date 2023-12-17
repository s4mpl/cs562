#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <virtio.h>
#include <debug.h>
#include <util.h>
#include <sbi.h>
#include <kmalloc.h>
#include <mmu.h>
#include <input.h>
#include <input-event-codes.h>
#include <list.h>
#include <gpu.h>

static char keys[] = {
    [KEY_A] = 'a',
    [KEY_B] = 'b',
    [KEY_C] = 'c',
    [KEY_D] = 'd',
    [KEY_E] = 'e',
    [KEY_F] = 'f',
    [KEY_G] = 'g',
    [KEY_H] = 'h',
    [KEY_I] = 'i',
    [KEY_J] = 'j',
    [KEY_K] = 'k',
    [KEY_L] = 'l',
    [KEY_M] = 'm',
    [KEY_N] = 'n',
    [KEY_O] = 'o',
    [KEY_P] = 'p',
    [KEY_Q] = 'q',
    [KEY_R] = 'r',
    [KEY_S] = 's',
    [KEY_T] = 't',
    [KEY_U] = 'u',
    [KEY_V] = 'v',
    [KEY_W] = 'w',
    [KEY_X] = 'x',
    [KEY_Y] = 'y',
    [KEY_Z] = 'z',
    [KEY_0] = '0',
    [KEY_1] = '1',
    [KEY_2] = '2',
    [KEY_3] = '3',
    [KEY_4] = '4',
    [KEY_5] = '5',
    [KEY_6] = '6',
    [KEY_7] = '7',
    [KEY_8] = '8',
    [KEY_9] = '9',
    [KEY_ENTER] = '\n',
    [KEY_BACKSPACE] = '\b',
    [KEY_SPACE] = ' '
};

InputDevice* g_keydev = NULL;
InputDevice* g_tabdev = NULL;

// Takes 
static void input_ring_buffer_push(InputDevice* idev, uint64_t event){
    for (int i = 0; i < 8; ++i) ring_push(idev->ring_buffer, (char)((event >> (8 * (7 - i))) & 0xFF));
}

uint64_t input_ring_buffer_pop(InputDevice* idev){


    // printf("popopop\n");
    if(ring_size(idev->ring_buffer) == 0){
        return 0;
    }

    // printf("Bruh\n");
// //debugf("g_keydev->ring_buffer: 0x%x\n", g_keydev->ring_buffer);
    volatile uint64_t value = 0;
    // printf("Bruh vdfgdfsgd\n");
    for (int i = 0; i < 8; ++i) {
        // //debugf("i: %i\n", i);
        uint64_t popped_val =  ring_pop(idev->ring_buffer);
        // //debugf("i: %i\n", i);
        value |= ((popped_val) << (8 * (7 - i)));
        // //debugf("i: %i\n", i);
    }
    // printf("Bruh past\n");
    return value;
}

static inline uint64_t pack_event(uint64_t evaddr)
{

    ////debugf("pack_event(): %x\n", *((const uint64_t *)evaddr));
    return *((const uint64_t *)evaddr);
}
 
void input_init(VirtioDevice* vio_dev){

    InputDevice* new_dev = (InputDevice*)kmalloc(sizeof(InputDevice));
    new_dev->ring_buffer = ring_new_with_policy(INPUT_RING_BUFFER_CAPACITY, INPUT_RING_BUFFER_BEHAVIOR);
    new_dev->viodev = vio_dev;
    new_dev->config = (InputConfig*)(vio_dev->pcidev->dev_config);

    uint16_t q_size = new_dev->viodev->common_cfg->queue_size;
    //debugf("queue_select: 0x%x\n", new_dev->viodev->common_cfg->queue_select);
    //debugf("q_size: %d\n", vio_dev->common_cfg->queue_size);

    uint16_t i = 0;
    uint16_t at_idx;
    InputEvent *event_buffers = (InputEvent *)kmalloc(sizeof(InputEvent) * q_size);

    // new_dev->env_buffs = event_buffers;

    // //debugf("Event_buffers: 0x%x\n", event_buffers);
    volatile uint64_t event_physical = mmu_translate_ptr_to_u64(kernel_mmu_table, event_buffers);
    // //debugf("event_physical: 0x%x\n", event_physical);
    
    mutex_spinlock(&vio_dev->lock);
    for(i = 0; i < q_size; i++){

        at_idx = vio_dev->driver_idx;
        new_dev->viodev->desc[i].addr = event_physical + (sizeof(InputEvent) * i);//Grab physical memory address of each buffer
        new_dev->viodev->desc[i].len = sizeof(InputEvent);//Len of event buffer
        new_dev->viodev->desc[i].flags = VIRTQ_DESC_F_WRITE; //Device needs to be able to write to this!
        new_dev->viodev->desc[i].next = 0; //There is no chaining in here!
        
        // Set descriptor in driver ring.
        vio_dev->driver->ring[vio_dev->driver->idx % q_size] = at_idx;
        //Tells device there are new buffers available to use
        new_dev->viodev->driver->idx++;

        vio_dev->driver_idx = (vio_dev->driver_idx + 1) % q_size; 

        ////debugf("addr[%d]: 0x%x\n", i, new_dev->viodev->desc[i].addr);
        // //debugf("len[%d]: 0x%x\n", i, new_dev->viodev->desc[i].len);
        // //debugf("flags[%d]: 0x%x\n", i, new_dev->viodev->desc[i].flags);
        // //debugf("next[%d]: 0x%x\n", i, new_dev->viodev->desc[i].next);
        // //debugf("driver->idx: 0x%d\n", i, new_dev->viodev->driver->idx);
    }

    mutex_unlock(&vio_dev->lock);

    *(new_dev->viodev->notify) = 0;

    debugf("%.128s", new_dev->config->string);
    new_dev->config->select = VIRTIO_INPUT_CFG_ID_DEVIDS;
    new_dev->config->subsel = 0;
    if (new_dev->config->ids.product == EV_KEY) {
        debugf("Found keyboard input device.\n");
        new_dev->device_type = 1;
        g_keydev = new_dev;
    }
    else if (new_dev->config->ids.product == EV_ABS) {
        debugf("Found tablet input device.\n");
        new_dev->device_type = 2;
        g_tabdev = new_dev;
    }
    else {
        debugf("Found an input device product id %d\n", new_dev->config->ids.product);
    }

    // list_add_ptr(g_input_devices, new_dev);

return;
}

uint32_t input_handle(InputDevice *idev) {

    // //debugf("input_handle()\n");

    VirtioDevice *dev = idev->viodev;

    uint32_t handled = 0;
    uint16_t qsize = dev->common_cfg->queue_size;
    // //debugf("Device Type: %x\n", idev->device_type);
    // printf("Flags: %x\n", dev->device->flags);

    // for(uint16_t i = 0; i < 100; i++){

    mutex_spinlock(&dev->lock);
    // dev->common_cfg->queue_device + 1;
    // //debugf("BEFORE LOOP: \n");

    while (dev->device_idx != dev->device->idx) {

        // //debugf("GRABBING SOMETHING: \n");

        // //debugf("dev->device_idx: %d\n", dev->device_idx);
        // //debugf("dev->device->idx: %d\n", dev->device->idx);

        uint32_t idx = dev->device_idx % qsize;
        // //debugf("idx: %d\n", idx);
        uint32_t id = dev->device->ring[idx].id;
        // //debugf("id: %d\n", id);
        uint32_t len = dev->device->ring[idx].len;

        uint64_t before_push;

        handled += 1;
        dev->device_idx += 1;
        //printf("[INPUT]: %d/%d (%d:%d)\n", handled, dev->device->idx, id, len);
        if (len != sizeof(InputEvent)) {
            printf("   [EVENT]: Invalid event\n");
            return -1;
        }
        else {
            
            InputEvent *ev = dev->desc[id].addr;
            //Get the correct descriptor, and then access the addrss field
            
            // Ignore sync events.
                // //debugf("ADD\n");
            before_push = pack_event(dev->desc[id].addr);

                 
            if(ev->type == EV_ABS){
                // debugf("%016lX ---- handled %i\n", before_push, handled);
            }

            if (ev->type != EV_SYN) {
                input_ring_buffer_push(idev, before_push);
            }
            else{
                handled-=1;  
            }
            dev->driver->idx += 1; // Allow the buffer to be reused.
        }
    }
    mutex_unlock(&dev->lock);
    // //debugf("f\n");
    return handled;
}

void input_test(){
    
    uint64_t handled = 0;
    handled =  input_handle(g_keydev);

    //debugf("g_keydev->rb.size(): %d: \n", ring_size(g_keydev->ring_buffer));
    //debugf("g_keydev->rb.size(): %d: \n", ring_size(g_keydev->ring_buffer) / 8);
    //debugf("handled: %d\n", handled);
    
    handled =  input_handle(g_tabdev);
    debugf("g_tabdev->rb.size(): %d: \n", ring_size(g_tabdev->ring_buffer));
    debugf("g_tabdev->rb.size(): %d: \n", ring_size(g_tabdev->ring_buffer) / 8);
    debugf("handled: %d\n", handled);
}

signed char get_keyboard() {
    input_handle(g_keydev);

    if (ring_size(g_keydev->ring_buffer) > 0) {
        uint64_t inputValue =  input_ring_buffer_pop(g_keydev);

        if (inputValue == 0) return 0;

        char returnValue;

        uint16_t code = (uint16_t)((inputValue >> 16) & 0xFFFF);
        uint32_t value = (uint32_t)((inputValue >> 32) & 0xFFFFFFFF);

        returnValue = keys[code];
        if (value < 1) returnValue *= -1; // if it is a release event, make it negative. 

        return returnValue; 
    }
    else {
        return 0;
    }

}

coord_pair get_cursor() {
    InputEvent curr_event;
    coord_pair coord;
    coord.x = -1;
    coord.y = -1;

    input_handle(g_tabdev);

    if(ring_size(g_tabdev->ring_buffer) >= 2) {
        while(ring_size(g_tabdev->ring_buffer) > 0 && (coord.x == -1 || coord.y == -1)) {
            uint64_t raw_event = input_ring_buffer_pop(g_tabdev);

            curr_event.type = (uint16_t)(raw_event & 0xFFFF);  // Extract the lower 16 bits of after_pop as the type
            curr_event.code = (uint16_t)((raw_event >> 16) & 0xFFFF); // Extract the next 16 bits as the code
            curr_event.value = (uint32_t)((raw_event >> 32) & 0xFFFFFFFF); // Extract the upper 32 bits as the value


            if(curr_event.code == ABS_X) {
                coord.x = (curr_event.value * g_gdev->width) / 32767;
            } else if(curr_event.code == ABS_Y) {
                coord.y = (curr_event.value * g_gdev->height) / 32767;
            }
        }
    }

    // debugf("cursor at (%d, %d)\n", coord.x, coord.y);

    return coord;
}