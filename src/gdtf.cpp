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
        return fail("Archive trop volumineuse");
    int end = -1;
    for (int p = b.size() - 22; p >= qMax(0, int(b.size()) - 65557); --p)
        if (b.mid(p, 4) == QByteArray("PK\5\6", 4)) {
            end = p;
            break;
        }
    if (end < 0)
        return fail("Archive ZIP invalide");
    quint32 pos = u32(b, end + 16);
    QByteArray xml;
    for (int i = 0; i < u16(b, end + 10); ++i) {
        if (quint64(pos) + 46 > quint64(b.size()) || b.mid(pos, 4) != QByteArray("PK\1\2", 4))
            return fail("Répertoire ZIP invalide");
        int nameLen = u16(b, pos + 28), extra = u16(b, pos + 30), comment = u16(b, pos + 32);
        quint32 compressed = u32(b, pos + 20), size = u32(b, pos + 24), local = u32(b, pos + 42);
        int method = u16(b, pos + 10);
        if (quint64(pos) + 46 + nameLen + extra + comment > quint64(b.size()))
            return fail("Entrée ZIP tronquée");
        if (b.mid(pos + 46, nameLen) == "description.xml") {
            if (u16(b, pos + 8) & 1)
                return fail("Archive chiffrée non prise en charge");
            if (size > 16 * 1024 * 1024 || quint64(local) + 30 > quint64(b.size()) ||
                b.mid(local, 4) != QByteArray("PK\3\4", 4))
                return fail("Description invalide");
            quint64 start = quint64(local) + 30 + u16(b, local + 26) + u16(b, local + 28);
            if (start + compressed > quint64(b.size()))
                return fail("Description tronquée");
            if (method == 0) {
                if (compressed != size)
                    return fail("Taille ZIP invalide");
                xml = b.mid(start, size);
            } else if (method == 8) {
                xml.resize(size);
                z_stream z{};
                z.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(b.constData() + start));
                z.avail_in = compressed;
                z.next_out = reinterpret_cast<Bytef *>(xml.data());
                z.avail_out = size;
                if (inflateInit2(&z, -MAX_WBITS) != Z_OK)
                    return fail("Initialisation ZIP impossible");
                int rc = inflate(&z, Z_FINISH);
                bool valid = rc == Z_STREAM_END && z.total_out == size;
                inflateEnd(&z);
                if (!valid)
                    return fail("Décompression impossible");
            } else
                return fail("Compression ZIP non prise en charge");
            if (crc32(0, reinterpret_cast<const Bytef *>(xml.constData()), xml.size()) !=
                u32(b, pos + 16))
                return fail("CRC description incorrect");
            break;
        }
        pos += 46 + nameLen + extra + comment;
    }
    if (xml.isEmpty())
        return fail("description.xml absent");
    QXmlStreamReader r(xml);
    FixtureProfile profile;
    FixtureMode mode;
    bool inMode = false;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isDTD())
            return fail("DTD interdite");
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
        return fail("Description GDTF invalide ou sans mode DMX");
    result = profile;
    return true;
}
} // namespace Flame
