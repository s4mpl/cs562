#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <virtio.h>

#define VIRTIO_GPU_EVENT_DISPLAY (1 << 0)
typedef struct VirtioGpuConfig {
  uint32_t events_read;
  uint32_t events_clear;
  uint32_t num_scanouts;
  uint32_t reserved;
} VirtioGpuConfig;

// static Map virtio_gpu_devices;
// static int num_gpu_devices;

#define VIRTIO_GPU_FLAG_FENCE 1
typedef struct GpuControlHeader {
  uint32_t control_type;
  uint32_t flags;
  uint64_t fence_id;
  uint32_t context_id;
  uint32_t padding;
} GpuControlHeader;

typedef enum GpuControlType {
  /* 2D commands */
  VIRTIO_GPU_CMD_GET_DISPLAY_INFO = 0x0100,
  VIRTIO_GPU_CMD_RESOURCE_CREATE_2D,
  VIRTIO_GPU_CMD_RESOURCE_UNREF,
  VIRTIO_GPU_CMD_SET_SCANOUT,
  VIRTIO_GPU_CMD_RESOURCE_FLUSH,
  VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D,
  VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING,
  VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING,
  VIRTIO_GPU_CMD_GET_CAPSET_INFO,
  VIRTIO_GPU_CMD_GET_CAPSET,
  VIRTIO_GPU_CMD_GET_EDID,
  /* cursor commands */
  VIRTIO_GPU_CMD_UPDATE_CURSOR = 0x0300,
  VIRTIO_GPU_CMD_MOVE_CURSOR,
  /* success responses */
  VIRTIO_GPU_RESP_OK_NODATA = 0x1100,
  VIRTIO_GPU_RESP_OK_DISPLAY_INFO,
  VIRTIO_GPU_RESP_OK_CAPSET_INFO,
  VIRTIO_GPU_RESP_OK_CAPSET,
  VIRTIO_GPU_RESP_OK_EDID,
  /* error responses */
  VIRTIO_GPU_RESP_ERR_UNSPEC = 0x1200,
  VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY,
  VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID,
  VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID,
  VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID,
  VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER,
} GpuControlType;

#define VIRTIO_GPU_MAX_SCANOUTS 16
typedef struct Rectangle {
  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;
} Rectangle;

typedef struct GpuDisplayInfoResponse {
  GpuControlHeader hdr;  /* VIRTIO_GPU_RESP_OK_DISPLAY_INFO */
  struct GpuDisplay {
    struct Rectangle rect;
    uint32_t enabled;
    uint32_t flags;
  } displays[VIRTIO_GPU_MAX_SCANOUTS];
} GpuDisplayInfoResponse;

enum GpuFormats {
  B8G8R8A8_UNORM = 1,
  B8G8R8X8_UNORM = 2,
  A8R8G8B8_UNORM = 3,
  X8R8G8B8_UNORM = 4,
  R8G8B8A8_UNORM = 67,
  X8B8G8R8_UNORM = 68,
  A8B8G8R8_UNORM = 121,
  R8G8B8X8_UNORM = 134,
};

typedef struct GpuResourceCreate2dRequest {
  GpuControlHeader hdr;      /* VIRTIO_GPU_CMD_RESOURCE_CREATE_2D */
  uint32_t resource_id;   /* We get to give a unique ID to each resource */
  uint32_t format;        /* From GpuFormats above */
  uint32_t width;
  uint32_t height;
} GpuResourceCreate2dRequest;

typedef struct GpuResourceUnrefRequest {
  GpuControlHeader hdr; /* VIRTIO_GPU_CMD_RESOURCE_UNREF */
  uint32_t resource_id;
  uint32_t padding;
} GpuResourceUnrefRequest;

typedef struct GpuSetScanoutRequest {
  GpuControlHeader hdr; /* VIRTIO_GPU_CMD_SET_SCANOUT */
  Rectangle rect;
  uint32_t scanout_id;
  uint32_t resource_id;
} GpuSetScanoutRequest;

typedef struct GpuResourceAttachBacking {
  GpuControlHeader hdr;
  uint32_t resource_id;
  uint32_t num_entries;
} GpuResourceAttachBacking;

typedef struct GpuResourceDetachBacking {
  GpuControlHeader hdr;
  uint32_t resource_id;
  uint32_t num_entries;
} GpuResourceDetachBacking;

typedef struct GpuMemEntry {
  uint64_t addr;
  uint32_t length;
  uint32_t padding;
} GpuMemEntry;

typedef struct GpuResourceFlush {
  GpuControlHeader hdr;
  Rectangle rect;
  uint32_t resource_id;
  uint32_t padding;
} GpuResourceFlush;

typedef struct GpuTransferToHost2d {
  GpuControlHeader hdr;
  Rectangle rect;
  uint64_t offset;
  uint32_t resource_id;
  uint32_t padding;
} GpuTransferToHost2d;

typedef struct PixelRGBA {
  /* This pixel structure must match the format! */
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
} PixelRGBA;

typedef struct GpuDevice {
  VirtioDevice *viodev;
  VirtioGpuConfig *config;
  uint32_t width;
  uint32_t height;
  PixelRGBA *framebuffer;
  // Map rqbuf;
  // Mutex rqbufmut;
} GpuDevice;

extern List *g_gpu_devices; /* Global list for GPU devices. */
extern GpuDevice *g_gdev;

GpuDevice *gpu_init(VirtioDevice *viodev);

bool gpu_setup(GpuDevice *gdev);

/* Allows for updating of the GPU memory after initialization. */
bool gpu_redraw(const Rectangle *rect, GpuDevice *gdev);

void fb_fill_rect(uint32_t screen_width, uint32_t screen_height, PixelRGBA *buffer, Rectangle *rect, PixelRGBA *fill_color);

void fb_stroke_rect(uint32_t screen_width, uint32_t screen_height, PixelRGBA *buffer, Rectangle *rect, PixelRGBA *line_color, uint32_t line_size);

#define STRING_SCALE_WIDTH 2
#define STRING_SCALE_HEIGHT 2
/* `termfont` is in the assembly, and each character is in order in the array. */
extern const uint8_t termfont[]; // `uint8_t *` didn't work here for some reason
void fb_fill_char(char c, uint32_t row, uint32_t col, uint32_t screen_width, uint32_t screen_height, PixelRGBA *buffer, const PixelRGBA *fg_color, const PixelRGBA *bg_color, uint32_t scale_w, uint32_t scale_h);
void fb_fill_string(char *s, uint32_t row, uint32_t col, GpuDevice *gdev, uint32_t screen_width, uint32_t screen_height, PixelRGBA *buffer, const PixelRGBA *fg_color, const PixelRGBA *bg_color, uint32_t scale_w, uint32_t scale_h);
void fb_stroke_char(char c, uint32_t row, uint32_t col, uint32_t screen_width, uint32_t screen_height, PixelRGBA *buffer, const PixelRGBA *color, uint32_t scale_w, uint32_t scale_h);
void fb_stroke_string(char *s, uint32_t row, uint32_t col, GpuDevice *gdev, uint32_t screen_width, uint32_t screen_height, PixelRGBA *buffer, const PixelRGBA *color, uint32_t scale_w, uint32_t scale_h);
void fb_fill_circ(uint32_t screen_width, uint32_t screen_height, PixelRGBA *buffer, uint32_t center_x, uint32_t center_y, uint32_t radius, PixelRGBA *fill_color);
void fb_stroke_circ(uint32_t screen_width, uint32_t screen_height, PixelRGBA *buffer, uint32_t center_x, uint32_t center_y, uint32_t radius, PixelRGBA *line_color, uint32_t line_size);
