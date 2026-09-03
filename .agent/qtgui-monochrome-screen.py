from pathlib import Path


def replace_one(path, old, new):
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one match, found {count}\n--- old ---\n{old}")
    p.write_text(text.replace(old, new, 1))


replace_one(
    "src/libcolorscreen/include/render-parameters.h",
    """    /* Monochrome positive capture with no attached color screen.  */\n    capture_transparency,\n    /* Monochrome negative capture with no attached color screen.  */\n    capture_negative,\n""",
    """    /* Monochrome positive made through a separate historical color screen.\n       The screen colors are not present in the captured data.  */\n    capture_transparency,\n    /* Monochrome negative made through a separate historical color screen.\n       The screen colors are not present in the captured data.  */\n    capture_negative,\n""",
)

replace_one(
    "src/libcolorscreen/include/render-parameters.h",
    """    /* Human readable name.  */\n    const char *pretty_name;\n    /* Flags.  */\n    int flags;\n    /* Supported flags.  */\n    enum flag\n    {\n      SUPPORTS_SCR_DETECT = 1,\n      HAS_IR = 2,\n      MAYBE_MONOCHROMATIC_DEMOSAIC = 4\n    };\n""",
    """    /* Human readable name.  */\n    const char *pretty_name;\n    /* Explanation shown by user interfaces.  */\n    const char *help;\n    /* Flags.  */\n    int flags;\n    /* Supported flags.  */\n    enum flag\n    {\n      SUPPORTS_SCR_DETECT = 1,\n      HAS_IR = 2,\n      MAYBE_MONOCHROMATIC_DEMOSAIC = 4,\n      USES_SCREEN = 8,\n      REGULAR_SCREEN_ONLY = 16\n    };\n""",
)

replace_one(
    "src/libcolorscreen/include/render-parameters.h",
    """  /* Return true if CAPTURE_TYPE describes a plate carrying a historical\n     additive color screen.  */\n  pure_attr static bool\n  capture_has_screen_p (enum capture_type capture_type)\n  {\n    return capture_type == capture_transparency_with_screen\n           || capture_type == capture_transparency_with_screen_and_infrared\n           || capture_type == capture_negative_with_screen\n           || capture_type == capture_negative_with_screen_and_infrared;\n  }\n\n  /* Return true if CAPTURE_TYPE has RGB information from which individual\n     screen elements can be detected without a geometric lattice.  */\n  pure_attr static bool\n  capture_supports_screen_detection_p (enum capture_type capture_type)\n  {\n    return capture_type == capture_transparency_with_screen\n           || capture_type == capture_transparency_with_screen_and_infrared\n           || capture_type == capture_negative_with_screen\n           || capture_type == capture_negative_with_screen_and_infrared;\n  }\n""",
    """  /* Return true if reconstruction of CAPTURE_TYPE uses a historical additive\n     color screen.  For monochrome-through-screen captures the screen itself is\n     separate or its colors were suppressed by capture (for example in IR), so\n     it must be re-attached geometrically.  */\n  pure_attr static bool\n  capture_has_screen_p (enum capture_type capture_type)\n  {\n    return capture_type >= capture_unknown && capture_type < capture_max\n           && (capture_properties[(int)capture_type].flags\n               & capture_type_property::USES_SCREEN);\n  }\n\n  /* Return true if CAPTURE_TYPE has RGB information from which individual\n     screen elements can be detected without a geometric lattice.  */\n  pure_attr static bool\n  capture_supports_screen_detection_p (enum capture_type capture_type)\n  {\n    return capture_type >= capture_unknown && capture_type < capture_max\n           && (capture_properties[(int)capture_type].flags\n               & capture_type_property::SUPPORTS_SCR_DETECT);\n  }\n\n  /* Return true if the screen colors are absent from CAPTURE_TYPE, so a\n     stochastic screen cannot be reconstructed and the operator must choose a\n     regular screen with geometry.  */\n  pure_attr static bool\n  capture_requires_regular_screen_p (enum capture_type capture_type)\n  {\n    return capture_type >= capture_unknown && capture_type < capture_max\n           && (capture_properties[(int)capture_type].flags\n               & capture_type_property::REGULAR_SCREEN_ONLY);\n  }\n""",
)

replace_one(
    "src/libcolorscreen/include/render-parameters.h",
    """  /* Return true when CAPTURE_TYPE is compatible with the channel layout of\n     a loaded capture.  Plain monochrome captures require a scalar image;\n     screen captures require RGB, and RGB+IR variants additionally require\n     the fourth grayscale/infrared plane.  */\n""",
    """  /* Return true when CAPTURE_TYPE is compatible with the channel layout of\n     a loaded capture.  Monochrome-through-screen captures may be stored either\n     as a scalar image or in an RGB container whose channels carry the same\n     monochrome signal; captures with recorded screen colors require RGB, and\n     RGB+IR variants additionally require the fourth grayscale/infrared plane.  */\n""",
)
replace_one(
    "src/libcolorscreen/include/render-parameters.h",
    """      case capture_transparency:\n      case capture_negative:\n        return !has_rgb;\n""",
    """      case capture_transparency:\n      case capture_negative:\n        return has_rgb || has_ir;\n""",
)
replace_one(
    "src/libcolorscreen/include/render-parameters.h",
    """  /* Return the capture type only when it matches the loaded image.  A\n     parameter file may describe a different capture, so incompatible RGB/IR or\n     monochrome choices are deliberately demoted to Unknown rather than silently\n     changing their physical meaning.  */\n""",
    """  /* Return the capture type only when it matches the loaded image.  A\n     parameter file may describe a different capture, so incompatible RGB/IR or\n     channel-layout choices are deliberately demoted to Unknown rather than\n     silently changing their physical meaning.  */\n""",
)

replace_one(
    "src/libcolorscreen/scr-to-img.C",
    """const render_parameters::capture_type_property render_parameters::capture_properties[] = {\n  { \"unknown\", \"Unknown\", 0 },\n  { \"positive-screen-rgb\", \"Color positive with screen\",\n    render_parameters::capture_type_property::SUPPORTS_SCR_DETECT },\n  { \"positive-screen-rgb-ir\", \"Color positive + IR with screen\",\n    render_parameters::capture_type_property::SUPPORTS_SCR_DETECT | render_parameters::capture_type_property::HAS_IR },\n  { \"negative-screen-rgb\", \"Color negative with screen\",\n    render_parameters::capture_type_property::SUPPORTS_SCR_DETECT },\n  { \"negative-screen-rgb-ir\", \"Color negative + IR with screen\",\n    render_parameters::capture_type_property::SUPPORTS_SCR_DETECT | render_parameters::capture_type_property::HAS_IR },\n  { \"positive-mono\", \"Monochrome positive (no color screen)\",\n    render_parameters::capture_type_property::MAYBE_MONOCHROMATIC_DEMOSAIC },\n  { \"negative-mono\", \"Monochrome negative (no color screen)\",\n    render_parameters::capture_type_property::MAYBE_MONOCHROMATIC_DEMOSAIC },\n  { \"ordinary-image\", \"Ordinary image (no color screen)\", 0 },\n};\n""",
    """const render_parameters::capture_type_property render_parameters::capture_properties[] = {\n  { \"unknown\", \"Unknown\",\n    \"Capture type is not known yet. The GUI keeps the restoration workflow \"\n    \"conservative until it is selected.\",\n    0 },\n  { \"positive-screen-rgb\", \"Color positive with screen\",\n    \"Color transparency in which the historical additive screen colors are \"\n    \"recorded in RGB. Regular and stochastic screens can be reconstructed.\",\n    render_parameters::capture_type_property::SUPPORTS_SCR_DETECT\n    | render_parameters::capture_type_property::USES_SCREEN },\n  { \"positive-screen-rgb-ir\", \"Color positive + IR with screen\",\n    \"Color transparency with recorded additive screen colors plus a separate \"\n    \"infrared/grayscale channel.\",\n    render_parameters::capture_type_property::SUPPORTS_SCR_DETECT\n    | render_parameters::capture_type_property::HAS_IR\n    | render_parameters::capture_type_property::USES_SCREEN },\n  { \"negative-screen-rgb\", \"Color negative with screen\",\n    \"Color negative in which the historical additive screen colors are \"\n    \"recorded in RGB. Regular and stochastic screens can be reconstructed.\",\n    render_parameters::capture_type_property::SUPPORTS_SCR_DETECT\n    | render_parameters::capture_type_property::USES_SCREEN },\n  { \"negative-screen-rgb-ir\", \"Color negative + IR with screen\",\n    \"Color negative with recorded additive screen colors plus a separate \"\n    \"infrared/grayscale channel.\",\n    render_parameters::capture_type_property::SUPPORTS_SCR_DETECT\n    | render_parameters::capture_type_property::HAS_IR\n    | render_parameters::capture_type_property::USES_SCREEN },\n  { \"positive-mono\", \"Monochrome transparency taken through color screen\",\n    \"Monochrome transparency produced through a separate additive color \"\n    \"screen, or a capture in which the screen colors are not recorded (for \"\n    \"example an infrared capture of a screen transparency). The original \"\n    \"screen must be re-attached during reconstruction. Choose the original \"\n    \"regular screen type and fit its geometry; stochastic screen colors \"\n    \"cannot be recovered from monochrome data.\",\n    render_parameters::capture_type_property::MAYBE_MONOCHROMATIC_DEMOSAIC\n    | render_parameters::capture_type_property::USES_SCREEN\n    | render_parameters::capture_type_property::REGULAR_SCREEN_ONLY },\n  { \"negative-mono\", \"Monochrome negative taken through color screen\",\n    \"Monochrome negative exposed through a separate additive color screen. \"\n    \"The screen filter is not part of the negative and must be re-attached \"\n    \"when producing the reconstructed positive. Choose the original regular \"\n    \"screen type and fit its geometry; stochastic screen colors cannot be \"\n    \"recovered from a monochrome negative.\",\n    render_parameters::capture_type_property::MAYBE_MONOCHROMATIC_DEMOSAIC\n    | render_parameters::capture_type_property::USES_SCREEN\n    | render_parameters::capture_type_property::REGULAR_SCREEN_ONLY },\n  { \"ordinary-image\", \"Ordinary image (no color screen)\",\n    \"Ordinary image with no historical additive color screen. Screen \"\n    \"registration and reconstruction are not applicable.\",\n    0 },\n};\n""",
)

replace_one(
    "src/qtgui/CapturePanel.cpp",
    """    for (int i = 0; i < (int)colorscreen::render_parameters::capture_max; ++i)\n      m_captureTypeCombo->addItem(\n          QString::fromUtf8(\n              colorscreen::render_parameters::capture_properties[i].pretty_name),\n          i);\n""",
    """    for (int i = 0; i < (int)colorscreen::render_parameters::capture_max; ++i) {\n      const auto &property =\n          colorscreen::render_parameters::capture_properties[i];\n      m_captureTypeCombo->addItem(QString::fromUtf8(property.pretty_name), i);\n      if (property.help && property.help[0])\n        m_captureTypeCombo->setItemData(\n            m_captureTypeCombo->count() - 1, QString::fromUtf8(property.help),\n            Qt::ToolTipRole);\n    }\n""",
)
replace_one(
    "src/qtgui/CapturePanel.cpp",
    """          if (!img || capture == colorscreen::render_parameters::capture_unknown ||\n              colorscreen::render_parameters::capture_type_compatible_p(\n                  capture, img.get()))\n            m_captureTypeCombo->addItem(\n                QString::fromUtf8(colorscreen::render_parameters::\n                                      capture_properties[i].pretty_name),\n                i);\n""",
    """          if (!img || capture == colorscreen::render_parameters::capture_unknown ||\n              colorscreen::render_parameters::capture_type_compatible_p(\n                  capture, img.get())) {\n            const auto &property =\n                colorscreen::render_parameters::capture_properties[i];\n            m_captureTypeCombo->addItem(QString::fromUtf8(property.pretty_name),\n                                        i);\n            if (property.help && property.help[0])\n              m_captureTypeCombo->setItemData(\n                  m_captureTypeCombo->count() - 1,\n                  QString::fromUtf8(property.help), Qt::ToolTipRole);\n          }\n""",
)
replace_one(
    "src/qtgui/CapturePanel.cpp",
    """            return colorscreen::render_parameters::capture_has_screen_p(capture)\n                   && (colorscreen::screen_has_regular_geometry_p(\n                           s.scrToImg.type)\n                       || (s.scrToImg.type == colorscreen::NoScreen\n                           && img->has_rgb()));\n""",
    """            if (!colorscreen::render_parameters::capture_has_screen_p(capture))\n              return false;\n            if (colorscreen::render_parameters::\n                    capture_requires_regular_screen_p(capture))\n              return colorscreen::screen_has_regular_geometry_p(\n                  s.scrToImg.type);\n            return colorscreen::screen_has_regular_geometry_p(s.scrToImg.type)\n                   || (s.scrToImg.type == colorscreen::NoScreen\n                       && img->has_rgb());\n""",
)

replace_one(
    "src/qtgui/ScreenPanel.cpp",
    """#include <QPushButton>\n#include <QStyleOptionComboBox>\n""",
    """#include <QPushButton>\n#include <QStandardItemModel>\n#include <QStyleOptionComboBox>\n""",
)
replace_one(
    "src/qtgui/ScreenPanel.cpp",
    """  screenCombo->setToolTip(\n      \"Select the physical color screen process. None means no historical \"\n      \"screen; Random, Autochrome and Agfa Farbenplatte are stochastic \"\n      \"screens without a regular geometric lattice.\");\n""",
    """  screenCombo->setToolTip(\n      \"Select the physical color screen process. None means no historical \"\n      \"screen; Random, Autochrome and Agfa Farbenplatte are stochastic \"\n      \"screens without a regular geometric lattice. Monochrome captures made \"\n      \"through a color screen require a regular screen because the color \"\n      \"identity of stochastic screen elements is not recoverable.\");\n""",
)
replace_one(
    "src/qtgui/ScreenPanel.cpp",
    """  // Updater: State -> UI\n  m_paramUpdaters.push_back([screenCombo](const ParameterState &state) {\n    int val = (int)state.scrToImg.type;\n""",
    """  // Updater: State -> UI\n  m_paramUpdaters.push_back([this, screenCombo](const ParameterState &state) {\n    const auto img = m_imageGetter();\n    const auto capture =\n        img ? state.rparams.get_capture_type(img.get())\n            : state.rparams.capture_type;\n    const bool regularOnly =\n        render_parameters::capture_requires_regular_screen_p(capture);\n    if (auto *model =\n            qobject_cast<QStandardItemModel *>(screenCombo->model())) {\n      for (int i = 0; i < screenCombo->count(); ++i) {\n        const auto type = (scr_type)screenCombo->itemData(i).toInt();\n        if (QStandardItem *item = model->item(i))\n          item->setEnabled(!regularOnly || screen_has_regular_geometry_p(type));\n      }\n    }\n\n    int val = (int)state.scrToImg.type;\n""",
)
replace_one(
    "src/qtgui/ScreenPanel.cpp",
    """          return colorscreen::render_parameters::capture_has_screen_p(capture)\n                 && (colorscreen::screen_has_regular_geometry_p(\n                         s.scrToImg.type)\n                     || (s.scrToImg.type == colorscreen::NoScreen\n                         && img->has_rgb()));\n""",
    """          if (!render_parameters::capture_has_screen_p(capture))\n            return false;\n          if (render_parameters::capture_requires_regular_screen_p(capture))\n            return screen_has_regular_geometry_p(s.scrToImg.type);\n          return screen_has_regular_geometry_p(s.scrToImg.type)\n                 || (s.scrToImg.type == NoScreen && img->has_rgb());\n""",
)

replace_one(
    "src/qtgui/MainWindow.cpp",
    """          // RGB is only a Bayer-container detail in this case. Until we have\n          // additive-process recognition, do not offer color-screen workflows.\n""",
    """          // RGB may only be a Bayer-container detail here. Offer the\n          // monochrome-through-screen paths, which reconstruct a regular\n          // screen geometrically, but not RGB screen-color detection paths.\n""",
)
replace_one(
    "src/qtgui/MainWindow.cpp",
    """        if (show)\n          m_captureType->addItem(\n              QString::fromUtf8(colorscreen::render_parameters::\n                                    capture_properties[i].pretty_name),\n              i);\n""",
    """        if (show) {\n          const auto &property =\n              colorscreen::render_parameters::capture_properties[i];\n          m_captureType->addItem(QString::fromUtf8(property.pretty_name), i);\n          if (property.help && property.help[0])\n            m_captureType->setItemData(\n                m_captureType->count() - 1, QString::fromUtf8(property.help),\n                Qt::ToolTipRole);\n        }\n""",
)
replace_one(
    "src/qtgui/MainWindow.cpp",
    """  const bool hasScreenCapture =\n      m_scan && colorscreen::render_parameters::capture_has_screen_p(capture);\n  const bool hasRegularGeometry =\n""",
    """  const bool hasScreenCapture =\n      m_scan && colorscreen::render_parameters::capture_has_screen_p(capture);\n  const bool hasScreenColorData =\n      m_scan && colorscreen::render_parameters::\n                    capture_supports_screen_detection_p(capture);\n  const bool hasRegularGeometry =\n""",
)
replace_one(
    "src/qtgui/MainWindow.cpp",
    """    setPanelVisible(m_profilePanel,\n                    hasScreenCapture && m_scan && m_scan->has_rgb());\n""",
    """    setPanelVisible(m_profilePanel,\n                    hasScreenColorData && m_scan && m_scan->has_rgb());\n""",
)
replace_one(
    "src/qtgui/MainWindow.cpp",
    """  } else if (colorDetection && regularScreen) {\n    nextStep = tr(\n        \"Next: choose either Geometry-based reconstruction or screen-colour \"\n        \"detection from the RGB scan.\");\n  } else if (hasScreen && type == colorscreen::NoScreen) {\n    nextStep = tr(\"Next: choose the physical Screen type.\");\n  } else {\n    nextStep = tr(\"Next: reconstruct the image and refine Color/Profile.\");\n  }\n""",
    """  } else if (colorDetection && regularScreen) {\n    nextStep = tr(\n        \"Next: choose either Geometry-based reconstruction or screen-colour \"\n        \"detection from the RGB scan.\");\n  } else if (colorscreen::render_parameters::\n                 capture_requires_regular_screen_p(capture)\n             && !regularScreen) {\n    nextStep = tr(\n        \"Next: choose the original regular Screen type. Stochastic screen \"\n        \"colors cannot be recovered from a monochrome capture.\");\n  } else if (!colorDetection && regularScreen) {\n    nextStep = tr(\n        \"Next: fit and validate Geometry so the original color screen can be \"\n        \"re-attached to the monochrome capture.\");\n  } else if (hasScreen && type == colorscreen::NoScreen) {\n    nextStep = tr(\"Next: choose the physical Screen type.\");\n  } else {\n    nextStep = tr(\"Next: reconstruct the image and refine Color/Profile.\");\n  }\n""",
)

replace_one(
    "testsuite/unittests.test",
    """  if (render_parameters::capture_has_screen_p (\n          render_parameters::capture_plain_image)\n      || render_parameters::capture_has_screen_p (\n          render_parameters::capture_transparency)\n      || render_parameters::capture_has_screen_p (\n          render_parameters::capture_negative)\n      || !render_parameters::capture_has_screen_p (\n          render_parameters::capture_transparency_with_screen)\n      || !render_parameters::capture_supports_screen_detection_p (\n          render_parameters::capture_transparency_with_screen)\n      || !render_parameters::capture_type_compatible_p (\n          render_parameters::capture_transparency, false, true)\n      || render_parameters::capture_type_compatible_p (\n          render_parameters::capture_transparency, true, false)\n      || !render_parameters::capture_type_compatible_p (\n          render_parameters::capture_transparency_with_screen, true, false)\n""",
    """  if (render_parameters::capture_has_screen_p (\n          render_parameters::capture_plain_image)\n      || !render_parameters::capture_has_screen_p (\n          render_parameters::capture_transparency)\n      || !render_parameters::capture_has_screen_p (\n          render_parameters::capture_negative)\n      || !render_parameters::capture_has_screen_p (\n          render_parameters::capture_transparency_with_screen)\n      || render_parameters::capture_supports_screen_detection_p (\n          render_parameters::capture_transparency)\n      || render_parameters::capture_supports_screen_detection_p (\n          render_parameters::capture_negative)\n      || !render_parameters::capture_supports_screen_detection_p (\n          render_parameters::capture_transparency_with_screen)\n      || !render_parameters::capture_requires_regular_screen_p (\n          render_parameters::capture_transparency)\n      || !render_parameters::capture_requires_regular_screen_p (\n          render_parameters::capture_negative)\n      || render_parameters::capture_requires_regular_screen_p (\n          render_parameters::capture_transparency_with_screen)\n      || !render_parameters::capture_type_compatible_p (\n          render_parameters::capture_transparency, false, true)\n      || !render_parameters::capture_type_compatible_p (\n          render_parameters::capture_transparency, true, false)\n      || !render_parameters::capture_type_compatible_p (\n          render_parameters::capture_transparency_with_screen, true, false)\n""",
)
replace_one(
    "testsuite/unittests.test",
    """      || render_parameters::get_capture_type (\n             render_parameters::capture_transparency, &rgb_scan)\n             != render_parameters::capture_unknown\n""",
    """      || render_parameters::get_capture_type (\n             render_parameters::capture_transparency, &rgb_scan)\n             != render_parameters::capture_transparency\n""",
)

replace_one(
    "doc/qtgui-workflow-roadmap.md",
    """creative adjustment.\n\n### Stage 4 — Screen detection and geometry\n""",
    """creative adjustment.\n\nMonochrome transparency/negative captures made through a separate additive\ncolour screen remain historical-screen workflows even though the screen colours\nare absent from the captured data.  This includes an infrared capture that\neffectively suppresses the screen colours of an otherwise screened\ntransparency.  In these cases the operator must choose the original **regular**\nscreen type and fit its geometry so the screen can be re-attached during\nreconstruction.  Stochastic screens (Random, Autochrome, Agfa Farbenplatte)\ncannot be reconstructed from monochrome data because the colour identity of\ntheir individual screen elements has been lost.\n\n### Stage 4 — Screen detection and geometry\n""",
)

print("Applied monochrome-through-screen capture workflow correction")
