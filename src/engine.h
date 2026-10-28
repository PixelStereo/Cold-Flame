#pragma once
#include <QtCore>
#include <optional>
namespace Flame {
using Values = QMap<int, double>;
struct Value {
    double level = 0;
    int palette = 0;
};
struct ChannelTiming {
    double up = 2, down = 2, wait = 0;
};
struct Submaster {
    int number = 1;
    QString name;
    Values values;
    double level = 0;
};
struct StoredCue {
    QString number, label;
    QMap<int, Value> values;
};
struct Cue {
    QString reference; // Shared stored cue; number identifies this sequence step.
    QString number, label;
    double time = 2;
    double down = -1, wait = 0; // -1 means use the common fade time.
    QString link, execute, after;
    std::optional<double> follow, hang;
    QMap<int, ChannelTiming> channelTimes;
    QMap<int, Value> values;
    bool block = false, asserted = false;
    QMap<int, Value> blocked;
};
struct Sequence {
    QString name;
    int number = 1;
    int priority = 4;
    QList<Cue> cues;
    int current = -1;
    QString pendingCue; // One-shot next GO override; never changes stored links.
    Values output, start, target;
    QMap<int, double> channelElapsed;
    QMap<int, quint64> ownership;
    double elapsed = 0, duration = 0;
    quint64 stamp = 0;
    bool paused = false, linkPending = false, autoPending = false;
    QSet<int> releasing;
    QMap<int, ChannelTiming> timings;
};
class Engine {
  public:
    QMap<int, QString> channels;
    QMap<int, int> patch;
    QMap<int, QList<int>> additionalPatch;
    QList<int> dmxAddresses(int channel) const;
    static constexpr int ChannelsPerPage = 120;
    QMap<QString, StoredCue> cueLibrary;
    QMap<int, Values> palettes, groups;
    QList<Sequence> sequences;
    QList<Submaster> submasters;
    QMap<int, Sequence> linkedPlaybacks; // One automatic side playback per originating sequence.
    QMap<int, Value> manual;
    QSet<int> selection;
    Values pixels, parked;
    QSet<int> captured;
    double master = 1;
    bool blackout = false;
    quint64 clock = 0;
    int commandSequence = 0, commandCue = -1;
    double defaultFade = 2;
    bool cueOnlyMode = false;
    Engine();
    Values tracked(int sequence, int cue) const;
    double resolve(int channel, Value value) const;
    Values output(bool mastered = true, bool includePark = true) const;
    void toggleCapture();
    void parkSelection(bool enabled);
    void setBlock(int sequence, int cue, bool enabled);
    void editBlind(int sequence, int cue, int channel, Value value, bool cueOnly = false);
    QMap<int, Value> cueValues(const Cue &step) const;
    QString cueLabel(const Cue &step) const;
    QString nextStoredCueNumber() const;
    bool storeCue(QString number, QString label = {}, bool manualOnly = false);
    bool deleteStoredCue(QString number);
    bool renameStoredCue(QString number, QString replacement);
    int insertStoredCue(int sequence, QString number, int position = -1);
    bool moveStep(int sequence, int index, int destination);
    bool removeStep(int sequence, int index);
    bool updateStoredCue(QString number, const QMap<int, Value> &values, bool replace = false);
    QString nextCueNumber(int sequence) const;
    int addChannelPage();
    int addSequence();
    bool removeSequence(int sequence);
    int nextCueIndex(int sequence) const;
    bool setNextCue(int sequence, int cue);
    void recordSubmaster();
    void level(double percent);
    void applyPalette(int id);
    void recordPalette(int id);
    void record(int sequence, QString number, QString label, double time, bool cueOnly = false);
    void recordLook(int sequence, QString number, QString label, double time, bool cueOnly = false);
    void go(int sequence, int index = -1);
    void back(int sequence);
    void release(int sequence);
    void tick(double seconds);
    bool command(QString text, QString *error = nullptr);
    QJsonObject json() const;
    bool load(const QJsonObject &object, QString *error = nullptr);

  private:
    void beginPlayback(Sequence &playback, int cue, Values target, bool assertAll = true);
    QPair<int, int> cueTarget(QString target, int defaultSequence) const;
    bool ticking = false;
    int triggerDepth = 0;
};
} // namespace Flame
