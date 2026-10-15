#include "ColorScreenApplication.h"

#include "MainWindow.h"
#include "ImageWidget.h"
#include "../libcolorscreen/include/cache-stats.h"
#include "../libcolorscreen/parameter-archive.h"
#include "ImageViewWindow.h"
#include "WorkspaceWindow.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QLineEdit>
#include <QListWidget>
#include <QFileOpenEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QKeySequence>
#include <QMenu>
#include <QMessageBox>
#include <QPoint>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>
#include <QVBoxLayout>
#include <QUrl>
#include <QUuid>

#include <algorithm>
#include <limits>

namespace {

/** Return the cache directory historically used by the Qt GUI. */
QString applicationCacheDirectory() {
  QString path = QDir::fromNativeSeparators(
      QStandardPaths::writableLocation(QStandardPaths::CacheLocation));

  // Preserve the pre-existing shared "colorscreen" cache location.  On some
  // platforms QStandardPaths appends an extra "cache" component, so replace
  // the application-name path component rather than assuming it is last.
  const QString oldComponent = QStringLiteral("/colorscreen-qt");
  const int componentStart = path.lastIndexOf(oldComponent);
  if (componentStart >= 0) {
    const int componentEnd = componentStart + oldComponent.size();
    if (componentEnd == path.size() || path.at(componentEnd) == QLatin1Char('/'))
      path.replace(componentStart + 1, oldComponent.size() - 1,
                   QStringLiteral("colorscreen"));
  }
  return QDir::cleanPath(path);
}

/** Return the root that contains one crash-recovery directory per document. */
QString recoveryRootDirectory() {
  return QDir(applicationCacheDirectory())
      .filePath(QStringLiteral("recovery-sessions"));
}

/** Return a fresh path for one document's crash-recovery payload. */
QString newRecoveryDirectory() {
  return QDir(recoveryRootDirectory())
      .filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

/** Return the legacy pre-multi-document recovery file path NAME. */
QString legacyRecoveryFile(const QString &name) {
  return QDir(applicationCacheDirectory()).filePath(name);
}

/** Return the path of recovery payload NAME inside DIRECTORY. */
QString recoveryFile(const QString &directory, const QString &name) {
  return QDir(directory).filePath(name);
}

/** Return the per-document metadata file for auxiliary reference scans. */
QString recoveryReferencesFile(const QString &directory) {
  return recoveryFile(directory, QStringLiteral("recovery_references.json"));
}

/** Copy SOURCE to DESTINATION without removing the legacy payload. */
bool copyRecoveryFile(const QString &source, const QString &destination) {
  if (!QFile::exists(source))
    return true;
  return QFile::copy(source, destination);
}

/** Display a parent-owned, nonblocking Preferences validation warning.
    Failed settings must leave the dialog open for correction, without a
    nested event loop that could dispatch unrelated image-worker callbacks. */
void showPreferencesWarning(QDialog *parent, const QString &title,
                            const QString &detail) {
  auto *warning = new QMessageBox(
      QMessageBox::Warning, title, detail, QMessageBox::Ok, parent);
  warning->setObjectName(QStringLiteral("PreferencesWarningDialog"));
  warning->setAttribute(Qt::WA_DeleteOnClose);
  warning->open();
}

} // namespace

/** Construct the application-level document manager. */
ColorScreenApplication::ColorScreenApplication(int &argc, char **argv)
    : QApplication(argc, argv) {
  setQuitOnLastWindowClosed(true);
}

/** Apply the persistent process-wide preferences before opening any image.
    Invalid/deleted scratch folders leave the OS default in place and can be
    corrected from Edit -> Preferences; no image parameter state is involved. */
void ColorScreenApplication::applySavedPreferences() {
  QSettings settings;
  const int percent =
      settings.value(QStringLiteral("preferences/cachePercent"), 0).toInt();
  if (!colorscreen::set_cache_memory_percent(percent)) {
    qWarning() << "Invalid saved cache-memory percentage; using Auto";
    colorscreen::set_cache_memory_percent(0);
  }

  const QString folder =
      settings.value(QStringLiteral("preferences/scratchDirectory")).toString();
  if (!colorscreen::set_cache_scratch_directory(
          QFile::encodeName(folder).toStdString())) {
    qWarning() << "The configured scratch folder is no longer usable:" << folder;
    colorscreen::set_cache_scratch_directory(std::string());
  }
}

/** Show a Photoshop-like, extensible preferences workspace: navigation on
    the left, independent settings pages on the right. Only OK applies settings
    globally, while Cancel and closing the window leave them unchanged. */
void ColorScreenApplication::showPreferences(QWidget *parent) {
  if (m_preferencesDialog) {
    m_preferencesDialog->show();
    m_preferencesDialog->raise();
    m_preferencesDialog->activateWindow();
    return;
  }

  QWidget *owner = parent ? parent->window() : activeWindow();
  auto *dialog = new QDialog(owner);
  m_preferencesDialog = dialog;
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setObjectName(QStringLiteral("ApplicationPreferencesDialog"));
  dialog->setWindowTitle(tr("Preferences — Color-Screen"));
  dialog->resize(755, 475);
  auto *outer = new QVBoxLayout(dialog);
  auto *body = new QHBoxLayout;
  body->setSpacing(16);
  auto *categories = new QListWidget(dialog);
  categories->setObjectName(QStringLiteral("PreferencesCategories"));
  categories->setFixedWidth(168);
  categories->setSpacing(4);
  body->addWidget(categories);
  auto *pages = new QStackedWidget(dialog);
  pages->setObjectName(QStringLiteral("PreferencesPages"));
  body->addWidget(pages, 1);
  outer->addLayout(body, 1);

  const auto createPage = [categories, pages, dialog](const QString &title) {
    categories->addItem(title);
    auto *page = new QWidget(dialog);
    auto *layout = new QVBoxLayout(page);
    layout->setSpacing(12);
    pages->addWidget(page);
    return layout;
  };
  QSettings saved;

  // Memory policy belongs to libcolorscreen, never to an individual scan.
  auto *memory = createPage(tr("Performance"));
  auto *title = new QLabel(tr("<h3>Memory and cache</h3>"), dialog);
  memory->addWidget(title);
  auto *automatic = new QCheckBox(tr("Automatically manage cache memory"),
                                   dialog);
  automatic->setObjectName(QStringLiteral("PreferencesAutomaticCache"));
  const int savedPercent =
      saved.value(QStringLiteral("preferences/cachePercent"), 0).toInt();
  automatic->setChecked(savedPercent == 0);
  memory->addWidget(automatic);
  auto *percentRow = new QHBoxLayout;
  auto *percentLabel = new QLabel(tr("Maximum cache share:"), dialog);
  percentRow->addWidget(percentLabel);
  auto *slider = new QSlider(Qt::Horizontal, dialog);
  slider->setObjectName(QStringLiteral("PreferencesCacheSlider"));
  slider->setRange(10, 85);
  slider->setSingleStep(5);
  slider->setPageStep(5);
  slider->setValue(savedPercent >= 10 && savedPercent <= 85
                       ? savedPercent : 50);
  percentRow->addWidget(slider, 1);
  auto *percentage = new QSpinBox(dialog);
  percentage->setObjectName(QStringLiteral("PreferencesCachePercent"));
  percentage->setRange(10, 85);
  percentage->setSuffix(QStringLiteral(" %"));
  percentage->setValue(slider->value());
  percentRow->addWidget(percentage);
  memory->addLayout(percentRow);
  connect(slider, &QSlider::valueChanged, percentage, &QSpinBox::setValue);
  connect(percentage, QOverload<int>::of(&QSpinBox::valueChanged),
          slider, &QSlider::setValue);
  auto *currentMemory = new QLabel(dialog);
  currentMemory->setObjectName(QStringLiteral("PreferencesMemorySummary"));
  currentMemory->setWordWrap(true);
  memory->addWidget(currentMemory);
  auto updateMemory = [automatic, slider, percentage, currentMemory]() {
    const bool custom = !automatic->isChecked();
    slider->setEnabled(custom);
    percentage->setEnabled(custom);
    const colorscreen::cache_memory_statistics stats =
        colorscreen::get_cache_memory_statistics();
    const auto size = [](uint64_t bytes) {
      return QLocale().formattedDataSize(
          static_cast<qint64>(std::min<uint64_t>(
              bytes, static_cast<uint64_t>(
                         std::numeric_limits<qint64>::max()))),
          1, QLocale::DataSizeIecFormat);
    };
    currentMemory->setText(
        QObject::tr("Available memory: %1     Physical memory: %2\n"
                    "Current shared cache budget: %3\n"
                    "LRU-owned: %4     Retained RAW mosaics: %5")
            .arg(stats.available_memory_bytes
                     ? size(stats.available_memory_bytes)
                     : QObject::tr("unknown"),
                 stats.total_memory_bytes
                     ? size(stats.total_memory_bytes)
                     : QObject::tr("unknown"),
                 size(stats.cache_budget_bytes),
                 size(stats.cached_bytes),
                 size(stats.retained_raw_source_bytes)));
  };
  connect(automatic, &QCheckBox::toggled, dialog, updateMemory);
  updateMemory();
  auto *memoryHint = new QLabel(
      tr("Automatic balances cache usage against free memory. Custom uses "
         "the chosen percentage of available memory, including bytes "
         "already retained by the shared cache. Images still in use and "
         "temporary processing buffers are not constrained by this budget."),
      dialog);
  memoryHint->setWordWrap(true);
  memory->addWidget(memoryHint);
  memory->addStretch();

  auto *scratch = createPage(tr("Scratch disks"));
  scratch->addWidget(new QLabel(tr("<h3>Memory-mapped temporary files</h3>"),
                                 dialog));
  auto *scratchHint = new QLabel(
      tr("When large working arrays cannot be kept in RAM, Color-Screen can "
         "store them in memory-mapped scratch files. Select a writable, "
         "disk-backed location with plenty of free space. Changes only "
         "affect new allocations; active mappings remain valid."),
      dialog);
  scratchHint->setWordWrap(true);
  scratch->addWidget(scratchHint);
  auto *systemScratch =
      new QCheckBox(tr("Use the system temporary directory"), dialog);
  systemScratch->setObjectName(QStringLiteral("PreferencesSystemScratch"));
  const QString storedFolder =
      saved.value(QStringLiteral("preferences/scratchDirectory")).toString();
  systemScratch->setChecked(storedFolder.isEmpty());
  scratch->addWidget(systemScratch);
  auto *pathRow = new QHBoxLayout;
  auto *folder = new QLineEdit(storedFolder, dialog);
  folder->setObjectName(QStringLiteral("PreferencesScratchDirectory"));
  folder->setPlaceholderText(QDir::toNativeSeparators(QDir::tempPath()));
  pathRow->addWidget(folder, 1);
  auto *browse = new QPushButton(tr("Browse…"), dialog);
  browse->setObjectName(QStringLiteral("PreferencesBrowseScratch"));
  pathRow->addWidget(browse);
  scratch->addLayout(pathRow);
  connect(systemScratch, &QCheckBox::toggled, folder, &QWidget::setDisabled);
  folder->setDisabled(systemScratch->isChecked());
  connect(browse, &QPushButton::clicked, dialog, [dialog, folder, systemScratch]() {
    auto *chooser = new QFileDialog(
        dialog, QObject::tr("Select scratch-file folder"),
        folder->text().isEmpty() ? QDir::tempPath() : folder->text());
    chooser->setObjectName(QStringLiteral("PreferencesScratchChooser"));
    chooser->setFileMode(QFileDialog::Directory);
    chooser->setOption(QFileDialog::ShowDirsOnly, true);
    chooser->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(chooser, &QFileDialog::fileSelected, dialog,
                     [folder, systemScratch](const QString &path) {
                       folder->setText(QDir::cleanPath(path));
                       systemScratch->setChecked(false);
                     });
    chooser->open();
  });
  auto *scratchWarning = new QLabel(
      tr("Some system temporary folders are RAM-backed (for example tmpfs "
         "on Linux). Choose a physical disk if scratch files could exceed "
         "available RAM. The application will not relocate existing mappings."),
      dialog);
  scratchWarning->setWordWrap(true);
  scratch->addWidget(scratchWarning);
  scratch->addStretch();

  auto *eggs = createPage(tr("Easter eggs"));
  eggs->addWidget(new QLabel(tr("<h3>Idle-screen animations</h3>"), dialog));
  auto *eggsHint = new QLabel(
      tr("Choose which historical photography animations may appear on "
         "empty image canvases. Disabling all of them leaves the canvas "
         "blank. Existing views update when you apply these preferences."),
      dialog);
  eggsHint->setWordWrap(true);
  eggs->addWidget(eggsHint);
  const QStringList eggKeys = {
      QStringLiteral("thames"), QStringLiteral("paget"),
      QStringLiteral("joly"), QStringLiteral("hurley")};
  const QStringList eggLabels = {
      tr("Thames"), tr("Paget"), tr("Joly"), tr("Hurley")};
  QList<QCheckBox *> eggToggles;
  for (qsizetype i = 0; i < eggKeys.size(); ++i) {
    auto *toggle = new QCheckBox(eggLabels[i], dialog);
    toggle->setObjectName(
        QStringLiteral("PreferencesEgg_%1").arg(eggKeys[i]));
    toggle->setChecked(saved.value(
        QStringLiteral("preferences/easterEggs/%1").arg(eggKeys[i]),
        true).toBool());
    eggs->addWidget(toggle);
    eggToggles.append(toggle);
  }
  eggs->addStretch();

  connect(categories, &QListWidget::currentRowChanged, pages,
          &QStackedWidget::setCurrentIndex);
  categories->setCurrentRow(0);
  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
          QDialogButtonBox::RestoreDefaults,
      Qt::Horizontal, dialog);
  buttons->setObjectName(QStringLiteral("PreferencesButtons"));
  outer->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  connect(buttons->button(QDialogButtonBox::RestoreDefaults),
          &QPushButton::clicked, dialog,
          [automatic, percentage, systemScratch, folder, eggToggles]() {
            automatic->setChecked(true);
            percentage->setValue(50);
            systemScratch->setChecked(true);
            folder->clear();
            for (QCheckBox *egg : eggToggles)
              egg->setChecked(true);
          });
  connect(buttons, &QDialogButtonBox::accepted, dialog,
          [dialog, automatic, percentage, systemScratch, folder,
           eggKeys, eggToggles]() {
            const int percent =
                automatic->isChecked() ? 0 : percentage->value();
            const QString chosen =
                systemScratch->isChecked()
                    ? QString()
                    : QDir::cleanPath(folder->text().trimmed());
            if (!systemScratch->isChecked()) {
              const QDir directory(chosen);
              if (chosen.isEmpty() || !directory.isAbsolute() ||
                  !directory.exists()) {
                showPreferencesWarning(
                    dialog, QObject::tr("Invalid scratch directory"),
                    QObject::tr("Select an existing absolute directory."));
                return;
              }
              QTemporaryFile probe(
                  directory.filePath(QStringLiteral(".colorscreen-write-XXXXXX")));
              if (!probe.open()) {
                showPreferencesWarning(
                    dialog, QObject::tr("Scratch directory not writable"),
                    QObject::tr("Cannot create a temporary file in %1.")
                        .arg(QDir::toNativeSeparators(chosen)));
                return;
              }
            }
            const int oldPercent = colorscreen::cache_memory_percent();
            const std::string oldScratch =
                colorscreen::cache_scratch_directory();
            if (!colorscreen::set_cache_scratch_directory(
                    QFile::encodeName(chosen).toStdString())) {
              showPreferencesWarning(
                  dialog, QObject::tr("Scratch directory unavailable"),
                  QObject::tr("The chosen directory cannot be used for mmap "
                              "scratch files. Choose a shorter, accessible "
                              "path or use the system default."));
              return;
            }
            if (!colorscreen::set_cache_memory_percent(percent)) {
              colorscreen::set_cache_scratch_directory(oldScratch);
              showPreferencesWarning(
                  dialog, QObject::tr("Invalid cache budget"),
                  QObject::tr("Choose a value from 10 to 85%."));
              return;
            }

            QSettings settings;
            settings.setValue(QStringLiteral("preferences/cachePercent"),
                              percent);
            settings.setValue(QStringLiteral("preferences/scratchDirectory"),
                              chosen);
            for (qsizetype i = 0; i < eggKeys.size(); ++i)
              settings.setValue(
                  QStringLiteral("preferences/easterEggs/%1").arg(eggKeys[i]),
                  eggToggles[i]->isChecked());
            settings.sync();
            if (settings.status() != QSettings::NoError) {
              colorscreen::set_cache_memory_percent(oldPercent);
              colorscreen::set_cache_scratch_directory(oldScratch);
              showPreferencesWarning(
                  dialog, QObject::tr("Could not save preferences"),
                  QObject::tr("Your application settings file is not writable."));
              return;
            }
            // A scan or Undo stack must never be touched by presentation
            // preferences. Existing empty and detached views read the new
            // Easter-egg permissions without constructing another cache.
            for (QWidget *widget : QApplication::allWidgets())
              if (auto *image = qobject_cast<ImageWidget *>(widget))
                image->refreshEasterEggPreferences();
            dialog->accept();
          });
  dialog->show();
}

/** Create one independently owned document and present it as a tab by default. */
MainWindow *ColorScreenApplication::createDocumentWindow(
    const QString &recoveryDirectory, bool detached) {
  pruneDocumentWindows();

  const QString assignedRecoveryDirectory =
      recoveryDirectory.isEmpty() ? newRecoveryDirectory()
                                  : recoveryDirectory;
  auto *document = new MainWindow(assignedRecoveryDirectory);
  document->setAttribute(Qt::WA_DeleteOnClose);
  m_documentWindows.append(document);

  connect(document, &QObject::destroyed, this, [this, document]() {
    m_hiddenDocumentPresentations.remove(document);
    m_finalizingDocuments.remove(document);
    m_closingDocuments.remove(document);
    QTimer::singleShot(0, this, [this]() {
      pruneDocumentWindows();
      refreshWindowMenus();
    });
  });

  if (detached) {
    document->setWindowFlags(Qt::Window);
    document->show();
    document->raise();
    document->activateWindow();
  } else {
    workspaceWindow()->addDocument(document);
  }

  refreshWindowMenus();
  return document;
}

/** Create an additional display-only view of SOURCE. */
ImageViewWindow *ColorScreenApplication::createViewWindow(MainWindow *source,
                                                           bool detached) {
  if (!source || !source->sharedImageData())
    return nullptr;

  pruneViewWindows();
  int viewNumber = 2;
  for (ImageViewWindow *view : viewWindows()) {
    if (view->sourceDocument() == source)
      viewNumber = qMax(viewNumber, view->viewNumber() + 1);
  }

  auto *view = new ImageViewWindow(source, viewNumber);
  view->setAttribute(Qt::WA_DeleteOnClose);
  m_viewWindows.append(view);
  QPointer<MainWindow> guardedSource(source);
  connect(view, &QObject::destroyed, this, [this, guardedSource, view]() {
    m_closingViews.remove(view);
    QTimer::singleShot(0, this, [this, guardedSource]() {
      pruneViewWindows();
      if (guardedSource) {
        // A final detached view approves its hidden document's close before
        // the view itself disappears.  Destroy the borrower first, then the
        // document whose inspector and state it may still reference.
        if (m_closingDocuments.contains(guardedSource.data()))
          guardedSource->deleteLater();
        else
          closeHiddenDocumentWithoutViews(guardedSource);
      }
      refreshWindowMenus();
    });
  });

  if (detached) {
    view->setWindowFlags(Qt::Window);
    view->show();
    view->raise();
    view->activateWindow();
  } else {
    workspaceWindow()->addView(view);
  }

  refreshWindowMenus();
  return view;
}

/** Create a slanted-edge reference view whose parameters belong to SOURCE. */
ImageViewWindow *ColorScreenApplication::createSlantedEdgeReference(
    MainWindow *source, const QString &referenceFile, bool detached) {
  if (!source || referenceFile.trimmed().isEmpty())
    return nullptr;

  pruneViewWindows();
  int viewNumber = 2;
  for (ImageViewWindow *view : viewWindows()) {
    if (view->sourceDocument() == source)
      viewNumber = qMax(viewNumber, view->viewNumber() + 1);
  }

  auto *view = new ImageViewWindow(
      source, viewNumber, QFileInfo(referenceFile).absoluteFilePath());
  view->setAttribute(Qt::WA_DeleteOnClose);
  m_viewWindows.append(view);
  QPointer<MainWindow> guardedSource(source);
  connect(view, &QObject::destroyed, this, [this, guardedSource, view]() {
    m_closingViews.remove(view);
    QTimer::singleShot(0, this, [this, guardedSource]() {
      pruneViewWindows();
      if (guardedSource) {
        saveSlantedEdgeReferenceRecovery(guardedSource);
        if (m_closingDocuments.contains(guardedSource.data()))
          guardedSource->deleteLater();
        else
          closeHiddenDocumentWithoutViews(guardedSource);
      }
      refreshWindowMenus();
    });
  });

  saveSlantedEdgeReferenceRecovery(source);

  if (detached) {
    view->setWindowFlags(Qt::Window);
    view->show();
    view->raise();
    view->activateWindow();
  } else {
    workspaceWindow()->addView(view);
  }

  refreshWindowMenus();
  return view;
}

/** Ask asynchronously for another scan to use as a slanted-edge reference.

    Exactly one chooser belongs to each SOURCE. Repeating the action closes the
    obsolete chooser before installing a new one, and the accepted continuation
    is guarded against source-document teardown. */
void ColorScreenApplication::openSlantedEdgeReference(
    MainWindow *source, QWidget *dialogParent) {
  if (!source || m_closingDocuments.contains(source))
    return;

  if (QPointer<QFileDialog> obsolete =
          m_slantedReferenceFileDialogs.value(source)) {
    m_slantedReferenceFileDialogs.remove(source);
    obsolete->close();
  } else {
    m_slantedReferenceFileDialogs.remove(source);
  }

  QWidget *parent = dialogParent ? dialogParent : source;
  auto *dialog = new QFileDialog(
      parent, tr("Open slanted edge reference"), QString(),
      tr("Images (*.tif *.tiff *.jpg *.jpeg *.jp2 *.j2k *.jpc *.jpf *.jpx "
         "*.png *.raw *.dng *.iiq *.nef *.cr2 *.eip *.arw *.raf *.arq);;"
         "All Files (*)"));
  dialog->setObjectName(QStringLiteral("SlantedEdgeReferenceFileDialog"));
  dialog->setFileMode(QFileDialog::ExistingFile);
  dialog->setAcceptMode(QFileDialog::AcceptOpen);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  m_slantedReferenceFileDialogs.insert(source, dialog);

  const QPointer<MainWindow> guardedSource(source);
  connect(dialog, &QDialog::accepted, this,
          [this, source, guardedSource, dialog]() {
            if (m_slantedReferenceFileDialogs.value(source) != dialog)
              return;
            m_slantedReferenceFileDialogs.remove(source);
            const QStringList selected = dialog->selectedFiles();
            if (selected.isEmpty())
              return;

            const QString fileName = selected.constFirst();
            // Let native chooser teardown finish before creating another
            // top-level/MDI presentation.
            QTimer::singleShot(0, this,
                               [this, guardedSource, fileName]() {
              if (!guardedSource ||
                  m_closingDocuments.contains(guardedSource.data()))
                return;
              createSlantedEdgeReference(guardedSource.data(), fileName);
            });
          });
  connect(dialog, &QDialog::rejected, this, [this, source, dialog]() {
    if (m_slantedReferenceFileDialogs.value(source) == dialog)
      m_slantedReferenceFileDialogs.remove(source);
  });
  connect(dialog, &QObject::destroyed, this, [this, source, dialog]() {
    // Parent-window destruction does not have to emit rejected(). Qt may clear
    // QPointer before or after destroyed() observers run, so accept either the
    // null weak pointer or the exact dying object. Never erase a replacement
    // chooser installed for the same source document.
    const QPointer<QFileDialog> current =
        m_slantedReferenceFileDialogs.value(source);
    if (!current || current.data() == dialog)
      m_slantedReferenceFileDialogs.remove(source);
  });
  // Use the chooser itself as connection context so superseding/deleting the
  // chooser removes this source-lifetime hook instead of accumulating one
  // lambda per click.
  connect(source, &QObject::destroyed, dialog, [this, source]() {
    if (QPointer<QFileDialog> current =
            m_slantedReferenceFileDialogs.take(source))
      current->close();
  });
  dialog->open();
}

/** Detach VIEW from the primary workspace without recreating it. */
void ColorScreenApplication::detachView(ImageViewWindow *view) {
  if (!view || !m_workspaceWindow || !m_workspaceWindow->containsView(view))
    return;
  m_workspaceWindow->removeView(view);
  refreshWindowMenus();
}

/** Attach VIEW to the primary workspace without changing view-local state. */
void ColorScreenApplication::attachView(ImageViewWindow *view) {
  if (!view)
    return;
  if (m_workspaceWindow && m_workspaceWindow->containsView(view)) {
    m_workspaceWindow->activateView(view);
    return;
  }
  workspaceWindow()->addView(view);
  refreshWindowMenus();
}

/** Consolidate every detached secondary view into the workspace. */
void ColorScreenApplication::attachAllViews() {
  const QList<ImageViewWindow *> views = viewWindows();
  for (ImageViewWindow *view : views)
    attachView(view);
}

/** Close VIEW using the ownership model of its current presentation. */
bool ColorScreenApplication::closeView(ImageViewWindow *view) {
  if (!view)
    return false;
  if (m_workspaceWindow && m_workspaceWindow->containsView(view))
    return m_workspaceWindow->closeView(view);

  const bool closed = view->close();
  if (closed)
    view->deleteLater();
  return closed;
}

/** Return whether DOCUMENT still has its original visible presentation. */
bool ColorScreenApplication::isDocumentPresentationOpen(
    MainWindow *document) const {
  return document && !m_hiddenDocumentPresentations.contains(document) &&
         !m_closingDocuments.contains(document);
}

/** Keep DOCUMENT alive as a hidden state owner while secondary views remain. */
bool ColorScreenApplication::retainDocumentForOpenViews(MainWindow *document) {
  if (!document || m_finalizingDocuments.contains(document) ||
      m_closingDocuments.contains(document))
    return false;

  if (m_hiddenDocumentPresentations.contains(document))
    return true;
  if (!hasOpenViews(document))
    return false;

  m_hiddenDocumentPresentations.insert(document);
  if (m_workspaceWindow && m_workspaceWindow->containsDocument(document))
    m_workspaceWindow->prepareDocumentForClose(document);
  else
    document->hide();
  refreshWindowMenus();
  return true;
}

/** Approve closing VIEW and finalize its hidden document owner when it is last. */
bool ColorScreenApplication::requestViewClose(ImageViewWindow *view) {
  if (!view)
    return false;
  if (m_closingViews.contains(view))
    return true;

  MainWindow *source = view->sourceDocument();
  if (source && m_hiddenDocumentPresentations.contains(source) &&
      !hasOpenViews(source, view)) {
    // Enter the logical closing state before calling QWidget::close().  The
    // close path can run nested Qt callbacks, so publishing the state after
    // close() returns leaves a window where the source and final view still
    // look live to those callbacks.
    m_closingViews.insert(view);
    m_closingDocuments.insert(source);
    m_finalizingDocuments.insert(source);
    // Closing the hidden owner is also the save-policy check for the final
    // view.  Suppress WA_DeleteOnClose for this call: the view still borrows
    // document-owned UI and must be destroyed before its owner.
    const bool deleteOnClose = source->testAttribute(Qt::WA_DeleteOnClose);
    source->setAttribute(Qt::WA_DeleteOnClose, false);
    const bool closed = source->close();
    source->setAttribute(Qt::WA_DeleteOnClose, deleteOnClose);
    m_finalizingDocuments.remove(source);
    if (!closed) {
      m_closingDocuments.remove(source);
      m_closingViews.remove(view);
      return false;
    }

    m_hiddenDocumentPresentations.remove(source);
    // The view's destroyed callback queues the document after the borrower
    // has released all document-owned UI.
  } else {
    m_closingViews.insert(view);
  }

  if (m_workspaceWindow && m_workspaceWindow->containsView(view))
    m_workspaceWindow->prepareViewForClose(view);
  return true;
}

/** Return all live secondary views. */
QList<ImageViewWindow *> ColorScreenApplication::viewWindows() {
  pruneViewWindows();
  QList<ImageViewWindow *> views;
  for (const QPointer<ImageViewWindow> &view : m_viewWindows) {
    if (view)
      views.append(view.data());
  }
  return views;
}

/** Reload every slanted-edge reference associated with SOURCE. */
void ColorScreenApplication::reloadSlantedEdgeReferences(MainWindow *source) {
  if (!source)
    return;
  for (ImageViewWindow *view : viewWindows()) {
    if (view && view->sourceDocument() == source &&
        view->isSlantedEdgeReference())
      view->reloadReferenceImage();
  }
}

/** Persist SOURCE's live slanted-edge reference filenames atomically. */
void ColorScreenApplication::saveSlantedEdgeReferenceRecovery(
    MainWindow *source) {
  if (!source || m_restoringReferenceRecovery.contains(source))
    return;

  const QString directory = source->recoveryDirectory();
  if (directory.isEmpty())
    return;

  QStringList references;
  for (const QPointer<ImageViewWindow> &guardedView : m_viewWindows) {
    ImageViewWindow *view = guardedView.data();
    if (!view || view->sourceDocument() != source ||
        !view->isSlantedEdgeReference() || view->referenceFile().isEmpty())
      continue;
    references.append(QFileInfo(view->referenceFile()).absoluteFilePath());
  }

  const QString path = recoveryReferencesFile(directory);
  if (references.isEmpty()) {
    QFile::remove(path);
    return;
  }
  if (!QDir().mkpath(directory))
    return;

  QJsonArray referenceArray;
  for (const QString &reference : references)
    referenceArray.append(reference);
  QJsonObject object;
  object.insert(QStringLiteral("version"), 1);
  object.insert(QStringLiteral("slantedEdgeReferences"), referenceArray);

  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    qWarning() << "Could not save slanted-edge recovery metadata to" << path;
    return;
  }
  file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
  if (!file.commit())
    qWarning() << "Could not commit slanted-edge recovery metadata to" << path;
}

/** Read SOURCE's slanted-edge reference recovery metadata. */
bool ColorScreenApplication::readSlantedEdgeReferenceRecovery(
    MainWindow *source, QStringList *references) const {
  if (!source || !references)
    return false;
  references->clear();

  const QString path = recoveryReferencesFile(source->recoveryDirectory());
  if (!QFile::exists(path))
    return true;

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return false;
  QJsonParseError parseError;
  const QJsonDocument document =
      QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject())
    return false;

  const QJsonObject object = document.object();
  if (object.value(QStringLiteral("version")).toInt(-1) != 1 ||
      !object.value(QStringLiteral("slantedEdgeReferences")).isArray())
    return false;

  const QJsonArray referenceArray =
      object.value(QStringLiteral("slantedEdgeReferences")).toArray();
  for (const QJsonValue &value : referenceArray) {
    if (!value.isString() || value.toString().trimmed().isEmpty())
      return false;
    references->append(QFileInfo(value.toString()).absoluteFilePath());
  }
  return true;
}

/** Recreate SOURCE's recorded reference scans after document recovery. */
int ColorScreenApplication::restoreSlantedEdgeReferencesFromRecovery(
    MainWindow *source) {
  QStringList references;
  if (!readSlantedEdgeReferenceRecovery(source, &references)) {
    qWarning() << "Ignoring unreadable slanted-edge recovery metadata for"
               << (source ? source->documentDisplayName() : QString());
    return -1;
  }
  if (!source || references.isEmpty())
    return 0;

  m_restoringReferenceRecovery.insert(source);
  int restored = 0;
  for (const QString &reference : references) {
    if (!QFileInfo::exists(reference)) {
      qWarning() << "Skipping missing recovered slanted-edge reference"
                 << reference;
      continue;
    }
    if (createSlantedEdgeReference(source, reference))
      ++restored;
  }
  m_restoringReferenceRecovery.remove(source);

  // Rewrite after the batch, rather than after every reference, so a crash
  // during restoration cannot truncate the not-yet-restored tail of the list.
  saveSlantedEdgeReferenceRecovery(source);
  return restored;
}

/** Verify the optional source-image reference and resolve a .cspar as a
    complete document, not as an image file. No live document changes until
    the source has been located and accepted by its asynchronous image load. */
void ColorScreenApplication::openParameterDocument(
    const QString &parameterFile, MainWindow *preferredWindow) {
  const QString parameters = QFileInfo(parameterFile).absoluteFilePath();
  colorscreen::scr_to_img_parameters geometry;
  colorscreen::scr_detect_parameters detection;
  colorscreen::render_parameters render;
  colorscreen::solver_parameters solver;
  std::vector<colorscreen::point_t> spots;
  colorscreen::parameter_source_image linked;
  std::string diagnostic;
  if (!colorscreen::read_parameter_json_v3_file(
          parameters.toUtf8().constData(), &geometry, &detection,
          &render, &solver, &spots, &diagnostic, &linked)) {
    auto *warning = new QMessageBox(
        QMessageBox::Critical, tr("Cannot Open Color-Screen Document"),
        tr("The parameter file is invalid or unsupported. Nothing was opened.\n\n%1")
            .arg(QString::fromStdString(diagnostic)),
        QMessageBox::Ok, workspaceWindow());
    warning->setObjectName(QStringLiteral("LinkedParameterOpenFailure"));
    warning->setAttribute(Qt::WA_DeleteOnClose);
    warning->open();
    return;
  }

  const QFileInfo paramInfo(parameters);
  const QDir base(paramInfo.absolutePath());
  const QString recorded = QString::fromUtf8(linked.path.c_str());
  if (!recorded.isEmpty()) {
    const QFileInfo source(recorded);
    const QString resolved = source.isAbsolute()
        ? source.absoluteFilePath() : base.absoluteFilePath(recorded);
    if (QFileInfo(resolved).isFile()) {
      openLinkedImage(parameters, resolved, preferredWindow, linked);
      return;
    }
  }

  // Common sidecars survive a folder move even if the embedded older absolute
  // pathname no longer exists. Do not accidentally bind to an ambiguous scan.
  QStringList candidates;
  auto addCandidate = [&candidates](const QString &candidate) {
    if (QFileInfo(candidate).isFile() && !candidates.contains(candidate))
      candidates.append(candidate);
  };
  if (!recorded.isEmpty())
    addCandidate(base.filePath(QFileInfo(recorded).fileName()));
  for (const QString &suffix : {
           QStringLiteral(".tif"), QStringLiteral(".tiff"),
           QStringLiteral(".dng"), QStringLiteral(".jpg"),
           QStringLiteral(".jpeg"), QStringLiteral(".png"),
           QStringLiteral(".raw"), QStringLiteral(".csprj")})
    addCandidate(base.filePath(paramInfo.completeBaseName() + suffix));
  if (candidates.size() == 1) {
    openLinkedImage(parameters, candidates.constFirst(), preferredWindow, linked);
    return;
  }

  chooseSourceForParameters(
      parameters,
      recorded.isEmpty()
          ? tr("This parameter document has no associated scan. Select an image.")
          : tr("The recorded scan %1 could not be found. Locate its new path.")
                .arg(recorded),
      preferredWindow, linked);
}

/** Offer an asynchronous native image picker for a missing/moved scan.
    Declining it cannot create a blank document or publish parameters. */
void ColorScreenApplication::chooseSourceForParameters(
    const QString &parameterFile, const QString &reason,
    MainWindow *preferredWindow,
    const colorscreen::parameter_source_image &reference) {
  auto *dialog = new QFileDialog(
      workspaceWindow(),
      tr("Locate source scan for %1 — %2")
          .arg(QFileInfo(parameterFile).fileName(), reason),
      QFileInfo(parameterFile).absolutePath(),
      tr("Images and projects (*.tif *.tiff *.jpg *.jpeg *.png *.jp2 "
         "*.raw *.dng *.iiq *.nef *.cr2 *.eip *.arw *.raf *.arq "
         "*.csprj);;All Files (*)"));
  dialog->setObjectName(QStringLiteral("LocateLinkedSourceDialog"));
  dialog->setFileMode(QFileDialog::ExistingFile);
  dialog->setAcceptMode(QFileDialog::AcceptOpen);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  QPointer<MainWindow> preferred(preferredWindow);
  connect(dialog, &QFileDialog::accepted, this,
          [this, dialog, parameterFile, reference, preferred]() {
    const QStringList selected = dialog->selectedFiles();
    if (selected.isEmpty())
      return;
    const QString chosen = QFileInfo(selected.constFirst()).absoluteFilePath();
    QTimer::singleShot(0, this,
        [this, parameterFile, chosen, reference, preferred]() {
      openLinkedImage(parameterFile, chosen, preferred.data(), reference);
    });
  });
  dialog->open();
}

/** Confirm changes to inexpensive source identity metadata. Decoded pixel
    dimensions are independently checked during MainWindow's image load. */
void ColorScreenApplication::openLinkedImage(
    const QString &parameterFile, const QString &sourceImage,
    MainWindow *preferredWindow,
    const colorscreen::parameter_source_image &reference) {
  const QFileInfo candidate(sourceImage);
  if (!candidate.isFile()) {
    chooseSourceForParameters(parameterFile,
                              tr("The selected scan is not a readable file."),
                              preferredWindow, reference);
    return;
  }
  const bool lengthChanged = reference.file_size_bytes > 0
      && (candidate.size() <= 0 ||
          static_cast<uint64_t>(candidate.size()) !=
              reference.file_size_bytes);
  if (lengthChanged) {
    auto *question = new QMessageBox(
        QMessageBox::Warning, tr("Verify Source Scan"),
        tr("The selected image (%1) has a different file size from the scan "
           "recorded in this parameter document.\n\n"
           "Use it anyway? The image dimensions will also be checked before "
           "any saved calibration is applied.")
            .arg(candidate.fileName()),
        QMessageBox::Yes | QMessageBox::No, workspaceWindow());
    question->setObjectName(QStringLiteral("LinkedSourceMismatchPrompt"));
    question->setDefaultButton(QMessageBox::No);
    question->setAttribute(Qt::WA_DeleteOnClose);
    QPointer<MainWindow> preferred(preferredWindow);
    connect(question, &QMessageBox::finished, this,
            [this, parameterFile, sourceImage, reference, preferred](int choice) {
      if (choice == QMessageBox::Yes) {
        MainWindow *target = reusableWindow(preferred.data());
        if (!target)
          target = createDocumentWindow();
        target->loadFile(sourceImage, false, false, parameterFile);
      } else if (choice == QMessageBox::No) {
        chooseSourceForParameters(
            parameterFile, tr("Locate the matching source image."),
            preferred.data(), reference);
      }
    });
    question->open();
    return;
  }

  MainWindow *target = reusableWindow(preferredWindow);
  if (!target)
    target = createDocumentWindow();
  target->loadFile(candidate.absoluteFilePath(), false, false, parameterFile);
}

/** Open a list of images, assigning one complete MainWindow to each image. */
void ColorScreenApplication::openFiles(const QStringList &fileNames,
                                       MainWindow *preferredWindow,
                                       bool suppressParamPrompt) {
  MainWindow *target = reusableWindow(preferredWindow);
  bool openedAny = false;

  for (const QString &fileName : fileNames) {
    if (fileName.trimmed().isEmpty())
      continue;
    if (fileName.endsWith(QLatin1String(".cspar"), Qt::CaseInsensitive)) {
      openParameterDocument(fileName, target);
      openedAny = true;
      target = nullptr;
      continue;
    }

    if (!target)
      target = createDocumentWindow();
    else {
      target->show();
      target->raise();
      target->activateWindow();
    }

    target->loadFile(QFileInfo(fileName).absoluteFilePath(),
                     suppressParamPrompt);
    openedAny = true;
    target = nullptr; // Only the first image may reuse an existing blank window.
  }

  if (!openedAny && documentWindows().isEmpty())
    createDocumentWindow();
}

/** Offer to restore every recoverable document from the previous session. */
bool ColorScreenApplication::restoreRecoverySession() {
  QStringList directories = recoveryDirectories();
  const bool hasLegacyRecovery =
      QFile::exists(legacyRecoveryFile(QStringLiteral("recovery_image.txt"))) ||
      QFile::exists(legacyRecoveryFile(QStringLiteral("recovery_params.par")));
  const int recoveryCount = directories.size() + (hasLegacyRecovery ? 1 : 0);

  if (recoveryCount == 0)
    return false;

  const QString message =
      recoveryCount == 1
          ? tr("Color-Screen found an image document left by an unclean "
               "shutdown.\nWould you like to restore it?")
          : tr("Color-Screen found %1 image documents left by an unclean "
               "shutdown.\nWould you like to restore them?")
                .arg(recoveryCount);
  const QMessageBox::StandardButton reply = QMessageBox::question(
      nullptr, tr("Crash Recovery"), message,
      QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

  if (reply != QMessageBox::Yes) {
    discardRecoveryData(directories);
    return false;
  }

  if (hasLegacyRecovery) {
    const QString migrated = migrateLegacyRecovery();
    if (!migrated.isEmpty()) {
      directories.prepend(migrated);
    } else {
      auto *warning = new QMessageBox(
          QMessageBox::Warning, tr("Crash Recovery"),
          tr("The legacy recovery document could not be migrated. Its "
             "original recovery files were left untouched."),
          QMessageBox::Ok, workspaceWindow());
      warning->setObjectName(
          QStringLiteral("LegacyRecoveryMigrationWarning"));
      warning->setAttribute(Qt::WA_DeleteOnClose);
      warning->open();
    }
  }

  bool restoredAny = false;
  for (const QString &directory : directories) {
    MainWindow *window = createDocumentWindow(directory);
    if (window->restoreRecoveryState()) {
      restoreSlantedEdgeReferencesFromRecovery(window);
      restoredAny = true;
    } else {
      // Invalid or incomplete recovery data should not leave an extra blank
      // document.  closeEvent removes that window's recovery directory.  Drop
      // it from the manager immediately because WA_DeleteOnClose destruction is
      // deferred until the event loop runs.
      if (window->close())
        m_documentWindows.removeAll(QPointer<MainWindow>(window));
    }
  }
  return restoredAny;
}

/** Return all live document windows and discard stale guarded pointers. */
QList<MainWindow *> ColorScreenApplication::documentWindows() {
  pruneDocumentWindows();
  QList<MainWindow *> windows;
  for (const QPointer<MainWindow> &window : m_documentWindows) {
    if (window)
      windows.append(window.data());
  }
  return windows;
}

/** Return the primary tabbed workspace, creating it on first use. */
WorkspaceWindow *ColorScreenApplication::workspaceWindow() {
  if (!m_workspaceWindow) {
    m_workspaceWindow = new WorkspaceWindow();
    m_workspaceWindow->setAttribute(Qt::WA_DeleteOnClose, false);
  }
  return m_workspaceWindow;
}

/** Detach DOCUMENT into a top-level window without recreating it. */
void ColorScreenApplication::detachDocument(MainWindow *document) {
  if (!isDocumentPresentationOpen(document) || !m_workspaceWindow ||
      !m_workspaceWindow->containsDocument(document))
    return;

  m_workspaceWindow->removeDocument(document);
  document->show();
  document->raise();
  document->activateWindow();
  refreshWindowMenus();
}

/** Attach DOCUMENT to the primary workspace without changing its state. */
void ColorScreenApplication::attachDocument(MainWindow *document) {
  if (!isDocumentPresentationOpen(document))
    return;
  if (m_workspaceWindow && m_workspaceWindow->containsDocument(document)) {
    m_workspaceWindow->activateDocument(document);
    return;
  }
  workspaceWindow()->addDocument(document);
  refreshWindowMenus();
}

/** Attach every detached document to the primary workspace. */
void ColorScreenApplication::attachAllDocuments() {
  const QList<MainWindow *> documents = documentWindows();
  for (MainWindow *document : documents) {
    if (isDocumentPresentationOpen(document))
      attachDocument(document);
  }
}

/** Refresh a document's tab or detached-window presentation. */
void ColorScreenApplication::refreshDocumentPresentation(MainWindow *document) {
  if (!document)
    return;
  if (m_workspaceWindow)
    m_workspaceWindow->refreshDocument(document);
  refreshWindowMenus();
}

/** Return workspace-owned presentation widgets before DOCUMENT closes. */
void ColorScreenApplication::prepareDocumentForClose(MainWindow *document) {
  if (!document)
    return;
  if (m_workspaceWindow)
    m_workspaceWindow->prepareDocumentForClose(document);
  else
    document->restoreWorkspaceInspector();
}

/** Return the number of attached document tabs. */
int ColorScreenApplication::tabCount() const {
  return m_workspaceWindow ? m_workspaceWindow->tabCount() : 0;
}

/** Populate the Window menu with tab and detached-window controls. */
void ColorScreenApplication::populateWindowMenu(QMenu *menu,
                                                 MainWindow *currentWindow,
                                                 ImageViewWindow *currentView) {
  if (!menu)
    return;
  menu->clear();

  QAction *newTabAction = menu->addAction(tr("&New Image Tab"));
  newTabAction->setShortcut(QKeySequence::New);
  newTabAction->setShortcutContext(Qt::WindowShortcut);
  connect(newTabAction, &QAction::triggered, menu, [this]() {
    QTimer::singleShot(0, this, [this]() { createDocumentWindow(); });
  });

  QAction *newWindowAction = menu->addAction(tr("New &Detached Window"));
  newWindowAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
  newWindowAction->setShortcutContext(Qt::WindowShortcut);
  connect(newWindowAction, &QAction::triggered, menu, [this]() {
    QTimer::singleShot(0, this, [this]() { createDocumentWindow(QString(), true); });
  });

  QAction *newViewAction = menu->addAction(tr("New &View"));
  newViewAction->setToolTip(
      tr("Open another view of this image with independent render mode and zoom."));
  newViewAction->setEnabled(currentWindow && currentWindow->sharedImageData());
  connect(newViewAction, &QAction::triggered, menu, [this, currentWindow]() {
    if (currentWindow)
      createViewWindow(currentWindow);
  });

  QAction *nextAction = menu->addAction(tr("Next Image"));
  nextAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab));
  nextAction->setShortcutContext(Qt::WindowShortcut);
  connect(nextAction, &QAction::triggered, menu,
          [this, currentWindow, currentView]() {
            activateRelativeWindow(currentWindow, currentView, 1);
          });

  QAction *previousAction = menu->addAction(tr("Previous Image"));
  previousAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab));
  previousAction->setShortcutContext(Qt::WindowShortcut);
  connect(previousAction, &QAction::triggered, menu,
          [this, currentWindow, currentView]() {
            activateRelativeWindow(currentWindow, currentView, -1);
          });

  menu->addSeparator();
  QAction *detachAction = menu->addAction(
      currentView ? tr("Detach Current View") : tr("Detach Current Image"));
  detachAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
  detachAction->setShortcutContext(Qt::WindowShortcut);
  detachAction->setEnabled(
      currentView ? (m_workspaceWindow && m_workspaceWindow->containsView(currentView))
                  : (currentWindow && m_workspaceWindow &&
                     m_workspaceWindow->containsDocument(currentWindow)));
  connect(detachAction, &QAction::triggered, menu,
          [this, currentWindow, currentView]() {
            if (currentView)
              detachView(currentView);
            else
              detachDocument(currentWindow);
          });

  QAction *attachAction = menu->addAction(
      currentView ? tr("Move Current View to Tabs")
                  : tr("Move Current Image to Tabs"));
  attachAction->setEnabled(
      currentView ? (!m_workspaceWindow || !m_workspaceWindow->containsView(currentView))
                  : (currentWindow &&
                     (!m_workspaceWindow ||
                      !m_workspaceWindow->containsDocument(currentWindow))));
  connect(attachAction, &QAction::triggered, menu,
          [this, currentWindow, currentView]() {
            if (currentView)
              attachView(currentView);
            else
              attachDocument(currentWindow);
          });

  QAction *attachAllAction = menu->addAction(tr("Attach All Images and Views as Tabs"));
  bool hasDetached = false;
  for (MainWindow *document : documentWindows()) {
    if (isDocumentPresentationOpen(document) &&
        (!m_workspaceWindow || !m_workspaceWindow->containsDocument(document))) {
      hasDetached = true;
      break;
    }
  }
  if (!hasDetached) {
    for (ImageViewWindow *view : viewWindows()) {
      if (!m_workspaceWindow || !m_workspaceWindow->containsView(view)) {
        hasDetached = true;
        break;
      }
    }
  }
  attachAllAction->setEnabled(hasDetached);
  connect(attachAllAction, &QAction::triggered, menu, [this]() {
    attachAllDocuments();
    attachAllViews();
  });

  QMenu *arrangeMenu = menu->addMenu(tr("&Arrange Images"));

  QAction *tabbedAction = arrangeMenu->addAction(tr("Consolidate All to &Tabs"));
  tabbedAction->setCheckable(true);
  tabbedAction->setChecked(m_workspaceWindow &&
                           m_workspaceWindow->isTabbedView());
  connect(tabbedAction, &QAction::triggered, menu, [this]() {
    attachAllDocuments();
    attachAllViews();
    workspaceWindow()->showTabbedDocuments();
  });

  int primaryPresentations = 0;
  for (MainWindow *document : documentWindows()) {
    if (isDocumentPresentationOpen(document))
      ++primaryPresentations;
  }
  const QList<ImageViewWindow *> views = viewWindows();
  const int presentationCount = primaryPresentations + views.size();

  QAction *tileAction = arrangeMenu->addAction(tr("&Tile All"));
  tileAction->setEnabled(presentationCount > 1);
  connect(tileAction, &QAction::triggered, menu, [this]() {
    attachAllDocuments();
    attachAllViews();
    workspaceWindow()->tileDocuments();
  });

  QAction *cascadeAction = arrangeMenu->addAction(tr("&Cascade"));
  cascadeAction->setEnabled(presentationCount > 1);
  connect(cascadeAction, &QAction::triggered, menu, [this]() {
    attachAllDocuments();
    attachAllViews();
    workspaceWindow()->cascadeDocuments();
  });

  QList<MainWindow *> documents;
  for (MainWindow *document : documentWindows()) {
    if (isDocumentPresentationOpen(document))
      documents.append(document);
  }
  nextAction->setEnabled(presentationCount > 1);
  previousAction->setEnabled(presentationCount > 1);

  menu->addSeparator();
  for (int i = 0; i < documents.size(); ++i) {
    QPointer<MainWindow> guarded(documents[i]);
    QString name = documents[i]->documentDisplayName();
    name.replace(QLatin1Char('&'), QStringLiteral("&&"));
    QAction *action = menu->addAction(tr("&%1 %2").arg(i + 1).arg(name));
    action->setCheckable(true);
    action->setChecked(!currentView && documents[i] == currentWindow);
    connect(action, &QAction::triggered, menu, [this, guarded]() {
      if (!guarded)
        return;
      if (m_workspaceWindow && m_workspaceWindow->containsDocument(guarded))
        m_workspaceWindow->activateDocument(guarded);
      else {
        if (guarded->isMinimized())
          guarded->showNormal();
        guarded->raise();
        guarded->activateWindow();
      }
    });
  }

  if (!views.isEmpty()) {
    menu->addSeparator();
    for (ImageViewWindow *view : views) {
      QPointer<ImageViewWindow> guarded(view);
      QAction *action = menu->addAction(view->windowTitle());
      action->setCheckable(true);
      action->setChecked(view == currentView);
      connect(action, &QAction::triggered, menu, [this, guarded]() {
        if (!guarded)
          return;
        if (m_workspaceWindow && m_workspaceWindow->containsView(guarded))
          m_workspaceWindow->activateView(guarded);
        else {
          if (guarded->isMinimized())
            guarded->showNormal();
          guarded->raise();
          guarded->activateWindow();
        }
      });
    }
  }
}

/** Activate the next or previous primary/secondary presentation. */
void ColorScreenApplication::activateRelativeWindow(
    MainWindow *currentWindow, ImageViewWindow *currentView, int offset) {
  QList<QWidget *> presentations;
  for (MainWindow *document : documentWindows()) {
    if (isDocumentPresentationOpen(document))
      presentations.append(document);
  }
  for (ImageViewWindow *view : viewWindows())
    presentations.append(view);
  if (presentations.isEmpty())
    return;

  QWidget *current = currentView ? static_cast<QWidget *>(currentView)
                                 : static_cast<QWidget *>(currentWindow);
  int currentIndex = presentations.indexOf(current);
  if (currentIndex < 0) {
    QWidget *active = activeWindow();
    currentIndex = presentations.indexOf(active);
  }
  if (currentIndex < 0)
    currentIndex = 0;

  const int count = presentations.size();
  QWidget *target =
      presentations[((currentIndex + offset) % count + count) % count];
  if (auto *document = qobject_cast<MainWindow *>(target)) {
    if (m_workspaceWindow && m_workspaceWindow->containsDocument(document))
      m_workspaceWindow->activateDocument(document);
    else {
      if (document->isMinimized())
        document->showNormal();
      document->raise();
      document->activateWindow();
    }
  } else if (auto *view = qobject_cast<ImageViewWindow *>(target)) {
    if (m_workspaceWindow && m_workspaceWindow->containsView(view))
      m_workspaceWindow->activateView(view);
    else {
      if (view->isMinimized())
        view->showNormal();
      view->raise();
      view->activateWindow();
    }
  }
}

/** Close every presentation for File -> Exit, respecting close vetoes. */
void ColorScreenApplication::closeAllDocumentWindows() {
  const QList<MainWindow *> documents = documentWindows();
  QList<QPointer<MainWindow>> preparedDocuments;

  auto cancelPreparedCloses = [&preparedDocuments]() {
    for (const QPointer<MainWindow> &document : preparedDocuments) {
      if (document)
        document->cancelPreparedApplicationClose();
    }
  };

  // Resolve every save/render question before destroying any presentation.
  // Previously secondary views were closed first, so cancelling a later save
  // prompt left the application running with those views already lost.
  for (MainWindow *document : documents) {
    if (!document || m_closingDocuments.contains(document))
      continue;
    if (!document->prepareForApplicationClose()) {
      cancelPreparedCloses();
      return;
    }
    preparedDocuments.append(document);
  }

  const QList<ImageViewWindow *> views = viewWindows();
  for (ImageViewWindow *view : views) {
    if (view && !closeView(view)) {
      cancelPreparedCloses();
      return;
    }
  }

  for (MainWindow *document : documents) {
    if (!document || m_closingDocuments.contains(document))
      continue;

    m_closingDocuments.insert(document);
    m_finalizingDocuments.insert(document);
    const bool closed = document->close();
    m_finalizingDocuments.remove(document);
    if (!closed) {
      m_closingDocuments.remove(document);
      cancelPreparedCloses();
      return;
    }
    m_hiddenDocumentPresentations.remove(document);
    document->deleteLater();
  }

  if (m_workspaceWindow && m_workspaceWindow->tabCount() == 0)
    m_workspaceWindow->close();
}

/** Open files delivered by Finder, Explorer, or the desktop shell. */
bool ColorScreenApplication::event(QEvent *event) {
  if (event && event->type() == QEvent::FileOpen) {
    auto *openEvent = static_cast<QFileOpenEvent *>(event);
    QString fileName = openEvent->file();
    if (fileName.isEmpty() && openEvent->url().isLocalFile())
      fileName = openEvent->url().toLocalFile();
    if (!fileName.isEmpty()) {
      openFiles({fileName});
      return true;
    }
  }
  return QApplication::event(event);
}

/** Remove null guarded pointers after document windows are destroyed. */
void ColorScreenApplication::pruneDocumentWindows() {
  for (auto it = m_documentWindows.begin(); it != m_documentWindows.end();) {
    if (it->isNull())
      it = m_documentWindows.erase(it);
    else
      ++it;
  }
}

/** Remove stale secondary-view pointers. */
void ColorScreenApplication::pruneViewWindows() {
  for (auto it = m_viewWindows.begin(); it != m_viewWindows.end();) {
    if (it->isNull())
      it = m_viewWindows.erase(it);
    else
      ++it;
  }
}

/** Return whether DOCUMENT has a secondary view that is not closing. */
bool ColorScreenApplication::hasOpenViews(MainWindow *document,
                                          ImageViewWindow *excluding) {
  if (!document)
    return false;
  for (ImageViewWindow *view : viewWindows()) {
    if (view != excluding && view->sourceDocument() == document &&
        !m_closingViews.contains(view))
      return true;
  }
  return false;
}

/** Finalize a hidden owner when its last view disappears unexpectedly. */
void ColorScreenApplication::closeHiddenDocumentWithoutViews(
    MainWindow *document) {
  if (!document || !m_hiddenDocumentPresentations.contains(document) ||
      m_closingDocuments.contains(document) || hasOpenViews(document))
    return;

  m_closingDocuments.insert(document);
  m_finalizingDocuments.insert(document);
  const bool closed = document->close();
  m_finalizingDocuments.remove(document);
  if (closed) {
    m_hiddenDocumentPresentations.remove(document);
    document->deleteLater();
    return;
  }

  m_closingDocuments.remove(document);
  // An externally destroyed final view cannot be restored after a close veto.
  // Reopen the primary presentation instead of leaving an invisible document.
  m_hiddenDocumentPresentations.remove(document);
  if (m_workspaceWindow)
    m_workspaceWindow->addDocument(document);
  else {
    document->show();
    document->raise();
    document->activateWindow();
  }
}

/** Find all per-document recovery directories that contain payload data. */
QStringList ColorScreenApplication::recoveryDirectories() const {
  QDir root(recoveryRootDirectory());
  if (!root.exists())
    return {};

  QStringList result;
  const QFileInfoList entries = root.entryInfoList(
      QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time | QDir::Reversed);
  for (const QFileInfo &entry : entries) {
    if (recoveryDirectoryHasData(entry.absoluteFilePath()))
      result.append(entry.absoluteFilePath());
    else
      QDir(entry.absoluteFilePath()).removeRecursively();
  }
  return result;
}

/** Test whether a directory contains image or parameter recovery data. */
bool ColorScreenApplication::recoveryDirectoryHasData(
    const QString &directory) const {
  return QFile::exists(
             recoveryFile(directory, QStringLiteral("recovery_image.txt"))) ||
         QFile::exists(
             recoveryFile(directory, QStringLiteral("recovery_params.cspar"))) ||
         QFile::exists(
             recoveryFile(directory, QStringLiteral("recovery_params.par")));
}

/** Migrate the old single recovery slot to a unique document directory. */
QString ColorScreenApplication::migrateLegacyRecovery() const {
  const QString targetDirectory = newRecoveryDirectory();
  if (!QDir().mkpath(targetDirectory))
    return {};

  const QStringList names = {QStringLiteral("recovery_image.txt"),
                             QStringLiteral("recovery_params.par"),
                             QStringLiteral("recovery_params_meta.txt")};
  for (const QString &name : names) {
    if (!copyRecoveryFile(legacyRecoveryFile(name),
                          recoveryFile(targetDirectory, name))) {
      QDir(targetDirectory).removeRecursively();
      return {};
    }
  }

  // Remove the old single-document slot only after every existing payload
  // file has been copied successfully, so a partial migration cannot lose the
  // user's recovery data.
  for (const QString &name : names)
    QFile::remove(legacyRecoveryFile(name));
  return targetDirectory;
}

/** Delete recovery data after the user declines session restoration. */
void ColorScreenApplication::discardRecoveryData(
    const QStringList &directories) const {
  for (const QString &directory : directories)
    QDir(directory).removeRecursively();

  const QStringList legacyNames = {
      QStringLiteral("recovery_image.txt"),
      QStringLiteral("recovery_params.par"),
      QStringLiteral("recovery_params_meta.txt")};
  for (const QString &name : legacyNames)
    QFile::remove(legacyRecoveryFile(name));
}

/** Rebuild Window menus after a document is added or removed. */
void ColorScreenApplication::refreshWindowMenus() {
  const QList<MainWindow *> windows = documentWindows();
  for (MainWindow *window : windows)
    window->refreshWindowMenu();
}

/** Return the active document from the workspace or a detached window. */
MainWindow *ColorScreenApplication::activeDocument() const {
  if (QWidget *active = activeWindow()) {
    if (auto *document = dynamic_cast<MainWindow *>(active))
      return document;
    if (auto *view = dynamic_cast<ImageViewWindow *>(active))
      return view->sourceDocument();
    if (m_workspaceWindow && active == m_workspaceWindow)
      return m_workspaceWindow->currentDocument();
  }
  return m_workspaceWindow ? m_workspaceWindow->currentDocument() : nullptr;
}

/** Find an untouched empty window that can host the first requested image. */
MainWindow *ColorScreenApplication::reusableWindow(
    MainWindow *preferredWindow) {
  if (preferredWindow && preferredWindow->canReuseForOpen())
    return preferredWindow;

  if (MainWindow *window = activeDocument()) {
    if (window->canReuseForOpen())
      return window;
  }

  for (MainWindow *window : documentWindows()) {
    if (window->canReuseForOpen())
      return window;
  }
  return nullptr;
}
