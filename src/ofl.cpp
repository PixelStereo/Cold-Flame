#include "gdtf.h"
#include <algorithm>
#include <array>
#include <cmath>

using namespace Flame;
namespace {
constexpr int MaxBytes = 16 * 1024 * 1024;
const QString Schema = "https://raw.githubusercontent.com/OpenLightingProject/open-fixture-library/master/schemas/fixture.json";
bool textValid(const QString &s, int maximum = 256) {
    return !s.trimmed().isEmpty() && s.size() <= maximum && !s.contains(QRegularExpression("[\\x00-\\x1f]"));
}
quint64 maximum(int bytes) { return (quint64(1) << (bytes * 8)) - 1; }
QString cellPath(QString key) { return QString::fromLatin1(QUrl::toPercentEncoding(key)); }
struct Pixel { QString key; std::array<int, 3> position; };
struct Channel { QJsonObject json; QString cell; QStringList keys; };

// OFL uses X as the fastest axis in eachPixelXYZ. Sparse pixelKeys retain their coordinates.
bool pixels(const QJsonObject &matrix, QList<Pixel> &result, QString &issue) {
    auto fail = [&](QString s) { issue = s; return false; };
    if (matrix.contains("pixelCount") == matrix.contains("pixelKeys")) return fail("Matrix needs pixelCount or pixelKeys");
    if (matrix.contains("pixelCount")) {
        const auto dimensions = matrix["pixelCount"].toArray();
        if (dimensions.size() != 3) return fail("Matrix pixelCount needs three dimensions");
        std::array<int, 3> size; qint64 total = 1; int axes = 0;
        for (int i = 0; i < 3; ++i) {
            size[i] = dimensions[i].toInt();
            if (size[i] < 1 || size[i] > 512) return fail("Matrix dimensions must be 1–512");
            total *= size[i]; if (size[i] > 1) ++axes;
        }
        if (total > 512) return fail("Matrix exceeds 512 pixels");
        for (int z = 1; z <= size[2]; ++z) for (int y = 1; y <= size[1]; ++y) for (int x = 1; x <= size[0]; ++x) {
            std::array<int, 3> position{x, y, z}; QStringList coordinates;
            for (int i = 0; i < 3; ++i) if (size[i] > 1) coordinates << QString::number(position[i]);
            // A single pixel is accepted as key "1".
            QString key = axes < 2 ? QString::number(qMax(x, qMax(y, z))) : "(" + coordinates.join(", ") + ")";
            result << Pixel{key, position};
        }
    } else {
        const auto zs = matrix["pixelKeys"].toArray(); QSet<QString> keys; int positions = 0;
        if (zs.isEmpty() || zs.size() > 512) return fail("Invalid matrix pixelKeys");
        for (int z = 0; z < zs.size(); ++z) {
            const auto ys = zs[z].toArray(); if (ys.isEmpty() || ys.size() > 512) return fail("Invalid matrix rows");
            for (int y = 0; y < ys.size(); ++y) {
                const auto xs = ys[y].toArray(); if (xs.isEmpty() || xs.size() > 512) return fail("Invalid matrix columns");
                positions += xs.size(); if (positions > 4096) return fail("Matrix exceeds 4096 positions");
                for (int x = 0; x < xs.size(); ++x) {
                    if (xs[x].isNull()) continue;
                    const auto key = xs[x].toString();
                    if (!textValid(key) || keys.contains(key) || key.contains('$')) return fail("Invalid or duplicate matrix pixel key");
                    keys.insert(key); result << Pixel{key, {x + 1, y + 1, z + 1}};
                    if (result.size() > 512) return fail("Matrix exceeds 512 pixels");
                }
            }
        }
    }
    return !result.isEmpty() || fail("Matrix has no pixels");
}
bool registerChannel(const QString &key, const QJsonObject &json, QString cell, QList<Channel> &channels,
                     QMap<QString, QPair<int, int>> &lookup, QString &issue) {
    QStringList keys{key}; const auto aliases = json["fineChannelAliases"];
    if (!aliases.isUndefined() && (!aliases.isArray() || aliases.toArray().size() > 2)) { issue = "Only 8-, 16- and 24-bit OFL channels are supported"; return false; }
    for (const auto &value : aliases.toArray()) keys << value.toString();
    const int index = channels.size();
    for (int byte = 0; byte < keys.size(); ++byte) {
        if (!textValid(keys[byte]) || keys[byte].contains('$') || lookup.contains(keys[byte])) { issue = "Invalid or ambiguous OFL channel key: " + keys[byte]; return false; }
        lookup[keys[byte]] = {index, byte};
    }
    channels << Channel{json, cell, keys}; return true;
}
QString attributeFor(const QJsonObject &capability) {
    const QString type = capability["type"].toString();
    if (type == "Intensity") return "Dimmer";
    if (type == "ColorIntensity") {
        const QMap<QString, QString> colors{{"Red", "R"}, {"Green", "G"}, {"Blue", "B"}, {"White", "W"},
            {"Warm White", "WW"}, {"Cold White", "CW"}, {"Amber", "A"}, {"UV", "UV"}, {"Lime", "L"}, {"Indigo", "I"}, {"Cyan", "C"}, {"Magenta", "M"}, {"Yellow", "Y"}};
        return colors.contains(capability["color"].toString()) ? "ColorAdd_" + colors.value(capability["color"].toString()) : QString();
    }
    const QMap<QString, QString> attributes{{"Pan", "Pan"}, {"Tilt", "Tilt"}, {"Zoom", "Zoom"}, {"Focus", "Focus1"},
        {"Iris", "Iris"}, {"Frost", "Frost1"}, {"ColorTemperature", "CTO"}, {"NoFunction", "NoFeature"}};
    return attributes.value(type);
}
bool channelParameter(const Channel &channel, QList<int> offsets, FixtureParameter &parameter, QStringList &warnings, QString &issue) {
    auto fail = [&](QString s) { issue = channel.keys.first() + ": " + s; return false; };
    const auto j = channel.json; QJsonArray capabilities;
    if (j.contains("capability") == j.contains("capabilities")) return fail("needs capability or capabilities");
    if (j.contains("capability")) capabilities << j["capability"];
    else capabilities = j["capabilities"].toArray();
    if (capabilities.isEmpty() || capabilities.size() > 1024) return fail("invalid capability count");
    if (j["constant"].toBool()) return fail("constant output channels are not supported");
    QString resolution = j["dmxValueResolution"].toString();
    int sourceBytes = resolution.isEmpty() ? channel.keys.size() : resolution == "8bit" ? 1 : resolution == "16bit" ? 2 : resolution == "24bit" ? 3 : 0;
    if (!sourceBytes || sourceBytes > channel.keys.size()) return fail("invalid DMX value resolution");
    quint64 next = 0;
    for (const auto &entry : capabilities) {
        const auto cap = entry.toObject();
        if (!textValid(cap["type"].toString())) return fail("missing capability type");
        if (cap.contains("switchChannels")) return fail("conditional switching channels are not supported");
        if (j.contains("capabilities")) {
            auto range = cap["dmxRange"].toArray();
            if (range.size() != 2 || range[0].toDouble(-1) != double(next) || range[1].toDouble(-1) < double(next) ||
                range[1].toDouble(-1) > double(maximum(sourceBytes)) || std::floor(range[1].toDouble()) != range[1].toDouble())
                return fail("capability ranges must cover DMX without gaps or overlap");
            next = quint64(range[1].toDouble()) + 1;
        }
    }
    if (j.contains("capabilities") && next != maximum(sourceBytes) + 1) return fail("capability ranges do not cover the channel");
    parameter.offsets = offsets; parameter.cell = channel.cell;
    const auto cap = capabilities.first().toObject();
    parameter.attribute = capabilities.size() == 1 ? attributeFor(cap) : QString();
    // Never treat a partial-range or reversed dimmer/emitter as a linear intensity control.
    for (const auto &entry : capabilities) {
        const auto c = entry.toObject(); const QString t = c["type"].toString();
        if (t == "Intensity" || t == "ColorIntensity") {
            if (capabilities.size() != 1 || c.contains("brightness") ||
                (c.contains("brightnessStart") && (c["brightnessStart"] != "0%" || c["brightnessEnd"] != "100%")))
                return fail("non-linear or partial-range intensity needs a capability evaluator");
        }
    }
    parameter.intensity = parameter.attribute == "Dimmer";
    const QString precedence = j["precedence"].toString();
    if ((!precedence.isEmpty() && precedence != "HTP" && precedence != "LTP") ||
        (precedence == "HTP" && !parameter.intensity) || (precedence == "LTP" && parameter.intensity)) return fail("unsupported channel precedence");
    if (parameter.attribute.isEmpty()) {
        parameter.attribute = j["name"].toString(channel.keys.first());
        // Keep unmodelled controls distinct from engine intensity and colour semantics.
        if (parameter.attribute == "Dimmer" || parameter.attribute.startsWith("ColorAdd_") || parameter.attribute.startsWith("ColorSub_")) parameter.attribute.prepend("Raw ");
        parameter.snap = true;
        warnings << channel.keys.first() + ": raw DMX control; capability ranges and physical units are not evaluated";
    }
    const auto home = j["defaultValue"];
    if (home.isString()) {
        const auto match = QRegularExpression("^(\\d+(?:\\.\\d+)?)%$").match(home.toString());
        if (!match.hasMatch() || match.captured(1).toDouble() > 100) return fail("invalid default percentage");
        parameter.home = match.captured(1).toDouble();
    } else if (!home.isUndefined()) {
        const double value = home.toDouble(-1);
        if (value < 0 || value > double(maximum(sourceBytes)) || std::floor(value) != value) return fail("default is outside its DMX resolution");
        quint64 raw = quint64(value);
        if (sourceBytes > offsets.size()) raw >>= 8 * (sourceBytes - offsets.size());
        parameter.home = double(raw) * 100. / double(maximum(qMin(sourceBytes, int(offsets.size()))));
    }
    return true;
}
struct Catalogue {
    QList<Pixel> matrixPixels;
    QMap<QString, QPair<int, int>> lookup;
    QList<Channel> channels;
    QSet<QString> pixelKeys;
    QString issue;
};
bool compileCatalogue(const QJsonObject &json, Catalogue &catalogue) {
    auto &matrixPixels = catalogue.matrixPixels; auto &lookup = catalogue.lookup;
    auto &channels = catalogue.channels; auto &pixelKeys = catalogue.pixelKeys; auto &issue = catalogue.issue;
    const bool hasTemplates = !json["templateChannels"].toObject().isEmpty();
    if (hasTemplates && !pixels(json["matrix"].toObject(), matrixPixels, issue)) return false;
    const auto available = json["availableChannels"].toObject(); const auto templates = json["templateChannels"].toObject();
    if (available.size() > 4096 || qint64(templates.size()) * matrixPixels.size() > 65536) { issue = "Too many channel definitions"; return false; }
    for (auto it = available.begin(); it != available.end(); ++it)
        if (!it.value().isObject() || !registerChannel(it.key(), it.value().toObject(), {}, channels, lookup, issue)) return false;
    for (const auto &pixel : matrixPixels) {
        pixelKeys.insert(pixel.key);
        for (auto it = templates.begin(); it != templates.end(); ++it) {
            if (!it.key().contains("$pixelKey") || !it.value().isObject()) { issue = "Invalid template channel"; return false; }
            auto channel = it.value().toObject();
            if (channel.contains("fineChannelAliases")) {
                QJsonArray aliases; for (auto alias : channel["fineChannelAliases"].toArray()) aliases << alias.toString().replace("$pixelKey", pixel.key);
                channel["fineChannelAliases"] = aliases;
            }
            if (!registerChannel(QString(it.key()).replace("$pixelKey", pixel.key), channel, cellPath(pixel.key), channels, lookup, issue)) return false;
        }
    }
    return true;
}
bool expandMode(const Catalogue &catalogue, const QJsonArray &entries, FixtureMode &mode, QString &issue) {
    const auto &matrixPixels = catalogue.matrixPixels; const auto &lookup = catalogue.lookup;
    const auto &channels = catalogue.channels; const auto &pixelKeys = catalogue.pixelKeys;
    QJsonArray dmxSlots;
    for (const auto &entry : entries) {
        if (dmxSlots.size() >= 512) { issue = "Mode exceeds one DMX universe"; return false; }
        if (!entry.isObject()) { dmxSlots << entry; continue; }
        const auto insert = entry.toObject(); const auto repeat = insert["repeatFor"]; QStringList keys;
        const auto order = insert["channelOrder"].toString(); const auto pattern = insert["templateChannels"].toArray();
        if (insert["insert"] != "matrixChannels" || (order != "perPixel" && order != "perChannel") || pattern.isEmpty()) { issue = "Invalid matrix channel insert"; return false; }
        if (repeat.isArray()) {
            QSet<QString> seen;
            for (const auto &key : repeat.toArray()) {
                if (!key.isString() || !pixelKeys.contains(key.toString()) || seen.contains(key.toString())) { issue = "Unknown or repeated pixel; pixel-group controls are not supported"; return false; }
                keys << key.toString(); seen.insert(key.toString());
            }
        } else {
            auto ordered = matrixPixels; const auto type = repeat.toString();
            if (type == "eachPixelABC") {
                QCollator collator(QLocale(QLocale::English)); collator.setNumericMode(true);
                std::sort(ordered.begin(), ordered.end(), [&](const Pixel &a, const Pixel &b) { return collator.compare(a.key, b.key) < 0; });
            } else if (QRegularExpression("^eachPixel(XYZ|XZY|YXZ|YZX|ZXY|ZYX)$").match(type).hasMatch()) {
                const QString axes = type.right(3);
                std::sort(ordered.begin(), ordered.end(), [&](const Pixel &a, const Pixel &b) {
                    for (int i = 2; i >= 0; --i) { const int axis = QString("XYZ").indexOf(axes[i]); if (a.position[axis] != b.position[axis]) return a.position[axis] < b.position[axis]; }
                    return false;
                });
            } else { issue = "Unsupported matrix repeat order or pixel group"; return false; }
            for (const auto &pixel : ordered) keys << pixel.key;
        }
        if (keys.isEmpty() || qint64(keys.size()) * pattern.size() + dmxSlots.size() > 512) { issue = "Matrix mode is empty or exceeds one universe"; return false; }
        auto append = [&](const QJsonValue &key, const QString &pixel) { dmxSlots << (key.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(key.toString().replace("$pixelKey", pixel))); };
        if (order == "perPixel") for (const auto &key : keys) for (const auto &channel : pattern) append(channel, key);
        else for (const auto &channel : pattern) for (const auto &key : keys) append(channel, key);
    }
    mode.footprint = dmxSlots.size(); QMap<int, QMap<int, int>> offsets; QList<int> orderedChannels;
    for (int i = 0; i < dmxSlots.size(); ++i) {
        if (dmxSlots[i].isNull()) continue;
        const QString key = dmxSlots[i].toString();
        if (!dmxSlots[i].isString() || !lookup.contains(key)) { issue = "Unknown channel key or switching alias: " + key; return false; }
        const auto channel = lookup[key];
        if (offsets[channel.first].contains(channel.second)) { issue = "Repeated channel key: " + key; return false; }
        if (!orderedChannels.contains(channel.first)) orderedChannels << channel.first;
        offsets[channel.first][channel.second] = i + 1;
    }
    for (int index : orderedChannels) {
        const auto bytes = offsets[index]; QList<int> addresses;
        for (int byte = 0; byte < bytes.size(); ++byte) {
            if (!bytes.contains(byte)) { issue = "Fine alias without all more significant bytes: " + channels[index].keys.first(); return false; }
            addresses << bytes[byte];
        }
        FixtureParameter parameter;
        if (!channelParameter(channels[index], addresses, parameter, mode.warnings, issue)) return false;
        mode.parameters << parameter; mode.attributes << parameter.attribute;
    }
    // An all-null mode still reserves its footprint, with a harmless control.
    if (mode.parameters.isEmpty() && mode.footprint > 0) { mode.parameters << FixtureParameter{"NoFeature", {1}, 0}; mode.attributes << "NoFeature"; }
    ensureCellControls(mode); mode.warnings.removeDuplicates();
    return validateMode(mode, issue);
}
}

bool Flame::readOfl(const QByteArray &bytes, FixtureProfile &profile, QString &error, QString manufacturer) {
    auto fail = [&](QString s) { error = s; return false; };
    if (bytes.size() > MaxBytes) return fail("OFL JSON exceeds 16 MB");
    QJsonParseError parse; const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error || !document.isObject()) return fail("Invalid OFL JSON: " + parse.errorString());
    const auto json = document.object(); const auto schema = json["$schema"].toString();
    if (!QRegularExpression("^https://raw\\.githubusercontent\\.com/OpenLightingProject/open-fixture-library/(master|schema-12\\.[0-9]+\\.[0-9]+)/schemas/fixture\\.json$").match(schema).hasMatch())
        return fail("Expected an OFL fixture schema (12.x or current master)");
    FixtureProfile result; result.name = json["name"].toString();
    result.manufacturer = manufacturer.trimmed().isEmpty() ? json["manufacturerKey"].toString() : manufacturer.trimmed();
    if (result.manufacturer.isEmpty()) {
        const auto comment = json["comment"].toString();
        const auto match = QRegularExpression("^Manufacturer: (.+)\\. Exported by Cold Flame;").match(comment);
        if (match.hasMatch()) result.manufacturer = match.captured(1);
    }
    if (result.manufacturer.isEmpty()) result.manufacturer = "Unknown manufacturer";
    const auto modes = json["modes"].toArray();
    if (!textValid(result.name, 512) || !textValid(result.manufacturer, 512) || modes.isEmpty() || modes.size() > 256)
        return fail("OFL fixture needs a name and 1–256 modes");
    Catalogue catalogue; const bool compiled = compileCatalogue(json, catalogue);
    QSet<QString> names;
    for (const auto &entry : modes) {
        const auto object = entry.toObject(); FixtureMode mode; mode.name = object["name"].toString();
        if (!textValid(mode.name) || names.contains(mode.name)) return fail("Missing or duplicate OFL mode name");
        names.insert(mode.name);
        const auto entries = object["channels"].toArray(); QString issue;
        if (entries.isEmpty() || entries.size() > 512) mode.unsupported = "Mode needs 1–512 channel entries";
        else if (!compiled) mode.unsupported = catalogue.issue.isEmpty() ? "Invalid OFL channel catalogue" : catalogue.issue;
        else if (!expandMode(catalogue, entries, mode, issue)) mode.unsupported = issue.isEmpty() ? "Invalid OFL channel definition" : issue;
        if (!mode.unsupported.isEmpty()) { mode.parameters.clear(); mode.attributes.clear(); }
        result.modes << mode;
    }
    result.oflSource = bytes; profile = result; error.clear(); return true;
}

QByteArray Flame::writeOfl(const FixtureProfile &profile, QString &error) {
    error.clear();
    if (!profile.oflSource.isEmpty()) return profile.oflSource;
    auto fail = [&](QString s) { error = s; return QByteArray(); };
    if (!textValid(profile.name, 512) || !textValid(profile.manufacturer, 512) || profile.modes.isEmpty()) return fail("Missing fixture name, manufacturer or modes");
    QJsonObject available, templates; QJsonArray modes; QStringList cells; QSet<QString> modeNames;
    const QMap<QString, QString> colors{{"R", "Red"}, {"G", "Green"}, {"B", "Blue"}, {"W", "White"}, {"WW", "Warm White"}, {"CW", "Cold White"}, {"A", "Amber"}, {"UV", "UV"}, {"L", "Lime"}, {"I", "Indigo"}, {"C", "Cyan"}, {"M", "Magenta"}, {"Y", "Yellow"}};
    for (int m = 0; m < profile.modes.size(); ++m) {
        const auto &mode = profile.modes[m]; QString issue;
        if (!validateMode(mode, issue)) return fail(mode.name + ": " + issue);
        if (mode.name.contains("mode", Qt::CaseInsensitive) || modeNames.contains(mode.name)) return fail("OFL mode names must be unique and cannot contain the word 'mode'. Rename the mode in a fixture copy.");
        modeNames.insert(mode.name); QJsonArray dmxSlots;
        for (int i = 0; i < mode.footprint; ++i) dmxSlots << QJsonValue(QJsonValue::Null);
        for (int p = 0; p < mode.parameters.size(); ++p) {
            const auto &parameter = mode.parameters[p];
            if (parameter.offsets.isEmpty()) {
                if (!parameter.intensity || parameter.snap || parameter.home != (parameter.cell.isEmpty() ? 0 : 100)) return fail("OFL cannot represent this virtual parameter default or Snap setting");
                continue;
            }
            if (parameter.offsets.size() > 3) return fail("OFL export supports at most 24-bit channels");
            if (parameter.intensity != (parameter.attribute == "Dimmer")) return fail("Intensity must use the Dimmer attribute");
            if (parameter.cell.contains('/')) return fail("OFL export of hierarchical cell groups is not supported; use GDTF or the native show format");
            QJsonObject capability; const QString attribute = parameter.attribute;
            if (attribute == "Dimmer") capability["type"] = "Intensity";
            else if (attribute.startsWith("ColorAdd_") && colors.contains(attribute.mid(9))) { capability["type"] = "ColorIntensity"; capability["color"] = colors.value(attribute.mid(9)); }
            else {
                const QMap<QString, QString> types{{"Pan", "Pan"}, {"Tilt", "Tilt"}, {"Zoom", "Zoom"}, {"Focus1", "Focus"}, {"Iris", "Iris"}, {"Frost1", "Frost"}, {"CTO", "ColorTemperature"}, {"NoFeature", "NoFunction"}};
                capability["type"] = types.value(attribute, "Generic");
                if (capability["type"] == "Generic") capability["comment"] = attribute;
                if (attribute == "Pan" || attribute == "Tilt" || attribute == "Zoom") capability["angleStart"] = "0%", capability["angleEnd"] = "100%";
                if (attribute == "Focus1") capability["distanceStart"] = "0%", capability["distanceEnd"] = "100%";
                if (attribute == "CTO") capability["colorTemperatureStart"] = "0%", capability["colorTemperatureEnd"] = "100%";
                if (attribute == "Iris") capability["openPercentStart"] = "0%", capability["openPercentEnd"] = "100%";
                if (attribute == "Frost1") capability["frostIntensityStart"] = "0%", capability["frostIntensityEnd"] = "100%";
            }
            if (attribute == "NoFeature" && parameter.offsets.size() > 1) return fail("OFL NoFunction channels cannot have fine aliases");
            // These attributes have fixed fade behaviour in the OFL adapter. Refuse lossy conversion.
            const bool importedSnap = capability["type"] == "Generic";
            if (parameter.snap != importedSnap) return fail("OFL cannot preserve the Snap setting for " + attribute + "; use GDTF or the native show format");
            const QString base = QString("M%1 P%2 %3").arg(m + 1).arg(p + 1).arg(attribute);
            if (base.contains('$') || !textValid(base)) return fail("Attribute is too long or contains OFL template variables");
            if (attribute.startsWith("ColorSub_")) return fail("OFL cannot preserve subtractive-colour semantics in this export; use GDTF or the native show format");
            QString key = base; QJsonObject channel{{"name", attribute}, {"defaultValue", double(qRound64(parameter.home * double(maximum(parameter.offsets.size())) / 100.))}, {"capability", capability}};
            QJsonArray aliases;
            if (!parameter.cell.isEmpty()) {
                if (cellPath(QUrl::fromPercentEncoding(parameter.cell.toLatin1())) != parameter.cell) return fail("Cell names must be OFL-compatible encoded keys; use GDTF or rename cells in a fixture copy");
                if (!cells.contains(parameter.cell)) cells << parameter.cell;
                key += " $pixelKey";
            }
            for (int byte = 1; byte < parameter.offsets.size(); ++byte) aliases << key + QString(" fine %1").arg(byte);
            if (!aliases.isEmpty()) channel["fineChannelAliases"] = aliases;
            if (parameter.cell.isEmpty()) available[key] = channel; else templates[key] = channel;
            for (int byte = 0; byte < parameter.offsets.size(); ++byte) {
                QString resolved = byte == 0 ? key : aliases[byte - 1].toString();
                if (!parameter.cell.isEmpty()) resolved.replace("$pixelKey", QUrl::fromPercentEncoding(parameter.cell.toLatin1()));
                dmxSlots[parameter.offsets[byte] - 1] = resolved;
            }
        }
        modes << QJsonObject{{"name", mode.name}, {"channels", dmxSlots}};
    }
    const QString date = QDate::currentDate().toString(Qt::ISODate);
    QJsonObject json{{"$schema", Schema}, {"name", profile.name}, {"categories", QJsonArray{"Other"}},
        {"comment", "Manufacturer: " + profile.manufacturer + ". Exported by Cold Flame; verify against the fixture manual before use."},
        {"meta", QJsonObject{{"authors", QJsonArray{"Cold Flame"}}, {"createDate", date}, {"lastModifyDate", date}}}, {"modes", modes}};
    if (!available.isEmpty()) json["availableChannels"] = available;
    if (!templates.isEmpty()) {
        QJsonArray keys; for (const auto &cell : cells) keys << QUrl::fromPercentEncoding(cell.toLatin1());
        json["templateChannels"] = templates; json["matrix"] = QJsonObject{{"pixelKeys", QJsonArray{QJsonArray{keys}}}};
    }
    if (available.isEmpty() && templates.isEmpty()) return fail("OFL export needs at least one physical channel");
    return QJsonDocument(json).toJson();
}
