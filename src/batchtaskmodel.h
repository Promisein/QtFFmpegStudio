#ifndef QTFFMPEGSTUDIO_BATCHTASKMODEL_H
#define QTFFMPEGSTUDIO_BATCHTASKMODEL_H

#include <QAbstractTableModel>
#include <QVector>
#include <QtGlobal>

#include "transcodeoptions.h"

struct BatchTask
{
    enum class Status { Pending, Running, Succeeded, Failed, Cancelled };
    TranscodeOptions options;
    quint64 id = 0;
    Status status = Status::Pending;
    int transcodeProgress = -1;
    qint64 elapsedMilliseconds = -1;
    QString detail;
};

class BatchTaskModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    static constexpr int ProgressValueRole = Qt::UserRole + 1;
    explicit BatchTaskModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    int addFiles(const QStringList& inputs, const QString& outputDirectory,
                 const TranscodeOptions& optionsTemplate);
    BatchTask task(int row) const;
    int rowForId(quint64 id) const;
    int nextPendingIndex() const;
    void setStatus(int row, BatchTask::Status status, const QString& detail = {},
                   qint64 elapsedMilliseconds = -1);
    void setTranscodeProgress(int row, int percent);
    void cancelPending();
    void clear();

private:
    QVector<BatchTask> tasks_;
    quint64 nextId_ = 1;
};

#endif
