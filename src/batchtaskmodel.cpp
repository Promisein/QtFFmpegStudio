#include "batchtaskmodel.h"
#include "elapsedformat.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

namespace {

QString pathKey(const QString& path)
{
    QString key = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}

QString statusText(BatchTask::Status status)
{
    switch (status) {
    case BatchTask::Status::Pending: return QStringLiteral("等待");
    case BatchTask::Status::Running: return QStringLiteral("运行中");
    case BatchTask::Status::Succeeded: return QStringLiteral("成功");
    case BatchTask::Status::Failed: return QStringLiteral("失败");
    case BatchTask::Status::Cancelled: return QStringLiteral("已取消");
    }
    return {};
}

QString resultText(const BatchTask& task)
{
    if (task.elapsedMilliseconds < 0) return task.detail;
    const QString elapsed = QStringLiteral("耗时 %1").arg(formatElapsed(task.elapsedMilliseconds));
    return task.detail.isEmpty() ? elapsed : elapsed + QStringLiteral("｜") + task.detail;
}

} // namespace

BatchTaskModel::BatchTaskModel(QObject* parent) : QAbstractTableModel(parent) {}

int BatchTaskModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : tasks_.size();
}

int BatchTaskModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : 5;
}

QVariant BatchTaskModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= tasks_.size()) return {};
    const BatchTask& item = tasks_.at(index.row());
    if (role == Qt::ToolTipRole) {
        return QStringLiteral("输入：%1\n输出：%2\n%3")
            .arg(item.options.inputPath, item.options.outputPath, resultText(item));
    }
    if (role == ProgressValueRole) {
        if (index.column() == 3 && item.transcodeProgress >= 0
            && (item.status == BatchTask::Status::Running
                || item.status == BatchTask::Status::Succeeded)) {
            return item.transcodeProgress;
        }
        return {};
    }
    if (role != Qt::DisplayRole) return {};
    switch (index.column()) {
    case 0: return QFileInfo(item.options.inputPath).fileName();
    case 1: return QFileInfo(item.options.outputPath).fileName();
    case 2: return statusText(item.status);
    case 3:
        if (item.status == BatchTask::Status::Pending
            || (item.status == BatchTask::Status::Cancelled && item.transcodeProgress < 0)) {
            return QStringLiteral("—");
        }
        if (item.transcodeProgress < 0) return QStringLiteral("估算中");
        return QStringLiteral("%1%").arg(item.transcodeProgress);
    case 4: return resultText(item);
    default: return {};
    }
}

QVariant BatchTaskModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case 0: return QStringLiteral("输入");
    case 1: return QStringLiteral("输出");
    case 2: return QStringLiteral("状态");
    case 3: return QStringLiteral("转码进度");
    case 4: return QStringLiteral("结果");
    default: return {};
    }
}

int BatchTaskModel::addFiles(const QStringList& inputs, const QString& outputDirectory,
                             const TranscodeOptions& optionsTemplate)
{
    const QFileInfo directoryInfo(outputDirectory);
    if (!directoryInfo.isDir() || !directoryInfo.isWritable()) return 0;

    QSet<QString> usedInputs;
    QSet<QString> usedOutputs;
    for (const BatchTask& task : tasks_) {
        const QString canonical = QFileInfo(task.options.inputPath).canonicalFilePath();
        usedInputs.insert(pathKey(canonical.isEmpty() ? task.options.inputPath : canonical));
        usedOutputs.insert(pathKey(task.options.outputPath));
    }
    const QDir directory(directoryInfo.absoluteFilePath());
    int added = 0;
    for (const QString& inputPath : inputs) {
        const QFileInfo input(inputPath);
        if (!input.isFile() || !input.isReadable()
            || input.suffix().compare(QStringLiteral("mp4"), Qt::CaseInsensitive) != 0) continue;
        const QString inputKey = pathKey(input.canonicalFilePath());
        if (usedInputs.contains(inputKey)) continue;

        const QString stem = input.completeBaseName() + QStringLiteral("_batch");
        QString output = directory.filePath(stem + QStringLiteral(".mp4"));
        int suffix = 2;
        while (QFileInfo::exists(output) || usedOutputs.contains(pathKey(output))
               || pathKey(output) == inputKey) {
            output = directory.filePath(stem + QStringLiteral("_%1.mp4").arg(suffix++));
        }
        BatchTask task;
        task.id = nextId_++;
        task.options = optionsTemplate;
        task.options.inputPath = input.absoluteFilePath();
        task.options.outputPath = output;
        const int row = tasks_.size();
        beginInsertRows({}, row, row);
        tasks_.append(task);
        endInsertRows();
        usedInputs.insert(inputKey);
        usedOutputs.insert(pathKey(output));
        ++added;
    }
    return added;
}

BatchTask BatchTaskModel::task(int row) const
{
    return row >= 0 && row < tasks_.size() ? tasks_.at(row) : BatchTask{};
}

int BatchTaskModel::rowForId(quint64 id) const
{
    for (int row = 0; row < tasks_.size(); ++row) {
        if (tasks_.at(row).id == id) return row;
    }
    return -1;
}

int BatchTaskModel::nextPendingIndex() const
{
    for (int row = 0; row < tasks_.size(); ++row) {
        if (tasks_.at(row).status == BatchTask::Status::Pending) return row;
    }
    return -1;
}

void BatchTaskModel::setStatus(int row, BatchTask::Status status, const QString& detail,
                               qint64 elapsedMilliseconds)
{
    if (row < 0 || row >= tasks_.size()) return;
    tasks_[row].status = status;
    tasks_[row].detail = detail;
    tasks_[row].elapsedMilliseconds = status == BatchTask::Status::Running
        ? -1 : elapsedMilliseconds;
    if (status == BatchTask::Status::Running) tasks_[row].transcodeProgress = -1;
    if (status == BatchTask::Status::Succeeded) tasks_[row].transcodeProgress = 100;
    emit dataChanged(index(row, 2), index(row, 4),
                     {Qt::DisplayRole, Qt::ToolTipRole, ProgressValueRole});
}

void BatchTaskModel::setTranscodeProgress(int row, int percent)
{
    if (row < 0 || row >= tasks_.size() || percent < 0 || percent > 99) return;
    BatchTask& task = tasks_[row];
    if (task.status != BatchTask::Status::Running || percent <= task.transcodeProgress) return;
    task.transcodeProgress = percent;
    emit dataChanged(index(row, 3), index(row, 3), {Qt::DisplayRole, ProgressValueRole});
}

void BatchTaskModel::cancelPending()
{
    for (int row = 0; row < tasks_.size(); ++row) {
        if (tasks_.at(row).status == BatchTask::Status::Pending) {
            setStatus(row, BatchTask::Status::Cancelled, QStringLiteral("队列已停止"));
        }
    }
}

void BatchTaskModel::clear()
{
    beginResetModel();
    tasks_.clear();
    endResetModel();
}
