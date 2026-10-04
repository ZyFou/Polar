#pragma once
#include <QJsonObject>
#include <QVector>
#include <functional>
class QListWidget;
class QWidget;
namespace ListTransition {
QString playerKey(const QJsonObject &player);
void reconcile(QListWidget *list,const QVector<QJsonObject> &players,
               const std::function<QWidget *(int)> &makeRow,int rowHeight);
}
