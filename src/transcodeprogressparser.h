#ifndef QTFFMPEGSTUDIO_TRANSCODEPROGRESSPARSER_H
#define QTFFMPEGSTUDIO_TRANSCODEPROGRESSPARSER_H

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <cmath>

// 只识别版本化的机器事件；人类日志不再参与进度计算。
class TranscodeProgressParser
{
public:
    QVector<int> append(const QString& decodedText, QString* visibleLog = nullptr)
    {
        pending_ += decodedText;
        QVector<int> changes;
        qsizetype newline = -1;
        while ((newline = pending_.indexOf(QLatin1Char('\n'))) >= 0) {
            QString line = pending_.left(newline);
            const QString rawLine = pending_.left(newline + 1);
            pending_.remove(0, newline + 1);
            if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
            if (!line.startsWith(prefix_)) {
                if (visibleLog) *visibleLog += rawLine;
                continue;
            }
            if (line.size() > maximumLineLength_) continue;
            const QJsonDocument document = QJsonDocument::fromJson(
                line.mid(prefix_.size()).toUtf8());
            if (!document.isObject()) continue;
            const QJsonObject event = document.object();
            if (event.value(QStringLiteral("version")).toInt() != 1
                || event.value(QStringLiteral("stage")).toString() != QStringLiteral("transcode")) {
                continue;
            }
            const QJsonValue value = event.value(QStringLiteral("percent"));
            if (!value.isDouble()) continue;
            const double number = value.toDouble();
            if (!std::isfinite(number) || number < 0 || number > 99
                || std::floor(number) != number) continue;
            const int progress = static_cast<int>(number);
            if (progress > lastProgress_) {
                lastProgress_ = progress;
                changes.append(progress);
            }
        }
        if (pending_.size() > maximumLineLength_) {
            if (visibleLog && !pending_.startsWith(prefix_)) *visibleLog += pending_;
            pending_.clear();
        }
        return changes;
    }

    QString takePendingLog()
    {
        QString tail;
        if (!pending_.startsWith(prefix_)) tail = pending_;
        pending_.clear();
        return tail;
    }

    void reset()
    {
        pending_.clear();
        lastProgress_ = -1;
    }

private:
    static constexpr qsizetype maximumLineLength_ = 4096;
    QString pending_;
    int lastProgress_ = -1;
    const QString prefix_{QStringLiteral("QFS_PROGRESS ")};
};

#endif
