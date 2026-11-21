#pragma once
#include "network.h"
#include <QtWidgets>
#include <functional>
#include <pxs/OutputsPanel.h>
// The Network tab: pxs's panel for the outputs, applied to the console's Network as they are edited. An output that cannot work
// (an address that is not one, a patched universe past the protocol's last) is not applied and says why; what runs keeps running.
class OutputPanel : public QWidget {
    Flame::Network &network;
    const Flame::Engine &engine;
    std::function<void()> edited;
    pxs::dmx::OutputsPanel *panel;
    QLabel *feedback;
    void changed(const QList<pxs::dmx::Output> &outputs);
  public:
    OutputPanel(Flame::Network &, const Flame::Engine &, std::function<void()> edited, QWidget *parent = nullptr);
    void reload(); // the network's outputs, as they are now (a show was opened, the startup route was made)
};
