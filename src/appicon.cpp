#include "appicon.h"
#include <QResource>

QIcon studioApplicationIcon()
{
    static const QIcon icon=[] {
        // Explicit reference keeps RCC data linked from the static GUI library,
        // including in tests and other executables that create MainWindow directly.
        Q_INIT_RESOURCE(studio);
        QIcon result;
        for(int size:{16,20,24,32,40,48,64,128,256})
            result.addFile(QStringLiteral(":/icons/studio/%1.png").arg(size),QSize(size,size));
        return result;
    }();
    return icon;
}
