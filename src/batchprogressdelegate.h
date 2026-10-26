#ifndef QTFFMPEGSTUDIO_BATCHPROGRESSDELEGATE_H
#define QTFFMPEGSTUDIO_BATCHPROGRESSDELEGATE_H

#include "batchtaskmodel.h"

#include <QApplication>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QStyleOptionProgressBar>
#include <algorithm>

// 只负责把模型给出的数值画成进度条；未知、失败和取消仍显示模型文字。
class BatchProgressDelegate final : public QStyledItemDelegate
{
public:
    explicit BatchProgressDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        const QVariant progress = index.data(BatchTaskModel::ProgressValueRole);
        if (!progress.isValid()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        const QStyle* style = option.widget ? option.widget->style() : QApplication::style();
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &option, painter, option.widget);

        QStyleOptionProgressBar bar;
        bar.rect = option.rect.adjusted(4, 3, -4, -3);
        bar.state = option.state;
        bar.direction = option.direction;
        bar.palette = option.palette;
        bar.minimum = 0;
        bar.maximum = 100;
        bar.progress = progress.toInt();
        bar.text = index.data(Qt::DisplayRole).toString();
        bar.textVisible = true;
        style->drawControl(QStyle::CE_ProgressBar, &bar, painter, option.widget);
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const QSize base = QStyledItemDelegate::sizeHint(option, index);
        return {std::max(base.width(), 120), std::max(base.height(), 24)};
    }
};

#endif
