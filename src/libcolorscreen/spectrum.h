#ifndef SPECTRUM_H
#define SPECTRUM_H
#include "include/color.h"
#include "include/spectrum-to-xyz.h"

namespace colorscreen
{
#define XSPECT_MAX_BANDS 77		/* Enough for 5nm from 380 to 760 */
#define CLAMP -65535
typedef struct {
	int    spec_n;				/* Number of spectral bands, 0 if not valid */
	luminosity_t spec_wl_short;		/* First reading wavelength in nm (shortest) */
	luminosity_t spec_wl_long;		/* Last reading wavelength in nm (longest) */
	luminosity_t norm;			/* Normalising scale value, ie. 1, 100 etc. */
	luminosity_t spec[XSPECT_MAX_BANDS];    /* Spectral value, shortest to longest */
} xspect;
struct spectra_entry
{
  float wavelength;
  float transmitance;
};


/* Output gnuplottable data.  */
void print_transmitance_spectrum (FILE * out, const spectrum spec, int start = SPECTRUM_START, int end = SPECTRUM_END);
void print_absorbance_spectrum (FILE * out, const spectrum spec, int start = SPECTRUM_START, int end = SPECTRUM_END);
void compute_spectrum (spectrum s, luminosity_t start, luminosity_t end, int size,
		       const luminosity_t data[], bool absorbance, luminosity_t norm = 1, bool limit_range = true, bool clamp = false);
void compute_spectrum (spectrum s, const xspect &in);
void compute_spectrum (spectrum s, int size, const spectra_entry * data, bool absorbance = false, luminosity_t norm = 1, luminosity_t min_transmitance = -1, luminosity_t max_transmitance = -1, bool clamp = false);

inline void
compute_log_sensitivity (spectrum s, int size, const spectra_entry * data)
{
  compute_spectrum (s, size, data, false, 1, -1, -1, true);
}
void log_sensitivity_to_reversal_transmitance(spectrum response);
void log2_sensitivity_to_reversal_transmitance(spectrum response);
void set_illuminant_to (spectrum backlight, enum spectrum_dyes_to_xyz::illuminants il, luminosity_t temperature = 5400);
void set_response (spectrum film_response, spectrum_dyes_to_xyz::responses type);
}
#endif
