#include "MainWindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QIcon>
#include <QImageReader>

#include <QStyleFactory>

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("colorscreen-qt");
  QApplication::setApplicationVersion("1.1");
  QApplication::setStyle(QStyleFactory::create("Fusion"));

  QCommandLineParser parser;
  parser.setApplicationDescription("ColorScreen Qt GUI");
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addPositionalArgument("image", "Image file to open.");
  parser.process(app);

  // Set icon search paths and theme
#ifdef Q_OS_WIN
  QStringList paths = QIcon::themeSearchPaths();
  QString appDir = QCoreApplication::applicationDirPath();
  // Add ../share/icons relative to bin/
  paths.prepend(appDir + "/../share/icons");
  // Also try local share/icons for robustness
  paths.prepend(appDir + "/share/icons");
  QIcon::setThemeSearchPaths(paths);

  // Use Adwaita
  QIcon::setThemeName("Adwaita");

#endif

  // Set dark mode palette
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
  // Fusion uses Mid/Dark/Light for 3D bevels
  darkPalette.setColor(QPalette::Mid, QColor(45, 45, 45)); 
  darkPalette.setColor(QPalette::Dark, QColor(35, 35, 35));
  darkPalette.setColor(QPalette::Light, QColor(65, 65, 65));

  // Robust disabled colors
  darkPalette.setColor(QPalette::Disabled, QPalette::Text, QColor(127, 127, 127));
  darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(127, 127, 127));
  darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(127, 127, 127));
  darkPalette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(80, 80, 80));
  darkPalette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(127, 127, 127));
  
  app.setPalette(darkPalette);

  // Fusion style handles menu text color via Palette (WindowText), so manual stylesheet is removed.

  MainWindow window;
  window.show();

  // Handle positional arguments
  const QStringList args = parser.positionalArguments();
  if (!args.isEmpty()) {
    window.loadFile(args.first());
  }

  return app.exec();
}
