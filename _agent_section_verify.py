#!/usr/bin/env python3
"""Exercise the exact section helper and smoke with a minimal state fixture."""
from pathlib import Path
import os
import shlex
import subprocess

panel = Path('src/qtgui/ParameterPanel.cpp').read_text()
main = Path('src/qtgui/main.cpp').read_text()


def between(text: str, start: str, end: str) -> str:
    """Extract an exact production region bounded by unique source anchors."""
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


preamble = r'''
#include <QtWidgets>
#include <functional>
#include <memory>
#include <utility>
#include <vector>
namespace colorscreen { class image_data {}; }
struct ParameterState {
  struct { bool scan_mirror = false; } rparams;
};
constexpr auto parameterApplicableProperty = "parameterApplicable";
constexpr auto parameterSectionExpandedProperty = "parameterSectionExpanded";
constexpr auto parameterKeyProperty = "parameterKey";
class ParameterPanel : public QWidget {
public:
  using StateGetter = std::function<ParameterState()>;
  using StateSetter = std::function<void(const ParameterState &, const QString &,
                                        const QString &)>;
  using ImageGetter = std::function<std::shared_ptr<colorscreen::image_data>()>;
  ParameterPanel(StateGetter, StateSetter, ImageGetter, QWidget * = nullptr,
                 bool = true);
  ~ParameterPanel() override;
  void updateUI();
  void applyChange(std::function<void(ParameterState &)>, const QString &,
                   const QString &);
  QCheckBox *addCheckboxParameter(const QString &,
      std::function<bool(const ParameterState &)>,
      std::function<void(ParameterState &, bool)>,
      std::function<bool(const ParameterState &)> = nullptr,
      const QString & = QString(), const QString & = QString());
  QToolButton *addSeparator(const QString &, const QString & = QString());
  void setParameterApplicability(QWidget *,
      std::function<bool(const ParameterState &)>);
  virtual void onParametersRefreshed(const ParameterState &) {}
  StateGetter m_stateGetter;
  StateSetter m_stateSetter;
  ImageGetter m_imageGetter;
  QFormLayout *m_currentGroupForm = nullptr;
  QVBoxLayout *m_layout = nullptr;
  QFormLayout *m_form = nullptr;
  std::vector<QFormLayout *> m_groupForms;
  std::vector<std::function<void(const ParameterState &)>> m_paramUpdaters;
  std::vector<std::function<void()>> m_widgetStateUpdaters;
};
class CheckboxSemanticsProbe final : public ParameterPanel {
public:
  using ParameterPanel::ParameterPanel;
};
'''
code = preamble
code += between(panel, 'void setParameterKey(', '/** Numeric editor')
code += between(panel, 'ParameterPanel::ParameterPanel(', '/** Add quiet modified/default/reset UI')
code += between(panel, 'QCheckBox *ParameterPanel::addCheckboxParameter(',
                'QCheckBox *ParameterPanel::addCheckboxWithReset(')
code += between(panel, 'QToolButton *ParameterPanel::addSeparator(',
                'QWidget *\nParameterPanel::createDetachableSection(')
code += between(main, 'bool parameterSectionPreferencesSmoke()',
                '/** Exercise beta-critical non-rendering UI/document invariants. */')
code += r'''
int main(int argc, char **argv) {
  QApplication application(argc, argv);
  QTemporaryDir settingsDirectory;
  if (!settingsDirectory.isValid()) return 2;
  QCoreApplication::setOrganizationName("Color-Screen-agent-verification");
  QCoreApplication::setApplicationName("section-preferences");
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                     settingsDirectory.path());
  for (int i = 0; i < 100; ++i) {
    if (!parameterSectionPreferencesSmoke()) return 1;
    if (!QSettings().allKeys().empty()) {
      qCritical() << "Smoke left settings behind";
      return 3;
    }
  }
  qInfo() << "PASS: 100 section-preference smoke iterations; settings cleaned";
  return 0;
}
'''
build = Path('build-qt/section-verification')
build.mkdir(parents=True, exist_ok=True)
source = build / 'section-smoke.cpp'
source.write_text(code)
flags = shlex.split(subprocess.check_output(
    ['pkg-config', '--cflags', '--libs', 'Qt6Widgets'], text=True))
compiler = shlex.split(os.environ.get('CXX', 'g++'))
env = dict(os.environ, QT_QPA_PLATFORM='offscreen')
# Leak checking is left to the complete application CI; this Qt offscreen
# harness checks address/undefined behavior in the section interactions.
env['ASAN_OPTIONS'] = 'detect_leaks=0:halt_on_error=1'
env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
for name, extra in [('ordinary', []),
                    ('asan-ubsan', ['-fsanitize=address,undefined',
                                    '-fno-omit-frame-pointer'])]:
    binary = build / name
    subprocess.run([*compiler, '-std=c++17', '-O1', '-g', '-Wall', '-Wextra',
                    *extra, str(source), '-o', str(binary), *flags], check=True)
    subprocess.run([str(binary)], env=env, check=True)

for name, removed in [('without-persistence',
                       'QSettings().setValue(settingsKey, checked);'),
                      ('without-restoration-refresh',
                       'm_widgetStateUpdaters.push_back(updateVisibility);')]:
    if code.count(removed) != 1:
        raise RuntimeError(f'negative control anchor mismatch: {name}')
    negative = build / (name + '.cpp')
    negative.write_text(code.replace(removed, '/* negative control */'))
    binary = build / name
    subprocess.run([*compiler, '-std=c++17', '-O1', str(negative),
                    '-o', str(binary), *flags], check=True)
    result = subprocess.run([str(binary)], env=env)
    if result.returncode != 1:
        raise RuntimeError(f'{name}: expected smoke rejection (1), got {result.returncode}')
    print(f'PASS: negative control {name} rejected', flush=True)
