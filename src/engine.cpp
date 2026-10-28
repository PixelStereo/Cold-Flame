#include "engine.h"
#include <algorithm>
#include <cmath>
namespace Flame {
Engine::Engine() {
    for (int i = 1; i <= 48; ++i) {
        channels[i] = QString("Circuit %1").arg(i);
        patch[i] = i;
    }
    Sequence s;
    s.name = "Conduite principale";
    sequences << s;
}
double Engine::resolve(int channel, Value v) const {
    return v.palette ? palettes.value(v.palette).value(channel, v.level) : v.level;
}
Values Engine::tracked(int s, int c) const {
    Values out;
    if (s < 0 || s >= sequences.size())
        return out;
    const auto &seq = sequences[s];
    for (int i = 0; i <= c && i < seq.cues.size(); ++i)
        for (auto it = seq.cues[i].values.begin(); it != seq.cues[i].values.end(); ++it)
            out[it.key()] = resolve(it.key(), it.value());
    return out;
}
Values Engine::output() const {
    Values out;
    QMap<int, QPair<int, quint64>> owners;
    for (const auto &s : sequences)
        for (auto it = s.output.begin(); it != s.output.end(); ++it) {
            auto rank = qMakePair(s.priority, s.stamp);
            if (!owners.contains(it.key()) || rank > owners[it.key()]) {
                owners[it.key()] = rank;
                out[it.key()] = it.value();
            }
        }
    for (auto it = pixels.begin(); it != pixels.end(); ++it)
        out[it.key()] = it.value();
    for (auto it = manual.begin(); it != manual.end(); ++it)
        out[it.key()] = resolve(it.key(), it.value());
    for (auto it = out.begin(); it != out.end(); ++it)
        it.value() = blackout ? 0 : qBound(0., it.value() * master, 100.);
    return out;
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
    auto o = output();
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
    int idx = 0;
    while (idx < list.size() && list[idx].number.toDouble() < number.toDouble())
        ++idx;
    bool exists = idx < list.size() && list[idx].number == number;
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
void Engine::go(int s, int idx) {
    if (s < 0 || s >= sequences.size())
        return;
    auto &seq = sequences[s];
    if (idx < 0)
        idx = seq.current + 1;
    if (idx < 0 || idx >= seq.cues.size())
        return;
    seq.current = idx;
    seq.start = seq.output;
    seq.target = tracked(s, idx);
    seq.duration = seq.cues[idx].time;
    seq.elapsed = 0;
    seq.paused = false;
    seq.stamp = ++clock;
    if (seq.duration == 0)
        seq.output = seq.target;
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
        q.duration = 0;
    }
}
void Engine::tick(double dt) {
    for (auto &s : sequences) {
        if (s.paused || s.duration <= 0)
            continue;
        s.elapsed = qMin(s.duration, s.elapsed + qMax(0., dt));
        double f = s.elapsed / s.duration;
        for (auto it = s.target.begin(); it != s.target.end(); ++it)
            s.output[it.key()] =
                s.start.value(it.key()) + (it.value() - s.start.value(it.key())) * f;
    }
}
bool Engine::command(QString text, QString *error) {
    auto fail = [&](QString e) {
        if (error)
            *error = e;
        return false;
    };
    text = text.trimmed().toLower();
    auto target =
        QRegularExpression("^(group|intensity|sequence|cue)\\s+(\\d+)(?:/(\\d+(?:\\.\\d+)?))?$")
            .match(text);
    if (target.hasMatch()) {
        int id = target.captured(2).toInt();
        auto type = target.captured(1);
        if (type == "group") {
            if (!groups.contains(id))
                return fail("Groupe absent");
            selection.clear();
            for (auto i = groups[id].begin(); i != groups[id].end(); ++i)
                selection.insert(i.key());
            return true;
        }
        if (type == "intensity") {
            if (!palettes.contains(id))
                return fail("Palette absente");
            applyPalette(id);
            return true;
        }
        if (type == "sequence") {
            if (id < 1 || id > sequences.size())
                return fail("Séquence absente");
            go(id - 1);
            return true;
        }
        int seq = target.captured(3).isEmpty() ? 0 : id - 1;
        QString num = target.captured(3).isEmpty() ? target.captured(2) : target.captured(3);
        if (seq < 0 || seq >= sequences.size())
            return fail("Séquence absente");
        for (int i = 0; i < sequences[seq].cues.size(); ++i)
            if (sequences[seq].cues[i].number.toDouble() == num.toDouble()) {
                go(seq, i);
                return true;
            }
        return fail("Mémoire absente");
    }
    text = text.trimmed().toLower();
    text.replace("channel ", "");
    text.replace("chan ", "");
    text.replace("full", "100");
    text.replace("enter", "");
    auto m =
        QRegularExpression("^(\\d+)(?:\\s+thru\\s+(\\d+))?(?:\\s+at\\s+(\\d+(?:\\.\\d+)?))?\\s*$")
            .match(text);
    if (!m.hasMatch())
        return fail("Syntaxe : Channel 1 Thru 12 At 50");
    int a = m.captured(1).toInt(), b = m.captured(2).isEmpty() ? a : m.captured(2).toInt();
    if (a > b || b - a > 10000)
        return fail("Plage de circuits incorrecte");
    for (int c = a; c <= b; ++c)
        if (!channels.contains(c))
            return fail("Circuit absent du patch");
    if (!m.captured(3).isEmpty() && m.captured(3).toDouble() > 100)
        return fail("Niveau entre 0 et 100");
    selection.clear();
    for (int c = a; c <= b; ++c)
        selection.insert(c);
    if (!m.captured(3).isEmpty())
        level(m.captured(3).toDouble());
    return true;
}
QJsonObject Engine::json() const {
    QJsonObject root;
    root["format"] = "cold-flame";
    root["version"] = 1;
    QJsonArray ch, ps, gs, ss;
    for (auto it = channels.begin(); it != channels.end(); ++it)
        ch.append(QJsonObject{
            {"id", it.key()}, {"name", it.value()}, {"address", patch.value(it.key())}});
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
            cs.append(QJsonObject{
                {"number", c.number}, {"label", c.label}, {"time", c.time}, {"values", vs}});
        }
        ss.append(QJsonObject{{"name", s.name}, {"priority", s.priority}, {"cues", cs}});
    }
    root["channels"] = ch;
    root["palettes"] = saveValues(palettes);
    root["groups"] = saveValues(groups);
    root["sequences"] = ss;
    return root;
}
bool Engine::load(const QJsonObject &r, QString *error) {
    auto fail = [&]() {
        if (error)
            *error = "Fichier spectacle invalide ou version inconnue";
        return false;
    };
    if (r["format"] != "cold-flame" || r["version"].toInt() != 1 || !r["channels"].isArray() ||
        r["sequences"].toArray().isEmpty())
        return fail();
    Engine e;
    e.channels.clear();
    e.patch.clear();
    e.sequences.clear();
    QSet<int> addresses;
    for (auto x : r["channels"].toArray()) {
        auto o = x.toObject();
        int id = o["id"].toInt(), a = o["address"].toInt();
        if (id < 1 || a < 1 || a > 512 * 63999 || e.channels.contains(id) || addresses.contains(a))
            return fail();
        e.channels[id] = o["name"].toString();
        e.patch[id] = a;
        addresses.insert(a);
    }
    for (QString key : {"palettes", "groups"})
        for (auto x : r[key].toArray()) {
            auto o = x.toObject();
            Values v;
            auto vo = o["values"].toObject();
            for (auto it = vo.begin(); it != vo.end(); ++it) {
                double n = it.value().toDouble(-1);
                if (!e.channels.contains(it.key().toInt()) || n < 0 || n > 100)
                    return fail();
                v[it.key().toInt()] = n;
            }
            (key == "palettes" ? e.palettes : e.groups)[o["id"].toInt()] = v;
        }
    for (auto x : r["sequences"].toArray()) {
        auto o = x.toObject();
        Sequence s;
        s.name = o["name"].toString();
        s.priority = o["priority"].toInt(4);
        double prev = -1;
        for (auto y : o["cues"].toArray()) {
            auto co = y.toObject();
            Cue c;
            c.number = co["number"].toString();
            bool ok;
            double n = c.number.toDouble(&ok);
            c.label = co["label"].toString();
            c.time = co["time"].toDouble(-1);
            if (!ok || n <= prev || c.time < 0 || !std::isfinite(c.time))
                return fail();
            prev = n;
            auto vs = co["values"].toObject();
            for (auto it = vs.begin(); it != vs.end(); ++it) {
                auto v = it.value().toObject();
                double l = v["level"].toDouble(-1);
                int p = v["palette"].toInt();
                if (!e.channels.contains(it.key().toInt()) || l < 0 || l > 100 ||
                    (p && !e.palettes.contains(p)))
                    return fail();
                c.values[it.key().toInt()] = {l, p};
            }
            s.cues << c;
        }
        e.sequences << s;
    }
    *this = e;
    return true;
}
} // namespace Flame
