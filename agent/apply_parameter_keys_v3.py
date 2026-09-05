from pathlib import Path

source = Path("helper/agent/apply_parameter_keys_v2.py").read_text()
old = '''# Derived panels keep their refresh hooks when keyed helpers call applyChange.
for panel in ("SharpnessPanel", "ColorPanel"):
    path = f"src/qtgui/{panel}.h"
    replace_exact(
        path,
        "  void applyChange(std::function<void(ParameterState &)> modifier, const QString &description = QString()) override;",
        "  void applyChange(std::function<void(ParameterState &)> modifier,\\n"
        "                   const QString &description = QString(),\\n"
        "                   const QString &parameterKey = QString()) override;",
    )
    path = f"src/qtgui/{panel}.cpp"
    replace_exact(
        path,
        f"void {panel}::applyChange(\\n"
        "    std::function<void(ParameterState &)> modifier, const QString &description) {\\n"
        "  ParameterPanel::applyChange(modifier, description);",
        f"void {panel}::applyChange(\\n"
        "    std::function<void(ParameterState &)> modifier, const QString &description,\\n"
        "    const QString &parameterKey) {\\n"
        "  ParameterPanel::applyChange(modifier, description, parameterKey);",
    )
'''
new = '''# Derived panels keep their refresh hooks when keyed helpers call applyChange.
for panel in ("SharpnessPanel", "ColorPanel"):
    path = f"src/qtgui/{panel}.h"
    replace_exact(
        path,
        "  void applyChange(std::function<void(ParameterState &)> modifier, const QString &description = QString()) override;",
        "  void applyChange(std::function<void(ParameterState &)> modifier,\\n"
        "                   const QString &description = QString(),\\n"
        "                   const QString &parameterKey = QString()) override;",
    )

path = "src/qtgui/SharpnessPanel.cpp"
replace_exact(
    path,
    "void SharpnessPanel::applyChange(\\n"
    "    std::function<void(ParameterState &)> modifier, const QString &description) {\\n"
    "  ParameterPanel::applyChange(modifier, description);",
    "void SharpnessPanel::applyChange(\\n"
    "    std::function<void(ParameterState &)> modifier, const QString &description,\\n"
    "    const QString &parameterKey) {\\n"
    "  ParameterPanel::applyChange(modifier, description, parameterKey);",
)
path = "src/qtgui/ColorPanel.cpp"
replace_exact(
    path,
    "void ColorPanel::applyChange(std::function<void(ParameterState &)> modifier, const QString &description) {\\n"
    "  ParameterPanel::applyChange(modifier, description);",
    "void ColorPanel::applyChange(std::function<void(ParameterState &)> modifier,\\n"
    "                             const QString &description,\\n"
    "                             const QString &parameterKey) {\\n"
    "  ParameterPanel::applyChange(modifier, description, parameterKey);",
)
'''
if source.count(old) != 1:
    raise RuntimeError("could not locate v2 derived-panel migration block")
source = source.replace(old, new)
exec(compile(source, "apply_parameter_keys_v3_generated.py", "exec"), {"__name__": "__main__"})
