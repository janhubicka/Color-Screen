from pathlib import Path
import re
import subprocess
root = Path.cwd()
def replace(path, old, new):
    p = root/path
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{path}: expected one match, found {count}: {old[:100]!r}')
    p.write_text(text.replace(old, new, 1))

(root/'src/qtgui/CoordinateOptimizationWorker.h').write_text('''#ifndef COORDINATE_OPTIMIZATION_WORKER_H
#define COORDINATE_OPTIMIZATION_WORKER_H

#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/finetune.h"
#include "../libcolorscreen/include/progress-info.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include <memory>

namespace colorscreen {
class image_data;
}

/** Final result of detecting a screen's initial coordinate system. */
struct CoordinateAutodetectionResult {
  bool success = false;
  bool cancelled = false;
  colorscreen::scr_to_img_parameters coordinates;
};

/** Final result of refining an existing screen coordinate system. */
struct CoordinateOptimizationResult {
  bool success = false;
  bool cancelled = false;
  colorscreen::finetune_result finetune;
};

/** Synchronous coordinate helpers, run with immutable background inputs. */
class CoordinateOptimizationWorker final {
public:
  static CoordinateAutodetectionResult autodetect(
      colorscreen::scr_to_img_parameters params,
      colorscreen::render_parameters rparams,
      std::shared_ptr<colorscreen::image_data> scan,
      colorscreen::progress_info *progress);

  static CoordinateOptimizationResult optimize(
      colorscreen::scr_to_img_parameters params,
      colorscreen::render_parameters rparams,
      std::shared_ptr<colorscreen::image_data> scan,
      colorscreen::progress_info *progress);
};

#endif // COORDINATE_OPTIMIZATION_WORKER_H
''')
(root/'src/qtgui/CoordinateOptimizationWorker.cpp').write_text('''#include "CoordinateOptimizationWorker.h"
#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/imagedata.h"
#include <QDebug>
#include <exception>

CoordinateAutodetectionResult CoordinateOptimizationWorker::autodetect(
    colorscreen::scr_to_img_parameters params,
    colorscreen::render_parameters rparams,
    std::shared_ptr<colorscreen::image_data> scan,
    colorscreen::progress_info *progress) {
  CoordinateAutodetectionResult result;
  result.coordinates = params;
  if (progress && progress->pool_cancel()) {
    result.cancelled = true;
    return result;
  }
  if (!scan)
    return result;

  try {
    colorscreen::sub_task task(progress);
    result.success = colorscreen::autodetect_coordinates(
        *scan, result.coordinates, rparams, progress);
  } catch (const std::exception &e) {
    qWarning() << "Autodetect coordinates failed with exception:" << e.what();
  } catch (...) {
    qWarning() << "Autodetect coordinates failed with unknown exception";
  }

  result.cancelled = progress &&
      (progress->pool_cancel() || progress->cancelled());
  result.success = result.success && !result.cancelled;
  return result;
}

CoordinateOptimizationResult CoordinateOptimizationWorker::optimize(
    colorscreen::scr_to_img_parameters params,
    colorscreen::render_parameters rparams,
    std::shared_ptr<colorscreen::image_data> scan,
    colorscreen::progress_info *progress) {
  CoordinateOptimizationResult result;
  if (progress && progress->pool_cancel()) {
    result.cancelled = true;
    return result;
  }
  if (!scan) {
    result.finetune.err = "No scan available.";
    return result;
  }

  try {
    colorscreen::sub_task task(progress);
    colorscreen::finetune_parameters fparams;
    fparams.flags = colorscreen::finetune_position | colorscreen::finetune_verbose |
                    colorscreen::finetune_coordinates | colorscreen::finetune_bw |
                    colorscreen::finetune_use_strip_widths |
                    colorscreen::finetune_produce_images;

    // RANGE is the half-extent in periodic screen coordinates: keep the
    // existing local 2x2-screen-period sample rather than the larger default.
    fparams.range = 1;

    result.finetune = colorscreen::finetune(
        rparams, params, *scan, {}, nullptr, fparams, progress);
    result.success = result.finetune.success;
  } catch (const std::exception &e) {
    result.finetune.err = e.what();
    qWarning() << "Optimize coordinates failed with exception:" << e.what();
  } catch (...) {
    result.finetune.err = "Unknown coordinate optimization exception.";
    qWarning() << "Optimize coordinates failed with unknown exception";
  }

  result.cancelled = progress &&
      (progress->pool_cancel() || progress->cancelled());
  result.success = result.success && !result.cancelled;
  return result;
}
''')

replace('src/qtgui/MainWindow.h', 'class CoordinateOptimizationWorker;\n', '')
replace('src/qtgui/MainWindow.h', '''  void onAutodetectCoordinatesFinished(int reqId, colorscreen::scr_to_img_parameters result, std::shared_ptr<colorscreen::progress_info> progress, bool success, bool cancelled);
  void onOptimizeCoordinatesFinished(int reqId, colorscreen::finetune_result result, std::shared_ptr<colorscreen::progress_info> progress, bool success, bool cancelled);
''', '')
replace('src/qtgui/MainWindow.h', '''    QString description;
    std::function<bool()> prerequisites;''', '''    QString description;
    // Non-empty for a dedicated task row; otherwise use transient progress.
    QString progressTitle;
    std::function<bool()> prerequisites;''')
replace('src/qtgui/MainWindow.h', '''  /** Present RESULT and defer accepted publication through a window-modal prompt. */''', '''  /** Detect an initial basis; optionally continue with automatic point finding.
      The continuation belongs to this request, never to mutable window state. */
  void startCoordinateAutodetection(bool addPointsAfterDetection);

  /** Apply a successful coordinate refinement as one undoable document edit. */
  void applyOptimizedCoordinates(const colorscreen::finetune_result &result);

  /** Present RESULT and defer accepted publication through a window-modal prompt. */''')
replace('src/qtgui/MainWindow.h', '  bool m_autoAddPointsAfterCoordinates = false;\n', '')
replace('src/qtgui/MainWindow.h', '''  int m_coordinateAutodetectRequest = 0;
  std::shared_ptr<colorscreen::progress_info> m_coordinateAutodetectProgress;
  int m_coordinateOptimizeRequest = 0;
''', '')
replace('src/qtgui/MainWindow.h', '''  // Coordinate Optimization Worker
  CoordinateOptimizationWorker *m_coordOptimizationWorker = nullptr;
  QThread *m_coordOptimizationThread = nullptr;

''', '')
replace('src/qtgui/MainWindow.cpp', '''  // Initialize Coordinate Optimization Worker
  m_coordOptimizationThread = new QThread(this);
  m_coordOptimizationWorker = new CoordinateOptimizationWorker(m_scan);
  m_coordOptimizationWorker->moveToThread(m_coordOptimizationThread);
  m_coordOptimizationThread->start();

  connect(m_coordOptimizationWorker, &CoordinateOptimizationWorker::autodetectFinished, this,
          &MainWindow::onAutodetectCoordinatesFinished);
  connect(m_coordOptimizationWorker, &CoordinateOptimizationWorker::optimizeFinished, this,
          &MainWindow::onOptimizeCoordinatesFinished);

''', '')
replace('src/qtgui/MainWindow.cpp', '''   optimizer, coordinate optimizer) and waits for them to finish.  Explicitly''', '''   optimizer) and waits for them to finish. Explicitly''')
replace('src/qtgui/MainWindow.cpp', '''  if (m_coordOptimizationWorker)
    disconnect(m_coordOptimizationWorker, nullptr, this, nullptr);
''', '')
replace('src/qtgui/MainWindow.cpp', '''  if (m_coordOptimizationThread) {
    m_coordOptimizationThread->quit();
    m_coordOptimizationThread->wait();
    delete m_coordOptimizationWorker;
    m_coordOptimizationWorker = nullptr;
  }

''', '')
replace('src/qtgui/MainWindow.cpp', '''    if (m_coordOptimizationWorker)
      m_coordOptimizationWorker->setScan(m_scan);
''', '')
replace('src/qtgui/MainWindow.cpp', '''  // A control point appearing while coordinate autodetection runs makes its
  // result unsafe to publish: the point is expressed in the old basis.
  if (documentPointCount > 0 && m_coordinateAutodetectProgress &&
      !m_coordinateAutodetectProgress->pool_cancel()) {
    m_coordinateAutodetectProgress->cancel();
    ++m_coordinateAutodetectRequest;
    m_autoAddPointsAfterCoordinates = false;
    statusBar()->showMessage(
        tr("Coordinate autodetection cancelled because control points now exist."),
        4000);
  }

''', '''  // Coordinate autodetection now uses the shared one-shot cancellation and
  // exact snapshot gate, which also rejects any newly added control point.

''')
replace('src/qtgui/MainWindow.cpp', '''      [lifecycle]() {
        if (lifecycle->onStart)
          lifecycle->onStart();
      });''', '''      [this, lifecycle](std::shared_ptr<colorscreen::progress_info> progress) {
        if (!lifecycle->progressTitle.isEmpty()) {
          // TaskQueue already registered ordinary progress. Replace that entry
          // rather than tracking the same request twice in the workspace.
          removeProgress(progress);
          addUserVisibleProgress(progress, lifecycle->progressTitle);
        }
        if (lifecycle->onStart)
          lifecycle->onStart();
      });''')
replace('src/qtgui/MainWindow.cpp', '''      // refine/add points, but must never replace that basis underneath them.
      m_autoAddPointsAfterCoordinates = false;
''', '''      // refine/add points, but must never replace that basis underneath them.
''')
replace('src/qtgui/MainWindow.cpp', '''    m_autoAddPointsAfterCoordinates = true;
    onAutodetectCoordinatesRequested();''', '''    startCoordinateAutodetection(true);''')

p = root/'src/qtgui/MainWindow.cpp'
s = p.read_text()
a = s.index('/** Launch autodetection of screen coordinates (center, coordinate1,')
b = s.index('/** Propagate coordinate system parameter changes to the renderer.', a)
s = s[:a] + '''/** Detect coordinates without implicitly continuing another request's workflow. */
void MainWindow::onAutodetectCoordinatesRequested() {
  startCoordinateAutodetection(false);
}

/** Detect an initial coordinate system from immutable scan/parameter snapshots.
    Existing control points prohibit replacement of their coordinate frame.
    ADDPOINTSAFTERDETECTION is captured by this request, so a superseded request
    cannot start registration work or change a newer request's continuation. */
void MainWindow::startCoordinateAutodetection(bool addPointsAfterDetection) {
  if (!m_scan)
    return;
  if (m_solverParams.n_points() > 0) {
    statusBar()->showMessage(
        tr("Delete existing control points before detecting a new screen "
           "coordinate system."),
        5000);
    return;
  }

  const auto scan = m_scan;
  const ParameterState baseline = getCurrentState();
  auto result = std::make_shared<CoordinateAutodetectionResult>();

  OneShotOperation operation;
  operation.description = tr("Autodetecting coordinates");
  operation.progressTitle = tr("Coordinate autodetection");
  operation.prerequisites = [this, scan]() { return m_scan == scan; };
  operation.resultValid = [this, scan, baseline, result]() {
    return m_scan == scan && getCurrentState() == baseline &&
           m_solverParams.n_points() == 0 && !result->cancelled;
  };
  operation.applyResult = [this, result, addPointsAfterDetection]() {
    if (!result->success) {
      QMessageBox::warning(this, tr("Autodetect Coordinates"),
                           tr("Autodetect coordinates failed."));
      return;
    }

    ParameterState newState = getCurrentState();
    newState.scrToImg = result->coordinates;
    // Switch before applyState refreshes the canvas with accepted geometry.
    m_renderTypeParams.type = colorscreen::render_type_interpolated;
    changeParameters(newState, "Autodetect Coordinates");
    if (m_addPointAction)
      m_addPointAction->setChecked(true);
    m_imageWidget->update();
    statusBar()->showMessage(tr("Autodetect coordinates finished"), 3000);

    if (addPointsAfterDetection && m_geometryPanel)
      onAutomaticallyAddPointsRequested(m_geometryPanel->finetuneAreaParams());
  };

  runOneShotOperation(
      std::move(operation),
      [scan, baseline, result](colorscreen::progress_info *progress) mutable {
        *result = CoordinateOptimizationWorker::autodetect(
            baseline.scrToImg, baseline.rparams, scan, progress);
      });
}

/** Forward coordinate optimisation request to onOptimizeCoordinates. */
void MainWindow::onOptimizeCoordinatesRequested() {
  onOptimizeCoordinates();
}

/** Refine an existing coordinate system under the shared one-shot lifecycle. */
void MainWindow::onOptimizeCoordinates() {
  if (!m_scan)
    return;
  if (!colorscreen::screen_geometry_configured_p(m_scrToImgParams)) {
    statusBar()->showMessage(tr("Detect screen coordinates before refining them."), 3000);
    return;
  }

  const auto scan = m_scan;
  const ParameterState baseline = getCurrentState();
  auto result = std::make_shared<CoordinateOptimizationResult>();

  OneShotOperation operation;
  operation.description = tr("Optimizing coordinates");
  operation.prerequisites = [this, scan]() { return m_scan == scan; };
  operation.resultValid = [this, scan, baseline, result]() {
    return m_scan == scan && getCurrentState() == baseline && !result->cancelled;
  };
  operation.applyResult = [this, result]() {
    if (result->success) {
      applyOptimizedCoordinates(result->finetune);
    } else {
      QMessageBox::warning(this, tr("Optimization"),
                           tr("Optimization failed: ") +
                               QString::fromStdString(result->finetune.err));
    }
  };

  runOneShotOperation(
      std::move(operation),
      [scan, baseline, result](colorscreen::progress_info *progress) mutable {
        *result = CoordinateOptimizationWorker::optimize(
            baseline.scrToImg, baseline.rparams, scan, progress);
      });
}

/** Apply accepted coordinates without mutating the live state before Undo sees it.
    A new linear basis invalidates the old nonlinear mesh. Diagnostics remain
    document-local derived presentation, outside the saved ParameterState. */
void MainWindow::applyOptimizedCoordinates(
    const colorscreen::finetune_result &result) {
  ParameterState newState = getCurrentState();
  newState.scrToImg.center = result.center;
  newState.scrToImg.coordinate1 = result.coordinate1;
  newState.scrToImg.coordinate2 = result.coordinate2;
  newState.scrToImg.mesh_trans = nullptr;
  changeParameters(newState, "Optimize Coordinates");
  m_imageWidget->update();
  if (m_geometryPanel)
    m_geometryPanel->updateFinetuneImages(result);
  updateWorkflowSummary();
  statusBar()->showMessage(tr("Optimize coordinates finished"), 3000);
}

''' + s[b:]
p.write_text(s)

replace('src/qtgui/TaskQueue.h', '                 std::function<void ()> started = nullptr);', '''                 std::function<void (
                     std::shared_ptr<colorscreen::progress_info>)> started = nullptr);''')
replace('src/qtgui/TaskQueue.cpp', '                          std::function<void ()> started)', '''                          std::function<void (
                              std::shared_ptr<colorscreen::progress_info>)> started)''')
replace('src/qtgui/TaskQueue.cpp', '''    if (started)
      started();''', '''    if (started)
      started(progress);''')
replace('src/qtgui/TaskQueue.h', '''   * is fully published to QThreadPool before a worker can execute it.
''', '''   * is fully published to QThreadPool before a worker can execute it.
   * STARTED receives the request's progress on the GUI thread, before worker
   * dispatch, so callers can select the appropriate progress presentation.
''')

replace('src/qtgui/main.cpp', '#include "CoordinateTransformer.h"\n', '#include "CoordinateTransformer.h"\n#include "CoordinateOptimizationWorker.h"\n')
replace('src/qtgui/main.cpp', '''  colorscreen::finetune_area_parameters finetuneAreaParams;
''', '''  // Coordinate helpers must fail without a scan and short-circuit an already
  // cancelled request before touching an uninitialized image's pixel data.
  const colorscreen::scr_to_img_parameters coordinateInputs;
  const auto missingCoordinates = CoordinateOptimizationWorker::autodetect(
      coordinateInputs, colorscreen::render_parameters(), {}, nullptr);
  const auto missingRefinement = CoordinateOptimizationWorker::optimize(
      coordinateInputs, colorscreen::render_parameters(), {}, nullptr);
  if (missingCoordinates.success || missingCoordinates.cancelled ||
      missingCoordinates.coordinates != coordinateInputs ||
      missingRefinement.success || missingRefinement.cancelled ||
      missingRefinement.finetune.err.empty())
    return fail("coordinate helper accepted a missing scan");

  auto emptyScan = std::make_shared<colorscreen::image_data>();
  colorscreen::progress_info cancelledCoordinates;
  cancelledCoordinates.cancel();
  const auto cancelledDetection = CoordinateOptimizationWorker::autodetect(
      coordinateInputs, colorscreen::render_parameters(), emptyScan,
      &cancelledCoordinates);
  const auto cancelledRefinement = CoordinateOptimizationWorker::optimize(
      coordinateInputs, colorscreen::render_parameters(), emptyScan,
      &cancelledCoordinates);
  if (cancelledDetection.success || !cancelledDetection.cancelled ||
      cancelledDetection.coordinates != coordinateInputs ||
      cancelledRefinement.success || !cancelledRefinement.cancelled)
    return fail("coordinate helper ignored pre-dispatch cancellation");

  colorscreen::finetune_area_parameters finetuneAreaParams;
''')
replace('src/qtgui/WorkspaceChurnSmoke.cpp', '#include <QToolButton>\n', '#include <QToolButton>\n#include <QUndoStack>\n')
replace('src/qtgui/WorkspaceChurnSmoke.cpp', '''        const int coordinateRequestBefore =
            first->m_coordinateAutodetectRequest;
        first->onAutodetectCoordinatesRequested();
        if (first->m_coordinateAutodetectRequest != coordinateRequestBefore) {''', '''        first->onAutodetectCoordinatesRequested();
        if (first->m_oneShotOperationQueue.hasActiveTasks()) {''')
replace('src/qtgui/WorkspaceChurnSmoke.cpp', '''        // A state-mutating one-shot must be cancelled as soon as the document
''', '''        // Exercise the actual coordinate-result publisher. Previously it
        // mutated live parameters before changeParameters(), making the edit
        // look like a no-op and silently losing the undo/dirty transition.
        const ParameterState beforeCoordinates = first->getCurrentState();
        QUndoStack *coordinateUndo = first->findChild<QUndoStack *>();
        if (!coordinateUndo) {
          fail(QStringLiteral("Coordinate refinement smoke lost the undo stack"));
          return;
        }
        const int coordinateUndoIndex = coordinateUndo->index();
        colorscreen::finetune_result refinedCoordinates;
        refinedCoordinates.success = true;
        refinedCoordinates.center = beforeCoordinates.scrToImg.center;
        refinedCoordinates.center.x += 1;
        refinedCoordinates.coordinate1 = beforeCoordinates.scrToImg.coordinate1;
        refinedCoordinates.coordinate2 = beforeCoordinates.scrToImg.coordinate2;
        ParameterState expectedCoordinates = beforeCoordinates;
        expectedCoordinates.scrToImg.center = refinedCoordinates.center;
        expectedCoordinates.scrToImg.mesh_trans = nullptr;
        first->applyOptimizedCoordinates(refinedCoordinates);
        if (first->getCurrentState() != expectedCoordinates ||
            coordinateUndo->index() != coordinateUndoIndex + 1 ||
            !first->isDocumentModified()) {
          fail(QStringLiteral("Coordinate refinement did not create one undoable dirty edit"));
          return;
        }
        coordinateUndo->undo();
        if (first->getCurrentState() != beforeCoordinates ||
            coordinateUndo->index() != coordinateUndoIndex) {
          fail(QStringLiteral("Undo coordinate refinement did not restore its exact baseline"));
          return;
        }
        coordinateUndo->redo();
        if (first->getCurrentState() != expectedCoordinates) {
          fail(QStringLiteral("Redo coordinate refinement did not restore its result"));
          return;
        }
        coordinateUndo->undo();

        // A state-mutating one-shot must be cancelled as soon as the document
''')
replace('src/qtgui/WorkspaceChurnSmoke.cpp', '''        oneShotSmoke.description = QStringLiteral("One-shot cancellation smoke");
''', '''        oneShotSmoke.description = QStringLiteral("One-shot cancellation smoke");
        oneShotSmoke.progressTitle = QStringLiteral("One-shot progress smoke");
''')
replace('src/qtgui/WorkspaceChurnSmoke.cpp', '''        // Give the source a distinctive processing-state sentinel without
''', '''        int oneShotProgressEntries = 0;
        for (const ProgressEntry &entry : first->m_activeProgresses) {
          if (entry.userVisible &&
              entry.title == QStringLiteral("One-shot progress smoke")) {
            ++oneShotProgressEntries;
            int matchingEntries = 0;
            for (const ProgressEntry &other : first->m_activeProgresses)
              if (other.info == entry.info)
                ++matchingEntries;
            if (!entry.row || !entry.rowActionButton || matchingEntries != 1) {
              fail(QStringLiteral("One-shot progress row duplicated its request or lost Cancel"));
              return;
            }
          }
        }
        if (oneShotProgressEntries != 1) {
          fail(QStringLiteral("One-shot did not register its dedicated progress row"));
          return;
        }

        // Give the source a distinctive processing-state sentinel without
''')
replace('src/qtgui/WorkspaceChurnSmoke.cpp', '''              "Workspace churn allowed a cancelled one-shot result to publish"));
          return;
        }

''', '''              "Workspace churn allowed a cancelled one-shot result to publish"));
          return;
        }

        for (const ProgressEntry &entry : first->m_activeProgresses) {
          if (entry.title == QStringLiteral("One-shot progress smoke")) {
            fail(QStringLiteral("Cancelled one-shot retained its progress row"));
            return;
          }
        }

''')

replace('src/qtgui/Makefile.am', '    moc_CoordinateOptimizationWorker.cpp \\\n', '')
p = root/'src/qtgui/Makefile.in'
s = p.read_text()
s, count = re.subn(r'^colorscreen_qt-moc_CoordinateOptimizationWorker\.(?:o|obj):[^\n]*\n.*?\n\n', '', s, flags=re.M|re.S)
if count != 2:
    raise RuntimeError(f'Expected two obsolete moc compilation rules, found {count}')
s = ''.join(line for line in s.splitlines(keepends=True) if 'moc_CoordinateOptimizationWorker' not in line)
p.write_text(s)

replace('doc/qtgui-internal-cleanup.md', '''  member are therefore gone.
''', '''  member are therefore gone.
  Coordinate autodetection and local coordinate refinement now also use these
  snapshot/publication rules. Their shared persistent QObject/QThread and both
  manual request counters are removed. The known-screen Detect Screen path
  captures its optional automatic-point-finding continuation in the detection
  request rather than a mutable window flag, so cancellation or a newer direct
  coordinate request cannot inherit another request's continuation. Detection
  retains its dedicated Cancel row through an optional one-shot progress title.
  Refinement applies a copied state through `changeParameters()` before updating
  diagnostics; the old live-state mutation caused `changeParameters()` to see
  a no-op and omit the undo/dirty transition. Workspace-churn smoke checks the
  exact coordinate-edit Undo/Redo state and dedicated progress-row cleanup.
''')
replace('.agents/qtgui.md', '''Screen detection additionally keeps its asynchronous confirmation prompt''', '''Coordinate autodetection and refinement use the same lifecycle too; the optional post-detection point-finding continuation is request-local. A non-empty `OneShotOperation::progressTitle` retains a dedicated Cancel row, replacing rather than duplicating TaskQueue's ordinary progress entry before dispatch. Coordinate refinement must construct the new state before calling `changeParameters()`; mutating the live coordinates first makes that setter see a no-op and loses Undo. Screen detection additionally keeps its asynchronous confirmation prompt''')
expected = {
  '.agents/qtgui.md': '0e4b7f9e6b71744155bcf9cfa264cb3e256a8f65',
  'doc/qtgui-internal-cleanup.md': '6fdaf6038c001cd797841e71766ad2770ab544f1',
  'src/qtgui/CoordinateOptimizationWorker.cpp': 'eb7849b5c7a777629525fef0a7f39ad0c33831fe',
  'src/qtgui/CoordinateOptimizationWorker.h': '5d992d42db7f7e8966cd18a41732382c6cd538dd',
  'src/qtgui/MainWindow.cpp': 'eb07b7531e58b89961525d7cf5a7430eb2951c11',
  'src/qtgui/MainWindow.h': '34fb1ad7d7ed5df7c5f36febbd08157bf2e7a409',
  'src/qtgui/Makefile.am': '023bb5f11d36f394a6cc9f63be61376acce49999',
  'src/qtgui/Makefile.in': 'b8b180a50a19325015872e0422ce6e7c3912ecd7',
  'src/qtgui/TaskQueue.cpp': 'a61c34e66f4efcf7330f75b71a2b1c5739d3c06c',
  'src/qtgui/TaskQueue.h': 'fd553b1ea64459c01a8162ccd0cb534df6d7e5c4',
  'src/qtgui/WorkspaceChurnSmoke.cpp': '268dbf069d763d2d3c6072db32e7700a3e8f9d27',
  'src/qtgui/main.cpp': 'ce91c425a47b93cfb8f79acaca14309a8c08fadb'
}
actual = set(subprocess.check_output(['git', 'diff', '--name-only']).decode().splitlines())
assert actual == set(expected), (actual, set(expected))
for name, sha in expected.items():
    actual_sha = subprocess.check_output(['git', 'hash-object', name]).decode().strip()
    assert actual_sha == sha, (name, actual_sha, sha)
subprocess.run(['git', 'diff', '--check'], check=True)
subprocess.run(['sh', 'build-aux/check-generated-build-metadata.sh'], check=True)
print('All twelve reviewed file hashes match; coordinate migration ready.')
