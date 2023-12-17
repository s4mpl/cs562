#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <virtio.h>

/*job struct for async rng requests
*/
typedef struct rng_job {
    uint32_t id;
    void *buffer;
    uint16_t size;
    bool complete;
    uint64_t uuid;
} rng_job;

/*rng init function
    shouldn't be anything here but maybe we'll discover something we need to init
*/
void rng_init(struct VirtioDevice *viodev);

/*rng request function
    fills buffer with rng 
*/
bool rng_fill(void *buffer, uint16_t size);

/*rng handle device interrupt based on queue_interrupt
*/
void rng_finish();