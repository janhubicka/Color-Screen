// Link-only stubs for JPEG XL entry points referenced by generic DNG SDK
// translation units. The reference probe exercises only WarpRectilinear and
// never calls any of these functions.

#include "dng_tag_values.h"
#include "dng_jxl.h"
#include "dng_exceptions.h"
#include "dng_image.h"
#include "dng_image_writer.h"
#include "dng_info.h"
#include "dng_metadata.h"
#include "dng_stream.h"

bool SupportsJXL (const dng_image &)
{
	return false;
}

void ParseJXL (dng_host &, dng_stream &, dng_info &, bool, bool)
{
	ThrowProgramError ();
}

void dng_jxl_decoder::Decode (dng_host &, dng_stream &)
{
	ThrowProgramError ();
}

void PreviewColorSpaceToJXLEncoding (PreviewColorSpaceEnum,
									 uint32,
									 dng_jxl_color_space_info &)
{
	ThrowProgramError ();
}

void EncodeJXL_Tile (dng_host &,
					 dng_stream &,
					 const dng_pixel_buffer &,
					 const dng_jxl_color_space_info &,
					 const dng_jxl_encode_settings &)
{
	ThrowProgramError ();
}

void EncodeJXL_Container (dng_host &,
						  dng_stream &,
						  const dng_pixel_buffer &,
						  const dng_jxl_encode_settings &,
						  const dng_jxl_color_space_info &,
						  const dng_metadata *,
						  bool,
						  bool,
						  bool,
						  const std::vector<std::shared_ptr<dng_bmff_box>> *)
{
	ThrowProgramError ();
}
