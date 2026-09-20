#!/usr/bin/env python3
"""Verify real Geometry panel behavior against an unchanged CI-built core."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import os
import shlex
import subprocess

root = Path.cwd()
work = root / 'build-qt/geometry-section-verification'
work.mkdir(parents=True, exist_ok=True)
gui = root / 'src/qtgui'
core = root / 'runtime/usr/local/lib'
text = (gui / 'main.cpp').read_text()
start = text.index('/** Exercise persisted Geometry folds and incremental diagnostic visibility. */')
end = text.index('/** Exercise beta-critical non-rendering UI/document invariants. */', start)
headers = '''#include "GeometryPanel.h"
#include "../libcolorscreen/include/imagedata.h"
#include <QApplication>
#include <QDebug>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSettings>
#include <QTemporaryDir>
#include <QStringList>
#include <algorithm>
#include <memory>
#include <functional>
#include <vector>
#include <cstdlib>
'''
main = '''
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("GeometrySectionVerification");
  QCoreApplication::setOrganizationDomain("geometry-section-verification.invalid");
  QCoreApplication::setApplicationName("VerifyPanels");
  QSettings().setValue("hostPreference", 73);
  const int count = argc > 1 ? std::atoi(argv[1]) : 1;
  for (int i = 0; i < count; ++i) {
    if (!geometrySectionPreferencesSmoke()) return 1;
    if (QCoreApplication::organizationName() != "GeometrySectionVerification"
        || QCoreApplication::organizationDomain() != "geometry-section-verification.invalid"
        || QCoreApplication::applicationName() != "VerifyPanels"
        || QSettings().value("hostPreference").toInt() != 73) return 2;
  }
  qInfo() << "Real Geometry section smoke passed" << count << "iterations";
  return 0;
}
'''
probe = work / 'probe.cpp'
probe.write_text(headers + text[start:end] + main)
units = ['ParameterPanel', 'ParameterState', 'GeometryPanel',
         'DeformationChartWidget', 'CoordinateTransformer', 'FinetuneImagesPanel',
         'ScalableImageLabel', 'Logging']
sources = [gui / (unit + '.cpp') for unit in units]
for unit in units + ['SmartSpinBox']:
    header = gui / (unit + '.h')
    if header.exists() and 'Q_OBJECT' in header.read_text():
        output = work / ('moc_' + unit + '.cpp')
        subprocess.run(['moc', str(header), '-o', str(output)], check=True)
        sources.append(output)
sources.append(probe)

def pkg(flag, names):
    """Read compiler/linker options without invoking a shell."""
    return shlex.split(subprocess.check_output(['pkg-config', flag, *names], text=True))

qt = ['Qt6Widgets', 'Qt6Concurrent']
flags = ['-std=c++17', '-O0', '-g1', '-fPIC', '-fopenmp', '-Wall',
         '-I' + str(gui), '-I' + str(root / 'build-qt'),
         '-I' + str(root / 'build-qt/src/libcolorscreen/include'),
         '-I' + str(root / 'src/libcolorscreen/include')] + pkg('--cflags', qt)
libs = ['-L' + str(core), '-Wl,-rpath,' + str(core), '-lcolorscreen'] + pkg('--libs', qt + ['gsl', 'fftw3', 'lcms2', 'libtiff-4', 'libturbojpeg', 'libzip', 'libraw', 'exiv2'])
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', OMP_NUM_THREADS='2',
           XDG_CONFIG_HOME=str(work / 'settings'), ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',
           UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')

def build(mode, extra):
    """Compile the real GUI units and run repeated panel lifecycle checks."""
    out = work / mode
    out.mkdir(exist_ok=True)
    def compile_one(source):
        obj = out / (source.stem + '.o')
        subprocess.run(['g++', *flags, *extra, '-c', str(source), '-o', str(obj)], check=True)
        return obj
    with ThreadPoolExecutor(max_workers=2) as pool:
        objects = list(pool.map(compile_one, sources))
    executable = out / 'probe'
    subprocess.run(['g++', '-fopenmp', '-no-pie', *extra, *map(str, objects), *libs, '-o', str(executable)], check=True)
    subprocess.run([str(executable), '20'], env=env, check=True, timeout=120)
    return objects

plain = build('ordinary', [])
build('asan-ubsan', ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'])
original = (gui / 'GeometryPanel.cpp').read_text()
variants = [
    ('missing-key', original.replace('QStringLiteral("geometry.fit")', 'QString()', 1)),
    ('escaped-fit-message', original.replace('    addToPanel(label);', '    m_form->addRow(label);', 1)),
    ('chart-resurrection', original.replace('  updateWidgetStates();\n\n  if (!hasGeometry) {',
        '  updateWidgetStates();\n  m_chartContainer->parentWidget()->setVisible(hasGeometry);\n\n  if (!hasGeometry) {', 1)),
    ('construction-edit', original.replace('    const QSignalBlocker blocker(m_autoOptimizeBox);\n', '', 1)),
]
for name, broken in variants:
    if broken == original:
        raise SystemExit('Negative control did not modify production source: ' + name)
    file = work / (name + '.cpp')
    file.write_text(broken)
    obj = work / (name + '.o')
    executable = work / name
    subprocess.run(['g++', *flags, '-c', str(file), '-o', str(obj)], check=True)
    objects = [obj if item.name == 'GeometryPanel.o' else item for item in plain]
    subprocess.run(['g++', '-fopenmp', '-no-pie', *map(str, objects), *libs, '-o', str(executable)], check=True)
    result = subprocess.run([str(executable), '1'], env=env, text=True, capture_output=True, timeout=30)
    print(result.stdout, result.stderr, flush=True)
    if result.returncode != 1 or 'Geometry section smoke failed:' not in result.stderr:
        raise SystemExit('Negative control was not rejected as expected: ' + name)
    print('Negative control rejected:', name, flush=True)
print('VALIDATION PASS: 20 ordinary + 20 ASan/UBSan iterations; four negative controls rejected.', flush=True)
