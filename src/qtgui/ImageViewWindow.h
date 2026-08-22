#pragma once

#include "ParameterState.h"

#include "../libcolorscreen/include/render-type-parameters.h"

#include <QMainWindow>
#include <QPointer>
#include <memory>

class ImageWidget;
class MainWindow;
class QCheckBox;
class QComboBox;
class QToolBar;

/** Lightweight secondary view of one MainWindow document.

    The source MainWindow remains the sole owner of editable document state,
    undo/recovery, workers, and filenames.  This window shares the loaded scan
    and follows snapshots of the source parameters, while keeping display-only
    state (render mode, IR/RGB choice, zoom, and pan) independent. */
class ImageViewWindow final : public QMainWindow {
  Q_OBJECT

public:
  /** Construct secondary view number VIEWNUMBER for DOCUMENT. */
  explicit ImageViewWindow(MainWindow *document, int viewNumber,
                           QWidget *parent = nullptr);

  /** Return the document whose image and parameters this view follows. */
  MainWindow *sourceDocument() const;

  /** Return the stable per-document view number used in the title. */
  int viewNumber() const { return m_viewNumber; }

  /** Return this view's independent render mode for smoke tests/UI state. */
  colorscreen::render_type_t renderType() const {
    return m_renderTypeParams.type;
  }

  /** Return the shared scan currently displayed by this view. */
  std::shared_ptr<colorscreen::image_data> sharedImageData() const {
    return m_scan;
  }

  /** Select TYPE in this view without changing the source document view. */
  bool setRenderType(colorscreen::render_type_t type);

private slots:
  /** Refresh shared image/parameter snapshots after the document changes. */
  void refreshFromDocument();

  /** Change only this view's rendering mode. */
  void onModeChanged(int index);

  /** Change only this view's IR/RGB presentation. */
  void onColorChanged(bool checked);

private:
  /** Build the compact image-view UI and view-local toolbar/menu actions. */
  void setupUi();

  /** Rebuild render-mode choices valid for the current shared document. */
  void rebuildModeList();

  /** Synchronize the color checkbox with render-mode capabilities. */
  void updateColorControl();

  /** Push copied shared parameters plus view-local render mode to ImageWidget. */
  void updateImageParameters(bool imageChanged);

  QPointer<MainWindow> m_document;
  int m_viewNumber = 1;
  ImageWidget *m_imageWidget = nullptr;
  QToolBar *m_toolbar = nullptr;
  QComboBox *m_modeComboBox = nullptr;
  QCheckBox *m_colorCheckBox = nullptr;

  std::shared_ptr<colorscreen::image_data> m_scan;
  colorscreen::render_parameters m_rparams;
  colorscreen::scr_detect_parameters m_detectParams;
  colorscreen::scr_to_img_parameters m_scrToImgParams;
  colorscreen::solver_parameters m_solverParams;
  colorscreen::render_type_parameters m_renderTypeParams;
};
