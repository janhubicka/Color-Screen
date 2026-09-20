#!/usr/bin/env python3
"""Build the real panel smoke against an unchanged, CI-built core library."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import os
import shlex
import subprocess

root = Path.cwd()
work = root / 'build-qt/color-section-verification'
work.mkdir(parents=True, exist_ok=True)
gui = root / 'src/qtgui'
core = root / 'runtime/usr/local/lib'
text = (gui / 'main.cpp').read_text()
start = text.index('/** Exercise persisted folding in the real Color and Contact Copy panels. */')
end = text.index('/** Exercise beta-critical non-rendering UI/document invariants. */', start)
headers = '''#include "ColorPanel.h"
#include "ContactCopyPanel.h"
#include "ToneCurveWidget.h"
#include <QApplication>
#include <QDebug>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QStringList>
#include <memory>
#include <functional>
#include <vector>
#include <cstdlib>
'''
main = '''
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("ColorSectionVerification");
  QCoreApplication::setOrganizationDomain("color-section-verification.invalid");
  QCoreApplication::setApplicationName("VerifyPanels");
  QSettings().setValue("hostPreference", 73);
  const int count = argc > 1 ? std::atoi(argv[1]) : 1;
  for (int i = 0; i < count; ++i) {
    if (!colorSectionPreferencesSmoke()) return 1;
    if (QCoreApplication::organizationName() != "ColorSectionVerification"
        || QCoreApplication::organizationDomain() != "color-section-verification.invalid"
        || QCoreApplication::applicationName() != "VerifyPanels"
        || QSettings().value("hostPreference").toInt() != 73) return 2;
  }
  qInfo() << "Real Color/Contact Copy smoke passed" << count << "iterations";
  return 0;
}
'''
probe = work / 'probe.cpp'
probe.write_text(headers + text[start:end] + main)
units = ['ParameterPanel', 'ParameterState', 'ColorPanel', 'ContactCopyPanel',
         'TilePreviewPanel', 'TaskQueue', 'WorkerBase', 'HistogramWorker',
         'HDCurveWidget', 'ToneCurveWidget', 'InteractiveChartWidget',
         'CIEChartWidget', 'SpectraChartWidget', 'ScalableImageLabel', 'Logging']
sources = [gui / (unit + '.cpp') for unit in units]
for unit in units + ['SmartSpinBox']:
    header = gui / (unit + '.h')
    if header.exists() and 'Q_OBJECT' in header.read_text():
        output = work / ('moc_' + unit + '.cpp')
        subprocess.run(['moc', str(header), '-o', str(output)], check=True)
        sources.append(output)
sources.append(probe)
def pkg(flag, names):
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
original = (gui / 'ColorPanel.cpp').read_text()
for name, broken in [
    ('missing-key', original.replace('QStringLiteral("color.final")', 'QString()', 1)),
    ('escaped-tone-curve', original.replace('m_currentGroupForm->addRow(toneCurveSection);', 'm_form->addRow(toneCurveSection);', 1)),
]:
    if broken == original:
        raise SystemExit('Negative control did not modify production source')
    file = work / (name + '.cpp')
    file.write_text(broken)
    obj = work / (name + '.o')
    executable = work / name
    subprocess.run(['g++', *flags, '-c', str(file), '-o', str(obj)], check=True)
    objects = [obj if item.name == 'ColorPanel.o' else item for item in plain]
    subprocess.run(['g++', '-fopenmp', '-no-pie', *map(str, objects), *libs, '-o', str(executable)], check=True)
    result = subprocess.run([str(executable), '1'], env=env, text=True, capture_output=True, timeout=30)
    print(result.stdout, result.stderr, flush=True)
    if result.returncode != 1 or 'Color/Contact Copy section smoke failed:' not in result.stderr:
        raise SystemExit('Negative control was not rejected as expected: ' + name)
    print('Negative control rejected:', name, flush=True)
print('VALIDATION PASS: 20 ordinary + 20 ASan/UBSan iterations; both negative controls rejected.', flush=True)
