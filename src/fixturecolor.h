#pragma once
#include "engine.h"
#include <QColor>
namespace Flame {
struct ColorControls {
    QList<int> red, green, blue, white, cyan, magenta, yellow, extra;
    bool additive() const { return !red.isEmpty() && !green.isEmpty() && !blue.isEmpty(); }
    bool subtractive() const { return !cyan.isEmpty() && !magenta.isEmpty() && !yellow.isEmpty(); }
    bool available() const { return additive() || subtractive(); }
    QList<int> keys() const;
};
ColorControls colorControls(const Engine &engine, int channel);
QColor fixtureColor(const Engine &engine, int channel, const Values *output = nullptr);
bool setFixtureColor(Engine &engine, int channel, const QColor &color);
} // namespace Flame
