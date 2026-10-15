/* Single compilation unit for the vendored stb headers (POSIX builds). */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#include "../../third_party/stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../third_party/stb/stb_image_write.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "image_bgra.h"

static void path_fix(const char *in, char *out, size_t cap)
{
    size_t i;
    for (i = 0; in[i] && i + 1 < cap; i++) out[i] = in[i] == '\\' ? '/' : in[i];
    out[i] = 0;
}

uint32_t *nfl2k5_image_load_bgra(const char *path, uint32_t *w, uint32_t *h)
{
    char p[4096];
    int iw = 0, ih = 0, n = 0;
    unsigned char *rgba;
    path_fix(path, p, sizeof p);
    rgba = stbi_load(p, &iw, &ih, &n, 4);
    if (!rgba) return NULL;
    for (size_t i = 0, c = (size_t)iw * ih; i < c; i++) {     /* RGBA -> BGRA */
        unsigned char t = rgba[i * 4];
        rgba[i * 4] = rgba[i * 4 + 2];
        rgba[i * 4 + 2] = t;
    }
    *w = (uint32_t)iw;
    *h = (uint32_t)ih;
    return (uint32_t *)rgba;
}

void nfl2k5_image_free(void *px) { stbi_image_free(px); }

int nfl2k5_image_write_png_bgra(const char *path, const uint32_t *px, uint32_t w, uint32_t h)
{
    char p[4096];
    size_t c = (size_t)w * h;
    unsigned char *rgba = (unsigned char *)malloc(c * 4);
    int ok;
    if (!rgba) return 0;
    memcpy(rgba, px, c * 4);
    for (size_t i = 0; i < c; i++) {
        unsigned char t = rgba[i * 4];
        rgba[i * 4] = rgba[i * 4 + 2];
        rgba[i * 4 + 2] = t;
    }
    path_fix(path, p, sizeof p);
    ok = stbi_write_png(p, (int)w, (int)h, 4, rgba, (int)w * 4);
    free(rgba);
    return ok != 0;
}
