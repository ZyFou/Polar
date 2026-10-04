#pragma once
#include <QWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
class QListWidget;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class SwitchButton;
class LeaderboardOverview : public QWidget {
    Q_OBJECT
public:
    explicit LeaderboardOverview(QWidget *parent=nullptr);
    void setSnapshot(const QJsonArray &players);
    void setMetadata(qint64 start,qint64 end);
    void selectPlayer(const QJsonObject &player);
    void clearSnapshot();
signals:
    void playerChosen(const QJsonObject &player);
protected:
    void paintEvent(QPaintEvent *) override;
    void changeEvent(QEvent *) override;
private:
    QListWidget *list;
    QComboBox *window;
    QDoubleSpinBox *pause;
    QLabel *status,*windowLabel,*pauseLabel,*columns;
    SwitchButton *background;
    QVector<QJsonObject> players;
    QJsonObject focus;
    qint64 startEpoch=0,endEpoch=0;
    QDateTime received;
    void refresh();
    void retranslate();
};
