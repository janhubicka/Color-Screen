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
    m_progress->set_task("Flat Field", 1);
  }
  colorscreen::image_data whiteScan;
  colorscreen::sub_task task (m_progress.get ());  /* Keep so tasks are nsted. */
  {
    if (m_progress) {
      m_progress->set_task("Loading white reference", 1);
    }
    colorscreen::sub_task task (m_progress.get ());  /* Keep so tasks are nsted. */

    if (!whiteScan.load(m_whiteFile.toLocal8Bit().constData(), false, &error, m_progress.get(), m_demosaic)) {
      qWarning() << "Failed to load white reference:" << error;
      emit finished(false, nullptr);
      return;
    }

    if (m_progress && m_progress->cancelled()) {
      emit finished(false, nullptr);
      return;
    }
  }

  std::unique_ptr<colorscreen::image_data> blackScan;
  if (!m_blackFile.isEmpty()) {
    if (m_progress) {
      m_progress->set_task("Loading black reference", 1);
    }
    colorscreen::sub_task task (m_progress.get ());  /* Keep so tasks are nsted. */
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
  colorscreen::sub_task task2 (m_progress.get ());  /* Keep so tasks are nsted. */

  std::shared_ptr<colorscreen::backlight_correction_parameters> cor = 
      colorscreen::backlight_correction_parameters::analyze_scan(whiteScan, m_gamma, blackScan.get());

  if (m_progress && m_progress->cancelled()) {
    emit finished(false, nullptr);
    return;
  }

  if (!cor) {
    qWarning() << "Flat field analysis failed";
    emit finished(false, nullptr);
    return;
  }

  emit finished(true, cor);
}
