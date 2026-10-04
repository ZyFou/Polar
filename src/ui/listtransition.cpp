#include "listtransition.h"
#include <QListWidget>
#include <QScrollBar>
#include <QPainter>
#include <QVariantAnimation>
#include <QSignalBlocker>
#include <QSet>

namespace {
struct Frame {QRect rect;QPixmap image;};
using Frames=QHash<QString,Frame>;
constexpr int keyRole=Qt::UserRole+1;
Frames capture(QListWidget *list) {
    if(auto *old=list->viewport()->findChild<QWidget*>("rankTransition")) delete old;
    Frames frames;
    if(!list->isVisible()) return frames;
    for(int i=0;i<list->count();++i) {
        auto *item=list->item(i);const QRect rect=list->visualItemRect(item);
        const QString key=item->data(keyRole).toString();
        if(key.isEmpty()) continue;
        QPixmap image;
        if(rect.intersects(list->viewport()->rect())) {
            if(auto *widget=list->itemWidget(item)) image=widget->grab();
            else image=list->viewport()->grab(rect);
        }
        frames.insert(key,{rect,image});
    }
    return frames;
}
class MovingRows : public QWidget {
    struct Move {QRect from,to;QPixmap image;};
    QVector<Move> moves;
    QVariantAnimation clock;
    qreal progress=0;
public:
    MovingRows(QListWidget *list,const Frames &before,const Frames &after) : QWidget(list->viewport()) {
        setObjectName("rankTransition");setAttribute(Qt::WA_TransparentForMouseEvents);
        setGeometry(parentWidget()->rect());
        QSet<QString> keys;
        for(auto it=before.cbegin();it!=before.cend();++it) keys.insert(it.key());
        for(auto it=after.cbegin();it!=after.cend();++it) keys.insert(it.key());
        bool changed=false;
        for(const auto &key:keys) {
            const auto old=before.value(key),next=after.value(key);
            if(old.image.isNull() && next.image.isNull()) continue;
            QRect from=old.rect,to=next.rect;
            if(!before.contains(key) || from.top()>height()) from=QRect(next.rect.left(),height()+8,next.rect.width(),next.rect.height());
            if(!after.contains(key) || to.top()>height()) to=QRect(old.rect.left(),height()+8,old.rect.width(),old.rect.height());
            if(from!=to) changed=true;
            moves.append({from,to,next.image.isNull()?old.image:next.image});
        }
        if(!changed || before.isEmpty()) {deleteLater();return;}
        clock.setDuration(420);clock.setStartValue(0.0);clock.setEndValue(1.0);clock.setEasingCurve(QEasingCurve::InOutCubic);
        connect(&clock,&QVariantAnimation::valueChanged,this,[this](const QVariant &v){progress=v.toReal();update();});
        connect(&clock,&QVariantAnimation::finished,this,&QObject::deleteLater);
        show();raise();clock.start();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);painter.fillRect(rect(),palette().base());
        for(const auto &move:moves) {
            const QPointF pos=move.from.topLeft()*(1-progress)+move.to.topLeft()*progress;
            painter.drawPixmap(pos,move.image);
        }
    }
};
}
QString ListTransition::playerKey(const QJsonObject &player) {
    for(const auto &field:{"id","identifier"}) {
        const auto key=player.value(field).toVariant().toString();if(!key.isEmpty()) return key;
    }
    return player.value("name").toString();
}
void ListTransition::reconcile(QListWidget *list,const QVector<QJsonObject> &players,
                              const std::function<QWidget *(int)> &makeRow,int rowHeight) {
    if(!list) return;
    const auto before=capture(list);
    const auto selected=list->currentItem()?list->currentItem()->data(keyRole).toString():QString();
    const int scroll=list->verticalScrollBar()->value();
    const QSignalBlocker blocker(list);list->setUpdatesEnabled(false);
    QHash<QString,QListWidgetItem*> existing;
    for(int i=0;i<list->count();++i) existing.insert(list->item(i)->data(keyRole).toString(),list->item(i));
    QSet<QString> retained;QHash<QString,int> occurrences;
    for(int i=0;i<players.size();++i) {
        const auto raw=playerKey(players[i]);
        const QString key=raw+"/"+QString::number(occurrences[raw]++);
        auto *item=existing.value(key);
        if(!item) {item=new QListWidgetItem;list->insertItem(i,item);}
        else if(list->row(item)!=i) {list->takeItem(list->row(item));list->insertItem(i,item);}
        item->setData(keyRole,key);item->setData(Qt::UserRole,players[i]);
        item->setData(Qt::AccessibleTextRole,players[i].value("name").toString());
        item->setSizeHint({list->viewport()->width(),rowHeight});
        list->setItemWidget(item,makeRow(i));retained.insert(key);
        if(key==selected) list->setCurrentItem(item);
    }
    for(auto it=existing.cbegin();it!=existing.cend();++it)
        if(!retained.contains(it.key())) delete list->takeItem(list->row(it.value()));
    list->setUpdatesEnabled(true);list->doItemsLayout();list->verticalScrollBar()->setValue(scroll);
    const auto after=capture(list);new MovingRows(list,before,after);
}
