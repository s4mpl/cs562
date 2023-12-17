#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <gpu.h>
#include <virtio.h>
#include <debug.h>
#include <util.h>
#include <sbi.h>
#include <alloc.h>
#include <mmu.h>
#include <pci.h>

List *g_gpu_devices = NULL;
GpuDevice *g_gdev = NULL;

static void gpu_send(GpuDevice *gdev, void *request, size_t req_size,
                                      void *response, size_t resp_size) {
  // Check if the device is enabled.
  if((gdev->viodev->common_cfg->device_status & VIRTIO_F_DRIVER_OK) == 0x0) {
    return;
  }

  // debugf("gpu_send: req size %d, resp size %d\n", req_size, resp_size);

  uint64_t req_phys, resp_phys;
  uint16_t at_idx, q_size;
  VirtioDevice *dev = gdev->viodev;
  VirtioDescriptor *desc;

  // Translate allocated pointers since devices work with physical addresses.
  req_phys = mmu_translate_ptr_to_u64(kernel_mmu_table, request);
  resp_phys = mmu_translate_ptr_to_u64(kernel_mmu_table, response);

  at_idx = dev->driver_idx;
  q_size = dev->common_cfg->queue_size;

  mutex_spinlock(&dev->lock);
  // debugf("gpu_send: locked!\n");

  // Fill descriptor fields -- request is read, response is write.
  desc = (VirtioDescriptor *)dev->common_cfg->queue_desc + at_idx;
  desc->addr = req_phys;
  desc->len = req_size;
  desc->flags = VIRTQ_DESC_F_NEXT; // don't set VIRTQ_DESC_F_WRITE
  desc->next = (at_idx + 1) % q_size;

  // Repeat for response.
  desc = (VirtioDescriptor *)dev->common_cfg->queue_desc + (at_idx + 1) % q_size;
  desc->addr = resp_phys;
  desc->len = resp_size;
  desc->flags = VIRTQ_DESC_F_WRITE;
  desc->next = 0;
  
  // Set descriptor in driver ring.
  dev->driver->ring[dev->driver->idx % q_size] = at_idx;
  // ONLY INCREMENT BY 1 PER JOB AND NOT PER DESCRIPTOR!!!
  dev->driver->idx += 1;
  dev->driver_idx = (dev->driver_idx + 1) % q_size;

  mutex_unlock(&dev->lock);
  // debugf("gpu_send: unlocked!\n");

  // debugf("at idx %d\n", at_idx);
  // debugf("driver: idx1 %d, idx2 %d\n", dev->driver_idx, dev->driver->idx);
  // debugf("device: idx1 %d, idx2 %d\n", dev->device_idx, dev->device->idx);

  // Notify the device.
  *(dev->notify) = 1;
}

static void gpu_send_3(GpuDevice *gdev, void *request,size_t req_size,
                                        void *data, size_t data_size,
                                        void *response, size_t resp_size) {
  // Check if the device is enabled.
  if((gdev->viodev->common_cfg->device_status & VIRTIO_F_DRIVER_OK) == 0x0) {
    return;
  }

  // debugf("gpu_send_3: req size %d, data size %d, resp size %d\n", req_size, data_size, resp_size);

  uint64_t req_phys, data_phys, resp_phys;
  uint16_t at_idx, q_size;
  VirtioDevice *dev = gdev->viodev;
  VirtioDescriptor *desc;

  // Translate allocated pointers since devices work with physical addresses.
  req_phys = mmu_translate_ptr_to_u64(kernel_mmu_table, request);
  data_phys = mmu_translate_ptr_to_u64(kernel_mmu_table, data);
  resp_phys = mmu_translate_ptr_to_u64(kernel_mmu_table, response);

  at_idx = dev->driver_idx;
  q_size = dev->common_cfg->queue_size;

  mutex_spinlock(&dev->lock);
  // debugf("gpu_send_3: locked!\n");

  // Fill descriptor fields -- request is read, data is read, response is write.
  desc = (VirtioDescriptor *)dev->common_cfg->queue_desc + at_idx;
  desc->addr = req_phys;
  desc->len = req_size;
  desc->flags = VIRTQ_DESC_F_NEXT; // don't set VIRTQ_DESC_F_WRITE
  desc->next = (at_idx + 1) % q_size;

  // Repeat for data.
  desc = (VirtioDescriptor *)dev->common_cfg->queue_desc + (at_idx + 1) % q_size;
  desc->addr = data_phys;
  desc->len = data_size;
  desc->flags = VIRTQ_DESC_F_NEXT; // don't set VIRTQ_DESC_F_WRITE
  desc->next = (at_idx + 2) % q_size;

  // Repeat for response.
  desc = (VirtioDescriptor *)dev->common_cfg->queue_desc + (at_idx + 2) % q_size;
  desc->addr = resp_phys;
  desc->len = resp_size;
  desc->flags = VIRTQ_DESC_F_WRITE;
  desc->next = 0;

  // Set descriptor in driver ring.
  dev->driver->ring[dev->driver->idx % q_size] = at_idx;
  // ONLY INCREMENT BY 1 PER JOB AND NOT PER DESCRIPTOR!!!
  dev->driver->idx += 1;
  dev->driver_idx = (dev->driver_idx + 1) % q_size;

  mutex_unlock(&dev->lock);
  // debugf("gpu_send_3: unlocked!\n");

  // debugf("at idx %d\n", at_idx);
  // debugf("driver: idx1 %d, idx2 %d\n", dev->driver_idx, dev->driver->idx);
  // debugf("device: idx1 %d, idx2 %d\n", dev->device_idx, dev->device->idx);

  // Notify the device.
  *(dev->notify) = 1;
}

static void gpu_wait_for_response(GpuDevice *gdev) {
  VirtioDevice *dev = gdev->viodev;
  while(dev->isr->queue_interrupt != 0) { // while(dev->device_idx == dev->device->idx) {
    // debugf("gpu_wait_for_response: waiting...\n");
  }
  // DO NOT COMMENT OUT!!! GPU NEEDS THIS TO WORK :(
  debugf("gpu_wait_for_response: done!\n");
  mutex_spinlock(&dev->lock);
  while(dev->device_idx != dev->device->idx) dev->device_idx += 1;
  mutex_unlock(&dev->lock);
}

static void gpu_handle_used(GpuDevice *gdev) {
  VirtioDevice *dev = gdev->viodev;
  mutex_spinlock(&dev->lock);
  while(dev->device_idx != dev->device->idx) dev->device_idx += 1;
  mutex_unlock(&dev->lock);
}

GpuDevice *gpu_init(VirtioDevice *viodev) {
  debugf("gpu_init: initializing gpu\n");

  if(!g_gpu_devices) g_gpu_devices = list_new();

  // Get capabilities.
  uint8_t capes_next = viodev->pcidev->ecam->common.capes_pointer;
  uint64_t cap_addr = (uint64_t)viodev->pcidev->ecam + capes_next;
  VirtioCapability *v_cap = VIRTIO_CAP(cap_addr);
  
  // Get device-specific config cape.
  while(capes_next != 0) {
    v_cap = VIRTIO_CAP((uint64_t)viodev->pcidev->ecam + v_cap->next);
    if(v_cap->type == VIRTIO_PCI_CAP_DEVICE_CFG) {
      break;
    }
    capes_next = v_cap->next;
  }
  if(!v_cap) {
    debugf("gpu_init: v_cap is not set for device\n");
    return NULL;
  }

  uint8_t bar_num = v_cap->bar;
  uint32_t *bar_addr = viodev->pcidev->ecam->type0.bar + bar_num;
  uint64_t MMIO_addr = ((((*bar_addr >> 1) & 0b11) == 0b10) ? (*(uint64_t *)bar_addr) & ~0xf : (*(uint32_t *)bar_addr)) & ~0xf;
  uint64_t gpu_cfg_adr = MMIO_addr + v_cap->offset;

  GpuDevice *gdev = (GpuDevice *)g_kmalloc(sizeof(GpuDevice));
  gdev->viodev = viodev;
  gdev->config = (VirtioGpuConfig *)gpu_cfg_adr;
  // map_init((Map *)&gdev->rqbuf);
  list_add_ptr(g_gpu_devices, gdev);
  return gdev;
}

bool gpu_setup(GpuDevice *gdev) {
  /* TODO:
   When we resize the window, we need to check for the device configuration bit in the interrupt
   and call this to reinitialize our framebuffer.
  */
  GpuDisplayInfoResponse disp;
  GpuControlHeader hdr;
  GpuResourceCreate2dRequest res2d;
  GpuResourceAttachBacking attach;
  GpuSetScanoutRequest scan;
  GpuMemEntry mem;

  // 1. Get the display dimensions.
  debugf("gpu_setup: getting display dimensions\n");
  memset(&hdr, 0, sizeof(hdr));
  hdr.control_type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
  gpu_send(gdev, &hdr, sizeof(hdr), &disp, sizeof(disp));
  gpu_wait_for_response(gdev);
  if(disp.hdr.control_type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO || disp.displays[0].enabled != 1) {
    debugf("gpu_setup: display initialization failed\n");
    return false;
  }

  gdev->width = disp.displays[0].rect.width;
  gdev->height = disp.displays[0].rect.height;
  gdev->framebuffer = (PixelRGBA *)g_kzalloc(sizeof(PixelRGBA) * gdev->width * gdev->height);

  // 2. Create a resource 2D.
  debugf("gpu_setup: creating 2D resource\n");
  memset(&res2d, 0, sizeof(res2d));
  res2d.format = R8G8B8A8_UNORM;
  res2d.width = gdev->width;
  res2d.height = gdev->height;
  res2d.resource_id = 1;
  res2d.hdr.control_type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
  memset(&hdr, 0, sizeof(hdr));
  gpu_send(gdev, &res2d, sizeof(res2d), &hdr, sizeof(hdr));
  gpu_wait_for_response(gdev);
  if(hdr.control_type != VIRTIO_GPU_RESP_OK_NODATA) return false;

  // 3. Attach resource 2D.
  debugf("gpu_setup: attaching 2D resource\n");
  memset(&attach, 0, sizeof(attach));
  attach.num_entries = 1;
  attach.resource_id = 1;
  attach.hdr.control_type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
  mem.addr = mmu_translate_ptr_to_u64(kernel_mmu_table, gdev->framebuffer);
  mem.length = sizeof(PixelRGBA) * gdev->width * gdev->height;
  mem.padding = 0;
  memset(&hdr, 0, sizeof(hdr));
  gpu_send_3(gdev, &attach, sizeof(attach), &mem, sizeof(mem), &hdr, sizeof(hdr));
  gpu_wait_for_response(gdev);
  if(hdr.control_type != VIRTIO_GPU_RESP_OK_NODATA) return false;

  // 4. Set scanout and connect it to the resource.
  debugf("gpu_setup: setting scanout\n");
  memset(&scan, 0, sizeof(scan));
  scan.hdr.control_type = VIRTIO_GPU_CMD_SET_SCANOUT;
  scan.rect.width = gdev->width;
  scan.rect.height = gdev->height;
  scan.resource_id = 1;
  scan.scanout_id = 0;
  memset(&hdr, 0, sizeof(hdr));
  gpu_send(gdev, &scan, sizeof(scan), &hdr, sizeof(hdr));
  gpu_wait_for_response(gdev);
  if(hdr.control_type != VIRTIO_GPU_RESP_OK_NODATA) return false;

  // Add something to the framebuffer so it will show up on screen.
  Rectangle r1 = { 0, 0, gdev->width, gdev->height };
  Rectangle r2 = { 100, 100, gdev->width - 200, gdev->height - 200 };
  PixelRGBA z1 = { 255, 100, 50, 255 };
  PixelRGBA z2 = { 50, 0, 255, 255 };
  PixelRGBA z3 = { 50, 255, 255, 255 };
  PixelRGBA z4 = { 0, 255, 50, 255 };
  PixelRGBA z5 = { 0, 0, 0, 255 };
  
  // fb_fill_rect(gdev->width, gdev->height, gdev->framebuffer, &r1, &z5);
  // fb_stroke_rect(gdev->width, gdev->height, gdev->framebuffer, &r2, &z1, 10);
  // fb_fill_circ(gdev->width, gdev->height, gdev->framebuffer, 60, 120, 50, &z2);
  // fb_stroke_circ(gdev->width, gdev->height, gdev->framebuffer, 160, 120, 50, &z2, 30);

  debugf("gpu_setup: drawing...\n");
  if(!gpu_redraw(&scan.rect, gdev)) return false;

  // fb_fill_rect(gdev->width, gdev->height, gdev->framebuffer, &r1, &z4);

  // fb_stroke_string("Hello, world!", 1, 1, gdev, gdev->width, gdev->height, gdev->framebuffer, &z2, 1, 1);
  // fb_stroke_string(" !\"#$%&\'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~", 5, 1, gdev, gdev->width, gdev->height, gdev->framebuffer, &z3, 1, 1);

  return true;
}

/* Allows for updating of the GPU memory after initialization. */
bool gpu_redraw(const Rectangle *rect, GpuDevice *gdev) {
  // Check if the device is enabled.
  if((gdev->viodev->common_cfg->device_status & VIRTIO_F_DRIVER_OK) == 0x0) {
    return false;
  }

  GpuControlHeader hdr;
  GpuResourceFlush flush;
  GpuTransferToHost2d tx;

  // 5. Transfer the framebuffer to the host 2d.
  memset(&hdr, 0, sizeof(hdr));
  memset(&tx, 0, sizeof(tx));
  tx.hdr.control_type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
  tx.resource_id = 1;
  tx.offset = rect->x * sizeof(PixelRGBA) + rect->y * gdev->width * sizeof(PixelRGBA);
  tx.rect = *rect;
  memset(&hdr, 0, sizeof(hdr));
  gpu_send(gdev, &tx, sizeof(tx), &hdr, sizeof(hdr));
  gpu_wait_for_response(gdev);
  if(hdr.control_type != VIRTIO_GPU_RESP_OK_NODATA) return false;

  // 6. Flush the resource to draw to the screen.
  memset(&flush, 0, sizeof(flush));
  flush.hdr.control_type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
  flush.resource_id = 1;
  /*
   Always flush the entire screen (!!!!!), but only the transferred pixels change, so this is efficient I think.
   Otherwise, it only updates partially and in weird ways. Spent way too long debugging this.
  */
  Rectangle r = { 0, 0, gdev->width, gdev->height };
  flush.rect = r;
  memset(&hdr, 0, sizeof(hdr));
  gpu_send(gdev, &flush, sizeof(flush), &hdr, sizeof(hdr));
  gpu_wait_for_response(gdev);
  if(hdr.control_type != VIRTIO_GPU_RESP_OK_NODATA) return false;

  return true;
}

void fb_fill_rect(uint32_t screen_width, uint32_t screen_height, PixelRGBA *buffer, Rectangle *rect, PixelRGBA *fill_color) {
  uint32_t top = rect->y;
  uint32_t bottom = rect->y + rect->height;
  uint32_t left = rect->x;
  uint32_t right = rect->x + rect->width;
  uint32_t row;
  uint32_t col;
  if(bottom > screen_height) bottom = screen_height;
  if(right > screen_width) right = screen_width;
  for(row = top; row < bottom; row++) {
    for(col = left; col < right; col++) {
      uint32_t offset = row * screen_width + col;
      buffer[offset] = *fill_color;
    }
  }
}

void fb_stroke_rect(uint32_t screen_width, uint32_t screen_height, PixelRGBA *buffer, Rectangle *rect, PixelRGBA *line_color, uint32_t line_size) {
  // Top
  Rectangle top_rect = {rect->x, 
                        rect->y,
                        rect->width,
                        line_size};
  fb_fill_rect(screen_width, screen_height, buffer, &top_rect, line_color);
  // Bottom
  Rectangle bot_rect = {rect->x,
                        rect->height + rect->y,
                        rect->width,
                        line_size};
  fb_fill_rect(screen_width, screen_height, buffer, &bot_rect, line_color);
  // Left
  Rectangle left_rect = {rect->x,
                        rect->y,
                        line_size,
                        rect->height};
  fb_fill_rect(screen_width, screen_height, buffer, &left_rect, line_color);
  // Right
  Rectangle right_rect = {rect->x + rect->width,
                          rect->y,
                          line_size,
                          rect->height + line_size};
  fb_fill_rect(screen_width, screen_height, buffer, &right_rect, line_color);
}

void fb_fill_char(char c, uint32_t row, uint32_t col, uint32_t screen_width, uint32_t screen_height, PixelRGBA *buffer, const PixelRGBA *fg_color, const PixelRGBA *bg_color, uint32_t scale_w, uint32_t scale_h) {
  // //debugf("fb_draw_char: termfont is at 0x%x\n", termfont);
  // Each character is 16x8 bits = 16 bytes of the array and in ASCII order. 
  uint32_t pos = c * 16;
  const PixelRGBA *color;

  col *= 8 * STRING_SCALE_WIDTH * scale_w;
  row *= 16 * STRING_SCALE_HEIGHT * scale_h;

  // This assumes the font is exactly 8x16.
  for(uint32_t i = 0; i < 16; i++) {
    for(uint32_t j = 0; j < 8; j++) {
      // Color is either foreground or background based on bitmap font.
      color = (termfont[pos + i] & (1 << j)) ? fg_color : bg_color;

      for(uint32_t k = 0; k < STRING_SCALE_WIDTH * scale_w; k++) {
        for(uint32_t l = 0; l < STRING_SCALE_HEIGHT * scale_h; l++) {
          uint32_t offset = (row + i * STRING_SCALE_HEIGHT * scale_h + l) * screen_width +
                            (col + j * STRING_SCALE_WIDTH * scale_w + k);
          buffer[offset] = *color;
        }
      }
    }
  }
}

void fb_fill_string(char *s, uint32_t row, uint32_t col, GpuDevice *gdev, uint32_t screen_width, uint32_t screen_height, PixelRGBA *buffer, const PixelRGBA *fg_color, const PixelRGBA *bg_color, uint32_t scale_w, uint32_t scale_h) {
  uint32_t start_row = row, start_col = col, len = strlen(s);
  
  for(uint32_t i = 0; *s; i++) {
    // Wrap text to next row of given area if the end of this character is not within bounds.
    if((col + i + 1) * 8 * STRING_SCALE_WIDTH * scale_w > screen_width) {
      row++;
      len = i;
      i = 0;
    }
    fb_fill_char(*s++, row, col + i, screen_width, screen_height, buffer, fg_color, bg_color, scale_w, scale_h);
  }

  // The font is 8x16, so it's hardcoded here. Probably not the best idea.
  Rectangle redraw;
  redraw.x      = start_col * STRING_SCALE_WIDTH * scale_w * 8;
  redraw.y      = start_row * STRING_SCALE_HEIGHT * scale_h * 16;
  redraw.width  = len * 8 * STRING_SCALE_WIDTH * scale_w;
  redraw.height = (row - start_row + 1) * 16 * STRING_SCALE_HEIGHT * scale_h;
  // Go ahead and redraw only the rectangle containing the string so we don't have to redraw the whole frame separately later.
  // fb_stroke_rect(screen_width, screen_height, buffer, &redraw, fg_color, 2); // View redraw rectangle for debugging
  // gpu_redraw(&redraw, gdev);
}

void fb_stroke_char(char c, uint32_t row, uint32_t col, uint32_t screen_width, uint32_t screen_height, PixelRGBA *buffer, const PixelRGBA *color, uint32_t scale_w, uint32_t scale_h) {
  // debugf("fb_draw_char: termfont is at 0x%x\n", termfont);
  // Each character is 16x8 bits = 16 bytes of the array and in ASCII order. 
  uint32_t pos = c * 16;

  col *= 8 * STRING_SCALE_WIDTH * scale_w;
  row *= 16 * STRING_SCALE_HEIGHT * scale_h;

  // This assumes the font is exactly 8x16.
  for(uint32_t i = 0; i < 16; i++) {
    for(uint32_t j = 0; j < 8; j++) {
      for(uint32_t k = 0; k < STRING_SCALE_WIDTH * scale_w; k++) {
        for(uint32_t l = 0; l < STRING_SCALE_HEIGHT * scale_h; l++) {
          uint32_t offset = (row + i * STRING_SCALE_HEIGHT * scale_h + l) * screen_width +
                            (col + j * STRING_SCALE_WIDTH * scale_w + k);
          if(termfont[pos + i] & (1 << j)) buffer[offset] = *color;
        }
      }
    }
  }
}

void fb_stroke_string(char *s, uint32_t row, uint32_t col, GpuDevice *gdev, uint32_t screen_width, uint32_t screen_height, PixelRGBA *buffer, const PixelRGBA *color, uint32_t scale_w, uint32_t scale_h) {
  uint32_t start_row = row, start_col = col, len = strlen(s);
  
  for(uint32_t i = 0; *s; i++) {
    // Wrap text to next row of given area if the end of this character is not within bounds.
    if((col + i + 1) * 8 * STRING_SCALE_WIDTH * scale_w > screen_width) {
      row++;
      len = i;
      i = 0;
    }
    fb_stroke_char(*s++, row, col + i, screen_width, screen_height, buffer, color, scale_w, scale_h);
  }

  // The font is 8x16, so it's hardcoded here. Probably not the best idea.
  Rectangle redraw;
  redraw.x      = start_col * STRING_SCALE_WIDTH * scale_w * 8;
  redraw.y      = start_row * STRING_SCALE_HEIGHT * scale_h * 16;
  redraw.width  = len * 8 * STRING_SCALE_WIDTH * scale_w;
  redraw.height = (row - start_row + 1) * 16 * STRING_SCALE_HEIGHT * scale_h;
  // Go ahead and redraw only the rectangle containing the string so we don't have to redraw the whole frame separately later.
  // fb_stroke_rect(screen_width, screen_height, buffer, &redraw, color, 2); // View redraw rectangle for debugging
  // gpu_redraw(&redraw, gdev);
}

void fb_fill_circ(uint32_t screen_width, uint32_t screen_height, PixelRGBA *buffer, uint32_t center_x, uint32_t center_y, uint32_t radius, PixelRGBA *fill_color) {
  uint32_t radius2 = radius * radius;

  for(uint32_t y = 0; y < screen_height; y++) {
    for(uint32_t x = 0; x < screen_width; x++) {
      uint32_t offset = y * screen_width + x;
      uint32_t dx = x - center_x;
      uint32_t dy = y - center_y;
      uint32_t distance2 = dx * dx + dy * dy;
      if(distance2 <= radius2) buffer[offset] = *fill_color;
    }
  }
}

void fb_stroke_circ(uint32_t screen_width, uint32_t screen_height, PixelRGBA *buffer, uint32_t center_x, uint32_t center_y, uint32_t radius, PixelRGBA *line_color, uint32_t line_size) {
  uint32_t radius2 = radius * radius;
  uint32_t line_size2 = line_size * line_size;

  for(uint32_t y = 0; y < screen_height; y++) {
    for(uint32_t x = 0; x < screen_width; x++) {
      uint32_t offset = y * screen_width + x;
      uint32_t dx = x - center_x;
      uint32_t dy = y - center_y;
      uint32_t distance2 = dx * dx + dy * dy;
      if(distance2 - radius2 <= line_size2) buffer[offset] = *line_color;
    }
  }
}