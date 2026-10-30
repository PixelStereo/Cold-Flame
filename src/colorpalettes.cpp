#include "engine.h"
#include <cmath>
namespace Flame {
static bool validColor(const ColorPalette &color) {
    return std::isfinite(color.red) && std::isfinite(color.green) && std::isfinite(color.blue) &&
           color.red >= 0 && color.red <= 1 && color.green >= 0 && color.green <= 1 && color.blue >= 0 && color.blue <= 1;
}
ColorPalette Engine::colorPalette(int id) const {
    return id == 0 ? ColorPalette{"White", 1, 1, 1} : colorPalettes.value(id);
}
int Engine::findOrCreateColorPalette(ColorPalette color) {
    if (!validColor(color)) return -1;
    if (qAbs(color.red - 1) < 1e-7 && qAbs(color.green - 1) < 1e-7 && qAbs(color.blue - 1) < 1e-7) return 0;
    for (auto it = colorPalettes.begin(); it != colorPalettes.end(); ++it)
        if (it.key() > 0 && qAbs(it->red - color.red) < 1e-7 && qAbs(it->green - color.green) < 1e-7 && qAbs(it->blue - color.blue) < 1e-7) return it.key();
    int id = colorPalettes.isEmpty() ? 1 : colorPalettes.lastKey() + 1;
    if (id > 9999) return -1;
    colorPalettes[id] = color;
    return id;
}
bool Engine::updateColorPalette(int id, ColorPalette color) {
    if (id <= 0 || !colorPalettes.contains(id) || !validColor(color)) return false;
    colorPalettes[id] = color; return true;
}
bool Engine::deleteColorPalette(int id) {
    if (id <= 0 || !colorPalettes.contains(id)) return false;
    auto used = [&](const auto &values) { for (const auto &v : values) if (v.colorPalette == id) return true; return false; };
    if (used(manual)) return false;
    for (const auto &cue : cueLibrary) if (used(cue.values)) return false;
    auto inList = [&](const Sequence &seq) { for (const auto &c : seq.cues) if (used(c.values) || used(c.blocked)) return true; return false; };
    for (const auto &seq : sequences) if (inList(seq)) return false;
    for (const auto &seq : linkedPlaybacks) if (inList(seq)) return false;
    return colorPalettes.remove(id) > 0;
}
double Engine::colorComponent(int key, int id) const {
    if (key >= 0 || id < 0 || (id > 0 && !colorPalettes.contains(id))) return -1;
    int channel = channelForControl(key);
    const int index = (-qint64(key) - 1) % 512;
    const auto controls = colorControls(channel, fixtureMode(channel).parameters.value(index).cell);
    if (!controls.available()) return -1;
    const auto color = colorPalette(id);
    const double white = controls.white.isEmpty() ? 0. : qMin(color.red, qMin(color.green, color.blue));
    if (controls.additive()) {
        if (controls.red.contains(key)) return (color.red - white) * 100;
        if (controls.green.contains(key)) return (color.green - white) * 100;
        if (controls.blue.contains(key)) return (color.blue - white) * 100;
        if (controls.white.contains(key)) return white * 100;
        if (controls.extra.contains(key)) return 0;
    }
    if (controls.subtractive()) {
        if (controls.cyan.contains(key)) return controls.additive() ? 0 : (1 - color.red) * 100;
        if (controls.magenta.contains(key)) return controls.additive() ? 0 : (1 - color.green) * 100;
        if (controls.yellow.contains(key)) return controls.additive() ? 0 : (1 - color.blue) * 100;
    }
    return -1;
}
bool Engine::applyColorPalette(int channel, int id, QString cell) {
    if (!channels.contains(channel) || id < 0 || (id > 0 && !colorPalettes.contains(id))) return false;
    const auto controls = colorControls(channel, cell);
    if (!controls.available()) return false;
    for (int key : controls.keys()) manual[key] = {colorComponent(key, id), 0, id};
    return true;
}
int Engine::assignColorPaletteToCue(QString number, int id, QSet<int> fixtures) {
    if (!cueLibrary.contains(number) || id < 0 || (id > 0 && !colorPalettes.contains(id))) return 0;
    auto values = cueLibrary.value(number).values;
    if (fixtures.isEmpty()) for (int key : values.keys()) fixtures.insert(channelForControl(key));
    int count = 0;
    for (int channel : fixtures) {
        const auto controls = colorControls(channel);
        if (!controls.available()) continue;
        for (int key : controls.keys()) values[key] = {colorComponent(key, id), 0, id};
        ++count;
    }
    if (count && updateStoredCue(number, values, true)) return count;
    return 0;
}
bool Engine::validValue(int key, Value value) const {
    return controlExists(key) && std::isfinite(value.level) && value.level >= 0 && value.level <= 100 &&
           value.palette >= 0 && (!value.palette || palettes.contains(value.palette)) && value.colorPalette >= -1 &&
           (value.colorPalette == -1 || (!value.palette && colorComponent(key, value.colorPalette) >= 0));
}
Value Engine::trackedValue(int s, int step, int key) const {
    Value value{controlDefault(key), 0, colorComponent(key, 0) >= 0 ? 0 : -1};
    if (s < 0 || s >= sequences.size()) return value;
    for (int i = 0; i <= step && i < sequences[s].cues.size(); ++i) {
        const auto &cue = sequences[s].cues[i];
        if (cue.blocked.contains(key)) value = cue.blocked.value(key);
        const auto moves = cueValues(cue);
        if (moves.contains(key)) value = moves.value(key);
    }
    return value;
}
Value Engine::recordedValue(int key, const Values &live) const {
    if (manual.contains(key) && qAbs(resolve(key, manual.value(key)) - live.value(key, controlDefault(key))) < 1e-7)
        return manual.value(key);
    Value result{live.value(key, controlDefault(key)), 0};
    if (colorComponent(key, 0) < 0) return result;
    QPair<int, quint64> owner{-1, 0};
    auto read = [&](const Sequence &seq) {
        if (!seq.output.contains(key) || seq.current < 0 || seq.current >= seq.cues.size()) return;
        auto rank = qMakePair(seq.priority, seq.ownership.value(key, seq.stamp));
        if (rank <= owner) return;
        owner = rank; result = {live.value(key, controlDefault(key)), 0};
        Value instruction; bool found = false;
        for (int i = 0; i <= seq.current; ++i) {
            if (seq.cues[i].blocked.contains(key)) { instruction = seq.cues[i].blocked.value(key); found = true; }
            const auto moves = cueValues(seq.cues[i]);
            if (moves.contains(key)) { instruction = moves.value(key); found = true; }
        }
        if (found && instruction.colorPalette >= 0 && qAbs(resolve(key, instruction) - result.level) < 1e-7) result = instruction;
    };
    for (const auto &seq : sequences) read(seq);
    for (const auto &seq : linkedPlaybacks) read(seq);
    if (owner.first < 0 && qAbs(result.level - colorComponent(key, 0)) < 1e-7) result.colorPalette = 0;
    return result;
}
} // namespace Flame
