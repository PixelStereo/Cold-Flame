#include "editing.h"
#include "engine.h"
#include "gdtf.h"
#include "network.h"
#include <QtTest>
#include <zlib.h>
using namespace Flame;
class Tests : public QObject {
    Q_OBJECT
  private slots:
    void submasterCommandsUseStableNumbers() {
        Engine e; Submaster sub; sub.number = 7; sub.values[1] = 80; e.submasters << sub;
        QVERIFY(e.command("Sub 7 At 50")); QCOMPARE(e.output()[1], 40.);
        QVERIFY(!e.command("Sub 1 Full")); QVERIFY(!e.command("Sub 7 At 101")); QCOMPARE(e.output()[1], 40.);
        QVERIFY(e.command("Sub 7 Bump")); QCOMPARE(e.output()[1], 80.);
        QVERIFY(e.command("Sub 7 Release")); QCOMPARE(e.output()[1], 0.);
        e.submasters[0].mode = Submaster::Mode::Inhibitive;
        QVERIFY(e.command("Submaster 7 Home")); QCOMPARE(e.submasters[0].level, 1.);
    }
    void timedSubmasterBumpConsumesWholeTick() {
        Engine e; Submaster sub; sub.values[1] = 80;
        sub.dwell = Submaster::Dwell::Timed; sub.upTime = 1; sub.dwellTime = 2; sub.downTime = 1;
        e.submasters << sub;
        QVERIFY(e.bumpSubmaster(0, true)); QVERIFY(e.bumpSubmaster(0, false));
        e.tick(.5); QCOMPARE(e.output()[1], 40.);
        e.tick(1.5); QCOMPARE(e.output()[1], 80.); QCOMPARE(e.submasters[0].phase, Submaster::Phase::Dwell);
        e.tick(1.5); QCOMPARE(e.output()[1], 40.); QCOMPARE(e.submasters[0].phase, Submaster::Phase::Down);
        e.tick(.5); QCOMPARE(e.output()[1], 0.); QCOMPARE(e.submasters[0].phase, Submaster::Phase::Idle);
        e.bumpSubmaster(0, true); e.bumpSubmaster(0, false); e.tick(20);
        QCOMPARE(e.submasters[0].level, 0.); QCOMPARE(e.submasters[0].phase, Submaster::Phase::Idle);
        QVERIFY(!e.setSubmasterLevel(0, 2)); QVERIFY(!e.bumpSubmaster(9, true));
    }
    void manualHoldAndFaderOverrideSubmasterBumps() {
        Engine e; Submaster sub; sub.values[1] = 100; sub.upTime = 2; sub.downTime = 1; e.submasters << sub;
        e.bumpSubmaster(0, true); e.tick(1); QCOMPARE(e.output()[1], 50.);
        e.bumpSubmaster(0, false); e.tick(.5); QCOMPARE(e.output()[1], 25.);
        e.tick(.5); QCOMPARE(e.output()[1], 0.);
        e.submasters[0].dwell = Submaster::Dwell::Hold;
        e.bumpSubmaster(0, true); e.bumpSubmaster(0, false); e.tick(10); QCOMPARE(e.output()[1], 100.);
        e.bumpSubmaster(0, true); e.bumpSubmaster(0, false); e.tick(.5); QCOMPARE(e.output()[1], 50.);
        QVERIFY(e.setSubmasterLevel(0, .3)); e.tick(20); QCOMPARE(e.output()[1], 30.);
        QCOMPARE(e.submasters[0].phase, Submaster::Phase::Idle);
        // Zero-time Manual is a true flash, even between engine ticks.
        e.submasters[0].dwell = Submaster::Dwell::Manual; e.submasters[0].upTime = e.submasters[0].downTime = 0;
        e.bumpSubmaster(0, true); QCOMPARE(e.output()[1], 100.);
        e.bumpSubmaster(0, false); QCOMPARE(e.output()[1], 0.);
    }
    void inhibitiveSubmastersScaleIntensityAndRespectPark() {
        Engine e; e.fixtures[1] = {"rgb", 0}; e.patch[1] = 513; e.manual[1] = {80, 0}; e.manual[2] = {60, 0};
        e.applyColorPalette(1, e.findOrCreateColorPalette({"Red", 1, 0, 0}));
        Submaster sub; sub.mode = Submaster::Mode::Inhibitive; sub.level = 1; sub.values[1] = 100; e.submasters << sub;
        QCOMPARE(e.output()[1], 80.); e.setSubmasterLevel(0, .5);
        QCOMPARE(e.output()[1], 40.); QCOMPARE(e.output()[2], 60.);
        QCOMPARE(e.output()[e.parameterKey(1, 0)], 100.);
        Submaster other = sub; other.number = 2; other.level = .75; e.submasters << other;
        QCOMPARE(e.output()[1], 40.); // Lowest inhibitor wins; levels do not multiply.
        e.selection = {1}; e.toggleCapture(); QCOMPARE(e.output()[1], 40.); // Capture does not apply inhibition twice.
        e.storeCue("10"); QCOMPARE(e.cueLibrary["10"].values[1].level, 40.);
        e.master = .5; QCOMPARE(e.output()[1], 20.);
        e.parked[1] = 70; e.blackout = true; QCOMPARE(e.output()[1], 70.);
        e.parked.clear(); e.blackout = false; e.master = 1;
        e.bumpSubmaster(0, true); QCOMPARE(e.output()[1], 0.);
        e.bumpSubmaster(0, false); QCOMPARE(e.submasters[0].level, 1.); QCOMPARE(e.output()[1], 60.);
        e.manual.clear(); e.submasters[1].values.clear(); QCOMPARE(e.output()[1], 0.); // Inhibitors do not generate a look.
    }
    void submasterTimingPersistenceValidationAndUndo() {
        Engine e; Submaster sub; sub.mode = Submaster::Mode::Inhibitive; sub.level = 1; sub.values[1] = 100;
        sub.dwell = Submaster::Dwell::Timed; sub.upTime = 2; sub.dwellTime = 3; sub.downTime = 4; e.submasters << sub;
        EditHistory history; history.reset(e); e.bumpSubmaster(0, true); e.bumpSubmaster(0, false); e.tick(1);
        e.submasters[0].name = "Edited while fading"; history.commit(e); e.tick(.5);
        QVERIFY(history.undo(e)); QCOMPARE(e.submasters[0].level, .25); QCOMPARE(e.submasters[0].elapsed, 1.5);
        e.tick(.5); QCOMPARE(e.submasters[0].level, 0.);
        auto json = e.json(); Engine loaded; QVERIFY(loaded.load(json));
        QCOMPARE(loaded.submasters[0].mode, Submaster::Mode::Inhibitive);
        QCOMPARE(loaded.submasters[0].level, 1.); QCOMPARE(loaded.submasters[0].phase, Submaster::Phase::Idle);
        QCOMPARE(loaded.submasters[0].dwellTime, 3.); QCOMPARE(loaded.submasters[0].downTime, 4.);
        for (const auto &bad : {QJsonValue(-1), QJsonValue("2"), QJsonValue(86401)}) {
            auto invalid = json; auto subs = invalid["submasters"].toArray(); auto item = subs[0].toObject();
            item["upTime"] = bad; subs[0] = item; invalid["submasters"] = subs;
            QVERIFY(!loaded.load(invalid)); QCOMPARE(loaded.submasters[0].level, 1.);
        }
        auto legacy = json; legacy["version"] = 4; auto subs = legacy["submasters"].toArray(); auto item = subs[0].toObject();
        for (const auto &key : {"mode", "dwell", "upTime", "dwellTime", "downTime"}) item.remove(key);
        subs[0] = item; legacy["submasters"] = subs; QVERIFY(loaded.load(legacy));
        QCOMPARE(loaded.submasters[0].mode, Submaster::Mode::Additive); QCOMPARE(loaded.submasters[0].level, 0.);
    }
    void hiddenWhiteAndCompleteRgbRecording() {
        Engine e; e.fixtures[1] = {"rgbw", 0}; e.patch[1] = 513;
        QVERIFY(e.colorPalettes.isEmpty()); QCOMPARE(e.colorPalette(0).red, 1.);
        QVERIFY(!e.updateColorPalette(0, {"Black", 0, 0, 0})); QVERIFY(!e.deleteColorPalette(0));
        e.manual[1] = {50, 0}; QVERIFY(e.storeCue("10", {}, true));
        QCOMPARE(e.cueLibrary["10"].values[1].level, 50.);
        for (int key : e.colorControls(1).keys()) QCOMPARE(e.cueLibrary["10"].values[key].colorPalette, 0);
        QVERIFY(e.colorPalettes.isEmpty());
        auto frame = e.dmxFrames()[1]; QCOMPARE(frame.left(3), QByteArray(3, '\0')); QCOMPARE(quint8(frame[3]), 128);
        Engine restored; QVERIFY(restored.load(e.json())); QVERIFY(restored.colorPalettes.isEmpty());
        QCOMPARE(restored.cueLibrary["10"].values[e.parameterKey(1, 3)].colorPalette, 0);
        auto invalid = e.json(); invalid["colorPalettes"] = QJsonArray{QJsonObject{{"id", 0}, {"red", 0}, {"green", 0}, {"blue", 0}}};
        QVERIFY(!restored.load(invalid)); QCOMPARE(restored.colorPalette(0).red, 1.);
    }
    void assignPaletteDirectlyToStoredCue() {
        Engine e; e.fixtures[1] = {"rgb", 0}; e.patch[1] = 513;
        e.fixtures[2] = {"rgbw", 0}; e.patch[2] = 520;
        e.manual[1] = {60, 0}; e.manual[2] = {25, 0}; e.storeCue("10"); e.manual.clear();
        int id = e.findOrCreateColorPalette({"Red", 1, 0, 0});
        QCOMPARE(e.assignColorPaletteToCue("10", id, {1}), 1);
        QCOMPARE(e.cueLibrary["10"].values[e.parameterKey(1, 0)].colorPalette, id);
        QCOMPARE(e.cueLibrary["10"].values[e.parameterKey(2, 0)].colorPalette, 0);
        QCOMPARE(e.assignColorPaletteToCue("10", id), 2);
        QCOMPARE(e.cueLibrary["10"].values[1].level, 60.); QCOMPARE(e.cueLibrary["10"].values[2].level, 25.);
        QVERIFY(e.manual.isEmpty()); auto before = e.json(); QCOMPARE(e.assignColorPaletteToCue("10", 999), 0); QCOMPARE(e.json(), before);
    }
    void colorPaletteCueReferencesAndLocalSteps() {
        Engine e; e.fixtures[1] = {"rgb", 0}; e.patch[1] = 513;
        e.manual[1] = {45, 0}; QVERIFY(e.storeCue("10")); e.manual.clear();
        int red = e.findOrCreateColorPalette({"Red", 1, 0, 0}); QVERIFY(red > 0);
        QVERIFY(e.applyColorPalette(1, red)); QVERIFY(e.updateStoredCue("10", e.manual));
        QCOMPARE(e.cueLibrary["10"].values[1].level, 45.);
        int r = e.parameterKey(1, 0), g = e.parameterKey(1, 1), b = e.parameterKey(1, 2);
        QCOMPARE(e.cueLibrary["10"].values[r].colorPalette, red);
        e.insertStoredCue(0, "10"); e.insertStoredCue(0, "10"); int other = e.addSequence(); e.insertStoredCue(other, "10");
        QVERIFY(e.updateColorPalette(red, {"Blue now", 0, 0, 1}));
        QCOMPARE(e.tracked(0, 0)[r], 0.); QCOMPARE(e.tracked(0, 1)[b], 100.); QCOMPARE(e.tracked(other, 0)[b], 100.);
        QCOMPARE(e.tracked(0, 1)[1], 45.); QVERIFY(!e.deleteColorPalette(red));
        int green = e.findOrCreateColorPalette({"Green", 0, 1, 0}); e.applyColorPalette(1, green);
        for (int key : e.colorControls(1).keys()) e.editBlind(0, 0, key, e.manual.value(key));
        QCOMPARE(e.tracked(0, 0)[g], 100.); QCOMPARE(e.tracked(0, 1)[b], 100.); QCOMPARE(e.tracked(other, 0)[b], 100.);
        e.setChannelBlock(0, 1, r, true); QCOMPARE(e.sequences[0].cues[1].blocked[r].colorPalette, red);
        Engine restored; QVERIFY(restored.load(e.json())); QCOMPARE(restored.tracked(0, 0)[g], 100.);
        QCOMPARE(restored.cueLibrary["10"].values[b].colorPalette, red);
    }
    void colorReferencesSurviveRecallRecordingAndUndo() {
        Engine e; e.fixtures[1] = {"rgb", 0}; e.patch[1] = 513;
        int id = e.findOrCreateColorPalette({"Red", 1, 0, 0}); e.applyColorPalette(1, id); e.manual[1] = {75, 0};
        e.storeCue("10"); e.insertStoredCue(0, "10"); e.sequences[0].cues[0].time = 0; e.go(0);
        QVERIFY(e.manual.isEmpty()); QVERIFY(e.storeCue("20")); int g = e.parameterKey(1, 1);
        QCOMPARE(e.cueLibrary["20"].values[g].colorPalette, id);
        e.recordLook(0, "30", "Playback snapshot", 0);
        QCOMPARE(e.sequences[0].cues[1].values[g].colorPalette, id); QCOMPARE(e.tracked(0, 1)[1], 75.);
        e.selection = {1}; e.toggleCapture(); QCOMPARE(e.manual[g].colorPalette, id); e.toggleCapture(); e.manual.clear();
        EditHistory history; history.reset(e); QVERIFY(e.updateColorPalette(id, {"Green", 0, 1, 0})); history.commit(e);
        QCOMPARE(e.tracked(0, 0)[g], 100.); QVERIFY(history.undo(e)); QCOMPARE(e.tracked(0, 0)[g], 0.);
        Engine fresh; fresh.fixtures[1] = {"rgb", 0}; fresh.patch[1] = 513; history.reset(fresh);
        id = fresh.findOrCreateColorPalette({"Red", 1, 0, 0}); fresh.applyColorPalette(1, id); history.commit(fresh);
        QVERIFY(history.undo(fresh)); QVERIFY(fresh.colorPalettes.isEmpty());
        QCOMPARE(fresh.manual[fresh.parameterKey(1, 0)].colorPalette, -1); QCOMPARE(fresh.output()[fresh.parameterKey(1, 0)], 100.);
        auto bad = e.json(); auto library = bad["cueLibrary"].toArray(); auto cue = library[0].toObject(); auto values = cue["values"].toObject();
        auto value = values[QString::number(g)].toObject(); value["colorPalette"] = 9999; values[QString::number(g)] = value; cue["values"] = values; library[0] = cue; bad["cueLibrary"] = library;
        Engine rejected; QVERIFY(!rejected.load(bad));
    }
    void startupBroadcastRoutePreservesSettings() {
        Network network; DmxOutput unicast, broadcast;
        broadcast.name = "Rig broadcast"; broadcast.mode = "Broadcast"; broadcast.firstUniverse = 42;
        QVERIFY(network.applyOutputs({unicast, broadcast}));
        QVERIFY(network.startDefaultArtNet()); QCOMPARE(network.outputs.size(), 2);
        QVERIFY(!network.outputs[0].enabled); QVERIFY(network.outputs[1].enabled);
        QCOMPARE(network.outputs[1].firstUniverse, 42); QVERIFY(network.enabled);
        QVERIFY(network.startDefaultArtNet()); QCOMPARE(network.outputs.size(), 2);
        Network restored; QVERIFY(restored.loadOutputs(network.outputJson())); QVERIFY(!restored.enabled);
        QVERIFY(restored.startDefaultArtNet()); QVERIFY(restored.outputs[1].enabled);
        Network fresh; QVERIFY(fresh.startDefaultArtNet()); QCOMPARE(fresh.outputs.size(), 1);
        QCOMPARE(fresh.outputs[0].protocol, QString("Art-Net")); QCOMPARE(fresh.outputs[0].mode, QString("Broadcast"));
        QVERIFY(fresh.enabled); fresh.stop(); QVERIFY(!fresh.enabled);
    }
    void undoFixtureTypeReleasesIncompatibleLiveParameters() {
        Engine e; EditHistory history; history.reset(e);
        e.fixtures[1] = {"moving-rgbw", 0}; e.patch[1] = 513; history.commit(e);
        const int pan = e.parameterKey(1, 0); e.manual[pan] = {100, 0}; e.manual[1] = {80, 0};
        e.selection = {1}; e.toggleCapture(); e.parkSelection(true);
        QVERIFY(history.undo(e)); QCOMPARE(e.fixtures.value(1).profile, QString("dimmer8"));
        QVERIFY(!e.manual.contains(pan)); QVERIFY(!e.captured.contains(pan)); QVERIFY(!e.parked.contains(pan));
        QCOMPARE(e.manual[1].level, 80.); QVERIFY(e.captured.contains(1));
    }
    void traceInactiveAndCueOnly() {
        Engine e; e.manual[1] = {0, 0}; e.record(0, "10", "Zero", 0); e.manual.clear();
        e.record(0, "20", "Track", 0); e.record(0, "30", "Track", 0);
        e.traceEdit(0, 1, 1, {70, 0}); QCOMPARE(e.tracked(0, 0)[1], 0.); QCOMPARE(e.tracked(0, 1)[1], 70.);
        e.trackFromPrevious(0, 1, 1); e.traceEdit(0, 1, 1, {40, 0}, true, true);
        QCOMPARE(e.tracked(0, 0)[1], 40.); QCOMPARE(e.tracked(0, 1)[1], 40.); QCOMPARE(e.tracked(0, 2)[1], 0.);
    }
    void fixtureGdtfRoundtripAndReservations() {
        const auto profiles = genericFixtures(); QString error;
        for (auto it = profiles.begin(); it != profiles.end(); ++it) {
            const auto bytes = writeGdtf(it.value()); FixtureProfile loaded;
            QVERIFY2(readGdtf(bytes, loaded, error), qPrintable(error));
            QCOMPARE(loaded.name, it->name); QCOMPARE(loaded.modes[0].footprint, it->modes[0].footprint);
            QCOMPARE(loaded.modes[0].parameters.size(), it->modes[0].parameters.size());
            QVERIFY2(validateMode(loaded.modes[0], error), qPrintable(error));
            QCOMPARE(writeGdtf(loaded), bytes); // Imported vendor archives are re-exported byte for byte.
            FixtureProfile restored; QVERIFY(fixtureFromJson(fixtureJson(loaded), restored, error));
            for (int i = 0; i < loaded.modes[0].parameters.size(); ++i) {
                const auto a = loaded.modes[0].parameters[i], b = it->modes[0].parameters[i];
                QCOMPARE(a.offsets, b.offsets); QCOMPARE(a.attribute, b.attribute);
                QVERIFY(qAbs(a.home - b.home) <= 100. / 255.); QCOMPARE(a.snap, b.snap);
            }
        }
        Engine e; e.fixtures[1] = {"moving-rgbw", 0};
        QVERIFY(e.patchErrors().contains(1)); QVERIFY(e.patchErrors().contains(2));
        e.patch[1] = 513; QVERIFY(e.patchErrors().isEmpty());
        e.additionalPatch[1] = {1020}; QVERIFY(e.patchErrors().contains(1)); // Eleven slots cannot start at 2.508.
        e.additionalPatch[1] = {1025}; QVERIFY(e.patchErrors().isEmpty());
        e.patch.remove(1); e.additionalPatch.remove(1); QVERIFY(e.patchErrors().isEmpty()); // Unpatching is valid.
        Engine copy; QVERIFY(copy.load(e.json())); QVERIFY(copy.dmxAddresses(1).isEmpty());
        e.patch[1] = 1; QVERIFY(!copy.load(e.json())); // Whole footprint overlap rejected while loading.
        FixtureMode invalid = profiles["rgb"].modes[0]; invalid.parameters[1].offsets = {1};
        QVERIFY(!validateMode(invalid, error));
        invalid = profiles["dimmer8"].modes[0]; invalid.unsupported = "Multiple DMX breaks are not supported";
        QVERIFY(!validateMode(invalid, error));
    }
    void fixtureParametersRecordFadeAndNetwork() {
        Engine e; e.fixtures[1] = {"moving-rgbw", 0}; e.patch[1] = 513; e.additionalPatch[1] = {1025};
        int pan = e.parameterKey(1, 0), red = e.parameterKey(1, 3), gobo = e.parameterKey(1, 8);
        QVERIFY(pan < 0); QCOMPARE(e.channelForControl(pan), 1); QVERIFY(e.controlExists(pan));
        QCOMPARE(e.output().value(pan), 50.);
        e.manual = {{1, {100, 0}}, {pan, {100, 0}}, {red, {25, 0}}, {gobo, {80, 0}}};
        QVERIFY(e.storeCue("10")); e.insertStoredCue(0, "10"); e.sequences[0].cues[0].time = 2;
        e.manual.clear(); e.go(0); e.tick(1);
        QCOMPARE(e.output().value(1), 50.); QCOMPARE(e.output().value(pan), 75.);
        QCOMPARE(e.output().value(gobo), 80.); // Snap is honored for discrete parameters.
        e.tick(1); QCOMPARE(e.output().value(red), 25.);
        e.master = 0.5;
        QCOMPARE(e.output().value(pan), 100.); QCOMPARE(e.output().value(1), 50.); // Grand master affects intensity only.
        auto frames = e.dmxFrames(); QCOMPARE(quint8(frames[1][0]), 255); QCOMPARE(quint8(frames[1][1]), 255);
        QCOMPARE(quint8(frames[1][4]), 128); QCOMPARE(quint8(frames[1][5]), 64); QCOMPARE(frames[1], frames[2]);
        QUdpSocket receiver; receiver.setProxy(QNetworkProxy::NoProxy); QVERIFY(receiver.bind(QHostAddress::LocalHost, 6454));
        Network n; DmxOutput route; route.enabled = true; route.interfaceAddress = "127.0.0.1"; route.firstPatchUniverse = 2; route.universeCount = 1;
        QVERIFY(n.applyOutputs({route})); n.send(e); QVERIFY(receiver.waitForReadyRead(1000));
        QCOMPARE(receiver.receiveDatagram().data().mid(18), frames[1]);
        Engine restored; QString error; QVERIFY2(restored.load(e.json(), &error), qPrintable(error));
        QCOMPARE(restored.cueLibrary["10"].values[pan].level, 100.);
        restored.go(0); restored.tick(2); QCOMPARE(restored.output()[red], 25.);
        QVERIFY(!restored.canChangeFixture(1, {"rgb", 0})); // Stored internal identities must not be reinterpreted.
        EditHistory history; history.reset(restored); restored.patch[1] = 2049; history.commit(restored);
        QVERIFY(history.undo(restored)); QCOMPARE(restored.patch[1], 513); QCOMPARE(restored.fixtures[1].profile, QString("moving-rgbw"));
    }
    void fixtureVirtualDimmerAndByteOrder() {
        Engine e; e.fixtures[1] = {"rgb", 0}; e.patch[1] = 513;
        int green = e.parameterKey(1, 1); e.manual[1] = {50, 0}; e.manual[green] = {20, 0};
        auto frame = e.dmxFrames()[1]; QCOMPARE(quint8(frame[0]), 128); QCOMPARE(quint8(frame[1]), 26); QCOMPARE(quint8(frame[2]), 128);
        e.blackout = true; frame = e.dmxFrames()[1]; QCOMPARE(frame.left(3), QByteArray(3, '\0'));
        QCOMPARE(e.output()[green], 20.); // Blackout does not alter color or position values.
        e.blackout = false; e.fixtures[1] = {"dimmer16", 0}; e.manual.clear(); e.manual[1] = {50, 0};
        frame = e.dmxFrames()[1]; QCOMPARE(quint8(frame[0]), 128); QCOMPARE(quint8(frame[1]), 0);
        auto profile = genericFixtures()["dimmer16"];
        profile.modes[0].parameters[0].offsets = {2, 1}; e.fixtureProfiles["swapped"] = profile; e.fixtures[1] = {"swapped", 0};
        frame = e.dmxFrames()[1]; QCOMPARE(quint8(frame[0]), 0); QCOMPARE(quint8(frame[1]), 128);
        profile.modes[0].footprint = 4; profile.modes[0].parameters[0].offsets = {1, 2, 3, 4};
        e.fixtureProfiles["wide"] = profile; e.fixtures[1] = {"wide", 0};
        frame = e.dmxFrames()[1]; QCOMPARE(frame.left(4).toHex(), QByteArray("80000000"));
    }
    void channelTrackingTraceAndSharedCueSuppression() {
        Engine e; e.manual[1] = {20, 0}; QVERIFY(e.storeCue("10", {}, true));
        e.insertStoredCue(0, "10"); e.insertStoredCue(0, "10"); e.insertStoredCue(0, "10"); e.insertStoredCue(0, "10");
        e.trackFromPrevious(0, 1, 1); e.trackFromPrevious(0, 2, 1);
        QCOMPARE(e.traceSource(0, 2, 1), 0); QCOMPARE(e.tracked(0, 2)[1], 20.);
        e.editBlind(0, 0, 1, {35, 0}); QCOMPARE(e.tracked(0, 2)[1], 35.); QCOMPARE(e.tracked(0, 3)[1], 20.);
        e.setChannelBlock(0, 1, 1, true); e.editBlind(0, 0, 1, {70, 0});
        QCOMPARE(e.tracked(0, 2)[1], 35.); QCOMPARE(e.traceSource(0, 2, 1), 1);
        e.traceEdit(0, 2, 1, {45, 0}); QCOMPARE(e.tracked(0, 0)[1], 70.); QCOMPARE(e.tracked(0, 2)[1], 45.);
        QCOMPARE(e.cueLibrary["10"].values[1].level, 20.); // Trace never changes other occurrences in the library.
        e.trackFromPrevious(0, 1, 1, true); QCOMPARE(e.tracked(0, 1)[1], 70.); QCOMPARE(e.tracked(0, 2)[1], 45.);
        Engine restored; QVERIFY(restored.load(e.json())); QCOMPARE(restored.tracked(0, 1)[1], 70.); QCOMPARE(restored.tracked(0, 2)[1], 45.);
        QVERIFY(restored.sequences[0].cues[1].suppressed.contains(1));
        e.fixtures[2] = {"moving-rgbw", 0}; e.patch[2] = 513; int pan = e.parameterKey(2, 0);
        e.editBlind(0, 0, pan, {80, 0}); e.setChannelBlock(0, 1, pan, true); e.editBlind(0, 0, pan, {30, 0});
        QCOMPARE(e.tracked(0, 2)[pan], 80.); e.setChannelBlock(0, 1, pan, false); QCOMPARE(e.tracked(0, 2)[pan], 30.);
    }
    void recordedLookRecallWithoutRelease() {
        Engine e;
        e.selection = {1};
        e.level(20);
        e.recordLook(0, "10", "cue-10", 2);
        e.level(80);
        e.recordLook(0, "20", "cue-20", 2);
        e.go(0, 0);
        e.tick(2);
        QCOMPARE(e.output()[1], 20.);
        QVERIFY(!e.manual.contains(1));
        e.go(0);
        e.tick(2);
        QCOMPARE(e.output()[1], 80.);
        e.back(0);
        e.tick(2);
        QCOMPARE(e.output()[1], 20.);
    }
    void backReleasesLaterChannels() {
        Engine e;
        e.selection = {1};
        e.level(30);
        e.record(0, "10", "", 0);
        e.manual.clear();
        e.selection = {2};
        e.level(90);
        e.record(0, "20", "", 0);
        e.go(0, 1);
        QCOMPARE(e.output()[2], 90.);
        e.back(0);
        QCOMPARE(e.output().value(2), 0.);
    }
    void splitTimesWaitAndChannelOverride() {
        Engine e;
        e.selection = {1, 2, 3};
        e.level(100);
        e.recordLook(0, "10", "", 0);
        e.go(0);
        e.selection = {1, 2};
        e.level(0);
        e.selection = {3};
        e.level(100);
        e.recordLook(0, "20", "", 2);
        auto &cue = e.sequences[0].cues[1];
        cue.down = 4;
        cue.wait = 1;
        cue.channelTimes[2] = {2, 2, 2};
        e.manual.clear(); // Return to cue 10 before starting the timed transfer.
        e.go(0);
        e.tick(1);
        QCOMPARE(e.output()[1], 100.);
        e.tick(1);
        QCOMPARE(e.output()[1], 75.);
        QCOMPARE(e.output()[2], 100.);
        e.tick(1);
        QCOMPARE(e.output()[1], 50.);
        QCOMPARE(e.output()[2], 50.);
        e.tick(2);
        QCOMPARE(e.output()[1], 0.);
        QCOMPARE(e.output()[2], 0.);
    }
    void linkedPendingAndDefaults() {
        Engine e;
        QCOMPARE(e.sequences[0].number, 1);
        QCOMPARE(e.nextCueNumber(0), QString("10"));
        e.recordLook(0, "10", "cue-10", 0);
        QCOMPARE(e.nextCueNumber(0), QString("20"));
        e.recordLook(0, "20", "cue-20", 0);
        e.recordLook(0, "30", "cue-30", 0);
        e.sequences[0].cues[0].after = "30";
        e.go(0);
        QCOMPARE(e.sequences[0].current, 0);
        QVERIFY(e.linkedPlaybacks.contains(0));
        QCOMPARE(e.linkedPlaybacks[0].current, 2);
        auto stamp = e.linkedPlaybacks[0].stamp;
        e.tick(1);
        QCOMPARE(e.linkedPlaybacks[0].stamp, stamp);
        e.go(0);
        QCOMPARE(e.sequences[0].current, 1);
        int s = e.addSequence();
        QCOMPARE(e.sequences[s].number, 2);
        QCOMPARE(e.sequences[s].name, QString("sequence-2"));
    }
    void nextCueOverrideAndSequenceDeletion() {
        Engine e;
        e.selection = {1};
        for (int i = 1; i <= 3; ++i) {
            e.level(i * 20);
            e.recordLook(0, QString::number(i * 10), "", 0);
        }
        e.manual.clear();
        e.go(0);
        QCOMPARE(e.output()[1], 20.);
        e.sequences[0].cues[0].link = "20";
        QVERIFY(e.setNextCue(0, 2));
        QCOMPARE(e.nextCueIndex(0), 2);
        QCOMPARE(e.output()[1], 20.);
        e.go(0);
        QCOMPARE(e.sequences[0].current, 2);
        QCOMPARE(e.output()[1], 60.);
        QVERIFY(e.sequences[0].pendingCue.isEmpty());
        QCOMPARE(e.sequences[0].cues[0].link, QString("20"));
        int second = e.addSequence();
        e.recordLook(second, "10", "", 0);
        e.sequences[0].cues[0].execute = "2/10";
        QVERIFY(e.removeSequence(second));
        QVERIFY(e.sequences[0].cues[0].execute.isEmpty());
        QVERIFY(!e.removeSequence(0));
        e.recordSubmaster();
        e.recordSubmaster();
        QCOMPARE(e.submasters[0].number, 1);
        QCOMPARE(e.submasters[1].number, 2);
        e.submasters.removeFirst();
        e.recordSubmaster();
        QCOMPARE(e.submasters.last().number, 3);
        Engine restored;
        QVERIFY(restored.load(e.json()));
        QCOMPARE(restored.submasters.last().number, 3);
    }
    void storedCuesRepeatWithIndependentStepTimingAndLinks() {
        Engine e;
        QCOMPARE(e.channels.size(), 120);
        QCOMPARE(e.addChannelPage(), 1); QCOMPARE(e.channels.size(), 240);
        e.selection = {1}; e.level(20); QVERIFY(e.storeCue("10"));
        e.level(80); QVERIFY(e.storeCue("20"));
        QCOMPARE(e.nextStoredCueNumber(), QString("30"));
        QVERIFY(e.sequences[0].cues.isEmpty());
        e.defaultFade = 0;
        QCOMPARE(e.insertStoredCue(0, "10"), 0);
        QCOMPARE(e.insertStoredCue(0, "20"), 1);
        QCOMPARE(e.insertStoredCue(0, "10"), 2);
        QCOMPARE(e.insertStoredCue(0, "10"), 3);
        int second = e.addSequence();
        auto &steps = e.sequences[0].cues;
        QCOMPARE(steps[0].number, QString("1")); QCOMPARE(steps[2].number, QString("3"));
        steps[2].time = 4; steps[3].time = 9;
        steps[1].link = "3";
        e.manual.clear(); e.go(0); QCOMPARE(e.output()[1], 20.);
        e.go(0); QCOMPARE(e.output()[1], 80.);
        e.go(0); QCOMPARE(e.sequences[0].current, 2);
        QCOMPARE(e.sequences[0].duration, 4.); e.tick(4); QCOMPARE(e.output()[1], 20.);
        // A shared edit changes the next recall of every occurrence, without moving live output.
        QVERIFY(e.updateStoredCue("10", {{1, {35, 0}}}));
        QCOMPARE(e.output()[1], 20.);
        e.go(0, 0); QCOMPARE(e.output()[1], 35.);
        QCOMPARE(e.tracked(0, 2)[1], 35.); QCOMPARE(steps[2].time, 4.); QCOMPARE(steps[3].time, 9.);
        e.insertStoredCue(second, "10");
        QCOMPARE(e.tracked(second, 0)[1], 35.);
        QString error;
        QVERIFY(!e.command("go to cue 10", &error)); QVERIFY(error.contains("several occurrences"));
        QVERIFY(e.command("step 3")); QCOMPARE(e.commandCue, 2);
        QVERIFY(e.moveStep(0, 2, 0));
        QCOMPARE(steps[0].number, QString("3")); QCOMPARE(steps[2].link, QString("3"));
        e.go(0, 2); e.go(0); QCOMPARE(e.sequences[0].current, 0); // Link follows the ID after reordering.
        QVERIFY(!e.deleteStoredCue("10"));
        QVERIFY(e.renameStoredCue("10", "30")); QCOMPARE(steps[0].reference, QString("30"));
        Engine loaded; QVERIFY(loaded.load(e.json())); QCOMPARE(loaded.json(), e.json());
        QCOMPARE(loaded.sequences[0].cues[0].reference, QString("30"));
        QVERIFY(e.removeStep(0, 0)); QVERIFY(steps[1].link.isEmpty());
        QCOMPARE(e.cueLibrary.size(), 2);
        auto invalid = loaded.json(); auto sequences = invalid["sequences"].toArray();
        auto sequence = sequences[0].toObject(); auto cues = sequence["cues"].toArray();
        auto cue = cues[0].toObject(); cue["reference"] = "999"; cues[0] = cue;
        sequence["cues"] = cues; sequences[0] = sequence; invalid["sequences"] = sequences;
        QVERIFY(!loaded.load(invalid));
    }
    void sharedCueUndoAndLegacyLoad() {
        Engine e; e.selection = {1}; e.level(50); e.record(0, "10", "Old cue", 0);
        auto legacy = e.json(); legacy["version"] = 1; legacy.remove("cueLibrary");
        Engine imported; QVERIFY(imported.load(legacy));
        imported.manual.clear(); imported.go(0); QCOMPARE(imported.output()[1], 50.);
        e.storeCue("10"); e.insertStoredCue(0, "10");
        EditHistory history; history.reset(e);
        e.updateStoredCue("10", {{1, {90, 0}}}); history.commit(e);
        QVERIFY(history.undo(e)); QCOMPARE(e.cueLibrary["10"].values[1].level, 50.);
        QVERIFY(history.redo(e)); QCOMPARE(e.cueLibrary["10"].values[1].level, 90.);
    }
    void submasterHtpAndPersistence() {
        Engine e;
        e.selection = {1};
        e.level(80);
        e.recordSubmaster();
        e.manual.clear();
        e.submasters[0].level = .5;
        QCOMPARE(e.output()[1], 40.);
        e.master = .5;
        QCOMPARE(e.output()[1], 20.);
        Engine restored;
        QVERIFY(restored.load(e.json()));
        QCOMPARE(restored.submasters.size(), 1);
        QCOMPARE(restored.submasters[0].values[1], 80.);
        QCOMPARE(restored.submasters[0].level, 0.);
    }
    void advancedPersistence() {
        Engine e;
        e.recordLook(0, "10", "cue-10", 2);
        auto &c = e.sequences[0].cues[0];
        c.down = 3;
        c.wait = 1;
        c.link = "10";
        c.channelTimes[1] = {4, 5, 6};
        Engine copy;
        QVERIFY(copy.load(e.json()));
        QCOMPARE(copy.json(), e.json());
    }
    void multiPatchOutputAndRoundtrip() {
        Engine e;
        e.patch[12] = (22 - 1) * 512 + 13;
        e.additionalPatch[12] = {(3 - 1) * 512 + 18, (3 - 1) * 512 + 512};
        Engine restored;
        QVERIFY(restored.load(e.json()));
        QCOMPARE(restored.dmxAddresses(12), e.dmxAddresses(12));
        QUdpSocket receiver;
        QVERIFY(receiver.bind(QHostAddress::LocalHost, 5568));
        e.selection = {12};
        e.level(100);
        Network n;
        n.enabled = true;
        n.protocol = "sACN";
        n.firstUniverse = 1;
        n.send(e);
        QVERIFY(receiver.waitForReadyRead(1000));
        QMap<int, QByteArray> frames;
        while (receiver.hasPendingDatagrams()) {
            auto p = receiver.receiveDatagram().data();
            int universe = (quint8(p[113]) << 8) | quint8(p[114]);
            frames[universe] = p.mid(126);
        }
        QVERIFY(frames.contains(22));
        QVERIFY(frames.contains(3));
        QCOMPARE(quint8(frames[22][12]), 255);
        QCOMPARE(quint8(frames[3][17]), 255);
        QCOMPARE(quint8(frames[3][511]), 255);
        e.blackout = true;
        n.send(e);
        QVERIFY(receiver.waitForReadyRead(1000));
        while (receiver.hasPendingDatagrams()) {
            auto p = receiver.receiveDatagram().data();
            QCOMPARE(quint8(p[138]), 0);
            QCOMPARE(quint8(p[143]), 0);
            QCOMPARE(quint8(p[637]), 0);
        }
    }
    void automaticLinkKeepsMainCursor() {
        Engine e;
        e.selection = {1};
        e.level(10);
        e.recordLook(0, "10", "cue-10", 2);
        e.level(20);
        e.recordLook(0, "20", "cue-20", 0);
        e.level(90);
        e.recordLook(0, "30", "cue-30", 1);
        e.sequences[0].cues[0].after = "30";
        e.manual.clear();
        e.go(0);
        e.tick(1);
        QVERIFY(e.linkedPlaybacks.isEmpty());
        e.sequences[0].paused = true;
        e.tick(5);
        QVERIFY(e.linkedPlaybacks.isEmpty());
        e.sequences[0].paused = false;
        e.tick(1);
        QCOMPARE(e.sequences[0].current, 0);
        QCOMPARE(e.linkedPlaybacks[0].current, 2);
        e.tick(1);
        QCOMPARE(e.output()[1], 90.);
        e.go(0);
        QCOMPARE(e.sequences[0].current, 1);
        QCOMPARE(e.output()[1], 20.);
        e.release(0);
        QVERIFY(e.linkedPlaybacks.isEmpty());
    }
    void eosLinkAndFollow() {
        Engine e;
        e.selection = {1};
        for (int i = 1; i <= 3; ++i) {
            e.level(i * 20);
            e.recordLook(0, QString::number(i * 10), "", 1);
        }
        e.manual.clear();
        e.sequences[0].cues[0].link = "30";
        e.go(0);
        e.tick(1);
        QCOMPARE(e.sequences[0].current, 0);
        QVERIFY(e.linkedPlaybacks.isEmpty());
        e.go(0);
        QCOMPARE(e.sequences[0].current, 2);
        e.sequences[0].cues[0].follow = .5;
        e.go(0, 0);
        e.tick(.5);
        QCOMPARE(e.sequences[0].current, 2);
    }
    void eosHangAndExecute() {
        Engine e;
        e.recordLook(0, "10", "", 2);
        e.recordLook(0, "20", "", 0);
        e.sequences[0].cues[0].hang = 1;
        e.go(0);
        e.tick(2.9);
        QCOMPARE(e.sequences[0].current, 0);
        e.tick(.1);
        QCOMPARE(e.sequences[0].current, 1);
        int s = e.addSequence();
        e.recordLook(s, "10", "", 0);
        e.sequences[0].cues[0].execute = "2/10";
        e.go(0, 0);
        QCOMPARE(e.sequences[0].current, 0);
        QCOMPARE(e.sequences[s].current, 0);
    }
    void eosTrackedManualAndOwnership() {
        Engine e;
        e.selection = {1};
        e.level(30);
        e.record(0, "10", "", 0);
        e.manual.clear();
        e.selection = {2};
        e.level(70);
        e.record(0, "20", "", 0);
        e.manual.clear();
        e.go(0);
        e.selection = {1};
        e.level(50);
        e.go(0);
        QCOMPARE(e.output()[1], 50.);
        QVERIFY(e.manual.contains(1));
        e.go(0, 1);
        QCOMPARE(e.output()[1], 30.);
        QVERIFY(!e.manual.contains(1));
    }
    void sequentialGoDoesNotStealTrackedChannels() {
        Engine e;
        e.selection = {1};
        e.level(30);
        e.record(0, "10", "", 0);
        e.manual.clear();
        e.selection = {2};
        e.level(70);
        e.record(0, "20", "", 0);
        e.manual.clear();
        int side = e.addSequence();
        e.selection = {1};
        e.level(80);
        e.record(side, "10", "", 0);
        e.manual.clear();
        e.go(0);
        e.go(side);
        QCOMPARE(e.output()[1], 80.);
        e.go(0);
        QCOMPARE(e.output()[1], 80.);
        e.go(0, 1);
        QCOMPARE(e.output()[1], 30.);
    }
    void unchangedFadeContinuesAcrossNextCue() {
        Engine e;
        e.selection = {1};
        e.level(100);
        e.record(0, "10", "", 4);
        e.manual.clear();
        e.selection = {2};
        e.level(100);
        e.record(0, "20", "", 0);
        e.manual.clear();
        e.go(0);
        e.tick(1);
        QCOMPARE(e.output()[1], 25.);
        e.go(0);
        e.tick(1);
        QCOMPARE(e.output()[1], 50.);
        QCOMPARE(e.output()[2], 100.);
    }
    void parkBypassesMastersAndIsNotRecorded() {
        Engine e;
        e.selection = {1};
        e.level(65);
        e.parkSelection(true);
        e.level(20);
        e.master = .1;
        e.blackout = true;
        QCOMPARE(e.output()[1], 65.);
        e.recordLook(0, "10", "", 0);
        QCOMPARE(e.tracked(0, 0)[1], 20.);
        e.recordSubmaster();
        QCOMPARE(e.submasters[0].values[1], 20.);
        e.parkSelection(false);
        QCOMPARE(e.output()[1], 0.);
    }
    void captureSurvivesPlayback() {
        Engine e;
        e.selection = {1};
        e.level(30);
        e.record(0, "10", "", 0);
        e.level(80);
        e.toggleCapture();
        e.go(0);
        QCOMPARE(e.output()[1], 80.);
        e.level(50);
        QCOMPARE(e.output()[1], 50.);
        e.toggleCapture();
        QVERIFY(!e.captured.contains(1));
        e.go(0, 0);
        QCOMPARE(e.output()[1], 30.);
    }
    void blockIsEditingAndAssertIsPlayback() {
        Engine e;
        e.selection = {1};
        e.level(30);
        e.record(0, "10", "", 0);
        e.manual.clear();
        e.record(0, "20", "", 0);
        e.setBlock(0, 1, true);
        e.editBlind(0, 0, 1, {70, 0});
        QCOMPARE(e.tracked(0, 1)[1], 30.);
        e.go(0);
        QCOMPARE(e.output()[1], 70.);
        e.go(0);
        QCOMPARE(e.output()[1], 30.);
        e.editBlind(0, 0, 1, {30, 0});
        e.go(0, 0);
        e.selection = {1};
        e.level(80);
        e.go(0);
        QCOMPARE(e.output()[1], 80.);
        e.sequences[0].cues[1].asserted = true;
        e.go(0, 0);
        e.level(80);
        e.go(0);
        QCOMPARE(e.output()[1], 30.);
        Engine copy;
        QVERIFY(copy.load(e.json()));
        QCOMPARE(copy.json(), e.json());
    }
    void blindEditsDoNotRecallLive() {
        Engine e;
        e.selection = {1};
        e.level(30);
        e.record(0, "10", "", 0);
        e.manual.clear();
        e.record(0, "20", "", 0);
        e.go(0);
        e.editBlind(0, 0, 1, {70, 0}, true);
        QCOMPARE(e.output()[1], 30.);
        QCOMPARE(e.tracked(0, 0)[1], 70.);
        QCOMPARE(e.tracked(0, 1)[1], 30.);
    }
    void editUndoDoesNotRewindTransport() {
        Engine e;
        e.selection = {1};
        e.level(100);
        e.record(0, "10", "", 4);
        e.manual.clear();
        EditHistory history;
        history.reset(e);
        e.editBlind(0, 0, 1, {50, 0});
        history.commit(e);
        e.go(0);
        e.tick(1);
        double live = e.output()[1];
        QVERIFY(history.undo(e));
        QCOMPARE(e.tracked(0, 0)[1], 100.);
        QCOMPARE(e.output()[1], live);
        QCOMPARE(e.sequences[0].elapsed, 1.);
        QVERIFY(history.redo(e));
        QCOMPARE(e.tracked(0, 0)[1], 50.);
        QCOMPARE(e.output()[1], live);
    }
    void commandProgrammingDoesNotAccidentallyFire() {
        Engine e;
        e.defaultFade = 0;
        QVERIFY(e.command("1 thru 4 + 8 - 2 at 50"));
        QCOMPARE(e.selection, QSet<int>({1, 3, 4, 8}));
        QVERIFY(e.command("record cue 10"));
        QCOMPARE(e.cueLibrary["10"].label, QString("cue-10"));
        QVERIFY(e.sequences[0].cues.isEmpty());
        QVERIFY(e.command("insert cue 10"));
        e.manual.clear();
        QVERIFY(e.command("step 1"));
        QCOMPARE(e.output().value(1), 0.);
        QVERIFY(e.command("go to step 1"));
        QCOMPARE(e.output()[1], 50.);
        QVERIFY(e.command("at + 10") == false);
        QVERIFY(e.command("at +10"));
        QCOMPARE(e.output()[1], 60.);
        QVERIFY(e.command("record only cue 20"));
        QCOMPARE(e.cueLibrary["20"].values[1].level, 60.);
    }
    void followOverlapPreservesUnchangedFade() {
        Engine e;
        e.selection = {1};
        e.level(100);
        e.record(0, "10", "", 10);
        e.manual.clear();
        e.selection = {2};
        e.level(100);
        e.record(0, "20", "", 4);
        e.manual.clear();
        e.sequences[0].cues[0].follow = 2;
        e.go(0);
        e.tick(3);
        QCOMPARE(e.sequences[0].current, 1);
        QCOMPARE(e.output()[1], 30.);
        QCOMPARE(e.output()[2], 25.);
    }
    void zeroTimeLoopIsBounded() {
        Engine e;
        e.recordLook(0, "10", "", 0);
        e.sequences[0].cues[0].link = "10";
        e.sequences[0].cues[0].follow = 0;
        e.go(0);
        QVERIFY(e.sequences[0].paused);
        QVERIFY(e.clock < 300);
    }
    void command() {
        Engine e;
        QString err;
        QVERIFY(e.command("Channel 1 Thru 4 At 50", &err));
        QCOMPARE(e.selection.size(), 4);
        QCOMPARE(e.output()[4], 50.);
        auto old = e.manual;
        QVERIFY(!e.command("1 thru 99999 at 50", &err));
        QCOMPARE(e.manual.size(), old.size());
        QVERIFY(!e.command("1 at 101"));
    }
    void tracking() {
        Engine e;
        e.selection = {1};
        e.level(30);
        e.record(0, "1", "Start", 0);
        e.manual.clear();
        e.selection = {2};
        e.level(70);
        e.record(0, "2", "Next", 0);
        QCOMPARE(e.tracked(0, 1)[1], 30.);
        e.manual.clear();
        e.go(0);
        QCOMPARE(e.output()[1], 30.);
        e.go(0);
        QCOMPARE(e.output()[2], 70.);
    }
    void paletteReferences() {
        Engine e;
        e.selection = {1};
        e.level(25);
        e.recordPalette(1);
        e.applyPalette(1);
        e.record(0, "1", "", 0);
        e.level(75);
        e.recordPalette(1);
        QCOMPARE(e.tracked(0, 0)[1], 75.);
    }
    void cueOnly() {
        Engine e;
        e.selection = {1};
        e.level(20);
        e.record(0, "1", "", 0);
        e.manual.clear();
        e.record(0, "2", "", 0);
        e.level(80);
        e.record(0, "1", "", 0, true);
        QCOMPARE(e.tracked(0, 0)[1], 80.);
        QCOMPARE(e.tracked(0, 1)[1], 20.);
    }
    void fadePause() {
        Engine e;
        e.selection = {1};
        e.level(100);
        e.record(0, "1", "", 2);
        e.manual.clear();
        e.go(0);
        e.tick(1);
        QCOMPARE(e.output()[1], 50.);
        e.sequences[0].paused = true;
        e.tick(1);
        QCOMPARE(e.output()[1], 50.);
        e.sequences[0].paused = false;
        e.tick(1);
        QCOMPARE(e.output()[1], 100.);
    }
    void prioritiesAndRelease() {
        Engine e;
        e.selection = {1};
        e.level(20);
        e.record(0, "1", "", 0);
        Sequence s;
        s.name = "Overlay";
        s.priority = 8;
        e.sequences << s;
        e.level(80);
        e.record(1, "1", "", 0);
        e.manual.clear();
        e.go(1);
        e.go(0);
        QCOMPARE(e.output()[1], 80.);
        e.release(1);
        QCOMPARE(e.output()[1], 20.);
        e.blackout = true;
        QCOMPARE(e.output()[1], 0.);
    }
    void persistence() {
        Engine e;
        e.selection = {1};
        e.level(35);
        e.recordPalette(1);
        e.applyPalette(1);
        e.record(0, "1", "test", 2);
        Engine copy;
        QVERIFY(copy.load(e.json()));
        QCOMPARE(copy.json(), e.json());
        auto j = e.json();
        j["version"] = 999;
        QVERIFY(!copy.load(j));
        QCOMPARE(copy.json(), e.json());
    }
    void packets() {
        QByteArray d(512, '\0');
        d[0] = char(255);
        auto a = artnet(d, 0, 1);
        QCOMPARE(a.size(), 530);
        QCOMPARE(a.left(8), QByteArray("Art-Net\0", 8));
        QCOMPARE(quint8(a[9]), 0x50);
        QCOMPARE(quint8(a[16]), 2);
        QCOMPARE(quint8(a[18]), 255);
        auto id = QUuid::createUuid();
        auto s = sacn(d, 1, 5, id);
        QCOMPARE(s.size(), 638);
        QCOMPARE(s.mid(22, 16), id.toRfc4122());
        QCOMPARE(quint8(s[111]), 5);
        QCOMPARE(quint8(s[114]), 1);
        QCOMPARE(quint8(s[126]), 255);
        QVERIFY(sacn(d, 0, 1, id).isEmpty());
    }
    void multipleNetworkOutputsAndTermination() {
        QUdpSocket art, streaming;
        art.setProxy(QNetworkProxy::NoProxy); streaming.setProxy(QNetworkProxy::NoProxy);
        QVERIFY(art.bind(QHostAddress::LocalHost, 6454));
        QVERIFY(streaming.bind(QHostAddress::LocalHost, 5568));
        Engine e;
        e.patch[1] = 513;
        e.selection = {1}; e.level(100);
        Network n;
        DmxOutput a, b;
        a.enabled = true; a.firstPatchUniverse = 2; a.firstUniverse = 7; a.universeCount = 1;
        a.interfaceAddress = "127.0.0.1";
        b = a; b.name = "sACN backup"; b.protocol = "sACN"; b.firstUniverse = 22;
        b.priority = 150; b.sourceName = "Theatre A";
        QVERIFY(n.applyOutputs({a, b})); n.send(e);
        QVERIFY(art.waitForReadyRead(1000)); QVERIFY(streaming.waitForReadyRead(1000));
        auto p = art.receiveDatagram().data();
        QCOMPARE(quint8(p[14]), 7); QCOMPARE(quint8(p[18]), 255);
        p = streaming.receiveDatagram().data();
        QCOMPARE(quint8(p[114]), 22); QCOMPARE(quint8(p[126]), 255);
        QCOMPARE(quint8(p[108]), 150); QVERIFY(p.mid(44).startsWith("Theatre A"));
        QCOMPARE(n.state(0).packets, quint64(1)); QCOMPARE(n.state(1).packets, quint64(1));
        auto bad = b; bad.destinations = "invalid";
        QVERIFY(!n.applyOutputs({bad})); QVERIFY(n.enabled); // Atomic rejection preserves running routes.
        Network loaded;
        QVERIFY(loaded.loadOutputs(n.outputJson()));
        QVERIFY(!loaded.enabled); QCOMPARE(loaded.outputs[1].firstUniverse, 22);
        QCOMPARE(loaded.outputs[1].priority, 150);
        n.stop(); QVERIFY(!n.enabled);
        QVERIFY(streaming.waitForReadyRead(1000));
        int terminated = 0;
        while (streaming.hasPendingDatagrams()) {
            p = streaming.receiveDatagram().data();
            QVERIFY(quint8(p[112]) & 0x40); ++terminated;
        }
        QCOMPARE(terminated, 3);
    }
    void udpLoopback() {
        QUdpSocket receiver;
        QVERIFY(receiver.bind(QHostAddress::LocalHost, 6454));
        Engine e;
        e.selection = {1};
        e.level(100);
        Network n;
        n.enabled = true;
        n.firstUniverse = 0;
        n.send(e);
        QVERIFY(receiver.waitForReadyRead(1000));
        auto p = receiver.receiveDatagram().data();
        QCOMPARE(p.size(), 530);
        QCOMPARE(quint8(p[18]), 255);
        e.blackout = true;
        n.send(e);
        QVERIFY(receiver.waitForReadyRead(1000));
        QCOMPARE(quint8(receiver.receiveDatagram().data()[18]), 0);
    }
    void gdtfArchive() {
        QByteArray xml =
            "<GDTF><FixtureType Name=\"Wash\" Manufacturer=\"Test\"><DMXModes><DMXMode "
            "Name=\"Basic\"><DMXChannels><DMXChannel Offset=\"1,2\"><LogicalChannel "
            "Attribute=\"Pan\"/></DMXChannel></DMXChannels></DMXMode></DMXModes></FixtureType></"
            "GDTF>";
        QByteArray name = "description.xml", b(30, '\0');
        auto put16 = [](QByteArray &a, int p, quint16 v) {
            a[p] = char(v);
            a[p + 1] = char(v >> 8);
        };
        auto put32 = [&](QByteArray &a, int p, quint32 v) {
            put16(a, p, v);
            put16(a, p + 2, v >> 16);
        };
        quint32 crc = crc32(0, reinterpret_cast<const Bytef *>(xml.constData()), xml.size());
        b.replace(0, 4, QByteArray("PK\3\4", 4));
        put16(b, 26, name.size());
        put32(b, 14, crc);
        put32(b, 18, xml.size());
        put32(b, 22, xml.size());
        b += name + xml;
        int central = b.size();
        QByteArray c(46, '\0');
        c.replace(0, 4, QByteArray("PK\1\2", 4));
        put32(c, 16, crc);
        put32(c, 20, xml.size());
        put32(c, 24, xml.size());
        put16(c, 28, name.size());
        b += c + name;
        QByteArray end(22, '\0');
        end.replace(0, 4, QByteArray("PK\5\6", 4));
        put16(end, 8, 1);
        put16(end, 10, 1);
        put32(end, 12, c.size() + name.size());
        put32(end, 16, central);
        b += end;
        FixtureProfile profile;
        QString error;
        QVERIFY2(readGdtf(b, profile, error), qPrintable(error));
        QCOMPARE(profile.name, QString("Wash"));
        QCOMPARE(profile.modes[0].footprint, 2);
        QCOMPARE(profile.modes[0].attributes, QStringList{"Pan"});
        b[30 + name.size()] = '!';
        QVERIFY(!readGdtf(b, profile, error));
        QVERIFY(!readGdtf(QByteArray("broken"), profile, error));
    }
    void groupAndPaletteCommands() {
        Engine e;
        e.groups[1] = {{2, 0}, {4, 0}};
        QVERIFY(e.command("group 1"));
        QCOMPARE(e.selection, QSet<int>({2, 4}));
        e.level(60);
        e.recordPalette(2);
        e.level(0);
        QVERIFY(e.command("intensity 2"));
        QCOMPARE(e.output()[2], 60.);
    }
    void pixelLayer() {
        Engine e;
        e.pixels[1] = 80;
        QCOMPARE(e.output()[1], 80.);
        e.master = .5;
        QCOMPARE(e.output()[1], 40.);
        e.selection = {1};
        e.level(20);
        QCOMPARE(e.output()[1], 10.);
    }
    void osc() {
        QString a;
        float v;
        auto b = oscMessage("/coldflame/master", 0.5);
        QVERIFY(oscDecode(b, a, v));
        QCOMPARE(v, 0.5f);
        QCOMPARE(a, QString("/coldflame/master"));
        QVERIFY(!oscDecode(b.left(b.size() - 1), a, v));
    }
};
QTEST_GUILESS_MAIN(Tests)
#include "engine_test.moc"
