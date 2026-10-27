#ifndef QTFFMPEGSTUDIO_MUXPERFORMANCE_H
#define QTFFMPEGSTUDIO_MUXPERFORMANCE_H

#include <QHash>
#include <QMetaType>
#include <QRegularExpression>
#include <QString>
#include <cmath>
#include <optional>

struct MuxPerformance
{
    double totalMs = 0;
    double paramsWaitMs = 0;
    double videoPopMs = 0;
    double audioPopMs = 0;
    double writeMs = 0;
    double trailerMs = 0;
    double sleepMs = 0;

    static std::optional<MuxPerformance> parseLine(const QString& line)
    {
        if (!line.contains(QStringLiteral("Mux 性能统计:"))) return std::nullopt;
        static const QRegularExpression pair(QStringLiteral("([a-z_]+)=([0-9]+(?:\\.[0-9]+)?)"));
        QHash<QString, double> values;
        auto it = pair.globalMatch(line);
        while (it.hasNext()) {
            const auto match = it.next();
            values.insert(match.captured(1), match.captured(2).toDouble());
        }
        for (const QString& key : {QStringLiteral("total_ms"),
                                   QStringLiteral("params_wait_ms"),
                                   QStringLiteral("video_pop_ms"),
                                   QStringLiteral("audio_pop_ms"),
                                   QStringLiteral("write_ms"),
                                   QStringLiteral("trailer_ms"),
                                   QStringLiteral("sleep_ms")}) {
            if (!values.contains(key) || !std::isfinite(values.value(key))) return std::nullopt;
        }
        if (values.value(QStringLiteral("total_ms")) <= 0) return std::nullopt;
        return MuxPerformance{values.value(QStringLiteral("total_ms")),
                              values.value(QStringLiteral("params_wait_ms")),
                              values.value(QStringLiteral("video_pop_ms")),
                              values.value(QStringLiteral("audio_pop_ms")),
                              values.value(QStringLiteral("write_ms")),
                              values.value(QStringLiteral("trailer_ms")),
                              values.value(QStringLiteral("sleep_ms"))};
    }
};

Q_DECLARE_METATYPE(MuxPerformance)

inline QString formatMuxPerformance(const MuxPerformance& value)
{
    const auto seconds = [](double milliseconds) {
        return QString::number(milliseconds / 1000.0, 'f', 3) + QStringLiteral(" 秒");
    };
    return QStringLiteral("Mux 总计 %1 · 等参数 %2 · 取视频包 %3 · 取音频包 %4\n固定休眠 %5 · 写包 %6 · 写文件尾 %7")
        .arg(seconds(value.totalMs), seconds(value.paramsWaitMs),
             seconds(value.videoPopMs), seconds(value.audioPopMs),
             seconds(value.sleepMs), seconds(value.writeMs), seconds(value.trailerMs));
}

#endif
