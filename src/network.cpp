#include "network.h"
#include <cmath>
#include <algorithm>
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
QByteArray sacn(const QByteArray &dmx, quint16 universe, quint8 sequence, const QUuid &cid, int priority, QString sourceName, bool terminated) {
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
    QByteArray name = sourceName.toUtf8().left(63);
    b.replace(44, name.size(), name);
    b[108] = char(qBound(0, priority, 200));
    b[112] = terminated ? char(0x40) : 0;
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
QString Network::validateOutput(const DmxOutput &o) {
    if (o.protocol != "Art-Net" && o.protocol != "sACN") return "Unknown protocol";
    bool art = o.protocol == "Art-Net";
    if (o.firstUniverse < (art ? 0 : 1) || o.firstUniverse > (art ? 32767 : 63999)) return "First network universe is outside protocol limits";
    if (o.firstPatchUniverse < 1 || o.firstPatchUniverse > 32768 || o.universeCount < 0 || o.universeCount > 32768)
        return "Invalid patch universe range";
    if (o.universeCount && (o.firstPatchUniverse + o.universeCount - 1 > 32768 || o.firstUniverse + o.universeCount - 1 > (art ? 32767 : 63999)))
        return "Universe range exceeds protocol limits";
    if (o.rate < 1 || o.rate > 44 || o.priority < 0 || o.priority > 200) return "Rate must be 1–44 Hz; priority must be 0–200";
    if (o.mode != "Unicast" && o.mode != (art ? "Broadcast" : "Multicast")) return "Invalid destination mode for protocol";
    auto ipv4 = [](QString text) { return QHostAddress(text).protocol() == QAbstractSocket::IPv4Protocol; };
    if (!o.interfaceAddress.isEmpty() && !ipv4(o.interfaceAddress)) return "Interface must be an IPv4 address";
    if (o.mode == "Unicast") {
        auto addresses = o.destinations.split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts);
        if (addresses.isEmpty()) return "Enter at least one unicast IPv4 address";
        for (const auto &address : addresses) if (!ipv4(address)) return "Invalid IPv4 address: " + address;
    }
    return {};
}
DmxState Network::state(int index) const {
    return index >= 0 && index < links.size() ? links[index].state : DmxState{};
}
void Network::terminate(int index) {
    if (index < 0 || index >= links.size() || index >= outputs.size()) return;
    auto &link = links[index];
    const auto &o = outputs[index];
    if (!link.socket || o.protocol != "sACN") return;
    for (auto it = link.lastFrames.begin(); it != link.lastFrames.end(); ++it) {
        QStringList destinations = o.mode == "Multicast"
            ? QStringList{QString("239.255.%1.%2").arg(it.key() >> 8).arg(it.key() & 255)}
            : o.destinations.split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts);
        for (int repeat = 0; repeat < 3; ++repeat) {
            auto packet = sacn(it.value(), it.key(), ++link.sequence, cid, o.priority, o.sourceName, true);
            for (const auto &address : destinations) link.socket->writeDatagram(packet, QHostAddress(address), 5568);
        }
    }
}
void Network::stop() {
    for (int i = 0; i < links.size(); ++i) terminate(i);
    links.clear();
    links.resize(outputs.size());
    enabled = false;
    lastError.clear();
}
bool Network::applyOutputs(const QList<DmxOutput> &settings, QString *error) {
    for (const auto &o : settings) {
        auto invalid = validateOutput(o);
        if (!invalid.isEmpty()) { if (error) *error = o.name + ": " + invalid; return false; }
    }
    stop();
    outputs = settings;
    links.resize(outputs.size());
    enabled = std::any_of(outputs.begin(), outputs.end(), [](const DmxOutput &o) { return o.enabled; });
    return true;
}
QJsonArray Network::outputJson() const {
    QJsonArray json;
    for (const auto &o : outputs)
        json.append(QJsonObject{{"name", o.name}, {"protocol", o.protocol}, {"mode", o.mode},
            {"interface", o.interfaceAddress}, {"destinations", o.destinations}, {"sourceName", o.sourceName},
            {"firstPatchUniverse", o.firstPatchUniverse}, {"firstUniverse", o.firstUniverse},
            {"universeCount", o.universeCount}, {"priority", o.priority}, {"rate", o.rate}});
    return json;
}
bool Network::loadOutputs(const QJsonArray &json, QString *error) {
    QList<DmxOutput> settings;
    for (auto entry : json) {
        auto v = entry.toObject(); DmxOutput o;
        o.name = v["name"].toString(); o.protocol = v["protocol"].toString(); o.mode = v["mode"].toString();
        o.interfaceAddress = v["interface"].toString(); o.destinations = v["destinations"].toString();
        o.sourceName = v["sourceName"].toString("Cold Flame");
        o.firstPatchUniverse = v["firstPatchUniverse"].toInt(1); o.firstUniverse = v["firstUniverse"].toInt(1);
        o.universeCount = v["universeCount"].toInt(); o.priority = v["priority"].toInt(100); o.rate = v["rate"].toInt(44);
        settings << o; // Opening a show always leaves output switches off.
    }
    return applyOutputs(settings, error);
}
void Network::send(const Engine &e) {
    if (!enabled)
        return;
    const auto universes = e.dmxFrames();
    if (!outputs.isEmpty()) {
        if (!transportClock.isValid()) transportClock.start();
        const qint64 now = transportClock.elapsed();
        lastError.clear();
        for (int index = 0; index < outputs.size(); ++index) {
            const auto &o = outputs[index];
            auto &link = links[index];
            if (!o.enabled || now < link.nextFrame) continue;
            link.nextFrame = now + qMax(1, 1000 / o.rate);
            link.state.error.clear();
            if (!link.socket) {
                link.socket.reset(new QUdpSocket);
                link.socket->setProxy(QNetworkProxy::NoProxy);
                QHostAddress local = o.interfaceAddress.isEmpty() ? QHostAddress(QHostAddress::AnyIPv4) : QHostAddress(o.interfaceAddress);
                if (!link.socket->bind(local, 0)) {
                    link.state.error = link.socket->errorString();
                    link.socket.reset();
                    lastError = o.name + ": " + link.state.error;
                    continue;
                }
                for (const auto &card : QNetworkInterface::allInterfaces())
                    for (const auto &entry : card.addressEntries())
                        if (entry.ip() == local && o.mode == "Multicast")
                            link.socket->setMulticastInterface(card);
            }
            if (++link.sequence == 0) ++link.sequence;
            for (auto it = universes.begin(); it != universes.end(); ++it) {
                int offset = it.key() + 1 - o.firstPatchUniverse;
                if (offset < 0 || (o.universeCount && offset >= o.universeCount)) continue;
                int wire = o.firstUniverse + offset;
                QByteArray packet = o.protocol == "Art-Net" ? artnet(it.value(), wire, link.sequence)
                    : sacn(it.value(), wire, link.sequence, cid, o.priority, o.sourceName);
                if (wire > (o.protocol == "Art-Net" ? 32767 : 63999) || packet.isEmpty()) {
                    link.state.error = QString("Mapped universe %1 is outside protocol limits").arg(wire);
                    continue;
                }
                QStringList destinations;
                if (o.mode == "Unicast") destinations = o.destinations.split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts);
                else if (o.mode == "Multicast") destinations << QString("239.255.%1.%2").arg(wire >> 8).arg(wire & 255);
                else {
                    QString broadcast = "255.255.255.255";
                    for (const auto &card : QNetworkInterface::allInterfaces())
                        for (const auto &entry : card.addressEntries())
                            if (entry.ip().toString() == o.interfaceAddress && !entry.broadcast().isNull())
                                broadcast = entry.broadcast().toString();
                    destinations << broadcast;
                }
                for (const auto &destination : destinations) {
                    if (link.socket->writeDatagram(packet, QHostAddress(destination), o.protocol == "Art-Net" ? 6454 : 5568) == packet.size())
                        ++link.state.packets;
                    else link.state.error = link.socket->errorString();
                }
                link.lastFrames[wire] = it.value();
            }
            if (!link.state.error.isEmpty()) lastError = o.name + ": " + link.state.error;
        }
        return;
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
