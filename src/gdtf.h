#pragma once
#include <QtCore>
namespace Flame {
struct FixtureMode {
    QString name;
    QStringList attributes;
    int footprint = 0;
};
struct FixtureProfile {
    QString manufacturer, name;
    QList<FixtureMode> modes;
};
bool readGdtf(const QByteArray &archive, FixtureProfile &profile, QString &error);
} // namespace Flame
