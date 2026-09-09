#include "FlatFieldWorker.h"

#include <QDebug>

/** Analyze flat-field reference images and return one final correction result. */
FlatFieldAnalysisResult FlatFieldWorker::analyze(
    const QString &whiteFile, const QString &blackFile,
    colorscreen::luminosity_t gamma,
    colorscreen::image_data::demosaicing_t demosaic,
    colorscreen::progress_info *progress) {
  FlatFieldAnalysisResult result;
  const char *error = nullptr;

  if (progress)
    progress->set_task("Flat Field", 1);
  colorscreen::sub_task task(progress);

  colorscreen::image_data whiteScan;
  {
    if (progress)
      progress->set_task("Loading white reference", 1);
    colorscreen::sub_task loadTask(progress);
    if (!whiteScan.load(whiteFile.toLocal8Bit().constData(), false, &error,
                        progress, demosaic)) {
      result.cancelled = progress && progress->pool_cancel();
      if (!result.cancelled) {
        result.error = error ? QString::fromUtf8(error)
                             : QStringLiteral("Could not load white reference");
        qWarning() << "Failed to load white reference:" << result.error;
      }
      return result;
    }
  }

  if (progress && progress->pool_cancel()) {
    result.cancelled = true;
    return result;
  }

  std::unique_ptr<colorscreen::image_data> blackScan;
  if (!blackFile.isEmpty()) {
    if (progress)
      progress->set_task("Loading black reference", 1);
    colorscreen::sub_task loadTask(progress);
    blackScan = std::make_unique<colorscreen::image_data>();
    // Black references usually do not contain enough signal for corrected
    // Bayer scaling weights. Preserve the existing monochromatic fallback.
    auto blackDemosaic = demosaic;
    if (blackDemosaic ==
        colorscreen::image_data::demosaic_monochromatic_bayer_corrected)
      blackDemosaic = colorscreen::image_data::demosaic_monochromatic;

    if (!blackScan->load(blackFile.toLocal8Bit().constData(), false, &error,
                         progress, blackDemosaic)) {
      result.cancelled = progress && progress->pool_cancel();
      if (!result.cancelled) {
        result.error = error ? QString::fromUtf8(error)
                             : QStringLiteral("Could not load black reference");
        qWarning() << "Failed to load black reference:" << result.error;
      }
      return result;
    }
  }

  if (progress && progress->pool_cancel()) {
    result.cancelled = true;
    return result;
  }

  if (progress)
    progress->set_task("Analyzing flat field", 1);
  colorscreen::sub_task analysisTask(progress);
  result.correction = colorscreen::backlight_correction_parameters::analyze_scan(
      whiteScan, gamma, blackScan.get());

  if (progress && progress->pool_cancel()) {
    result.cancelled = true;
    result.correction.reset();
    return result;
  }
  if (!result.correction) {
    result.error = QStringLiteral("Flat-field correction could not be fitted");
    qWarning() << result.error;
    return result;
  }

  result.success = true;
  return result;
}
