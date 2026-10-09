#include "MainWindow.h"
#include "AtomicFileSave.h"
#include "../libcolorscreen/include/base.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/stitch.h"
#include "../libcolorscreen/parameter-archive.h"
#include "ColorOptimizerWorker.h"
#include "ColorScreenApplication.h"
#include "GeometryPanel.h"
#include "GeometrySolverWorker.h"
#include "ImageWidget.h"
#include "InitialSetupGuideDialog.h"
#include "NavigationView.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDialog>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFuture>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QObject>
#include <QPointer>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QSize>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QUndoStack>
#include <QtConcurrent>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <utility>

namespace {
/** Return the application-level document manager when MainWindow is running
    inside the normal Color-Screen Qt application. */
ColorScreenApplication *documentApplication() {
  return dynamic_cast<ColorScreenApplication *>(QCoreApplication::instance());
}

/** Return an application-wide file-dialog directory when it still exists. */
QString fileDialogDirectoryPreference(const QString &settingsKey) {
  QSettings settings;
  const QString directory = settings.value(settingsKey).toString();
  return directory.isEmpty() || QDir(directory).exists() ? directory : QString();
}

/** Remember the containing directory of one successfully chosen/used file. */
void rememberFileDialogDirectory(const QString &settingsKey,
                                 const QString &fileName) {
  if (fileName.isEmpty())
    return;
  QSettings settings;
  settings.setValue(settingsKey, QFileInfo(fileName).absolutePath());
}

/** Serialize one complete CSP + Qt metadata payload into BYTES. */
bool serializeParameterPayload(
    const colorscreen::scr_to_img_parameters &scrToImg,
    const colorscreen::scr_detect_parameters *detect,
    const colorscreen::render_parameters &render,
    const colorscreen::solver_parameters &solver,
    const std::vector<colorscreen::point_t> &profileSpots,
    std::string *bytes, QString *error) {
  if (!bytes)
    return false;

  FILE *staged = std::tmpfile();
  if (!staged) {
    if (error)
      *error = QCoreApplication::translate(
          "MainWindow", "Could not create a temporary parameter payload.");
    return false;
  }

  bool ok = colorscreen::save_csp_with_profile_spots(
      staged, &scrToImg, detect, &render, &solver, profileSpots);
  if (ok && std::fflush(staged) != 0)
    ok = false;
  if (ok && std::fseek(staged, 0, SEEK_SET) != 0)
    ok = false;

  std::string result;
  char buffer[64 * 1024];
  while (ok) {
    const size_t count = std::fread(buffer, 1, sizeof(buffer), staged);
    if (count > 0)
      result.append(buffer, count);
    if (count < sizeof(buffer)) {
      if (std::ferror(staged))
        ok = false;
      break;
    }
  }
  std::fclose(staged);

  if (!ok || result.empty()) {
    if (error)
      *error = QCoreApplication::translate(
          "MainWindow", "Could not serialize the complete parameter payload.");
    return false;
  }

  *bytes = std::move(result);
  if (error)
    error->clear();
  return true;
}

/** Atomically save one legacy CSP or versioned parameter archive. */
bool saveParameterPayloadAtomically(
    const QString &path, bool archive,
    const colorscreen::scr_to_img_parameters &scrToImg,
    const colorscreen::scr_detect_parameters *detect,
    const colorscreen::render_parameters &render,
    const colorscreen::solver_parameters &solver,
    const std::vector<colorscreen::point_t> &profileSpots, QString *error) {
  if (!archive) {
    if (render.image_area.set) {
      if (error)
        *error = QCoreApplication::translate(
            "MainWindow",
            "Legacy .par cannot preserve the photographic image area. "
            "Save as .cspar or clear the inner image area first.");
      return false;
    }
    return qtgui_io::saveStdioAtomically(
        path,
        [&scrToImg, detect, &render, &solver, &profileSpots](FILE *staged) {
          return colorscreen::save_csp_with_profile_spots(
              staged, &scrToImg, detect, &render, &solver, profileSpots);
        },
        error);
  }

  std::string payload;
  if (!serializeParameterPayload(scrToImg, detect, render, solver, profileSpots,
                                 &payload, error))
    return false;

  QString version = QCoreApplication::applicationVersion();
  if (version.isEmpty())
    version = QStringLiteral(PACKAGE_VERSION);
  const QByteArray versionBytes = version.toUtf8();

  const colorscreen::parameter_archive_render_overrides renderOverrides =
      colorscreen::parameter_archive_render_overrides_from(render);
  const colorscreen::parameter_archive_geometry_final_frame geometryFrame =
      colorscreen::parameter_archive_geometry_final_frame_from(scrToImg);
  const colorscreen::parameter_archive_image_area imageArea =
      colorscreen::parameter_archive_image_area_from(render);
  std::string archiveError;
  const QByteArray targetName = path.toUtf8();
  const bool written = colorscreen::write_parameter_payload_file(
      targetName.constData(), payload, true, versionBytes.constData(),
      &archiveError, &renderOverrides, &geometryFrame, &imageArea);
  if (!written && error)
    *error = QString::fromUtf8(archiveError);
  else if (written && error)
    error->clear();
  return written;
}

/** Atomically replace a small UTF-8 recovery metadata file. */
bool saveRecoveryTextAtomically(const QString &path, const QString &text) {
  return qtgui_io::saveTextAtomically(path, text);
}

/** Parse one parameter payload into private default state.

    LOAD_CSP merges into its outputs, so callers must never pass live document
    members here. ERROR receives a copied open/parse diagnostic on failure. */
bool loadParameterPayload(
    const QString &path, ParameterState *state,
    std::vector<colorscreen::color_match> *spotResults, bool *isArchive,
    QString *error) {
  if (!state)
    return false;

  std::string openError;
  bool archive = false;
  colorscreen::parameter_archive_manifest archiveManifest;
  const QByteArray encodedPath = path.toUtf8();
  FILE *f = colorscreen::open_parameter_payload(
      encodedPath.constData(), &archive, &openError, &archiveManifest);
  if (!f) {
    if (error) {
      const QString detail = QString::fromUtf8(openError);
      *error = detail.isEmpty()
                   ? QCoreApplication::translate(
                         "MainWindow", "Could not open %1.").arg(path)
                   : detail;
    }
    return false;
  }

  ParameterState loadedState;
  std::vector<colorscreen::color_match> loadedSpotResults;
  const char *libraryError = nullptr;
  const bool loaded = colorscreen::load_csp_with_profile_spots(
      f, &loadedState.scrToImg, &loadedState.detect, &loadedState.rparams,
      &loadedState.solver, &libraryError, &loadedState.profileSpots,
      &loadedSpotResults);
  const QString errorDetail =
      libraryError ? QString::fromUtf8(libraryError) : QString();
  fclose(f);

  if (!loaded || !errorDetail.isEmpty()) {
    if (error)
      *error = errorDetail.isEmpty()
                   ? QCoreApplication::translate(
                         "MainWindow",
                         "The file contains invalid or incomplete parameter data.")
                   : errorDetail;
    return false;
  }

  if (archive) {
    colorscreen::apply_parameter_archive_render_overrides(
        archiveManifest.render_overrides, &loadedState.rparams);
    colorscreen::apply_parameter_archive_image_area(
        archiveManifest.image_area, &loadedState.rparams);
    colorscreen::apply_parameter_archive_geometry_final_frame(
        archiveManifest.geometry_final_frame, &loadedState.scrToImg);
  }

  *state = std::move(loadedState);
  if (spotResults)
    *spotResults = std::move(loadedSpotResults);
  if (isArchive)
    *isArchive = archive;
  if (error)
    error->clear();
  return true;
}

/** Return the current text of a named provenance/status label. */
QString reportStatusText(const QObject *root, const QString &objectName) {
  if (!root)
    return QString();
  const QLabel *label = root->findChild<QLabel *>(objectName);
  return label ? label->text().trimmed() : QString();
}

/** Present one nonblocking parameter-load failure without changing state. */
void showParameterLoadFailure(QWidget *parent, const QString &detail) {
  auto *box = new QMessageBox(
      QMessageBox::Critical,
      QCoreApplication::translate("MainWindow", "Parameter Load Failed"),
      QCoreApplication::translate(
          "MainWindow",
          "The parameter file could not be loaded. The current document "
          "parameters and calibration state were left unchanged.\n\n%1")
          .arg(detail),
      QMessageBox::Ok, parent);
  box->setObjectName(QStringLiteral("ParameterLoadFailureDialog"));
  box->setAttribute(Qt::WA_DeleteOnClose);
  box->open();
}
} // namespace

/** Open a legacy or archive parameter file chosen by the user.
   Prompts for unsaved changes first, then delegates to the transactional
   parameter loader. On success, re-initialises the image widget and renderer,
   clears undo history, and refreshes the UI. Failed parsing never mutates live
   document parameters. */
void MainWindow::onOpenParameters() {
  // Check for unsaved changes before loading new parameters
  if (!maybeSave()) {
    return;
  }

  QString initialPath = m_parameterFile.path;
  if (initialPath.isEmpty())
    initialPath =
        fileDialogDirectoryPreference(QStringLiteral("lastParameterDir"));

  auto *dialog = new QFileDialog(
      this, tr("Open Parameters"), initialPath,
      tr("Color-Screen parameters (*.cspar *.par);;Archive parameters (*.cspar);;Legacy parameters (*.par);;All Files (*)"));
  dialog->setObjectName(QStringLiteral("ParameterOpenFileDialog"));
  dialog->setFileMode(QFileDialog::ExistingFile);
  dialog->setAcceptMode(QFileDialog::AcceptOpen);
  dialog->setAttribute(Qt::WA_DeleteOnClose);

  connect(dialog, &QDialog::accepted, this, [this, dialog]() {
    const QStringList selected = dialog->selectedFiles();
    if (selected.isEmpty())
      return;
    const QString fileName = selected.constFirst();

    // Preserve the existing one-turn handoff so native file-dialog teardown is
    // complete before parameter parsing can present its own warning.
    QTimer::singleShot(0, this, [this, fileName]() {
      if (loadParameterFile(fileName)) {
        statusBar()->showMessage(
            QString("Parameters loaded from %1").arg(fileName), 3000);
      }
    });
  });
  dialog->open();
}

/** Save parameters to the current parameter target.
   A weak auto-suggested filename is confirmed through Save As before writing.
   Saving is synchronous so closeEvent can reliably decide whether it is safe
   to close this particular document window.  */
void MainWindow::onSaveParameters() {
  if (m_parameterFile.path.isEmpty() || m_parameterFile.suggested) {
    saveParametersAs();
    return;
  }
  saveParametersToFile(m_parameterFile.path);
}

/** Save parameters to a new legacy or archive file chosen by the user. */
void MainWindow::onSaveParametersAs() { saveParametersAs(); }

/** Atomically write the current document parameters and mark them saved. */
bool MainWindow::saveParametersToFile(const QString &fileName) {
  if (m_parameterSaveFailurePrompt) {
    QPointer<QMessageBox> obsolete = m_parameterSaveFailurePrompt;
    m_parameterSaveFailurePrompt.clear();
    obsolete->close();
  }

  const QString absoluteFileName = QFileInfo(fileName).absoluteFilePath();
  const bool preservingCurrentTarget =
      !m_parameterFile.suggested && !m_parameterFile.path.isEmpty() &&
      QFileInfo(m_parameterFile.path).absoluteFilePath() == absoluteFileName;
  const ParameterFileState::Format format =
      preservingCurrentTarget
          ? m_parameterFile.format
          : (absoluteFileName.endsWith(QLatin1String(".cspar"),
                                       Qt::CaseInsensitive)
                 ? ParameterFileState::Format::Archive
                 : ParameterFileState::Format::LegacyCsp);
  const bool archive = format == ParameterFileState::Format::Archive;
  const bool hasRgb = m_scan && m_scan->has_rgb();
  QString error;
  if (!saveParameterPayloadAtomically(
          absoluteFileName, archive, m_scrToImgParams,
          hasRgb ? &m_detectParams : nullptr, m_rparams, m_solverParams,
          m_profileSpots, &error)) {
    // The write result is synchronous because closeEvent needs it immediately,
    // but its explanation must not enter a nested event loop while close/save
    // policy is still on the stack. Veto the close first and let Qt present the
    // warning through the ordinary event loop.
    auto *message = new QMessageBox(
        QMessageBox::Critical, tr("Parameter Save Failed"),
        tr("Failed to save parameters to %1. The previous file was left "
           "unchanged.\n\n%2")
            .arg(absoluteFileName,
                 error.isEmpty() ? tr("Unknown write error.") : error),
        QMessageBox::Ok, this);
    message->setObjectName(QStringLiteral("ParameterSaveFailureDialog"));
    message->setAttribute(Qt::WA_DeleteOnClose);
    m_parameterSaveFailurePrompt = message;
    connect(message, &QMessageBox::finished, this, [this, message](int) {
      if (m_parameterSaveFailurePrompt == message)
        m_parameterSaveFailurePrompt.clear();
    });
    message->open();
    return false;
  }

  m_parameterFile.setLoaded(absoluteFileName, format);
  m_recoveryDirty = false;
  rememberFileDialogDirectory(QStringLiteral("lastParameterDir"),
                              absoluteFileName);
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
  QString initialPath = m_parameterFile.path;
  if (initialPath.isEmpty())
    initialPath =
        fileDialogDirectoryPreference(QStringLiteral("lastParameterDir"));
  const QString archiveFilter = tr("Archive parameters (*.cspar)");
  const QString legacyFilter = tr("Legacy parameters (*.par)");
  const QString allFilter = tr("All Files (*)");
  // A new document defaults to the versioned archive. Existing/suggested
  // targets keep their explicit physical format so Save As never silently
  // converts a legacy workflow merely because the new default changed.
  QString selectedFilter =
      m_parameterFile.path.isEmpty()
          ? archiveFilter
          : (m_parameterFile.format == ParameterFileState::Format::Archive
                 ? archiveFilter
                 : legacyFilter);
  QString fileName = QFileDialog::getSaveFileName(
      this, tr("Save Parameters"), initialPath,
      archiveFilter + QStringLiteral(";;") + legacyFilter +
          QStringLiteral(";;") + allFilter,
      &selectedFilter);
  if (fileName.isEmpty())
    return false;

  if (selectedFilter == legacyFilter) {
    if (!fileName.endsWith(QLatin1String(".par"), Qt::CaseInsensitive))
      fileName += QStringLiteral(".par");
  } else if (selectedFilter == archiveFilter) {
    if (!fileName.endsWith(QLatin1String(".cspar"), Qt::CaseInsensitive))
      fileName += QStringLiteral(".cspar");
  } else if (!fileName.endsWith(QLatin1String(".par"), Qt::CaseInsensitive) &&
             !fileName.endsWith(QLatin1String(".cspar"),
                                Qt::CaseInsensitive)) {
    fileName += QStringLiteral(".cspar");
  }
  return saveParametersToFile(fileName);
}

/** Atomically write a human-readable provenance snapshot followed by the exact
    current CSP/Qt parameter payload. This is a diagnostic export, not a new
    project-file format, and therefore never changes dirty/undo/save-target state. */
bool MainWindow::saveReproducibilityReportToFile(const QString &fileName,
                                                 QString *error) {
  updateWorkflowSummary();

  const ParameterState state = getCurrentState();
  const bool hasRgb = m_scan && m_scan->has_rgb();

  QJsonObject workflow;
  workflow.insert(QStringLiteral("process"),
                  m_workflowProcessLabel ? m_workflowProcessLabel->text()
                                         : QString());
  workflow.insert(QStringLiteral("image_layer"),
                  m_workflowImageLayerLabel ? m_workflowImageLayerLabel->text()
                                            : QString());
  workflow.insert(QStringLiteral("registration"),
                  m_workflowRegistrationLabel
                      ? m_workflowRegistrationLabel->text()
                      : QString());
  workflow.insert(QStringLiteral("sharpening_and_mtf"),
                  m_workflowCalibrationLabel
                      ? m_workflowCalibrationLabel->text()
                      : QString());
  workflow.insert(QStringLiteral("profile"),
                  m_workflowProfileLabel ? m_workflowProfileLabel->text()
                                         : QString());

  const QWidget *inspector = workspaceInspectorWidget();
  QJsonObject provenance;
  provenance.insert(
      QStringLiteral("flat_field"),
      reportStatusText(inspector, QStringLiteral("CaptureFlatFieldStatus")));
  provenance.insert(
      QStringLiteral("geometry"),
      reportStatusText(inspector, QStringLiteral("GeometryFitStatus")));
  provenance.insert(
      QStringLiteral("adaptive_sharpening"),
      reportStatusText(inspector,
                       QStringLiteral("SharpnessAdaptiveCorrectionStatus")));
  provenance.insert(
      QStringLiteral("mtf"),
      reportStatusText(inspector, QStringLiteral("MtfCalibrationStatus")));
  provenance.insert(
      QStringLiteral("profile"),
      reportStatusText(inspector, QStringLiteral("ProfileCalibrationStatus")));

  QString parameterState = QStringLiteral("none");
  if (!m_parameterFile.path.isEmpty())
    parameterState =
        m_parameterFile.suggested ? QStringLiteral("suggested")
                                  : QStringLiteral("loaded");

  QString applicationVersion = QCoreApplication::applicationVersion();
  if (applicationVersion.isEmpty())
    applicationVersion = QStringLiteral(PACKAGE_VERSION);

  QJsonObject metadata;
  metadata.insert(QStringLiteral("format"),
                  QStringLiteral("colorscreen-reproducibility-report"));
  metadata.insert(QStringLiteral("format_version"), 1);
  metadata.insert(QStringLiteral("application"), QStringLiteral("Color-Screen"));
  metadata.insert(QStringLiteral("application_version"), applicationVersion);
  metadata.insert(QStringLiteral("image_file"), m_currentImageFile);
  metadata.insert(QStringLiteral("parameter_file"), m_parameterFile.path);
  metadata.insert(QStringLiteral("parameter_file_state"), parameterState);
  metadata.insert(
      QStringLiteral("parameter_file_format"),
      m_parameterFile.format == ParameterFileState::Format::Archive
          ? QStringLiteral("archive")
          : QStringLiteral("legacy"));
  metadata.insert(QStringLiteral("document_modified"), isDocumentModified());
  metadata.insert(QStringLiteral("registration_point_count"),
                  static_cast<int>(state.solver.n_points()));
  metadata.insert(QStringLiteral("profile_spot_count"),
                  static_cast<int>(state.profileSpots.size()));
  metadata.insert(
      QStringLiteral("mtf_measurement_count"),
      static_cast<int>(state.rparams.sharpen.scanner_mtf.measurements.size()));

  QJsonObject renderOverrides;
  renderOverrides.insert(QStringLiteral("ignore_infrared"),
                         state.rparams.ignore_infrared);
  renderOverrides.insert(
      QStringLiteral("demosaiced_scaling"),
      QString::fromLatin1(
          colorscreen::render_parameters::demosaiced_scaling_names
              [static_cast<int>(state.rparams.demosaiced_scaling)]
                  .name));
  QJsonArray observerWhitepoint;
  observerWhitepoint.append(state.rparams.observer_whitepoint.x);
  observerWhitepoint.append(state.rparams.observer_whitepoint.y);
  renderOverrides.insert(QStringLiteral("observer_whitepoint"),
                         observerWhitepoint);
  renderOverrides.insert(
      QStringLiteral("output_profile"),
      QString::fromLatin1(
          colorscreen::render_output_parameters::output_profile_names
              [(int)colorscreen::render_output_parameters::output_profile_sRGB]));
  renderOverrides.insert(QStringLiteral("output_gamma"), -1);
  renderOverrides.insert(QStringLiteral("gamut_warning"), false);
  metadata.insert(QStringLiteral("render_overrides"), renderOverrides);

  QJsonObject geometryFinalFrame;
  geometryFinalFrame.insert(QStringLiteral("final_angle"),
                            state.scrToImg.final_angle);
  geometryFinalFrame.insert(QStringLiteral("final_ratio"),
                            state.scrToImg.final_ratio);
  metadata.insert(QStringLiteral("geometry_final_frame"), geometryFinalFrame);

  // The inner photographic bounds are not part of the trailing CSP mirror.
  QJsonObject photographicArea;
  photographicArea.insert(QStringLiteral("enabled"),
                          state.rparams.image_area.set);
  QJsonArray photographicRect;
  const auto &bounds = state.rparams.image_area;
  photographicRect.append(bounds.set ? bounds.x : 0);
  photographicRect.append(bounds.set ? bounds.y : 0);
  photographicRect.append(bounds.set ? bounds.width : 0);
  photographicRect.append(bounds.set ? bounds.height : 0);
  photographicArea.insert(QStringLiteral("rect"), photographicRect);
  metadata.insert(QStringLiteral("image_area"), photographicArea);

  metadata.insert(QStringLiteral("workflow"), workflow);
  metadata.insert(QStringLiteral("provenance"), provenance);

  const QByteArray json =
      QJsonDocument(metadata).toJson(QJsonDocument::Indented);
  const QByteArray title =
      QByteArrayLiteral("# Color-Screen reproducibility report\n"
                        "# Metadata (JSON)\n");
  const QByteArray payloadMarker =
      QByteArrayLiteral(
          "\n# Legacy-compatible Color-Screen parameter payload follows.\n"
          "# Structured-only render, final geometry and photographic bounds are in JSON metadata.\n"
          "# The legacy payload starts at the next screen_alignment_version line.\n");

  const QString absoluteFileName = QFileInfo(fileName).absoluteFilePath();
  return qtgui_io::saveStdioAtomically(
      absoluteFileName,
      [title, json, payloadMarker, state, hasRgb](FILE *staged) {
        if (std::fwrite(title.constData(), 1, static_cast<size_t>(title.size()),
                        staged) != static_cast<size_t>(title.size()) ||
            std::fwrite(json.constData(), 1, static_cast<size_t>(json.size()),
                        staged) != static_cast<size_t>(json.size()) ||
            std::fwrite(payloadMarker.constData(), 1,
                        static_cast<size_t>(payloadMarker.size()), staged) !=
                static_cast<size_t>(payloadMarker.size()))
          return false;

        return colorscreen::save_csp_with_profile_spots(
            staged, &state.scrToImg, hasRgb ? &state.detect : nullptr,
            &state.rparams, &state.solver, state.profileSpots);
      },
      error);
}

/** Choose a report destination asynchronously and save a provenance snapshot. */
void MainWindow::onSaveReproducibilityReport() {
  if (QFileDialog *existing =
          findChild<QFileDialog *>(
              QStringLiteral("ReproducibilityReportFileDialog"))) {
    existing->raise();
    existing->activateWindow();
    return;
  }

  QString baseName;
  QString sourceDirectory;
  if (!m_currentImageFile.isEmpty()) {
    const QFileInfo imageInfo(m_currentImageFile);
    baseName = imageInfo.completeBaseName();
    sourceDirectory = imageInfo.absolutePath();
  } else if (!m_parameterFile.path.isEmpty()) {
    const QFileInfo parameterInfo(m_parameterFile.path);
    baseName = parameterInfo.completeBaseName();
    sourceDirectory = parameterInfo.absolutePath();
  }
  if (baseName.isEmpty())
    baseName = QStringLiteral("colorscreen");

  QString directory =
      fileDialogDirectoryPreference(QStringLiteral("lastReportDir"));
  if (directory.isEmpty())
    directory = sourceDirectory;

  const QString suggested =
      QDir(directory).filePath(baseName + QStringLiteral("-report.txt"));
  auto *dialog = new QFileDialog(
      this, tr("Save Reproducibility Report"), suggested,
      tr("Text report (*.txt);;All Files (*)"));
  dialog->setObjectName(QStringLiteral("ReproducibilityReportFileDialog"));
  dialog->setAcceptMode(QFileDialog::AcceptSave);
  dialog->setFileMode(QFileDialog::AnyFile);
  dialog->setDefaultSuffix(QStringLiteral("txt"));
  dialog->setAttribute(Qt::WA_DeleteOnClose);

  connect(dialog, &QDialog::accepted, this, [this, dialog]() {
    const QStringList selected = dialog->selectedFiles();
    if (selected.isEmpty())
      return;
    const QString fileName = selected.constFirst();

    // Let the native chooser tear down before a write error can present a
    // nonblocking warning.
    QTimer::singleShot(0, this, [this, fileName]() {
      QString error;
      if (!saveReproducibilityReportToFile(fileName, &error)) {
        auto *message = new QMessageBox(
            QMessageBox::Critical, tr("Reproducibility Report Save Failed"),
            tr("Failed to save the reproducibility report to %1. Any previous "
               "file at that path was left unchanged.\n\n%2")
                .arg(QFileInfo(fileName).absoluteFilePath(),
                     error.isEmpty() ? tr("Unknown write error.") : error),
            QMessageBox::Ok, this);
        message->setObjectName(
            QStringLiteral("ReproducibilityReportSaveFailureDialog"));
        message->setAttribute(Qt::WA_DeleteOnClose);
        message->open();
        return;
      }

      rememberFileDialogDirectory(QStringLiteral("lastReportDir"), fileName);
      statusBar()->showMessage(
          tr("Reproducibility report saved to %1")
              .arg(QFileInfo(fileName).absoluteFilePath()),
          3000);
    });
  });
  dialog->open();
}

/** Show a multi-selection file dialog and open each image independently.
   The application may reuse this window only when it is untouched and empty;
   otherwise every selected image receives a new MainWindow.  Dispatch is
   deferred by one event-loop turn so KDE can dispose of KIO file-dialog jobs
   before an associated parameter prompt is shown.  */
void MainWindow::onOpenImage() {
  const QString startDirectory =
      fileDialogDirectoryPreference(QStringLiteral("lastOpenDir"));

  auto *dialog = new QFileDialog(
      this, tr("Open Images"), startDirectory,
      tr("Images (*.tif *.tiff *.jpg *.jpeg *.jp2 *.j2k *.jpc *.jpf *.jpx "
         "*.png *.raw *.dng *.iiq *.nef *.cr2 *.eip *.arw *.raf *.arq "
         "*.csprj);;All Files (*)"));
  dialog->setObjectName(QStringLiteral("ImageOpenFileDialog"));
  dialog->setFileMode(QFileDialog::ExistingFiles);
  dialog->setAcceptMode(QFileDialog::AcceptOpen);
  dialog->setAttribute(Qt::WA_DeleteOnClose);

  connect(dialog, &QDialog::accepted, this, [this, dialog]() {
    const QStringList fileNames = dialog->selectedFiles();
    if (fileNames.isEmpty())
      return;

    rememberFileDialogDirectory(QStringLiteral("lastOpenDir"),
                                fileNames.constFirst());
    const QPointer<MainWindow> guardedWindow(this);
    // Keep the KDE/KIO compatibility handoff: let the native chooser finish
    // teardown before any image-sidecar prompt is allowed to appear.
    QTimer::singleShot(0, qApp, [guardedWindow, fileNames]() {
      if (!guardedWindow)
        return;
      if (ColorScreenApplication *application = documentApplication())
        application->openFiles(fileNames, guardedWindow);
      else
        guardedWindow->loadFile(fileNames.constFirst());
    });
  });
  dialog->open();
}

/** Post-load initialisation after a new image has been opened.
   Updates the render mode menu, feeds the image to NavigationView, shows/hides
   the Tiles tab based on stitch data, shows/hides Profile and ImageLayer tabs
   based on RGB availability, refreshes all panel state, and enables the Render
   action. Geometry/profile workers receive source scans only in immutable
   request snapshots. */
void MainWindow::onImageLoaded() {
  clearFocusAreaAnalysis();
  // Update UI components that depend on loaded image
  updateModeMenu();
  if (m_scan) {
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
  // A preferred specialist stage can be hidden while the document is empty.
  // Re-apply it only after image/capture-dependent tab visibility is known.
  restorePreferredInspectorPanel();
  m_renderAction->setEnabled(m_scan != nullptr);
}

/** Persist one application-wide recent-items list. */
void MainWindow::saveRecentItems(const RecentItemsState &state) {
  QSettings settings;
  settings.setValue(state.settingsKey, state.items);
}

/** Rebuild one recent-items submenu from its application-wide list. */
void MainWindow::updateRecentItemsActions(RecentItemsState &state,
                                          RecentItemHandler handler) {
  if (!state.menu)
    return;

  state.menu->clear();
  for (int i = 0; i < state.items.size(); ++i) {
    const QString path = state.items[i];
    QString fileName = QFileInfo(path).fileName();
    fileName.replace(QLatin1Char('&'), QStringLiteral("&&"));
    QAction *action = state.menu->addAction(
        tr("&%1 %2").arg(i + 1).arg(fileName));
    action->setData(path);
    action->setToolTip(path);
    connect(action, &QAction::triggered, this,
            [this, handler, path]() { (this->*handler)(path); });
  }

  if (state.items.isEmpty()) {
    state.menu->addAction(state.emptyLabel)->setEnabled(false);
    return;
  }

  state.menu->addSeparator();
  QAction *clearAction = state.menu->addAction(state.clearLabel);
  RecentItemsState *const statePtr = &state;
  connect(clearAction, &QAction::triggered, this,
          [this, statePtr, handler]() {
            statePtr->items.clear();
            saveRecentItems(*statePtr);
            updateRecentItemsActions(*statePtr, handler);
          });
}

/** Reload one recent-items list from QSettings and rebuild its menu. */
void MainWindow::loadRecentItems(RecentItemsState &state,
                                 RecentItemHandler handler) {
  QSettings settings;
  state.items = settings.value(state.settingsKey).toStringList();
  updateRecentItemsActions(state, handler);
}

/** Add PATH to one MRU list without overwriting entries from another window. */
void MainWindow::addToRecentItems(RecentItemsState &state,
                                  const QString &filePath,
                                  RecentItemHandler handler) {
  QSettings settings;
  state.items = settings.value(state.settingsKey).toStringList();
  const QString absolutePath = QFileInfo(filePath).absoluteFilePath();
  state.items.removeAll(absolutePath);
  state.items.prepend(absolutePath);
  while (state.items.size() > MaxRecentFiles)
    state.items.removeLast();

  saveRecentItems(state);
  updateRecentItemsActions(state, handler);
}

void MainWindow::addToRecentFiles(const QString &filePath) {
  addToRecentItems(m_recentFiles, filePath, &MainWindow::openRecentFile);
}

void MainWindow::updateRecentFileActions() {
  updateRecentItemsActions(m_recentFiles, &MainWindow::openRecentFile);
}

/** Open a recent image without replacing an occupied document window. */
void MainWindow::openRecentFile(const QString &fileName) {
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
    m_imageLoad.screenAutodetectAfterGeneration = m_imageLoad.generation + 1;
  else
    m_imageLoad.screenAutodetectAfterGeneration.reset();

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

/** Load an image file and optionally its associated parameter sidecar.
   If SUPPRESSPARAMPROMPT is false, the optional sidecar decision is presented
   asynchronously and owned by this image-load generation. An accepted sidecar
   is parsed into private staging state and may guide image decoding, but is
   published only after the image itself loads successfully.
   Declined/invalid/missing sidecars likewise stage only a later Save-As
   suggestion. A failed image therefore adopts neither the requested image
   target nor sidecar state/target.
   The actual image loading runs asynchronously via QtConcurrent::run; on
   completion, the scan is set on ImageWidget, stitch tile loading is launched
   in parallel for .csprj projects, and undo history is cleared.  */
void MainWindow::loadFile(const QString &fileName, bool suppressParamPrompt) {
  if (fileName.isEmpty())
    return;

  const QString requestedImageFile = QFileInfo(fileName).absoluteFilePath();
  const QString outgoingImageFile = m_currentImageFile;
  const auto outgoingScan = m_scan;
  const bool restoreOutgoingImage = static_cast<bool>(outgoingScan);

  struct SidecarLoadStaging {
    std::optional<ParameterState> state;
    std::vector<colorscreen::color_match> spotResults;
    QString loadedPath;
    bool loadedArchive = false;
    QString suggestedPath;
    bool suggestedArchive = false;
    std::optional<ParameterState> baseline;
  };
  auto sidecarStaging = std::make_shared<SidecarLoadStaging>();
  if (!suppressParamPrompt)
    sidecarStaging->baseline = getCurrentState();

  // A failed older load may still have a non-blocking warning open. Starting a
  // new image request supersedes that presentation just like it supersedes the
  // old worker/result generation.
  if (m_imageLoad.failurePrompt) {
    m_imageLoad.failurePrompt->close();
    m_imageLoad.failurePrompt.clear();
  }
  if (m_imageLoad.sidecarPrompt) {
    QPointer<QMessageBox> obsolete = m_imageLoad.sidecarPrompt;
    m_imageLoad.sidecarPrompt.clear();
    obsolete->close();
  }

  // Final-result work and any pending one-shot confirmation belong to the
  // current image snapshot. Invalidate both before starting replacement I/O.
  dismissOneShotPrompts();
  m_oneShotOperations.cancelAll();
  // Geometry/profile optimization are image-backed TaskQueue jobs. Disown
  // pending publication immediately; each worker keeps its captured source scan
  // alive until it unwinds.
  m_solverQueue.cancelAll();
  m_geometryFit.clearRequest();
  m_colorOptimizerQueue.cancelAll();
  m_profileCalibration.pendingInputs.reset();
  m_profileCalibration.pendingRequestId.reset();
  const uint64_t loadGeneration = ++m_imageLoad.generation;
  if (m_imageLoad.screenAutodetectAfterGeneration &&
      *m_imageLoad.screenAutodetectAfterGeneration != loadGeneration)
    m_imageLoad.screenAutodetectAfterGeneration.reset();
  m_imageLoad.pending = true;

  // Progressive workers retain the outgoing m_scan until they unwind.  Marking
  // image replacement pending is therefore part of their staleness contract:
  // preserve batches/results already accepted before this user gesture, but
  // reject/cancel every later point, geometry or adaptive-chart publication.
  const ParameterState replacementState = getCurrentState();
  cancelStaleAdaptiveSharpening(replacementState);
  cancelStaleRegistrationDiscovery(replacementState);

  m_currentImageFile = requestedImageFile;
  updateWindowTitle();

  // Clear current image and stop rendering. Processing panels deliberately
  // see no usable source while replacement is pending even though m_scan keeps
  // the outgoing image alive for transactional reload failure recovery.
  m_imageWidget->setImage(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
  refreshImageAvailabilityPresentation();

  // Continue into the actual image worker only after the optional sidecar
  // decision has settled. The continuation is generation-gated because the
  // non-blocking question may remain open while another image load supersedes
  // this request.
  const auto startImageRead =
      [this, requestedImageFile, outgoingImageFile, outgoingScan,
       restoreOutgoingImage, sidecarStaging, suppressParamPrompt,
       loadGeneration]() {
        if (m_closeLifecycle.closing() ||
            loadGeneration != m_imageLoad.generation)
          return;

        const bool parameterDataLoaded = sidecarStaging->state.has_value();
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
        // A staged sidecar may select the demosaic algorithm needed to decode the
        // image, even though the rest of its state remains private until success.
        const colorscreen::image_data::demosaicing_t demosaic =
            sidecarStaging->state ? sidecarStaging->state->rparams.demosaic
                                  : m_rparams.demosaic;

        bool isCsprj =
            requestedImageFile.endsWith(QLatin1String(".csprj"), Qt::CaseInsensitive);

        QFutureWatcher<std::pair<bool, QString>> *watcher =
            new QFutureWatcher<std::pair<bool, QString>>(this);
        connect(
            watcher, &QFutureWatcher<std::pair<bool, QString>>::finished, this,
            [this, watcher, tempScan, progress, isCsprj,
             allowInitialGuide, suggestDetectedMetadata, loadGeneration, outgoingScan,
             outgoingImageFile, restoreOutgoingImage, sidecarStaging,
             suppressParamPrompt]() {
              if (m_closeLifecycle.closing()) {
                watcher->deleteLater();
                return;
              }

              const std::pair<bool, QString> result = watcher->result();
              removeProgress(progress);
              watcher->deleteLater();

              // Reloading (notably after changing demosaic mode) can start another
              // asynchronous image load before this one finishes. Only the newest
              // generation may clear the pending state or replace the document scan.
              if (loadGeneration != m_imageLoad.generation)
                return;

              const bool autodetectScreenAfterLoad =
                  m_imageLoad.screenAutodetectAfterGeneration &&
                  *m_imageLoad.screenAutodetectAfterGeneration == loadGeneration;
              if (autodetectScreenAfterLoad)
                m_imageLoad.screenAutodetectAfterGeneration.reset();

              m_imageLoad.pending = false;

              if (result.first) {
                const bool liveEditsWhileLoading =
                    !suppressParamPrompt && sidecarStaging->baseline &&
                    getCurrentState() != *sidecarStaging->baseline;

                if (!suppressParamPrompt) {
                  if (sidecarStaging->state) {
                    // Do not let a delayed image completion overwrite edits the user
                    // made while loading. In that uncommon case the image still opens
                    // using the staged demosaic, but the sidecar remains only a safe
                    // suggested parameter target.
                    const bool unchanged =
                        sidecarStaging->baseline &&
                        getCurrentState() == *sidecarStaging->baseline;
                    if (unchanged) {
                      ParameterState sidecarState =
                          std::move(*sidecarStaging->state);
                      m_scrToImgParams = std::move(sidecarState.scrToImg);
                      m_detectParams = std::move(sidecarState.detect);
                      m_rparams = std::move(sidecarState.rparams);
                      m_solverParams = std::move(sidecarState.solver);
                      m_profileSpots = std::move(sidecarState.profileSpots);
                      m_profileCalibration.spotResults =
                          std::move(sidecarStaging->spotResults);
                      m_parameterFile.setLoaded(
                          sidecarStaging->loadedPath,
                          sidecarStaging->loadedArchive
                              ? ParameterFileState::Format::Archive
                              : ParameterFileState::Format::LegacyCsp);
                      addToRecentParams(sidecarStaging->loadedPath);

                      if (colorscreen::screen_geometry_configured_p(
                              m_scrToImgParams))
                        m_renderTypeParams.type =
                            colorscreen::render_type_interpolated;
                    } else {
                      m_parameterFile.setSuggested(
                          sidecarStaging->loadedPath,
                          sidecarStaging->loadedArchive
                              ? ParameterFileState::Format::Archive
                              : ParameterFileState::Format::LegacyCsp);
                      inspectorStatusBar()->showMessage(
                          tr("Image loaded; sidecar parameters were not applied "
                             "because settings changed while the image was loading."),
                          6000);
                    }
                  } else if (!sidecarStaging->suggestedPath.isEmpty()) {
                    m_parameterFile.setSuggested(
                        sidecarStaging->suggestedPath,
                        sidecarStaging->suggestedArchive
                            ? ParameterFileState::Format::Archive
                            : ParameterFileState::Format::LegacyCsp);
                  }
                }

                clearDetectedScreenDiagnostics();
                // A new/reloaded image establishes new geometry and colour-sampling
                // contexts. Persisted parameter values remain available, but accepted
                // session provenance from the replaced scan cannot carry across the
                // source-image boundary.
                m_geometryFit.clearAccepted();
                m_profileCalibration.clear();
                if (m_profilePanel)
                  m_profilePanel->setSpotResults(m_profileCalibration.spotResults);
                m_scan = tempScan;

                if ((int)m_scan->gamma != -2 && m_scan->gamma > 0 &&
                    m_rparams.gamma == -1) // Update only if unknown
                  m_rparams.gamma = m_scan->gamma;
                else if (m_rparams.gamma == -1)
                  m_rparams.gamma = -1;

                m_undoStack->clear();
                if (!suppressParamPrompt)
                  m_recoveryDirty = liveEditsWhileLoading;

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
                        if (m_closeLifecycle.closing() || loadGeneration != m_imageLoad.generation ||
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
                                if (m_closeLifecycle.closing()) {
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
                // A failed open never adopts its requested image target. In
                // particular, a fresh blank document remains reusable instead of
                // becoming permanently named after an image that never opened.
                m_currentImageFile = outgoingImageFile;

                // Image replacement is transactional with respect to presentation.
                // The old scan was deliberately retained in m_scan while loading, so
                // put it back on the primary canvas when this still-current request
                // fails instead of leaving a logically loaded document blank.
                if (restoreOutgoingImage && outgoingScan && m_scan == outgoingScan) {
                  m_imageWidget->setImage(
                      outgoingScan, &m_rparams, &m_scrToImgParams, &m_detectParams,
                      &m_renderTypeParams, &m_solverParams);
                  m_imageWidget->update();
                }

                // Re-enable image-backed controls after either restoring the outgoing
                // scan or completing an ordinary failed open with no source.
                refreshImageAvailabilityPresentation();
                updateWindowTitle();
                if (!progress->cancelled()) {
                  QString messageText =
                      result.second.isEmpty() ? tr("Failed to load image.")
                                              : result.second;
                  if (restoreOutgoingImage && outgoingScan && m_scan == outgoingScan)
                    messageText +=
                        tr("\n\nThe previous image remains open.");
                  auto *message = new QMessageBox(
                      QMessageBox::Critical, tr("Error Loading Image"),
                      messageText, QMessageBox::Ok, this);
                  message->setObjectName(QStringLiteral("ImageLoadFailureDialog"));
                  message->setAttribute(Qt::WA_DeleteOnClose);
                  m_imageLoad.failurePrompt = message;
                  connect(message, &QMessageBox::finished, this,
                          [this, message](int) {
                            if (m_imageLoad.failurePrompt == message)
                              m_imageLoad.failurePrompt.clear();
                          });
                  message->open();
                }
              }
            });

        const QString absolutePath = requestedImageFile;
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
  };

  // Prefer the new archive sidecar when both formats are present; never
  // merge archive and legacy sidecars. Retain legacy .par fallback and
  // legacy-first suggestions until the archive rollout gate is complete. The
  // question is presentation state of this exact image-load generation: never
  // enter a nested event loop, and never let an obsolete prompt launch decoding
  // after a newer Open/Reload request has taken ownership.
  if (!suppressParamPrompt) {
    const QFileInfo fileInfo(requestedImageFile);
    const QString base =
        fileInfo.path() + "/" + fileInfo.completeBaseName();
    const QString archiveFile = base + QStringLiteral(".cspar");
    const QString legacyFile = base + QStringLiteral(".par");
    const bool haveArchive = QFile::exists(archiveFile);
    const QString sidecarFile = haveArchive ? archiveFile : legacyFile;
    const bool haveSidecar = haveArchive || QFile::exists(legacyFile);

    if (haveSidecar) {
      auto *question = new QMessageBox(
          QMessageBox::Question, tr("Load Parameters?"),
          tr("A parameter file was found for this image:\n%1\n\n"
             "Do you want to load it?")
              .arg(QFileInfo(sidecarFile).fileName()),
          QMessageBox::Yes | QMessageBox::No, this);
      question->setObjectName(QStringLiteral("ImageSidecarLoadPrompt"));
      question->setDefaultButton(QMessageBox::Yes);
      question->setEscapeButton(QMessageBox::No);
      question->setAttribute(Qt::WA_DeleteOnClose);
      m_imageLoad.sidecarPrompt = question;

      connect(
          question, &QMessageBox::finished, this,
          [this, question, sidecarFile, haveArchive, sidecarStaging,
           loadGeneration, startImageRead](int result) {
            const bool ownsPrompt = m_imageLoad.sidecarPrompt == question;
            if (ownsPrompt)
              m_imageLoad.sidecarPrompt.clear();
            if (!ownsPrompt || m_closeLifecycle.closing() ||
                loadGeneration != m_imageLoad.generation)
              return;

            if (result == QMessageBox::Yes) {
              ParameterState sidecarState;
              std::vector<colorscreen::color_match> sidecarSpotResults;
              bool sidecarArchive = false;
              QString loadError;
              if (!loadParameterPayload(sidecarFile, &sidecarState,
                                        &sidecarSpotResults, &sidecarArchive,
                                        &loadError)) {
                // Parsing failed, so the named sidecar is at most a later
                // Save-As suggestion. Publish that suggestion only if the
                // image itself opens successfully.
                sidecarStaging->suggestedPath = sidecarFile;
                sidecarStaging->suggestedArchive = haveArchive;
                showParameterLoadFailure(this, loadError);
              } else {
                sidecarStaging->state = std::move(sidecarState);
                sidecarStaging->spotResults = std::move(sidecarSpotResults);
                sidecarStaging->loadedPath = sidecarFile;
                sidecarStaging->loadedArchive = sidecarArchive;
              }
            } else {
              // Declining the optional question retains the chosen existing
              // sidecar only as a format-aware Save-As suggestion.
              sidecarStaging->suggestedPath = sidecarFile;
              sidecarStaging->suggestedArchive = haveArchive;
            }

            startImageRead();
          });
      question->open();
    } else {
      // No sidecar exists. The post-migration natural Save-As target is the
      // versioned archive; explicit/declined legacy sidecars above still retain
      // LegacyCsp identity.
      sidecarStaging->suggestedPath = archiveFile;
      sidecarStaging->suggestedArchive = true;
      startImageRead();
    }
  } else {
    startImageRead();
  }
}

/** Load the recent image files list from QSettings. */
void MainWindow::loadRecentFiles() {
  loadRecentItems(m_recentFiles, &MainWindow::openRecentFile);
}

/** Return whether this document has parameters not represented by its saved
    parameter target. Recovered state remains dirty even though the
    reconstructed undo stack starts empty. */
bool MainWindow::isDocumentModified() const {
  return m_recoveryDirty || (m_undoStack && !m_undoStack->isClean());
}

/** Return whether a new image may safely reuse this document window. */
bool MainWindow::canReuseForOpen() const {
  return !m_closeLifecycle.closing() && !m_scan && !m_imageLoad.pending &&
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
    if (m_parameterFile.path.isEmpty() || m_parameterFile.suggested)
      return saveParametersAs();
    return saveParametersToFile(m_parameterFile.path);
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
  if (m_closeLifecycle.closing() || m_closeLifecycle.preflightApproved())
    return true;
  if (!confirmClose())
    return false;
  m_closeLifecycle.approvePreflight();
  return true;
}

/** Forget a preflight approval when another document vetoes File -> Exit. */
void MainWindow::cancelPreparedApplicationClose() {
  m_closeLifecycle.cancelPreflight();
}

/** Handle closing one image-document window.
   Prompts for this document's unsaved changes, asks to cancel its active
   render, cancels only its background tasks, removes only its recovery data,
   and saves the shared preferred window layout.  */
void MainWindow::closeEvent(QCloseEvent *event) {
  if (m_closeLifecycle.closing()) {
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

  if (m_closeLifecycle.consumePreflight()) {
    // File -> Exit already resolved every user-visible veto before it started
    // tearing down secondary views. Consume the one-shot approval here.
  } else if (!confirmClose()) {
    event->ignore();
    return;
  }

  m_closeLifecycle.beginClosing();
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

void MainWindow::addToRecentParams(const QString &filePath) {
  addToRecentItems(m_recentParams, filePath, &MainWindow::openRecentParams);
}

void MainWindow::updateRecentParamsActions() {
  updateRecentItemsActions(m_recentParams, &MainWindow::openRecentParams);
}

/** Load one recent parameter file after resolving unsaved document state. */
void MainWindow::openRecentParams(const QString &fileName) {
  if (maybeSave() && loadParameterFile(fileName))
    statusBar()->showMessage(
        QString("Parameters loaded from %1").arg(fileName), 3000);
}

/** Load the recent parameter files list from QSettings. */
void MainWindow::loadRecentParams() {
  loadRecentItems(m_recentParams, &MainWindow::openRecentParams);
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
  const QString paramsPath =
      directory.filePath(QStringLiteral("recovery_params.cspar"));
  const bool hasRgb = m_scan->has_rgb();
  QString paramsError;
  if (!saveParameterPayloadAtomically(
          paramsPath, true, m_scrToImgParams,
          hasRgb ? &m_detectParams : nullptr, m_rparams, m_solverParams,
          m_profileSpots, &paramsError)) {
    // Preserve the previous usable recovery snapshot and its image/target
    // metadata rather than publishing metadata for a snapshot we did not save.
    qWarning() << "Could not atomically save recovery parameters to" << paramsPath
               << paramsError;
    return;
  }

  // A newly complete archive supersedes the old per-document legacy snapshot.
  // Remove it only after archive commit succeeds, so an interrupted migration
  // can still restore the older snapshot.
  const QString oldParamsPath =
      directory.filePath(QStringLiteral("recovery_params.par"));
  if (QFile::exists(oldParamsPath) && !QFile::remove(oldParamsPath))
    qWarning() << "Could not remove superseded recovery parameters"
               << oldParamsPath;

  saveRecoveryTextAtomically(
      directory.filePath(QStringLiteral("recovery_image.txt")),
      m_currentImageFile);

  const QString meta =
      m_parameterFile.path + QLatin1Char('\n') +
      (m_parameterFile.suggested ? QStringLiteral("1\n")
                                 : QStringLiteral("0\n")) +
      (isDocumentModified() ? QStringLiteral("1\n")
                            : QStringLiteral("0\n")) +
      (m_parameterFile.format == ParameterFileState::Format::Archive
           ? QStringLiteral("archive\n")
           : QStringLiteral("legacy\n"));
  if (!saveRecoveryTextAtomically(
          directory.filePath(QStringLiteral("recovery_params_meta.txt")),
          meta)) {
    qWarning() << "Could not atomically save recovery metadata in"
               << m_recoveryDir;
  }
}

/** Restore this document from its private recovery payload.

   Prefer complete .cspar snapshots, falling back to old .par snapshots only
   when no archive exists. Parse transactionally, preserve structured render
   values, and keep invalid/incomplete recoveries dirty. Returns false only
   when the directory contains no usable recovery reference. */
bool MainWindow::restoreRecoveryState() {
  if (m_recoveryDir.isEmpty())
    return false;

  const QDir directory(m_recoveryDir);
  const QString imagePath =
      directory.filePath(QStringLiteral("recovery_image.txt"));
  const QString archivePath =
      directory.filePath(QStringLiteral("recovery_params.cspar"));
  const QString legacyPath =
      directory.filePath(QStringLiteral("recovery_params.par"));
  const bool hasArchiveSnapshot = QFile::exists(archivePath);
  const QString paramsPath =
      hasArchiveSnapshot ? archivePath : legacyPath;
  if (!QFile::exists(imagePath) && !QFile::exists(paramsPath))
    return false;

  QStringList recoveryWarnings;
  QString imageToLoad;
  QFile imageFile(imagePath);
  if (imageFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&imageFile);
    imageToLoad = in.readLine().trimmed();
  } else if (QFile::exists(imagePath)) {
    recoveryWarnings.push_back(
        tr("The recovered image reference could not be read."));
  }

  bool parametersRecovered = false;
  if (QFile::exists(paramsPath)) {
    // A present archive is authoritative. Never silently fall back to an
    // obsolete .par if the archive is corrupt: that would recover stale state.
    // The shared loader parses into private defaults, validates the full ZIP
    // manifest and Qt postamble, then applies required structured render state.
    ParameterState recoveredState;
    std::vector<colorscreen::color_match> recoveredSpotResults;
    bool recoveredArchive = false;
    QString loadError;
    if (!loadParameterPayload(paramsPath, &recoveredState,
                              &recoveredSpotResults, &recoveredArchive,
                              &loadError)) {
      recoveryWarnings.push_back(
          loadError.isEmpty()
              ? tr("The recovered parameter payload is invalid. "
                   "No recovered parameters were applied.")
              : tr("The recovered parameter payload is invalid: %1\n"
                   "No recovered parameters were applied.")
                    .arg(loadError));
    } else {
      m_scrToImgParams = std::move(recoveredState.scrToImg);
      m_detectParams = std::move(recoveredState.detect);
      m_rparams = std::move(recoveredState.rparams);
      m_solverParams = std::move(recoveredState.solver);
      m_profileSpots = std::move(recoveredState.profileSpots);
      m_profileCalibration.spotResults = std::move(recoveredSpotResults);
      parametersRecovered = true;
    }
  }

  m_recoveryDirty = true; // Legacy recovery metadata has no dirty flag.
  QFile metaFile(
      directory.filePath(QStringLiteral("recovery_params_meta.txt")));
  if (metaFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&metaFile);
    const QString recoveredParameterPath = in.readLine().trimmed();
    const bool recoveredParameterPathSuggested =
        (in.readLine().trimmed() == QLatin1String("1"));
    const QString dirtyFlag = in.readLine().trimmed();
    const QString recoveredFormat = in.readLine().trimmed();
    const ParameterFileState::Format format =
        recoveredFormat == QLatin1String("archive") ||
                (recoveredFormat.isEmpty() &&
                 recoveredParameterPath.endsWith(
                     QLatin1String(".cspar"), Qt::CaseInsensitive))
            ? ParameterFileState::Format::Archive
            : ParameterFileState::Format::LegacyCsp;
    if (recoveredParameterPathSuggested)
      m_parameterFile.setSuggested(recoveredParameterPath, format);
    else
      m_parameterFile.setLoaded(recoveredParameterPath, format);
    if (!dirtyFlag.isEmpty())
      m_recoveryDirty = (dirtyFlag == QLatin1String("1"));
  }

  // Neither a failed recovery nor an old CSP-only recovery can certify that
  // all current structured render fields were restored. The old metadata
  // clean bit must not allow a later ordinary Save to silently replace the
  // user's complete .cspar state with legacy defaults.
  if (!parametersRecovered || !hasArchiveSnapshot)
    m_recoveryDirty = true;

  if (!imageToLoad.isEmpty() && QFile::exists(imageToLoad)) {
    loadFile(imageToLoad, true);
  } else if (!imageToLoad.isEmpty()) {
    recoveryWarnings.push_back(
        tr("The recovered image file could not be found: %1")
            .arg(imageToLoad));
  }

  if (!recoveryWarnings.isEmpty()) {
    auto *box = new QMessageBox(
        QMessageBox::Warning, tr("Crash Recovery"),
        tr("Color-Screen could not restore all recovery data. "
           "Usable recovered state was kept, and the recovery files were left "
           "untouched so they can be inspected or retried.\n\n%1")
            .arg(recoveryWarnings.join(QStringLiteral("\n\n"))),
        QMessageBox::Ok, this);
    box->setObjectName(QStringLiteral("RecoveryWarningDialog"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
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

/** Load legacy or archive parameters and update all UI components.
   Parses into private default state because load_csp merges into its outputs;
   only a complete payload is published. Updates ImageWidget, NavigationView,
   gamut warning, undo history, and all panels. Returns true on success. */
bool MainWindow::loadParameterFile(const QString &fileName) {
  // Loading external parameters invalidates every final-result state snapshot
  // and any one-shot confirmation waiting on the old parameters.
  dismissOneShotPrompts();
  m_oneShotOperations.cancelAll();

  ParameterState loadedState;
  std::vector<colorscreen::color_match> loadedSpotResults;
  bool loadedArchive = false;
  QString loadError;
  if (!loadParameterPayload(fileName, &loadedState, &loadedSpotResults,
                            &loadedArchive, &loadError)) {
    showParameterLoadFailure(this, loadError);
    return false;
  }

  m_scrToImgParams = std::move(loadedState.scrToImg);
  m_detectParams = std::move(loadedState.detect);
  m_rparams = std::move(loadedState.rparams);
  m_solverParams = std::move(loadedState.solver);
  m_profileSpots = std::move(loadedState.profileSpots);
  m_profileCalibration.spotResults = std::move(loadedSpotResults);

  // Successful external parameter load establishes a new calibration context.
  m_solverQueue.cancelAll();
  m_geometryFit.clear();
  m_mtfFit.clear();
  m_colorOptimizerQueue.cancelAll();
  m_profileCalibration.clear();
  m_flatFieldCalibration.clear();
  m_adaptiveSharpening.clearAccepted();

  // Update UI/Renderer
  if (m_scan) {
    m_imageWidget->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                            &m_detectParams, &m_renderTypeParams,
                            &m_solverParams);
    syncProfileSpotOverlay(m_imageWidget);
    if (m_profilePanel)
      m_profilePanel->setSpotResults(m_profileCalibration.spotResults);
    m_navigationView->setImage(m_scan, &m_rparams, &m_scrToImgParams,
                               &m_detectParams);
    updateColorCheckBoxState();
  }

  // Loading document processing state must not reset this view's output
  // colours or its gamut diagnostics.
  syncInspectorViewActions();

  if (m_undoStack)
    m_undoStack->clear();
  m_recoveryDirty = false;

  const QString absoluteFileName = QFileInfo(fileName).absoluteFilePath();
  m_parameterFile.setLoaded(
      absoluteFileName,
      loadedArchive ? ParameterFileState::Format::Archive
                    : ParameterFileState::Format::LegacyCsp);
  rememberFileDialogDirectory(QStringLiteral("lastParameterDir"),
                              absoluteFileName);

  updateModeMenu();
  updateUIFromState(getCurrentState());
  addToRecentParams(absoluteFileName);
  updateWindowTitle();
  saveRecoveryState();

  return true;
}
