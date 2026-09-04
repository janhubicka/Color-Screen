from pathlib import Path

path = Path('src/qtgui/WorkspaceChurnSmoke.cpp')
text = path.read_text(encoding='utf-8')

anchor = '''QPushButton *mtfMeasurementLocate = inspector->findChild<QPushButton *>(
    QStringLiteral("MtfMeasurementLocate"));

if (!workflowSummary || !workflowToggle || !workflowStages ||
'''
replacement = '''QPushButton *mtfMeasurementLocate = inspector->findChild<QPushButton *>(
    QStringLiteral("MtfMeasurementLocate"));
const auto profileCapture =
    first->documentStateSnapshot().rparams.get_capture_type(
        first->sharedImageData().get());
const bool profileApplicable =
    first->sharedImageData()->has_rgb() &&
    colorscreen::render_parameters::capture_supports_screen_detection_p(
        profileCapture);

if (!workflowSummary || !workflowToggle || !workflowStages ||
'''
if text.count(anchor) != 1:
    raise SystemExit(f'expected one profile applicability anchor, found {text.count(anchor)}')
text = text.replace(anchor, replacement)

anchor = '''    calibrationSummary->text().contains(QStringLiteral("Profile:")) ||
    !nextStepSummary->text().startsWith(QStringLiteral("Next:")) ||
'''
replacement = '''    calibrationSummary->text().contains(QStringLiteral("Profile:")) ||
    profileSummary->property("workflowApplicable").toBool() !=
        profileApplicable ||
    (profileApplicable
         ? !profileSummary->text().startsWith(QStringLiteral("Profile:"))
         : !profileSummary->text().isEmpty()) ||
    !nextStepSummary->text().startsWith(QStringLiteral("Next:")) ||
'''
if text.count(anchor) != 1:
    raise SystemExit(f'expected one workflow profile assertion anchor, found {text.count(anchor)}')
text = text.replace(anchor, replacement)

old = '''        if (!profileCalibrationStatus || !profileOptimizeButton ||
            !profileCalibrationStatus->text().startsWith(
                QStringLiteral("Profile:")) ||
            !mtfCalibrationStatus || !mtfMeasurementSelector ||
'''
new = '''        if (!profileCalibrationStatus || !profileOptimizeButton ||
            (profileApplicable
                 ? !profileCalibrationStatus->text().startsWith(
                       QStringLiteral("Profile:"))
                 : !profileCalibrationStatus->text().isEmpty()) ||
            !mtfCalibrationStatus || !mtfMeasurementSelector ||
'''
if text.count(old) != 1:
    raise SystemExit(f'expected one profile status assertion, found {text.count(old)}')
text = text.replace(old, new)

old = '''          fail(QStringLiteral(
              "Workspace churn source document lost MTF calibration/provenance controls"));
'''
new = '''          fail(QStringLiteral(
              "Workspace churn source document lost calibration/provenance controls"));
'''
if text.count(old) != 1:
    raise SystemExit(f'expected one calibration failure message, found {text.count(old)}')
text = text.replace(old, new)

if 'const bool profileApplicable =' not in text:
    raise SystemExit('profile applicability derivation missing')
if text.count('property("workflowApplicable")') != 1:
    raise SystemExit('workflow applicability assertion missing or duplicated')
if '''!profileCalibrationStatus->text().startsWith(
                QStringLiteral("Profile:"))''' in text:
    raise SystemExit('obsolete unconditional Profile status assertion remains')

path.write_text(text, encoding='utf-8')
