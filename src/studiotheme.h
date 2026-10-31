#pragma once
#include <QString>
#include <QPainter>
#include <QIcon>
#include <QWidget>
#include <QVBoxLayout>
#include <QPushButton>
namespace StudioTheme {
inline QIcon icon(int kind) {
    QPixmap canvas(64,64); canvas.fill(Qt::transparent); QPainter p(&canvas); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#1677FF"),3,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    if(kind==0) { p.drawRoundedRect(QRectF(12,12,40,40),5,5); p.drawLine(21,13,21,51); p.drawLine(43,13,43,51); for(int y:{22,32,42}) {p.drawLine(13,y,20,y);p.drawLine(44,y,51,y);} }
    else if(kind==1) { for(int y:{14,29,44}) {p.drawRoundedRect(QRectF(12,y,12,8),2,2);p.drawLine(32,y+4,52,y+4);} }
    else { for(int y:{17,32,47}) {p.drawLine(12,y,52,y);} p.setBrush(QColor("#F1F5F9"));p.drawEllipse(QPointF(24,17),5,5);p.drawEllipse(QPointF(42,32),5,5);p.drawEllipse(QPointF(27,47),5,5); }
    return QIcon(canvas);
}
inline QString styleSheet() { return QStringLiteral(R"(
QMainWindow, QWidget#appShell { background: #F8FAFC; }
QWidget { background: #F8FAFC; color: #182235; font-family: "Microsoft YaHei"; font-size: 13px; }
QWidget[cardContent="true"], QWidget#singleFileControls { background: white; }
QWidget#sidebar { background: #F1F5F9; }
QLabel { background: transparent; }
QLabel#pageTitle { font-size: 27px; font-weight: 700; }
QLabel#appTitle { font-size: 16px; font-weight: 600; }
QLabel[muted="true"] { color: #7B8798; }
QGroupBox { background: white; border: 1px solid #E7ECF2; border-radius: 13px; margin-top: 14px; padding: 20px 18px 16px; font-weight: 600; }
QGroupBox::title { subcontrol-origin: margin; left: 18px; padding: 0 5px; }
QLineEdit, QComboBox, QDoubleSpinBox, QSpinBox { background: #FFFFFF; border: 1px solid #E7ECF2; border-radius: 8px; padding: 7px 9px; min-height: 20px; selection-background-color: #1677FF; }
QComboBox::drop-down { border: 0; width: 24px; }
QComboBox QAbstractItemView { background: white; selection-background-color: #DBEAFE; selection-color: #1677FF; }
QLineEdit:focus, QComboBox:focus, QDoubleSpinBox:focus, QSpinBox:focus { border-color: #1677FF; }
QPlainTextEdit { background: #F8FAFC; border: 1px solid #E7ECF2; border-radius: 8px; padding: 8px; }
QPushButton { background: white; border: 1px solid #E7ECF2; border-radius: 9px; padding: 9px 16px; }
QPushButton:hover { background: #EFF6FF; border-color: #BBD7FF; }
QPushButton:focus { border-color: #1677FF; }
QPushButton:disabled { color: #9AA5B5; background: #F1F5F9; }
QPushButton[primary="true"], QPushButton#startRealButton, QPushButton#batchAddFilesButton { background: #1677FF; color: white; border-color: #1677FF; font-weight: 600; }
QPushButton[primary="true"]:hover, QPushButton#startRealButton:hover, QPushButton#batchAddFilesButton:hover { background: #0866E8; }
QPushButton#startRealButton:disabled { background: #AFCFFB; border-color: #AFCFFB; color: white; }
QPushButton[nav="true"] { background: transparent; text-align: left; border: 0; border-left: 3px solid transparent; padding: 14px 14px; font-size: 15px; }
QPushButton[nav="true"]:checked { background: #DBEAFE; color: #1677FF; border-left-color: #1677FF; font-weight: 600; }
QPushButton[filter="true"]:checked { background: #1677FF; color: white; border-color: #1677FF; }
QProgressBar { border: 0; border-radius: 6px; background: #E7ECF2; color: #182235; text-align: center; min-height: 14px; }
QProgressBar::chunk { background: #1677FF; border-radius: 6px; }
QSlider::groove:horizontal { background: #E7ECF2; height: 5px; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #1677FF; border-radius: 2px; }
QSlider::handle:horizontal { background: #1677FF; border: 2px solid white; width: 10px; height: 10px; margin: -5px 0; border-radius: 7px; }
QScrollArea, QTableView { border: 0; background: transparent; }
QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }
QScrollBar::handle:vertical { background: #CBD5E1; border-radius: 4px; min-height: 28px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QStatusBar { background: #F8FAFC; color: #7B8798; }
QMenu { background: white; border: 1px solid #E7ECF2; padding: 6px; }
QMenu::item { padding: 8px 22px; }
QMenu::item:selected { background: #DBEAFE; }
)"); }
inline QWidget* disclosure(const QString& title, QWidget* content, QWidget* parent = nullptr) {
    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel); layout->setContentsMargins(0,0,0,0); layout->setSpacing(8);
    auto* toggle = new QPushButton(QStringLiteral("›  ") + title, panel);
    toggle->setCheckable(true); toggle->setObjectName(title + QStringLiteral("Toggle"));
    layout->addWidget(toggle); layout->addWidget(content); content->hide();
    QObject::connect(toggle, &QPushButton::toggled, content, [content,toggle,title](bool open) {
        content->setVisible(open); toggle->setText((open ? QStringLiteral("⌄  ") : QStringLiteral("›  ")) + title);
    });
    return panel;
}
}
