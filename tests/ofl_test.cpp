#include "engine.h"
#include <QtTest>
using namespace Flame;
class OflTests : public QObject {
    Q_OBJECT
    QByteArray fixture(const char *name) {
        QFile file(QFINDTESTDATA(name)); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll();
    }
    QJsonObject desk() { return QJsonDocument::fromJson(fixture("fixtures/ofl-desk-channel.json")).object(); }
    static bool read(QJsonObject json, FixtureProfile &profile, QString &issue) { return readOfl(QJsonDocument(json).toJson(), profile, issue, "Generic"); }
    static bool available(const FixtureProfile &profile, int mode) { QString issue; return validateMode(profile.modes[mode], issue); }
  private slots:
    void upstreamDeskChannelResolutionsAndOriginalExport() {
        const auto bytes = fixture("fixtures/ofl-desk-channel.json"); QVERIFY(!bytes.isEmpty());
        FixtureProfile profile; QString issue; QVERIFY2(readOfl(bytes, profile, issue, "Generic"), qPrintable(issue));
        QCOMPARE(profile.manufacturer, QString("Generic")); QCOMPARE(profile.modes.size(), 3);
        for (int m = 0; m < 3; ++m) {
            QVERIFY2(available(profile, m), qPrintable(profile.modes[m].unsupported));
            QCOMPARE(profile.modes[m].parameters.size(), 1); QCOMPARE(profile.modes[m].parameters[0].offsets.size(), m + 1);
            QVERIFY(profile.modes[m].parameters[0].intensity);
        }
        QCOMPARE(writeOfl(profile, issue), bytes);
        auto exported = desk(); exported["$schema"] = "https://raw.githubusercontent.com/OpenLightingProject/open-fixture-library/schema-12.6.0/schemas/fixture.json";
        exported["manufacturerKey"] = "vendor"; exported["fixtureKey"] = "desk"; exported["oflURL"] = "https://open-fixture-library.org/vendor/desk";
        QVERIFY(readOfl(QJsonDocument(exported).toJson(), profile, issue)); QCOMPARE(profile.manufacturer, QString("vendor"));
    }
    void fineDefaultResolutionAndNonAdjacentBytes() {
        auto json = desk(); auto channels = json["availableChannels"].toObject(); auto intensity = channels["Intensity"].toObject();
        intensity["defaultValue"] = 65535; channels["Intensity"] = intensity; json["availableChannels"] = channels;
        FixtureProfile profile; QString issue; QVERIFY(read(json, profile, issue));
        QCOMPARE(profile.modes[0].parameters[0].home, 0.); // 24-bit default's high byte is zero.
        QCOMPARE(profile.modes[1].parameters[0].home, 255. * 100. / 65535.);
        intensity["dmxValueResolution"] = "8bit"; intensity["defaultValue"] = 128;
        channels["Intensity"] = intensity; json["availableChannels"] = channels;
        auto modes = json["modes"].toArray(); auto mode = modes[1].toObject(); mode["channels"] = QJsonArray{"Intensity fine", QJsonValue(QJsonValue::Null), "Intensity"}; modes[1] = mode; json["modes"] = modes;
        QVERIFY(read(json, profile, issue)); QCOMPARE(profile.modes[1].footprint, 3); QCOMPARE(profile.modes[1].parameters[0].offsets, QList<int>({3, 1}));
        QCOMPARE(profile.modes[1].parameters[0].home, 128. * 100. / 255.);
        intensity["defaultValue"] = "25%"; channels["Intensity"] = intensity; json["availableChannels"] = channels;
        QVERIFY(read(json, profile, issue)); QCOMPARE(profile.modes[2].parameters[0].home, 25.);
    }
    void matrixOrderCellsColourIntensityCueAndPersistence() {
        const auto bytes = fixture("fixtures/ofl-matrix.json"); FixtureProfile profile; QString issue;
        QVERIFY2(readOfl(bytes, profile, issue, "Test"), qPrintable(issue)); QCOMPARE(profile.modes.size(), 3);
        for (int m = 0; m < 3; ++m) QVERIFY2(available(profile, m), qPrintable(profile.modes[m].unsupported));
        auto mode = profile.modes[0]; QCOMPARE(mode.footprint, 20); QCOMPARE(mode.parameters[1].offsets, QList<int>({2, 5}));
        QCOMPARE(mode.parameters[1].cell, QString("%281%2C%201%29")); QCOMPARE(mode.parameters[4].cell, QString("%282%2C%201%29"));
        QCOMPARE(mode.parameters[13].offsets, QList<int>({18, 20}));
        QCOMPARE(profile.modes[1].parameters[1].cell, QString("%281%2C%202%29"));
        QCOMPARE(profile.modes[1].parameters[4].offsets, QList<int>({5})); // First green in per-channel ordering.
        Engine engine; engine.fixtureProfiles["matrix"] = profile; engine.fixtures[1] = {"matrix", 0}; engine.patch[1] = 513;
        QCOMPARE(engine.fixtureCells(1).size(), 4); engine.manual[1] = {50, 0};
        const QString cell = mode.parameters[1].cell; const auto controls = engine.colorControls(1, cell); QVERIFY(controls.available());
        auto color = engine.colorControls(1, mode.parameters[4].cell); QVERIFY(color.available());
        engine.colorPalettes[1] = {"Blue", 0, 0, 1};
        QVERIFY(engine.applyColorPalette(1, 1, cell));
        auto frame = engine.dmxFrames().value(1); QCOMPARE(quint8(frame[0]), 128); QCOMPARE(quint8(frame[1]), 0); QCOMPARE(quint8(frame[3]), 255);
        engine.record(0, "10", "OFL blue", 0, false); engine.manual.clear(); engine.go(0); engine.tick(0);
        frame = engine.dmxFrames().value(1); QCOMPARE(quint8(frame[0]), 128); QCOMPARE(quint8(frame[1]), 0); QCOMPARE(quint8(frame[3]), 255);
        Engine loaded; QVERIFY2(loaded.load(engine.json(), &issue), qPrintable(issue));
        QCOMPARE(loaded.fixtureProfiles["matrix"].oflSource, bytes); QCOMPARE(loaded.fixtureCells(1), engine.fixtureCells(1));
        QCOMPARE(writeOfl(loaded.fixtureProfiles["matrix"], issue), bytes);
        const auto gdtf = writeGdtf(profile); QVERIFY(!gdtf.isEmpty()); FixtureProfile converted; QVERIFY2(readGdtf(gdtf, converted, issue), qPrintable(issue));
        QCOMPARE(converted.modes[0].footprint, 20);
    }
    void unsupportedModesAreIsolatedAndBadFilesAreAtomic() {
        auto json = desk(); auto modes = json["modes"].toArray(); auto bad = modes[0].toObject();
        bad["name"] = "Bad alias"; bad["channels"] = QJsonArray{"Intensity fine"}; modes << bad;
        bad["name"] = "Duplicate channel"; bad["channels"] = QJsonArray{"Intensity", "Intensity"}; modes << bad;
        bad["name"] = "Oversized"; QJsonArray huge; for (int i = 0; i < 513; ++i) huge << QJsonValue(QJsonValue::Null); bad["channels"] = huge; modes << bad;
        json["modes"] = modes; FixtureProfile profile; QString issue; QVERIFY(read(json, profile, issue));
        for (int m = 0; m < 3; ++m) QVERIFY(available(profile, m));
        for (int m = 3; m < 6; ++m) { QVERIFY(!available(profile, m)); QVERIFY(!profile.modes[m].unsupported.isEmpty()); }
        const auto original = fixtureJson(profile); QVERIFY(!readOfl("{}", profile, issue)); QCOMPARE(fixtureJson(profile), original);
        QVERIFY(!readOfl(QByteArray(16 * 1024 * 1024 + 1, ' '), profile, issue)); QCOMPARE(fixtureJson(profile), original);
        auto collision = original; collision["gdtf"] = "AAAA"; QVERIFY(!fixtureFromJson(collision, profile, issue));
    }
    void switchingAndPartialIntensityCannotDriveWrongOutputs() {
        auto json = desk(); QJsonObject channel{{"capabilities", QJsonArray{
            QJsonObject{{"dmxRange", QJsonArray{0, 127}}, {"type", "Intensity"}},
            QJsonObject{{"dmxRange", QJsonArray{128, 255}}, {"type", "NoFunction"}}}}};
        json["availableChannels"] = QJsonObject{{"Intensity", channel}};
        json["modes"] = QJsonArray{QJsonObject{{"name", "Partial"}, {"channels", QJsonArray{"Intensity"}}}};
        FixtureProfile profile; QString issue; QVERIFY(read(json, profile, issue)); QVERIFY(!available(profile, 0));
        QVERIFY(profile.modes[0].unsupported.contains("partial-range"));
        auto capabilities = channel["capabilities"].toArray(); auto cap = capabilities[0].toObject(); cap["switchChannels"] = QJsonObject{{"Alias", "Pan"}}; capabilities[0] = cap; channel["capabilities"] = capabilities;
        json["availableChannels"] = QJsonObject{{"Intensity", channel}}; QVERIFY(read(json, profile, issue)); QVERIFY(profile.modes[0].unsupported.contains("switching"));
    }
    void rawCapabilitiesAreVisibleWithoutInventingSemantics() {
        auto json = desk();
        const QJsonArray ranges{
            QJsonObject{{"type", "ShutterStrobe"}, {"shutterEffect", "Open"}, {"dmxRange", QJsonArray{0, 127}}},
            QJsonObject{{"type", "ShutterStrobe"}, {"shutterEffect", "Strobe"}, {"dmxRange", QJsonArray{128, 255}}}};
        json["availableChannels"] = QJsonObject{{"Shutter", QJsonObject{{"defaultValue", 10}, {"capabilities", ranges}}}};
        json["modes"] = QJsonArray{QJsonObject{{"name", "Raw shutter"}, {"channels", QJsonArray{"Shutter"}}}};
        FixtureProfile profile; QString issue; QVERIFY(read(json, profile, issue)); QVERIFY(available(profile, 0));
        const auto mode = profile.modes[0]; QCOMPARE(mode.parameters[0].attribute, QString("Shutter"));
        QVERIFY(mode.parameters[0].snap); QVERIFY(!mode.parameters[0].intensity); QCOMPARE(mode.warnings.size(), 1);
        QVERIFY(mode.warnings[0].contains("raw DMX")); QCOMPARE(mode.parameters[0].home, 10. * 100. / 255.);
        auto invalid = json["availableChannels"].toObject(); auto channel = invalid["Shutter"].toObject(); channel["constant"] = true; invalid["Shutter"] = channel; json["availableChannels"] = invalid;
        QVERIFY(read(json, profile, issue)); QVERIFY(!available(profile, 0)); QVERIFY(profile.modes[0].unsupported.contains("constant"));
    }
    void sparseMatrixFineAliasesAndLimits() {
        auto json = QJsonDocument::fromJson(fixture("fixtures/ofl-matrix.json")).object();
        json["matrix"] = QJsonObject{{"pixelKeys", QJsonArray{QJsonArray{QJsonArray{"Left", QJsonValue(QJsonValue::Null), "Right"}}}}};
        auto mode = json["modes"].toArray()[0].toObject(); mode["channels"] = QJsonArray{QJsonObject{{"insert", "matrixChannels"}, {"repeatFor", "eachPixelXYZ"}, {"channelOrder", "perPixel"}, {"templateChannels", QJsonArray{"Red $pixelKey", "Red fine $pixelKey", "Green $pixelKey", "Blue $pixelKey"}}}};
        json["modes"] = QJsonArray{mode}; FixtureProfile profile; QString issue; QVERIFY(read(json, profile, issue)); QVERIFY(available(profile, 0));
        QCOMPARE(profile.modes[0].footprint, 8); QCOMPARE(profile.modes[0].parameters[0].cell, QString("Left"));
        QCOMPARE(profile.modes[0].parameters[3].cell, QString("Right"));
        json["matrix"] = QJsonObject{{"pixelKeys", QJsonArray{QJsonArray{QJsonArray{"10", "2", "1"}}}}};
        auto insert = mode["channels"].toArray()[0].toObject(); insert["repeatFor"] = "eachPixelABC";
        mode["channels"] = QJsonArray{insert}; json["modes"] = QJsonArray{mode};
        QVERIFY(read(json, profile, issue)); QVERIFY(available(profile, 0));
        QCOMPARE(profile.modes[0].parameters[0].cell, QString("1")); QCOMPARE(profile.modes[0].parameters[3].cell, QString("2")); QCOMPARE(profile.modes[0].parameters[6].cell, QString("10"));
        json["matrix"] = QJsonObject{{"pixelCount", QJsonArray{512, 512, 512}}}; QVERIFY(read(json, profile, issue)); QVERIFY(!available(profile, 0));
        QVERIFY(profile.modes[0].unsupported.contains("512 pixels"));
    }
    void nativeExportsPreserveSupportedParametersAndValidateWithOflSchema() {
        auto profiles = genericFixtures(); FixtureProfile matrix; QString issue;
        QVERIFY(readOfl(fixture("fixtures/ofl-matrix.json"), matrix, issue, "Test")); matrix.oflSource.clear(); profiles["matrix"] = matrix;
        FixtureProfile beam = genericFixtures()["dimmer8"]; beam.name = "Beam controls";
        auto &mode = beam.modes[0]; mode.parameters = {{"Zoom", {1}, 50}, {"Focus1", {2}, 25}, {"Iris", {3}, 100}, {"Frost1", {4}, 0}, {"CTO", {5}, 50}, {"NoFeature", {6}, 0}, {"Prism", {7}, 0, false, true}};
        mode.footprint = 7; profiles["beam"] = beam;
        const auto folder = qEnvironmentVariable("FLAME_OFL_EXPORT_DIR");
        for (auto it = profiles.cbegin(); it != profiles.cend(); ++it) {
            const auto bytes = writeOfl(it.value(), issue); QVERIFY2(!bytes.isEmpty(), qPrintable(it.key() + ": " + issue));
            FixtureProfile loaded; QVERIFY2(readOfl(bytes, loaded, issue), qPrintable(issue)); QCOMPARE(loaded.manufacturer, it->manufacturer);
            QCOMPARE(loaded.modes.size(), it->modes.size());
            for (int m = 0; m < loaded.modes.size(); ++m) {
                QVERIFY2(available(loaded, m), qPrintable(loaded.modes[m].unsupported)); QCOMPARE(loaded.modes[m].footprint, it->modes[m].footprint);
                const auto before = it->modes[m].parameters;
                for (const auto &parameter : before) {
                    bool found = false;
                    for (const auto &next : loaded.modes[m].parameters) if (next.offsets == parameter.offsets && next.cell == parameter.cell && next.attribute == parameter.attribute) {
                        QCOMPARE(next.intensity, parameter.intensity); QCOMPARE(next.snap, parameter.snap); QVERIFY(qAbs(next.home - parameter.home) <= 100. / 255.); found = true; break;
                    }
                    QVERIFY2(found, qPrintable(it.key() + " missing " + parameter.attribute + " / " + parameter.cell));
                }
            }
            if (!folder.isEmpty()) { QFile file(folder + "/" + it.key() + ".json"); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), bytes.size()); }
        }
        auto invalid = profiles["dimmer8"]; invalid.modes[0].name = "My mode"; QVERIFY(writeOfl(invalid, issue).isEmpty()); QVERIFY(issue.contains("Rename"));
        invalid = profiles["dimmer8"]; invalid.modes[0].parameters[0].snap = true; QVERIFY(writeOfl(invalid, issue).isEmpty()); QVERIFY(issue.contains("Snap"));
        invalid = profiles["dimmer8"]; invalid.modes[0].parameters[0].offsets = {1, 2, 3, 4}; invalid.modes[0].footprint = 4; QVERIFY(writeOfl(invalid, issue).isEmpty());
    }
};
QTEST_GUILESS_MAIN(OflTests)
#include "ofl_test.moc"
