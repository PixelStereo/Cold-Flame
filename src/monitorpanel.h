#pragma once
#include <QtWidgets>
#include <pxs/ChannelGrid.h>
#include <pxs/DmxNet.h>
namespace Flame {
class Network;
}
// What the network carries, to see what the other devices send and whether the console's own output arrives: a read-only
// view of a universe (Art-Net or sACN) that listens only when asked.
class MonitorPanel : public QWidget {
    Flame::Network &network;
    pxs::dmx::Monitor monitor;
    pxs::dmx::ChannelGrid *grid;
    QComboBox *interface, *protocol, *merge;
    QSpinBox *universe;
    QPushButton *listen;
    QToolButton *activeOnly;
    QLabel *status, *universes;
    QTimer refresh;
    QMap<QString, QByteArray> held; // the last levels of each universe, kept when its sources go quiet
    QString key() const;
    void interfaces();
    void start();
    void stop();
    void render();
  public:
    MonitorPanel(Flame::Network &, QWidget *parent = nullptr);
    bool listening() const { return monitor.listening(); }
};
