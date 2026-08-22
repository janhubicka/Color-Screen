#include "ColorScreenApplication.h"
#include "MainWindow.h"
#include "ImageViewWindow.h"
#include "WorkspaceWindow.h"
#include "progress-info.h"

#include <QAction>
#include <QColor>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
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
      "Close all documents and require a fresh reusable empty tab");
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
      "Require the active document to use the workspace status bar");
  parser.addOption(globalStatusBarOption);

  QCommandLineOption userVisibleProgressOption(
      "smoke-test-user-visible-progress",
      "Exercise dedicated Cancel/Stop rows for long-running tasks");
  parser.addOption(userVisibleProgressOption);

  QCommandLineOption newViewOption(
      "smoke-test-new-view",
      "Create a secondary view and verify shared image and independent render mode");
  parser.addOption(newViewOption);

  QCommandLineOption slantedReferenceOption(
      "smoke-test-slanted-reference",
      "Open the current image as a separate slanted-edge reference view");
  parser.addOption(slantedReferenceOption);

  QCommandLineOption timeReportOption(
      "time-report", "Enable internal time reporting of tasks");
  parser.addOption(timeReportOption);

  parser.addPositionalArgument("image", "Image file(s) to open.", "[image...]");
  parser.process(app);

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
    app.openFiles(images);
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
      QTimer::singleShot(0, &app, [&app]() {
        QTimer::singleShot(0, &app, [&app]() {
          const QList<MainWindow *> remaining = app.documentWindows();
          if (remaining.size() != 1 || app.tabCount() != 1 ||
              !remaining.front()->canReuseForOpen()) {
            qCritical() << "Smoke test did not leave one reusable empty tab";
            app.exit(5);
          }
        });
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
      QTimer::singleShot(100, &app, [mdiArea, first, second, firstPosition,
                                     secondPosition, target, &app]() {
        if (mdiArea->activeSubWindow() != target ||
            first->pos() != firstPosition || second->pos() != secondPosition) {
          qCritical() << "Activating a tiled document moved or swapped tiles";
          app.exit(11);
        }
      });
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
          document->statusBar()->isVisible()) {
        qCritical() << "Active document is not using the global status bar";
        app.exit(11);
        return;
      }

      const QString marker = QStringLiteral("workspace-status-smoke");
      document->statusBar()->showMessage(marker);
      QCoreApplication::processEvents();
      if (workspaceStatus->currentMessage() != marker) {
        qCritical() << "Document status message was not mirrored globally";
        app.exit(11);
      }
      document->statusBar()->clearMessage();
    });
  }

  if (parser.isSet(userVisibleProgressOption)) {
    QTimer::singleShot(250, &app, [&app]() {
      WorkspaceWindow *workspace = app.workspaceWindow();
      const QList<MainWindow *> documents = app.documentWindows();
      if (!workspace || documents.size() < 2) {
        qCritical()
            << "User-visible progress smoke test requires two documents";
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

      // A short-lived transient task may occupy the bottom status line but must
      // not reparent dedicated rows or alter the height of that line.
      auto transientProgress = std::make_shared<colorscreen::progress_info>();
      transientProgress->set_task("transient smoke task", 100);
      transientProgress->set_progress(10);
      cancelDocument->addProgress(transientProgress);
      QCoreApplication::processEvents();
      if (workspaceStatus->height() != statusHeight ||
          !taskStack->isAncestorOf(cancelContainer) ||
          !taskStack->isAncestorOf(stopContainer)) {
        qCritical() << "Transient progress disturbed the dedicated task strip";
        app.exit(13);
        return;
      }
      cancelDocument->removeProgress(transientProgress);
      QCoreApplication::processEvents();
      if (workspaceStatus->height() != statusHeight) {
        qCritical() << "Transient progress changed status-bar height on exit";
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

      stopButton->click();
      cancelButton->click();
      if (!stopProgress->pool_cancel() || !cancelProgress->pool_cancel()) {
        qCritical() << "Dedicated progress actions did not request termination";
        app.exit(13);
        return;
      }

      stopDocument->removeProgress(stopProgress);
      cancelDocument->removeProgress(cancelProgress);
    });
  }

  if (parser.isSet(newViewOption)) {
    QTimer::singleShot(300, &app, [&app]() {
      const QList<MainWindow *> documents = app.documentWindows();
      if (documents.isEmpty() || !documents.front()->sharedImageData()) {
        qCritical() << "New View smoke test requires a loaded document";
        app.exit(14);
        return;
      }

      MainWindow *source = documents.front();
      const auto sourceScan = source->sharedImageData();
      const auto sourceType = source->viewRenderTypeParameters().type;
      const int previousTabs = app.tabCount();
      ImageViewWindow *view = app.createViewWindow(source);
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

      bool hasIconOnlyZoom = false;
      bool hasIconOnlyRotation = false;
      if (QToolBar *toolbar = view->workspaceToolBar()) {
        for (QAction *action : toolbar->actions()) {
          if (action->text() == QObject::tr("Zoom In") && !action->icon().isNull())
            hasIconOnlyZoom = true;
          if (action->text() == QObject::tr("Rotate Right") && !action->icon().isNull())
            hasIconOnlyRotation = true;
        }
      }
      if (!hasIconOnlyZoom || !hasIconOnlyRotation) {
        qCritical() << "New View toolbar does not use the standard image-view icons";
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

      app.closeView(view);
    });
  }

  if (parser.isSet(slantedReferenceOption)) {
    // When both view smoke tests are requested, allow WA_DeleteOnClose from
    // New View to complete before constructing the reference view.  This keeps
    // the tests deterministic and also exercises the normal view-close path.
    const int referenceSmokeDelay = parser.isSet(newViewOption) ? 700 : 350;
    QTimer::singleShot(referenceSmokeDelay, &app, [&app]() {
      const QList<MainWindow *> documents = app.documentWindows();
      if (documents.isEmpty() || documents.front()->currentImageFile().isEmpty()) {
        qCritical() << "Slanted reference smoke test requires a loaded image";
        app.exit(15);
        return;
      }

      MainWindow *source = documents.front();
      const int documentCount = documents.size();
      const int tabCount = app.tabCount();
      ImageViewWindow *reference = app.createSlantedEdgeReference(
          source, source->currentImageFile());
      if (!reference) {
        qCritical() << "Could not create slanted-edge reference view";
        app.exit(15);
        return;
      }

      auto checkReference =
          std::make_shared<std::function<void(int)>>();
      *checkReference = [&app, source, reference, documentCount, tabCount,
                         checkReference](int attemptsLeft) {
        if (reference && !reference->sharedImageData() && attemptsLeft > 0) {
          QTimer::singleShot(250, &app, [checkReference, attemptsLeft]() {
            (*checkReference)(attemptsLeft - 1);
          });
          return;
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
            reference->setRenderType(colorscreen::render_type_interpolated)) {
          qCritical() << "Slanted-edge reference exposes unexpected render modes";
          app.exit(15);
          return;
        }

        const auto beforeReload = reference->sharedImageData();
        app.reloadSlantedEdgeReferences(source);
        auto checkReload = std::make_shared<std::function<void(int)>>();
        *checkReload = [&app, reference, beforeReload, checkReload](
                           int attemptsLeft) {
          if (reference && reference->sharedImageData() == beforeReload &&
              attemptsLeft > 0) {
            QTimer::singleShot(250, &app, [checkReload, attemptsLeft]() {
              (*checkReload)(attemptsLeft - 1);
            });
            return;
          }
          if (!reference || reference->sharedImageData() == beforeReload) {
            qCritical() << "Slanted-edge reference did not reload";
            app.exit(15);
            return;
          }
          app.closeView(reference);
        };
        (*checkReload)(28);
      };
      (*checkReference)(28);
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
      if (!app.documentWindows().isEmpty())
        app.quit();
    });
  }

  return app.exec();
}
