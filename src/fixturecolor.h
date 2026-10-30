#pragma once
#include "engine.h"
#include <QColor>
namespace Flame {
ColorControls colorControls(const Engine &engine, int channel);
QColor fixtureColor(const Engine &engine, int channel, const Values *output = nullptr, QString cell = "*");
bool setFixtureColor(Engine &engine, int channel, const QColor &color, bool referenced = true, QString cell = "*");
} // namespace Flame
