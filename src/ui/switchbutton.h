#pragma once
#include <QCheckBox>
#include <QVariantAnimation>
class SwitchButton : public QCheckBox {
    Q_OBJECT
public:
    explicit SwitchButton(const QString &text, QWidget *parent = nullptr);
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent *) override;
    bool hitButton(const QPoint &point) const override { return rect().contains(point); }
private:
    QVariantAnimation animation;
    qreal position = 0;
};
