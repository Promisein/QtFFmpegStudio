#pragma once
#include <QObject>
#include <QCache>
#include <QImage>
#include <QQueue>
#include <QSet>
class QMediaPlayer;

// Session-only, bounded LRU cache. Only visible cards request covers.
class TaskThumbnailCache final : public QObject {
    Q_OBJECT
public:
    explicit TaskThumbnailCache(QObject* parent = nullptr);
    ~TaskThumbnailCache() override;
    QImage image(quint64 id, const QString& path);
    void clear();
signals:
    void ready(quint64 id);
private:
    void pump();
    void finish(const QImage& image);
    struct Request { quint64 id; QString path; };
    QCache<quint64,QImage> cache_{128};
    QQueue<Request> queue_;
    QSet<quint64> pending_;
    Request current_{};
    QObject* job_ = nullptr;
    QMediaPlayer* player_ = nullptr;
};
