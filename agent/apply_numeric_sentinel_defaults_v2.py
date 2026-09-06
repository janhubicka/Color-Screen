from pathlib import Path

script_path = Path("helper/agent/apply_numeric_sentinel_defaults.py")
script = script_path.read_text()

old = r'''replace_exact(
    path,
    "  // Container: Slider + SpinBox\n"
    "  QWidget *container = new QWidget();\n"
    "  QHBoxLayout *hLayout = new QHBoxLayout(container);\n"
    "  hLayout->setContentsMargins(0, 0, 0, 0);\n\n"
    "  QSlider *slider = new QSlider(Qt::Horizontal);",
    "  Q_ASSERT(!specialMinimumValue.has_value() ||\n"
    "           *specialMinimumValue <= min);\n"
    "  const bool hasSeparatedSpecialMinimum =\n"
    "      specialMinimumValue.has_value() && *specialMinimumValue < min;\n"
    "  const double specialStateValue =\n"
    "      specialMinimumValue.value_or(min);\n\n"
    "  // Container: Slider + SpinBox\n"
    "  QWidget *container = new QWidget();\n"
    "  QHBoxLayout *hLayout = new QHBoxLayout(container);\n"
    "  hLayout->setContentsMargins(0, 0, 0, 0);\n\n"
    "  QSlider *slider = new QSlider(Qt::Horizontal);",
)
'''

new = r'''replace_exact(
    path,
    "    const QString &parameterKey, bool showDefaultReset,\n"
    "    std::optional<double> specialMinimumValue) {\n"
    "  // Container: Slider + SpinBox\n"
    "  QWidget *container = new QWidget();\n"
    "  QHBoxLayout *hLayout = new QHBoxLayout(container);\n"
    "  hLayout->setContentsMargins(0, 0, 0, 0);\n\n"
    "  QSlider *slider = new QSlider(Qt::Horizontal);",
    "    const QString &parameterKey, bool showDefaultReset,\n"
    "    std::optional<double> specialMinimumValue) {\n"
    "  Q_ASSERT(!specialMinimumValue.has_value() ||\n"
    "           *specialMinimumValue <= min);\n"
    "  const bool hasSeparatedSpecialMinimum =\n"
    "      specialMinimumValue.has_value() && *specialMinimumValue < min;\n"
    "  const double specialStateValue =\n"
    "      specialMinimumValue.value_or(min);\n\n"
    "  // Container: Slider + SpinBox\n"
    "  QWidget *container = new QWidget();\n"
    "  QHBoxLayout *hLayout = new QHBoxLayout(container);\n"
    "  hLayout->setContentsMargins(0, 0, 0, 0);\n\n"
    "  QSlider *slider = new QSlider(Qt::Horizontal);",
)
'''

count = script.count(old)
if count != 1:
    raise RuntimeError(f"expected one broad slider migration block, found {count}")
script = script.replace(old, new)
exec(compile(script, str(script_path), "exec"))
