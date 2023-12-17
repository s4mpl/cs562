#include <drawing.h>
#include <input.h>
#include <minix3.h>
#include <input-event-codes.h>

static bool isdigit(char c) {
  return '0' <= c && c <= '9';
}

PPM *ppm_read(char *file_path) {
  PPM *ppm = kzalloc(sizeof(ppm));
  int fd = open(file_path, O_RDONLY, 666);
  char buf[4] = { 0 };

  /* Read file header. */
  read(fd, buf, 3);
  if(strcmp(buf, "P6\n") != 0) return NULL;
  ppm->width = read_integer(fd);
  ppm->height = read_integer(fd);

  if(read_integer(fd) != 255) return NULL; // max value

  debugf("width: %d, height: %d\n", ppm->width, ppm->height);

  /* Allocate image data array and fill it. */
  ppm->img = kzalloc(ppm->width * ppm->height * 3);
  read(fd, ppm->img, ppm->width * ppm->height * 3);

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

  kfree(ppm->img);
  kfree(ppm);
}

void ppm_write_fb(char *file_path, GpuDevice *gdev, uint32_t x_offset, uint32_t y_offset, uint32_t width, uint32_t height) {
  PPM *ppm = kzalloc(sizeof(ppm));
  ppm->width = width;
  ppm->height = height;

  ppm->img = kzalloc(ppm->width * ppm->height * 3);
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
  kfree(ppm->img);
  kfree(ppm);
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
  Rectangle screen = { 0, 0, g_gdev->width, g_gdev->height };
  PixelRGBA bg_color = { 0, 0, 0, 255 };
  PixelRGBA draw_color = { 0, 0, 0, 0 };
  uint8_t draw_size = 2;

  InputEvent curr_event;
  uint32_t current_event_idx, event_ring_size;
  signed char key;
  coord_pair coord, saved_coord;

  /* Clear screen to start. */
  fb_fill_rect(g_gdev->width, g_gdev->height, g_gdev->framebuffer, &screen, &bg_color);

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
        ppm_write_fb("/home/art.ppm", g_gdev, 0, 0, g_gdev->width, g_gdev->height);
        key = 'n';
      }
      // (u)ndo / (u)se image brush at top-left corner
      if(key == 'u') {
        ppm_read_fb("/home/brush.ppm", g_gdev, coord.x, coord.y);
        key = 'n';
      }
      // (i)mage brush save at top-left corner
      if(key == 'i') {
        ppm_write_fb("/home/brush.ppm", g_gdev, coord.x, coord.y, saved_coord.x - coord.x, saved_coord.y - coord.y);
        key = 'n';
      }
      // image brush save bottom-right corner
      if(key == 'j') {
        saved_coord = coord;
        key = 'n';
      }
      // Mar(z)
      if(key == 'z') {
        ppm_read_fb("/images/marz_photo_ppm.ppm", g_gdev, coord.x, coord.y);
        key = 'n';
      }

      get_color(key, &draw_color);
      key = get_keyboard();
    }

    /* Get position and draw. */
    input_handle(g_tabdev);
    event_ring_size = ring_size(g_tabdev->ring_buffer);

    current_event_idx = 0;
    for(current_event_idx = 0; current_event_idx < event_ring_size; current_event_idx++) {
      uint64_t raw_event = input_ring_buffer_pop(g_tabdev);

      curr_event.type = (uint16_t)(raw_event & 0xFFFF);  // Extract the lower 16 bits of after_pop as the type
      
      if(curr_event.type == EV_SYN) {
        // debugf("Skipping sync\n");
        continue;
      }

      curr_event.code = (uint16_t)((raw_event >> 16) & 0xFFFF); // Extract the next 16 bits as the code
      curr_event.value = (uint32_t)((raw_event >> 32) & 0xFFFFFFFF); // Extract the upper 32 bits as the value

      // debugf("code: %u, value: %u\n", curr_event.code, curr_event.value);

      if(curr_event.code == ABS_X) {
        coord.x = (curr_event.value * g_gdev->width) / 32767;
      } else if(curr_event.code == ABS_Y) {
        coord.y = (curr_event.value * g_gdev->height) / 32767;

        // debugf("drawing circle at (%u, %u)\n", coord_x, coord_y);

        // Every pair that completes is ready to be drawn.
        // (n)othing
        if(draw_color.a != 0)
          fb_fill_circ(g_gdev->width, g_gdev->height, g_gdev->framebuffer, coord.x, coord.y, draw_size * 2, &draw_color);
      }
    }

    // coord = get_cursor();
    // if(draw_color.a != 0 && coord.x != -1)
    //   fb_fill_circ(g_gdev->width, g_gdev->height, g_gdev->framebuffer, coord.x, coord.y, draw_size, &draw_color);

    gpu_redraw(&screen, g_gdev);
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