#include "spectrum.h"
#include "include/render.h"
/* Output gnuplottable data.  */
void
print_transmitance_spectrum (FILE * out, const spectrum spec, int start, int end)
{
  for (int i = 0; i < SPECTRUM_SIZE; i++)
    if (i * SPECTRUM_STEP + SPECTRUM_START >= start && i * SPECTRUM_STEP + SPECTRUM_START <= end)
      {
	fprintf (out, "%i %f\n", i * SPECTRUM_STEP + SPECTRUM_START, spec[i]);
	//assert (spec[i] > -100 && spec [i] < 100);
      }
}

void
print_absorbance_spectrum (FILE * out, const spectrum spec, int start, int end)
{
  for (int i = 0; i < SPECTRUM_SIZE; i++)
    if (i * SPECTRUM_STEP + SPECTRUM_START >= start && i * SPECTRUM_STEP + SPECTRUM_START <= end)
    fprintf (out, "%i %f\n", i * SPECTRUM_STEP + SPECTRUM_START,
	     transmitance_to_absorbance (spec[i]));
}

/* Process chart with regular step to a spectrum.
   cubically interpolate for missing data.  */

void
compute_spectrum (spectrum s, luminosity_t start, luminosity_t end, int size,
		  const luminosity_t data[], bool absorbance, luminosity_t norm, bool limit_range, bool clamp)
{
  luminosity_t step = (end - start) / (luminosity_t) (size - 1);
  luminosity_t repnorm = 1 / norm;
  //printf ("start %f end %f step %f size %i\n",start,end,step,size);
  for (int i = 0; i < SPECTRUM_SIZE; i++)
    {
      int p = SPECTRUM_START + i * SPECTRUM_STEP;
      float sp = (p - start) / (float) step;
      int ri;
      float rp = my_modf (sp, &ri);


      /* If out of range continue interpolating linearly.
       * ??? maybe this gets to too extreme values towards the end.  */
      if (ri < 1)
	{
	  rp += ri - 1;
	  ri = 1;
	  s[i] = data[1] + rp * (data[1] - data[0]);
	  /* Just clamp when data are missing.  */
	  if (clamp && p < start)
	    {
	      s[i] = CLAMP;
	      continue;
	    }
	}
      else if (ri >= size - 2)
	{
	  rp += ri - size + 2;
	  ri = size - 2;
	  s[i] = data[size - 2] + rp * (data[size - 1] - data[size - 2]);
	  if (clamp && p > end)
	    {
	      s[i] = CLAMP;
	      continue;
	    }
	}
      else
	s[i] =
	  cubic_interpolate (data[ri - 1], data[ri], data[ri + 1],
			     data[ri + 2], rp);
      if (absorbance)
	s[i] = absorbance_to_transmitance (s[i] * repnorm);
      else
	s[i] *= repnorm;
      if (limit_range && s[i] < 0)
	s[i] = 0;
      if (limit_range && s[i] > 1)
	s[i] = 1;
    }
}

void
compute_spectrum (spectrum s, const xspect &in)
{
  compute_spectrum (s, (luminosity_t)in.spec_wl_short, (luminosity_t)in.spec_wl_long, in.spec_n, in.spec, false, (luminosity_t)in.norm, false);
}

/* Process absorbance chart with regular step to a spectrum.
   Since I am lazy use linear interpolation.  */

void
compute_spectrum (spectrum s, int size, const spectra_entry * data, bool absorbance, luminosity_t norm, luminosity_t min_transmitance, luminosity_t max_transmitance, bool clamp)
{
  /* Check that data are linearly ordered.  */
  luminosity_t repnorm = 1 / norm;
  for (int i = 1; i < size; i++)
    if (data[i-1].wavelength > data[i].wavelength)
      {
	printf ("%f %f\n", data[i-1].wavelength, data[i].wavelength);
        abort ();
      }
  for (int i = 0; i < SPECTRUM_SIZE; i++)
    {
      int p = SPECTRUM_START + i * SPECTRUM_STEP;
      int first;
      if (data[0].wavelength > p)
	{
	  if (clamp)
	    {
	      s[i] = CLAMP;
	      continue;
	    }
	  first = 0;
	  //s[i]=0;continue;
	}
      else if (data[size - 1].wavelength < p)
	{
	  first = size - 2;
	  //s[i]=0;continue;
	  if (clamp)
	    {
	      s[i] = CLAMP;
	      continue;
	    }
	}
      else
	for (first = 0; data[first + 1].wavelength < p; first++)
	  ;
      float pos =
	(p - data[first].wavelength) / (data[first + 1].wavelength -
					data[first].wavelength);
      s[i] =
	(data[first].transmitance + (data[first + 1].transmitance -
				    data[first].transmitance) * pos) * repnorm;
      if (absorbance)
	s[i] = absorbance_to_transmitance (s[i]);
      if (max_transmitance > 0 && s[i] > max_transmitance * repnorm)
        s[i] = max_transmitance * repnorm;
      if (min_transmitance >= 0 && s[i] < min_transmitance * repnorm)
        s[i] = min_transmitance * repnorm;
    }
}

/* Wedge histograms seems to be log sensitivity.  Lets assume that it is base 10 logarithm.
   Sensitivity is the reciprocal of time needed to obtain given density.  For reversal film
   I assume that it is reciprocal of time needed to get maximal brightness.  So to translate
   this to linear data and assuming that iflm is linear it should be 1/time, where time is
   10^{1/response}.  */

void
log_sensitivity_to_reversal_transmitance(spectrum response)
{
  for (int i = 0; i < SPECTRUM_SIZE; i++)
  {
    if (response[i] != CLAMP)
      //response[i] = 1/ (pow(10,1/response[i]));
      // We do flipping positive to negative by 1/density.  This should be done by
      // characteristic curve.
      response[i] = pow(10,response[i]);
    else
      response[i]=0;
  }
}
void
log2_sensitivity_to_reversal_transmitance(spectrum response)
{
  for (int i = 0; i < SPECTRUM_SIZE; i++)
  {
    if (response[i]>0)
      response[i] = pow(2,response[i]);
    else
      response[i]=0;
  }
}
