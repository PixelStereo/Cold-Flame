#pragma once
#include "engine.h"
#include <QtNetwork>
namespace Flame {
QByteArray artnet(const QByteArray &dmx, quint16 universe, quint8 sequence);
QByteArray sacn(const QByteArray &dmx, quint16 universe, quint8 sequence, const QUuid &cid);
QByteArray oscMessage(QString address, float value);
bool oscDecode(const QByteArray &packet, QString &address, float &value);
class Network : public QObject {
  public:
    QUdpSocket sender, receiver;
    QUuid cid = QUuid::createUuid();
    quint8 serial = 0;
    bool enabled = false;
    QString protocol = "Art-Net", host = "127.0.0.1", lastError;
    int firstUniverse = 1;
    explicit Network(QObject *parent = nullptr) : QObject(parent) {}
    void send(const Engine &engine);
};
} // namespace Flame
