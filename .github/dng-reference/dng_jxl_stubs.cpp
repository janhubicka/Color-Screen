// Link-only stubs for JPEG XL entry points referenced by generic DNG SDK
// translation units. The reference probe exercises only WarpRectilinear and
// never calls any of these functions.

#include "dng_exceptions.h"
#include "dng_image.h"
#include "dng_info.h"
#include "dng_jxl.h"
#include "dng_pixel_buffer.h"
#include "dng_stream.h"
#include "dng_string.h"
#include "dng_tag_values.h"

bool
SupportsJXL (const dng_image &)
{
  return false;
}

bool
ParseJXL (dng_host &, dng_stream &, dng_info &, bool, bool)
{
  ThrowProgramError ();
  return false;
}

dng_jxl_decoder::~dng_jxl_decoder ()
{
}

void
dng_jxl_decoder::Decode (dng_host &, dng_stream &)
{
  ThrowProgramError ();
}

void
dng_jxl_decoder::ProcessExifBox (dng_host &, const std::vector<uint8> &)
{
  ThrowProgramError ();
}

void
dng_jxl_decoder::ProcessXMPBox (dng_host &, const std::vector<uint8> &)
{
  ThrowProgramError ();
}

void
dng_jxl_decoder::ProcessBox (dng_host &, const dng_string &,
                             const std::vector<uint8> &)
{
  ThrowProgramError ();
}

void
PreviewColorSpaceToJXLEncoding (PreviewColorSpaceEnum, uint32,
                                dng_jxl_color_space_info &)
{
  ThrowProgramError ();
}

void
EncodeJXL_Tile (dng_host &, dng_stream &, const dng_pixel_buffer &,
                const dng_jxl_color_space_info &,
                const dng_jxl_encode_settings &)
{
  ThrowProgramError ();
}

void
EncodeJXL_Tile (dng_host &, dng_stream &, const dng_image &,
                const dng_jxl_color_space_info &,
                const dng_jxl_encode_settings &)
{
  ThrowProgramError ();
}

void
EncodeJXL_Container (dng_host &, dng_stream &, const dng_image &,
                     const dng_jxl_encode_settings &,
                     const dng_jxl_color_space_info &, const dng_metadata *,
                     bool, bool, bool, const dng_bmff_box_list *)
{
  ThrowProgramError ();
}

void
EncodeJXL_Container (dng_host &, dng_stream &, const dng_pixel_buffer &,
                     const dng_jxl_encode_settings &,
                     const dng_jxl_color_space_info &, const dng_metadata *,
                     bool, bool, bool, const dng_bmff_box_list *)
{
  ThrowProgramError ();
}
