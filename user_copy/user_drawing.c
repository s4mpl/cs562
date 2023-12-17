#include <stdint.h>
#include <stdbool.h>
#include <event.h>
#include <unistd.h>
#include <stddef.h>
#include <printf.h>
#include <string.h>

typedef struct ppm {
  uint32_t width;
  uint32_t height;
  char *img;
} PPM;

typedef struct Rectangle {
  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;
} Rectangle;

typedef struct PixelRGBA {
  /* This pixel structure must match the format! */
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
} PixelRGBA;

typedef struct coord_pair {
  uint32_t x, y;
} coord_pair;

typedef struct GpuDevice {
  void *viodev;
  void *config;
  uint32_t width;
  uint32_t height;
  PixelRGBA *framebuffer;
} GpuDevice;

typedef struct virtio_input_event InputEvent;

/* Utility functions for reading/writing P6 PPM files. */
static PPM *ppm_read(char *file_path);
static void ppm_write(PPM *ppm, char *file_path);
static void ppm_read_fb(char *file_path, GpuDevice *gdev, uint32_t x_offset, uint32_t y_offset);
static void ppm_write_fb(char *file_path, GpuDevice *gdev, uint32_t x_offset, uint32_t y_offset, uint32_t width, uint32_t height);

static void fb_fill_rect(uint32_t screen_width, uint32_t screen_height, PixelRGBA *buffer, Rectangle *rect, PixelRGBA *fill_color);
static void fb_fill_circ(uint32_t screen_width, uint32_t screen_height, PixelRGBA *buffer, uint32_t center_x, uint32_t center_y, uint32_t radius, PixelRGBA *fill_color);

static void drawing_proc();

static uint32_t read_integer(int fd);
static void write_integer(int fd, int n);

static bool isdigit(char c) {
  return '0' <= c && c <= '9';
}

void main(void) {
  drawing_proc();
}

PPM *ppm_read(char *file_path) {
  PPM *ppm = malloc(sizeof(ppm));
  int fd = open(file_path, O_RDONLY, 666);

  printf("opened file sucessfully!\n");

  char buf[4] = { 0 };

  /* Read file header. */
  read(fd, buf, 3);
  if(strcmp(buf, "P6\n") != 0) return NULL;
  ppm->width = read_integer(fd);
  ppm->height = read_integer(fd);

  if(read_integer(fd) != 255) return NULL; // max value

  printf("read ppm header successfully!\n");

  /* Allocate image data array and fill it. */
  ppm->img = malloc(ppm->width * ppm->height * 3);
  printf("malloc'd %p\n", ppm->img);

  read(fd, ppm->img, ppm->width * ppm->height * 3);

  printf("read ppm image data successfully!\n");

  close(fd);
  return ppm;
}

void ppm_write(PPM *ppm, char *file_path) {
  int fd = open(file_path, O_WRONLY | O_CREAT, 666);

  /* Write file header. */
  write(fd, "P6\n", 3);
  write_integer(fd, ppm->width);
  write(fd, " ", 1);
  write_integer(fd, ppm->height);
  write(fd, "\n", 1);
  write_integer(fd, 255);
  write(fd, "\n", 1);

  /* Write image data. */
  write(fd, ppm->img, ppm->width * ppm->height * 3);

  close(fd);
}

void ppm_read_fb(char *file_path, GpuDevice *gdev, uint32_t x_offset, uint32_t y_offset) {
  PPM *ppm = ppm_read(file_path);

  for(uint32_t i = 0, j = 0, pixel_idx = y_offset * gdev->width + x_offset; i < ppm->width * ppm->height * 3; i += 3, j++) {
    // debugf("pixel idx %u\n", pixel_idx);

    gdev->framebuffer[pixel_idx] = (PixelRGBA){ .r = ppm->img[i], .g = ppm->img[i+1], .b = ppm->img[i+2], .a = 255 };

    // If the next pixel will start the next row of the ppm, advance the fb pointer there.
    if(j + 1 == ppm->width) {
      pixel_idx += (gdev->width - ppm->width + 1);
      j = -1; // since j++ will execute
    }
    else pixel_idx++;
  }

  free(ppm->img);
  free(ppm);
}

void ppm_write_fb(char *file_path, GpuDevice *gdev, uint32_t x_offset, uint32_t y_offset, uint32_t width, uint32_t height) {
  PPM *ppm = malloc(sizeof(ppm));
  ppm->width = width;
  ppm->height = height;

  ppm->img = malloc(ppm->width * ppm->height * 3);
  // Coming up with this math was fun!
  for(uint32_t i = 0, j = 0, pixel_idx = y_offset * gdev->width + x_offset; pixel_idx < ((gdev->width * (height - 1) + width) + (y_offset * gdev->width + x_offset)); i += 3, j++) {
    ppm->img[i] = gdev->framebuffer[pixel_idx].r;
    ppm->img[i+1] = gdev->framebuffer[pixel_idx].g;
    ppm->img[i+2] = gdev->framebuffer[pixel_idx].b;

    // If the next pixel will start the next row of the ppm, advance the fb pointer there.
    if(j + 1 == width) {
      pixel_idx += (gdev->width - width + 1);
      j = -1; // since j++ will execute
    }
    else pixel_idx++;
  }

  ppm_write(ppm, file_path);
  free(ppm->img);
  free(ppm);
}

static void get_color(signed char key, PixelRGBA *color) {
  switch(key) {
    // (r)ed
    case 'r':
      *color = (PixelRGBA){ 255, 0, 0, 255 };
      break;
    // (g)reen
    case 'g':
      *color = (PixelRGBA){ 0, 255, 0, 255 };
      break;
    // (b)lue
    case 'b':
      *color = (PixelRGBA){ 0, 0, 255, 255 };
      break;
    // (c)yan
    case 'c':
      *color = (PixelRGBA){ 0, 255, 255, 255 };
      break;
    // (m)agenta
    case 'm':
      *color = (PixelRGBA){ 255, 0, 255, 255 };
      break;
    // (y)ellow
    case 'y':
      *color = (PixelRGBA){ 255, 255, 0, 255 };
      break;
    // (o)range
    case 'o':
      *color = (PixelRGBA){ 255, 150, 0, 255 };
      break;
    // (p)urple
    case 'p':
      *color = (PixelRGBA){ 150, 0, 255, 255 };
      break;
    // (w)hite
    case 'w':
      *color = (PixelRGBA){ 255, 255, 255, 255 };
      break;
    // (e)rase / black
    case 'e':
      *color = (PixelRGBA){ 0, 0, 0, 255 };
      break;
    // (n)othing
    case 'n':
      *color = (PixelRGBA){ 0, 0, 0, 0 };
      break;
  }
}

void drawing_proc() {
  GpuDevice *gdev = get_gpu();

  Rectangle screen = { 0, 0, gdev->width, gdev->height };
  PixelRGBA bg_color = { 0, 0, 0, 255 };
  PixelRGBA draw_color = { 0, 0, 0, 0 };
  uint8_t draw_size = 2;

  InputEvent curr_event;
  uint32_t current_event_idx, event_ring_size;
  signed char key;
  coord_pair coord, saved_coord, tmp;
  bool saved = false;
  
  /* Clear screen to start. */
  fb_fill_rect(gdev->width, gdev->height, gdev->framebuffer, &screen, &bg_color);

  // ppm_read_fb("/images/marz_photo_ppm.ppm", gdev, 0, 0);

  while(1) {
    /* Get last key release for draw options. Ignore key press events. */
    key = get_keyboard();
    while(key != 0) {
      if(key > 0) {
        key = get_keyboard();
        continue;
      }

      key = -key;

      if(isdigit(key)) {
        draw_size = key - '0';
        continue;
      }

      // (s)ave
      if(key == 's') {
        ppm_write_fb("/home/art.ppm", gdev, 0, 0, gdev->width, gdev->height);
        key = 'n';
        saved = true;
      }
      // (u)ndo / (u)se image brush at top-left corner
      if(key == 'u') {
        if(saved) ppm_read_fb("/home/art.ppm", gdev, coord.x, coord.y);
        else ppm_read_fb("/home/brush.ppm", gdev, coord.x, coord.y);
        key = 'n';
      }
      // (i)mage brush save at top-left corner
      if(key == 'i') {
        ppm_write_fb("/home/brush.ppm", gdev, coord.x, coord.y, saved_coord.x - coord.x, saved_coord.y - coord.y);
        key = 'n';
        saved = false;
      }
      // image brush save bottom-right corner
      if(key == 'j') {
        saved_coord = coord;
        key = 'n';
      }
      // Mar(z)
      if(key == 'z') {
        printf("Marz coords: (%u, %u)\n", coord.x, coord.y);
        ppm_read_fb("/images/marz_photo_ppm.ppm", gdev, coord.x, coord.y);
        key = 'n';
      }

      get_color(key, &draw_color);
      key = get_keyboard();
    }

    /* Get position and draw. */
    // input_handle(g_tabdev);
    // event_ring_size = ring_size(g_tabdev->ring_buffer);

    // current_event_idx = 0;
    // for(current_event_idx = 0; current_event_idx < event_ring_size; current_event_idx++) {
    //   uint64_t raw_event = input_ring_buffer_pop(g_tabdev);

    //   curr_event.type = (uint16_t)(raw_event & 0xFFFF);  // Extract the lower 16 bits of after_pop as the type
      
    //   if(curr_event.type == EV_SYN) {
    //     // debugf("Skipping sync\n");
    //     continue;
    //   }

    //   curr_event.code = (uint16_t)((raw_event >> 16) & 0xFFFF); // Extract the next 16 bits as the code
    //   curr_event.value = (uint32_t)((raw_event >> 32) & 0xFFFFFFFF); // Extract the upper 32 bits as the value

    //   // debugf("code: %u, value: %u\n", curr_event.code, curr_event.value);

    //   if(curr_event.code == ABS_X) {
    //     coord.x = (curr_event.value * gdev->width) / 32767;
    //   } else if(curr_event.code == ABS_Y) {
    //     coord.y = (curr_event.value * gdev->height) / 32767;

    //     // debugf("drawing circle at (%u, %u)\n", coord_x, coord_y);

    //     // Every pair that completes is ready to be drawn.
    //     // (n)othing
    //     if(draw_color.a != 0)
    //       fb_fill_circ(gdev->width, gdev->height, gdev->framebuffer, coord.x, coord.y, draw_size * 2, &draw_color);
    //   }
    // }

    get_cursor(&tmp);
    while(tmp.x != -1) {
      coord = tmp;

      if(draw_color.a != 0)
        fb_fill_circ(gdev->width, gdev->height, gdev->framebuffer, coord.x, coord.y, draw_size, &draw_color);

      get_cursor(&tmp);
    }

    gpu_redraw(gdev);
  }
}

uint32_t read_integer(int fd) {
  uint32_t n = 0;
  char c;

  read(fd, &c, 1);
  while(!isdigit(c)) read(fd, &c, 1);

  while(isdigit(c)) {
    n *= 10;
    n += c - '0';
    read(fd, &c, 1);
  }

  return n;
}

void write_integer(int fd, int n) {
  char buf[4]; // assume number is no longer than 3 digits! :-)
  uint32_t idx = 0;

  if(n < 0) {
    write(fd, "-", 1);
    n = -n;
  }

  while(n > 0) {
    buf[idx++] = (n % 10) + '0';
    n /= 10;
  }

  for(; idx > 0; idx--) {
    write(fd, &buf[idx-1], 1);
  }
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
