#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("ColorScreen Qt");
    QApplication::setApplicationVersion("1.0");

    MainWindow window;
    window.show();

    return app.exec();
}
