#!/usr/bin/env python3
from pathlib import Path
import re


def replace_exact(path, old, new):
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exact block once, found {count}")
    p.write_text(text.replace(old, new, 1))


def replace_regex(path, pattern, replacement):
    p = Path(path)
    text = p.read_text()
    updated, count = re.subn(pattern, replacement, text, count=1,
                             flags=re.MULTILINE | re.DOTALL)
    if count != 1:
        raise SystemExit(f"{path}: regex did not match exactly once: {pattern[:80]}")
    p.write_text(updated)


# Keep Profile as its own optional workflow line rather than mixing it into
# capture sharpening/MTF readiness.
replace_exact(
    "src/qtgui/MainWindow.h",
    """  QLabel *m_workflowProcessLabel = nullptr;\n  QLabel *m_workflowRegistrationLabel = nullptr;\n  QLabel *m_workflowCalibrationLabel = nullptr;\n  QLabel *m_workflowNextStepLabel = nullptr;\n""",
    """  QLabel *m_workflowProcessLabel = nullptr;\n  QLabel *m_workflowRegistrationLabel = nullptr;\n  QLabel *m_workflowCalibrationLabel = nullptr;\n  QLabel *m_workflowProfileLabel = nullptr;\n  QLabel *m_workflowNextStepLabel = nullptr;\n""")

replace_exact(
    "src/qtgui/MainWindow.cpp",
    """  m_workflowCalibrationLabel = new QLabel(workflowSummary);\n  m_workflowCalibrationLabel->setObjectName(\n      QStringLiteral(\"WorkflowCalibrationSummary\"));\n  m_workflowCalibrationLabel->setWordWrap(true);\n  workflowLayout->addWidget(m_workflowCalibrationLabel);\n\n  QFont workflowSectionFont = m_workflowProcessLabel->font();\n""",
    """  m_workflowCalibrationLabel = new QLabel(workflowSummary);\n  m_workflowCalibrationLabel->setObjectName(\n      QStringLiteral(\"WorkflowCalibrationSummary\"));\n  m_workflowCalibrationLabel->setWordWrap(true);\n  workflowLayout->addWidget(m_workflowCalibrationLabel);\n\n  m_workflowProfileLabel = new QLabel(workflowSummary);\n  m_workflowProfileLabel->setObjectName(\n      QStringLiteral(\"WorkflowProfileSummary\"));\n  m_workflowProfileLabel->setWordWrap(true);\n  m_workflowProfileLabel->setProperty(\"workflowApplicable\", false);\n  m_workflowProfileLabel->setToolTip(tr(\n      \"Optional RGB screen-capture calibration. It fits a simple matrix \"\n      \"profile from calibration spots and is not part of sharpening.\"));\n  workflowLayout->addWidget(m_workflowProfileLabel);\n\n  QFont workflowSectionFont = m_workflowProcessLabel->font();\n""")

replace_exact(
    "src/qtgui/MainWindow.cpp",
    """  m_workflowProcessLabel->setFont(workflowSectionFont);\n  m_workflowRegistrationLabel->setFont(workflowSectionFont);\n  m_workflowCalibrationLabel->setFont(workflowSectionFont);\n\n  m_workflowNextStepLabel = new QLabel(workflowSummary);\n""",
    """  m_workflowProcessLabel->setFont(workflowSectionFont);\n  m_workflowRegistrationLabel->setFont(workflowSectionFont);\n  m_workflowCalibrationLabel->setFont(workflowSectionFont);\n  m_workflowProfileLabel->setFont(workflowSectionFont);\n\n  m_workflowNextStepLabel = new QLabel(workflowSummary);\n""")

replace_exact(
    "src/qtgui/MainWindow.cpp",
    """        m_workflowProcessLabel->setVisible(expanded);\n        m_workflowRegistrationLabel->setVisible(expanded);\n        m_workflowCalibrationLabel->setVisible(expanded);\n        m_workflowNextStepLabel->setVisible(expanded);\n""",
    """        m_workflowProcessLabel->setVisible(expanded);\n        m_workflowRegistrationLabel->setVisible(expanded);\n        m_workflowCalibrationLabel->setVisible(expanded);\n        m_workflowProfileLabel->setVisible(\n            expanded &&\n            m_workflowProfileLabel->property(\"workflowApplicable\").toBool());\n        m_workflowNextStepLabel->setVisible(expanded);\n""")

# Profile is only meaningful when the capture type explicitly says that RGB
# contains the historical screen filter. Monochrome-through-screen captures
# use geometry but have no screen colour data to profile.
replace_regex(
    "src/qtgui/MainWindow.cpp",
    r"QString MainWindow::profileCalibrationSummary\(\) const \{.*?\n\}\n\n/\*\* Refresh the persistent workflow summary",
    r'''QString MainWindow::profileCalibrationSummary() const {
  if (!m_scan)
    return QString();

  const auto capture = m_rparams.get_capture_type(m_scan.get());
  if (!colorscreen::render_parameters::capture_supports_screen_detection_p(
          capture) ||
      !m_scan->has_rgb())
    return QString();

  const qsizetype count = static_cast<qsizetype>(m_profileSpots.size());
  const bool savedCalibration = m_rparams.has_correction_profile();
  if (count < 4) {
    if (savedCalibration)
      return tr("Profile: optional matrix correction • saved calibration present — provenance not verified • %1/4 spots")
          .arg(count);
    return tr("Profile: optional matrix correction • %1/4 calibration spots — add %2 more")
        .arg(count).arg(4 - count);
  }

  const ColorOptimizerRequestData current{
      m_scrToImgParams, m_rparams, m_profileSpots};
  const bool fitCurrent = m_profileCalibrationBaseline &&
      !profileCalibrationInputsDiffer(*m_profileCalibrationBaseline, current);
  const bool failureCurrent = m_profileCalibrationFailureInputs &&
      !profileCalibrationInputsDiffer(*m_profileCalibrationFailureInputs, current);

  QString summary =
      tr("Profile: optional matrix correction • %1 calibration spots")
          .arg(count);
  if (m_profileCalibrationPendingInputs) {
    summary += tr(" • optimizing…");
  } else if (fitCurrent) {
    summary += tr(" • calibration current");
    if (m_profileCalibrationAverageDeltaE >= 0)
      summary += tr(" • avg ΔE₂₀₀₀ %1")
          .arg(m_profileCalibrationAverageDeltaE, 0, 'f', 2);
    if (failureCurrent)
      summary += tr(" • last retry failed");
  } else if (failureCurrent) {
    summary += tr(" • optimization failed — adjust inputs and retry");
  } else if (m_profileCalibrationBaseline) {
    summary += tr(" • calibration stale — reoptimize");
  } else if (savedCalibration) {
    summary += tr(" • saved calibration present — provenance not verified");
  } else {
    summary += tr(" • ready to optimize");
  }
  return summary;
}

/** Refresh the persistent workflow summary''')

replace_exact(
    "src/qtgui/MainWindow.cpp",
    """  if (!m_workflowProcessLabel || !m_workflowRegistrationLabel ||\n      !m_workflowCalibrationLabel || !m_workflowNextStepLabel)\n    return;\n""",
    """  if (!m_workflowProcessLabel || !m_workflowRegistrationLabel ||\n      !m_workflowCalibrationLabel || !m_workflowProfileLabel ||\n      !m_workflowNextStepLabel)\n    return;\n""")

replace_exact(
    "src/qtgui/MainWindow.cpp",
    """  const QString profileSummary = profileCalibrationSummary();\n  if (m_profilePanel)\n    m_profilePanel->setCalibrationStatus(profileSummary);\n  m_workflowCalibrationLabel->setText(\n      sharpenSummary + QStringLiteral(\" • \") + mtfSummary +\n      QStringLiteral(\" • \") + profileSummary);\n\n  QString nextStep;\n""",
    """  const bool profileApplicable =\n      colorDetection && m_scan && m_scan->has_rgb();\n  const QString profileSummary =\n      profileApplicable ? profileCalibrationSummary() : QString();\n  if (m_profilePanel)\n    m_profilePanel->setCalibrationStatus(profileSummary);\n  m_workflowCalibrationLabel->setText(\n      sharpenSummary + QStringLiteral(\" • \") + mtfSummary);\n  m_workflowProfileLabel->setProperty(\"workflowApplicable\",\n                                      profileApplicable);\n  m_workflowProfileLabel->setText(profileSummary);\n  m_workflowProfileLabel->setVisible(\n      profileApplicable && m_workflowProcessLabel->isVisible());\n\n  QString nextStep;\n""")

# Once geometry is fitted, validate it in the reconstruction itself. This is
# more useful to an operator than inspecting solver residual arrows.
replace_exact(
    "src/qtgui/MainWindow.cpp",
    """  } else if (colorDetection && stochasticScreen) {\n    nextStep = tr(\n        \"Next: reconstruct from detected screen colours; stochastic screens \"\n        \"do not use Geometry.\");\n  } else if (colorDetection && regularScreen) {\n    nextStep = tr(\n        \"Next: choose either Geometry-based reconstruction or screen-colour \"\n        \"detection from the RGB scan.\");\n  } else if (colorscreen::render_parameters::\n                 capture_requires_regular_screen_p(capture)\n             && !regularScreen) {\n    nextStep = tr(\n        \"Next: choose the original regular Screen type. Stochastic screen \"\n        \"colors cannot be recovered from a monochrome capture.\");\n  } else if (!colorDetection && regularScreen) {\n    if (m_geometryFitPendingInputs) {\n      nextStep = tr(\"Next: Geometry fit is running…\");\n    } else if (fitCurrent) {\n      nextStep = tr(\n          \"Next: Geometry — turn on Show registration points and inspect \"\n          \"residual arrows; if they are small and patternless, reconstruct.\");\n    } else if (failureCurrent) {\n      nextStep = tr(\n          \"Next: Geometry — adjust registration points/settings and optimize \"\n          \"the fit again.\");\n    } else {\n      nextStep = tr(\n          \"Next: Geometry — optimize the fit, then turn on Show registration \"\n          \"points to inspect residual arrows.\");\n    }\n""",
    """  } else if (colorDetection && stochasticScreen) {\n    nextStep = tr(\n        \"Next: reconstruct from detected screen colours; stochastic screens \"\n        \"do not use Geometry.\");\n  } else if (regularScreen && m_geometryFitPendingInputs) {\n    nextStep = tr(\"Next: Geometry fit is running…\");\n  } else if (regularScreen && fitCurrent) {\n    nextStep = tr(\n        \"Next: reconstruct — choose Mode → Image layer + screen filter (or \"\n        \"Image layer + screen filter demosaiced with detail recovery) and \"\n        \"inspect the image. If the reconstructed colours line up cleanly, \"\n        \"the geometry is good.\");\n  } else if (colorDetection && regularScreen) {\n    nextStep = tr(\n        \"Next: choose either Geometry-based reconstruction or screen-colour \"\n        \"detection from the RGB scan.\");\n  } else if (colorscreen::render_parameters::\n                 capture_requires_regular_screen_p(capture)\n             && !regularScreen) {\n    nextStep = tr(\n        \"Next: choose the original regular Screen type. Stochastic screen \"\n        \"colors cannot be recovered from a monochrome capture.\");\n  } else if (!colorDetection && regularScreen) {\n    if (failureCurrent) {\n      nextStep = tr(\n          \"Next: Geometry — adjust registration points/settings and optimize \"\n          \"the fit again.\");\n    } else {\n      nextStep = tr(\"Next: Geometry — optimize the fit.\");\n    }\n""")

# Avoid briefly showing Profile on any generic RGB file during image-load UI
# initialization. updateRegistrationGroupVisibility() already uses the same
# capture predicate later; use it here as well.
replace_exact(
    "src/qtgui/MainWindow.cpp",
    """  if (m_profilePanel) {\n    int profileTabIndex = m_configTabs->indexOf(m_profilePanel);\n    if (profileTabIndex >= 0) {\n      m_configTabs->setTabVisible(profileTabIndex, m_scan && m_scan->has_rgb());\n    }\n  }\n""",
    """  if (m_profilePanel) {\n    int profileTabIndex = m_configTabs->indexOf(m_profilePanel);\n    if (profileTabIndex >= 0) {\n      const auto capture =\n          m_scan ? m_rparams.get_capture_type(m_scan.get())\n                 : colorscreen::render_parameters::capture_unknown;\n      const bool profileApplicable =\n          m_scan && m_scan->has_rgb() &&\n          colorscreen::render_parameters::\n              capture_supports_screen_detection_p(capture);\n      m_configTabs->setTabVisible(profileTabIndex, profileApplicable);\n    }\n  }\n""")

# Centralize conversion of rubber-band widget rectangles to scan rectangles in
# ImageWidget. Use floor/ceil explicitly rather than implicit truncation so the
# selected image area encloses the user's rectangle under fractional zoom and
# transformed coordinate spaces.
replace_exact(
    "src/qtgui/ImageWidget.h",
    """  colorscreen::point_t widgetToImage(QPointF p) const;\n""",
    """  colorscreen::point_t widgetToImage(QPointF p) const;\n\n  /** Convert a widget-local selection rectangle to a bounded scan rectangle. */\n  QRect widgetAreaToImageArea(const QRect &area) const;\n""")

replace_exact(
    "src/qtgui/ImageWidget.cpp",
    """#include <QWheelEvent>\n#include <QTimer>\n#include <QtMath>\n""",
    """#include <QWheelEvent>\n#include <QTimer>\n#include <QtMath>\n#include <cmath>\n""")

replace_exact(
    "src/qtgui/ImageWidget.cpp",
    """colorscreen::point_t ImageWidget::widgetToImage(QPointF p) const {\n  if (!m_scan || !m_scrToImg)\n    return {p.x(), p.y()};\n\n  // 1. Undo Offset & Scale (View -> Transformed-Crop)\n  double xr = p.x() / m_scale + m_viewX;\n  double yr = p.y() / m_scale + m_viewY;\n\n  // 2. Use Transformer (Transformed-Crop -> Scan)\n  CoordinateTransformer transformer(m_scan.get(), *m_rparams, m_scrToImg, m_coordinateSpace);\n  return transformer.transformedToScanCrop({xr, yr});\n}\n""",
    """colorscreen::point_t ImageWidget::widgetToImage(QPointF p) const {\n  if (!m_scan || !m_scrToImg)\n    return {p.x(), p.y()};\n\n  // 1. Undo Offset & Scale (View -> Transformed-Crop)\n  double xr = p.x() / m_scale + m_viewX;\n  double yr = p.y() / m_scale + m_viewY;\n\n  // 2. Use Transformer (Transformed-Crop -> Scan)\n  CoordinateTransformer transformer(m_scan.get(), *m_rparams, m_scrToImg, m_coordinateSpace);\n  return transformer.transformedToScanCrop({xr, yr});\n}\n\n/** Convert one rubber-band rectangle from this widget's local coordinates to\n    the enclosing scan-pixel rectangle. The rectangle is mapped here, before\n    document/tool state can change, so callers never need to reinterpret a\n    child-widget geometry in another coordinate system. */\nQRect ImageWidget::widgetAreaToImageArea(const QRect &area) const {\n  if (!m_scan || area.isEmpty())\n    return {};\n\n  const colorscreen::point_t p1 = widgetToImage(area.topLeft());\n  const colorscreen::point_t p2 = widgetToImage(area.topRight());\n  const colorscreen::point_t p3 = widgetToImage(area.bottomLeft());\n  const colorscreen::point_t p4 = widgetToImage(area.bottomRight());\n  const double xmin = std::min({p1.x, p2.x, p3.x, p4.x});\n  const double xmax = std::max({p1.x, p2.x, p3.x, p4.x});\n  const double ymin = std::min({p1.y, p2.y, p3.y, p4.y});\n  const double ymax = std::max({p1.y, p2.y, p3.y, p4.y});\n\n  const int left = std::max(0, static_cast<int>(std::floor(xmin)));\n  const int top = std::max(0, static_cast<int>(std::floor(ymin)));\n  const int right = std::min(static_cast<int>(m_scan->width) - 1,\n                             static_cast<int>(std::ceil(xmax)));\n  const int bottom = std::min(static_cast<int>(m_scan->height) - 1,\n                              static_cast<int>(std::ceil(ymax)));\n  if (right < left || bottom < top)\n    return {};\n  return QRect(left, top, right - left + 1, bottom - top + 1);\n}\n""")

replace_regex(
    "src/qtgui/MainWindow.cpp",
    r"/\*\* Convert a widget-space rectangle to an image-space rectangle\..*?\nQRect MainWindow::getImageArea\(QRect area, ImageWidget \*imageWidget\) \{.*?\n\}\n\n/\*\* Dispatch a drawn rectangle",
    r'''/** Convert a widget-local rectangle to the enclosing scan rectangle. */
QRect MainWindow::getImageArea(QRect area, ImageWidget *imageWidget) {
  ImageWidget *image = imageWidget ? imageWidget : inspectorImageWidget();
  return image ? image->widgetAreaToImageArea(area) : QRect();
}

/** Dispatch a drawn rectangle''')

replace_regex(
    "src/qtgui/ImageViewWindow.cpp",
    r"/\*\* Convert a widget-space selection to a bounded rectangle in reference scan\. \*/\nQRect ImageViewWindow::referenceImageArea\(QRect area\) const \{.*?\n\}\n\n/\*\* Measure the selected reference edge",
    r'''/** Convert a widget-local selection to the reference scan rectangle. */
QRect ImageViewWindow::referenceImageArea(QRect area) const {
  return m_imageWidget ? m_imageWidget->widgetAreaToImageArea(area) : QRect();
}

/** Measure the selected reference edge''')

# Update smoke expectations for the separated optional Profile line.
replace_exact(
    "src/qtgui/WorkspaceChurnSmoke.cpp",
    """QLabel *calibrationSummary = inspector->findChild<QLabel *>(\n    QStringLiteral(\"WorkflowCalibrationSummary\"));\nQLabel *nextStepSummary = inspector->findChild<QLabel *>(\n    QStringLiteral(\"WorkflowNextStepSummary\"));\n""",
    """QLabel *calibrationSummary = inspector->findChild<QLabel *>(\n    QStringLiteral(\"WorkflowCalibrationSummary\"));\nQLabel *profileSummary = inspector->findChild<QLabel *>(\n    QStringLiteral(\"WorkflowProfileSummary\"));\nQLabel *nextStepSummary = inspector->findChild<QLabel *>(\n    QStringLiteral(\"WorkflowNextStepSummary\"));\n""")

replace_exact(
    "src/qtgui/WorkspaceChurnSmoke.cpp",
    """    !processSummary || !registrationSummary || !calibrationSummary ||\n    !nextStepSummary || !captureTypeCombo ||\n""",
    """    !processSummary || !registrationSummary || !calibrationSummary ||\n    !profileSummary || !nextStepSummary || !captureTypeCombo ||\n""")

replace_exact(
    "src/qtgui/WorkspaceChurnSmoke.cpp",
    """    !calibrationSummary->text().contains(\n        QStringLiteral(\"Capture MTF:\")) ||\n    !calibrationSummary->text().contains(QStringLiteral(\"Profile:\")) ||\n    !nextStepSummary->text().startsWith(QStringLiteral(\"Next:\")) ||\n""",
    """    !calibrationSummary->text().contains(\n        QStringLiteral(\"Capture MTF:\")) ||\n    calibrationSummary->text().contains(QStringLiteral(\"Profile:\")) ||\n    !nextStepSummary->text().startsWith(QStringLiteral(\"Next:\")) ||\n""")

replace_exact(
    "src/qtgui/WorkspaceChurnSmoke.cpp",
    """    processSummary->font().weight() < QFont::DemiBold ||\n    registrationSummary->font().weight() < QFont::DemiBold ||\n    calibrationSummary->font().weight() < QFont::DemiBold) {\n""",
    """    processSummary->font().weight() < QFont::DemiBold ||\n    registrationSummary->font().weight() < QFont::DemiBold ||\n    calibrationSummary->font().weight() < QFont::DemiBold ||\n    profileSummary->font().weight() < QFont::DemiBold) {\n""")

# Static sanity checks: no workflow path may still instruct users to validate a
# fitted geometry by showing registration points, Profile stays separate from
# sharpening/MTF, and both document/reference selections share one conversion.
main = Path("src/qtgui/MainWindow.cpp").read_text()
if "turn on Show registration points and inspect residual arrows" in main:
    raise SystemExit("obsolete registration-point validation guidance remains")
if "sharpenSummary + QStringLiteral(\" • \") + mtfSummary +" in main:
    raise SystemExit("Profile is still mixed into Sharpening/MTF workflow summary")
if "capture_supports_screen_detection_p(capture)" not in main:
    raise SystemExit("Profile applicability is not capture-screen-data aware")

image_widget = Path("src/qtgui/ImageWidget.cpp").read_text()
if image_widget.count("widgetAreaToImageArea") != 1:
    raise SystemExit("selection conversion helper definition is missing/duplicated")
if "std::floor(xmin)" not in image_widget or "std::ceil(xmax)" not in image_widget:
    raise SystemExit("selection bounds do not use enclosing floor/ceil rounding")

print("workflow/profile/selection transformer completed successfully")
