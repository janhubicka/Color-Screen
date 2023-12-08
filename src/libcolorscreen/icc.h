#include "include/color.h"
size_t create_profile (const char *desc, xyz r, xyz g, xyz b, luminosity_t gamma, void **buffer);
size_t create_wide_gammut_rgb_profile (void **buffer);
size_t create_pro_photo_rgb_profile (void **buffer);
