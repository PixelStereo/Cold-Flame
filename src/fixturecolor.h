#pragma once
#include "engine.h"
#include <QColor>
namespace Flame {
ColorControls colorControls(const Engine &engine, int channel);
QColor fixtureColor(const Engine &engine, int channel, const Values *output = nullptr);
bool setFixtureColor(Engine &engine, int channel, const QColor &color, bool referenced = true);
} // namespace Flame
