from pathlib import Path

script_path = Path("helper/agent/apply_numeric_sentinel_defaults.py")
script = script_path.read_text()

old = '''def replace_exact(path, old, new, expected=1):
    file_path = ROOT / path
    text = file_path.read_text()
    count = text.count(old)
    if count != expected:
        raise RuntimeError(
            f"{path}: expected {expected} copies, found {count}: {old[:120]!r}"
        )
    file_path.write_text(text.replace(old, new))
'''

new = '''def replace_exact(path, old, new, expected=1):
    file_path = ROOT / path
    text = file_path.read_text()
    count = text.count(old)
    if count == expected:
        file_path.write_text(text.replace(old, new))
        return

    # ParameterPanel intentionally shares slider implementation snippets
    # between the persistent-state helper and the stateless helper.  Sentinel
    # support belongs only to addSliderParameter().  When an otherwise exact
    # source fragment is duplicated, constrain the replacement to that one
    # function instead of weakening the match globally.
    if path == "src/qtgui/ParameterPanel.cpp":
        start = text.find("QWidget *ParameterPanel::addSliderParameter(")
        end = text.find("\\nQWidget* ParameterPanel::addSlider(", start)
        if start >= 0 and end > start:
            region = text[start:end]
            region_count = region.count(old)
            if region_count == expected:
                region = region.replace(old, new)
                file_path.write_text(text[:start] + region + text[end:])
                return

    raise RuntimeError(
        f"{path}: expected {expected} copies, found {count}: {old[:120]!r}"
    )
'''

count = script.count(old)
if count != 1:
    raise RuntimeError(f"expected one replace_exact helper, found {count}")
script = script.replace(old, new)
exec(compile(script, str(script_path), "exec"))
