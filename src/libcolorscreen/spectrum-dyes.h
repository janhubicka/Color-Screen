#ifndef SPECTRUM_DYES_H
#define SPECTRUM_DYES_H
#include "include/color.h"
#include "include/spectrum-to-xyz.h"
namespace colorscreen
{
bool set_dyes_to (spectrum red, spectrum green, spectrum blue, spectrum cyan, spectrum magenta, spectrum yellow, enum spectrum_dyes_to_xyz::dyes dyes);
void set_synthetic_dufay_red (spectrum red, luminosity_t d1, luminosity_t d2);
void set_synthetic_dufay_green (spectrum green, luminosity_t d1, luminosity_t d2);
void set_synthetic_dufay_blue (spectrum blue, luminosity_t d1, luminosity_t d2);
}
#endif
