#include "fixturecolor.h"
namespace Flame {
QList<int> ColorControls::keys() const {
    if (!available()) return {};
    QList<int> keys = additive() ? red + green + blue + white + extra : QList<int>{};
    if (subtractive()) keys += cyan + magenta + yellow;
    return keys;
}
ColorControls colorControls(const Engine &engine, int channel) {
    ColorControls controls;
    if (!engine.channels.contains(channel)) return controls;
    const auto mode = engine.fixtureMode(channel);
    for (int i = 0; i < mode.parameters.size(); ++i) {
        const auto &attribute = mode.parameters[i].attribute;
        const int key = engine.parameterKey(channel, i);
        if (attribute == "ColorAdd_R") controls.red << key;
        else if (attribute == "ColorAdd_G") controls.green << key;
        else if (attribute == "ColorAdd_B") controls.blue << key;
        else if (attribute == "ColorAdd_W") controls.white << key;
        else if (attribute == "ColorSub_C") controls.cyan << key;
        else if (attribute == "ColorSub_M") controls.magenta << key;
        else if (attribute == "ColorSub_Y") controls.yellow << key;
        else if (attribute.startsWith("ColorAdd_")) controls.extra << key;
    }
    return controls;
}
QColor fixtureColor(const Engine &engine, int channel, const Values *output) {
    const auto controls = colorControls(engine, channel);
    if (!controls.available()) return {};
    const auto values = output ? *output : engine.output(false); // Intensity/master do not change the color swatch.
    auto level = [&](const QList<int> &keys) {
        return keys.isEmpty() ? 0. : values.value(keys.first(), engine.controlDefault(keys.first())) / 100.;
    };
    if (controls.additive()) {
        const double white = level(controls.white);
        return QColor::fromRgbF(qMin(1., level(controls.red) + white),
                                qMin(1., level(controls.green) + white),
                                qMin(1., level(controls.blue) + white));
    }
    return QColor::fromRgbF(1. - level(controls.cyan), 1. - level(controls.magenta), 1. - level(controls.yellow));
}
bool setFixtureColor(Engine &engine, int channel, const QColor &color) {
    const auto controls = colorControls(engine, channel);
    if (!color.isValid() || !controls.available()) return false;
    auto write = [&](const QList<int> &keys, double level) {
        for (int key : keys) engine.manual[key] = {qBound(0., level * 100., 100.), 0};
    };
    const double r = color.redF(), g = color.greenF(), b = color.blueF();
    if (controls.additive()) {
        const double white = controls.white.isEmpty() ? 0. : qMin(r, qMin(g, b));
        write(controls.red, r - white); write(controls.green, g - white); write(controls.blue, b - white);
        write(controls.white, white); write(controls.extra, 0);
    }
    if (controls.subtractive()) {
        // Hybrid fixtures use their additive system and open the CMY flags.
        write(controls.cyan, controls.additive() ? 0. : 1. - r);
        write(controls.magenta, controls.additive() ? 0. : 1. - g);
        write(controls.yellow, controls.additive() ? 0. : 1. - b);
    }
    return true;
}
} // namespace Flame
