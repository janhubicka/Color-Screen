#include "FlatFieldWorker.h"
#include <QFile>
#include <QDebug>

FlatFieldWorker::FlatFieldWorker(
    QString whiteFile, QString blackFile,
    colorscreen::luminosity_t gamma,
    colorscreen::image_data::demosaicing_t demosaic,
    std::shared_ptr<colorscreen::progress_info> progress)
    : m_whiteFile(whiteFile), m_blackFile(blackFile),
      m_gamma(gamma), m_demosaic(demosaic), m_progress(progress) {
}

void FlatFieldWorker::run() {
  const char *error = nullptr;
  
  if (m_progress) {
    m_progress->set_task("Loading white reference", 100);
  }

  colorscreen::image_data whiteScan;
  if (!whiteScan.load(m_whiteFile.toLocal8Bit().constData(), false, &error, m_progress.get(), m_demosaic)) {
    qWarning() << "Failed to load white reference:" << error;
    emit finished(false, nullptr);
    return;
  }

  if (m_progress && m_progress->cancelled()) {
    emit finished(false, nullptr);
    return;
  }

  std::unique_ptr<colorscreen::image_data> blackScan;
  if (!m_blackFile.isEmpty()) {
    if (m_progress) {
      m_progress->set_task("Loading black reference", 100);
    }
    blackScan = std::make_unique<colorscreen::image_data>();
    // Black references usually don't have enough data for scaling weights, so use standard monochromatic for Bayer
    colorscreen::image_data::demosaicing_t blackDemosaic = m_demosaic;
    if (blackDemosaic == colorscreen::image_data::demosaic_monochromatic_bayer_corrected) {
      blackDemosaic = colorscreen::image_data::demosaic_monochromatic;
    }

    if (!blackScan->load(m_blackFile.toLocal8Bit().constData(), false, &error, m_progress.get(), blackDemosaic)) {
      qWarning() << "Failed to load black reference:" << error;
      emit finished(false, nullptr);
      return;
    }
  }

  if (m_progress && m_progress->cancelled()) {
    emit finished(false, nullptr);
    return;
  }

  if (m_progress) {
    m_progress->set_task("Analyzing flat field", 1);
  }

  colorscreen::backlight_correction_parameters *cor = 
      colorscreen::backlight_correction_parameters::analyze_scan(whiteScan, m_gamma, blackScan.get());

  if (m_progress && m_progress->cancelled()) {
    if (cor) delete cor;
    emit finished(false, nullptr);
    return;
  }

  if (!cor) {
    qWarning() << "Backlight analysis failed";
    emit finished(false, nullptr);
    return;
  }

  emit finished(true, std::shared_ptr<colorscreen::backlight_correction_parameters>(cor));
}
