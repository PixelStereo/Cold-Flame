#pragma once
#include "engine.h"
namespace Flame {
// Undo show definitions without rewinding transport, manual levels, Park or Capture.
class EditHistory {
    QList<QJsonObject> states;
    int cursor = -1;
    bool restore(Engine &engine, const QJsonObject &state) {
        Engine definitions;
        if (!definitions.load(state))
            return false;
        auto old = engine.sequences;
        for (auto &next : definitions.sequences)
            for (const auto &running : old)
                if (next.number == running.number) {
                    auto cues = next.cues;
                    auto name = next.name;
                    int priority = next.priority;
                    QString number = running.current >= 0 && running.current < running.cues.size()
                                         ? running.cues[running.current].number
                                         : QString();
                    next = running;
                    next.cues = cues;
                    next.name = name;
                    next.priority = priority;
                    next.current = -1;
                    for (int i = 0; i < cues.size(); ++i)
                        if (cues[i].number == number)
                            next.current = i;
                    if (next.current < 0) {
                        next.output.clear();
                        next.target.clear();
                        next.timings.clear();
                        next.autoPending = false;
                        next.linkPending = false;
                    }
                }
        for (auto &sub : definitions.submasters)
            for (const auto &live : engine.submasters)
                if (sub.number == live.number) sub.level = live.level;
        engine.channels = definitions.channels;
        engine.patch = definitions.patch;
        engine.additionalPatch = definitions.additionalPatch;
        engine.palettes = definitions.palettes;
        engine.groups = definitions.groups;
        QMap<int, Sequence> side;
        for (auto it = engine.linkedPlaybacks.begin(); it != engine.linkedPlaybacks.end(); ++it)
            if (it.key() >= 0 && it.key() < old.size())
                for (int i = 0; i < definitions.sequences.size(); ++i)
                    if (definitions.sequences[i].number == old[it.key()].number)
                        side[i] = it.value();
        engine.linkedPlaybacks = side;
        engine.sequences = definitions.sequences;
        engine.submasters = definitions.submasters;
        return true;
    }

  public:
    void reset(const Engine &engine) {
        states = {engine.json()};
        cursor = 0;
    }
    void commit(const Engine &engine) {
        auto state = engine.json();
        if (cursor >= 0 && states[cursor] == state)
            return;
        while (states.size() > cursor + 1)
            states.removeLast();
        states << state;
        if (states.size() > 100)
            states.removeFirst();
        cursor = states.size() - 1;
    }
    bool undo(Engine &engine) {
        if (cursor <= 0)
            return false;
        if (!restore(engine, states[cursor - 1]))
            return false;
        --cursor;
        return true;
    }
    bool redo(Engine &engine) {
        if (cursor + 1 >= states.size())
            return false;
        if (!restore(engine, states[cursor + 1]))
            return false;
        ++cursor;
        return true;
    }
};
} // namespace Flame
