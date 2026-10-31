#pragma once
#include <QWidget>
class QMediaPlayer;
class QAudioOutput;
class PreviewSurface;
class QVideoSink;
class QPushButton;
class QSlider;
class QLabel;
class QTimer;

// Input-only preview. It owns no transcode state and never opens the output file.
class VideoPreview final : public QWidget {
    Q_OBJECT
public:
    explicit VideoPreview(QWidget* parent = nullptr);
    void setFile(const QString& path);
    void pause();
protected:
    void hideEvent(QHideEvent* event) override;
private:
    void updateControls();
    void updateTime();
    void fail(const QString& reason);
    QMediaPlayer* player_;
    QAudioOutput* audio_;
    PreviewSurface* video_;
    QVideoSink* sink_;
    QPushButton* play_;
    QPushButton* mute_;
    QSlider* seek_;
    QLabel* time_;
    QLabel* status_;
    QTimer* loadTimeout_;
    bool priming_ = false;
    bool ready_ = false;
};
