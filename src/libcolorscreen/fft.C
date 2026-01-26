#include "fft.h"

namespace colorscreen
{
/* FFTW execute is thread safe. Everything else is not.  */
std::mutex fft_lock;
}
