#ifndef QTFFMPEGSTUDIO_MEDIAPROBE_H
#define QTFFMPEGSTUDIO_MEDIAPROBE_H

#include <QObject>
#include <QString>
#include <QByteArray>

class QProcess;

struct MediaInfo {
    QString container;
    double durationSeconds = -1.0;
    qint64 fileSizeBytes = -1;
    QString videoCodec;
    int width = 0;
    int height = 0;
    double frameRate = -1.0;
    QString audioCodec;
    int sampleRate = 0;
    int channels = 0;
    bool hasVideo = false;
    bool hasAudio = false;
    bool supported = false;
};

class MediaProbe : public QObject
{
    Q_OBJECT
public:
    explicit MediaProbe(QObject* parent = nullptr);
    void inspect(const QString& path);
    void clear();
    static bool parseJson(const QByteArray& json, MediaInfo* info, QString* error);

signals:
    void ready(const QString& path, const MediaInfo& info);
    void failed(const QString& path, const QString& error);

private:
    QProcess* current_ = nullptr;
    quint64 generation_ = 0;
};

#endif
