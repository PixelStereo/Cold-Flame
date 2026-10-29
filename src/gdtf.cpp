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
    QXmlStreamReader r(xml);
    FixtureProfile profile;
    FixtureMode mode;
    bool inMode = false;
    FixtureParameter parameter;
    QString defaultValue, initialFunction;
    bool inChannel = false;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isDTD())
            return fail("DTD is not allowed");
        if (r.isStartElement()) {
            auto a = r.attributes();
            if (r.name() == QStringLiteral("FixtureType")) {
                profile.name = a.value("Name").toString();
                profile.manufacturer = a.value("Manufacturer").toString();
            } else if (r.name() == QStringLiteral("DMXMode")) {
                mode = {};
                mode.name = a.value("Name").toString();
                inMode = true;
            } else if (inMode && r.name() == QStringLiteral("DMXChannel")) {
                parameter = {}; inChannel = true;
                defaultValue = a.value("Default").toString();
                initialFunction = a.value("InitialFunction").toString();
                QString dmxBreak = a.value("DMXBreak").toString();
                if (!dmxBreak.isEmpty() && dmxBreak != "1") mode.unsupported = "Multiple DMX breaks are not supported";
                for (auto v : a.value("Offset").toString().split(',')) {
                    bool ok;
                    int offset = v.toInt(&ok);
                    if (ok && offset > 0) {
                        parameter.offsets << offset;
                        mode.footprint = qMax(mode.footprint, offset);
                    } else mode.unsupported = "Virtual channels require a fixture relation evaluator";
                }
            } else if (inMode && r.name() == QStringLiteral("LogicalChannel")) {
                auto attr = a.value("Attribute").toString();
                if (!parameter.attribute.isEmpty()) mode.unsupported = "Multiplexed logical channels are not supported";
                parameter.attribute = attr;
                parameter.intensity = attr == "Dimmer";
                parameter.snap = a.value("Snap") == QStringLiteral("Yes") || a.value("Snap") == QStringLiteral("On");
                if (!attr.isEmpty() && !mode.attributes.contains(attr))
                    mode.attributes << attr;
            } else if (inMode && (r.name() == QStringLiteral("Relation") || r.name() == QStringLiteral("ModeMaster"))) {
                mode.unsupported = "Channel relations and mode masters are not supported";
            } else if (inChannel && r.name() == QStringLiteral("ChannelFunction")) {
                if (!a.value("ModeMaster").isEmpty()) mode.unsupported = "Mode-dependent channels are not supported";
                if (defaultValue.isEmpty() || (!initialFunction.isEmpty() && initialFunction.section('.', -1) == a.value("Name"))) defaultValue = a.value("Default").toString();
            } else if (r.name() == QStringLiteral("GeometryReference")) {
                // Expanded cells can shift offsets. Reject rather than address the wrong emitter.
                for (auto &m : profile.modes) m.unsupported = "Geometry references and multi-cell expansion are not supported";
                profile.source = "geometry-reference";
            }
        } else if (r.isEndElement() && r.name() == QStringLiteral("DMXChannel")) {
            inChannel = false;
            if (!defaultValue.isEmpty()) {
                const auto parts = defaultValue.split('/'); bool ok = false, widthOk = false;
                quint64 value = parts.value(0).toULongLong(&ok); int width = parts.value(1).toInt(&widthOk);
                if (!ok || !widthOk || width < 1 || width > 4 || value > ((quint64(1) << (8 * width)) - 1))
                    mode.unsupported = "Invalid DMX default";
                else parameter.home = double(value) * 100. / double((quint64(1) << (8 * width)) - 1);
            }
            mode.parameters << parameter;
        } else if (r.isEndElement() && r.name() == QStringLiteral("DMXMode")) {
            profile.modes << mode;
            inMode = false;
        }
    }
    if (r.hasError() || profile.name.isEmpty() || profile.modes.isEmpty())
        return fail("Invalid GDTF description or missing DMX mode");
    for (auto &m : profile.modes) {
        if (profile.source == "geometry-reference") m.unsupported = "Geometry references and multi-cell expansion are not supported";
        QString issue;
        if (!validateMode(m, issue) && m.unsupported.isEmpty()) m.unsupported = issue;
    }
    profile.source = b;
    result = profile;
    return true;
}

bool validateMode(const FixtureMode &m, QString &error) {
    auto fail = [&](QString text) { error = text; return false; };
    if (!m.unsupported.isEmpty()) return fail(m.unsupported);
    if (m.name.isEmpty() || m.footprint < 1 || m.footprint > 512 || m.parameters.isEmpty() || m.parameters.size() > 512)
        return fail("A mode needs a name and 1–512 DMX slots");
    QSet<int> used; int intensities = 0;
    for (const auto &p : m.parameters) {
        if (p.attribute.isEmpty() || p.offsets.isEmpty() || p.offsets.size() > 4 || !qIsFinite(p.home) || p.home < 0 || p.home > 100)
            return fail("Invalid parameter, resolution or home value");
        if (p.intensity && ++intensities > 1) return fail("Multi-cell intensity modes are not supported");
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
            parameters << QJsonObject{{"attribute", v.attribute}, {"offsets", offsets}, {"home", v.home}, {"intensity", v.intensity}, {"snap", v.snap}}; }
        modes << QJsonObject{{"name", m.name}, {"footprint", m.footprint}, {"parameters", parameters}, {"unsupported", m.unsupported}};
    }
    return {{"manufacturer", p.manufacturer}, {"name", p.name}, {"modes", modes}, {"gdtf", QString::fromLatin1(p.source.toBase64())}};
}
bool fixtureFromJson(const QJsonObject &j, FixtureProfile &p, QString &error) {
    if (!j["gdtf"].toString().isEmpty()) return readGdtf(QByteArray::fromBase64(j["gdtf"].toString().toLatin1()), p, error);
    FixtureProfile result; result.manufacturer = j["manufacturer"].toString(); result.name = j["name"].toString();
    for (auto entry : j["modes"].toArray()) {
        auto o = entry.toObject(); FixtureMode m; m.name = o["name"].toString(); m.footprint = o["footprint"].toInt(); m.unsupported = o["unsupported"].toString();
        for (auto value : o["parameters"].toArray()) {
            auto v = value.toObject(); FixtureParameter param;
            param.attribute = v["attribute"].toString(); param.home = v["home"].toDouble(-1); param.intensity = v["intensity"].toBool(); param.snap = v["snap"].toBool();
            for (auto offset : v["offsets"].toArray()) param.offsets << offset.toInt();
            m.parameters << param; m.attributes << param.attribute;
        }
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
    w.writeStartElement("Geometries"); w.writeEmptyElement("Geometry"); w.writeAttribute("Name", "Body");
    w.writeAttribute("Position", "{1,0,0,0}{0,1,0,0}{0,0,1,0}{0,0,0,1}"); w.writeEndElement();
    w.writeStartElement("DMXModes");
    for (const auto &m : p.modes) {
        w.writeStartElement("DMXMode"); w.writeAttribute("Name", m.name); w.writeAttribute("Geometry", "Body"); w.writeStartElement("DMXChannels");
        for (const auto &v : m.parameters) {
            w.writeStartElement("DMXChannel"); w.writeAttribute("DMXBreak", "1"); w.writeAttribute("Geometry", "Body");
            QStringList offsets; for (int o : v.offsets) offsets << QString::number(o); w.writeAttribute("Offset", offsets.join(','));
            quint64 max = (quint64(1) << (8 * v.offsets.size())) - 1;
            const QString home = QString::number(quint64(qRound64(v.home * double(max) / 100.))) + "/" + QString::number(v.offsets.size());
            w.writeStartElement("LogicalChannel"); w.writeAttribute("Attribute", v.attribute); w.writeAttribute("Snap", v.snap ? "Yes" : "No");
            w.writeStartElement("ChannelFunction"); w.writeAttribute("Name", v.attribute); w.writeAttribute("Attribute", v.attribute); w.writeAttribute("DMXFrom", "0/1"); w.writeAttribute("Default", home);
            w.writeEndElement(); w.writeEndElement(); w.writeEndElement();
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
