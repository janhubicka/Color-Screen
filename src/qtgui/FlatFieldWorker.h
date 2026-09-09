#pragma once

#include <QString>
#include <memory>

#include "../libcolorscreen/include/backlight-correction-parameters.h"
#include "../libcolorscreen/include/imagedata.h"
#include "../libcolorscreen/include/progress-info.h"

/** Final result of one flat-field reference analysis. */
struct FlatFieldAnalysisResult {
  bool success = false;
  bool cancelled = false;
  QString error;
  std::shared_ptr<colorscreen::backlight_correction_parameters> correction;
};

/** Synchronous flat-field analysis helper intended for a background thread. */
class FlatFieldWorker {
public:
  /** Analyze WHITEFILE and optional BLACKFILE using the captured input setup. */
  static FlatFieldAnalysisResult analyze(
      const QString &whiteFile, const QString &blackFile,
      colorscreen::luminosity_t gamma,
      colorscreen::image_data::demosaicing_t demosaic,
      colorscreen::progress_info *progress);
};
