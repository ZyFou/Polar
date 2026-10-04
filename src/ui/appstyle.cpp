#include "appstyle.h"
#include "appsettings.h"
#include <QApplication>
#include <QFocusEvent>
#include <QProxyStyle>
#include <QPainter>
#include <QStyleOption>
#include <QWidget>

namespace {
// Mouse focus remains usable for input, but only keyboard navigation draws a ring.
class FocusEvents : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject *object, QEvent *event) override {
        auto *widget = qobject_cast<QWidget *>(object);
        if (!widget) return false;
        bool keyboard = widget->property("keyboardFocus").toBool();
        if (event->type() == QEvent::FocusIn) {
            const auto reason = static_cast<QFocusEvent *>(event)->reason();
            keyboard = reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason
                || reason == Qt::ShortcutFocusReason;
        } else if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::FocusOut) {
            keyboard = false;
        } else return false;
        if (widget->property("keyboardFocus").toBool() != keyboard) {
            widget->setProperty("keyboardFocus", keyboard);
            widget->style()->unpolish(widget);
            widget->style()->polish(widget);
            widget->update();
        }
        return false;
    }
};
class FocusStyle : public QProxyStyle {
public:
    explicit FocusStyle(QStyle *base) : QProxyStyle(base) {}
    void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                       QPainter *painter, const QWidget *widget = nullptr) const override {
        if (element == PE_FrameFocusRect) {
            if (widget && widget->property("keyboardFocus").toBool()) {
                painter->save();
                painter->setPen(QPen(option->palette.highlight().color(), 2));
                painter->setBrush(Qt::NoBrush);
                painter->drawRoundedRect(option->rect.adjusted(1,1,-1,-1), 3, 3);
                painter->restore();
            }
            return;
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
};
}
void AppStyle::installFocusStyle() {
    if (qApp->property("polarFocusStyle").toBool()) return;
    qApp->setProperty("polarFocusStyle", true);
    qApp->setStyle(new FocusStyle(qApp->style()));
    qApp->installEventFilter(new FocusEvents(qApp));
}
QString AppStyle::stylesheet() {
    if (!AppSettings::useNewUI) return {};
    const QString panel = AppSettings::transparentControls ? "transparent" : "palette(window)";
    const QString field = AppSettings::transparentControls ? "rgba(128,128,128,20)" : "palette(base)";
    return QStringLiteral(
        "QGroupBox { background:%1; border:1px solid rgba(128,128,128,60); border-radius:4px; margin-top:18px; font-weight:bold; }"
        "QGroupBox::title { subcontrol-origin:margin; subcontrol-position:top left; left:10px; padding:0 5px; }"
        "QTabWidget::pane { background:%1; border:1px solid rgba(128,128,128,60); border-radius:4px; }"
        "QTabBar::tab { background:%1; padding:5px 12px; border:1px solid rgba(128,128,128,60); border-top-left-radius:4px; border-top-right-radius:4px; margin-right:2px; }"
        "QTabBar::tab:selected { border-top:2px solid #1a73e8; }"
        "QLineEdit,QComboBox,QSpinBox,QDoubleSpinBox,QTextEdit,QListWidget,QTableWidget { background:%2; border:1px solid rgba(128,128,128,60); border-radius:4px; padding:4px; }"
        "QPushButton { background:palette(button); border:1px solid rgba(128,128,128,60); border-radius:4px; padding:5px 12px; font-weight:bold; }"
        "QPushButton:hover,QComboBox:hover { border-color:#1a73e8; }"
        "QPushButton:pressed { background:palette(mid); }"
        "QAbstractItemView { outline:0; }"
        "QCheckBox,QRadioButton { spacing:7px; padding:3px; border:1px solid transparent; border-radius:3px; }"
        "QCheckBox::indicator,QRadioButton::indicator { width:15px; height:15px; border:1px solid palette(mid); background:%2; border-radius:3px; }"
        "QRadioButton::indicator { border-radius:8px; }"
        "QCheckBox::indicator:checked,QRadioButton::indicator:checked { background:#1a73e8; border-color:#1a73e8; }"
        "QCheckBox::indicator:disabled,QRadioButton::indicator:disabled { background:palette(window); border-color:palette(mid); }"
        "QPushButton:disabled,QCheckBox:disabled,QLabel:disabled { color:palette(mid); }"
        "QWidget[keyboardFocus=\"true\"]:focus { border:1px solid #1a73e8; }"
        "QHeaderView::section { background:%1; padding:5px; border:1px solid palette(mid); }"
    ).arg(panel, field);
}
void AppStyle::apply(QWidget *root) {
    if(!root) return;
    const auto sheet=stylesheet();if(root->styleSheet()!=sheet) root->setStyleSheet(sheet);
}
