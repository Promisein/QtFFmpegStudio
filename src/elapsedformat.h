#ifndef QTFFMPEGSTUDIO_ELAPSEDFORMAT_H
#define QTFFMPEGSTUDIO_ELAPSEDFORMAT_H

#include <QString>
#include <QtGlobal>

inline QString formatElapsed(qint64 milliseconds)
{
    return QStringLiteral("%1 秒").arg(milliseconds / 1000.0, 0, 'f', 1);
}

#endif
