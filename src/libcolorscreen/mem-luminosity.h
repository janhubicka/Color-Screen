#ifndef MEM_LUMINOSITY_H
#define MEM_LUMINOSITY_H
#include "config.h"
namespace colorscreen
{
#ifndef COLORSCREEN_16BIT_FLOAT
typedef float mem_luminosity_t;
#else
/* mem_luminosity_t is used for very large temporary data.  */

#if 0
#ifdef __x86_64__
typedef _Float16 mem_luminosity_t;
#elif __aarch64__
typedef __fp16 mem_luminosity_t;
#else
#warning 16bit data type not available
typedef float mem_luminosity_t;
#endif

#else

/* Open coded implementation seems to work faster than Float16 on x86_64 so far.
   based on https://www.researchgate.net/publication/362275548_Accuracy_and_performance_of_the_lattice_Boltzmann_method_with_64-bit_32-bit_and_customized_16-bit_number_formats  */
struct mem_luminosity_t
{
  uint16_t x;

  constexpr mem_luminosity_t ()
  : x (0)
  { }

  /* Testsuite reproduces undefined shift, but it is multiplied by 0.  */
  __attribute__((no_sanitize("undefined")))
  always_inline_attr inline
  mem_luminosity_t (float y)
  {
    const unsigned int b = as_uint(y)+0x00001000; // round-to-nearest-even: add last bit after truncated mantissa
    const unsigned int e = (b&0x7F800000)>>23; // exponent
    const unsigned int m = b&0x007FFFFF; // mantissa; in line below: 0x007FF000 = 0x00800000-0x00001000 = decimal indicator flag - initial rounding
    x = (b&0x80000000)>>16 | (e>112)*((((e-112)<<10)&0x7C00)|m>>13) | ((e<113)&(e>101))*((((0x007FF000+m)>>(125-e))+1)>>1) | (e>143)*0x7FFF; // sign : normalized : denormalized : saturate
  }
  __attribute__((no_sanitize("undefined")))
  always_inline_attr inline const_attr
  operator float () const
  {
    const unsigned int e = (x&0x7C00)>>10; // exponent
    const unsigned int m = (x&0x03FF)<<13; // mantissa
    const unsigned int v = as_uint((float)m)>>23; // evil log2 bit hack to count leading zeros in denormalized format
    return as_float((x&0x8000)<<16 | (e!=0)*((e+112)<<23|m) | ((e==0)&(m!=0))*((v-37)<<23|((m<<(150-v))&0x007FE000))); // sign : normalized : denormalized
  }
  always_inline_attr inline const_attr
  operator double () const
  {
    return (double)(float)*this;
  }
private:
  static inline always_inline_attr const_attr
  unsigned int as_uint(const float x) {
    union u {float f; uint32_t i;} v;
    v.f = x;
    return v.i;
  }
  static inline always_inline_attr const_attr
  float as_float(const unsigned int x) {
    union u {float f; uint32_t i;} v;
    v.i = x;
    return v.f;
  }
};
#endif
#endif
/* Datastructure used to store information about dye luminosities.  */
struct mem_rgbdata
{
  mem_luminosity_t red, green, blue;
  mem_rgbdata(rgbdata c)
  : red ((mem_luminosity_t)c.red), green ((mem_luminosity_t)c.green), blue ((mem_luminosity_t)c.blue)
  { }
  constexpr mem_rgbdata()
  { }
};
inline rgbdata::rgbdata (mem_rgbdata color)
: red (color.red), green (color.green), blue (color.blue)
{
}
}
#endif
