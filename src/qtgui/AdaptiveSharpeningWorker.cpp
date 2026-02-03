#include "AdaptiveSharpeningWorker.h"
#include "../libcolorscreen/include/analyze-scanner-blur.h"
#include <QtConcurrent>
#include <vector>

AdaptiveSharpeningWorker::AdaptiveSharpeningWorker(
    colorscreen::scr_to_img_parameters scrToImg,
    colorscreen::render_parameters rparams,
    std::shared_ptr<colorscreen::image_data> scan,
    int xsteps,
    std::shared_ptr<colorscreen::progress_info> progress)
    : m_scrToImg(scrToImg), m_rparams(rparams), m_scan(scan), m_xsteps(xsteps),
      m_progress(progress) {}

void AdaptiveSharpeningWorker::run() {
    // Mimic analyze_scanner_blur_img logic
    // Using default/hardcoded values for now as per analyze_scanner_blur_img signature
    // but allowing xsteps/ysteps customization.
    
    // Calculate dependent steps
    int ysteps = m_xsteps * m_scan->height / m_scan->width;
    if (ysteps < 1) ysteps = 1;

    // Hardcoded defaults matching CLI
    int strip_xsteps = 0;
    int strip_ysteps = 0;
    int xsubsteps = 0; // Means 0 -> defaults
    int ysubsteps = 0;
    uint64_t flags = colorscreen::finetune_position | 
                     colorscreen::finetune_no_progress_report | 
                     colorscreen::finetune_scanner_mtf_defocus;
    bool reoptimize_strip_widths = false;
    double skipmin = 25;
    double skipmax = 25;
    double tolerance = -1;

    colorscreen::analyze_scanner_blur_worker worker(m_scrToImg, m_rparams, *m_scan);
    worker.strip_xsteps = strip_xsteps;
    worker.strip_ysteps = strip_ysteps;
    worker.xsteps = m_xsteps;
    worker.ysteps = ysteps;
    worker.xsubsteps = xsubsteps;
    worker.ysubsteps = ysubsteps;
    worker.flags = flags;
    worker.reoptimize_strip_widths = reoptimize_strip_widths;
    worker.skipmin = skipmin;
    worker.skipmax = skipmax;
    worker.tolerance = tolerance;
    worker.progress = m_progress.get();
    worker.verbose = false; // Disable verbose for GUI

    if (!worker.step1()) {
        emit finished(false, nullptr);
        return;
    }

    if (worker.do_strips()) {
        // Run sequentially to support cancellation (or chunked parallel if needed, but sequential is safer for responsive cancel)
        // Actually, analyze_scanner_blur_img uses OpenMP. QtConcurrent::blockingMap is good but hard to cancel instantly.
        // However, we can check cancelled() in the functor.
        
        std::vector<QPair<int, int>> stripTasks;
        for (int y = 0; y < worker.strip_ysteps; y++) {
            for (int x = 0; x < worker.strip_xsteps; x++) {
                stripTasks.push_back({x, y});
            }
        }
        
        QtConcurrent::blockingMap(stripTasks, [&worker](const QPair<int, int>& task) {
             if (worker.progress && worker.progress->cancelled()) return;
             worker.analyze_strips(task.first, task.second);
        });
        
        if (m_progress && m_progress->cancelled()) {
            emit finished(false, nullptr);
            return;
        }
    }

    if (!worker.step2()) {
        emit finished(false, nullptr);
        return;
    }

    // Parallel loop for blur analysis
    std::vector<QPair<int, int>> blurTasks;
    // Note: step2 calculates actual xsteps/ysteps/substeps logic, so use worker values
    for (int y = 0; y < worker.ysteps * worker.ysubsteps; y++) {
        for (int x = 0; x < worker.xsteps * worker.xsubsteps; x++) {
            blurTasks.push_back({x, y});
        }
    }

    QtConcurrent::blockingMap(blurTasks, [&worker](const QPair<int, int>& task) {
        if (worker.progress && worker.progress->cancelled()) return;
        worker.analyze_blur(task.first, task.second);
    });
    
    if (m_progress && m_progress->cancelled()) {
        emit finished(false, nullptr);
        return;
    }

    auto result = worker.step3();
    if (result) {
        emit finished(true, std::shared_ptr<colorscreen::scanner_blur_correction_parameters>(result.release()));
    } else {
        emit finished(false, nullptr);
    }
}
