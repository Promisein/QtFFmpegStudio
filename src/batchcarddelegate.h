#pragma once
#include "batchtaskmodel.h"
#include "elapsedformat.h"
#include <QStyledItemDelegate>
#include <QPainter>
#include <QPainterPath>
#include <QImage>
#include <QFileInfo>
#include <QMouseEvent>
#include <functional>
class BatchCardDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    std::function<void(int,const QPoint&)> action;
    std::function<qint64(quint64)> runningElapsed;
    std::function<QImage(quint64,const QString&)> thumbnail;
    bool editorEvent(QEvent* event,QAbstractItemModel*,const QStyleOptionViewItem& option,const QModelIndex& index) override {
        if(event->type()==QEvent::MouseButtonRelease) {
            auto* mouse=static_cast<QMouseEvent*>(event);
            if(mouse->button()==Qt::LeftButton && mouse->position().x()>option.rect.right()-48 && action) { action(index.row(),mouse->globalPosition().toPoint()); return true; }
        }
        return false;
    }
    QSize sizeHint(const QStyleOptionViewItem&,const QModelIndex&) const override { return {540,112}; }
    void paint(QPainter* p,const QStyleOptionViewItem& o,const QModelIndex& i) const override {
        const auto* model=qobject_cast<const BatchTaskModel*>(i.model()); if(!model) return;
        const auto task=model->task(i.row());
        p->save(); p->fillRect(o.rect,QColor("#F8FAFC")); p->setRenderHint(QPainter::Antialiasing);
        QRect card=o.rect.adjusted(1,5,-2,-5);
        p->setPen(QColor(o.state & QStyle::State_Selected ? "#B3D3FF":"#E7ECF2")); p->setBrush(Qt::white); p->drawRoundedRect(card,13,13);
        QRect icon(card.left()+16,card.center().y()-32,104,64);
        const QImage cover=thumbnail?thumbnail(task.id,task.options.inputPath):QImage();
        if(cover.isNull()) {
            p->setPen(Qt::NoPen); p->setBrush(QColor("#EFF6FF")); p->drawRoundedRect(icon,9,9);
            p->setPen(QColor("#1677FF")); p->drawText(icon,Qt::AlignCenter,QStringLiteral("MP4"));
        } else {
            p->save(); QPainterPath clip; clip.addRoundedRect(QRectF(icon),9,9); p->setClipPath(clip);
            p->fillRect(icon,QColor("#101827"));
            const QSize fitted=cover.size().scaled(icon.size(),Qt::KeepAspectRatio);
            const QRect destination(QPoint(icon.center().x()-fitted.width()/2,icon.center().y()-fitted.height()/2),fitted);
            p->setRenderHint(QPainter::SmoothPixmapTransform); p->drawImage(destination,cover); p->restore();
        }
        int x=icon.right()+18; int textWidth=qMax(70,card.width()/2-(x-card.left())-8);
        auto font=o.font; font.setBold(true); p->setFont(font); p->setPen(QColor("#182235"));
        p->drawText(QRect(x,card.top()+23,textWidth,24),Qt::AlignVCenter,p->fontMetrics().elidedText(QFileInfo(task.options.inputPath).fileName(),Qt::ElideMiddle,textWidth));
        font.setBold(false); p->setFont(font); p->setPen(QColor("#7B8798"));
        QString description=task.options.videoCodec.toUpper()+QStringLiteral(" / ")+(task.options.maxShortEdge?QString::number(task.options.maxShortEdge)+"p":QStringLiteral("原始尺寸"));
        p->drawText(QRect(x,card.top()+53,textWidth,24),Qt::AlignVCenter,p->fontMetrics().elidedText(description,Qt::ElideRight,textWidth));
        QString status; QColor color("#7B8798");
        switch(task.status) {
        case BatchTask::Status::Pending: status=QStringLiteral("等待中 · 排队"); break;
        case BatchTask::Status::Running: status=task.transcodeProgress<0?QStringLiteral("运行中 · 估算中"):QStringLiteral("运行中 · %1%").arg(task.transcodeProgress); color=QColor("#1677FF"); break;
        case BatchTask::Status::Succeeded: status=QStringLiteral("✓ 已完成"); color=QColor("#22A06B"); break;
        case BatchTask::Status::Failed: status=QStringLiteral("× 失败 · 查看原因"); color=QColor("#E5484D"); break;
        case BatchTask::Status::Cancelled: status=QStringLiteral("已取消"); break;
        }
        int sx=card.left()+card.width()/2+12; int sw=card.right()-sx-50;
        p->setPen(color); p->drawText(QRect(sx,card.top()+22,sw,24),Qt::AlignVCenter,status);
        if(task.status==BatchTask::Status::Running) {
            QRect bar(sx,card.top()+57,qMax(40,sw-90),7); p->setPen(Qt::NoPen); p->setBrush(QColor("#E7ECF2")); p->drawRoundedRect(bar,3,3);
            p->setBrush(color); bar.setWidth(bar.width()*qMax(0,task.transcodeProgress)/100); p->drawRoundedRect(bar,3,3);
        }
        p->setPen(QColor("#7B8798")); p->drawText(QRect(sx,card.top()+73,sw,20),Qt::AlignVCenter,task.status==BatchTask::Status::Running && runningElapsed ? formatElapsed(runningElapsed(task.id)) : task.elapsedMilliseconds>=0?formatElapsed(task.elapsedMilliseconds):QStringLiteral("点击查看详情与操作"));
        p->setPen(QColor("#7B8798")); p->drawText(QRect(card.right()-40,card.top()+30,30,30),Qt::AlignCenter,QStringLiteral("···"));
        p->restore();
    }
};
