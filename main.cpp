#include <QApplication>

#include "mainwindow.h"
#include "appicon.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("QtFFmpegStudio"));
    app.setApplicationDisplayName(QStringLiteral("QtFFmpegStudio"));
    app.setWindowIcon(studioApplicationIcon());

    MainWindow window;
    window.show();
    return app.exec();
}
