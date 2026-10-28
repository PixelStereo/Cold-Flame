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
                for (auto v : a.value("Offset").toString().split(',')) {
                    bool ok;
                    int offset = v.toInt(&ok);
                    if (ok)
                        mode.footprint = qMax(mode.footprint, offset);
                }
            } else if (inMode && r.name() == QStringLiteral("LogicalChannel")) {
                auto attr = a.value("Attribute").toString();
                if (!attr.isEmpty() && !mode.attributes.contains(attr))
                    mode.attributes << attr;
            }
        } else if (r.isEndElement() && r.name() == QStringLiteral("DMXMode")) {
            profile.modes << mode;
            inMode = false;
        }
    }
    if (r.hasError() || profile.name.isEmpty() || profile.modes.isEmpty())
        return fail("Invalid GDTF description or missing DMX mode");
    result = profile;
    return true;
}
} // namespace Flame
