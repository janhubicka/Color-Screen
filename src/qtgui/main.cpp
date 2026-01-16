#include "MainWindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QIcon>
#include <QImageReader>

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("colorscreen-qt");
  QApplication::setApplicationVersion("1.1");

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

  // Debug: Print paths to verify
  qDebug() << "App Dir:" << appDir;
  qDebug() << "Icon Theme:" << QIcon::themeName();
  qDebug() << "Icon Search Paths:" << QIcon::themeSearchPaths();
  qDebug() << "Supported Image Formats:"
           << QImageReader::supportedImageFormats();

  for (const QString &path : paths) {
    QDir dir(path);
    if (dir.exists()) {
      qDebug() << "Search Path Exists:" << path;
      qDebug() << "Contents:"
               << dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
      if (dir.exists("Adwaita")) {
        qDebug() << "Adwaita found in" << path;
        QDir adwaitaDir(dir.filePath("Adwaita"));
        if (adwaitaDir.exists("index.theme")) {
          qDebug() << "index.theme found in Adwaita";

          qDebug() << "Searching for *rotate-left* in Adwaita ("
                   << adwaitaDir.absolutePath() << ")...";
          QDirIterator it(adwaitaDir.absolutePath(),
                          QStringList() << "*rotate-left*", QDir::Files,
                          QDirIterator::Subdirectories);
          while (it.hasNext()) {
            QString file = it.next();
            qDebug() << "Found file:" << file;
            QIcon directIcon(file);
            qDebug() << "  Direct load QIcon::isNull():" << directIcon.isNull();
            QPixmap pm(file);
            qDebug() << "  Direct load QPixmap::isNull():" << pm.isNull();
          }
        } else {
          qDebug() << "index.theme MISSING in Adwaita";
        }
      }
    } else {
      qDebug() << "Search Path DOES NOT EXIST:" << path;
    }
  }

  qDebug() << "Has 'object-rotate-left'?"
           << QIcon::hasThemeIcon("object-rotate-left");
  qDebug() << "Has 'edit-undo'?" << QIcon::hasThemeIcon("edit-undo");

#endif

  // Set dark mode palette
  QPalette darkPalette;
  darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
  darkPalette.setColor(QPalette::WindowText, Qt::white);
  darkPalette.setColor(QPalette::Base, QColor(25, 25, 25));
  darkPalette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
  darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
  darkPalette.setColor(QPalette::ToolTipText, Qt::white);
  darkPalette.setColor(QPalette::Text, Qt::white);
  darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
  darkPalette.setColor(QPalette::ButtonText, Qt::white);
  darkPalette.setColor(QPalette::BrightText, Qt::red);
  darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
  darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
  darkPalette.setColor(QPalette::HighlightedText, Qt::black);
  darkPalette.setColor(QPalette::Mid, QColor(90, 90, 90));
  darkPalette.setColor(QPalette::Disabled, QPalette::Text,
                       QColor(127, 127, 127));
  darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText,
                       QColor(127, 127, 127));
  app.setPalette(darkPalette);

  // Ensure menu bar text is white
  app.setStyleSheet("QMenuBar { color: white; } QMenuBar::item:selected { "
                    "background: #555; }");

  MainWindow window;
  window.show();

  // Handle positional arguments
  const QStringList args = parser.positionalArguments();
  if (!args.isEmpty()) {
    window.loadFile(args.first());
  }

  return app.exec();
}
