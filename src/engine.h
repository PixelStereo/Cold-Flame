#pragma once
#include <QtCore>
namespace Flame {
using Values = QMap<int, double>;
struct Value {
    double level = 0;
    int palette = 0;
};
struct Cue {
    QString number, label;
    double time = 2;
    QMap<int, Value> values;
    bool block = false;
};
struct Sequence {
    QString name;
    int priority = 4;
    QList<Cue> cues;
    int current = -1;
    Values output, start, target;
    double elapsed = 0, duration = 0;
    quint64 stamp = 0;
    bool paused = false;
};
class Engine {
  public:
    QMap<int, QString> channels;
    QMap<int, int> patch;
    QMap<int, Values> palettes, groups;
    QList<Sequence> sequences;
    QMap<int, Value> manual;
    QSet<int> selection;
    Values pixels;
    double master = 1;
    bool blackout = false;
    quint64 clock = 0;
    Engine();
    Values tracked(int sequence, int cue) const;
    double resolve(int channel, Value value) const;
    Values output() const;
    void level(double percent);
    void applyPalette(int id);
    void recordPalette(int id);
    void record(int sequence, QString number, QString label, double time, bool cueOnly = false);
    void go(int sequence, int index = -1);
    void back(int sequence);
    void release(int sequence);
    void tick(double seconds);
    bool command(QString text, QString *error = nullptr);
    QJsonObject json() const;
    bool load(const QJsonObject &object, QString *error = nullptr);
};
} // namespace Flame
