#include "engine.h"
#include <algorithm>
#include <cmath>
namespace Flame {
Engine::Engine() {
    for (int i = 1; i <= ChannelsPerPage; ++i) {
        channels[i] = QString("Channel %1").arg(i);
        patch[i] = i;
    }
    Sequence s;
    s.name = "sequence-1";
    sequences << s;
}
QList<int> Engine::dmxAddresses(int channel) const {
    QList<int> result;
    if (patch.contains(channel))
        result << patch.value(channel);
    result.append(additionalPatch.value(channel));
    return result;
}
double Engine::resolve(int channel, Value v) const {
    return v.palette ? palettes.value(v.palette).value(channel, v.level) : v.level;
}
QMap<int, Value> Engine::cueValues(const Cue &step) const {
    auto values = step.reference.isEmpty() ? QMap<int, Value>{} : cueLibrary.value(step.reference).values;
    for (int key : step.suppressed) values.remove(key);
    for (auto it = step.values.begin(); it != step.values.end(); ++it) values[it.key()] = it.value();
    return values;
}
QString Engine::cueLabel(const Cue &step) const {
    return step.reference.isEmpty() ? step.label : cueLibrary.value(step.reference).label;
}
QString Engine::nextStoredCueNumber() const {
    double last = 0;
    for (const auto &cue : cueLibrary) last = qMax(last, cue.number.toDouble());
    return QString::number(last + 10, 'g', 12);
}
bool Engine::storeCue(QString number, QString label, bool manualOnly) {
    bool valid;
    double n = number.toDouble(&valid);
    if (!valid || !std::isfinite(n) || n <= 0 || n > 9999.999) return false;
    number = QString::number(n, 'g', 12);
    StoredCue cue;
    cue.number = number;
    cue.label = label.isEmpty() ? "cue-" + number : label;
    if (!manualOnly) {
        auto live = output(false, false);
        for (int channel : controlIds()) cue.values[channel] = {live.value(channel), 0};
    }
    for (auto it = manual.begin(); it != manual.end(); ++it)
        if (controlExists(it.key())) cue.values[it.key()] = it.value();
    cueLibrary[number] = cue;
    return true;
}
bool Engine::updateStoredCue(QString number, const QMap<int, Value> &values, bool replace) {
    if (!cueLibrary.contains(number)) return false;
    for (auto it = values.begin(); it != values.end(); ++it)
        if (!controlExists(it.key()) || !std::isfinite(it->level) || it->level < 0 || it->level > 100 ||
            (it->palette && !palettes.contains(it->palette))) return false;
    if (replace) cueLibrary[number].values = values;
    else for (auto it = values.begin(); it != values.end(); ++it) cueLibrary[number].values[it.key()] = it.value();
    return true;
}
bool Engine::renameStoredCue(QString number, QString replacement) {
    bool valid; double n = replacement.toDouble(&valid);
    if (!cueLibrary.contains(number) || !valid || !std::isfinite(n) || n <= 0 || n > 9999.999) return false;
    replacement = QString::number(n, 'g', 12);
    if (number == replacement) return true;
    if (cueLibrary.contains(replacement)) return false;
    auto cue = cueLibrary.take(number);
    if (cue.label == "cue-" + number) cue.label = "cue-" + replacement;
    cue.number = replacement; cueLibrary[replacement] = cue;
    for (auto &sequence : sequences)
        for (auto &step : sequence.cues) if (step.reference == number) step.reference = replacement;
    for (auto &sequence : linkedPlaybacks)
        for (auto &step : sequence.cues) if (step.reference == number) step.reference = replacement;
    return true;
}
bool Engine::deleteStoredCue(QString number) {
    for (const auto &sequence : sequences)
        for (const auto &step : sequence.cues) if (step.reference == number) return false;
    return cueLibrary.remove(number) > 0;
}
int Engine::insertStoredCue(int sequence, QString number, int position) {
    if (sequence < 0 || sequence >= sequences.size() || !cueLibrary.contains(number)) return -1;
    auto &seq = sequences[sequence];
    Cue step;
    int id = 0;
    for (const auto &existing : seq.cues) id = qMax(id, int(std::ceil(existing.number.toDouble())));
    step.number = QString::number(id + 1); step.reference = number; step.time = defaultFade;
    if (position < 0) position = seq.cues.size();
    if (position > seq.cues.size()) return -1;
    seq.cues.insert(position, step);
    if (seq.current >= position) ++seq.current;
    return position;
}
bool Engine::moveStep(int sequence, int index, int destination) {
    if (sequence < 0 || sequence >= sequences.size()) return false;
    auto &seq = sequences[sequence];
    if (index < 0 || destination < 0 || index >= seq.cues.size() || destination >= seq.cues.size()) return false;
    QString current = seq.current >= 0 ? seq.cues[seq.current].number : QString();
    seq.cues.move(index, destination);
    seq.current = -1;
    for (int i = 0; i < seq.cues.size(); ++i) if (seq.cues[i].number == current) seq.current = i;
    return true;
}
bool Engine::removeStep(int sequence, int index) {
    if (sequence < 0 || sequence >= sequences.size() || index < 0 || index >= sequences[sequence].cues.size()) return false;
    auto &seq = sequences[sequence];
    const QString id = seq.cues[index].number;
    const QString qualified = QString::number(seq.number) + "/" + id;
    if (seq.current == index) release(sequence);
    else if (seq.current > index) --seq.current;
    if (seq.pendingCue == id) seq.pendingCue.clear();
    seq.cues.removeAt(index);
    for (int owner = 0; owner < sequences.size(); ++owner)
        for (auto &step : sequences[owner].cues)
            for (auto field : {&step.link, &step.execute, &step.after})
                if (*field == qualified || (owner == sequence && *field == id)) field->clear();
    return true;
}
Values Engine::tracked(int s, int c) const {
    Values out;
    if (s < 0 || s >= sequences.size())
        return out;
    const auto &seq = sequences[s];
    for (int i = 0; i <= c && i < seq.cues.size(); ++i) {
        for (auto it = seq.cues[i].blocked.begin(); it != seq.cues[i].blocked.end(); ++it)
            out[it.key()] = resolve(it.key(), it.value());
        auto values = cueValues(seq.cues[i]);
        for (auto it = values.begin(); it != values.end(); ++it)
            out[it.key()] = resolve(it.key(), it.value());
    }
    return out;
}
Values Engine::output(bool mastered, bool includePark) const {
    Values out;
    QMap<int, QPair<int, quint64>> owners;
    auto compose = [&](const Sequence &s) {
        for (auto it = s.output.begin(); it != s.output.end(); ++it) {
            auto rank = qMakePair(s.priority, s.ownership.value(it.key(), s.stamp));
            if (!owners.contains(it.key()) || rank > owners[it.key()]) {
                owners[it.key()] = rank;
                out[it.key()] = it.value();
            }
        }
    };
    for (const auto &s : sequences)
        compose(s);
    for (const auto &s : linkedPlaybacks)
        compose(s);
    for (const auto &sub : submasters)
        for (auto it = sub.values.begin(); it != sub.values.end(); ++it)
            out[it.key()] = qMax(out.value(it.key()), it.value() * sub.level);
    for (auto it = pixels.begin(); it != pixels.end(); ++it)
        out[it.key()] = it.value();
    for (auto it = manual.begin(); it != manual.end(); ++it)
        out[it.key()] = resolve(it.key(), it.value());
    for (int key : controlIds())
        if (key < 0 && !out.contains(key)) out[key] = controlDefault(key);
    if (mastered)
        for (auto it = out.begin(); it != out.end(); ++it)
            if (it.key() > 0) it.value() = blackout ? 0 : qBound(0., it.value() * master, 100.);
    if (includePark)
        for (auto it = parked.begin(); it != parked.end(); ++it)
            out[it.key()] = it.value();
    return out;
}
void Engine::toggleCapture() {
    QList<int> controls;
    for (int key : controlIds()) if (selection.contains(channelForControl(key))) controls << key;
    bool any = false;
    for (int key : controls) if (captured.contains(key)) any = true;
    const auto live = output(false, false);
    for (int key : controls) {
        if (any) captured.remove(key);
        else {
            captured.insert(key);
            if (!manual.contains(key)) manual[key] = {live.value(key, controlDefault(key)), 0};
        }
    }
}
void Engine::parkSelection(bool enabled) {
    const auto live = output();
    for (int key : controlIds()) if (selection.contains(channelForControl(key))) {
        if (enabled) parked[key] = live.value(key, controlDefault(key));
        else parked.remove(key);
    }
}
void Engine::setBlock(int s, int cue, bool enabled) {
    if (s < 0 || s >= sequences.size() || cue < 0 || cue >= sequences[s].cues.size())
        return;
    auto &c = sequences[s].cues[cue];
    if (enabled) {
        QMap<int, Value> refs;
        for (int i = 0; i <= cue; ++i) {
            for (auto it = sequences[s].cues[i].blocked.begin();
                 it != sequences[s].cues[i].blocked.end(); ++it)
                refs[it.key()] = it.value();
            auto values = cueValues(sequences[s].cues[i]);
            for (auto it = values.begin(); it != values.end(); ++it)
                refs[it.key()] = it.value();
        }
        c.blocked = refs;
    } else
        c.blocked.clear();
    c.block = enabled;
}
void Engine::editBlind(int s, int cue, int channel, Value value, bool only) {
    if (s < 0 || s >= sequences.size() || cue < 0 || cue >= sequences[s].cues.size() ||
        !controlExists(channel))
        return;
    auto before = tracked(s, cue);
    auto &list = sequences[s].cues;
    list[cue].suppressed.remove(channel);
    list[cue].values[channel] = value;
    if (list[cue].blocked.contains(channel)) list[cue].blocked[channel] = value;
    if (only && cue + 1 < list.size() && !cueValues(list[cue + 1]).contains(channel) &&
        !list[cue + 1].blocked.contains(channel))
        list[cue + 1].values[channel] = {before.value(channel, controlDefault(channel)), 0};
}
void Engine::level(double p) {
    for (int c : selection)
        manual[c] = {qBound(0., p, 100.), 0};
}
void Engine::applyPalette(int id) {
    if (!palettes.contains(id))
        return;
    for (int c : selection)
        if (palettes[id].contains(c))
            manual[c] = {palettes[id][c], id};
}
void Engine::recordPalette(int id) {
    auto o = output(false, false);
    Values p;
    for (int c : selection)
        p[c] = o.value(c);
    if (!p.isEmpty())
        palettes[id] = p;
}
void Engine::record(int s, QString number, QString label, double time, bool only) {
    if (s < 0 || s >= sequences.size())
        return;
    auto &list = sequences[s].cues;
    int idx = -1;
    for (int i = 0; i < list.size(); ++i)
        if (list[i].number.toDouble() == number.toDouble()) { idx = i; break; }
    bool exists = idx >= 0;
    if (!exists) {
        idx = 0;
        const bool inlineList = std::all_of(list.begin(), list.end(), [](const Cue &cue) { return cue.reference.isEmpty(); });
        if (inlineList) while (idx < list.size() && list[idx].number.toDouble() < number.toDouble()) ++idx;
        else idx = list.size();
    }
    auto before = tracked(s, idx - 1);
    auto after = tracked(s, exists ? idx : idx - 1);
    Cue cue;
    if (exists)
        cue = list[idx];
    cue.number = number;
    cue.label = label;
    cue.time = qMax(0., time);
    for (auto it = manual.begin(); it != manual.end(); ++it)
        cue.values[it.key()] = it.value();
    if (exists)
        list[idx] = cue;
    else
        list.insert(idx, cue);
    if (only && idx + 1 < list.size())
        for (auto it = manual.begin(); it != manual.end(); ++it)
            if (!list[idx + 1].values.contains(it.key()))
                list[idx + 1].values[it.key()] = {after.value(it.key(), before.value(it.key())), 0};
    // Editing invalidates a running list's positional index, but never silently fires a cue.
    if (!exists && sequences[s].current >= idx)
        ++sequences[s].current;
}
void Engine::recordLook(int s, QString number, QString label, double time, bool only) {
    if (s < 0 || s >= sequences.size())
        return;
    auto live = output(false, false);
    auto saved = manual;
    for (int key : controlIds())
        if (!manual.contains(key)) manual[key] = {live.value(key), 0};
    bool replacing = false;
    for (const auto &c : sequences[s].cues)
        if (c.number == number)
            replacing = true;
    record(s, number, label, time, only);
    if (!replacing) {
        auto &list = sequences[s].cues;
        int index = 0;
        while (index < list.size() && list[index].number != number)
            ++index;
        if (index > 0 && index < list.size()) {
            auto previous = tracked(s, index - 1);
            auto &values = list[index].values;
            for (auto it = values.begin(); it != values.end();) {
                if (!it->palette && previous.contains(it.key()) &&
                    qAbs(previous[it.key()] - it->level) < 1e-9)
                    it = values.erase(it);
                else
                    ++it;
            }
        }
    }
    manual = saved;
}
QString Engine::nextCueNumber(int s) const {
    if (s < 0 || s >= sequences.size() || sequences[s].cues.isEmpty())
        return "10";
    return QString::number(sequences[s].cues.last().number.toDouble() + 10, 'g', 12);
}
int Engine::addChannelPage() {
    const int page = channels.isEmpty() ? 0 : (channels.lastKey() - 1) / ChannelsPerPage + 1;
    const int first = page * ChannelsPerPage + 1;
    if (first + ChannelsPerPage - 1 > 10000) return -1;
    QSet<int> used;
    for (int channel : channels.keys())
        for (int address : occupiedAddresses(channel)) used.insert(address);
    int address = 1;
    for (int channel = first; channel < first + ChannelsPerPage; ++channel) {
        while (used.contains(address)) ++address;
        channels[channel] = QString("Channel %1").arg(channel);
        patch[channel] = address;
        used.insert(address++);
    }
    return page;
}
int Engine::addSequence() {
    int number = 0;
    for (const auto &s : sequences)
        number = qMax(number, s.number);
    Sequence s;
    s.number = number + 1;
    s.name = QString("sequence-%1").arg(s.number);
    sequences << s;
    return sequences.size() - 1;
}
bool Engine::removeSequence(int index) {
    if (index < 0 || index >= sequences.size() || sequences.size() == 1) return false;
    const QString prefix = QString::number(sequences[index].number) + "/";
    release(index);
    sequences.removeAt(index);
    for (auto &seq : sequences)
        for (auto &cue : seq.cues)
            for (auto field : {&cue.link, &cue.execute, &cue.after})
                if (field->startsWith(prefix)) field->clear();
    QMap<int, Sequence> remapped;
    for (auto it = linkedPlaybacks.begin(); it != linkedPlaybacks.end(); ++it)
        if (it.key() != index) remapped[it.key() > index ? it.key() - 1 : it.key()] = it.value();
    linkedPlaybacks = remapped;
    commandSequence = qBound(0, commandSequence > index ? commandSequence - 1 : commandSequence,
                             int(sequences.size()) - 1);
    commandCue = -1;
    return true;
}
int Engine::nextCueIndex(int s) const {
    if (s < 0 || s >= sequences.size()) return -1;
    const auto &seq = sequences[s];
    if (!seq.pendingCue.isEmpty())
        for (int i = 0; i < seq.cues.size(); ++i)
            if (seq.cues[i].number == seq.pendingCue) return i;
    if (seq.current >= 0 && !seq.cues[seq.current].link.isEmpty()) {
        auto target = cueTarget(seq.cues[seq.current].link, s);
        return target.first == s ? target.second : -1;
    }
    return seq.current + 1 < seq.cues.size() ? seq.current + 1 : -1;
}
bool Engine::setNextCue(int s, int cue) {
    if (s < 0 || s >= sequences.size() || cue < 0 || cue >= sequences[s].cues.size()) return false;
    sequences[s].pendingCue = sequences[s].cues[cue].number;
    return true;
}
void Engine::recordSubmaster() {
    Submaster s;
    for (const auto &existing : submasters)
        s.number = qMax(s.number, existing.number + 1);
    s.name = QString("sub-%1").arg(s.number);
    auto live = output(false, false);
    if (selection.isEmpty()) {
        for (int key : channels.keys()) s.values[key] = live.value(key);
    }
    else
        for (int c : selection)
            s.values[c] = live.value(c);
    submasters << s;
}
QPair<int, int> Engine::cueTarget(QString text, int owner) const {
    int source = owner;
    QString number = text;
    if (text.contains('/')) {
        auto parts = text.split('/');
        if (parts.size() != 2)
            return {-1, -1};
        source = -1;
        number = parts[1];
        for (int i = 0; i < sequences.size(); ++i)
            if (sequences[i].number == parts[0].toInt())
                source = i;
    }
    if (source < 0 || source >= sequences.size())
        return {-1, -1};
    for (int i = 0; i < sequences[source].cues.size(); ++i)
        if (sequences[source].cues[i].number.toDouble() == number.toDouble())
            return {source, i};
    return {-1, -1};
}
void Engine::go(int s, int idx) {
    if (s < 0 || s >= sequences.size() || triggerDepth >= 32)
        return;
    bool assertAll = idx >= 0 || sequences[s].current < 0;
    if (idx < 0) {
        auto &seq = sequences[s];
        if (!seq.pendingCue.isEmpty()) {
            idx = nextCueIndex(s);
            seq.pendingCue.clear();
            assertAll = true;
        } else if (seq.current >= 0 && !seq.cues[seq.current].link.isEmpty()) {
            auto target = cueTarget(seq.cues[seq.current].link, s);
            if (target.first < 0)
                return;
            s = target.first;
            idx = target.second;
            assertAll = true;
        } else
            idx = seq.current + 1;
    }
    auto &seq = sequences[s];
    if (idx < 0 || idx >= seq.cues.size())
        return;
    seq.pendingCue.clear();
    beginPlayback(seq, idx, tracked(s, idx), assertAll);
    seq.linkPending = !seq.cues[idx].after.isEmpty();
    seq.autoPending = seq.cues[idx].follow.has_value() || seq.cues[idx].hang.has_value();
    QString execute = seq.cues[idx].execute;
    if (!execute.isEmpty()) {
        auto target = cueTarget(execute, s);
        if (target.first >= 0) {
            ++triggerDepth;
            go(target.first, target.second);
            --triggerDepth;
        }
    }
    if (!ticking && triggerDepth == 0)
        tick(0);
}
void Engine::beginPlayback(Sequence &seq, int idx, Values target, bool assertAll) {
    auto live = output(false, false);
    seq.current = idx;
    const auto &cue = seq.cues[idx];
    assertAll = assertAll || cue.asserted;
    Values previous;
    for (int i = 0; i < idx; ++i) {
        for (auto it = seq.cues[i].blocked.begin(); it != seq.cues[i].blocked.end(); ++it)
            previous[it.key()] = resolve(it.key(), it.value());
        auto values = cueValues(seq.cues[i]);
        for (auto it = values.begin(); it != values.end(); ++it)
            previous[it.key()] = resolve(it.key(), it.value());
    }
    const auto instructions = cueValues(cue);
    seq.stamp = ++clock;
    if (assertAll) {
        seq.releasing.clear();
        for (auto it = seq.output.begin(); it != seq.output.end(); ++it)
            if (!target.contains(it.key())) {
                target[it.key()] = controlDefault(it.key());
                seq.releasing.insert(it.key());
            }
        seq.target.clear();
        seq.timings.clear();
        seq.channelElapsed.clear();
    }
    for (auto it = target.begin(); it != target.end(); ++it) {
        bool blockMove =
            cue.blocked.contains(it.key()) && qAbs(previous.value(it.key()) - it.value()) > 1e-9;
        if (!assertAll && !instructions.contains(it.key()) && !blockMove)
            continue;
        if (instructions.contains(it.key()))
            seq.releasing.remove(it.key());
        seq.start[it.key()] = live.value(it.key(), controlDefault(it.key()));
        seq.target[it.key()] = it.value();
        seq.timings[it.key()] = cue.channelTimes.value(
            it.key(), ChannelTiming{cue.time, cue.down < 0 ? cue.time : cue.down, cue.wait});
        if (it.key() < 0) {
            int channel = channelForControl(it.key()), index = (-qint64(it.key()) - 1) % 512;
            if (fixtureMode(channel).parameters.value(index).snap) seq.timings[it.key()].up = seq.timings[it.key()].down = 0;
        }
        seq.channelElapsed[it.key()] = 0;
        seq.ownership[it.key()] = seq.stamp;
        if (!captured.contains(it.key()))
            manual.remove(it.key());
        seq.output[it.key()] = live.value(it.key(), controlDefault(it.key()));
    }
    seq.duration = 0;
    for (auto it = seq.target.begin(); it != seq.target.end(); ++it) {
        auto t = seq.timings.value(it.key());
        double fade = it.value() >= seq.start.value(it.key()) ? t.up : t.down;
        seq.duration =
            qMax(seq.duration, qMax(0., t.wait + fade - seq.channelElapsed.value(it.key())));
    }
    seq.elapsed = 0;
    seq.paused = false;
}
void Engine::back(int s) {
    if (s >= 0 && s < sequences.size()) {
        if (sequences[s].current > 0)
            go(s, sequences[s].current - 1);
        else
            release(s);
    }
}
void Engine::release(int s) {
    if (s >= 0 && s < sequences.size()) {
        auto &q = sequences[s];
        q.output.clear();
        q.target.clear();
        q.start.clear();
        q.current = -1;
        q.pendingCue.clear();
        q.duration = 0;
        q.timings.clear();
        q.channelElapsed.clear();
        q.ownership.clear();
        q.releasing.clear();
        q.linkPending = false;
        q.autoPending = false;
        linkedPlaybacks.remove(s);
    }
}
void Engine::tick(double dt) {
    if (ticking)
        return;
    ticking = true;
    auto advance = [](Sequence &s, double dt) {
        if (s.paused || s.current < 0)
            return;
        s.elapsed += dt;
        for (auto it = s.target.begin(); it != s.target.end(); ++it) {
            auto t = s.timings.value(it.key());
            s.channelElapsed[it.key()] += dt;
            double elapsed = s.channelElapsed[it.key()];
            double start = s.start.value(it.key()), fade = it.value() >= start ? t.up : t.down;
            double f =
                elapsed < t.wait ? 0 : (fade <= 0 ? 1 : qBound(0., (elapsed - t.wait) / fade, 1.));
            s.output[it.key()] = start + (it.value() - start) * f;
        }
        if (s.elapsed >= s.duration)
            for (int c : s.releasing)
                s.output.remove(c);
    };
    auto autoTime = [](const Sequence &s) {
        const auto &c = s.cues[s.current];
        return c.follow.value_or(s.duration + c.hang.value_or(0));
    };
    double remaining = qMax(0., dt);
    for (int pass = 0; pass < 256; ++pass) {
        double step = remaining;
        for (const auto &s : sequences) {
            if (s.paused || s.current < 0)
                continue;
            if (s.linkPending)
                step = qMin(step, qMax(0., s.duration - s.elapsed));
            if (s.autoPending)
                step = qMin(step, qMax(0., autoTime(s) - s.elapsed));
        }
        for (auto &s : sequences)
            advance(s, step);
        for (auto &s : linkedPlaybacks)
            advance(s, step);
        remaining = qMax(0., remaining - step);
        bool fired = false;
        for (int owner = 0; owner < sequences.size(); ++owner) {
            auto &s = sequences[owner];
            if (s.paused || s.current < 0)
                continue;
            if (s.linkPending && s.elapsed + 1e-9 >= s.duration) {
                s.linkPending = false;
                auto target = cueTarget(s.cues[s.current].after, owner);
                if (target.first >= 0) {
                    Sequence side;
                    side.name = QString("After %1/%2")
                                    .arg(sequences[target.first].number)
                                    .arg(sequences[target.first].cues[target.second].number);
                    side.priority = sequences[target.first].priority;
                    side.cues = sequences[target.first].cues;
                    beginPlayback(side, target.second, tracked(target.first, target.second));
                    advance(side, 0);
                    linkedPlaybacks[owner] = side;
                }
                fired = true;
            }
            if (s.autoPending && s.elapsed + 1e-9 >= qMax(0., autoTime(s))) {
                s.autoPending = false;
                go(owner);
                fired = true;
            }
        }
        if (!fired && remaining <= 1e-9)
            break;
        if (pass == 255) {
            for (auto &s : sequences)
                if (s.autoPending) {
                    s.paused = true;
                    s.autoPending = false;
                }
        }
    }
    ticking = false;
}
bool Engine::command(QString text, QString *error) {
    auto fail = [&](QString e) {
        if (error)
            *error = e;
        return false;
    };
    text = text.trimmed().toLower();
    text.remove(QRegularExpression("\\s+enter$"));
    if (commandSequence < 0 || commandSequence >= sequences.size())
        commandSequence = 0;
    if (text == "go") {
        go(commandSequence);
        return true;
    }
    if (text == "back") {
        back(commandSequence);
        return true;
    }
    if (text == "pause") {
        sequences[commandSequence].paused = true;
        return true;
    }
    if (text == "resume") {
        sequences[commandSequence].paused = false;
        return true;
    }
    if (text == "capture") {
        toggleCapture();
        return true;
    }
    if (text == "park") {
        parkSelection(true);
        return true;
    }
    if (text == "unpark") {
        parkSelection(false);
        return true;
    }
    if (text == "home") {
        for (int key : controlIds()) if (selection.contains(channelForControl(key))) manual[key] = {controlDefault(key), 0};
        return true;
    }
    if (text == "release") {
        for (auto it = manual.begin(); it != manual.end();)
            if (!captured.contains(it.key()) &&
                (selection.isEmpty() || selection.contains(channelForControl(it.key()))))
                it = manual.erase(it);
            else
                ++it;
        return true;
    }
    if (text == "select active" || text == "select manual") {
        selection.clear();
        if (text == "select manual") {
            for (int c : manual.keys())
                selection.insert(channelForControl(c));
        } else {
            auto live = output();
            for (auto it = live.begin(); it != live.end(); ++it)
                if (it.key() > 0 && it.value() > 0)
                    selection.insert(it.key());
        }
        return true;
    }
    if (text == "block" || text == "assert") {
        if (commandCue < 0 || commandCue >= sequences[commandSequence].cues.size())
            return fail("Select a step first");
        auto &cue = sequences[commandSequence].cues[commandCue];
        if (text == "block")
            setBlock(commandSequence, commandCue, !cue.block);
        else
            cue.asserted = !cue.asserted;
        return true;
    }
    auto insert = QRegularExpression("^insert cue (\\d+(?:\\.\\d+)?)$").match(text);
    if (insert.hasMatch()) {
        QString number = QString::number(insert.captured(1).toDouble(), 'g', 12);
        int row = insertStoredCue(commandSequence, number);
        if (row < 0) return fail("Stored cue not found");
        commandCue = row; return true;
    }
    auto recordCommand =
        QRegularExpression(
            "^(record only|record|update)(?: cue)?(?: (\\d+(?:/\\d+)?(?:\\.\\d+)?))?$")
            .match(text);
    if (recordCommand.hasMatch()) {
        QString action = recordCommand.captured(1), target = recordCommand.captured(2);
        int seq = commandSequence;
        QString number = target;
        if (!target.contains('/')) {
            if (number.isEmpty()) {
                if (action == "update" && commandCue >= 0 && commandCue < sequences[seq].cues.size())
                    number = sequences[seq].cues[commandCue].reference;
                else number = nextStoredCueNumber();
            }
            bool valid; double n = number.toDouble(&valid);
            if (!valid || !std::isfinite(n) || n <= 0 || n > 9999.999) return fail("Invalid stored cue number");
            number = QString::number(n, 'g', 12);
            if (action == "update") {
                if (!updateStoredCue(number, manual)) return fail("Stored cue not found");
            } else {
                QString label = cueLibrary.contains(number) ? cueLibrary[number].label : QString();
                if (!storeCue(number, label, action == "record only")) return fail("Unable to store cue");
            }
            return true;
        }
        if (target.contains('/')) {
            auto parts = target.split('/');
            seq = -1;
            for (int i = 0; i < sequences.size(); ++i)
                if (sequences[i].number == parts[0].toInt())
                    seq = i;
            number = parts[1];
            if (seq < 0)
                return fail("Sequence not found");
        }
        if (number.isEmpty())
            number =
                action == "update" && commandCue >= 0 && commandCue < sequences[seq].cues.size()
                    ? sequences[seq].cues[commandCue].number
                    : nextCueNumber(seq);
        bool ok;
        double n = number.toDouble(&ok);
        if (!ok || n <= 0 || n > 9999.999)
            return fail("Invalid cue number");
        QString label = "cue-" + number;
        double time = defaultFade;
        int existing = -1;
        for (int i = 0; i < sequences[seq].cues.size(); ++i)
            if (sequences[seq].cues[i].number.toDouble() == n)
                existing = i;
        if (action == "update") {
            if (existing < 0)
                return fail("Cue not found");
            label = sequences[seq].cues[existing].label;
            time = sequences[seq].cues[existing].time;
        }
        if (action == "record")
            recordLook(seq, number, label, time, cueOnlyMode);
        else
            record(seq, number, label, time, cueOnlyMode);
        commandSequence = seq;
        for (int i = 0; i < sequences[seq].cues.size(); ++i)
            if (sequences[seq].cues[i].number.toDouble() == n)
                commandCue = i;
        return true;
    }
    auto target =
        QRegularExpression(
            "^(go to cue|goto "
            "cue|go to step|goto step|step|sequence|group|intensity)\\s+(\\d+(?:\\.\\d+)?(?:/\\d+(?:\\.\\d+)?)?)$")
            .match(text);
    if (target.hasMatch()) {
        QString type = target.captured(1), id = target.captured(2);
        if (type == "group") {
            if (!groups.contains(id.toInt()))
                return fail("Group not found");
            selection.clear();
            for (int c : groups[id.toInt()].keys())
                selection.insert(channelForControl(c));
            return true;
        }
        if (type == "intensity") {
            if (!palettes.contains(id.toInt()))
                return fail("Palette not found");
            applyPalette(id.toInt());
            return true;
        }
        if (type == "sequence") {
            for (int i = 0; i < sequences.size(); ++i)
                if (sequences[i].number == id.toInt()) {
                    commandSequence = i;
                    commandCue = -1;
                    return true;
                }
            return fail("Sequence not found");
        }
        QPair<int, int> destination{-1, -1};
        const bool stepSyntax = type.contains("step");
        int seq = commandSequence;
        QString number = id;
        if (id.contains('/')) {
            seq = -1; number = id.section('/', 1);
            for (int i = 0; i < sequences.size(); ++i)
                if (sequences[i].number == id.section('/', 0, 0).toInt()) seq = i;
        }
        if (!stepSyntax && seq >= 0) {
            int matches = 0;
            for (int i = 0; i < sequences[seq].cues.size(); ++i)
                if (sequences[seq].cues[i].reference == number ||
                    (sequences[seq].cues[i].reference.isEmpty() && sequences[seq].cues[i].number.toDouble() == number.toDouble()))
                    { destination = {seq, i}; ++matches; }
            if (matches > 1) return fail("Cue has several occurrences. Use Step or Go To Step with its step ID.");
        }
        if (stepSyntax) destination = cueTarget(id, commandSequence);
        if (destination.first < 0)
            return fail("Cue not found");
        commandSequence = destination.first;
        commandCue = destination.second;
        if (type != "cue" && type != "step")
            go(destination.first, destination.second);
        return true;
    }
    text.remove(QRegularExpression("^(channel|chan)\\s+"));
    text.replace("full", "100");
    text.replace("out", "0");
    text.replace('@', " at ");
    auto parts = text.split(QRegularExpression("\\s*\\bat\\b\\s*"));
    if (parts.size() > 2)
        return fail("Use one At value");
    QString list = parts[0].trimmed();
    QSet<int> selected = selection;
    if (!list.isEmpty()) {
        selected.clear();
        int offset = 0;
        bool subtract = false;
        auto expression = QRegularExpression("\\G\\s*(\\d+)(?:\\s+thru\\s+(\\d+))?\\s*");
        if (list.startsWith('+') || list.startsWith('-')) {
            selected = selection;
            subtract = list[0] == '-';
            offset = 1;
        }
        while (offset < list.size()) {
            auto match = expression.match(list, offset);
            if (!match.hasMatch())
                return fail("Invalid channel selection");
            int from = match.captured(1).toInt(),
                to = match.captured(2).isEmpty() ? from : match.captured(2).toInt();
            if (from > to || to - from > 10000)
                return fail("Invalid channel range");
            for (int c = from; c <= to; ++c) {
                if (!channels.contains(c))
                    return fail("Channel not patched");
                if (subtract)
                    selected.remove(c);
                else
                    selected.insert(c);
            }
            offset = match.capturedEnd();
            if (offset < list.size()) {
                QChar op = list[offset++];
                if (op != '+' && op != '-')
                    return fail("Use + or - between channels");
                subtract = op == '-';
                if (offset == list.size())
                    return fail("Missing channel after operator");
            }
        }
    }
    double levelValue = 0;
    bool relative = false;
    if (parts.size() == 2) {
        QString input = parts[1].trimmed();
        bool ok;
        levelValue = input.toDouble(&ok);
        relative = input.startsWith('+') || input.startsWith('-');
        if (!ok || !std::isfinite(levelValue) || qAbs(levelValue) > 100 ||
            (!relative && levelValue < 0))
            return fail("Level must be between 0 and 100");
    }
    if (list.isEmpty() && parts.size() == 1)
        return fail("Enter a command");
    auto live = output(false, false);
    selection = selected;
    if (parts.size() == 2) {
        if (relative)
            for (int c : selection)
                manual[c] = {qBound(0., live.value(c) + levelValue, 100.), 0};
        else
            level(levelValue);
    }
    return true;
}

QJsonObject Engine::json() const {
    QJsonObject root;
    root["format"] = "cold-flame";
    root["version"] = 3;
    QJsonObject profiles;
    for (auto it = fixtureProfiles.begin(); it != fixtureProfiles.end(); ++it) profiles[it.key()] = fixtureJson(it.value());
    root["fixtureProfiles"] = profiles;
    QJsonArray ch, ps, gs, ss;
    for (auto it = channels.begin(); it != channels.end(); ++it) {
        QJsonArray addresses;
        for (int a : dmxAddresses(it.key()))
            addresses.append(a);
        ch.append(QJsonObject{{"id", it.key()},
                              {"name", it.value()},
                              {"address", patch.value(it.key())},
                              {"profile", fixtures.value(it.key()).profile},
                              {"mode", fixtures.value(it.key()).mode},
                              {"addresses", addresses}});
    }
    auto saveValues = [](const QMap<int, Values> &source) {
        QJsonArray a;
        for (auto i = source.begin(); i != source.end(); ++i) {
            QJsonObject v;
            for (auto j = i->begin(); j != i->end(); ++j)
                v[QString::number(j.key())] = j.value();
            a.append(QJsonObject{{"id", i.key()}, {"values", v}});
        }
        return a;
    };
    for (const auto &s : sequences) {
        QJsonArray cs;
        for (const auto &c : s.cues) {
            QJsonObject vs;
            for (auto i = c.values.begin(); i != c.values.end(); ++i)
                vs[QString::number(i.key())] =
                    QJsonObject{{"level", i->level}, {"palette", i->palette}};
            QJsonObject times;
            for (auto it = c.channelTimes.begin(); it != c.channelTimes.end(); ++it)
                times[QString::number(it.key())] =
                    QJsonObject{{"up", it->up}, {"down", it->down}, {"wait", it->wait}};
            QJsonObject blocked;
            for (auto it = c.blocked.begin(); it != c.blocked.end(); ++it)
                blocked[QString::number(it.key())] =
                    QJsonObject{{"level", it->level}, {"palette", it->palette}};
            QJsonArray suppressed; for (int key : c.suppressed) suppressed << key;
            cs.append(QJsonObject{{"suppressed", suppressed}, {"blocked", blocked},
                                  {"number", c.number},
                                  {"reference", c.reference},
                                  {"block", c.block},
                                  {"assert", c.asserted},
                                  {"label", c.label},
                                  {"time", c.time},
                                  {"down", c.down},
                                  {"wait", c.wait},
                                  {"link", c.link},
                                  {"execute", c.execute},
                                  {"after", c.after},
                                  {"follow", c.follow ? QJsonValue(*c.follow) : QJsonValue()},
                                  {"hang", c.hang ? QJsonValue(*c.hang) : QJsonValue()},
                                  {"channelTimes", times},
                                  {"values", vs}});
        }
        ss.append(QJsonObject{
            {"number", s.number}, {"name", s.name}, {"priority", s.priority}, {"cues", cs}});
    }
    root["channels"] = ch;
    root["palettes"] = saveValues(palettes);
    root["groups"] = saveValues(groups);
    root["sequences"] = ss;
    QJsonArray library;
    for (const auto &cue : cueLibrary) {
        QJsonObject values;
        for (auto it = cue.values.begin(); it != cue.values.end(); ++it)
            values[QString::number(it.key())] = QJsonObject{{"level", it->level}, {"palette", it->palette}};
        library.append(QJsonObject{{"number", cue.number}, {"label", cue.label}, {"values", values}});
    }
    root["cueLibrary"] = library;
    QJsonArray subs;
    for (const auto &sub : submasters) {
        QJsonObject values;
        for (auto it = sub.values.begin(); it != sub.values.end(); ++it)
            values[QString::number(it.key())] = it.value();
        subs.append(QJsonObject{{"number", sub.number}, {"name", sub.name}, {"values", values}});
    }
    root["submasters"] = subs;
    return root;
}
bool Engine::load(const QJsonObject &r, QString *error) {
    auto fail = [&]() {
        if (error)
            *error = "Invalid show file or unsupported version";
        return false;
    };
    if (r["format"] != "cold-flame" || (r["version"].toInt() < 1 || r["version"].toInt() > 3) || !r["channels"].isArray() ||
        r["sequences"].toArray().isEmpty())
        return fail();
    Engine e;
    if (r.contains("fixtureProfiles")) {
        const auto profiles = r["fixtureProfiles"].toObject();
        for (auto it = profiles.begin(); it != profiles.end(); ++it) {
            FixtureProfile profile; QString issue;
            if (!fixtureFromJson(it.value().toObject(), profile, issue)) return fail();
            e.fixtureProfiles[it.key()] = profile;
        }
    }
    e.channels.clear();
    e.patch.clear();
    e.sequences.clear();
    QSet<int> addresses;
    for (auto x : r["channels"].toArray()) {
        auto o = x.toObject();
        int id = o["id"].toInt();
        if (id < 1 || id > 10000 || e.channels.contains(id))
            return fail();
        QList<int> targets;
        if (o.contains("addresses")) {
            if (!o["addresses"].isArray())
                return fail();
            for (auto value : o["addresses"].toArray()) {
                int a = value.toInt(-1);
                if (a < 1 || a > 512 * 63999 || addresses.contains(a))
                    return fail();
                addresses.insert(a);
                targets << a;
            }
        } else {
            int a = o["address"].toInt();
            if (a < 1 || a > 512 * 63999 || addresses.contains(a))
                return fail();
            addresses.insert(a);
            targets << a;
        }
        e.channels[id] = o["name"].toString();
        FixtureAssignment assignment{o["profile"].toString("dimmer8"), o["mode"].toInt()};
        if (!e.fixtureProfiles.contains(assignment.profile) || assignment.mode < 0 || assignment.mode >= e.fixtureProfiles[assignment.profile].modes.size()) return fail();
        e.fixtures[id] = assignment;
        if (!targets.isEmpty()) e.patch[id] = targets.takeFirst();
        e.additionalPatch[id] = targets;
    }
    if (!e.patchErrors().isEmpty()) return fail();
    for (QString key : {"palettes", "groups"})
        for (auto x : r[key].toArray()) {
            auto o = x.toObject();
            Values v;
            auto vo = o["values"].toObject();
            for (auto it = vo.begin(); it != vo.end(); ++it) {
                double n = it.value().toDouble(-1);
                if (!e.controlExists(it.key().toInt()) || n < 0 || n > 100)
                    return fail();
                v[it.key().toInt()] = n;
            }
            (key == "palettes" ? e.palettes : e.groups)[o["id"].toInt()] = v;
        }
    for (auto entry : r["cueLibrary"].toArray()) {
        auto object = entry.toObject(); StoredCue cue;
        bool ok; double number = object["number"].toString().toDouble(&ok);
        if (!ok || !std::isfinite(number) || number <= 0 || number > 9999.999) return fail();
        cue.number = QString::number(number, 'g', 12);
        if (e.cueLibrary.contains(cue.number)) return fail();
        cue.label = object["label"].toString();
        auto values = object["values"].toObject();
        for (auto it = values.begin(); it != values.end(); ++it) {
            auto value = it.value().toObject();
            double level = value["level"].toDouble(-1); int palette = value["palette"].toInt();
            if (!e.controlExists(it.key().toInt()) || !std::isfinite(level) || level < 0 || level > 100 ||
                (palette && !e.palettes.contains(palette))) return fail();
            cue.values[it.key().toInt()] = {level, palette};
        }
        e.cueLibrary[cue.number] = cue;
    }
    for (auto x : r["sequences"].toArray()) {
        auto o = x.toObject();
        Sequence s;
        s.name = o["name"].toString();
        s.number = o["number"].toInt(e.sequences.size() + 1);
        for (const auto &existing : e.sequences)
            if (existing.number == s.number)
                return fail();
        if (s.number < 1)
            return fail();
        s.priority = o["priority"].toInt(4);
        double prev = -1;
        QSet<double> stepNumbers;
        for (auto y : o["cues"].toArray()) {
            auto co = y.toObject();
            Cue c;
            c.number = co["number"].toString();
            c.reference = co["reference"].toString();
            if (!c.reference.isEmpty() && !e.cueLibrary.contains(c.reference)) return fail();
            bool ok;
            double n = c.number.toDouble(&ok);
            c.label = co["label"].toString();
            c.block = co["block"].toBool();
            c.asserted = co["assert"].toBool();
            for (auto key : co["suppressed"].toArray()) {
                if (!e.controlExists(key.toInt())) return fail(); c.suppressed.insert(key.toInt());
            }
            auto blocked = co["blocked"].toObject();
            for (auto it = blocked.begin(); it != blocked.end(); ++it) {
                auto o = it.value().toObject();
                double level = o["level"].toDouble(-1);
                int palette = o["palette"].toInt();
                if (!e.controlExists(it.key().toInt()) || level < 0 || level > 100 ||
                    (palette && !e.palettes.contains(palette)))
                    return fail();
                c.blocked[it.key().toInt()] = {level, palette};
            }
            c.time = co["time"].toDouble(-1);
            if (!ok || !std::isfinite(n) || n <= 0 || stepNumbers.contains(n) ||
                (r["version"].toInt() == 1 && n <= prev) || c.time < 0 || !std::isfinite(c.time))
                return fail();
            c.down = co["down"].toDouble(-1);
            c.wait = co["wait"].toDouble(0);
            c.link = co["link"].toString();
            c.execute = co["execute"].toString();
            c.after = co["after"].toString();
            if (co.contains("follow") && !co["follow"].isNull()) {
                double f = co["follow"].toDouble(-1);
                if (!std::isfinite(f) || f < 0)
                    return fail();
                c.follow = f;
            }
            if (co.contains("hang") && !co["hang"].isNull()) {
                double h = co["hang"].toDouble(std::numeric_limits<double>::quiet_NaN());
                if (!std::isfinite(h) || c.follow)
                    return fail();
                c.hang = h;
            }
            if (!std::isfinite(c.down) || c.down < -1 || !std::isfinite(c.wait) || c.wait < 0)
                return fail();
            if (!c.link.isEmpty()) {
                bool valid;
                double target = c.link.section('/', -1).toDouble(&valid);
                if (!valid || target <= 0)
                    return fail();
            }
            auto times = co["channelTimes"].toObject();
            for (auto it = times.begin(); it != times.end(); ++it) {
                auto o = it.value().toObject();
                ChannelTiming t{o["up"].toDouble(-1), o["down"].toDouble(-1),
                                o["wait"].toDouble(-1)};
                if (!e.controlExists(it.key().toInt()) || t.up < 0 || t.down < 0 ||
                    t.wait < 0 || !std::isfinite(t.up) || !std::isfinite(t.down) ||
                    !std::isfinite(t.wait))
                    return fail();
                c.channelTimes[it.key().toInt()] = t;
            }
            prev = n;
            stepNumbers.insert(n);
            auto vs = co["values"].toObject();
            for (auto it = vs.begin(); it != vs.end(); ++it) {
                auto v = it.value().toObject();
                double l = v["level"].toDouble(-1);
                int p = v["palette"].toInt();
                if (!e.controlExists(it.key().toInt()) || l < 0 || l > 100 ||
                    (p && !e.palettes.contains(p)))
                    return fail();
                c.values[it.key().toInt()] = {l, p};
            }
            s.cues << c;
        }
        e.sequences << s;
    }
    for (auto x : r["submasters"].toArray()) {
        auto o = x.toObject();
        Submaster sub;
        sub.number = o["number"].toInt(e.submasters.size() + 1);
        if (sub.number < 1) return fail();
        for (const auto &existing : e.submasters)
            if (existing.number == sub.number) return fail();
        sub.name = o["name"].toString();
        auto values = o["values"].toObject();
        for (auto it = values.begin(); it != values.end(); ++it) {
            double v = it.value().toDouble(-1);
            if (!e.channels.contains(it.key().toInt()) || v < 0 || v > 100)
                return fail();
            sub.values[it.key().toInt()] = v;
        }
        e.submasters << sub;
    }
    *this = e;
    return true;
}
} // namespace Flame
