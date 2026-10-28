#include "network.h"
#include <cmath>
#include <cstring>
namespace Flame {
static void be16(QByteArray &b, int o, quint16 n) {
    b[o] = char(n >> 8);
    b[o + 1] = char(n);
}
static void be32(QByteArray &b, int o, quint32 n) {
    for (int i = 0; i < 4; ++i)
        b[o + i] = char(n >> (24 - i * 8));
}
QByteArray artnet(const QByteArray &dmx, quint16 universe, quint8 sequence) {
    if (dmx.isEmpty() || dmx.size() > 512 || universe > 32767)
        return {};
    QByteArray data = dmx;
    if (data.size() % 2)
        data.append('\0');
    QByteArray b(18, '\0');
    b.replace(0, 8, QByteArray("Art-Net\0", 8));
    b[9] = 0x50;
    b[11] = 14;
    b[12] = char(sequence);
    b[14] = char(universe);
    b[15] = char(universe >> 8);
    be16(b, 16, data.size());
    return b + data;
}
QByteArray sacn(const QByteArray &dmx, quint16 universe, quint8 sequence, const QUuid &cid) {
    if (dmx.isEmpty() || dmx.size() > 512 || universe < 1 || universe > 63999)
        return {};
    QByteArray b(126, '\0');
    be16(b, 0, 16);
    b.replace(4, 12, QByteArray("ASC-E1.17\0\0\0", 12));
    be16(b, 16, 0x7000 | (110 + dmx.size()));
    be32(b, 18, 4);
    b.replace(22, 16, cid.toRfc4122());
    be16(b, 38, 0x7000 | (88 + dmx.size()));
    be32(b, 40, 2);
    QByteArray name = "Cold Flame";
    b.replace(44, name.size(), name);
    b[108] = 100;
    b[111] = char(sequence);
    be16(b, 113, universe);
    be16(b, 115, 0x7000 | (11 + dmx.size()));
    b[117] = 2;
    b[118] = char(0xa1);
    be16(b, 121, 1);
    be16(b, 123, dmx.size() + 1);
    return b + dmx;
}
static QByteArray oscString(QString s) {
    auto b = s.toUtf8();
    b.append('\0');
    while (b.size() % 4)
        b.append('\0');
    return b;
}
QByteArray oscMessage(QString address, float value) {
    QByteArray b = oscString(address) + oscString(",f");
    quint32 bits;
    memcpy(&bits, &value, 4);
    int o = b.size();
    b.resize(o + 4);
    be32(b, o, bits);
    return b;
}
bool oscDecode(const QByteArray &p, QString &a, float &v) {
    int end = p.indexOf('\0');
    if (end < 1 || !p.startsWith('/'))
        return false;
    int o = (end + 4) & ~3;
    int te = p.indexOf('\0', o);
    if (te < 0 || p.mid(o, te - o) != ",f")
        return false;
    int f = (te + 4) & ~3;
    if (p.size() != f + 4)
        return false;
    quint32 bits = 0;
    for (int i = 0; i < 4; ++i)
        bits = (bits << 8) | quint8(p[f + i]);
    memcpy(&v, &bits, 4);
    if (!std::isfinite(v))
        return false;
    a = QString::fromUtf8(p.left(end));
    return true;
}
void Network::send(const Engine &e) {
    if (!enabled)
        return;
    QMap<int, QByteArray> universes;
    auto values = e.output();
    for (int channel : e.channels.keys())
        for (int absolute : e.dmxAddresses(channel)) {
            int u = (absolute - 1) / 512;
            if (!universes.contains(u))
                universes[u] = QByteArray(512, '\0');
            universes[u][(absolute - 1) % 512] = char(qRound(values.value(channel) * 2.55));
        }
    ++serial;
    if (serial == 0)
        ++serial;
    lastError.clear();
    for (auto i = universes.begin(); i != universes.end(); ++i) {
        int u = firstUniverse + i.key();
        auto b =
            protocol == "Art-Net" ? artnet(i.value(), u, serial) : sacn(i.value(), u, serial, cid);
        QString destination = host;
        if (host == "multicast" && protocol == "sACN")
            destination = QString("239.255.%1.%2").arg(u >> 8).arg(u & 255);
        QHostAddress ip(destination);
        if (b.isEmpty() || ip.isNull()) {
            lastError = "Invalid universe or destination";
            continue;
        }
        if (sender.writeDatagram(b, ip, protocol == "Art-Net" ? 6454 : 5568) < 0)
            lastError = sender.errorString();
    }
}
} // namespace Flame
