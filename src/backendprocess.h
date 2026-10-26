#ifndef QTFFMPEGSTUDIO_BACKENDPROCESS_H
#define QTFFMPEGSTUDIO_BACKENDPROCESS_H

#include <QObject>
#include <QStringDecoder>
#include <QtGlobal>

#include "transcodeoptions.h"
#include "transcodeprogressparser.h"

class QProcess;

class BackendProcess : public QObject
{
    Q_OBJECT

public:
    explicit BackendProcess(QObject* parent = nullptr);
    bool running() const;
    void start(const TranscodeOptions& options, bool overwrite);
    void cancel();

signals:
    void logReceived(const QString& text);
    void transcodeProgressChanged(int percent);
    void finished(bool success, const QString& message);

private:
    void readOutput();
    void readError();
    void finishOnce(bool success, const QString& message);

    QProcess* process_ = nullptr;
    QString outputPath_;
    QString temporaryOutputPath_;
    QStringDecoder stdoutDecoder_{QStringDecoder::Utf8};
    QStringDecoder stderrDecoder_{QStringDecoder::Utf8};
    TranscodeProgressParser progressParser_;
    bool active_ = false;
    bool cancelled_ = false;
    quint64 runGeneration_ = 0;
};

#endif
