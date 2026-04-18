#include "include/render-parameters.h"
#include "out-color-adjustments.h"
namespace colorscreen
{
/* Out lookup table (translating linear values to output gamma) cache
   Output lookup table takes linear r,g,b values in range 0...65536
   and outputs r,g,b values in sRGB gamma curve in range 0...maxval.  */

precomputed_function<luminosity_t> *
get_new_out_lookup_table (struct out_lookup_table_params &p, progress_info *)
{
  std::vector<luminosity_t> lookup_table (
      out_color_adjustments::out_lookup_table_size);
  luminosity_t gamma = p.output_gamma;
  if (gamma != -1)
    gamma = std::clamp (gamma, (luminosity_t)0.0001, (luminosity_t)100.0);
  int maxval = p.maxval;
  luminosity_t mul
      = 1 / (luminosity_t)(out_color_adjustments::out_lookup_table_size - 1);

  for (int i = 0; i < (int)out_color_adjustments::out_lookup_table_size; i++)
    lookup_table[i]
        = invert_gamma (i * mul, gamma)
              * maxval
          + (luminosity_t)0.5;

  return new precomputed_function<luminosity_t> (
      0, 1, lookup_table.data (),
      out_color_adjustments::out_lookup_table_size);
}
static lru_cache<out_lookup_table_params, precomputed_function<luminosity_t>,
                 precomputed_function<luminosity_t> *,
                 get_new_out_lookup_table, 4>
    out_lookup_table_cache ("out lookup tables");

bool
out_color_adjustments::precompute (
    render_parameters &m_params,
    const image_data *m_img, /* Only used when producing original profile.  */
    bool normalized_patches, rgbdata patch_proportions,
    progress_info *progress)
{
  m_output_gamma = m_params.output_gamma;
  m_gammut_warning = m_params.gammut_warning;

  out_lookup_table_params out_par
      = { m_dst_maxval, m_params.output_gamma };
  m_out_lookup_table = out_lookup_table_cache.get_cached (out_par, progress);

  color_matrix color;

  if (m_params.output_profile != render_parameters::output_profile_original)
    {
      /* See if we want to do some output adjustments in pro photo RGB space.
         These should closely follow what DNG reference recommends.  */
      bool do_pro_photo
          = m_params.output_tone_curve != tone_curve::tone_curve_linear;
      // printf ("Prophoto %i\n", do_pro_photo);
      /* Matrix converting dyes to XYZ.  */
      color = m_params.get_rgb_to_xyz_matrix (
          m_img, normalized_patches, patch_proportions,
          do_pro_photo ? d50_white : d65_white);

      // printf ("To xyz\n");
      // color.print (stdout);

      /* For subtractive processes we do post-processing in separate matrix
         after spectrum dyes to xyz are applied.  */
      if (m_params.color_model == render_parameters::color_model_kodachrome25)
        {
          m_spectrum_dyes_to_xyz = std::make_unique<spectrum_dyes_to_xyz> ();
          m_spectrum_dyes_to_xyz->set_film_response (
              spectrum_dyes_to_xyz::response_even);
          m_spectrum_dyes_to_xyz->set_dyes (
              spectrum_dyes_to_xyz::kodachrome_25_sensitivity);
          m_spectrum_dyes_to_xyz->set_backlight (
              spectrum_dyes_to_xyz::il_D, m_params.backlight_temperature);

          spectrum_dyes_to_xyz dufay;
          dufay.set_film_response (
              spectrum_dyes_to_xyz::dufaycolor_harrison_horner_emulsion_cut);
          dufay.set_dyes (
              spectrum_dyes_to_xyz::
                  dufaycolor_harrison_horner /*dufaycolor_color_cinematography*/);
          dufay.set_backlight (spectrum_dyes_to_xyz::il_D,
                               m_params.backlight_temperature);
          // dufay.set_characteristic_curve
          // (spectrum_dyes_to_xyz::linear_reversal_curve);

          color = color
                  * m_spectrum_dyes_to_xyz->process_transformation_matrix (
                      &dufay);
          m_spectrum_dyes_to_xyz->set_characteristic_curve (
              spectrum_dyes_to_xyz::kodachrome25_curve);

          saturation_matrix m (m_params.saturation);
          m_color_matrix2 = (bradford_whitepoint_adaptation_matrix (
                                 m_spectrum_dyes_to_xyz->whitepoint_xyz (),
                                 do_pro_photo ? d50_white : d65_white)
                             * m)
                            * 1.5;
          if (m_params.output_profile == render_parameters::output_profile_xyz)
            ;
          else if (do_pro_photo)
            {
              xyz_pro_photo_rgb_matrix m;
              m_color_matrix2 = m * m_color_matrix2;
              m_tone_curve
                  = std::make_unique<tone_curve> (m_params.output_tone_curve);
              assert (!m_tone_curve->is_linear ());
            }
          else
            {
              xyz_srgb_matrix m;
              m_color_matrix2 = m * m_color_matrix2;
            }
        }
      else
        {
          if (m_params.output_profile == render_parameters::output_profile_xyz)
            ;
          else if (do_pro_photo)
            {
              xyz_pro_photo_rgb_matrix m;
              color = m * color;
              m_tone_curve
                  = std::make_unique<tone_curve> (m_params.output_tone_curve);
              assert (!m_tone_curve->is_linear ());
            }
          else
            {
              xyz_srgb_matrix m;
              color = m * color;
            }
        }
    }
  else
    color = m_params.get_rgb_adjustment_matrix (normalized_patches,
                                                patch_proportions);
  m_color_matrix = color;
  return true;
}
}
