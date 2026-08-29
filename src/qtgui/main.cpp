#include "ColorScreenApplication.h"
#include "MainWindow.h"
#include "ImageViewWindow.h"
#include "ImageWidget.h"
#include "SharpnessPanel.h"
#include "CoordinateTransformer.h"
#include "WorkspaceWindow.h"
#include "progress-info.h"

#include <QAction>
#include <QColor>
#include <QCheckBox>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QDockWidget>
#include <QIcon>
#include <QPalette>
#include <QPushButton>
#include <QMdiArea>
#include <QMenuBar>
#include <QMdiSubWindow>
#include <QMouseEvent>
#include <QSettings>
#include <QStyleFactory>
#include <QStatusBar>
#include <QTabBar>
#include <QThread>
#include <QThreadPool>
#include <QToolBar>
#include <QTimer>

#include <cstring>
#include <functional>
#include <memory>

/** Start the Qt GUI, restore any crashed document session, and open every
    positional image argument in an independent MainWindow.  */
int main(int argc, char *argv[]) {
  // Enable plugin diagnostics before QApplication initializes platform plugins.
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--debug-qt") == 0) {
      qputenv("QT_DEBUG_PLUGINS", "1");
      qputenv("QT_LOGGING_RULES", "*=true");
      break;
    }
  }

  ColorScreenApplication app(argc, argv);
  QApplication::setOrganizationName("ColorScreen");
  QApplication::setOrganizationDomain("colorscreen.org");
  QApplication::setApplicationName("colorscreen-qt");
  QApplication::setApplicationVersion("1.1");

  // Use INI files on every platform so settings are easy to inspect and move.
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QApplication::setStyle(QStyleFactory::create("Fusion"));
  QApplication::setWindowIcon(QIcon(":/images/icon.svg"));

  QCommandLineParser parser;
  parser.setApplicationDescription("ColorScreen Qt GUI");
  parser.addHelpOption();
  parser.addVersionOption();

  QCommandLineOption debugOption("debug-qt", "Enable Qt plugin debugging");
  parser.addOption(debugOption);

  QCommandLineOption smokeTestOption(
      "smoke-test", "Run for N ms and exit (for CI smoke testing)", "ms",
      "5000");
  parser.addOption(smokeTestOption);

  QCommandLineOption expectedWindowsOption(
      "smoke-test-expect-windows",
      "Fail smoke testing unless exactly N document windows were created",
      "count");
  parser.addOption(expectedWindowsOption);

  QCommandLineOption expectedTabsOption(
      "smoke-test-expect-tabs",
      "Fail smoke testing unless exactly N image tabs were created", "count");
  parser.addOption(expectedTabsOption);

  QCommandLineOption detachReattachOption(
      "smoke-test-detach-reattach",
      "Exercise detaching and reattaching the active image document");
  parser.addOption(detachReattachOption);

  QCommandLineOption closeToEmptyTabOption(
      "smoke-test-close-to-empty-tab",
      "Close all documents and require the empty workspace shell to disappear");
  parser.addOption(closeToEmptyTabOption);

  QCommandLineOption expectedTabBarOption(
      "smoke-test-expect-tabbar",
      "Fail smoke testing unless the document tab bar is visible or hidden",
      "state");
  parser.addOption(expectedTabBarOption);

  QCommandLineOption mdiArrangementOption(
      "smoke-test-mdi-arrangements",
      "Exercise tile, cascade, and tabbed QMdiArea presentations");
  parser.addOption(mdiArrangementOption);

  QCommandLineOption tileActivationStableOption(
      "smoke-test-tile-activation-stable",
      "Require activating a tiled document to keep every tile in place");
  parser.addOption(tileActivationStableOption);

  QCommandLineOption menuOrderOption(
      "smoke-test-menu-order",
      "Require Registration, Window, Help as the final three top-level menus");
  parser.addOption(menuOrderOption);

  QCommandLineOption toolbarOrderOption(
      "smoke-test-toolbar-before-tabs",
      "Require the active document toolbar to be above the document tab bar");
  parser.addOption(toolbarOrderOption);

  QCommandLineOption dragDetachOption(
      "smoke-test-tab-drag-detach",
      "Exercise dragging a document tab out into a detached window");
  parser.addOption(dragDetachOption);

  QCommandLineOption tabbedFillOption(
      "smoke-test-tabbed-fills-workspace",
      "Require the active tabbed MDI document to fill the document viewport");
  parser.addOption(tabbedFillOption);

  QCommandLineOption globalStatusBarOption(
      "smoke-test-global-statusbar",
      "Require every attached tab to share the workspace status bar");
  parser.addOption(globalStatusBarOption);

  QCommandLineOption userVisibleProgressOption(
      "smoke-test-user-visible-progress",
      "Exercise dedicated Cancel/Stop rows for long-running tasks");
  parser.addOption(userVisibleProgressOption);

  QCommandLineOption newViewOption(
      "smoke-test-new-view",
      "Create a secondary view and verify shared image and independent render mode");
  parser.addOption(newViewOption);

  QCommandLineOption windowLifetimeOption(
      "smoke-test-window-lifetime",
      "Exercise peer-view lifetime, detached-window lifetime, and one-tab UI");
  parser.addOption(windowLifetimeOption);

  QCommandLineOption slantedReferenceOption(
      "smoke-test-slanted-reference",
      "Open the current image as a separate slanted-edge reference view");
  parser.addOption(slantedReferenceOption);

  QCommandLineOption timeReportOption(
      "time-report", "Enable internal time reporting of tasks");
  parser.addOption(timeReportOption);

  parser.addPositionalArgument("image", "Image file(s) to open.", "[image...]");
  parser.process(app);

  // Smoke tests need an explicit shutdown turn so queued widget destruction and
  // QtConcurrent work can be drained before sanitizers inspect process state.
  if (parser.isSet(smokeTestOption) || parser.isSet(closeToEmptyTabOption) ||
      parser.isSet(windowLifetimeOption))
    app.setQuitOnLastWindowClosed(false);

  if (parser.isSet(timeReportOption))
    colorscreen::time_report = true;

  // Set icon search paths and theme for packaged Windows/macOS applications.
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
  QStringList paths = QIcon::themeSearchPaths();
  const QString appDir = QCoreApplication::applicationDirPath();
  paths.prepend(appDir + "/../share/icons");
  paths.prepend(appDir + "/share/icons");
  QIcon::setThemeSearchPaths(paths);
  QIcon::setThemeName("Adwaita");
#endif

  QPalette darkPalette;
  darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
  darkPalette.setColor(QPalette::WindowText, Qt::white);
  darkPalette.setColor(QPalette::Base, QColor(25, 25, 25));
  darkPalette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
  darkPalette.setColor(QPalette::ToolTipBase, QColor(53, 53, 53));
  darkPalette.setColor(QPalette::ToolTipText, Qt::white);
  darkPalette.setColor(QPalette::Text, Qt::white);
  darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
  darkPalette.setColor(QPalette::ButtonText, Qt::white);
  darkPalette.setColor(QPalette::BrightText, Qt::red);
  darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
  darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
  darkPalette.setColor(QPalette::HighlightedText, Qt::black);
  darkPalette.setColor(QPalette::Mid, QColor(45, 45, 45));
  darkPalette.setColor(QPalette::Dark, QColor(35, 35, 35));
  darkPalette.setColor(QPalette::Light, QColor(65, 65, 65));
  darkPalette.setColor(QPalette::Disabled, QPalette::Text,
                       QColor(127, 127, 127));
  darkPalette.setColor(QPalette::Disabled, QPalette::WindowText,
                       QColor(127, 127, 127));
  darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText,
                       QColor(127, 127, 127));
  darkPalette.setColor(QPalette::Disabled, QPalette::Highlight,
                       QColor(80, 80, 80));
  darkPalette.setColor(QPalette::Disabled, QPalette::HighlightedText,
                       QColor(127, 127, 127));
  app.setPalette(darkPalette);

  // Smoke tests must be deterministic and must not consume a developer's
  // pending recovery session when run locally.
  const bool restoredSession =
      !parser.isSet(smokeTestOption) && app.restoreRecoverySession();
  const QStringList images = parser.positionalArguments();
  if (!images.isEmpty())
    app.openFiles(images, nullptr, parser.isSet(smokeTestOption));
  else if (!restoredSession && app.documentWindows().isEmpty())
    app.createDocumentWindow();

  if (parser.isSet(expectedWindowsOption)) {
    bool converted = false;
    const int expected = parser.value(expectedWindowsOption).toInt(&converted);
    QTimer::singleShot(0, &app, [&app, expected, converted]() {
      const int actual = app.documentWindows().size();
      if (!converted || expected < 0 || actual != expected) {
        qCritical() << "Smoke test expected" << expected
                    << "document windows but created" << actual;
        app.exit(2);
      }
    });
  }

  if (parser.isSet(expectedTabsOption)) {
    bool converted = false;
    const int expected = parser.value(expectedTabsOption).toInt(&converted);
    QTimer::singleShot(0, &app, [&app, expected, converted]() {
      const int actual = app.tabCount();
      if (!converted || expected < 0 || actual != expected) {
        qCritical() << "Smoke test expected" << expected
                    << "document tabs but created" << actual;
        app.exit(3);
      }
    });
  }

  if (parser.isSet(detachReattachOption)) {
    QTimer::singleShot(0, &app, [&app]() {
      WorkspaceWindow *workspace = app.workspaceWindow();
      MainWindow *document = workspace ? workspace->currentDocument() : nullptr;
      const int documentsBefore = app.documentWindows().size();
      const int tabsBefore = app.tabCount();
      if (!document || tabsBefore < 1) {
        qCritical() << "Smoke test has no active tab to detach";
        app.exit(4);
        return;
      }
      app.detachDocument(document);
      if (app.documentWindows().size() != documentsBefore ||
          app.tabCount() != tabsBefore - 1 || document->parentWidget()) {
        qCritical() << "Smoke test detach did not preserve the live document";
        app.exit(4);
        return;
      }
      app.attachDocument(document);
      if (app.documentWindows().size() != documentsBefore ||
          app.tabCount() != tabsBefore ||
          !workspace->containsDocument(document)) {
        qCritical() << "Smoke test reattach did not restore the same document";
        app.exit(4);
      }
    });
  }

  if (parser.isSet(closeToEmptyTabOption)) {
    QTimer::singleShot(50, &app, [&app]() {
      const QList<MainWindow *> documents = app.documentWindows();
      for (MainWindow *document : documents)
        document->close();
      QTimer::singleShot(100, &app, [&app]() {
        WorkspaceWindow *workspace = app.workspaceWindow();
        if (!app.documentWindows().isEmpty() || !app.viewWindows().isEmpty() ||
            app.tabCount() != 0 || (workspace && workspace->isVisible())) {
          qCritical() << "Smoke test left an empty application workspace";
          app.exit(5);
          return;
        }
        app.exit(0);
      });
    });
  }

  if (parser.isSet(expectedTabBarOption)) {
    const QString expected = parser.value(expectedTabBarOption).trimmed().toLower();
    QTimer::singleShot(100, &app, [&app, expected]() {
      WorkspaceWindow *workspace = app.workspaceWindow();
      const bool valid = expected == QStringLiteral("visible") ||
                         expected == QStringLiteral("hidden");
      const bool wantedVisible = expected == QStringLiteral("visible");
      const bool actualVisible = workspace && workspace->isTabBarVisible();
      if (!valid || actualVisible != wantedVisible) {
        qCritical() << "Smoke test expected document tab bar" << expected
                    << "but visibility was" << actualVisible;
        app.exit(6);
      }
    });
  }

  if (parser.isSet(mdiArrangementOption)) {
    QTimer::singleShot(100, &app, [&app]() {
      WorkspaceWindow *workspace = app.workspaceWindow();
      if (!workspace || workspace->tabCount() < 2) {
        qCritical() << "MDI arrangement smoke test requires two documents";
        app.exit(7);
        return;
      }
      workspace->tileDocuments();
      auto *mdiArea = workspace->findChild<QMdiArea *>(
          QStringLiteral("documentMdiArea"));
      const QList<QMdiSubWindow *> tiledWindows =
          mdiArea ? mdiArea->subWindowList() : QList<QMdiSubWindow *>();
      if (workspace->isTabbedView() || tiledWindows.size() < 2 ||
          !tiledWindows[0]->geometry().isValid() ||
          !tiledWindows[1]->geometry().isValid() ||
          !tiledWindows[0]->geometry().intersected(
              tiledWindows[1]->geometry()).isEmpty()) {
        qCritical() << "Tile Documents did not create distinct non-overlapping"
                       " subwindows";
        app.exit(7);
        return;
      }
      workspace->cascadeDocuments();
      const QList<QMdiSubWindow *> cascadedWindows =
          mdiArea ? mdiArea->subWindowList() : QList<QMdiSubWindow *>();
      if (workspace->isTabbedView() || cascadedWindows.size() < 2 ||
          cascadedWindows[0]->pos() == cascadedWindows[1]->pos()) {
        qCritical() << "Cascade Documents did not create offset subwindows";
        app.exit(7);
        return;
      }
      workspace->showTabbedDocuments();
      if (!workspace->isTabbedView()) {
        qCritical() << "Tabbed Documents did not restore tabbed mode";
        app.exit(7);
      }
    });
  }

  if (parser.isSet(tileActivationStableOption)) {
    QTimer::singleShot(150, &app, [&app]() {
      WorkspaceWindow *workspace = app.workspaceWindow();
      auto *mdiArea = workspace
                          ? workspace->findChild<QMdiArea *>(
                                QStringLiteral("documentMdiArea"))
                          : nullptr;
      if (!workspace || !mdiArea || workspace->tabCount() < 2) {
        qCritical() << "Tile activation stability test requires two documents";
        app.exit(11);
        return;
      }
      workspace->tileDocuments();
      const QList<QMdiSubWindow *> windows =
          mdiArea->subWindowList(QMdiArea::CreationOrder);
      if (windows.size() < 2) {
        app.exit(11);
        return;
      }
      QMdiSubWindow *first = windows[0];
      QMdiSubWindow *second = windows[1];
      const QPoint firstPosition = first->pos();
      const QPoint secondPosition = second->pos();
      QMdiSubWindow *target =
          mdiArea->activeSubWindow() == first ? second : first;
      mdiArea->setActiveSubWindow(target);
      if (mdiArea->activeSubWindow() != target ||
          first->pos() != firstPosition || second->pos() != secondPosition) {
        qCritical() << "Activating a tiled document moved or swapped tiles";
        app.exit(11);
        return;
      }

      // Do not leave later smoke checks in a tiled workspace.
      workspace->showTabbedDocuments();
    });
  }

  if (parser.isSet(menuOrderOption)) {
    QTimer::singleShot(250, &app, [&app]() {
      WorkspaceWindow *workspace = app.workspaceWindow();
      const QList<QAction *> actions =
          workspace && workspace->menuBar() ? workspace->menuBar()->actions()
                                            : QList<QAction *>();
      QStringList menus;
      for (QAction *action : actions)
        menus.append(action ? action->text().remove(QLatin1Char('&'))
                            : QString());
      const int registration = menus.indexOf(QStringLiteral("Registration"));
      const int window = menus.indexOf(QStringLiteral("Window"));
      const int help = menus.indexOf(QStringLiteral("Help"));
      if (registration < 0 || window != registration + 1 ||
          help != window + 1 || help != menus.size() - 1) {
        qCritical() << "Unexpected top-level menu order:" << menus;
        app.exit(12);
      }
    });
  }

  if (parser.isSet(toolbarOrderOption)) {
    QTimer::singleShot(100, &app, [&app]() {
      WorkspaceWindow *workspace = app.workspaceWindow();
      auto *mdiArea = workspace
                          ? workspace->findChild<QMdiArea *>(
                                QStringLiteral("documentMdiArea"))
                          : nullptr;
      auto *tabBar = mdiArea
                         ? mdiArea->findChild<QTabBar *>(
                               QString(), Qt::FindDirectChildrenOnly)
                         : nullptr;
      auto *toolbar = workspace
                          ? workspace->findChild<QToolBar *>(
                                QStringLiteral("MainToolbar"),
                                Qt::FindDirectChildrenOnly)
                          : nullptr;
      if (!tabBar || !toolbar ||
          toolbar->mapToGlobal(toolbar->rect().bottomLeft()).y() >=
              tabBar->mapToGlobal(tabBar->rect().topLeft()).y()) {
        qCritical() << "Document tabs are not below the shared toolbar";
        app.exit(8);
      }
    });
  }

  if (parser.isSet(dragDetachOption)) {
    QTimer::singleShot(250, &app, [&app]() {
      WorkspaceWindow *workspace = app.workspaceWindow();
      auto *mdiArea = workspace
                          ? workspace->findChild<QMdiArea *>(
                                QStringLiteral("documentMdiArea"))
                          : nullptr;
      auto *tabBar = mdiArea
                         ? mdiArea->findChild<QTabBar *>(
                               QString(), Qt::FindDirectChildrenOnly)
                         : nullptr;
      MainWindow *document = workspace ? workspace->currentDocument() : nullptr;
      const int tabsBefore = app.tabCount();
      if (!tabBar || !document || tabsBefore < 2 || !tabBar->isVisible()) {
        qCritical() << "Tab drag smoke test requires two visible document tabs";
        app.exit(9);
        return;
      }

      const int index = tabBar->currentIndex();
      const QPoint localStart = tabBar->tabRect(index).center();
      const QPoint globalStart = tabBar->mapToGlobal(localStart);
      const QPoint localOutside(-80, tabBar->height() + 80);
      const QPoint globalOutside = tabBar->mapToGlobal(localOutside);

      QMouseEvent press(QEvent::MouseButtonPress, QPointF(localStart),
                        QPointF(globalStart), Qt::LeftButton, Qt::LeftButton,
                        Qt::NoModifier);
      QCoreApplication::sendEvent(tabBar, &press);
      QMouseEvent move(QEvent::MouseMove, QPointF(localOutside),
                       QPointF(globalOutside), Qt::NoButton, Qt::LeftButton,
                       Qt::NoModifier);
      QCoreApplication::sendEvent(tabBar, &move);
      QMouseEvent release(QEvent::MouseButtonRelease, QPointF(localOutside),
                          QPointF(globalOutside), Qt::LeftButton,
                          Qt::NoButton, Qt::NoModifier);
      QCoreApplication::sendEvent(tabBar, &release);

      QPointer<MainWindow> guardedDocument(document);
      QTimer::singleShot(100, &app, [&app, workspace, guardedDocument,
                                     tabsBefore]() {
        if (!guardedDocument || app.tabCount() != tabsBefore - 1 ||
            guardedDocument->parentWidget()) {
          qCritical() << "Dragging a document tab did not detach it";
          app.exit(9);
          return;
        }
        app.attachDocument(guardedDocument);
        if (app.tabCount() != tabsBefore ||
            !workspace->containsDocument(guardedDocument)) {
          qCritical() << "Dragged document did not reattach intact";
          app.exit(9);
        }
      });
    });
  }

  if (parser.isSet(tabbedFillOption)) {
    QTimer::singleShot(200, &app, [&app]() {
      WorkspaceWindow *workspace = app.workspaceWindow();
      if (!workspace || workspace->tabCount() < 2) {
        qCritical() << "Tabbed fill smoke test requires two documents";
        app.exit(10);
        return;
      }
      workspace->showTabbedDocuments();
      QTimer::singleShot(0, &app, [&app, workspace]() {
        auto *mdiArea = workspace->findChild<QMdiArea *>(
            QStringLiteral("documentMdiArea"));
        QMdiSubWindow *active = mdiArea ? mdiArea->activeSubWindow() : nullptr;
        if (!mdiArea || !active || !workspace->isTabbedView()) {
          qCritical() << "Tabbed fill smoke test has no active tabbed subwindow";
          app.exit(10);
          return;
        }

        const QSize viewportSize = mdiArea->viewport()->size();
        const QSize documentSize = active->size();
        if (!(active->windowState() & Qt::WindowMaximized) ||
            documentSize.width() * 10 < viewportSize.width() * 9 ||
            documentSize.height() * 10 < viewportSize.height() * 9) {
          qCritical() << "Active tabbed document does not fill MDI viewport"
                      << "document" << documentSize << "viewport"
                      << viewportSize << "state" << active->windowState();
          app.exit(10);
        }
      });
    });
  }

  if (parser.isSet(globalStatusBarOption)) {
    QTimer::singleShot(200, &app, [&app]() {
      WorkspaceWindow *workspace = app.workspaceWindow();
      MainWindow *document = workspace ? workspace->currentDocument() : nullptr;
      QStatusBar *workspaceStatus = workspace ? workspace->statusBar() : nullptr;
      QWidget *progress = document ? document->workspaceStatusWidget() : nullptr;
      if (!workspaceStatus || !document || !progress ||
          !workspaceStatus->isAncestorOf(progress) ||
          document->statusBar() != workspaceStatus ||
          document->standaloneStatusBar()->isVisible()) {
        qCritical() << "Active document is not using the shared window status bar";
        app.exit(11);
        return;
      }

      MainWindow *inactiveDocument = nullptr;
      for (MainWindow *candidate : app.documentWindows()) {
        if (!candidate || !workspace->containsDocument(candidate))
          continue;
        if (candidate->statusBar() != workspaceStatus ||
            candidate->standaloneStatusBar()->isVisible() ||
            !workspaceStatus->isAncestorOf(candidate->workspaceStatusWidget())) {
          qCritical() << "Attached document has a private status bar";
          app.exit(11);
          return;
        }
        if (candidate != document && !inactiveDocument)
          inactiveDocument = candidate;
      }
      for (ImageViewWindow *view : app.viewWindows()) {
        if (view && workspace->containsView(view) &&
            (view->statusBar() != workspaceStatus ||
             view->standaloneStatusBar()->isVisible())) {
          qCritical() << "Attached secondary view has a private status bar";
          app.exit(11);
          return;
        }
      }

      const QString marker = QStringLiteral("workspace-status-smoke");
      document->statusBar()->showMessage(marker);
      QCoreApplication::processEvents();
      if (workspaceStatus->currentMessage() != marker) {
        qCritical() << "Document status message did not reach shared status bar";
        app.exit(11);
        return;
      }

      if (inactiveDocument) {
        const QString inactiveMarker =
            QStringLiteral("workspace-status-inactive-tab-smoke");
        inactiveDocument->statusBar()->showMessage(inactiveMarker);
        QCoreApplication::processEvents();
        if (workspaceStatus->currentMessage() != inactiveMarker) {
          qCritical() << "Inactive tab did not share the window status bar";
          app.exit(11);
          return;
        }
      }
      workspaceStatus->clearMessage();
    });
  }

  if (parser.isSet(userVisibleProgressOption)) {
    auto startProgressSmoke = std::make_shared<std::function<void(int)>>();
    const std::weak_ptr<std::function<void(int)>> weakStartProgressSmoke =
        startProgressSmoke;
    *startProgressSmoke =
        [&app, weakStartProgressSmoke](int attemptsLeft) {
      WorkspaceWindow *workspace = app.workspaceWindow();
      const QList<MainWindow *> documents = app.documentWindows();
      bool loaded = documents.size() >= 2;
      for (MainWindow *document : documents) {
        if (document && !document->currentImageFile().isEmpty() &&
            !document->sharedImageData()) {
          loaded = false;
          break;
        }
      }
      if (!workspace || !loaded) {
        if (attemptsLeft > 0) {
          if (auto retry = weakStartProgressSmoke.lock()) {
            QTimer::singleShot(100, &app, [retry, attemptsLeft]() {
              (*retry)(attemptsLeft - 1);
            });
            return;
          }
        }
        qCritical()
            << "User-visible progress smoke test requires two ready documents";
        app.exit(13);
        return;
      }

      MainWindow *cancelDocument = documents[0];
      MainWindow *stopDocument = documents[1];
      auto cancelProgress = std::make_shared<colorscreen::progress_info>();
      cancelProgress->set_task("cancel smoke task", 100);
      cancelProgress->set_progress(25);
      auto stopProgress = std::make_shared<colorscreen::progress_info>();
      stopProgress->set_task("stop smoke task", 100);
      stopProgress->set_progress(50);

      cancelDocument->addUserVisibleProgress(
          cancelProgress, QStringLiteral("Visible Cancel Task"));
      stopDocument->addUserVisibleProgress(
          stopProgress, QStringLiteral("Visible Stop Task"),
          ProgressAction::Stop);
      QCoreApplication::processEvents();

      QWidget *cancelContainer =
          cancelDocument->workspaceUserVisibleStatusWidget();
      QWidget *stopContainer = stopDocument->workspaceUserVisibleStatusWidget();
      QStatusBar *workspaceStatus = workspace->statusBar();
      QWidget *taskStack = workspace->findChild<QWidget *>(
          QStringLiteral("WorkspaceUserVisibleProgressStack"));
      if (!cancelContainer || !stopContainer || !workspaceStatus || !taskStack ||
          !taskStack->isAncestorOf(cancelContainer) ||
          !taskStack->isAncestorOf(stopContainer) ||
          workspaceStatus->isAncestorOf(cancelContainer) ||
          workspaceStatus->isAncestorOf(stopContainer) ||
          cancelContainer->isHidden() || stopContainer->isHidden()) {
        qCritical() << "User-visible progress did not remain in the dedicated "
                       "task strip above the status bar";
        app.exit(13);
        return;
      }

      const int statusHeight = workspaceStatus->height();

      // Switching the active image must not hide long tasks belonging to the
      // other document.
      workspace->activateDocument(cancelDocument);
      QCoreApplication::processEvents();
      if (!taskStack->isAncestorOf(cancelContainer) ||
          !taskStack->isAncestorOf(stopContainer) ||
          workspaceStatus->height() != statusHeight ||
          cancelContainer->isHidden() || stopContainer->isHidden()) {
        qCritical() << "User-visible progress disappeared after tab switch or "
                       "changed the one-line status bar height";
        app.exit(13);
        return;
      }

      // Transient work belongs to the workspace, not the selected tab. Start
      // work in the second document while the first remains active and require
      // the shared status line to present it after the normal display delay.
      auto transientProgress = std::make_shared<colorscreen::progress_info>();
      transientProgress->set_task("inactive document transient smoke task", 100);
      transientProgress->set_progress(10);
      stopDocument->addProgress(transientProgress);
      QThread::msleep(350);
      QCoreApplication::processEvents();
      QWidget *workspaceProgress = workspace->findChild<QWidget *>(
          QStringLiteral("WorkspaceProgressArea"));
      if (!workspaceProgress || workspaceProgress->isHidden() ||
          workspace->currentDocument() != cancelDocument ||
          workspace->displayedProgressDocument() != stopDocument ||
          !workspaceProgress->isAncestorOf(stopDocument->workspaceStatusWidget()) ||
          workspaceStatus->height() != statusHeight ||
          !taskStack->isAncestorOf(cancelContainer) ||
          !taskStack->isAncestorOf(stopContainer)) {
        qCritical() << "Inactive document transient progress was not presented globally";
        app.exit(13);
        return;
      }
      stopDocument->removeProgress(transientProgress);
      QCoreApplication::processEvents();
      if (workspace->currentDocument() != cancelDocument ||
          workspaceStatus->height() != statusHeight) {
        qCritical() << "Transient progress exit changed tab or status-bar height";
        app.exit(13);
        return;
      }

      const QList<QWidget *> cancelRows = cancelContainer->findChildren<QWidget *>(
          QStringLiteral("UserVisibleProgressRow"), Qt::FindDirectChildrenOnly);
      const QList<QWidget *> stopRows = stopContainer->findChildren<QWidget *>(
          QStringLiteral("UserVisibleProgressRow"), Qt::FindDirectChildrenOnly);
      if (cancelRows.size() != 1 || stopRows.size() != 1) {
        qCritical() << "Expected one dedicated row per user-visible task";
        app.exit(13);
        return;
      }

      QPushButton *cancelButton = cancelRows.front()->findChild<QPushButton *>();
      QPushButton *stopButton = stopRows.front()->findChild<QPushButton *>();
      if (!cancelButton || !stopButton || cancelButton->text() != "Cancel" ||
          stopButton->text() != "Stop" ||
          cancelButton->property("progressAction").toString() !=
              QStringLiteral("cancel") ||
          stopButton->property("progressAction").toString() !=
              QStringLiteral("stop")) {
        qCritical() << "Dedicated progress rows have incorrect actions";
        app.exit(13);
        return;
      }

      // Reproduce the real Stop path with the task owner as the current tab.
      // Removing the focused task row must not make QMdiArea fall back to the
      // first document.
      workspace->activateDocument(stopDocument);
      QCoreApplication::processEvents();
      if (workspace->currentDocument() != stopDocument) {
        qCritical() << "Could not activate Stop task owner before termination";
        app.exit(13);
        return;
      }

      // Reproduce a mouse Stop click. Dedicated task controls must not accept
      // mouse focus, otherwise focusing a workspace-global dock can make
      // QMdiArea select another child before the click handler even runs.
      if (stopButton->focusPolicy() != Qt::TabFocus) {
        qCritical() << "Dedicated Stop button unexpectedly accepts mouse focus";
        app.exit(13);
        return;
      }
      stopDocument->primaryImageWidget()->setFocus(Qt::OtherFocusReason);
      QCoreApplication::processEvents();
      if (workspace->currentDocument() != stopDocument) {
        qCritical() << "Focusing Stop task image changed the active document";
        app.exit(13);
        return;
      }
      stopButton->click();
      QCoreApplication::processEvents();
      if (!stopProgress->pool_cancel()) {
        qCritical() << "Stop progress action did not request termination";
        app.exit(13);
        return;
      }
      if (workspace->currentDocument() != stopDocument) {
        qCritical() << "Pressing Stop changed the active document";
        app.exit(13);
        return;
      }
      stopDocument->removeProgress(stopProgress);
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      QCoreApplication::processEvents();
      if (workspace->currentDocument() != stopDocument) {
        qCritical() << "Stopping a task changed the active document";
        app.exit(13);
        return;
      }

      cancelButton->click();
      if (!cancelProgress->pool_cancel()) {
        qCritical() << "Cancel progress action did not request termination";
        app.exit(13);
        return;
      }
      cancelDocument->removeProgress(cancelProgress);
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      QCoreApplication::processEvents();
      if (workspace->currentDocument() != stopDocument) {
        qCritical() << "Removing another document's task changed the active document";
        app.exit(13);
        return;
      }
        };
    QTimer::singleShot(250, &app, [startProgressSmoke]() {
      (*startProgressSmoke)(80);
    });
  }

  // The combined CI command runs New View and slanted-reference checks in one
  // process. Track completion explicitly instead of relying on wall-clock
  // delays that processEvents() can overtake on slow or instrumented builds.
  const auto newViewSmokeDone =
      std::make_shared<bool>(!parser.isSet(newViewOption));

  if (parser.isSet(newViewOption)) {
    QTimer::singleShot(300, &app, [&app, newViewSmokeDone]() {
      const QList<MainWindow *> documents = app.documentWindows();
      if (documents.isEmpty() || !documents.front()->sharedImageData()) {
        qCritical() << "New View smoke test requires a loaded document";
        app.exit(14);
        return;
      }

      MainWindow *source = documents.front();
      const auto sourceScan = source->sharedImageData();
      const auto sourceType = source->viewRenderTypeParameters().type;

      // Install a simple regular screen mapping so this smoke path exercises
      // both the public final-coordinate tile API and the GUI selector without
      // depending on a sidecar parameter file.
      ParameterState coordinateState = source->documentStateSnapshot();
      coordinateState.scrToImg.type = colorscreen::Dufay;
      coordinateState.scrToImg.center =
          {(colorscreen::coord_t)sourceScan->width / 2,
           (colorscreen::coord_t)sourceScan->height / 2};
      coordinateState.scrToImg.coordinate1 = {8, 0};
      coordinateState.scrToImg.coordinate2 = {0, 8};
      coordinateState.scrToImg.final_rotation = 12.5;
      coordinateState.scrToImg.final_mirror = true;
      // This state exists only to exercise coordinate rendering below.  Do not
      // put it on the undo stack: the final-view lifetime check later in this
      // smoke path must exercise closing a clean document, not maybeSave().
      source->applyState(coordinateState);

      auto basePresentation = coordinateState.rparams;
      basePresentation.scan_rotation = 0;
      basePresentation.scan_mirror = false;
      basePresentation.scan_crop.set = false;
      CoordinateTransformer finalBase(sourceScan.get(), basePresentation,
                                      &coordinateState.scrToImg,
                                      colorscreen::render_final_coordinates);
      auto changedPresentation = basePresentation;
      changedPresentation.scan_rotation = 1;
      changedPresentation.scan_mirror = true;
      changedPresentation.scan_crop.set = true;
      changedPresentation.scan_crop.x = sourceScan->width / 4;
      changedPresentation.scan_crop.y = sourceScan->height / 4;
      changedPresentation.scan_crop.width = sourceScan->width / 2;
      changedPresentation.scan_crop.height = sourceScan->height / 2;
      CoordinateTransformer finalChanged(sourceScan.get(), changedPresentation,
                                         &coordinateState.scrToImg,
                                         colorscreen::render_final_coordinates);
      const auto baseRange = finalBase.getRenderCrop();
      const auto changedRange = finalChanged.getRenderCrop();
      const colorscreen::point_t probe = coordinateState.scrToImg.center;
      const auto baseProbe = finalBase.scanToTransformedCrop(probe);
      const auto changedProbe = finalChanged.scanToTransformedCrop(probe);
      if (finalBase.getTransformedCropSize() !=
              finalChanged.getTransformedCropSize() ||
          baseRange.x != changedRange.x || baseRange.y != changedRange.y ||
          baseRange.width != changedRange.width ||
          baseRange.height != changedRange.height ||
          qAbs(baseProbe.x - changedProbe.x) > 1e-9 ||
          qAbs(baseProbe.y - changedProbe.y) > 1e-9) {
        qCritical() << "Scan presentation leaked into final coordinates";
        app.exit(14);
        return;
      }

      colorscreen::render_type_parameters apiRender;
      apiRender.type = colorscreen::render_type_original;
      apiRender.color = sourceScan->has_rgb();
      colorscreen::tile_parameters apiTile;
      std::vector<unsigned char> apiPixels(8 * 8 * 3);
      apiTile.pixels = apiPixels.data();
      apiTile.rowstride = 8 * 3;
      apiTile.pixelbytes = 3;
      apiTile.width = 8;
      apiTile.height = 8;
      apiTile.pos = {0, 0};
      apiTile.step = 1;
      auto apiRparams = coordinateState.rparams;
      auto apiScrToImg = coordinateState.scrToImg;
      auto apiDetect = coordinateState.detect;
      if (!colorscreen::render_tile(*sourceScan, apiScrToImg, apiDetect,
                                    apiRparams, apiRender, apiTile,
                                    colorscreen::render_final_coordinates,
                                    nullptr)) {
        qCritical() << "Public render_tile final-coordinate API failed";
        app.exit(14);
        return;
      }
      const int previousTabs = app.tabCount();
      ImageViewWindow *view = app.createViewWindow(source);
      QPointer<MainWindow> guardedSource(source);
      QPointer<ImageViewWindow> guardedView(view);
      WorkspaceWindow *workspace = app.workspaceWindow();
      if (!view || view->sourceDocument() != source ||
          view->sharedImageData() != sourceScan ||
          app.documentWindows().size() != documents.size() ||
          !workspace || !workspace->containsView(view) ||
          app.tabCount() != previousTabs + 1 || view->isWindow()) {
        qCritical() << "New View was not added as a shared-image workspace tab";
        app.exit(14);
        return;
      }

      QWidget *inspector = view->workspaceInspectorWidget();
      QDockWidget *workspaceInspector = workspace->findChild<QDockWidget *>(
          QStringLiteral("DocumentControlsDock"));
      if (!inspector || inspector != source->workspaceInspectorWidget() ||
          !inspector->findChild<QWidget *>(QStringLiteral("ConfigTabs")) ||
          !workspaceInspector || workspaceInspector->isHidden() ||
          !workspaceInspector->isAncestorOf(inspector) ||
          source->inspectorImageWidget() != view->imageWidget()) {
        qCritical() << "New View does not present the owning document's full "
                       "inspector and Navigation/panel controls";
        app.exit(14);
        return;
      }

      // Ordinary New Views must expose the same document-editing chrome as the
      // primary presentation while keeping render/color/coordinate controls
      // view-local. In particular Edit and Registration may not disappear.
      QStringList viewMenus;
      for (QAction *action : view->menuBar()->actions())
        viewMenus << QString(action->text()).remove('&');
      const QStringList expectedViewMenus = {QStringLiteral("File"),
                                             QStringLiteral("Edit"),
                                             QStringLiteral("View"),
                                             QStringLiteral("Registration"),
                                             QStringLiteral("Window"),
                                             QStringLiteral("Help")};
      if (viewMenus != expectedViewMenus) {
        qCritical() << "New View menu chrome differs from an ordinary document"
                    << viewMenus;
        app.exit(14);
        return;
      }

      QCheckBox *viewColorToggle = nullptr;
      bool hasSelectTool = false;
      bool hasAddPointTool = false;
      if (QToolBar *toolbar = view->workspaceToolBar()) {
        for (QCheckBox *checkBox : toolbar->findChildren<QCheckBox *>())
          if (checkBox->text() == QObject::tr("Color"))
            viewColorToggle = checkBox;
        for (QAction *action : toolbar->actions()) {
          if (action->text() == QObject::tr("Select"))
            hasSelectTool = true;
          if (action->text() == QObject::tr("Add Point"))
            hasAddPointTool = true;
        }
      }
      if (!viewColorToggle ||
          (sourceScan->has_rgb() && viewColorToggle->isHidden()) ||
          !hasSelectTool || !hasAddPointTool) {
        qCritical() << "New View toolbar is missing ordinary document controls";
        app.exit(14);
        return;
      }

      // One-shot canvas tools belong to the document operation, not to the
      // canvas that happened to be active when they were armed. Verify that
      // distance and area tools migrate to another ordinary view of the same
      // loaded image and leave the old view inert.
      workspace->activateDocument(source);
      QCoreApplication::processEvents();
      if (!QMetaObject::invokeMethod(source, "onMeasureRequested",
                                     Qt::DirectConnection) ||
          source->primaryImageWidget()->interactionMode() !=
              ImageWidget::MeasureMode) {
        qCritical() << "Could not arm Measure in the primary view";
        app.exit(14);
        return;
      }
      workspace->activateView(view);
      QCoreApplication::processEvents();
      if (source->inspectorImageWidget() != view->imageWidget() ||
          view->imageWidget()->interactionMode() != ImageWidget::MeasureMode ||
          source->primaryImageWidget()->interactionMode() !=
              ImageWidget::PanMode) {
        qCritical() << "Measure tool did not follow the active ordinary view";
        app.exit(14);
        return;
      }
      view->imageWidget()->setInteractionMode(ImageWidget::PanMode);

      workspace->activateDocument(source);
      QCoreApplication::processEvents();
      if (!QMetaObject::invokeMethod(source, "onCropRequested",
                                     Qt::DirectConnection) ||
          source->primaryImageWidget()->interactionMode() !=
              ImageWidget::CropMode) {
        qCritical() << "Could not arm Crop in the primary view";
        app.exit(14);
        return;
      }
      workspace->activateView(view);
      QCoreApplication::processEvents();
      if (source->inspectorImageWidget() != view->imageWidget() ||
          view->imageWidget()->interactionMode() != ImageWidget::CropMode ||
          source->primaryImageWidget()->interactionMode() !=
              ImageWidget::PanMode) {
        qCritical() << "Area-selection tool did not follow the active ordinary view";
        app.exit(14);
        return;
      }
      if (!QMetaObject::invokeMethod(source, "onCropRequested",
                                     Qt::DirectConnection) ||
          view->imageWidget()->interactionMode() != ImageWidget::PanMode) {
        qCritical() << "Could not cancel transferred Crop tool";
        app.exit(14);
        return;
      }

      app.detachView(view);
      QCoreApplication::processEvents();
      if (!guardedSource || !guardedView) {
        qCritical() << "New View disappeared while detaching";
        app.exit(14);
        return;
      }
      guardedView->activateWindow();
      QCoreApplication::processEvents();
      if (!guardedSource || !guardedView) {
        qCritical() << "New View disappeared while activating detached view";
        app.exit(14);
        return;
      }
      QDockWidget *detachedInspector = guardedView->findChild<QDockWidget *>(
          QStringLiteral("SecondaryDocumentControlsDock"));
      if (!view->isWindow() || !detachedInspector ||
          detachedInspector->isHidden() ||
          !detachedInspector->isAncestorOf(inspector) ||
          source->inspectorImageWidget() != view->imageWidget()) {
        qCritical() << "Detached New View did not keep the full document panels";
        app.exit(14);
        return;
      }

      app.attachView(view);
      QCoreApplication::processEvents();
      if (!guardedSource || !guardedView) {
        qCritical() << "New View disappeared while reattaching";
        app.exit(14);
        return;
      }
      if (!workspace->containsView(guardedView) ||
          !workspaceInspector->isAncestorOf(inspector) ||
          source->inspectorImageWidget() != view->imageWidget()) {
        qCritical() << "Reattached New View did not restore shared panels";
        app.exit(14);
        return;
      }

      bool hasIconOnlyZoom = false;
      bool hasIconOnlyRotation = false;
      if (QToolBar *toolbar = view->workspaceToolBar()) {
        for (QAction *action : toolbar->actions()) {
          const QString actionText =
              QString(action->text()).remove(QLatin1Char('&'));
          if (actionText == QObject::tr("Zoom In") && !action->icon().isNull())
            hasIconOnlyZoom = true;
          if (actionText == QObject::tr("Rotate Right") && !action->icon().isNull())
            hasIconOnlyRotation = true;
        }
      }
      if (!hasIconOnlyZoom || !hasIconOnlyRotation) {
        qCritical() << "New View toolbar does not use the standard image-view icons";
        app.exit(14);
        return;
      }

      if (!source->primaryImageWidget() ||
          source->primaryImageWidget()->coordinateSpace() !=
              colorscreen::render_scan_coordinates ||
          !view->setCoordinateSpace(colorscreen::render_final_coordinates) ||
          view->coordinateSpace() != colorscreen::render_final_coordinates ||
          source->primaryImageWidget()->coordinateSpace() !=
              colorscreen::render_scan_coordinates) {
        qCritical() << "New View Scan/Screen coordinate selection is not independent";
        app.exit(14);
        return;
      }

      colorscreen::render_type_t alternate = sourceType;
      const colorscreen::render_type_t candidates[] = {
          colorscreen::render_type_original,
          colorscreen::render_type_image_layer,
          colorscreen::render_type_interpolated,
          colorscreen::render_type_realistic,
          colorscreen::render_type_screen};
      bool changed = false;
      for (colorscreen::render_type_t candidate : candidates) {
        if (candidate != sourceType && view->setRenderType(candidate)) {
          alternate = candidate;
          changed = true;
          break;
        }
      }
      if (!changed || view->renderType() != alternate ||
          source->viewRenderTypeParameters().type != sourceType) {
        qCritical() << "New View render mode is not independent";
        app.exit(14);
        return;
      }

      // The primary presentation is a peer of New View. Closing it must keep
      // the logical document and shared image alive until this final view also
      // closes. This simultaneously covers destruction of one loaded document
      // while another independent image remains open.
      const int documentCount = documents.size();
      if (source->close() || !guardedSource || !guardedView ||
          app.isDocumentPresentationOpen(source) ||
          workspace->containsDocument(source) ||
          !workspace->containsView(view) || view->sourceDocument() != source ||
          view->sharedImageData() != sourceScan) {
        qCritical() << "Closing the primary view destroyed or detached its peers";
        app.exit(14);
        return;
      }

      if (!app.closeView(view)) {
        qCritical() << "Closing the final peer view was rejected unexpectedly";
        app.exit(14);
        return;
      }

      auto checkFinalPeerClose =
          std::make_shared<std::function<void(int)>>();
      const std::weak_ptr<std::function<void(int)>> weakCheckFinalPeerClose =
          checkFinalPeerClose;
      *checkFinalPeerClose =
          [&app, guardedSource, guardedView, documentCount, newViewSmokeDone,
           weakCheckFinalPeerClose](int attemptsLeft) {
            if (guardedSource || guardedView ||
                app.documentWindows().size() != documentCount - 1) {
              if (attemptsLeft > 0) {
                if (auto retry = weakCheckFinalPeerClose.lock()) {
                  QTimer::singleShot(50, &app, [retry, attemptsLeft]() {
                    (*retry)(attemptsLeft - 1);
                  });
                  return;
                }
              }
              qCritical() << "Final peer view did not close its document owner";
              app.exit(14);
              return;
            }
            *newViewSmokeDone = true;
          };
      QTimer::singleShot(0, &app, [checkFinalPeerClose]() {
        (*checkFinalPeerClose)(40);
      });
    });
  }

  if (parser.isSet(windowLifetimeOption)) {
    auto startWindowLifetime =
        std::make_shared<std::function<void(int)>>();
    const std::weak_ptr<std::function<void(int)>> weakStartWindowLifetime =
        startWindowLifetime;
    *startWindowLifetime = [&app, weakStartWindowLifetime](int attemptsLeft) {
      const QList<MainWindow *> documents = app.documentWindows();
      WorkspaceWindow *workspace = app.workspaceWindow();
      if (documents.size() != 1 || !documents.front()->sharedImageData() ||
          !workspace || app.tabCount() != 1 || !workspace->isTabBarVisible()) {
        if (attemptsLeft > 0) {
          if (auto retry = weakStartWindowLifetime.lock()) {
            QTimer::singleShot(250, &app, [retry, attemptsLeft]() {
              (*retry)(attemptsLeft - 1);
            });
            return;
          }
        }
        qCritical() << "Window lifetime smoke test requires one standard tab";
        app.exit(16);
        return;
      }

      MainWindow *source = documents.front();
      app.detachDocument(source);
      QCoreApplication::processEvents();
      if (workspace->isVisible() || !source->isVisible() ||
          !source->isWindow() || app.tabCount() != 0) {
        qCritical() << "Detaching the sole image left a useless workspace shell";
        app.exit(16);
        return;
      }

      app.attachDocument(source);
      QCoreApplication::processEvents();
      if (!workspace->isVisible() || !workspace->containsDocument(source) ||
          app.tabCount() != 1 || !workspace->isTabBarVisible()) {
        qCritical() << "Reattaching the sole image did not restore a standard tab";
        app.exit(16);
        return;
      }

      ImageViewWindow *view = app.createViewWindow(source, true);
      if (!view || !view->isWindow() || !view->isVisible()) {
        qCritical() << "Could not create detached peer view";
        app.exit(16);
        return;
      }

      QPointer<MainWindow> guardedSource(source);
      QPointer<ImageViewWindow> guardedView(view);
      if (!workspace->close()) {
        qCritical() << "Workspace close was rejected with a detached peer";
        app.exit(16);
        return;
      }
      QCoreApplication::processEvents();
      if (workspace->isVisible() || !guardedSource || !guardedView ||
          !guardedView->isVisible() ||
          app.isDocumentPresentationOpen(guardedSource)) {
        qCritical() << "Closing the workspace also closed its detached peer";
        app.exit(16);
        return;
      }

      if (!app.closeView(guardedView)) {
        qCritical() << "Final detached peer did not accept close";
        app.exit(16);
        return;
      }

      auto checkFinalDetachedClose =
          std::make_shared<std::function<void(int)>>();
      const std::weak_ptr<std::function<void(int)>>
          weakCheckFinalDetachedClose = checkFinalDetachedClose;
      *checkFinalDetachedClose =
          [&app, workspace, guardedSource, guardedView,
           weakCheckFinalDetachedClose](int attemptsLeft) {
            // WA_DeleteOnClose is intentionally asynchronous.  In particular,
            // ASan can make the Windows event loop reach this check before Qt
            // has handled the queued widget destruction.
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCoreApplication::processEvents();
            if (guardedSource || guardedView ||
                !app.documentWindows().isEmpty() ||
                !app.viewWindows().isEmpty() ||
                (workspace && workspace->isVisible())) {
              if (attemptsLeft > 0) {
                if (auto retry = weakCheckFinalDetachedClose.lock()) {
                  QTimer::singleShot(50, &app, [retry, attemptsLeft]() {
                    (*retry)(attemptsLeft - 1);
                  });
                  return;
                }
              }
              qCritical()
                  << "Last application window did not release its document";
              app.exit(16);
              return;
            }
            app.exit(0);
          };
      QTimer::singleShot(0, &app, [checkFinalDetachedClose]() {
        (*checkFinalDetachedClose)(40);
      });
    };
    QTimer::singleShot(300, &app, [startWindowLifetime]() {
      (*startWindowLifetime)(40);
    });
  }

  if (parser.isSet(slantedReferenceOption)) {
    auto startReferenceSmoke =
        std::make_shared<std::function<void(int)>>();
    const std::weak_ptr<std::function<void(int)>> weakStartReferenceSmoke =
        startReferenceSmoke;
    *startReferenceSmoke =
        [&app, newViewSmokeDone, weakStartReferenceSmoke](int attemptsLeft) {
      if (!*newViewSmokeDone) {
        if (attemptsLeft <= 0) {
          qCritical() << "Slanted reference smoke test timed out waiting for "
                         "New View to finish";
          app.exit(15);
          return;
        }
        if (auto retry = weakStartReferenceSmoke.lock()) {
          QTimer::singleShot(100, &app, [retry, attemptsLeft]() {
            (*retry)(attemptsLeft - 1);
          });
          return;
        }
        qCritical() << "Slanted reference smoke retry expired unexpectedly";
        app.exit(15);
        return;
      }

      const QList<MainWindow *> documents = app.documentWindows();
      MainWindow *source = nullptr;
      for (MainWindow *document : documents) {
        if (document && app.isDocumentPresentationOpen(document) &&
            !document->currentImageFile().isEmpty()) {
          source = document;
          break;
        }
      }
      if (!source || !source->sharedImageData()) {
        if (attemptsLeft > 0) {
          if (auto retry = weakStartReferenceSmoke.lock()) {
            QTimer::singleShot(100, &app, [retry, attemptsLeft]() {
              (*retry)(attemptsLeft - 1);
            });
            return;
          }
        }
        qCritical() << "Slanted reference smoke test requires a visible loaded image";
        app.exit(15);
        return;
      }
      const int documentCount = documents.size();
      const int tabCount = app.tabCount();
      QPointer<MainWindow> guardedSource(source);
      QPointer<ImageViewWindow> guardedReference(
          app.createSlantedEdgeReference(source, source->currentImageFile()));
      ImageViewWindow *reference = guardedReference.data();
      if (!reference) {
        qCritical() << "Could not create slanted-edge reference view";
        app.exit(15);
        return;
      }

      auto checkReference =
          std::make_shared<std::function<void(int)>>();
      const std::weak_ptr<std::function<void(int)>> weakCheckReference =
          checkReference;
      *checkReference = [&app, guardedSource, guardedReference, documentCount,
                         tabCount, weakCheckReference](int attemptsLeft) {
        MainWindow *source = guardedSource.data();
        ImageViewWindow *reference = guardedReference.data();
        if (!source || !reference) {
          qCritical() << "Slanted-edge reference disappeared while waiting for its image";
          app.exit(15);
          return;
        }
        if (!reference->sharedImageData() && attemptsLeft > 0) {
          if (auto retry = weakCheckReference.lock()) {
            QTimer::singleShot(250, &app, [retry, attemptsLeft]() {
              (*retry)(attemptsLeft - 1);
            });
            return;
          }
        }

        WorkspaceWindow *workspace = app.workspaceWindow();
        if (!reference || !reference->isSlantedEdgeReference() ||
            reference->sourceDocument() != source ||
            !reference->sharedImageData() ||
            reference->sharedImageData() == source->sharedImageData() ||
            app.documentWindows().size() != documentCount || !workspace ||
            !workspace->containsView(reference) ||
            app.tabCount() != tabCount + 1 ||
            !reference->workspaceInspectorWidget() ||
            !reference->workspaceInspectorWidget()->findChild<QWidget *>(
                QStringLiteral("SlantedEdgeNavigation")) ||
            !reference->workspaceInspectorWidget()->findChild<QWidget *>(
                QStringLiteral("SlantedEdgeReferenceTabs"))) {
          qCritical() << "Slanted-edge reference did not remain a specialized "
                         "shared-parameter workspace view";
          app.exit(15);
          return;
        }

        if (!reference->setRenderType(colorscreen::render_type_original) ||
            !reference->setRenderType(colorscreen::render_type_image_layer) ||
            reference->setRenderType(colorscreen::render_type_interpolated) ||
            reference->setCoordinateSpace(colorscreen::render_final_coordinates)) {
          qCritical() << "Slanted-edge reference exposes unexpected render modes";
          app.exit(15);
          return;
        }

        // Reproduce the MTF-detach failure with the source and specialized
        // reference visible as MDI tiles.  The reference owns its Sharpness
        // panel, so the detached chart must be adopted by a dock belonging to
        // that view rather than disappearing from the detachable section.
        workspace->tileDocuments();
        workspace->activateView(reference);
        QCoreApplication::processEvents();
        SharpnessPanel *sharpness =
            reference->workspaceInspectorWidget()->findChild<SharpnessPanel *>();
        QWidget *mtfChart = sharpness ? sharpness->getMTFChartWidget() : nullptr;
        QWidget *mtfSection = mtfChart ? mtfChart->parentWidget() : nullptr;
        QPushButton *detachMtf = nullptr;
        if (mtfSection) {
          for (QPushButton *button : mtfSection->findChildren<QPushButton *>()) {
            if (button && button->text() == QStringLiteral("Detach")) {
              detachMtf = button;
              break;
            }
          }
        }
        if (!sharpness || !mtfChart || !detachMtf) {
          qCritical() << "Could not locate reference MTF detachable section";
          app.exit(15);
          return;
        }
        detachMtf->click();
        QCoreApplication::processEvents();
        QDockWidget *mtfDock = nullptr;
        for (QDockWidget *candidate : workspace->findChildren<QDockWidget *>()) {
          if (candidate && candidate->property("detachablePanel").toBool() &&
              candidate->property("detachableTitle").toString() ==
                  QStringLiteral("MTF Chart") &&
              candidate->isAncestorOf(mtfChart)) {
            mtfDock = candidate;
            break;
          }
        }
        if (!mtfDock || !mtfDock->isVisible() || !mtfDock->isFloating() ||
            !mtfDock->widget() || !mtfDock->isAncestorOf(mtfChart)) {
          qCritical() << "Reference MTF chart disappeared instead of detaching";
          app.exit(15);
          return;
        }
        QPointer<QDockWidget> guardedMtfDock(mtfDock);
        mtfDock->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        if (guardedMtfDock && guardedMtfDock->widget()) {
          qCritical() << "Generic MTF dock retained its content after close";
          app.exit(15);
          return;
        }
        if (!reference->workspaceInspectorWidget()->isAncestorOf(mtfChart) ||
            detachMtf->text() != QStringLiteral("Detach")) {
          qCritical() << "Reference MTF chart did not reattach after dock close";
          app.exit(15);
          return;
        }

        // Exercise the same implementation in two unrelated document panels.
        workspace->activateDocument(source);
        QCoreApplication::processEvents();
        auto exerciseDetachable = [workspace, source](const QString &title) {
          QWidget *inspector = source->workspaceInspectorWidget();
          QWidget *section = nullptr;
          for (QWidget *candidate : inspector->findChildren<QWidget *>()) {
            if (candidate->objectName() == QStringLiteral("DetachableSection") &&
                candidate->property("detachableTitle").toString() == title) {
              section = candidate;
              break;
            }
          }
          QPushButton *button = section
                                    ? section->findChild<QPushButton *>(
                                          QStringLiteral("DetachableSectionButton"))
                                    : nullptr;
          QWidget *content = nullptr;
          if (section) {
            for (QWidget *candidate : section->findChildren<QWidget *>()) {
              if (candidate->property("detachableContentTitle").toString() ==
                  title) {
                content = candidate;
                break;
              }
            }
          }
          if (!section || !button || !content)
            return false;
          button->click();
          QCoreApplication::processEvents();
          QDockWidget *dock = nullptr;
          for (QDockWidget *candidate : workspace->findChildren<QDockWidget *>()) {
            if (candidate->property("detachablePanel").toBool() &&
                candidate->property("detachableTitle").toString() == title &&
                candidate->isAncestorOf(content)) {
              dock = candidate;
              break;
            }
          }
          if (!dock || !dock->isVisible() || !dock->isFloating() ||
              button->text() != QStringLiteral("Reattach"))
            return false;
          QPointer<QDockWidget> guardedDock(dock);
          dock->close();
          QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
          QCoreApplication::processEvents();
          return (!guardedDock || !guardedDock->widget()) &&
                 inspector->isAncestorOf(content) &&
                 button->text() == QStringLiteral("Detach");
        };
        if (!exerciseDetachable(QStringLiteral("H&D Curve")) ||
            !exerciseDetachable(QStringLiteral("Backlight"))) {
          qCritical() << "Unrelated panels do not share the generic detach lifecycle";
          app.exit(15);
          return;
        }
        workspace->activateView(reference);
        QCoreApplication::processEvents();
        workspace->showTabbedDocuments();
        workspace->activateView(reference);
        QCoreApplication::processEvents();

        const auto beforeReload = reference->sharedImageData();
        app.reloadSlantedEdgeReferences(source);
        auto checkReload = std::make_shared<std::function<void(int)>>();
        const std::weak_ptr<std::function<void(int)>> weakCheckReload =
            checkReload;
        *checkReload = [&app, guardedSource, guardedReference, beforeReload,
                        weakCheckReload](int attemptsLeft) {
          MainWindow *source = guardedSource.data();
          ImageViewWindow *reference = guardedReference.data();
          if (!source || !reference) {
            qCritical() << "Slanted-edge reference disappeared during reload";
            app.exit(15);
            return;
          }
          if (reference->sharedImageData() == beforeReload &&
              attemptsLeft > 0) {
            if (auto retry = weakCheckReload.lock()) {
              QTimer::singleShot(250, &app, [retry, attemptsLeft]() {
                (*retry)(attemptsLeft - 1);
              });
              return;
            }
          }
          if (reference->sharedImageData() == beforeReload) {
            qCritical() << "Slanted-edge reference did not reload";
            app.exit(15);
            return;
          }

          // Creating the reference must already have persisted it. Replay that
          // recovery metadata while the original remains open; this should
          // create one additional specialized reference view. In a real crash
          // recovery the document starts with no secondary views.
          if (app.restoreSlantedEdgeReferencesFromRecovery(source) != 1) {
            qCritical() << "Slanted-edge recovery metadata did not recreate "
                           "the recorded reference";
            app.exit(15);
            return;
          }

          QList<ImageViewWindow *> references;
          for (ImageViewWindow *view : app.viewWindows()) {
            if (view && view->sourceDocument() == source &&
                view->isSlantedEdgeReference())
              references.append(view);
          }
          WorkspaceWindow *workspace = app.workspaceWindow();
          if (references.size() != 2 || !workspace) {
            qCritical() << "Recovery did not recreate exactly one reference";
            app.exit(15);
            return;
          }
          for (ImageViewWindow *view : references) {
            if (view->referenceFile() != source->currentImageFile() ||
                !workspace->containsView(view)) {
              qCritical() << "Recovered slanted-edge reference has wrong "
                             "file or presentation";
              app.exit(15);
              return;
            }
          }
          for (ImageViewWindow *view : references)
            app.closeView(view);
        };
        (*checkReload)(28);
      };
      (*checkReference)(28);
    };
    QTimer::singleShot(350, &app, [startReferenceSmoke]() {
      (*startReferenceSmoke)(80);
    });
  }

  if (parser.isSet(smokeTestOption)) {
    bool converted = false;
    int duration = parser.value(smokeTestOption).toInt(&converted);
    if (!converted || duration <= 0)
      duration = 5000;
    qDebug() << "Smoke Test Mode: Will exit in" << duration << "ms...";
    QTimer::singleShot(duration, &app, [&app]() {
      app.closeAllDocumentWindows();
      app.quit();
    });
  }

  // WorkspaceWindow deliberately survives Close while detached peer windows
  // exist. Keep an explicit owner in main() so the hidden workspace cannot
  // outlive QApplication teardown.
  QPointer<WorkspaceWindow> workspaceOwner(app.workspaceWindow());
  const int exitCode = app.exec();

  if (parser.isSet(smokeTestOption)) {
    // A smoke test can exit while a slanted-reference QtConcurrent load is
    // finishing or while WA_DeleteOnClose widgets are queued for deletion.
    // Drain both before LeakSanitizer inspects process state.
    app.closeAllDocumentWindows();
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }

  delete workspaceOwner.data();
  return exitCode;
}
