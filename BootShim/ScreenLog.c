/* Early framebuffer diagnostics for myron. No libc, SIMD or MMIO discovery. */
typedef unsigned int u32;
typedef unsigned long long u64;
#include "ScreenFont.h"

#define FB_BASE 0xfc800000ULL
#define WIDTH 1200U
#define HEIGHT 2608U
#define STRIDE 1200U
#define SCALE 2U
#define MARGIN 24U
#define BG 0xff102040U
#define FG 0xffffffffU

static void sync_screen(void)
{
  __asm__ volatile("dsb sy" ::: "memory");
}

void myron_screen_line(const char *text, unsigned row)
{
  volatile u32 *fb = (volatile u32 *)FB_BASE;
  unsigned x = MARGIN, y = MARGIN + row * 16U * SCALE;
  if (y + 16U * SCALE > HEIGHT) return;
  for (unsigned gy = 0; gy < 16U * SCALE; ++gy)
    for (unsigned gx = MARGIN; gx < WIDTH - MARGIN; ++gx)
      fb[(y + gy) * STRIDE + gx] = BG;
  while (*text && x + 8U * SCALE <= WIDTH - MARGIN) {
    unsigned ch = (unsigned char)*text++;
    if (ch < 0x20 || ch > 0x7e) ch = '?';
    const unsigned char *glyph = font8x16[ch - 0x20];
    for (unsigned gy = 0; gy < 16; ++gy) {
      volatile u32 *line = fb + (y + gy * SCALE) * STRIDE + x;
      for (unsigned gx = 0; gx < 8; ++gx) {
        u32 color = (glyph[gy] & (0x80U >> gx)) ? FG : BG;
        line[gx * SCALE] = color;
        line[gx * SCALE + 1] = color;
        line[STRIDE + gx * SCALE] = color;
        line[STRIDE + gx * SCALE + 1] = color;
      }
    }
    x += 8U * SCALE;
  }
  sync_screen();
}

static void hex_line(const char *label, u64 value, unsigned row)
{
  char buffer[72];
  unsigned at = 0;
  while (*label && at < 48) buffer[at++] = *label++;
  buffer[at++] = ' '; buffer[at++] = '0'; buffer[at++] = 'x';
  for (int shift = 60; shift >= 0; shift -= 4) {
    unsigned digit = (unsigned)(value >> shift) & 15U;
    buffer[at++] = (char)(digit < 10 ? '0' + digit : 'A' + digit - 10);
  }
  buffer[at] = 0;
  myron_screen_line(buffer, row);
}

void myron_screen_start(u64 level, u64 sctlr, u64 fdt, u64 load)
{
  volatile u32 *fb = (volatile u32 *)FB_BASE;
  for (unsigned i = 0; i < STRIDE * 400U; ++i) fb[i] = BG;
  myron_screen_line("[0] UniXNU myron - SEC screen log (framebuffer 0xfc800000)", 0);
  hex_line("[1] CurrentEL (4=EL1,8=EL2)", level, 1);
  hex_line("[2] SCTLR", sctlr, 2);
  hex_line("[3] FDT", fdt, 3);
  hex_line("Image loaded at", load, 10);
}

void myron_screen_sec_entry(void)
{
  myron_screen_line("[6] SEC assembly entry; before VFP and stack init", 6);
}

void myron_screen_fault(u64 esr, u64 elr, u64 far)
{
  myron_screen_line("EARLY EXCEPTION - retain this screen", 7);
  hex_line("ESR", esr, 8);
  hex_line("ELR", elr, 9);
  hex_line("FAR", far, 10);
}
