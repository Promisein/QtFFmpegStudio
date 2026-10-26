#include "mediaprobe.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QTimer>
#include <cmath>
#include <memory>

namespace {
double positiveNumber(const QJsonValue& value)
{
    bool ok = false;
    const double number = value.isString() ? value.toString().toDouble(&ok) : value.toDouble(-1);
    if (!value.isString()) ok = value.isDouble();
    return ok && std::isfinite(number) && number > 0 ? number : -1.0;
}

double frameRate(const QJsonValue& value)
{
    const QStringList parts = value.toString().split('/');
    if (parts.size() != 2) return -1.0;
    bool numeratorOk = false, denominatorOk = false;
    const double numerator = parts[0].toDouble(&numeratorOk);
    const double denominator = parts[1].toDouble(&denominatorOk);
    return numeratorOk && denominatorOk && denominator > 0 && numerator > 0
        ? numerator / denominator : -1.0;
}
}

MediaProbe::MediaProbe(QObject* parent) : QObject(parent) {}

bool MediaProbe::parseJson(const QByteArray& json, MediaInfo* info, QString* error)
{
    if (!info) return false;
    *info = {};
    if (error) error->clear();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("媒体信息格式无效：%1").arg(parseError.errorString());
        return false;
    }
    const QJsonObject root = document.object();
    const QJsonArray streams = root.value(QStringLiteral("streams")).toArray();
    if (streams.isEmpty()) {
        if (error) *error = QStringLiteral("未找到可读取的媒体流。");
        return false;
    }
    const QJsonObject format = root.value(QStringLiteral("format")).toObject();
    const QString formatName = format.value(QStringLiteral("format_name")).toString();
    info->container = formatName.contains(QStringLiteral("mp4"), Qt::CaseInsensitive)
        ? QStringLiteral("MP4") : formatName.section(',', 0, 0).toUpper();
    info->durationSeconds = positiveNumber(format.value(QStringLiteral("duration")));
    for (const QJsonValue& value : streams) {
        const QJsonObject stream = value.toObject();
        const QString type = stream.value(QStringLiteral("codec_type")).toString();
        if (type == QStringLiteral("video")) {
            info->hasVideo = true;
            info->videoCodec = stream.value(QStringLiteral("codec_name")).toString();
            info->width = stream.value(QStringLiteral("width")).toInt();
            info->height = stream.value(QStringLiteral("height")).toInt();
            info->frameRate = frameRate(stream.value(QStringLiteral("avg_frame_rate")));
        } else if (type == QStringLiteral("audio")) {
            info->hasAudio = true;
            info->audioCodec = stream.value(QStringLiteral("codec_name")).toString();
            info->sampleRate = stream.value(QStringLiteral("sample_rate")).toString().toInt();
            info->channels = stream.value(QStringLiteral("channels")).toInt();
        }
    }
    info->supported = info->hasVideo && info->hasAudio;
    return true;
}

void MediaProbe::clear()
{
    ++generation_;
    if (current_) {
        current_->kill();
        current_ = nullptr;
    }
}

void MediaProbe::inspect(const QString& path)
{
    clear();
    if (path.isEmpty()) return;
    const quint64 request = generation_;
    const QFileInfo file(path);
    if (!file.isFile() || !file.isReadable()) {
        emit failed(path, QStringLiteral("文件不存在或不可读取。"));
        return;
    }
    if (file.suffix().compare(QStringLiteral("mp4"), Qt::CaseInsensitive) != 0) {
        emit failed(path, QStringLiteral("目前仅支持 MP4 文件。"));
        return;
    }
    const QString executable = QCoreApplication::applicationDirPath() + QStringLiteral("/ffprobe.exe");
    if (!QFileInfo::exists(executable)) {
        emit failed(path, QStringLiteral("找不到 ffprobe.exe，请重新构建或检查安装目录。"));
        return;
    }
    auto* process = new QProcess(this);
    current_ = process;
    auto output = std::make_shared<QByteArray>();
    auto finished = std::make_shared<bool>(false);
    auto* timer = new QTimer(process);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, process, [this, process, path, request, finished]() {
        if (*finished) return;
        *finished = true;
        if (request == generation_) emit failed(path, QStringLiteral("分析超时，请重试。"));
        process->kill();
    });
    connect(process, &QProcess::readyReadStandardOutput, process,
            [this, process, output, path, request, finished]() {
        output->append(process->readAllStandardOutput());
        if (output->size() > 1024 * 1024 && !*finished) {
            *finished = true;
            if (request == generation_) emit failed(path, QStringLiteral("媒体信息过大，已停止分析。"));
            process->kill();
        }
    });
    auto errorOutputSize = std::make_shared<int>(0);
    connect(process, &QProcess::readyReadStandardError, process,
            [this, process, path, request, finished, errorOutputSize]() {
        *errorOutputSize += process->readAllStandardError().size();
        if (*errorOutputSize > 64 * 1024 && !*finished) {
            *finished = true;
            if (request == generation_) emit failed(path, QStringLiteral("媒体错误信息过多，已停止分析。"));
            process->kill();
        }
    });
    connect(process, &QProcess::errorOccurred, process,
            [this, process, path, request, finished](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || *finished) return;
        *finished = true;
        if (request == generation_) emit failed(path, QStringLiteral("无法启动 ffprobe：%1").arg(process->errorString()));
        if (current_ == process) current_ = nullptr;
        process->deleteLater();
    });
    connect(process, &QProcess::finished, process,
            [this, process, timer, output, finished, request, path, file](int code, QProcess::ExitStatus status) {
        timer->stop();
        if (current_ == process) current_ = nullptr;
        if (request == generation_ && !*finished) {
            output->append(process->readAllStandardOutput());
            MediaInfo info;
            QString error;
            if (code != 0 || status != QProcess::NormalExit) {
                emit failed(path, QStringLiteral("无法读取视频信息，请检查文件是否损坏。"));
            } else if (!parseJson(*output, &info, &error)) {
                emit failed(path, error);
            } else {
                info.fileSizeBytes = file.size();
                emit ready(path, info);
            }
        }
        *finished = true;
        process->deleteLater();
    });
    process->start(executable, {QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-show_entries"),
        QStringLiteral("format=format_name,duration:stream=index,codec_type,codec_name,width,height,avg_frame_rate,sample_rate,channels"),
        QStringLiteral("-of"), QStringLiteral("json"), path});
    timer->start(10000);
}
