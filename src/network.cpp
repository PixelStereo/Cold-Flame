#include "network.h"
#include <cmath>
#include <algorithm>
#include <cstring>
namespace Flame {
static void be32(QByteArray &b, int o, quint32 n) {
    for (int i = 0; i < 4; ++i)
        b[o + i] = char(n >> (24 - i * 8));
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

Network::Network(QObject *parent) : QObject(parent) {
    // Lighting UDP transports use direct LAN addresses, never HTTP/SOCKS proxy discovery.
    receiver.setProxy(QNetworkProxy::NoProxy);
    // The sACN identity is the console's own and stays the same from one run to the next
    if (!QCoreApplication::organizationName().isEmpty()) {
        QSettings settings;
        QUuid stored(settings.value("dmx/cid").toString());
        if (stored.isNull()) settings.setValue("dmx/cid", cid.toString());
        else cid = stored;
    }
    sender.setCid(cid);
}
Network::~Network() { sender.stop(); }
bool Network::enabled() const {
    return std::any_of(outputs.begin(), outputs.end(), [](const pxs::dmx::Output &o) { return o.enabled; });
}
QString Network::lastError() const {
    for (const auto &o : outputs) {
        if (!o.enabled) continue;
        const auto state = sender.state(o.id);
        if (state.level == pxs::dmx::OutputState::Error) return o.name + ": " + state.text;
    }
    return {};
}
pxs::dmx::OutputState Network::state(int index) const {
    return index >= 0 && index < outputs.size() ? sender.state(outputs[index].id) : pxs::dmx::OutputState{};
}
void Network::follow() {
    std::vector<pxs::dmx::Output> list(outputs.begin(), outputs.end());
    sender.follow(list);
}
bool Network::applyOutputs(const QList<pxs::dmx::Output> &settings, QString *error) {
    for (const auto &o : settings) {
        const auto invalid = o.validate().isEmpty() ? o.validateMapping() : o.validate();
        if (!invalid.isEmpty()) { if (error) *error = o.name + ": " + invalid; return false; }
    }
    // Each output has an identity of its own, which the sender follows it by
    quint64 next = 1;
    for (const auto &o : settings) next = std::max(next, o.id + 1);
    outputs = settings;
    QSet<quint64> seen;
    for (auto &o : outputs) {
        if (!o.id || seen.contains(o.id)) o.id = next++;
        seen.insert(o.id);
    }
    follow();
    return true;
}
void Network::stop() {
    for (auto &o : outputs) o.enabled = false;
    follow(); // the sACN streams are ended
}
bool Network::startDefaultArtNet(QString *error) {
    auto settings = outputs;
    int index = -1;
    for (int i = 0; i < settings.size(); ++i) {
        settings[i].enabled = false;
        if (index < 0 && settings[i].protocol == pxs::dmx::Protocol::ArtNet && settings[i].delivery == pxs::dmx::Delivery::Group) index = i;
    }
    if (index < 0) {
        auto output = pxs::dmx::Output::defaults(pxs::dmx::Protocol::ArtNet);
        output.name = "Art-Net broadcast";
        output.rate = 44;
        settings << output;
        index = settings.size() - 1;
    }
    settings[index].enabled = true;
    return applyOutputs(settings, error);
}
QJsonArray Network::outputJson() const {
    QJsonArray json;
    for (const auto &o : outputs) json.append(o.toJson());
    return json;
}
bool Network::parseOutputs(const QJsonArray &json, QList<pxs::dmx::Output> &settings, QString *error) {
    settings.clear();
    for (auto entry : json) {
        auto o = pxs::dmx::Output::fromJson(entry.toObject());
        o.id = 0;
        o.enabled = false; // Opening a show always leaves output switches off.
        const auto invalid = o.validate().isEmpty() ? o.validateMapping() : o.validate();
        if (!invalid.isEmpty()) { if (error) *error = o.name + ": " + invalid; return false; }
        settings << o;
    }
    return true;
}
bool Network::loadOutputs(const QJsonArray &json, QString *error) {
    QList<pxs::dmx::Output> settings;
    return parseOutputs(json, settings, error) && applyOutputs(settings, error);
}
void Network::send(const Engine &e) {
    if (!enabled()) return;
    // The universes of the patch (1, 2…) onto each output's range; the channels that are patched are ours, so that sACN says
    // so channel by channel and another source can have the rest of a universe
    std::map<int, QByteArray> frames, owned;
    const auto universes = e.dmxFrames();
    for (auto it = universes.begin(); it != universes.end(); ++it) frames[it.key() + 1] = it.value();
    for (int channel : e.channels.keys()) for (int address : e.occupiedAddresses(channel)) {
        if (address < 1) continue;
        const int universe = (address - 1) / 512 + 1;
        if (!owned.count(universe)) owned[universe] = QByteArray(512, '\0');
        owned[universe][(address - 1) % 512] = 1;
    }
    std::vector<pxs::dmx::Output> list(outputs.begin(), outputs.end());
    sender.submit(pxs::dmx::route(frames, list), pxs::dmx::route(owned, list));
}
} // namespace Flame
