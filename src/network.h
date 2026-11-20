#pragma once
#include "engine.h"
#include <QtNetwork>
#include <pxs/DmxOut.h>
namespace Flame {
QByteArray oscMessage(QString address, float value);
bool oscDecode(const QByteArray &packet, QString &address, float &value);
// The console's DMX outputs: pxs's (settings, validation, JSON, the sender on its own thread), with the patch's universes routed onto
// them. Opening a show leaves every output off.
class Network : public QObject {
    pxs::dmx::Sender sender;
    void follow();
  public:
    QList<pxs::dmx::Output> outputs;
    QUuid cid = QUuid::createUuid(); // the console's sACN identity, also what the monitor recognises as its own
    QUdpSocket receiver; // the local OSC input
    explicit Network(QObject *parent = nullptr);
    ~Network() override;
    bool enabled() const; // an output is on
    QString lastError() const;
    pxs::dmx::OutputState state(int index) const;
    // Atomic: an output that cannot work rejects them all, and what runs keeps running
    bool applyOutputs(const QList<pxs::dmx::Output> &settings, QString *error = nullptr);
    void stop();
    bool startDefaultArtNet(QString *error = nullptr);
    QJsonArray outputJson() const;
    // The outputs a show saved, switched off and checked (the application's own are left as they are until it applies them)
    static bool parseOutputs(const QJsonArray &json, QList<pxs::dmx::Output> &outputs, QString *error = nullptr);
    bool loadOutputs(const QJsonArray &json, QString *error = nullptr);
    void send(const Engine &engine); // the latest levels, to be streamed at each output's rate
};
} // namespace Flame
