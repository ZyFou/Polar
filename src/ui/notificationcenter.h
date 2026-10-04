#pragma once
#include <QFrame>
#include <QQueue>
#include <QTimer>
#include <QPropertyAnimation>
#include <functional>
class QLabel;
class QPushButton;
class NotificationCenter : public QFrame {
    Q_OBJECT
public:
    explicit NotificationCenter(QWidget *host);
    void post(const QString &kind, const QString &message,
              std::function<void()> accept = {}, const QString &acceptLabel = {});
    void preferencesChanged();
    static QString cleanMessage(QString message);
protected:
    bool eventFilter(QObject *,QEvent *) override;
private:
    struct Notice {QString kind,message,acceptLabel;std::function<void()> accept;};
    QQueue<Notice> queue;
    Notice current;
    QLabel *message;
    QPushButton *yes,*no;
    QTimer hold;
    QPropertyAnimation slide;
    bool active=false,leaving=false;
    QPoint anchor() const;
    void showNext();
    void dismiss();
};
