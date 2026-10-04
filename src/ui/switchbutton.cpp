#include "switchbutton.h"
#include <QPainter>
SwitchButton::SwitchButton(const QString &text, QWidget *parent) : QCheckBox(text,parent) {
    setCursor(Qt::PointingHandCursor);
    animation.setDuration(180);
    animation.setEasingCurve(QEasingCurve::OutCubic);
    connect(&animation,&QVariantAnimation::valueChanged,this,[this](const QVariant &v){position=v.toReal();update();});
    connect(this,&QCheckBox::toggled,this,[this](bool enabled){animation.stop();animation.setStartValue(position);animation.setEndValue(enabled?1.0:0.0);animation.start();});
}
QSize SwitchButton::sizeHint() const { return {fontMetrics().horizontalAdvance(text())+66,34}; }
void SwitchButton::paintEvent(QPaintEvent *) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
    p.setOpacity(isEnabled()?1.0:0.45);
    const qreal y=(height()-24)/2.0;
    const QColor off=palette().mid().color(),on("#1a73e8");
    QColor track=QColor::fromRgbF(off.redF()*(1-position)+on.redF()*position,
        off.greenF()*(1-position)+on.greenF()*position,off.blueF()*(1-position)+on.blueF()*position);
    p.setPen(Qt::NoPen);p.setBrush(track);p.drawRoundedRect(QRectF(3,y,44,24),10,10);
    p.setBrush(Qt::white);p.drawEllipse(QRectF(6+20*position,y+3,18,18));
    p.setPen(palette().text().color());p.drawText(QRect(56,0,width()-56,height()),Qt::AlignVCenter|Qt::AlignLeft,text());
    if(hasFocus() && property("keyboardFocus").toBool()) {
        p.setPen(QPen(palette().highlight().color(),2));p.setBrush(Qt::NoBrush);p.drawRoundedRect(rect().adjusted(1,1,-1,-1),5,5);
    }
}
