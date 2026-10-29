#pragma once
#include "engine.h"
#include <QtNetwork>
namespace Flame {
QByteArray artnet(const QByteArray &dmx, quint16 universe, quint8 sequence);
QByteArray sacn(const QByteArray &dmx, quint16 universe, quint8 sequence, const QUuid &cid, int priority = 100,
                QString sourceName = "Cold Flame", bool terminated = false);
QByteArray oscMessage(QString address, float value);
bool oscDecode(const QByteArray &packet, QString &address, float &value);
struct DmxOutput {
    QString name = "Output 1", protocol = "Art-Net", mode = "Unicast";
    QString interfaceAddress, destinations = "127.0.0.1", sourceName = "Cold Flame";
    bool enabled = false;
    int firstPatchUniverse = 1, firstUniverse = 1, universeCount = 0, priority = 100, rate = 44;
};
struct DmxState { quint64 packets = 0; QString error; };
class Network : public QObject {
    struct Link {
        QSharedPointer<QUdpSocket> socket;
        DmxState state;
        QMap<int, QByteArray> lastFrames;
        quint8 sequence = 0;
        qint64 nextFrame = 0;
    };
    QList<Link> links;
    QElapsedTimer transportClock;
    void terminate(int index);
  public:
    QList<DmxOutput> outputs;
    bool applyOutputs(const QList<DmxOutput> &settings, QString *error = nullptr);
    static QString validateOutput(const DmxOutput &output);
    DmxState state(int index) const;
    QJsonArray outputJson() const;
    bool loadOutputs(const QJsonArray &json, QString *error = nullptr);
    void stop();
    bool startDefaultArtNet(QString *error = nullptr);
    ~Network() override { stop(); }

    // Legacy single-route API retained for existing transport clients.
    QUdpSocket sender, receiver;
    QUuid cid = QUuid::createUuid();
    quint8 serial = 0;
    bool enabled = false;
    QString protocol = "Art-Net", host = "127.0.0.1", lastError;
    int firstUniverse = 1;
    explicit Network(QObject *parent = nullptr) : QObject(parent) {
        // Lighting UDP transports use direct LAN addresses, never HTTP/SOCKS proxy discovery.
        sender.setProxy(QNetworkProxy::NoProxy);
        receiver.setProxy(QNetworkProxy::NoProxy);
    }
    void send(const Engine &engine);
};
} // namespace Flame
