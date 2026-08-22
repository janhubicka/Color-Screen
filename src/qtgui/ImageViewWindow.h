#pragma once

#include "ParameterState.h"

#include "../libcolorscreen/include/render-type-parameters.h"

#include <QMainWindow>
#include <QPointer>
#include <QRect>
#include <QString>
#include <memory>
#include <vector>

class ImageWidget;
class MainWindow;
class SharpnessPanel;
class QAction;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QToolBar;

/** Lightweight secondary view of one MainWindow document.

    The source MainWindow remains the sole owner of editable document state,
    undo/recovery, workers, and filenames.  This view shares the loaded scan
    and follows snapshots of the source parameters, while keeping display-only
    state (render mode, IR/RGB choice, zoom, and pan) independent.  By default
    ColorScreenApplication embeds it as another QMdiArea tab; it may also be
    detached as an ordinary top-level QMainWindow. */
class ImageViewWindow final : public QMainWindow {
  Q_OBJECT

public:
  /** Construct secondary view number VIEWNUMBER for DOCUMENT. */
  explicit ImageViewWindow(MainWindow *document, int viewNumber,
                           QWidget *parent = nullptr);

  /** Construct a slanted-edge reference view of REFERENCEFILE for DOCUMENT. */
  ImageViewWindow(MainWindow *document, int viewNumber,
                  const QString &referenceFile, QWidget *parent = nullptr);

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

  /** Return this view's toolbar while it is hosted by WorkspaceWindow. */
  QToolBar *workspaceToolBar() const { return m_toolbar; }

  /** Hide top-level chrome before this QMainWindow becomes an MDI child. */
  void prepareForWorkspaceEmbedding();

  /** Restore normal top-level chrome after the view leaves the workspace. */
  void restoreFromWorkspaceEmbedding();

  /** Return whether this view is currently presented inside the workspace. */
  bool isWorkspaceEmbedded() const { return m_workspaceEmbedded; }

  /** Return true when this view displays an external slanted-edge reference. */
  bool isSlantedEdgeReference() const { return m_slantedEdgeReference; }

  /** Return the optional specialized inspector presented by the workspace. */
  QWidget *workspaceInspectorWidget() const;

  /** Return the reference filename, or an empty string for a normal New View. */
  QString referenceFile() const { return m_referenceFile; }

private slots:
  /** Refresh shared image/parameter snapshots after the document changes. */
  void refreshFromDocument();

  /** Change only this view's rendering mode. */
  void onModeChanged(int index);

  /** Change only this view's IR/RGB presentation. */
  void onColorChanged(bool checked);

  /** Start/cancel slanted-edge area selection in a reference view. */
  void onMeasureMtfRequested(bool checked);

  /** Consume a selected rectangle when reference MTF measurement is active. */
  void onReferenceAreaSelected(QRect area);

private:
  /** Build the compact image-view UI and view-local toolbar/menu actions. */
  void setupUi();

  /** Build the reduced Sharpness-only inspector for a reference image. */
  void setupReferenceInspector();

  /** Load the external reference image without changing the source document. */
  void loadReferenceImage(const QString &fileName);

  /** Convert a selection rectangle to bounded reference-image coordinates. */
  QRect referenceImageArea(QRect widgetArea) const;

  /** Rebuild render-mode choices valid for the current shared document. */
  void rebuildModeList();

  /** Synchronize the color and mirror controls with their owners. */
  void updateViewControls();

  /** Push copied shared parameters plus view-local render mode to ImageWidget. */
  void updateImageParameters(bool imageChanged);

  QPointer<MainWindow> m_document;
  int m_viewNumber = 1;
  ImageWidget *m_imageWidget = nullptr;
  QToolBar *m_toolbar = nullptr;
  QComboBox *m_modeComboBox = nullptr;
  QCheckBox *m_colorCheckBox = nullptr;
  QAction *m_mirrorAction = nullptr;
  SharpnessPanel *m_sharpnessPanel = nullptr;
  QDockWidget *m_referenceInspectorDock = nullptr;
  bool m_workspaceEmbedded = false;
  bool m_slantedEdgeReference = false;
  bool m_referenceLoadPending = false;
  QString m_referenceFile;

  std::shared_ptr<colorscreen::image_data> m_scan;
  colorscreen::render_parameters m_rparams;
  colorscreen::scr_detect_parameters m_detectParams;
  colorscreen::scr_to_img_parameters m_scrToImgParams;
  colorscreen::solver_parameters m_solverParams;
  colorscreen::render_type_parameters m_renderTypeParams;
  colorscreen::slanted_edge_parameters m_slantedEdgeParameters;
  std::vector<colorscreen::slanted_edge_parameters> m_pendingMtfParameters;
};
