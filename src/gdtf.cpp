#include "gdtf.h"
#include <zlib.h>
namespace Flame {
static quint16 u16(const QByteArray &b, int p) {
    return quint8(b[p]) | (quint16(quint8(b[p + 1])) << 8);
}
static quint32 u32(const QByteArray &b, int p) {
    return u16(b, p) | (quint32(u16(b, p + 2)) << 16);
}
bool readGdtf(const QByteArray &b, FixtureProfile &result, QString &error) {
    auto fail = [&](QString s) {
        error = s;
        return false;
    };
    if (b.size() > 64 * 1024 * 1024)
        return fail("Archive is too large");
    int end = -1;
    for (int p = b.size() - 22; p >= qMax(0, int(b.size()) - 65557); --p)
        if (b.mid(p, 4) == QByteArray("PK\5\6", 4)) {
            end = p;
            break;
        }
    if (end < 0)
        return fail("Invalid ZIP archive");
    quint32 pos = u32(b, end + 16);
    QByteArray xml;
    for (int i = 0; i < u16(b, end + 10); ++i) {
        if (quint64(pos) + 46 > quint64(b.size()) || b.mid(pos, 4) != QByteArray("PK\1\2", 4))
            return fail("Invalid ZIP directory");
        int nameLen = u16(b, pos + 28), extra = u16(b, pos + 30), comment = u16(b, pos + 32);
        quint32 compressed = u32(b, pos + 20), size = u32(b, pos + 24), local = u32(b, pos + 42);
        int method = u16(b, pos + 10);
        if (quint64(pos) + 46 + nameLen + extra + comment > quint64(b.size()))
            return fail("Truncated ZIP entry");
        if (b.mid(pos + 46, nameLen) == "description.xml") {
            if (u16(b, pos + 8) & 1)
                return fail("Encrypted archives are not supported");
            if (size > 16 * 1024 * 1024 || quint64(local) + 30 > quint64(b.size()) ||
                b.mid(local, 4) != QByteArray("PK\3\4", 4))
                return fail("Invalid description");
            quint64 start = quint64(local) + 30 + u16(b, local + 26) + u16(b, local + 28);
            if (start + compressed > quint64(b.size()))
                return fail("Truncated description");
            if (method == 0) {
                if (compressed != size)
                    return fail("Invalid ZIP size");
                xml = b.mid(start, size);
            } else if (method == 8) {
                xml.resize(size);
                z_stream z{};
                z.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(b.constData() + start));
                z.avail_in = compressed;
                z.next_out = reinterpret_cast<Bytef *>(xml.data());
                z.avail_out = size;
                if (inflateInit2(&z, -MAX_WBITS) != Z_OK)
                    return fail("ZIP initialization failed");
                int rc = inflate(&z, Z_FINISH);
                bool valid = rc == Z_STREAM_END && z.total_out == size;
                inflateEnd(&z);
                if (!valid)
                    return fail("Decompression failed");
            } else
                return fail("Unsupported ZIP compression");
            if (crc32(0, reinterpret_cast<const Bytef *>(xml.constData()), xml.size()) !=
                u32(b, pos + 16))
                return fail("Description CRC mismatch");
            break;
        }
        pos += 46 + nameLen + extra + comment;
    }
    if (xml.isEmpty())
        return fail("description.xml not found");
    FixtureProfile profile;
    if (!parseGdtfDescription(xml, profile, error)) return false;
    profile.source = b;
    result = profile;
    return true;
}

bool validateMode(const FixtureMode &m, QString &error) {
    auto fail = [&](QString text) { error = text; return false; };
    if (!m.unsupported.isEmpty()) return fail(m.unsupported);
    if (m.name.isEmpty() || m.name.size() > 256 || m.footprint < 1 || m.footprint > 512 || m.parameters.isEmpty() || m.parameters.size() > 512)
        return fail("A mode needs a name and 1–512 DMX slots");
    QSet<int> used; QSet<QString> intensities;
    for (const auto &p : m.parameters) {
        if (p.cell == "*" || p.cell.size() > 2048) return fail("Invalid or oversized cell path");
        if (p.attribute.size() > 256) return fail("Parameter attribute exceeds 256 characters");
        if (p.attribute.isEmpty() || (p.offsets.isEmpty() && !p.intensity) || p.offsets.size() > 4 || !qIsFinite(p.home) || p.home < 0 || p.home > 100)
            return fail("Invalid parameter, resolution or home value");
        if (p.intensity) {
            if (intensities.contains(p.cell)) return fail("Only one intensity control is allowed per master or cell");
            intensities.insert(p.cell);
        }
        for (int offset : p.offsets) {
            if (offset < 1 || offset > m.footprint || used.contains(offset)) return fail("Overlapping or invalid offsets inside fixture mode");
            used.insert(offset);
        }
    }
    return true;
}
QMap<QString, FixtureProfile> genericFixtures() {
    QMap<QString, FixtureProfile> profiles;
    auto add = [&](QString id, QString name, QList<FixtureParameter> parameters) {
        FixtureMode mode; mode.name = "Standard"; mode.parameters = parameters;
        for (const auto &p : parameters) { mode.attributes << p.attribute; for (int o : p.offsets) mode.footprint = qMax(mode.footprint, o); }
        profiles[id] = {"Generic", name, {mode}, {}};
    };
    add("dimmer8", "Dimmer 8-bit", {{"Dimmer", {1}, 0, true}});
    add("dimmer16", "Dimmer 16-bit", {{"Dimmer", {1, 2}, 0, true}});
    QList<FixtureParameter> rgb{{"ColorAdd_R", {1}, 100}, {"ColorAdd_G", {2}, 100}, {"ColorAdd_B", {3}, 100}};
    add("rgb", "RGB · virtual dimmer", rgb);
    rgb << FixtureParameter{"ColorAdd_W", {4}, 0}; add("rgbw", "RGBW · virtual dimmer", rgb);
    add("rgbd", "Dimmer + RGB", {{"Dimmer", {1}, 0, true}, {"ColorAdd_R", {2}, 100}, {"ColorAdd_G", {3}, 100}, {"ColorAdd_B", {4}, 100}});
    add("moving-rgbw", "Moving head · RGBW 11ch", {{"Pan", {1, 2}, 50}, {"Tilt", {3, 4}, 50}, {"Dimmer", {5}, 0, true},
        {"ColorAdd_R", {6}, 100}, {"ColorAdd_G", {7}, 100}, {"ColorAdd_B", {8}, 100}, {"ColorAdd_W", {9}, 0},
        {"Shutter1", {10}, 100, false, true}, {"Gobo1", {11}, 0, false, true}});
    return profiles;
}
QJsonObject fixtureJson(const FixtureProfile &p) {
    QJsonArray modes;
    for (const auto &m : p.modes) {
        QJsonArray parameters;
        for (const auto &v : m.parameters) { QJsonArray offsets; for (int o : v.offsets) offsets << o;
            parameters << QJsonObject{{"attribute", v.attribute}, {"offsets", offsets}, {"home", v.home}, {"intensity", v.intensity}, {"snap", v.snap}, {"cell", v.cell}}; }
        modes << QJsonObject{{"name", m.name}, {"footprint", m.footprint}, {"parameters", parameters}, {"unsupported", m.unsupported}, {"warnings", QJsonArray::fromStringList(m.warnings)}};
    }
    return {{"manufacturer", p.manufacturer}, {"name", p.name}, {"modes", modes}, {"gdtf", QString::fromLatin1(p.source.toBase64())}, {"ofl", QString::fromLatin1(p.oflSource.toBase64())}};
}
bool fixtureFromJson(const QJsonObject &j, FixtureProfile &p, QString &error) {
    if (!j["gdtf"].toString().isEmpty() && !j["ofl"].toString().isEmpty()) { error = "A fixture cannot have both GDTF and OFL sources"; return false; }
    if (!j["gdtf"].toString().isEmpty()) return readGdtf(QByteArray::fromBase64(j["gdtf"].toString().toLatin1()), p, error);
    if (!j["ofl"].toString().isEmpty()) return readOfl(QByteArray::fromBase64(j["ofl"].toString().toLatin1()), p, error, j["manufacturer"].toString());
    FixtureProfile result; result.manufacturer = j["manufacturer"].toString(); result.name = j["name"].toString();
    for (auto entry : j["modes"].toArray()) {
        auto o = entry.toObject(); FixtureMode m; m.name = o["name"].toString(); m.footprint = o["footprint"].toInt(); m.unsupported = o["unsupported"].toString();
        for (const auto &warning : o["warnings"].toArray()) m.warnings << warning.toString();
        for (auto value : o["parameters"].toArray()) {
            auto v = value.toObject(); FixtureParameter param;
            param.attribute = v["attribute"].toString(); param.home = v["home"].toDouble(-1); param.intensity = v["intensity"].toBool(); param.snap = v["snap"].toBool(); param.cell = v["cell"].toString();
            for (auto offset : v["offsets"].toArray()) param.offsets << offset.toInt();
            m.parameters << param; m.attributes << param.attribute;
        }
        ensureCellControls(m);
        if (!validateMode(m, error)) return false;
        result.modes << m;
    }
    if (result.name.isEmpty() || result.modes.isEmpty()) { error = "Missing fixture name or modes"; return false; }
    p = result; return true;
}
static void put16(QByteArray &b, quint16 n) { b.append(char(n)); b.append(char(n >> 8)); }
static void put32(QByteArray &b, quint32 n) { put16(b, n & 65535); put16(b, n >> 16); }
QByteArray writeGdtf(const FixtureProfile &p) {
    if (!p.source.isEmpty()) return p.source;
    if (p.name.trimmed().isEmpty() || p.manufacturer.trimmed().isEmpty() || p.modes.isEmpty()) return {};
    for (const auto &mode : p.modes) {
        QString issue; if (!validateMode(mode, issue)) return {};
        int last = 0;
        for (const auto &parameter : mode.parameters) {
            if (parameter.intensity != (parameter.attribute == "Dimmer")) return {};
            for (int offset : parameter.offsets) last = qMax(last, offset);
        }
        // GDTF derives its footprint from occupied offsets; it has no independent
        // trailing reservation. Refuse an export that would silently shrink it.
        if (last != mode.footprint) return {};
    }
    QByteArray xml; QXmlStreamWriter w(&xml); w.setAutoFormatting(true); w.writeStartDocument();
    w.writeStartElement("GDTF"); w.writeAttribute("DataVersion", "1.2"); w.writeStartElement("FixtureType");
    w.writeAttribute("Name", p.name); w.writeAttribute("ShortName", p.name); w.writeAttribute("LongName", p.name);
    w.writeAttribute("Manufacturer", p.manufacturer); w.writeAttribute("Description", "Generic DMX profile");
    w.writeAttribute("FixtureTypeID", QUuid::createUuid().toString(QUuid::WithoutBraces));
    w.writeStartElement("AttributeDefinitions"); w.writeStartElement("FeatureGroups"); w.writeStartElement("FeatureGroup"); w.writeAttribute("Name", "Control"); w.writeAttribute("Pretty", "Control");
    w.writeEmptyElement("Feature"); w.writeAttribute("Name", "Control"); w.writeEndElement(); w.writeEndElement(); w.writeStartElement("Attributes");
    QSet<QString> attrs;
    for (const auto &m : p.modes) for (const auto &v : m.parameters) attrs.insert(v.attribute);
    auto ordered = attrs.values(); ordered.sort();
    for (const auto &attr : ordered) { w.writeEmptyElement("Attribute"); w.writeAttribute("Name", attr); w.writeAttribute("Pretty", attr); w.writeAttribute("Feature", "Control.Control"); w.writeAttribute("PhysicalUnit", "None"); }
    w.writeEndElement(); w.writeEndElement();
    QStringList cells;
    for (const auto &mode : p.modes) for (const auto &parameter : mode.parameters)
        if (!parameter.cell.isEmpty() && !cells.contains(parameter.cell)) cells << parameter.cell;
    auto geometryName = [&](const QString &cell) { return cell.isEmpty() ? QString("Body") : QString("CF_Cell_%1").arg(cells.indexOf(cell)); };
    w.writeStartElement("Geometries"); w.writeStartElement("Geometry"); w.writeAttribute("Name", "Body");
    w.writeAttribute("Position", "{1,0,0,0}{0,1,0,0}{0,0,1,0}{0,0,0,1}");
    for (const auto &cell : cells) {
        w.writeStartElement("GeometryReference"); w.writeAttribute("Name", cell); w.writeAttribute("Geometry", geometryName(cell));
        w.writeEmptyElement("Break"); w.writeAttribute("DMXBreak", "1"); w.writeAttribute("DMXOffset", "1"); w.writeEndElement();
    }
    w.writeEndElement();
    for (const auto &cell : cells) { w.writeEmptyElement("Geometry"); w.writeAttribute("Name", geometryName(cell)); }
    w.writeEndElement();
    w.writeStartElement("DMXModes");
    for (const auto &m : p.modes) {
        w.writeStartElement("DMXMode"); w.writeAttribute("Name", m.name); w.writeAttribute("Geometry", "Body"); w.writeStartElement("DMXChannels");
        for (const auto &v : m.parameters) {
            w.writeStartElement("DMXChannel"); w.writeAttribute("DMXBreak", "1"); w.writeAttribute("Geometry", geometryName(v.cell));
            QStringList offsets; for (int o : v.offsets) offsets << QString::number(o); w.writeAttribute("Offset", offsets.join(','));
            const int width = v.offsets.isEmpty() ? 2 : v.offsets.size();
            quint64 max = (quint64(1) << (8 * width)) - 1;
            const QString home = QString::number(quint64(qRound64(v.home * double(max) / 100.))) + "/" + QString::number(width);
            w.writeStartElement("LogicalChannel"); w.writeAttribute("Attribute", v.attribute); w.writeAttribute("Snap", v.snap ? "Yes" : "No");
            w.writeStartElement("ChannelFunction"); w.writeAttribute("Name", v.attribute); w.writeAttribute("Attribute", v.attribute); w.writeAttribute("DMXFrom", "0/1"); w.writeAttribute("Default", home);
            w.writeEndElement(); w.writeEndElement(); w.writeEndElement();
        }
        w.writeEndElement();
        w.writeStartElement("Relations");
        for (const auto &master : m.parameters) if (master.intensity && master.offsets.isEmpty())
            for (const auto &target : m.parameters) if (target.cell == master.cell && target.attribute.startsWith("ColorAdd_")) {
                const auto geometry = geometryName(master.cell);
                w.writeEmptyElement("Relation"); w.writeAttribute("Name", geometry + "_" + target.attribute);
                w.writeAttribute("Type", "Multiply"); w.writeAttribute("Master", geometry + "_Dimmer");
                w.writeAttribute("Follower", geometry + "_" + target.attribute + "." + target.attribute + "." + target.attribute);
            }
        w.writeEndElement(); w.writeEndElement();
    }
    w.writeEndElement(); w.writeEndElement(); w.writeEndElement(); w.writeEndDocument();
    const QByteArray name("description.xml"); quint32 crc = crc32(0, reinterpret_cast<const Bytef *>(xml.constData()), xml.size());
    QByteArray b; put32(b, 0x04034b50); put16(b, 20); put16(b, 0); put16(b, 0); put32(b, 0); put32(b, crc); put32(b, xml.size()); put32(b, xml.size()); put16(b, name.size()); put16(b, 0); b += name; b += xml;
    quint32 central = b.size(); put32(b, 0x02014b50); put16(b, 20); put16(b, 20); put16(b, 0); put16(b, 0); put32(b, 0); put32(b, crc); put32(b, xml.size()); put32(b, xml.size()); put16(b, name.size()); put16(b, 0); put16(b, 0); put16(b, 0); put16(b, 0); put32(b, 0); put32(b, 0); b += name;
    quint32 size = b.size() - central; put32(b, 0x06054b50); put16(b, 0); put16(b, 0); put16(b, 1); put16(b, 1); put32(b, size); put32(b, central); put16(b, 0); return b;
}
} // namespace Flame
