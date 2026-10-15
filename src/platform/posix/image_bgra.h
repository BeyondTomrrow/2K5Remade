/* PNG/JPEG/BMP/TGA decode to 32bpp BGRA and PNG encode, for POSIX builds
 * (Windows uses WIC). Paths may use backslashes. DDS is not supported. */
#ifndef NFL2K5_IMAGE_BGRA_H
#define NFL2K5_IMAGE_BGRA_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t *nfl2k5_image_load_bgra(const char *path, uint32_t *w, uint32_t *h);
void nfl2k5_image_free(void *px);
int nfl2k5_image_write_png_bgra(const char *path, const uint32_t *px, uint32_t w, uint32_t h);
#ifdef __cplusplus
}
#endif
#endif
