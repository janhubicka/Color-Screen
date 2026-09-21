#include "MainWindow.h"
#include "../libcolorscreen/include/base.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "ColorScreenApplication.h"
#include "GeometryPanel.h"
#include "ImageWidget.h"
#include "InitialSetupGuideDialog.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSaveFile>
#include <QSettings>
#include <QStatusBar>
#include <QTextStream>
#include <QTimer>
#include <QUndoStack>

#include <algorithm>
#include <memory>
#include <utility>

namespace {
/** Return the application-level document manager when MainWindow is running
    inside the normal Color-Screen Qt application. */
ColorScreenApplication *documentApplication() {
  return dynamic_cast<ColorScreenApplication *>(QCoreApplication::instance());
}
} // namespace

/** Open a .par parameter file chosen by the user.
   Prompts for unsaved changes first, then resets all parameter structs to
   defaults before loading (load_csp merges into existing values, so a reset
   is needed for clean loading).  On success, re-initialises the image widget
   and renderer with new parameters, clears undo history, and refreshes the
   UI.  On error, restores the previous parameter values.  */
void MainWindow::onOpenParameters() {
  // Check for unsaved changes before loading new parameters
  if (!maybeSave()) {
    return;
  }

  QString fileName = QFileDialog::getOpenFileName(
      this, "Open Parameters", QString(), "Parameters (*.par);;All Files (*)");
  if (fileName.isEmpty())
    return;

  QTimer::singleShot(0, this, [this, fileName]() {
    if (loadParameterFile(fileName)) {
      statusBar()->showMessage(QString("Parameters loaded from %1").arg(fileName),
                               3000);
    }
  });
}

/** Save parameters to the current .par file.
   A weak auto-suggested filename is confirmed through Save As before writing.
   Saving is synchronous so closeEvent can reliably decide whether it is safe
   to close this particular document window.  */
void MainWindow::onSaveParameters() {
  if (m_currentParamsFile.isEmpty() || m_currentParamsFileIsWeak) {
    saveParametersAs();
    return;
  }
  saveParametersToFile(m_currentParamsFile);
}

/** Save parameters to a new .par file chosen by the user. */
void MainWindow::onSaveParametersAs() { saveParametersAs(); }

/** Write the current document parameters to FILENAME and mark them saved. */
bool MainWindow::saveParametersToFile(const QString &fileName) {
  const QString absoluteFileName = QFileInfo(fileName).absoluteFilePath();
  FILE *f = fopen(absoluteFileName.toUtf8().constData(), "wt");
  if (!f) {
    QMessageBox::critical(
        this, "Error",
        QString("Could not open file for writing: %1").arg(absoluteFileName));
    return false;
  }

  const bool hasRgb = m_scan && m_scan->has_rgb();
  bool saved = colorscreen::save_csp_with_profile_spots(
      f, &m_scrToImgParams, hasRgb ? &m_detectParams : nullptr, &m_rparams,
      &m_solverParams, m_profileSpots);
  if (fclose(f) != 0)
    saved = false;

  if (!saved) {
    QMessageBox::critical(this, "Error", "Failed to save parameters.");
    return false;
  }

  m_currentParamsFile = absoluteFileName;
  m_currentParamsFileIsWeak = false;
  m_recoveryDirty = false;
  addToRecentParams(absoluteFileName);
  if (m_undoStack)
    m_undoStack->setClean();
  updateWindowTitle();
  saveRecoveryState();

  statusBar()->showMessage(
      QString("Parameters saved to %1").arg(absoluteFileName), 3000);
  return true;
}

/** Ask for a parameter filename and save it before returning to the caller. */
bool MainWindow::saveParametersAs() {
  QString fileName = QFileDialog::getSaveFileName(
      this, "Save Parameters",
      m_currentParamsFile.isEmpty() ? QString() : m_currentParamsFile,
      "Parameters (*.par);;All Files (*)");
  if (fileName.isEmpty())
    return false;

  if (!fileName.endsWith(QLatin1String(".par"), Qt::CaseInsensitive))
    fileName += QStringLiteral(".par");
  return saveParametersToFile(fileName);
}

/** Show a multi-selection file dialog and open each image independently.
   The application may reuse this window only when it is untouched and empty;
   otherwise every selected image receives a new MainWindow.  Dispatch is
   deferred by one event-loop turn so KDE can dispose of KIO file-dialog jobs
   before an associated parameter prompt is shown.  */
void MainWindow::onOpenImage() {
  const QStringList fileNames = QFileDialog::getOpenFileNames(
      this, "Open Images", m_lastOpenDir,
      "Images (*.tif *.tiff *.jpg *.jpeg *.jp2 *.j2k *.jpc *.jpf *.jpx *.png "
      "*.raw *.dng *.iiq *.nef *.cr2 *.eip *.arw *.raf *.arq *.csprj);;All "
      "Files (*)");
  if (fileNames.isEmpty())
    return;

  m_lastOpenDir = QFileInfo(fileNames.constFirst()).absolutePath();
  const QPointer<MainWindow> guardedWindow(this);
  QTimer::singleShot(0, qApp, [guardedWindow, fileNames]() {
    if (!guardedWindow)
      return;
    if (ColorScreenApplication *application = documentApplication())
      application->openFiles(fileNames, guardedWindow);
    else
      guardedWindow->loadFile(fileNames.constFirst());
  });
}

/** Post-load initialisation after a new image has been opened.
   Updates the render mode menu, sets the scan reference on all background
   workers, feeds the image to NavigationView, shows/hides the Tiles tab
   based on stitch data, shows/hides Profile and ImageLayer tabs based on
   RGB availability, refreshes all panel state, and enables the Render
   action.  */
void MainWindow::onImageLoaded() {
  clearFocusAreaAnalysis();
  // Update UI components that depend on loaded image
  updateModeMenu();
  if (m_scan) {
    if (m_solverWorker)
      m_solverWorker->setScan(m_scan);
    if (m_colorOptimizerWorker)
      m_colorOptimizerWorker->setScan(m_scan);
    m_navigationView->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                               &m_detectParams);
    m_navigationView->setMinScale(m_imageWidget->getMinScale());
  }

  if (m_tilesPanel) {
    m_tilesPanel->updateForNewImage();
    bool hasStitch = (m_scan && m_scan->stitch);
    int tilesTabIndex = m_configTabs->indexOf(m_tilesPanel);
    if (tilesTabIndex >= 0) {
      m_configTabs->setTabVisible(tilesTabIndex, hasStitch);
    }
  }

  if (m_profilePanel) {
    int profileTabIndex = m_configTabs->indexOf(m_profilePanel);
    if (profileTabIndex >= 0) {
      const auto capture =
          m_scan ? m_rparams.get_capture_type(m_scan.get())
                 : colorscreen::render_parameters::capture_unknown;
      const bool profileApplicable =
          m_scan && m_scan->has_rgb() &&
          colorscreen::render_parameters::
              capture_supports_screen_detection_p(capture);
      m_configTabs->setTabVisible(profileTabIndex, profileApplicable);
    }
  }

  if (m_imageLayerPanel) {
    int layerTabIndex = m_configTabs->indexOf(m_imageLayerPanel);
    if (layerTabIndex >= 0) {
      m_configTabs->setTabVisible(layerTabIndex, m_scan && m_scan->has_rgb());
    }
  }

  // Refresh param values too
  applyState(getCurrentState());
  updateRegistrationActions();
  updateRegistrationGroupVisibility();
  m_renderAction->setEnabled(m_scan != nullptr);
}

/** Add a file path to the most-recently-used image files list.
   Moves it to the front, caps the list at MaxRecentFiles, rebuilds
   the menu, and persists to QSettings.  */
void MainWindow::addToRecentFiles(const QString &filePath) {
  // Another document may have updated the application-wide list since this
  // window was created.  Merge against the latest persisted value before
  // writing so independently finishing image loads cannot lose entries.
  QSettings settings;
  m_recentFiles = settings.value("recentFiles").toStringList();
  QString absolutePath = QFileInfo(filePath).absoluteFilePath();
  m_recentFiles.removeAll(absolutePath);
  m_recentFiles.prepend(absolutePath);

  while (m_recentFiles.size() > MaxRecentFiles)
    m_recentFiles.removeLast();

  settings.setValue("recentFiles", m_recentFiles);
  updateRecentFileActions();
}

/** Rebuild the "Open Recent" submenu from the m_recentFiles list.
   Adds a "Clear Recent Files" action at the bottom.  */
void MainWindow::updateRecentFileActions() {
  m_recentFilesMenu->clear();
  m_recentFileActions.clear();

  for (int i = 0; i < m_recentFiles.size(); ++i) {
    QString fileName = QFileInfo(m_recentFiles[i]).fileName();
    fileName.replace(QLatin1Char('&'), QStringLiteral("&&"));
    QString text = tr("&%1 %2").arg(i + 1).arg(fileName);
    QAction *action =
        m_recentFilesMenu->addAction(text, this, &MainWindow::openRecentFile);
    action->setData(m_recentFiles[i]);
    action->setToolTip(m_recentFiles[i]);
    m_recentFileActions.append(action);
  }

  if (m_recentFiles.isEmpty()) {
    m_recentFilesMenu->addAction("No Recent Files")->setEnabled(false);
  } else {
    m_recentFilesMenu->addSeparator();
    QAction *clearAction = m_recentFilesMenu->addAction("Clear Recent Files");
    connect(clearAction, &QAction::triggered, this, [this]() {
      m_recentFiles.clear();
      updateRecentFileActions();
      saveRecentFiles();
    });
  }
}

/** Open a recent image without replacing an occupied document window. */
void MainWindow::openRecentFile() {
  QAction *action = qobject_cast<QAction *>(sender());
  if (!action)
    return;

  const QString fileName = action->data().toString();
  if (ColorScreenApplication *application = documentApplication())
    application->openFiles({fileName}, this);
  else
    loadFile(fileName);
}

/** Reload the current scan with the selected demosaic mode.  Reloading clears
   the undo stack after replacing image_data, so preserve the document's dirty
   state explicitly when unsaved parameters preceded the reload. */
void MainWindow::reloadCurrentImageWithDemosaic(bool autodetectScreen) {
  if (m_currentImageFile.isEmpty())
    return;
  if (isDocumentModified())
    m_recoveryDirty = true;

  if (autodetectScreen)
    m_screenAutodetectAfterLoadGeneration = m_imageLoadGeneration + 1;
  else
    m_screenAutodetectAfterLoadGeneration.reset();

  loadFile(m_currentImageFile, true);
  if (ColorScreenApplication *application = documentApplication())
    application->reloadSlantedEdgeReferences(this);
}

/** Offer conservative post-load setup recommendations for a new image.
   Detected capture metadata is copied only after explicit user confirmation;
   loading an existing parameter file remains authoritative. */
void MainWindow::maybeOfferInitialSetupGuide(
    const colorscreen::monochrome_bayer_analysis &analysis,
    bool suggestDetectedMetadata) {
  if (!m_scan)
    return;

  const bool suggestCaptureType =
      m_rparams.get_capture_type(m_scan.get()) ==
          colorscreen::render_parameters::capture_unknown;
  const bool looksMonochrome = analysis.candidate && m_scan->has_rgb();
  bool suggestBayer = suggestDetectedMetadata && looksMonochrome &&
      m_rparams.demosaic !=
          colorscreen::image_data::demosaic_monochromatic_bayer_corrected;
  bool suggestFStop = suggestDetectedMetadata && m_scan->f_stop > 0 &&
      std::abs(m_scan->f_stop - m_rparams.sharpen.scanner_mtf.f_stop) > 0.01;
  bool suggestPitch = suggestDetectedMetadata && m_scan->pixel_pitch > 0 &&
      std::abs(m_scan->pixel_pitch - m_rparams.sharpen.scanner_mtf.pixel_pitch) > 0.001;
  bool suggestFill = suggestDetectedMetadata && m_scan->sensor_fill_factor > 0 &&
      std::abs(m_scan->sensor_fill_factor - m_rparams.sharpen.scanner_mtf.sensor_fill_factor) > 0.001;
  bool suggestDPI = suggestDetectedMetadata && m_scan->xdpi > 0 &&
      std::abs(m_scan->xdpi - m_rparams.sharpen.scanner_mtf.scan_dpi) > 0.1;
  bool suggestWavelengths = false;
  for (int c = 0; suggestDetectedMetadata && c < 4; ++c) {
    const bool present = c < 3 ? m_scan->has_rgb()
                               : m_scan->has_grayscale_or_ir();
    const double wavelength = m_scan->wavelengths[c];
    if (present && colorscreen::my_isfinite(wavelength) && wavelength > 0
        && std::abs(wavelength
                    - m_rparams.sharpen.scanner_mtf.wavelengths[c]) > 0.5)
      suggestWavelengths = true;
  }

  if (!suggestCaptureType && !suggestBayer && !suggestFStop && !suggestPitch
      && !suggestFill && !suggestDPI && !suggestWavelengths)
    return;

  const std::shared_ptr<colorscreen::image_data> guideScan = m_scan;
  auto *dialog = new InitialSetupGuideDialog(
      this, suggestCaptureType, looksMonochrome, suggestBayer, suggestFStop,
      suggestPitch, suggestFill, suggestDPI, suggestWavelengths, guideScan.get(),
      m_rparams.get_capture_type(guideScan.get()), m_scrToImgParams.type);
  connect(
      dialog, &QDialog::finished, this,
      [this, dialog, guideScan, suggestCaptureType, suggestBayer, suggestFStop,
       suggestPitch, suggestFill, suggestDPI, suggestWavelengths](int result) {
        if (result != QDialog::Accepted || !m_scan ||
            m_scan.get() != guideScan.get())
          return;

        ParameterState state = getCurrentState();
        QStringList changes;

        const auto capture = suggestCaptureType
            ? dialog->selectedCaptureType()
            : state.rparams.get_capture_type(guideScan.get());
        if (suggestCaptureType &&
            capture != colorscreen::render_parameters::capture_unknown) {
          state.rparams.capture_type = capture;
          if (!colorscreen::render_parameters::capture_has_screen_p(capture))
            state.scrToImg.type = colorscreen::NoScreen;
          changes << tr("capture type");
        }

        colorscreen::scr_type selectedScreen = state.scrToImg.type;
        if (colorscreen::render_parameters::capture_requires_regular_screen_p(
                capture)) {
          selectedScreen = dialog->selectedScreenType();
          if (colorscreen::screen_has_regular_geometry_p(selectedScreen) &&
              state.scrToImg.type != selectedScreen) {
            state.scrToImg.type = selectedScreen;
            changes << tr("screen type");
          }
        }

        if (dialog->usePreferredColorModel() &&
            colorscreen::screen_has_regular_geometry_p(selectedScreen)) {
          const auto previousModel = state.rparams.color_model;
          if (state.rparams.auto_color_model(selectedScreen) &&
              state.rparams.color_model != previousModel)
            changes << tr("preferred color model");
        }

        const bool autoDetectScreen =
            dialog->automaticallyDetectScreen();

        const bool useBayer =
            suggestBayer && dialog->useMonochromeBayerCorrection();
        if (useBayer) {
          state.rparams.demosaic =
              colorscreen::image_data::demosaic_monochromatic_bayer_corrected;
          changes << tr("Bayer compensation");
        }

        if (suggestFStop && dialog->useFStop()) {
          state.rparams.sharpen.scanner_mtf.f_stop = guideScan->f_stop;
          changes << tr("f-stop");
        }

        if (suggestPitch && dialog->usePixelPitch()) {
          state.rparams.sharpen.scanner_mtf.pixel_pitch =
              guideScan->pixel_pitch;
          changes << tr("pixel pitch");
        }

        if (suggestFill && dialog->useFillFactor()) {
          state.rparams.sharpen.scanner_mtf.sensor_fill_factor =
              guideScan->sensor_fill_factor;
          changes << tr("sensor fill factor");
        }

        if (suggestDPI && dialog->useDPI()) {
          state.rparams.sharpen.scanner_mtf.scan_dpi = guideScan->xdpi;
          changes << tr("image resolution");
        }

        if (suggestWavelengths && dialog->useWavelengths()) {
          for (int c = 0; c < 4; ++c) {
            const bool present = c < 3 ? guideScan->has_rgb()
                                       : guideScan->has_grayscale_or_ir();
            const double wavelength = guideScan->wavelengths[c];
            if (present && colorscreen::my_isfinite(wavelength)
                && wavelength > 0)
              state.rparams.sharpen.scanner_mtf.wavelengths[c] = wavelength;
          }
          changes << tr("channel wavelengths");
        }

        if (!changes.isEmpty()) {
          changeParameters(
              state, tr("Use suggested %1").arg(changes.join(", ")));
        }

        if (useBayer) {
          reloadCurrentImageWithDemosaic(autoDetectScreen);
        } else if (autoDetectScreen) {
          // Let parameter/panel refresh finish before Screen detection inspects
          // the accepted capture and optional regular-screen choice.
          QTimer::singleShot(0, this, &MainWindow::onAutodetectScreen);
        }
      });
  connect(dialog, &QDialog::finished, dialog, &QObject::deleteLater);
  dialog->open();
}

/** Load an image file and optionally its associated .par parameter file.
   If SUPPRESSPARAMPROMPT is false, checks for a .par file alongside the
   image and offers to load it.  If the user declines or no .par file exists,
   a weak (suggested) parameter filename is set for later Save.
   The actual image loading runs asynchronously via QtConcurrent::run; on
   completion, the scan is set on ImageWidget, stitch tile loading is launched
   in parallel for .csprj projects, and undo history is cleared.  */
void MainWindow::loadFile(const QString &fileName, bool suppressParamPrompt) {
  if (fileName.isEmpty())
    return;

  // Final-result work and any pending one-shot confirmation belong to the
  // current image snapshot. Invalidate both before starting replacement I/O.
  dismissOneShotPrompts();
  m_oneShotOperations.cancelAll();
  const uint64_t loadGeneration = ++m_imageLoadGeneration;
  if (m_screenAutodetectAfterLoadGeneration &&
      *m_screenAutodetectAfterLoadGeneration != loadGeneration)
    m_screenAutodetectAfterLoadGeneration.reset();
  m_imageLoadPending = true;
  bool parameterDataLoaded = false;
  if (!suppressParamPrompt)
    m_recoveryDirty = false;
  m_currentImageFile = QFileInfo(fileName).absoluteFilePath();
  updateWindowTitle();

  // Clear current image and stop rendering
  m_imageWidget->setImage(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

  // Check for .par file (only if not suppressed, e.g., during recovery)
  if (!suppressParamPrompt) {
    QFileInfo fileInfo(m_currentImageFile);
    QString parFile =
        fileInfo.path() + "/" + fileInfo.completeBaseName() + ".par";

    if (QFile::exists(parFile)) {
      if (QMessageBox::question(this, "Load Parameters?",
                                "A parameter file was found for this image. Do "
                                "you want to load it?") == QMessageBox::Yes) {

        FILE *f = fopen(parFile.toUtf8().constData(), "r");
        if (f) {
          const char *error = nullptr;
          // load_csp merges parameters in; reset first.
          colorscreen::scr_to_img_parameters emptyScrToImg;
          m_scrToImgParams = emptyScrToImg;
          colorscreen::scr_detect_parameters emptyScrDetect;
          m_detectParams = emptyScrDetect;
          colorscreen::render_parameters emptyRparams;
          m_rparams = emptyRparams;
          colorscreen::solver_parameters emptySolver;
          m_solverParams = emptySolver;
          m_profileSpots.clear();
          m_profileSpotResults.clear();
          if (!colorscreen::load_csp_with_profile_spots(
                  f, &m_scrToImgParams, &m_detectParams, &m_rparams,
                  &m_solverParams, &error, &m_profileSpots,
                  &m_profileSpotResults)) {
            QMessageBox::warning(this, "Error Loading Parameters",
                                 error ? QString::fromUtf8(error)
                                       : "Unknown error loading parameters.");
          } else {
            parameterDataLoaded = true;
            m_prevScrToImgParams = m_scrToImgParams;
            m_prevDetectParams = m_detectParams;

            // Track the loaded parameter file
            m_currentParamsFile = parFile;
            m_currentParamsFileIsWeak =
                false; // This is a real file, not a suggestion
            addToRecentParams(parFile);

            // If we have a valid screen type, default to formatted
            // (interpolated) view
            if (colorscreen::screen_geometry_configured_p(m_scrToImgParams)) {
              m_renderTypeParams.type = colorscreen::render_type_interpolated;
            }
          }
          fclose(f);
        }
      } else {
        // User declined to load parameters - suggest filename
        QFileInfo fileInfo(fileName);
        m_currentParamsFile =
            fileInfo.path() + "/" + fileInfo.completeBaseName() + ".par";
        m_currentParamsFileIsWeak = true;
      }
    } else {
      // No parameter file exists - suggest filename
      QFileInfo fileInfo(m_currentImageFile);
      m_currentParamsFile =
          fileInfo.path() + "/" + fileInfo.completeBaseName() + ".par";
      m_currentParamsFileIsWeak = true;
    }
  }

  const bool suggestDetectedMetadata =
      !suppressParamPrompt && !parameterDataLoaded;
  // Capture-type compatibility can only be checked after image_data has
  // been loaded. Keep this as permission to offer the guide; the actual
  // decision is made in the successful-load callback below.
  const bool allowInitialGuide = !suppressParamPrompt;

  auto progress = std::make_shared<colorscreen::progress_info>();
  progress->set_task("Opening image", 0);
  addProgress(progress);

  std::shared_ptr<colorscreen::image_data> tempScan =
      std::make_shared<colorscreen::image_data>();
  // Access m_rparams carefully. It's a member.
  colorscreen::image_data::demosaicing_t demosaic = m_rparams.demosaic;

  bool isCsprj =
      fileName.endsWith(QLatin1String(".csprj"), Qt::CaseInsensitive);

  QFutureWatcher<std::pair<bool, QString>> *watcher =
      new QFutureWatcher<std::pair<bool, QString>>(this);
  connect(
      watcher, &QFutureWatcher<std::pair<bool, QString>>::finished, this,
      [this, watcher, tempScan, progress, fileName, isCsprj,
       allowInitialGuide, suggestDetectedMetadata, loadGeneration]() {
        if (m_closing) {
          watcher->deleteLater();
          return;
        }

        const std::pair<bool, QString> result = watcher->result();
        removeProgress(progress);
        watcher->deleteLater();

        // Reloading (notably after changing demosaic mode) can start another
        // asynchronous image load before this one finishes. Only the newest
        // generation may clear the pending state or replace the document scan.
        if (loadGeneration != m_imageLoadGeneration)
          return;

        const bool autodetectScreenAfterLoad =
            m_screenAutodetectAfterLoadGeneration &&
            *m_screenAutodetectAfterLoadGeneration == loadGeneration;
        if (autodetectScreenAfterLoad)
          m_screenAutodetectAfterLoadGeneration.reset();

        m_imageLoadPending = false;

        if (result.first) {
          m_detectedScreenMap.reset();
          if (m_detectedPatchCentersAction)
            m_detectedPatchCentersAction->setEnabled(false);
          m_scan = tempScan;

          if ((int)m_scan->gamma != -2 && m_scan->gamma > 0 &&
              m_rparams.gamma == -1) // Update only if unknown
            m_rparams.gamma = m_scan->gamma;
          else if (m_rparams.gamma == -1)
            m_rparams.gamma = -1;

          m_undoStack->clear();

          // If this is a stitched project, disable all tiles initially so
          // the UI is responsive while tiles load in the background.
          if (isCsprj && m_scan->stitch) {
            colorscreen::stitch_project *stitch = m_scan->stitch;
            int w = stitch->params.width;
            int h = stitch->params.height;
            m_rparams.set_tile_adjustments_dimensions(w, h);
            for (int y = 0; y < h; y++)
              for (int x = 0; x < w; x++)
                m_rparams.get_tile_adjustment(x, y).enabled = false;
          }

          m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                                  &m_detectParams, &m_renderTypeParams,
                                  &m_solverParams);
          onImageLoaded();

          if (autodetectScreenAfterLoad) {
            QTimer::singleShot(0, this, &MainWindow::onAutodetectScreen);
          }

          // Add to recent files and immediately establish this document's
          // independent crash-recovery payload.
          addToRecentFiles(m_currentImageFile);
          saveRecoveryState();
          updateWindowTitle();

          const bool offerInitialGuide =
              allowInitialGuide &&
              (suggestDetectedMetadata ||
               m_rparams.get_capture_type(m_scan.get()) ==
                   colorscreen::render_parameters::capture_unknown);
          if (offerInitialGuide) {
            const colorscreen::monochrome_bayer_analysis analysis =
                m_scan->analyze_monochrome_bayer();
            QTimer::singleShot(
                0, this,
                [this, analysis, suggestDetectedMetadata, loadGeneration,
                 tempScan]() {
                  if (m_closing || loadGeneration != m_imageLoadGeneration ||
                      m_scan != tempScan)
                    return;
                  maybeOfferInitialSetupGuide(analysis,
                                              suggestDetectedMetadata);
                });
          }

          // Launch background tile loading for stitch projects.
          if (isCsprj && m_scan->stitch) {
            colorscreen::stitch_project *stitch = m_scan->stitch;
            int w = stitch->params.width;
            int h = stitch->params.height;

            for (int ty = 0; ty < h; ty++) {
              for (int tx = 0; tx < w; tx++) {
                auto tileProgress =
                    std::make_shared<colorscreen::progress_info>();
                tileProgress->set_task(
                    qPrintable(tr("Loading tile %1,%2").arg(tx).arg(ty)), 1);
                addProgress(tileProgress);

                auto scanRef = m_scan; // keep scan alive
                int capturedX = tx;
                int capturedY = ty;

                auto *tileWatcher = new QFutureWatcher<bool>(this);
                connect(tileWatcher, &QFutureWatcher<bool>::finished, this,
                        [this, tileWatcher, tileProgress, scanRef, capturedX,
                         capturedY]() {
                          if (m_closing) {
                            tileWatcher->deleteLater();
                            return;
                          }
                          const bool ok = tileWatcher->result();
                          removeProgress(tileProgress);
                          tileWatcher->deleteLater();

                          // A demosaic/project reload replaces m_scan while
                          // workers for the previous image_data may still
                          // finish. Their tile bytes belong to scanRef only;
                          // never publish an enable edit into the replacement
                          // document state.
                          if (m_scan != scanRef)
                            return;

                          if (ok) {
                            // Enable the tile and trigger a re-render.
                            ParameterState state = getCurrentState();
                            state.rparams
                                .get_tile_adjustment(capturedX, capturedY)
                                .enabled = true;
                            changeParameters(state, tr("Tile loaded %1,%2")
                                                        .arg(capturedX)
                                                        .arg(capturedY));
                          }
                        });

                QFuture<bool> tileFuture = QtConcurrent::run(
                    [scanRef, capturedX, capturedY, tileProgress]() -> bool {
                      try {
                        if (!scanRef || !scanRef->stitch)
                          return false;
                        const char *err = nullptr;
                        return scanRef->stitch->images[capturedY][capturedX]
                            .load_img(&err, tileProgress.get());
                      } catch (...) {
                        // A failed tile remains disabled. Never let a worker
                        // exception escape through QFutureWatcher::result().
                        return false;
                      }
                    });
                tileWatcher->setFuture(tileFuture);
              }
            }
          }

        } else {
          updateWindowTitle();
          if (progress->cancelled()) {
          } else {
            QMessageBox::critical(this, "Error Loading Image",
                                  result.second.isEmpty()
                                      ? "Failed to load image."
                                      : result.second);
          }
        }
      });

  QString absolutePath = m_currentImageFile;
  QFuture<std::pair<bool, QString>> future = QtConcurrent::run(
      [tempScan, absolutePath, progress, demosaic, isCsprj]() {
        try {
          const char *error = nullptr;
          colorscreen::sub_task task(progress.get());
          const bool res =
              tempScan->load(absolutePath.toUtf8().constData(),
                             /*preload_all=*/!isCsprj, &error,
                             progress.get(), demosaic);
          QString errStr;
          if (!res && error)
            errStr = QString::fromUtf8(error);
          return std::make_pair(res, errStr);
        } catch (const std::exception &exception) {
          return std::make_pair(false, QString::fromUtf8(exception.what()));
        } catch (...) {
          return std::make_pair(
              false, QStringLiteral("Unexpected exception while loading image."));
        }
      });

  watcher->setFuture(future);
}

/** Load the recent image files list from QSettings on startup.  */
void MainWindow::loadRecentFiles() {
  QSettings settings;
  m_recentFiles = settings.value("recentFiles").toStringList();
  updateRecentFileActions();
}

/** Persist the recent image files list to QSettings.  */
void MainWindow::saveRecentFiles() {
  QSettings settings;
  settings.setValue("recentFiles", m_recentFiles);
}

/** Return whether this document has parameters not represented by its saved
    .par file.  Recovered state remains dirty even though the reconstructed undo
    stack starts empty.  */
bool MainWindow::isDocumentModified() const {
  return m_recoveryDirty || (m_undoStack && !m_undoStack->isClean());
}

/** Return whether a new image may safely reuse this document window. */
bool MainWindow::canReuseForOpen() const {
  return !m_closing && !m_scan && !m_imageLoadPending &&
         m_currentImageFile.isEmpty() && !isDocumentModified();
}

/** Return this document's concise name for the application Window menu. */
QString MainWindow::documentDisplayName() const {
  QString name = m_currentImageFile.isEmpty()
                     ? tr("Untitled")
                     : QFileInfo(m_currentImageFile).fileName();
  if (isDocumentModified())
    name += QLatin1Char('*');
  return name;
}

/** Check for unsaved parameter changes and prompt the user.
   Returns true only after a successful synchronous save, an explicit discard,
   or when this document has no unsaved state.  */
bool MainWindow::maybeSave() {
  if (!isDocumentModified())
    return true;

  QString displayName = documentDisplayName();
  displayName.remove(QLatin1Char('*'));
  const QMessageBox::StandardButton result = QMessageBox::warning(
      this, "Unsaved Changes",
      QString("Parameters for %1 have been modified.\n"
              "Do you want to save your changes?")
          .arg(displayName),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

  switch (result) {
  case QMessageBox::Save:
    if (m_currentParamsFile.isEmpty() || m_currentParamsFileIsWeak)
      return saveParametersAs();
    return saveParametersToFile(m_currentParamsFile);
  case QMessageBox::Discard:
    return true;
  case QMessageBox::Cancel:
  default:
    return false;
  }
}

/** Ask all user-visible questions that may veto destroying this document. */
bool MainWindow::confirmClose() {
  if (!maybeSave())
    return false;

  if (m_fileRenderController.hasActiveRenders()) {
    const auto result = QMessageBox::question(
        this, tr("Rendering in Progress"),
        tr("A render is currently in progress. Cancel it and close this window?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (result != QMessageBox::Yes)
      return false;
  }
  return true;
}

/** Preflight final application closure without tearing down this document. */
bool MainWindow::prepareForApplicationClose() {
  if (m_closing || m_applicationClosePrepared)
    return true;
  if (!confirmClose())
    return false;
  m_applicationClosePrepared = true;
  return true;
}

/** Forget a preflight approval when another document vetoes File -> Exit. */
void MainWindow::cancelPreparedApplicationClose() {
  m_applicationClosePrepared = false;
}

/** Handle closing one image-document window.
   Prompts for this document's unsaved changes, asks to cancel its active
   render, cancels only its background tasks, removes only its recovery data,
   and saves the shared preferred window layout.  */
void MainWindow::closeEvent(QCloseEvent *event) {
  if (m_closing) {
    event->accept();
    return;
  }

  // The primary MainWindow is also the document state owner.  Closing this
  // presentation must not destroy that owner while peer ImageViewWindows still
  // need its parameters, undo stack, workers, and recovery state.
  if (ColorScreenApplication *application = documentApplication()) {
    if (application->retainDocumentForOpenViews(this)) {
      event->ignore();
      return;
    }
  }

  if (m_applicationClosePrepared) {
    // File -> Exit already resolved every user-visible veto before it started
    // tearing down secondary views.  Consume the one-shot approval here.
    m_applicationClosePrepared = false;
  } else if (!confirmClose()) {
    event->ignore();
    return;
  }

  m_closing = true;
  dismissOneShotPrompts();

  // Cancel all active processes.
  m_fileRenderController.cancelAll();
  m_progressController.cancelAll();

  // Clean up recovery files on normal exit
  clearRecoveryFiles();

  // Return workspace-owned presentation widgets before saving state or
  // destroying this document.  This keeps all document UI owned by the
  // MainWindow during teardown and lets the workspace select the next image.
  if (ColorScreenApplication *application = documentApplication())
    application->prepareDocumentForClose(this);

  // Recent lists are persisted at the point of each change.  Saving a stale
  // per-window copy here would let the last closed document overwrite entries
  // added by another open window.
  saveWindowState();
  event->accept();
}

/** Update the title and standard Qt modification marker for this document. */
void MainWindow::updateWindowTitle() {
  QString title = QStringLiteral("Color-Screen " PACKAGE_VERSION ": ") +
                  (m_currentImageFile.isEmpty()
                       ? tr("Untitled")
                       : QFileInfo(m_currentImageFile).fileName());
  setWindowModified(isDocumentModified());
  setWindowTitle(title + QStringLiteral("[*]"));
  if (ColorScreenApplication *application = documentApplication())
    application->refreshDocumentPresentation(this);
}

/** Save window geometry, state, splitter positions, and current desktop
   size to QSettings for restoration on next launch.  */
void MainWindow::saveWindowState() {
  QSettings settings;

  // The primary workspace owns tabbed-window geometry.  Save document-window
  // geometry only while this document is detached; dock state is useful in
  // either presentation.
  if (isWindow()) {
    settings.setValue("windowGeometry", saveGeometry());
    if (QScreen *screen = QApplication::primaryScreen())
      settings.setValue("desktopSize", screen->availableGeometry().size());
  }
  settings.setValue("windowState", saveState());

  // Save splitter positions
  if (m_mainSplitter) {
    settings.setValue("mainSplitterState", m_mainSplitter->saveState());
  }
}

/** Restore window geometry and splitter positions from QSettings.
   Validates that the desktop size hasn't changed significantly (>100 px)
   since the layout was saved; if it has, falls back to a default size.
   After restoring, fixes any docks that were saved as visible but have
   no widget content (they would appear as empty floating windows).  */
void MainWindow::restoreWindowState() {
  QSettings settings;

  // Check if desktop size has changed
  bool desktopSizeValid = true;
  QScreen *screen = QApplication::primaryScreen();
  if (screen) {
    QSize savedDesktopSize = settings.value("desktopSize").toSize();
    QSize currentDesktopSize = screen->availableGeometry().size();

    // Allow some tolerance (e.g., taskbar changes)
    if (savedDesktopSize.isValid()) {
      int widthDiff =
          qAbs(savedDesktopSize.width() - currentDesktopSize.width());
      int heightDiff =
          qAbs(savedDesktopSize.height() - currentDesktopSize.height());

      // If desktop size changed significantly (more than 100 pixels), don't
      // restore
      if (widthDiff > 100 || heightDiff > 100) {
        desktopSizeValid = false;
      }
    }
  }

  // Restore window geometry if desktop size is compatible
  if (desktopSizeValid && settings.contains("windowGeometry")) {
    restoreGeometry(settings.value("windowGeometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());
    restoreState(settings.value("windowState").toByteArray());
  } else {
    // Default size and position
    resize(1200, 800);
    // Center on screen
    if (screen) {
      QRect screenGeometry = screen->availableGeometry();
      move(screenGeometry.center() - rect().center());
    }
  }

  // Restore splitter state (always try this, it's safe)
  if (m_mainSplitter && settings.contains("mainSplitterState")) {
    m_mainSplitter->restoreState(
        settings.value("mainSplitterState").toByteArray());
  }
}

/** Add a file path to the most-recently-used parameter files list.
   Same pattern as addToRecentFiles.  */
void MainWindow::addToRecentParams(const QString &filePath) {
  // Parameter saves and loads can finish in different document windows; start
  // from QSettings so the most recent writer merges rather than overwrites.
  QSettings settings;
  m_recentParams = settings.value("recentParams").toStringList();
  QString absolutePath = QFileInfo(filePath).absoluteFilePath();
  m_recentParams.removeAll(absolutePath);
  m_recentParams.prepend(absolutePath);

  while (m_recentParams.size() > MaxRecentFiles)
    m_recentParams.removeLast();

  settings.setValue("recentParams", m_recentParams);
  updateRecentParamsActions();
}

/** Rebuild the "Open Recent Parameters" submenu.  */
void MainWindow::updateRecentParamsActions() {
  m_recentParamsMenu->clear();
  m_recentParamsActions.clear();

  for (int i = 0; i < m_recentParams.size(); ++i) {
    QString fileName = QFileInfo(m_recentParams[i]).fileName();
    fileName.replace(QLatin1Char('&'), QStringLiteral("&&"));
    QString text = tr("&%1 %2").arg(i + 1).arg(fileName);
    QAction *action = m_recentParamsMenu->addAction(
        text, this, &MainWindow::openRecentParams);
    action->setData(m_recentParams[i]);
    action->setToolTip(m_recentParams[i]);
    m_recentParamsActions.append(action);
  }

  if (m_recentParams.isEmpty()) {
    m_recentParamsMenu->addAction("No Recent Parameters")->setEnabled(false);
  } else {
    m_recentParamsMenu->addSeparator();
    QAction *clearAction =
        m_recentParamsMenu->addAction("Clear Recent Parameters");
    connect(clearAction, &QAction::triggered, this, [this]() {
      m_recentParams.clear();
      updateRecentParamsActions();
      saveRecentParams();
    });
  }
}

/** Slot invoked when a recent parameter menu item is clicked.
   Loads the .par file (reset + merge), updates the renderer and UI,
   syncs gamut warning state, and clears undo history.  */
void MainWindow::openRecentParams() {
  QAction *action = qobject_cast<QAction *>(sender());
  if (!action)
    return;

  // maybeSave() can rebuild this submenu and delete ACTION, so capture its
  // payload before opening a nested save dialog.
  const QString fileName = action->data().toString();
  if (maybeSave()) {
    if (loadParameterFile(fileName)) {
      statusBar()->showMessage(QString("Parameters loaded from %1").arg(fileName),
                               3000);
    }
  }
}

/** Load the recent parameter files list from QSettings on startup.  */
void MainWindow::loadRecentParams() {
  QSettings settings;
  m_recentParams = settings.value("recentParams").toStringList();
  updateRecentParamsActions();
}

/** Persist the recent parameter files list to QSettings.  */
void MainWindow::saveRecentParams() {
  QSettings settings;
  settings.setValue("recentParams", m_recentParams);
}

/** Auto-save this document into its private crash-recovery directory.
   Called by a 30-second timer and immediately after image load/save.  The
   payload contains the image path, complete parameters, and current parameter
   filename metadata.  */
void MainWindow::saveRecoveryState() {
  if (!m_scan || m_recoveryDir.isEmpty())
    return;
  if (!QDir().mkpath(m_recoveryDir))
    return;

  const QDir directory(m_recoveryDir);
  QFile imageFile(directory.filePath(QStringLiteral("recovery_image.txt")));
  if (imageFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QTextStream out(&imageFile);
    out << m_currentImageFile;
  }

  const QString paramsPath =
      directory.filePath(QStringLiteral("recovery_params.par"));
  bool paramsSaved = false;
  FILE *f = fopen(paramsPath.toUtf8().constData(), "wt");
  if (f) {
    const bool hasRgb = m_scan->has_rgb();
    paramsSaved = colorscreen::save_csp_with_profile_spots(
        f, &m_scrToImgParams, hasRgb ? &m_detectParams : nullptr, &m_rparams,
        &m_solverParams, m_profileSpots);
    if (fclose(f) != 0)
      paramsSaved = false;
  }
  if (!paramsSaved)
    QFile::remove(paramsPath);

  QFile metaFile(
      directory.filePath(QStringLiteral("recovery_params_meta.txt")));
  if (metaFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QTextStream out(&metaFile);
    out << m_currentParamsFile << '\n';
    out << (m_currentParamsFileIsWeak ? "1" : "0") << '\n';
    out << (isDocumentModified() ? "1" : "0") << '\n';
  }
}

/** Restore this document from its private recovery payload.
   Restores the saved dirty flag when present; legacy payloads are treated as
   modified because they may contain edits never written to the user's .par
   file. Returns false only when the directory contains no usable data.  */
bool MainWindow::restoreRecoveryState() {
  if (m_recoveryDir.isEmpty())
    return false;

  const QDir directory(m_recoveryDir);
  const QString imagePath =
      directory.filePath(QStringLiteral("recovery_image.txt"));
  const QString paramsPath =
      directory.filePath(QStringLiteral("recovery_params.par"));
  if (!QFile::exists(imagePath) && !QFile::exists(paramsPath))
    return false;

  QString imageToLoad;
  QFile imageFile(imagePath);
  if (imageFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&imageFile);
    imageToLoad = in.readLine().trimmed();
  }

  if (QFile::exists(paramsPath)) {
    FILE *f = fopen(paramsPath.toUtf8().constData(), "r");
    if (f) {
      const char *error = nullptr;
      const bool loaded = colorscreen::load_csp_with_profile_spots(
          f, &m_scrToImgParams, &m_detectParams, &m_rparams, &m_solverParams,
          &error, &m_profileSpots, &m_profileSpotResults);
      fclose(f);
      if (!loaded || error) {
        QMessageBox::warning(
            this, "Recovery Warning",
            error ? QString("Error loading parameters: %1").arg(error)
                  : QStringLiteral("Could not load recovered parameters."));
      } else {
        m_prevScrToImgParams = m_scrToImgParams;
        m_prevDetectParams = m_detectParams;
      }
    }
  }

  m_recoveryDirty = true; // Legacy recovery metadata has no dirty flag.
  QFile metaFile(
      directory.filePath(QStringLiteral("recovery_params_meta.txt")));
  if (metaFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&metaFile);
    m_currentParamsFile = in.readLine().trimmed();
    m_currentParamsFileIsWeak = (in.readLine().trimmed() == QLatin1String("1"));
    const QString dirtyFlag = in.readLine().trimmed();
    if (!dirtyFlag.isEmpty())
      m_recoveryDirty = (dirtyFlag == QLatin1String("1"));
  }

  if (!imageToLoad.isEmpty() && QFile::exists(imageToLoad)) {
    loadFile(imageToLoad, true);
  } else if (!imageToLoad.isEmpty()) {
    QMessageBox::warning(
        this, "Recovery Warning",
        QString("Could not find image file: %1").arg(imageToLoad));
  }

  updateUIFromState(getCurrentState());
  updateWindowTitle();
  return true;
}

/** Delete only this document's recovery directory after a clean close. */
void MainWindow::clearRecoveryFiles() {
  if (!m_recoveryDir.isEmpty())
    QDir(m_recoveryDir).removeRecursively();
}

/** Load parameters from a .par file and update all UI components.
   Resets parameters to defaults before loading (as load_csp merges into
   existing values).  Updates ImageWidget, NavigationView, gamut warning,
   undo history, and all panels.  Returns true on success.  */
bool MainWindow::loadParameterFile(const QString &fileName) {
  // Loading external parameters invalidates every final-result state snapshot
  // and any one-shot confirmation waiting on the old parameters.
  dismissOneShotPrompts();
  m_oneShotOperations.cancelAll();
  FILE *f = fopen(fileName.toUtf8().constData(), "r");
  if (!f) {
    QMessageBox::critical(this, "Error", "Could not open file.");
    return false;
  }

  const char *error = nullptr;

  // Store previous state in case load fails.  Profile match results are
  // derived UI state rather than part of ParameterState, but failed loads must
  // preserve those too.
  const ParameterState oldState = getCurrentState();
  const std::vector<colorscreen::color_match> oldProfileSpotResults =
      m_profileSpotResults;

  // load_csp merges parameters in; reset first to ensure clean load.
  m_scrToImgParams = colorscreen::scr_to_img_parameters();
  m_detectParams = colorscreen::scr_detect_parameters();
  m_rparams = colorscreen::render_parameters();
  m_solverParams = colorscreen::solver_parameters();

  if (!colorscreen::load_csp_with_profile_spots(
          f, &m_scrToImgParams, &m_detectParams, &m_rparams, &m_solverParams,
          &error, &m_profileSpots, &m_profileSpotResults)) {
    fclose(f);
    QString errStr =
        error ? QString::fromUtf8(error) : "Unknown error loading parameters.";
    QMessageBox::critical(this, "Error Loading Parameters", errStr);

    // Restore previous state
    m_scrToImgParams = oldState.scrToImg;
    m_detectParams = oldState.detect;
    m_rparams = oldState.rparams;
    m_solverParams = oldState.solver;
    m_profileSpots = oldState.profileSpots;
    m_profileSpotResults = oldProfileSpotResults;
    return false;
  }
  fclose(f);

  // Successful external parameter load establishes a new calibration context.
  m_geometryFit.clear();
  m_mtfFit.clear();
  m_colorOptimizerQueue.cancelAll();
  m_profileCalibration.clear();

  // Update UI/Renderer
  if (m_scan) {
    m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                            &m_detectParams, &m_renderTypeParams,
                            &m_solverParams);
    m_imageWidget->setProfileSpots(&m_profileSpots, &m_profileSpotResults);
    if (m_profilePanel)
      m_profilePanel->setSpotResults(m_profileSpotResults);
    m_navigationView->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                               &m_detectParams);
    updateColorCheckBoxState();
  }

  // Sync Gamut Warning Button
  if (m_gamutWarningAction) {
    QSignalBlocker blocker(m_gamutWarningAction);
    m_gamutWarningAction->setChecked(m_rparams.gamut_warning);
  }

  if (m_undoStack)
    m_undoStack->clear();
  m_recoveryDirty = false;

  const QString absoluteFileName = QFileInfo(fileName).absoluteFilePath();
  m_currentParamsFile = absoluteFileName;
  m_currentParamsFileIsWeak = false;

  updateModeMenu();
  updateUIFromState(getCurrentState());
  addToRecentParams(absoluteFileName);
  updateWindowTitle();
  saveRecoveryState();

  return true;
}
