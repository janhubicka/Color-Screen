#include "ColorScreenApplication.h"
#include "progress-info.h"

#include <QColor>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QIcon>
#include <QPalette>
#include <QSettings>
#include <QStyleFactory>
#include <QTimer>

#include <cstring>

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
