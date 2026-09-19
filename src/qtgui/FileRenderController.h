#pragma once

#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/imagedata.h"
#include "../libcolorscreen/include/progress-info.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/render-type-parameters.h"
#include "../libcolorscreen/include/scr-detect-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"

#include <QFuture>
#include <QFutureWatcher>
#include <QPointer>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

/** Own accepted file-render jobs after MainWindow has collected UI settings.

    MainWindow keeps save-path/settings dialogs and document snapshot policy.
    This controller owns background execution, progress identity, cooperative
    cancellation, incomplete-file cleanup, and completion handoff. */

class FileRenderController final : public QObject {
public:
  /** Immutable snapshot needed by one file render. */
  struct Request {
    std::shared_ptr<colorscreen::image_data> scan;
    colorscreen::scr_to_img_parameters scrParams;
    colorscreen::scr_detect_parameters detectParams;
    colorscreen::render_parameters renderParams;
    colorscreen::render_type_parameters renderType;
    QString outputPath;
    QString progressTitle;
    bool dng = false;
    bool hdr = false;
    int depth = 16;
    colorscreen::render_to_file_params::output_geometry geometry =
        colorscreen::render_to_file_params::default_geometry;
    int antialias = 0;
    double scale = 1.0;
    double screenScale = 0.0;
    int width = 0;
    int height = 0;
  };

  /** Document-owned presentation/lifetime callbacks. */
  struct Callbacks {
    std::function<bool()> isClosing;
    std::function<void(std::shared_ptr<colorscreen::progress_info>,
                       const QString &)> addProgress;
    std::function<void(std::shared_ptr<colorscreen::progress_info>)>
        removeProgress;
    std::function<void(const QString &, bool, bool)> finished;
  };

  /** Ensure no render worker survives the controller. */
  ~FileRenderController() override;

  /** Bind callbacks once before starting renders. */
  void configure(Callbacks callbacks);

  /** Start REQUEST in the global thread pool. */
  void start(Request request);

  /** Return true while any accepted file render is still running. */
  bool hasActiveRenders() const { return !m_activeJobs.empty(); }

  /** Return whether INFO belongs to an active file-render job. */
  bool ownsProgress(
      const std::shared_ptr<colorscreen::progress_info> &info) const;

  /** Request cooperative cancellation of every active file render. */
  void cancelAll();

  /** Cancel, join and clean every active render before document teardown. */
  void shutdown();

private:
  /** Return whether the owning document is closing. */
  bool isClosing() const;

  struct ActiveJob {
    std::shared_ptr<colorscreen::progress_info> progress;
    QString outputPath;
    QPointer<QFutureWatcher<bool>> watcher;
    QFuture<bool> future;
  };

  /** Forget one completed render job. */
  void removeActiveJob(
      const std::shared_ptr<colorscreen::progress_info> &progress);

  Callbacks m_callbacks;
  std::vector<ActiveJob> m_activeJobs;
  bool m_configured = false;
  bool m_shuttingDown = false;
};
