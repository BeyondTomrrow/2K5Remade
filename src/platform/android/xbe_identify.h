/* Tells a modded copy of the game from another release of it, from the bytes
 * of its default.xbe. No dependencies, so it can be tested on any host.
 *
 * Retail USA ESPN NFL 2K5 has 22 sections. 2K5 Mod Studio adds its own
 * (.AS0000 ...) and keeps the title id and the region, so a SOFTDRINK disc
 * looks like the USA game with more sections. Everything in the header comes
 * from a file the player chose: every offset is checked. */
#ifndef NFL2K5_XBE_IDENTIFY_H
#define NFL2K5_XBE_IDENTIFY_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define NFL2K5_XBE_TITLE_ID   0x53450030u
#define NFL2K5_XBE_REGION_USA 0x00000001u
#define NFL2K5_XBE_SECTIONS   22u

static uint32_t nfl2k5_xbe_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* 1: an executable of the USA game with sections added by a mod tool. Call it
 * on an executable that already failed the retail hash check. */
static int nfl2k5_xbe_is_modded(const uint8_t *x, size_t len)
{
    uint32_t base, cert, sections;
    if (len < 0x180 || memcmp(x, "XBEH", 4)) return 0;
    base = nfl2k5_xbe_rd32(x + 0x104);
    cert = nfl2k5_xbe_rd32(x + 0x118);
    sections = nfl2k5_xbe_rd32(x + 0x11C);
    if (cert < base || (uint64_t)(cert - base) + 0xA4 > len) return 0;
    return nfl2k5_xbe_rd32(x + (cert - base) + 8) == NFL2K5_XBE_TITLE_ID &&
           (nfl2k5_xbe_rd32(x + (cert - base) + 0xA0) & NFL2K5_XBE_REGION_USA) &&
           sections > NFL2K5_XBE_SECTIONS;
}
#endif
