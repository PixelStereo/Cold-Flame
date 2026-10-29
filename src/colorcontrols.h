#pragma once
#include <QtCore>
namespace Flame {
struct ColorControls {
    QList<int> red, green, blue, white, cyan, magenta, yellow, extra;
    bool additive() const { return !red.isEmpty() && !green.isEmpty() && !blue.isEmpty(); }
    bool subtractive() const { return !cyan.isEmpty() && !magenta.isEmpty() && !yellow.isEmpty(); }
    bool available() const { return additive() || subtractive(); }
    QList<int> keys() const;
};
struct ColorPalette {
    QString name;
    double red = 1, green = 1, blue = 1;
};
} // namespace Flame
