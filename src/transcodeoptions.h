#ifndef QTFFMPEGSTUDIO_TRANSCODEOPTIONS_H
#define QTFFMPEGSTUDIO_TRANSCODEOPTIONS_H

#include <QString>

// 保存单文件或批量任务的参数；批量任务添加时会复制一份快照。
struct TranscodeOptions
{
    QString inputPath;
    QString outputPath;
    int rotationDegrees = 0;
    double speedRatio = 1.0;
    QString videoCodec = QStringLiteral("mpeg4");
    QString audioCodec = QStringLiteral("ac3");
};

#endif
