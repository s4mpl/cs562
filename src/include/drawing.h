#pragma once

#include <stdint.h>
#include <gpu.h>

typedef struct ppm {
  uint32_t width;
  uint32_t height;
  char *img;
} PPM;

/* Utility functions for reading/writing P6 PPM files. */
PPM *ppm_read(char *file_path);
void ppm_write(PPM *ppm, char *file_path);
void ppm_read_fb(char *file_path, GpuDevice *gdev, uint32_t x_offset, uint32_t y_offset);
void ppm_write_fb(char *file_path, GpuDevice *gdev, uint32_t x_offset, uint32_t y_offset, uint32_t width, uint32_t height);

void drawing_proc();

uint32_t read_integer(int fd);
void write_integer(int fd, int n);