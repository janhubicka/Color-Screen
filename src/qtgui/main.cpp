#include "MainWindow.h"
#include <QApplication>
#include <QCommandLineParser>

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
