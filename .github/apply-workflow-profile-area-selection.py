#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(
            f"{path}: expected one match, found {count}\n--- pattern ---\n{old}"
        )
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


def transform_region(path: str, start: str, end: str, transform) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    start_pos = text.index(start)
    end_pos = text.index(end, start_pos)
    region = text[start_pos:end_pos]
    replacement = transform(region)
    if replacement == region:
        raise RuntimeError(f"{path}: region transform made no change: {start}")
    p.write_text(text[:start_pos] + replacement + text[end_pos:], encoding="utf-8")


main = "src/qtgui/MainWindow.cpp"
image_cpp = "src/qtgui/ImageWidget.cpp"
image_h = "src/qtgui/ImageWidget.h"
view_cpp = "src/qtgui/ImageViewWindow.cpp"
smoke = "src/qtgui/WorkspaceChurnSmoke.cpp"

replace_once(
    main,
    '''  m_workflowCalibrationLabel->setWordWrap(true);
  workflowLayout->addWidget(m_workflowCalibrationLabel);

  QFont workflowSectionFont = m_workflowProcessLabel->font();
  workflowSectionFont.setWeight(QFont::DemiBold);
  if (workflowSectionFont.pointSizeF() > 1.0)
    workflowSectionFont.setPointSizeF(workflowSectionFont.pointSizeF() - 0.5);
  m_workflowProcessLabel->setFont(workflowSectionFont);
  m_workflowRegistrationLabel->setFont(workflowSectionFont);
  m_workflowCalibrationLabel->setFont(workflowSectionFont);

  m_workflowNextStepLabel = new QLabel(workflowSummary);''',
    '''  m_workflowCalibrationLabel->setWordWrap(true);
  workflowLayout->addWidget(m_workflowCalibrationLabel);

  auto *workflowProfileLabel = new QLabel(workflowSummary);
  workflowProfileLabel->setObjectName(
      QStringLiteral("WorkflowProfileSummary"));
  workflowProfileLabel->setWordWrap(true);
  workflowProfileLabel->setToolTip(tr(
      "Optional simple matrix color correction for RGB captures made "
      "with the screen filter present."));
  workflowLayout->addWidget(workflowProfileLabel);

  QFont workflowSectionFont = m_workflowProcessLabel->font();
  workflowSectionFont.setWeight(QFont::DemiBold);
  if (workflowSectionFont.pointSizeF() > 1.0)
    workflowSectionFont.setPointSizeF(workflowSectionFont.pointSizeF() - 0.5);
  m_workflowProcessLabel->setFont(workflowSectionFont);
  m_workflowRegistrationLabel->setFont(workflowSectionFont);
  m_workflowCalibrationLabel->setFont(workflowSectionFont);
  workflowProfileLabel->setFont(workflowSectionFont);

  m_workflowNextStepLabel = new QLabel(workflowSummary);''',
)

replace_once(
    main,
    '''  auto setWorkflowExpanded =
      [this, workflowToggle, workflowStages](bool expanded) {
        workflowToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        workflowStages->setVisible(expanded);
        m_workflowProcessLabel->setVisible(expanded);
        m_workflowRegistrationLabel->setVisible(expanded);
        m_workflowCalibrationLabel->setVisible(expanded);
        m_workflowNextStepLabel->setVisible(expanded);''',
    '''  auto setWorkflowExpanded =
      [this, workflowToggle, workflowStages,
       workflowProfileLabel](bool expanded) {
        workflowToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        workflowStages->setVisible(expanded);
        m_workflowProcessLabel->setVisible(expanded);
        m_workflowRegistrationLabel->setVisible(expanded);
        m_workflowCalibrationLabel->setVisible(expanded);
        workflowProfileLabel->setVisible(
            expanded && !workflowProfileLabel->text().isEmpty());
        m_workflowNextStepLabel->setVisible(expanded);''',
)


def update_profile_summary(region: str) -> str:
    updated = region.replace("Profile:", "Profile (optional):")
    old = "colorscreen::render_parameters::capture_has_screen_p(capture)"
    new = (
        "colorscreen::render_parameters::"
        "capture_supports_screen_detection_p(capture)"
    )
    if old not in updated:
        raise RuntimeError("profile summary capture predicate not found")
    return updated.replace(old, new, 1)


transform_region(
    main,
    "QString MainWindow::profileCalibrationSummary() const {",
    "/** Refresh the persistent workflow summary",
    update_profile_summary,
)

replace_once(
    main,
    '''  if (!m_workflowProcessLabel || !m_workflowRegistrationLabel ||
      !m_workflowCalibrationLabel || !m_workflowNextStepLabel)
    return;

  const ParameterState currentState = getCurrentState();''',
    '''  if (!m_workflowProcessLabel || !m_workflowRegistrationLabel ||
      !m_workflowCalibrationLabel || !m_workflowNextStepLabel)
    return;

  QLabel *workflowProfileLabel =
      m_rightColumn
          ? m_rightColumn->findChild<QLabel *>(
                QStringLiteral("WorkflowProfileSummary"))
          : nullptr;

  const ParameterState currentState = getCurrentState();''',
)

replace_once(
    main,
    '''  const QString profileSummary = profileCalibrationSummary();
  if (m_profilePanel)
    m_profilePanel->setCalibrationStatus(profileSummary);
  m_workflowCalibrationLabel->setText(
      sharpenSummary + QStringLiteral(" • ") + mtfSummary +
      QStringLiteral(" • ") + profileSummary);
''',
    '''  const QString profileSummary = profileCalibrationSummary();
  if (m_profilePanel)
    m_profilePanel->setCalibrationStatus(profileSummary);
  m_workflowCalibrationLabel->setText(
      sharpenSummary + QStringLiteral(" • ") + mtfSummary);

  const bool showProfile = m_scan && m_scan->has_rgb() && colorDetection;
  if (workflowProfileLabel) {
    workflowProfileLabel->setText(showProfile ? profileSummary : QString());
    workflowProfileLabel->setVisible(
        showProfile && m_workflowNextStepLabel->isVisible());
  }
''',
)

replace_once(
    main,
    '''    } else if (fitCurrent) {
      nextStep = tr(
          "Next: Geometry — turn on Show registration points and inspect "
          "residual arrows; if they are small and patternless, reconstruct.");
    } else if (failureCurrent) {
      nextStep = tr(
          "Next: Geometry — adjust registration points/settings and optimize "
          "the fit again.");
    } else {
      nextStep = tr(
          "Next: Geometry — optimize the fit, then turn on Show registration "
          "points to inspect residual arrows.");
    }''',
    '''    } else if (fitCurrent) {
      nextStep = tr(
          "Next: Reconstruct — switch Mode to Image layer + screen filter or "
          "Image layer + screen filter demosaiced with detail recovery, then "
          "check that the colors look correct.");
    } else if (failureCurrent) {
      nextStep = tr(
          "Next: Geometry — adjust registration points/settings and optimize "
          "the fit again.");
    } else {
      nextStep = tr(
          "Next: Geometry — optimize the fit; then switch Mode to a "
          "reconstructed Image layer + screen filter view and check that the "
          "colors look correct.");
    }''',
)

replace_once(
    main,
    '    nextStep = tr("Next: reconstruct the image and refine Color/Profile.");',
    '    nextStep = tr("Next: reconstruct the image and refine Color.");',
)

replace_once(
    main,
    '''  if (m_profilePanel) {
    int profileTabIndex = m_configTabs->indexOf(m_profilePanel);
    if (profileTabIndex >= 0) {
      m_configTabs->setTabVisible(profileTabIndex, m_scan && m_scan->has_rgb());
    }
  }
''',
    '''  if (m_profilePanel) {
    int profileTabIndex = m_configTabs->indexOf(m_profilePanel);
    if (profileTabIndex >= 0) {
      const bool showProfile =
          m_scan && m_scan->has_rgb() &&
          colorscreen::render_parameters::capture_supports_screen_detection_p(
              m_rparams.get_capture_type(m_scan.get()));
      m_configTabs->setTabVisible(profileTabIndex, showProfile);
    }
  }
''',
)

replace_once(
    image_h,
    '''  colorscreen::render_coordinate_space coordinateSpace() const {
    return m_coordinateSpace;
  }

  /**
   * @brief Clears the current selection.
''',
    '''  colorscreen::render_coordinate_space coordinateSpace() const {
    return m_coordinateSpace;
  }

  /** Convert a widget-space selection rectangle to half-open scan bounds. */
  QRect widgetAreaToImage(QRect area) const;

  /**
   * @brief Clears the current selection.
''',
)

replace_once(
    image_cpp,
    "    QSize transformedSize = transformer.getTransformedSize();\n",
    "    QSize transformedSize = transformer.getTransformedCropSize();\n",
)

replace_once(
    image_cpp,
    '''void ImageWidget::mousePressEvent(QMouseEvent *event) {
  if (m_interactionMode == SetCenterMode) {
''',
    '''void ImageWidget::mousePressEvent(QMouseEvent *event) {
  // Precise tools must interpret their input against a stationary canvas.
  // Otherwise smooth fit/zoom/pan can change the view during the drag.
  if (event->button() == Qt::LeftButton &&
      m_interactionMode != PanMode && m_interactionMode != ExploreMode) {
    m_plusHeld = false;
    m_minusHeld = false;
    m_keyboardZoomVelocity = 0;
    m_panAnimationActive = false;
    m_exploreTargetX = m_viewX;
    m_exploreTargetY = m_viewY;
    m_exploreTargetScale = m_scale;
    if (m_exploreTimer)
      m_exploreTimer->stop();
  }

  if (m_interactionMode == SetCenterMode) {
''',
)

replace_once(
    image_cpp,
    '''void ImageWidget::setInteractionMode(InteractionMode mode) {
  if (m_interactionMode == mode) return;
  m_interactionMode = mode;
  if (m_rubberBand) m_rubberBand->hide();
  emit interactionModeChanged(mode);
  update();
}
''',
    '''void ImageWidget::setInteractionMode(InteractionMode mode) {
  if (m_interactionMode == mode) return;

  // Coordinate-sensitive tools start from the view currently shown, rather
  // than from a smooth-navigation target still moving below the cursor.
  if (mode != PanMode && mode != ExploreMode) {
    m_plusHeld = false;
    m_minusHeld = false;
    m_keyboardZoomVelocity = 0;
    m_panAnimationActive = false;
    m_exploreTargetX = m_viewX;
    m_exploreTargetY = m_viewY;
    m_exploreTargetScale = m_scale;
    if (m_exploreTimer)
      m_exploreTimer->stop();
  }

  m_interactionMode = mode;
  if (m_rubberBand) m_rubberBand->hide();
  emit interactionModeChanged(mode);
  update();
}
''',
)

replace_once(
    image_cpp,
    '''  // 2. Apply View Offset & Scale (Transformed -> View)
  return QPointF((tr.x - m_viewX) * m_scale, (tr.y - m_viewY) * m_scale);
}

/**
 * @brief Rotates the image viewport 90 degrees to the left.
''',
    '''  // 2. Apply View Offset & Scale (Transformed -> View)
  return QPointF((tr.x - m_viewX) * m_scale, (tr.y - m_viewY) * m_scale);
}

QRect ImageWidget::widgetAreaToImage(QRect area) const {
  if (!m_scan || !m_rparams || !m_scrToImg || area.isEmpty())
    return {};

  // QRect has inclusive integer right/bottom coordinates. QRectF exposes the
  // corresponding half-open edges, which remain correct at fractional zoom.
  const QRectF widgetBounds(area.normalized());
  const colorscreen::point_t p1 = widgetToImage(widgetBounds.topLeft());
  const colorscreen::point_t p2 = widgetToImage(widgetBounds.topRight());
  const colorscreen::point_t p3 = widgetToImage(widgetBounds.bottomLeft());
  const colorscreen::point_t p4 = widgetToImage(widgetBounds.bottomRight());

  const double minX = std::min({p1.x, p2.x, p3.x, p4.x});
  const double maxX = std::max({p1.x, p2.x, p3.x, p4.x});
  const double minY = std::min({p1.y, p2.y, p3.y, p4.y});
  const double maxY = std::max({p1.y, p2.y, p3.y, p4.y});

  const int xmin = qBound(0, static_cast<int>(std::floor(minX)),
                         static_cast<int>(m_scan->width));
  const int xmax = qBound(0, static_cast<int>(std::ceil(maxX)),
                         static_cast<int>(m_scan->width));
  const int ymin = qBound(0, static_cast<int>(std::floor(minY)),
                         static_cast<int>(m_scan->height));
  const int ymax = qBound(0, static_cast<int>(std::ceil(maxY)),
                         static_cast<int>(m_scan->height));

  return xmax > xmin && ymax > ymin
             ? QRect(xmin, ymin, xmax - xmin, ymax - ymin)
             : QRect();
}

/**
 * @brief Rotates the image viewport 90 degrees to the left.
''',
)

replace_once(
    main,
    '''QRect MainWindow::getImageArea(QRect area, ImageWidget *imageWidget) {
  ImageWidget *image = imageWidget ? imageWidget : inspectorImageWidget();
  if (!m_scan || !image)
    return QRect();

  // Convert widget coordinates to image coordinates
  // Get the four corners and find min/max
  colorscreen::point_t p1 = image->widgetToImage(area.topLeft());
  colorscreen::point_t p2 = image->widgetToImage(area.topRight());
  colorscreen::point_t p3 = image->widgetToImage(area.bottomLeft());
  colorscreen::point_t p4 = image->widgetToImage(area.bottomRight());

  // Find bounding box in image coordinates
  int xmin = std::min({p1.x, p2.x, p3.x, p4.x});
  int xmax = std::max({p1.x, p2.x, p3.x, p4.x});
  int ymin = std::min({p1.y, p2.y, p3.y, p4.y});
  int ymax = std::max({p1.y, p2.y, p3.y, p4.y});

  // Clamp to image bounds
  xmin = std::max(0, xmin);
  ymin = std::max(0, ymin);
  xmax = std::min((int)m_scan->width - 1, xmax);
  ymax = std::min((int)m_scan->height - 1, ymax);

  return QRect(xmin, ymin, xmax - xmin + 1, ymax - ymin + 1);
}
''',
    '''QRect MainWindow::getImageArea(QRect area, ImageWidget *imageWidget) {
  ImageWidget *image = imageWidget ? imageWidget : inspectorImageWidget();
  return m_scan && image ? image->widgetAreaToImage(area) : QRect();
}
''',
)

replace_once(
    view_cpp,
    '''QRect ImageViewWindow::referenceImageArea(QRect area) const {
  if (!m_scan || area.width() <= 0 || area.height() <= 0)
    return {};

  const colorscreen::point_t p1 = m_imageWidget->widgetToImage(area.topLeft());
  const colorscreen::point_t p2 = m_imageWidget->widgetToImage(area.topRight());
  const colorscreen::point_t p3 =
      m_imageWidget->widgetToImage(area.bottomLeft());
  const colorscreen::point_t p4 =
      m_imageWidget->widgetToImage(area.bottomRight());
  int xmin = std::min({p1.x, p2.x, p3.x, p4.x});
  int xmax = std::max({p1.x, p2.x, p3.x, p4.x});
  int ymin = std::min({p1.y, p2.y, p3.y, p4.y});
  int ymax = std::max({p1.y, p2.y, p3.y, p4.y});
  xmin = std::max(0, xmin);
  ymin = std::max(0, ymin);
  xmax = std::min(static_cast<int>(m_scan->width) - 1, xmax);
  ymax = std::min(static_cast<int>(m_scan->height) - 1, ymax);
  return xmax >= xmin && ymax >= ymin
             ? QRect(xmin, ymin, xmax - xmin + 1, ymax - ymin + 1)
             : QRect();
}
''',
    '''QRect ImageViewWindow::referenceImageArea(QRect area) const {
  return m_scan && m_imageWidget
             ? m_imageWidget->widgetAreaToImage(area)
             : QRect();
}
''',
)

replace_once(
    smoke,
    '''QLabel *calibrationSummary = inspector->findChild<QLabel *>(
    QStringLiteral("WorkflowCalibrationSummary"));
QLabel *nextStepSummary = inspector->findChild<QLabel *>(
''',
    '''QLabel *calibrationSummary = inspector->findChild<QLabel *>(
    QStringLiteral("WorkflowCalibrationSummary"));
QLabel *profileSummary = inspector->findChild<QLabel *>(
    QStringLiteral("WorkflowProfileSummary"));
QLabel *nextStepSummary = inspector->findChild<QLabel *>(
''',
)

replace_once(
    smoke,
    '''QPushButton *mtfMeasurementLocate = inspector->findChild<QPushButton *>(
    QStringLiteral("MtfMeasurementLocate"));

if (!workflowSummary || !workflowToggle || !workflowStages ||
''',
    '''QPushButton *mtfMeasurementLocate = inspector->findChild<QPushButton *>(
    QStringLiteral("MtfMeasurementLocate"));

const ParameterState workflowState = first->documentStateSnapshot();
const auto workflowCapture = first->sharedImageData()
    ? workflowState.rparams.get_capture_type(first->sharedImageData().get())
    : workflowState.rparams.capture_type;
const bool profileExpected = first->sharedImageData() &&
    first->sharedImageData()->has_rgb() &&
    colorscreen::render_parameters::capture_supports_screen_detection_p(
        workflowCapture);
const int profileTabIndex =
    actualProcessingTabs.indexOf(QStringLiteral("Profile"));
const bool profileTabVisible = processingTabs && profileTabIndex >= 0 &&
    processingTabs->isTabVisible(profileTabIndex);
const bool profileSummaryCompatible = profileSummary &&
    (profileExpected
         ? profileSummary->text().startsWith(
               QStringLiteral("Profile (optional):"))
         : profileSummary->text().isEmpty());

if (!workflowSummary || !workflowToggle || !workflowStages ||
''',
)

replace_once(
    smoke,
    '''    !processSummary || !registrationSummary || !calibrationSummary ||
    !nextStepSummary || !captureTypeCombo ||
''',
    '''    !processSummary || !registrationSummary || !calibrationSummary ||
    !profileSummary || !nextStepSummary || !captureTypeCombo ||
''',
)

replace_once(
    smoke,
    '''    !calibrationSummary->text().contains(
        QStringLiteral("Capture MTF:")) ||
    !calibrationSummary->text().contains(QStringLiteral("Profile:")) ||
    !nextStepSummary->text().startsWith(QStringLiteral("Next:")) ||
''',
    '''    !calibrationSummary->text().contains(
        QStringLiteral("Capture MTF:")) ||
    calibrationSummary->text().contains(QStringLiteral("Profile")) ||
    !profileSummaryCompatible || profileTabVisible != profileExpected ||
    !nextStepSummary->text().startsWith(QStringLiteral("Next:")) ||
''',
)

replace_once(
    smoke,
    '''        if (!profileCalibrationStatus || !profileOptimizeButton ||
            !profileCalibrationStatus->text().startsWith(
                QStringLiteral("Profile:")) ||
''',
    '''        if (!profileCalibrationStatus || !profileOptimizeButton ||
            !profileCalibrationStatus->text().startsWith(
                QStringLiteral("Profile (optional):")) ||
''',
)

for path in (main, image_cpp, image_h, view_cpp, smoke):
    text = Path(path).read_text(encoding="utf-8")
    if "\t" in text:
        raise RuntimeError(f"{path}: patch introduced a tab")
