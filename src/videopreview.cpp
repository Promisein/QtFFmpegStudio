#include "videopreview.h"
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QPainter>
#include <QPainterPath>
#include <QImage>
#include <QVideoSink>
#include <QVideoFrame>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QFileInfo>
#include <QUrl>
#include <QHideEvent>

// Paint decoded frames in the regular widget hierarchy, including screenshots.
class PreviewSurface final : public QWidget {
public:
    using QWidget::QWidget;
    void setImage(const QImage& image) { image_=image; update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath clip; clip.addRoundedRect(QRectF(rect()),10,10); painter.setClipPath(clip);
        painter.fillRect(rect(),QColor("#101827"));
        if(image_.isNull()) {
            painter.setPen(QColor("#A8B5C8")); painter.drawText(rect(),Qt::AlignCenter,QStringLiteral("视频预览")); return;
        }
        const QSize size=image_.size().scaled(this->size(),Qt::KeepAspectRatio);
        const QRect destination(QPoint((width()-size.width())/2,(height()-size.height())/2),size);
        painter.setRenderHint(QPainter::SmoothPixmapTransform); painter.drawImage(destination,image_);
    }
private:
    QImage image_;
};

namespace {
QString clockText(qint64 milliseconds)
{
    const qint64 seconds=qMax<qint64>(0,milliseconds)/1000;
    const QString minutes=QStringLiteral("%1:%2").arg(seconds/60,2,10,QChar('0')).arg(seconds%60,2,10,QChar('0'));
    return minutes;
}
}

VideoPreview::VideoPreview(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("videoPreview"));
    auto* layout=new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0); layout->setSpacing(8);
    player_=new QMediaPlayer(this); player_->setObjectName("previewPlayer");
    audio_=new QAudioOutput(this); audio_->setMuted(true); player_->setAudioOutput(audio_);
    video_=new PreviewSurface(this); video_->setObjectName("previewVideoSurface");
    video_->setMinimumHeight(140); video_->setMaximumHeight(220);
    video_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    video_->setStyleSheet(QStringLiteral("background: #101827; border-radius: 10px;"));
    sink_=new QVideoSink(this); player_->setVideoSink(sink_); layout->addWidget(video_);
    status_=new QLabel(QStringLiteral("选择视频后显示预览"),this); status_->setObjectName("previewStatus");
    status_->setWordWrap(true); status_->setTextFormat(Qt::PlainText); status_->setProperty("muted",true); layout->addWidget(status_);
    auto* controls=new QHBoxLayout; controls->setSpacing(6);
    play_=new QPushButton(QStringLiteral("播放"),this); play_->setObjectName("previewPlayButton");
    play_->setToolTip(QStringLiteral("播放或暂停输入视频预览")); controls->addWidget(play_);
    seek_=new QSlider(Qt::Horizontal,this); seek_->setObjectName("previewSeekSlider"); seek_->setRange(0,1000);
    seek_->setToolTip(QStringLiteral("拖动定位预览位置")); seek_->setAccessibleName(QStringLiteral("预览进度")); controls->addWidget(seek_,1);
    mute_=new QPushButton(QStringLiteral("静音"),this); mute_->setCheckable(true); mute_->setChecked(true); mute_->setObjectName("previewMuteButton");
    mute_->setToolTip(QStringLiteral("切换预览声音，默认静音")); controls->addWidget(mute_); layout->addLayout(controls);
    time_=new QLabel(QStringLiteral("00:00 / 00:00"),this); time_->setProperty("muted",true); time_->setObjectName("previewTime"); layout->addWidget(time_);
    loadTimeout_=new QTimer(this); loadTimeout_->setSingleShot(true); loadTimeout_->setInterval(15000);
    connect(loadTimeout_,&QTimer::timeout,this,[this]{fail(QStringLiteral("加载超时，请重新选择文件"));});
    connect(play_,&QPushButton::clicked,this,[this]{
        if(!ready_) return;
        if(player_->playbackState()==QMediaPlayer::PlayingState) player_->pause();
        else { if(player_->mediaStatus()==QMediaPlayer::EndOfMedia) player_->setPosition(0); player_->play(); }
    });
    connect(mute_,&QPushButton::toggled,this,[this](bool muted){audio_->setMuted(muted); mute_->setText(muted?QStringLiteral("静音"):QStringLiteral("声音"));});
    connect(seek_,&QSlider::sliderReleased,this,[this]{if(ready_ && player_->isSeekable()) player_->setPosition(player_->duration()*seek_->value()/1000);});
    connect(player_,&QMediaPlayer::positionChanged,this,[this](qint64 position){
        if(!seek_->isSliderDown()) seek_->setValue(player_->duration()>0?int(position*1000/player_->duration()):0);
        updateTime();
    });
    connect(player_,&QMediaPlayer::durationChanged,this,[this]{updateTime(); updateControls();});
    connect(player_,&QMediaPlayer::seekableChanged,this,[this]{updateControls();});
    connect(player_,&QMediaPlayer::playbackStateChanged,this,[this]{updateControls();});
    connect(player_,&QMediaPlayer::mediaStatusChanged,this,[this](QMediaPlayer::MediaStatus status){
        // Decode just one frame silently so selecting a file gives a real poster.
        if(status==QMediaPlayer::LoadedMedia && priming_) player_->play();
    });
    connect(sink_,&QVideoSink::videoFrameChanged,this,[this](const QVideoFrame& frame){
        const QImage image=frame.toImage();
        if(image.isNull()) return;
        video_->setImage(image);
        if(!priming_) return;
        priming_=false; ready_=true; loadTimeout_->stop(); player_->pause();
        status_->setText(QStringLiteral("输入视频预览")); updateControls();
    });
    connect(player_,&QMediaPlayer::errorOccurred,this,[this](QMediaPlayer::Error,const QString& error){fail(error);});
    updateControls();
}

void VideoPreview::setFile(const QString& path)
{
    loadTimeout_->stop(); priming_=false; ready_=false;
    player_->stop(); player_->setSource(QUrl());
    video_->setImage(QImage()); seek_->setValue(0); updateTime();
    audio_->setMuted(true); mute_->setChecked(true);
    if(path.isEmpty()) { status_->setText(QStringLiteral("选择视频后显示预览")); updateControls(); return; }
    if(!QFileInfo(path).isFile()) {fail(QStringLiteral("文件不存在")); return;}
    priming_=true; status_->setText(QStringLiteral("正在加载预览…")); updateControls(); loadTimeout_->start();
    player_->setSource(QUrl::fromLocalFile(path));
}

void VideoPreview::pause()
{
    // Poster decoding is already silent and stops on its first frame.
    if(!priming_) player_->pause();
}
void VideoPreview::hideEvent(QHideEvent* event)
{
    pause(); QWidget::hideEvent(event);
}
void VideoPreview::updateControls()
{
    play_->setEnabled(ready_); mute_->setEnabled(ready_);
    seek_->setEnabled(ready_ && player_->isSeekable());
    play_->setText(ready_ && player_->playbackState()==QMediaPlayer::PlayingState?QStringLiteral("暂停"):QStringLiteral("播放"));
}
void VideoPreview::updateTime()
{
    time_->setText(clockText(player_->position())+QStringLiteral(" / ")+clockText(player_->duration()));
}
void VideoPreview::fail(const QString& reason)
{
    loadTimeout_->stop(); priming_=false; ready_=false; player_->stop();
    status_->setText(QStringLiteral("无法预览：%1").arg(reason)); updateControls();
}
