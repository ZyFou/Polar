#pragma once
#include <QString>
namespace StartMenuShortcut {
enum class State { Unsupported, Missing, Valid, Invalid };
State inspect(const QString &target);
bool create(const QString &target);
#ifdef POLAR_TESTING
void setDirectoryForTests(const QString &directory);
#endif
}
