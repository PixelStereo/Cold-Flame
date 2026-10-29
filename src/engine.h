#pragma once
#include <QtCore>
#include "gdtf.h"
#include "colorcontrols.h"
#include <optional>
namespace Flame {
using Values = QMap<int, double>;
struct Value {
    double level = 0;
    int palette = 0;
    int colorPalette = -1; // -1: absolute; 0: immutable hidden white; >0: shared CP.
};
struct ChannelTiming {
    double up = 2, down = 2, wait = 0;
};
struct Submaster {
    enum class Mode { Additive, Inhibitive };
    enum class Dwell { Manual, Hold, Timed };
    enum class Phase { Idle, Up, Dwell, Down };
    int number = 1;
    QString name;
    Values values;
    double level = 0;
    Mode mode = Mode::Additive;
    Dwell dwell = Dwell::Manual;
    double upTime = 0, dwellTime = 0, downTime = 0;
    // Transport state is deliberately excluded from saved show definitions.
    Phase phase = Phase::Idle;
    bool pressed = false;
    double elapsed = 0, start = 0;
    double home() const { return mode == Mode::Inhibitive ? 1 : 0; }
    double target() const { return 1 - home(); }
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
    QSet<int> suppressed; // Remove shared-cue moves locally to permit tracking.
};
struct FixtureAssignment {
    QString profile = "dimmer8";
    int mode = 0;
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
    QMap<QString, FixtureProfile> fixtureProfiles = genericFixtures();
    QMap<int, FixtureAssignment> fixtures;
    FixtureMode fixtureMode(int channel) const;
    int parameterKey(int channel, int parameter) const;
    int channelForControl(int key) const;
    QList<int> controlIds() const;
    bool controlExists(int key) const;
    double controlDefault(int key) const;
    QString controlLabel(int key) const;
    QList<int> occupiedAddresses(int channel) const;
    QMap<int, QString> patchErrors() const;
    QMap<int, QByteArray> dmxFrames() const;
    bool canChangeFixture(int channel, FixtureAssignment assignment) const;
    void trackFromPrevious(int sequence, int step, int control, bool cueOnly = false);
    void setChannelBlock(int sequence, int step, int control, bool enabled);
    int traceSource(int sequence, int step, int control) const;
    void traceEdit(int sequence, int step, int control, Value value, bool force = false, bool cueOnly = false);
    static constexpr int ChannelsPerPage = 120;
    QMap<QString, StoredCue> cueLibrary;
    QMap<int, Values> palettes, groups;
    QMap<int, ColorPalette> colorPalettes; // Only user palettes; white 0 is computed, never stored here.
    ColorControls colorControls(int channel) const;
    ColorPalette colorPalette(int id) const;
    int findOrCreateColorPalette(ColorPalette color);
    bool updateColorPalette(int id, ColorPalette color);
    bool deleteColorPalette(int id);
    bool applyColorPalette(int channel, int id);
    int assignColorPaletteToCue(QString cue, int id, QSet<int> fixtures = {});
    double colorComponent(int control, int palette) const;
    bool validValue(int control, Value value) const;
    Value recordedValue(int control, const Values &live) const;
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
    Value trackedValue(int sequence, int cue, int control) const;
    double resolve(int channel, Value value) const;
    Values output(bool mastered = true, bool includePark = true, bool includeInhibition = true) const;
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
    bool setSubmasterLevel(int index, double level);
    bool bumpSubmaster(int index, bool pressed);
    void advanceSubmasters(double dt);
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
