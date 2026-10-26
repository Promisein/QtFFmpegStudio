#include "backendprocess.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <QTemporaryFile>

BackendProcess::BackendProcess(QObject* parent)
    : QObject(parent), process_(new QProcess(this))
{
    connect(process_, &QProcess::readyReadStandardOutput, this, &BackendProcess::readOutput);
    connect(process_, &QProcess::readyReadStandardError, this, &BackendProcess::readError);
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            finishOnce(false, QStringLiteral("后端启动失败：") + process_->errorString());
        }
    });
    connect(process_, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        readOutput();
        readError();
        if (!active_) return;
        if (cancelled_) {
            finishOnce(false, QStringLiteral("转码已取消。"));
        } else if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            finishOnce(false, QStringLiteral("后端执行失败，退出码：%1").arg(exitCode));
        } else {
            const QFileInfo output(outputPath_);
            if (!output.isFile() || output.size() == 0) {
                finishOnce(false, QStringLiteral("后端正常退出，但输出文件不存在或为空。"));
            } else {
                finishOnce(true, QStringLiteral("转码完成：%1").arg(outputPath_));
            }
        }
    });
}

bool BackendProcess::running() const
{
    return active_;
}

void BackendProcess::start(const TranscodeOptions& options, bool overwrite)
{
    if (active_) return;

    outputPath_ = options.outputPath;
    cancelled_ = false;
    active_ = true;
    ++runGeneration_;
    stdoutDecoder_ = QStringDecoder(QStringDecoder::Utf8);
    stderrDecoder_ = QStringDecoder(QStringDecoder::Utf8);
    progressParser_.reset();

    const QString executable = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("FFmpegProjectBackend.exe"));
    if (!QFileInfo(executable).isFile()) {
        finishOnce(false, QStringLiteral("找不到后端程序：") + executable);
        return;
    }

    QTemporaryFile reservation(QFileInfo(outputPath_).dir()
        .filePath(QStringLiteral("QFS-XXXXXX.tmp")));
    if (!reservation.open()) {
        finishOnce(false, QStringLiteral("无法在输出目录预留临时文件：") + reservation.errorString());
        return;
    }
    temporaryOutputPath_ = reservation.fileName();
    reservation.setAutoRemove(false);
    reservation.close();

    QStringList arguments{
        QStringLiteral("-i"), options.inputPath,
        QStringLiteral("-o"), options.outputPath,
        QStringLiteral("-speed"), QString::number(options.speedRatio, 'f', 2),
        QStringLiteral("-vcodec"), options.videoCodec,
        QStringLiteral("-acodec"), options.audioCodec
    };
    if (options.rotationDegrees != 0) {
        arguments << QStringLiteral("-rotate") << QString::number(options.rotationDegrees);
    }
    if (overwrite) arguments << QStringLiteral("--overwrite");
    arguments << QStringLiteral("--temp-output") << temporaryOutputPath_;

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"),
                       QCoreApplication::applicationDirPath() + QDir::listSeparator()
                       + environment.value(QStringLiteral("PATH")));
    process_->setProcessEnvironment(environment);
    process_->setProgram(executable);
    process_->setArguments(arguments);
    process_->start();
}

void BackendProcess::cancel()
{
    if (!active_) return;
    cancelled_ = true;
    if (process_->state() == QProcess::NotRunning) return;
#ifdef Q_OS_WIN
    // 控制台后端不处理 WM_CLOSE；terminate() 在此不会使它退出。
    process_->kill();
#else
    const quint64 generation = runGeneration_;
    process_->terminate();
    QTimer::singleShot(3000, this, [this, generation]() {
        if (active_ && generation == runGeneration_
            && process_->state() != QProcess::NotRunning) process_->kill();
    });
#endif
}

void BackendProcess::readOutput()
{
    const QByteArray data = process_->readAllStandardOutput();
    if (data.isEmpty()) return;
    const QString decoded = stdoutDecoder_(data);
    if (decoded.isEmpty()) return;
    QString visibleLog;
    const auto changes = progressParser_.append(decoded, &visibleLog);
    if (!visibleLog.isEmpty()) emit logReceived(visibleLog);
    if (active_ && !cancelled_) {
        for (const int percent : changes) {
            emit transcodeProgressChanged(percent);
        }
    }
}

void BackendProcess::readError()
{
    const QByteArray data = process_->readAllStandardError();
    if (!data.isEmpty()) emit logReceived(stderrDecoder_(data));
}

void BackendProcess::finishOnce(bool success, const QString& message)
{
    if (!active_) return;
    active_ = false;
    const QString tail = progressParser_.takePendingLog();
    if (!tail.isEmpty()) emit logReceived(tail);
    QString resultMessage = message;
    if (!temporaryOutputPath_.isEmpty()) {
        const QString path = temporaryOutputPath_;
        temporaryOutputPath_.clear();
        if (QFileInfo::exists(path) && !QFile::remove(path)) {
            resultMessage += QStringLiteral("；无法清理本次临时文件：") + path;
        }
    }
    emit finished(success, resultMessage);
}
