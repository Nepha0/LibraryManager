#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName("LibraryTeam");
    app.setApplicationName("LibraryManager");
    MainWindow window;
    window.show();
    return app.exec();
}
