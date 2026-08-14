#include "AdaptiveSharpeningWorker.h"
#include "../libcolorscreen/include/analyze-scanner-blur.h"
#include <QtConcurrent>
#include <QThreadPool>
#include <vector>

AdaptiveSharpeningWorker::AdaptiveSharpeningWorker(
    colorscreen::scr_to_img_parameters scrToImg,
    colorscreen::render_parameters rparams,
    std::shared_ptr<colorscreen::image_data> scan,
    AdaptiveSharpeningParameters parameters,
    std::shared_ptr<colorscreen::progress_info> progress)
    : m_scrToImg(scrToImg), m_rparams(rparams), m_scan(scan),
      m_parameters(parameters), m_progress(progress) {}

/** Run adaptive blur/focus analysis using the settings selected in the GUI.
    The staged worker remains responsible for resolving zero-valued automatic
    dimensions, validating flag combinations and deriving the physical focus
    interpolation range.  */
void AdaptiveSharpeningWorker::run() {
    colorscreen::analyze_scanner_blur_worker worker(m_scrToImg, m_rparams, *m_scan);
    worker.strip_xsteps = m_parameters.stripXSteps;
    worker.strip_ysteps = m_parameters.stripYSteps;
    worker.xsteps = m_parameters.xSteps;
    worker.ysteps = m_parameters.ySteps;
    worker.xsubsteps = m_parameters.xSubsteps;
    worker.ysubsteps = m_parameters.ySubsteps;
    /* The GUI owns the outer progress task, so suppress the nested simplex
       progress display regardless of the user-selectable fitting flags.  */
    worker.flags = m_parameters.flags | colorscreen::finetune_no_progress_report;
    worker.optimize_strip_widths = m_parameters.optimizeStripWidthsInPrepass;
    worker.reoptimize_strip_widths = m_parameters.reoptimizeStripWidths;
    worker.skipmin = m_parameters.skipMin;
    worker.skipmax = m_parameters.skipMax;
    worker.tolerance = m_parameters.tolerance;
    worker.min_contrast = m_parameters.minimumContrast;
    worker.progress = m_progress.get();
    worker.verbose = false;
    worker.report_profile = m_parameters.reportProfile;
    worker.interpolate_focus = m_parameters.interpolateFocus;
    worker.focus_mtf_threshold = m_parameters.focusMtfThreshold;
    worker.focus_interpolation_nodes = m_parameters.focusInterpolationNodes;

    // Use local ThreadPool to avoid starving the global pool used by Renderer
    QThreadPool pool;
    // Leave one thread free for GUI/Renderer
    pool.setMaxThreadCount(std::max(1, QThread::idealThreadCount() - 1));

    if (!worker.step1()) {
        emit finished(false, nullptr, QString::fromStdString(worker.error()));
        return;
    }

    if (worker.do_strips()) {
        emit stripAnalysisStarted(worker.strip_xsteps, worker.strip_ysteps);
        // Run sequentially to support cancellation (or chunked parallel if needed, but sequential is safer for responsive cancel)
        // Actually, analyze_scanner_blur_img uses OpenMP. QtConcurrent::blockingMap is good but hard to cancel instantly.
        // However, we can check cancelled() in the functor.
        
        std::vector<QPair<int, int>> stripTasks;
        for (int y = 0; y < worker.strip_ysteps; y++) {
            for (int x = 0; x < worker.strip_xsteps; x++) {
                stripTasks.push_back({x, y});
            }
        }
        
        if (m_progress && m_progress->cancelled()) {
            emit finished(false, nullptr, tr("Analysis cancelled."));
            return;
        }

        for (const auto& task : stripTasks) {
            pool.start([this, &worker, task]() {
                if (worker.progress && worker.progress->cancelled()) return;
                
                colorscreen::coord_t red = 0, green = 0;
                if (worker.analyze_strips(task.first, task.second, &red, &green)) {
                    emit stripAnalyzed(task.first, task.second, red, green);
                }
            });
        }
        pool.waitForDone();
        
        if (m_progress && m_progress->cancelled()) {
            emit finished(false, nullptr, tr("Analysis cancelled."));
            return;
        }
    }

    if (!worker.step2()) {
        if (worker.report_profile)
            worker.print_profile();
        emit finished(false, nullptr,
                      QString::fromStdString(worker.error()));
        return;
    }

    int blurWidth = worker.xsteps * worker.xsubsteps;
    int blurHeight = worker.ysteps * worker.ysubsteps;
    emit blurAnalysisStarted(blurWidth, blurHeight);

    // Parallel loop for blur analysis
    std::vector<QPair<int, int>> blurTasks;
    // Note: step2 calculates actual xsteps/ysteps/substeps logic, so use worker values
    for (int y = 0; y < worker.ysteps * worker.ysubsteps; y++) {
        for (int x = 0; x < worker.xsteps * worker.xsubsteps; x++) {
            blurTasks.push_back({x, y});
        }
    }

    // Reuse pool for blur tasks
    for (const auto& task : blurTasks) {
        pool.start([this, &worker, task]() {
            if (worker.progress && worker.progress->cancelled()) return;
            
            colorscreen::rgbdata disp;
            if (worker.analyze_blur(task.first, task.second, &disp)) {
                 emit blurAnalyzed(task.first, task.second, disp.red); // Assuming uniform
            }
        });
    }
    pool.waitForDone();
    
    if (m_progress && m_progress->cancelled()) {
        emit finished(false, nullptr, tr("Analysis cancelled."));
        return;
    }

    auto result = worker.step3();
    if (worker.report_profile)
        worker.print_profile();
    if (result) {
        emit finished(
            true,
            std::shared_ptr<colorscreen::scanner_blur_correction_parameters>(
                result.release()),
            QString());
    } else {
        emit finished(false, nullptr, QString::fromStdString(worker.error()));
    }
}
