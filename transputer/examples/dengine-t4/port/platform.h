/* Services the dEngine port's libc, GL layer and drivers need from the
 * machine they run on: console, heap bounds, the frame clock and the ROM. */
#ifndef DE_PLATFORM_H
#define DE_PLATFORM_H
#include <stddef.h>

/* Supplied by the driver (node.c on the T4, host_main.c on the host). */
void de_platform_write(const char *text, int length);
/* Heap segment n (0, 1, ...); returns 0 when there is no such segment. */
int de_platform_heap(int segment, unsigned char **start, unsigned char **end);
void de_platform_exit(int status);
void de_platform_fatal(const char *message);
void de_platform_out_of_memory(size_t request);
unsigned int de_platform_time_ms(void);

/* romfs.c: read-only files packed by mkrom.py. Paths are relative to the
 * dEngine checkout ("data/..."); "./" and "/" prefixes are ignored. */
void de_rom_mount(const unsigned char *image, unsigned long size);
const unsigned char *de_rom_open(const char *path, unsigned long *size);
unsigned int de_rom_file_count(void);

size_t de_heap_peak(void);
size_t de_heap_live(void);
#endif
