#pragma once
#include <QtCore>
namespace Flame {
struct FixtureParameter {
    QString attribute;
    QList<int> offsets; // Most significant byte first, 1-based within the fixture.
    double home = 0;    // Normalized DMX percentage, independent of physical units.
    bool intensity = false, snap = false;
    QString cell; // Empty = fixture master; otherwise a stable geometry instance path.
};
struct FixtureMode {
    QString name;
    QStringList attributes;
    int footprint = 0;
    QList<FixtureParameter> parameters;
    QString unsupported;
};
struct FixtureProfile {
    QString manufacturer, name;
    QList<FixtureMode> modes;
    QByteArray source; // Preserve vendor metadata on GDTF re-export.
};
bool readGdtf(const QByteArray &archive, FixtureProfile &profile, QString &error);
QByteArray writeGdtf(const FixtureProfile &profile);
QMap<QString, FixtureProfile> genericFixtures();
QJsonObject fixtureJson(const FixtureProfile &profile);
bool fixtureFromJson(const QJsonObject &json, FixtureProfile &profile, QString &error);
bool validateMode(const FixtureMode &mode, QString &error);
void ensureCellControls(FixtureMode &mode);
bool parseGdtfDescription(const QByteArray &xml, FixtureProfile &profile, QString &error);
} // namespace Flame
