#include <lcms2.h>
#include "icc.h"
#include "include/color.h"

static const cmsCIEXYZ d65 = {0.95045471, 1.00000000, 1.08905029};
//D65 (sRGB, AdobeRGB, Rec2020)
static const cmsCIExyY D65xyY = {0.312700492, 0.329000939, 1.0};
//D60
static const cmsCIExyY d60 = {0.32168, 0.33767, 1.0};

//D50 (ProPhoto RGB)
static const cmsCIExyY D50xyY = {0.3457, 0.3585, 1.0};

// D65:
static const cmsCIExyYTRIPLE sRGB_Primaries = {
  {0.6400, 0.3300, 1.0}, // red
  {0.3000, 0.6000, 1.0}, // green
  {0.1500, 0.0600, 1.0}  // blue
};

static cmsHPROFILE create_lcms_profile(const char *desc,
                                       const cmsCIExyY *whitepoint,
                                       const cmsCIExyYTRIPLE *primaries,
				       cmsToneCurve *trc)
{
  cmsMLU *mlu1 = cmsMLUalloc(NULL, 1);
  cmsMLU *mlu2 = cmsMLUalloc(NULL, 1);
  cmsMLU *mlu3 = cmsMLUalloc(NULL, 1);


  cmsToneCurve *out_curves[3] = { trc, trc, trc };
  cmsHPROFILE profile = cmsCreateRGBProfile(whitepoint, primaries, out_curves);

  cmsSetProfileVersion(profile, 2.4);

  cmsSetHeaderFlags(profile, cmsEmbeddedProfileTrue);

  cmsMLUsetASCII(mlu1, "en", "US", "Public Domain");
  cmsWriteTag(profile, cmsSigCopyrightTag, mlu1);

  cmsMLUsetASCII(mlu2, "en", "US", desc);
  cmsWriteTag(profile, cmsSigProfileDescriptionTag, mlu2);

  cmsMLUsetASCII(mlu3, "en", "US", "ColorScreen");
  cmsWriteTag(profile, cmsSigDeviceMfgDescTag, mlu3);

  cmsMLUfree(mlu1);
  cmsMLUfree(mlu2);
  cmsMLUfree(mlu3);

  return profile;
}
size_t
create_profile (const char *desc, xyz r, xyz g, xyz b, luminosity_t gamma, void **buffer)
{
  cmsCIExyYTRIPLE primaries;
  cmsCIExyY whitepoint;
  float x, y, Y;
  cmsFloat64Number srgb_parameters[5] =
    { 2.4, 1.0 / 1.055,  0.055 / 1.055, 1.0 / 12.92, 0.04045 };
  cmsToneCurve *trc = gamma == -1 ? cmsBuildParametricToneCurve(NULL, 4, srgb_parameters): cmsBuildGamma(NULL, 1);
  xyz_to_xyY (r.x, r.y, r.z, &x, &y, &Y);
  primaries.Red.x = x;
  primaries.Red.y = y;
  primaries.Red.Y = Y;
  //fprintf (stderr, "Red XYZ: %f %f %f, xyY %f %f %f\n", r.x, r.y, r.z, x, y, Y);
  xyz_to_xyY (g.x, g.y, g.z, &x, &y, &Y);
  primaries.Green.x = x;
  primaries.Green.y = y;
  primaries.Green.Y = Y;
  //fprintf (stderr, "Green XYZ: %f %f %f, xyY %f %f %f\n", g.x, g.y, g.z, x, y, Y);
  xyz_to_xyY (b.x, b.y, b.z, &x, &y, &Y);
  primaries.Blue.x = x;
  primaries.Blue.y = y;
  primaries.Blue.Y = Y;
  //fprintf (stderr, "Blue XYZ: %f %f %f, xyY %f %f %f\n", b.x, b.y, b.z, x, y, Y);
  xyz_to_xyY (r.x + b.x + g.x, r.y + b.y + g.y, r.z + g.z + b.z, &x, &y, &Y);
  whitepoint.x = x;
  whitepoint.y = y;
  whitepoint.Y = Y;
  //fprintf (stderr, "Whitepoint XYZ: %f %f %f, xyY %f %f %f\n", r.x + b.x + g.x, r.y + b.y + g.y, r.z + g.z + b.z, x, y, Y);
  cmsHPROFILE prof = create_lcms_profile (desc, &whitepoint, &primaries, trc);
  cmsUInt32Number len;
  cmsSaveProfileToMem (prof, NULL, &len);
  *buffer = malloc (len);
  if (buffer)
    cmsSaveProfileToMem (prof, *buffer, &len);
  //cmsFreeProfile (prof);
  //TODO: Leak
  cmsFreeToneCurve (trc);
  return len;
}
			    
