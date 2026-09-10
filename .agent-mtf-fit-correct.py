from pathlib import Path
import subprocess


def replace(path, old, new):
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one match, found {count}")
    p.write_text(text.replace(old, new, 1))


replace('src/qtgui/MainWindow.h', '''  bool requestMtfModelFit(
      const ParameterState &baseline,
      const colorscreen::mtf_parameters &input,
      const colorscreen::mtf_estimation_options &options, int flags);
''', '''  bool requestMtfModelFit(
      const ParameterState &baseline,
      const colorscreen::mtf_parameters &input,
      const colorscreen::mtf_estimation_options &options, int flags,
      QWidget *resultParent = nullptr);
''')

replace('src/qtgui/MainWindow.cpp', '''  mtfCalibration.fitRequested =
      [this](const ParameterState &baseline,
             const colorscreen::mtf_parameters &input,
             const colorscreen::mtf_estimation_options &options, int flags) {
        return requestMtfModelFit(baseline, input, options, flags);
      };
''', '''  mtfCalibration.fitRequested =
      [this](const ParameterState &baseline,
             const colorscreen::mtf_parameters &input,
             const colorscreen::mtf_estimation_options &options, int flags,
             QWidget *resultParent) {
        return requestMtfModelFit(baseline, input, options, flags, resultParent);
      };
''')

replace('src/qtgui/MainWindow.cpp', '''bool MainWindow::requestMtfModelFit(
    const ParameterState &baseline, const colorscreen::mtf_parameters &input,
    const colorscreen::mtf_estimation_options &options, int flags) {
''', '''bool MainWindow::requestMtfModelFit(
    const ParameterState &baseline, const colorscreen::mtf_parameters &input,
    const colorscreen::mtf_estimation_options &options, int flags,
    QWidget *resultParent) {
''')

replace('src/qtgui/MainWindow.cpp', '''  const colorscreen::mtf_parameters baselineMtf =
      baseline.rparams.sharpen.scanner_mtf;
  auto result = std::make_shared<MtfModelFitResult>();
''', '''  const colorscreen::mtf_parameters baselineMtf =
      baseline.rparams.sharpen.scanner_mtf;
  const QPointer<QWidget> guardedResultParent(resultParent);
  auto result = std::make_shared<MtfModelFitResult>();
''')

replace('src/qtgui/MainWindow.cpp', '''  operation.applyResult = [this, baselineMtf, result]() {
''', '''  operation.applyResult = [this, baselineMtf, result,
                           guardedResultParent]() {
''')

replace('src/qtgui/MainWindow.cpp', '''          QMessageBox::Ok, this);
      box->setObjectName(QStringLiteral("MtfFitErrorDialog"));
''', '''          QMessageBox::Ok,
          guardedResultParent ? guardedResultParent.data() : this);
      box->setObjectName(QStringLiteral("MtfFitErrorDialog"));
''')

replace('src/qtgui/MainWindow.cpp', '''    auto *box = new QMessageBox(QMessageBox::Information, tr("MTF model fit"),
                                details, QMessageBox::Ok, this);
''', '''    auto *box = new QMessageBox(QMessageBox::Information, tr("MTF model fit"),
                                details, QMessageBox::Ok,
                                guardedResultParent ? guardedResultParent.data()
                                                    : this);
''')

replace('src/qtgui/ImageViewWindow.cpp', '''  mtfCalibration.fitRequested =
      [this](const ParameterState &baseline,
             const colorscreen::mtf_parameters &input,
             const colorscreen::mtf_estimation_options &options, int flags) {
        return m_document &&
               m_document->requestMtfModelFit(baseline, input, options, flags);
      };
''', '''  mtfCalibration.fitRequested =
      [this](const ParameterState &baseline,
             const colorscreen::mtf_parameters &input,
             const colorscreen::mtf_estimation_options &options, int flags,
             QWidget *resultParent) {
        return m_document && m_document->requestMtfModelFit(
                                 baseline, input, options, flags, resultParent);
      };
''')

replace('src/qtgui/SharpnessPanel.h', '''  std::function<bool(const ParameterState &,
                     const colorscreen::mtf_parameters &,
                     const colorscreen::mtf_estimation_options &, int)>
      fitRequested;
''', '''  std::function<bool(const ParameterState &,
                     const colorscreen::mtf_parameters &,
                     const colorscreen::mtf_estimation_options &, int,
                     QWidget *)>
      fitRequested;
''')

replace('src/qtgui/SharpnessPanel.cpp', '''        m_mtfCalibration.fitRequested(baseline, input, options, flags);
''', '''        m_mtfCalibration.fitRequested(baseline, input, options, flags, this);
''')

replace('.agents/qtgui.md', '''request, dedicated Cancel row, fitting provenance, success/failure dialogs, and
accepted Undo edit. Do not add a fit queue back to individual Sharpness panels:
''', '''request, dedicated Cancel row, fitting provenance, success/failure dialogs, and
accepted Undo edit. Result dialogs may retain a guarded initiating panel solely
as presentation ownership; that widget must never gate result publication or
worker lifetime. Do not add a fit queue back to individual Sharpness panels:
''')

replace('doc/mtf-fitting-workflow.md', '''late result from publishing. A valid result is committed as one undoable
parameter change. Failure or cancellation leaves the parameter state untouched.

The document also owns the accepted analytical-model fit as session-local
''', '''late result from publishing. A valid result is committed as one undoable
parameter change. Failure or cancellation leaves the parameter state untouched.
Completion messages stay parented to the Sharpness panel that started the fit
while that presentation exists; closing that panel does not own or cancel the
underlying document operation.

The document also owns the accepted analytical-model fit as session-local
''')

expected = {
    '.agents/qtgui.md': '08f78e99b2914ffe8c17b39ff539d4eae1b69646',
    'doc/mtf-fitting-workflow.md': '7164f7dd05ae195854099e760d437a5ed21f0806',
    'doc/qtgui-internal-cleanup.md': '7738a3631a963c563ababde26afed5da07a0daa2',
    'src/qtgui/ImageViewWindow.cpp': '5caa5a696278a5ef7716ef080490e9e89820a636',
    'src/qtgui/MainWindow.cpp': 'b9e8229a80a3711f57f31715a0b21d4e84370266',
    'src/qtgui/MainWindow.h': '3d48a8bb5311af899a7d5303b8345fef55546726',
    'src/qtgui/SharpnessPanel.cpp': '84ff01d275131192879bd392c75d8e1f4eaa7887',
    'src/qtgui/SharpnessPanel.h': '39afca6861ad01bed79b6371abe0b59f6066a844',
    'src/qtgui/WorkspaceChurnSmoke.cpp': 'eb879ff47bca995a783fa23ce4a26b327cb0331b',
}
changed = set(subprocess.check_output(
    ['git', 'diff', 'HEAD', '--name-only'], text=True).splitlines())
if changed != set(expected):
    raise RuntimeError(f"unexpected changed files: {sorted(changed)}")
for path, digest in expected.items():
    actual = subprocess.check_output(['git', 'hash-object', path], text=True).strip()
    if actual != digest:
        raise RuntimeError(f"{path}: {actual} != {digest}")
print('Corrected tree matches all nine reviewed file hashes.')
