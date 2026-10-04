#include "notificationcenter.h"
#include "appsettings.h"
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QRegularExpression>
NotificationCenter::NotificationCenter(QWidget *host) : QFrame(host),slide(this,"pos",this) {
    setObjectName("notificationToast");
    setStyleSheet("QFrame#notificationToast { background:palette(window); border:1px solid palette(mid); border-left:4px solid #1a73e8; border-radius:8px; } QLabel { border:0; background:transparent; }");
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(16,14,16,14);
    message=new QLabel(this);message->setObjectName("notificationMessage");message->setWordWrap(true);message->setTextFormat(Qt::PlainText);layout->addWidget(message);
    auto *buttons=new QHBoxLayout;buttons->addStretch();
    yes=new QPushButton(tr("Yes"),this);no=new QPushButton(tr("Dismiss"),this);
    yes->setObjectName("notificationAccept");no->setObjectName("notificationDismiss");
    buttons->addWidget(yes);buttons->addWidget(no);layout->addLayout(buttons);
    hold.setSingleShot(true);slide.setDuration(230);slide.setEasingCurve(QEasingCurve::OutCubic);
    connect(&hold,&QTimer::timeout,this,&NotificationCenter::dismiss);
    connect(yes,&QPushButton::clicked,this,[this]{auto action=current.accept;dismiss();if(action) action();});
    connect(no,&QPushButton::clicked,this,&NotificationCenter::dismiss);
    connect(&slide,&QPropertyAnimation::finished,this,[this]{if(leaving){hide();active=false;leaving=false;showNext();}});
    host->installEventFilter(this);hide();
}
QString NotificationCenter::cleanMessage(QString text) {
    text.replace(QRegularExpression("https?://[^\\s<>]+",QRegularExpression::CaseInsensitiveOption),tr("remote service"));
    text.replace(QRegularExpression("[\\r\\n]+")," ");
    return text.trimmed().left(500);
}
QPoint NotificationCenter::anchor() const {return {16,qMax(16,parentWidget()->height()-height()-24)};}
void NotificationCenter::post(const QString &kind,const QString &text,std::function<void()> accept,const QString &label) {
    if(!AppSettings::notificationEnabled(kind)) return;
    const auto cleaned=cleanMessage(text);
    if(cleaned.isEmpty() || (active && current.kind==kind && current.message==cleaned)) return;
    for(const auto &notice:queue) if(notice.kind==kind && notice.message==cleaned) return;
    if(queue.size()>=10) queue.dequeue();
    queue.enqueue({kind,cleaned,label,std::move(accept)});if(!active) showNext();
}
void NotificationCenter::showNext() {
    while(!queue.isEmpty()) {
        current=queue.dequeue();if(!AppSettings::notificationEnabled(current.kind)) continue;
        active=true;message->setText(current.message);
        yes->setVisible(bool(current.accept));yes->setText(current.acceptLabel.isEmpty()?tr("Yes"):current.acceptLabel);
        no->setText(current.accept?tr("No"):tr("Dismiss"));
        setFixedWidth(qMax(200,qMin(400,parentWidget()->width()-32)));adjustSize();
        move(-width(),anchor().y());show();raise();
        slide.setStartValue(pos());slide.setEndValue(anchor());slide.start();
        hold.start(AppSettings::notificationDuration(current.kind)*1000);return;
    }
}
void NotificationCenter::dismiss() {
    if(!active || leaving) return;
    hold.stop();slide.stop();leaving=true;
    slide.setStartValue(pos());slide.setEndValue(QPoint(-width(),anchor().y()));slide.start();
}
void NotificationCenter::preferencesChanged() {
    if(active && !AppSettings::notificationEnabled(current.kind)) dismiss();
    else if(active && !leaving) hold.start(AppSettings::notificationDuration(current.kind)*1000);
}
bool NotificationCenter::eventFilter(QObject *object,QEvent *event) {
    if(object==parentWidget() && event->type()==QEvent::Resize && active) {
        if(!leaving) {slide.stop();move(anchor());}raise();
    }
    return false;
}
