#include "engine.h"
#include <cmath>
namespace Flame {
QList<int> ColorControls::keys() const {
    if (!available()) return {};
    QList<int> keys = additive() ? red + green + blue + white + extra : QList<int>{};
    if (subtractive()) keys += cyan + magenta + yellow;
    return keys;
}
QStringList Engine::fixtureCells(int channel) const {
    QStringList cells;
    for (const auto &parameter : fixtureMode(channel).parameters) if (!parameter.cell.isEmpty() && !cells.contains(parameter.cell)) cells << parameter.cell;
    return cells;
}
ColorControls Engine::colorControls(int channel, QString cell) const {
    ColorControls controls;
    if (!channels.contains(channel)) return controls;
    const auto mode = fixtureMode(channel);
    if (cell == "*") {
        auto groups = fixtureCells(channel); groups.prepend(QString());
        for (const auto &group : groups) {
            const auto part = colorControls(channel, group); if (!part.available()) continue;
            controls.red += part.red; controls.green += part.green; controls.blue += part.blue; controls.white += part.white;
            controls.cyan += part.cyan; controls.magenta += part.magenta; controls.yellow += part.yellow; controls.extra += part.extra;
        }
        return controls;
    }
    for (int i = 0; i < mode.parameters.size(); ++i) {
        if (mode.parameters[i].cell != cell) continue;
        const auto &attribute = mode.parameters[i].attribute;
        const int key = parameterKey(channel, i);
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

FixtureMode Engine::fixtureMode(int channel) const {
    const auto assignment = fixtures.value(channel);
    return fixtureProfiles.value(assignment.profile).modes.value(assignment.mode);
}
int Engine::parameterKey(int channel, int index) const {
    const auto mode = fixtureMode(channel);
    if (index < 0 || index >= mode.parameters.size()) return 0;
    return mode.parameters[index].intensity && mode.parameters[index].cell.isEmpty() ? channel : -((channel - 1) * 512 + index + 1);
}
int Engine::channelForControl(int key) const { return key > 0 ? key : ((-qint64(key) - 1) / 512) + 1; }
QList<int> Engine::controlIds() const {
    QList<int> ids;
    for (int channel : channels.keys()) {
        ids << channel;
        const auto mode = fixtureMode(channel);
        for (int index = 0; index < mode.parameters.size(); ++index)
            if (parameterKey(channel, index) != channel) ids << parameterKey(channel, index);
    }
    return ids;
}
bool Engine::controlExists(int key) const {
    if (key > 0) return channels.contains(key);
    if (key == 0) return false;
    int channel = channelForControl(key), index = (-qint64(key) - 1) % 512;
    return channels.contains(channel) && index < fixtureMode(channel).parameters.size() && parameterKey(channel, index) == key;
}
double Engine::controlDefault(int key) const {
    if (key >= 0) return 0;
    int channel = channelForControl(key), index = (-qint64(key) - 1) % 512;
    const auto parameter = fixtureMode(channel).parameters.value(index);
    if (parameter.intensity && !parameter.cell.isEmpty()) return 100.;
    const double white = colorComponent(key, 0);
    return white >= 0 ? white : fixtureMode(channel).parameters.value(index).home;
}
QString Engine::controlLabel(int key) const {
    int channel = channelForControl(key);
    const auto parameter = key > 0 ? FixtureParameter{} : fixtureMode(channel).parameters.value((-qint64(key) - 1) % 512);
    QString attr = key > 0 ? "Intensity" : (parameter.cell.isEmpty() ? QString() : parameter.cell + " / ") + parameter.attribute;
    return QString("%1 · %2 · %3").arg(channel).arg(channels.value(channel), attr);
}
QList<int> Engine::occupiedAddresses(int channel) const {
    QList<int> addresses;
    const int footprint = fixtureMode(channel).footprint;
    for (int start : dmxAddresses(channel)) for (int offset = 0; offset < footprint; ++offset) addresses << start + offset;
    return addresses;
}
QMap<int, QString> Engine::patchErrors() const {
    QMap<int, QString> errors; QMap<int, int> owners;
    for (int channel : channels.keys()) {
        QString issue;
        if (!validateMode(fixtureMode(channel), issue)) { errors[channel] = issue; continue; }
        for (int start : dmxAddresses(channel)) {
            int footprint = fixtureMode(channel).footprint;
            if (start < 1 || start > 63999 * 512 || (start - 1) % 512 + footprint > 512) {
                errors[channel] = "Fixture footprint crosses a universe boundary or the address is out of range"; continue;
            }
            for (int a = start; a < start + footprint; ++a) {
                if (owners.contains(a)) {
                    const QString text = QString("Overlap at %1.%2: channels %3 and %4").arg((a - 1) / 512 + 1).arg((a - 1) % 512 + 1, 3, 10, QChar('0')).arg(owners[a]).arg(channel);
                    errors[channel] = text; errors[owners[a]] = text;
                } else owners[a] = channel;
            }
        }
    }
    return errors;
}
QMap<int, QByteArray> Engine::dmxFrames() const {
    QMap<int, QByteArray> frames; const auto values = output();
    for (int channel : channels.keys()) {
        const auto mode = fixtureMode(channel);
        bool realDimmer = false;
        for (const auto &p : mode.parameters) realDimmer |= p.intensity && p.cell.isEmpty() && !p.offsets.isEmpty();
        auto isAncestor = [](const QString &ancestor, const QString &cell) { return ancestor.isEmpty() || cell == ancestor || cell.startsWith(ancestor + '/'); };
        for (int start : dmxAddresses(channel)) {
            if (start < 1 || (start - 1) % 512 + mode.footprint > 512) continue;
            int u = (start - 1) / 512;
            if (!frames.contains(u)) frames[u] = QByteArray(512, '\0');
            for (int index = 0; index < mode.parameters.size(); ++index) {
                const auto &p = mode.parameters[index]; int key = parameterKey(channel, index);
                if (p.offsets.isEmpty()) continue;
                double level = values.value(key, controlDefault(key));
                if (!p.cell.isEmpty() && (p.intensity || p.attribute.startsWith("ColorAdd_"))) {
                    int physicalDepth = realDimmer ? 0 : -1;
                    for (int masterIndex = 0; masterIndex < mode.parameters.size(); ++masterIndex) {
                        const auto &master = mode.parameters[masterIndex];
                        if (master.intensity && !master.offsets.isEmpty() && masterIndex != index && isAncestor(master.cell, p.cell))
                            physicalDepth = qMax(physicalDepth, int(master.cell.size()));
                    }
                    for (int masterIndex = 0; masterIndex < mode.parameters.size(); ++masterIndex) {
                        const auto &master = mode.parameters[masterIndex];
                        if (master.intensity && master.offsets.isEmpty() && !master.cell.isEmpty() &&
                            isAncestor(master.cell, p.cell) && master.cell.size() > physicalDepth)
                            level *= values.value(parameterKey(channel, masterIndex), 100.) / 100.;
                    }
                    if (physicalDepth < 0) level *= values.value(channel) / 100.;
                } else if (!realDimmer && p.attribute.startsWith("ColorAdd_")) level *= values.value(channel) / 100.;
                quint64 max = (quint64(1) << (8 * p.offsets.size())) - 1;
                quint64 raw = quint64(qRound64(qBound(0., level, 100.) * double(max) / 100.));
                for (int byte = 0; byte < p.offsets.size(); ++byte)
                    frames[u][(start - 1) % 512 + p.offsets[byte] - 1] = char((raw >> (8 * (p.offsets.size() - byte - 1))) & 255);
            }
        }
    }
    return frames;
}
bool Engine::canChangeFixture(int channel, FixtureAssignment assignment) const {
    const auto old = fixtures.value(channel);
    if (assignment.profile == old.profile && assignment.mode == old.mode) return true;
    auto hasParameter = [&](int key) { return key < 0 && channelForControl(key) == channel; };
    auto contains = [&](const auto &values) { for (int key : values.keys()) if (hasParameter(key)) return true; return false; };
    if (contains(manual) || contains(parked) || contains(pixels)) return false;
    for (const auto &v : palettes) if (contains(v)) return false;
    for (const auto &v : cueLibrary) if (contains(v.values)) return false;
    for (const auto &seq : sequences) {
        if (contains(seq.output) || contains(seq.target)) return false;
        for (const auto &c : seq.cues) {
            if (contains(c.values) || contains(c.blocked) || contains(c.channelTimes)) return false;
            for (int key : c.suppressed) if (hasParameter(key)) return false;
        }
    }
    for (const auto &seq : linkedPlaybacks) if (contains(seq.output) || contains(seq.target)) return false;
    return true;
}
void Engine::trackFromPrevious(int s, int step, int key, bool only) {
    if (s < 0 || s >= sequences.size() || step < 0 || step >= sequences[s].cues.size() || !controlExists(key)) return;
    const Value before = trackedValue(s, step, key);
    auto &list = sequences[s].cues;
    if (only && step + 1 < list.size() && !cueValues(list[step + 1]).contains(key) && !list[step + 1].blocked.contains(key))
        list[step + 1].values[key] = before;
    auto &c = list[step]; c.values.remove(key); c.blocked.remove(key);
    if (!c.reference.isEmpty() && cueLibrary.value(c.reference).values.contains(key)) c.suppressed.insert(key);
}
void Engine::setChannelBlock(int s, int step, int key, bool enabled) {
    if (s < 0 || s >= sequences.size() || step < 0 || step >= sequences[s].cues.size() || !controlExists(key)) return;
    if (enabled) {
        // Capture before operator[] inserts a default block into the tracking source.
        const Value snapshot = trackedValue(s, step, key);
        sequences[s].cues[step].blocked[key] = snapshot;
    } else sequences[s].cues[step].blocked.remove(key);
}
int Engine::traceSource(int s, int step, int key) const {
    if (s < 0 || s >= sequences.size() || step < 0 || step >= sequences[s].cues.size()) return -1;
    for (int i = step; i >= 0; --i)
        if (cueValues(sequences[s].cues[i]).contains(key) || sequences[s].cues[i].blocked.contains(key)) return i;
    return -1;
}
void Engine::traceEdit(int s, int step, int key, Value value, bool force, bool only) {
    if (s < 0 || s >= sequences.size() || step < 0 || step >= sequences[s].cues.size() || !controlExists(key)) return;
    const auto before = tracked(s, step);
    const auto beforeReference = trackedValue(s, step, key);
    const double old = before.value(key, controlDefault(key));
    int source = traceSource(s, step, key);
    // Eos Trace does not push a newly active level backwards through an inactive track.
    if (!force && value.level > 0 && (!before.contains(key) || old == 0)) source = step;
    editBlind(s, source < 0 ? step : source, key, value);
    auto &list = sequences[s].cues;
    if (only && step + 1 < list.size() && !cueValues(list[step + 1]).contains(key) && !list[step + 1].blocked.contains(key))
        list[step + 1].values[key] = beforeReference;
}
} // namespace Flame
