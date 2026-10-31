#include "taskthumbnailcache.h"
#include <QMediaPlayer>
#include <QVideoSink>
#include <QVideoFrame>
#include <QTimer>
#include <QUrl>

TaskThumbnailCache::TaskThumbnailCache(QObject* parent) : QObject(parent) {}
TaskThumbnailCache::~TaskThumbnailCache() { clear(); }

QImage TaskThumbnailCache::image(quint64 id,const QString& path)
{
    if(auto* cached=cache_.object(id)) return *cached;
    if(!pending_.contains(id)) {
        pending_.insert(id); queue_.enqueue({id,path});
        // Defer opening media until after the current paint event.
        QTimer::singleShot(0,this,[this]{pump();});
    }
    return {};
}

void TaskThumbnailCache::pump()
{
    if(job_ || queue_.isEmpty()) return;
    current_=queue_.dequeue();
    auto* job=new QObject(this); job_=job;
    player_=new QMediaPlayer(job);
    auto* player=player_;
    auto* sink=new QVideoSink(job); player->setVideoSink(sink);
    auto* timeout=new QTimer(job); timeout->setSingleShot(true); timeout->setInterval(8000);
    connect(timeout,&QTimer::timeout,this,[this,job]{if(job_==job) finish({});});
    connect(player,&QMediaPlayer::errorOccurred,this,[this,job](QMediaPlayer::Error,const QString&){if(job_==job) finish({});});
    connect(player,&QMediaPlayer::mediaStatusChanged,this,[this,job,player](QMediaPlayer::MediaStatus status){
        if(job_!=job) return;
        if(status==QMediaPlayer::LoadedMedia) {
            if(!player->hasVideo()) finish({});
            else player->play(); // No audio output is attached: cover decoding is silent.
        }
    });
    connect(sink,&QVideoSink::videoFrameChanged,this,[this,job](const QVideoFrame& frame){
        if(job_!=job || !frame.isValid()) return;
        const QImage image=frame.toImage();
        if(!image.isNull()) finish(image.scaled(192,108,Qt::KeepAspectRatio,Qt::SmoothTransformation));
    });
    timeout->start(); player->setSource(QUrl::fromLocalFile(current_.path));
}

void TaskThumbnailCache::finish(const QImage& image)
{
    if(!job_) return;
    const quint64 id=current_.id;
    auto* job=job_; auto* player=player_;
    job_=nullptr; player_=nullptr; // Ignore any late signals from this extraction job.
    player->stop(); player->setSource({}); job->deleteLater();
    cache_.insert(id,new QImage(image)); // Empty images cache failures too.
    pending_.remove(id);
    emit ready(id);
    QTimer::singleShot(0,this,[this]{pump();});
}

void TaskThumbnailCache::clear()
{
    queue_.clear(); pending_.clear(); cache_.clear();
    auto* job=job_; auto* player=player_; job_=nullptr; player_=nullptr;
    if(player) {player->stop(); player->setSource({});}
    if(job) job->deleteLater();
}
