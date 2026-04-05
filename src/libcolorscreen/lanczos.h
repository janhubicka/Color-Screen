namespace
{
inline double
sinc (double x)
{
  if (x == 0)
    return 1.0;
  x *= M_PI;
  return std::sin (x) / x;
}

/* a = 1	Lanczos-1	Mathematically identical to a Sinc filter; very blurry.
   a = 2	Lanczos-2	Good balance; cleaner than Bicubic but less sharp than Lanczos-3.
   a = 3	Lanczos-3	The Gold Standard. High sharpness and excellent detail preservation.
   a = 4+	Lanczos-4+	Theoretically sharper, but often introduces
   				excessive "ringing" (ghost edges) that can ruin the image.  */

inline double
lanczos_kernel (double x, int a = 3)
{
  if (std::abs (x) >= a)
    return 0.0;
  return sinc (x) * sinc (x / a);
}
}
