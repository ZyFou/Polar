#pragma once
#include <QString>
class QWidget;
namespace AppStyle {
void installFocusStyle();
QString stylesheet();
void apply(QWidget *root);
}
