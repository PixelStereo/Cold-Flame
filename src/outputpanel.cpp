#include "outputpanel.h"
using namespace Flame;
OutputPanel::OutputPanel(Network &n, const Engine &e, std::function<void()> changedCallback, QWidget *parent)
    : QWidget(parent), network(n), engine(e), edited(std::move(changedCallback)) {
    setObjectName("dmxOutputs");
    auto layout = new QVBoxLayout(this);
    panel = new pxs::dmx::OutputsPanel;
    panel->setDefaultSourceName("Cold Flame");
    panel->setMappingShown(true); // the patch's universes are mapped onto each output's
    panel->setStateSource([this](quint64 id) {
        for (int i = 0; i < network.outputs.size(); ++i)
            if (network.outputs[i].id == id) return network.state(i);
        return pxs::dmx::OutputState{};
    });
    layout->addWidget(panel);
    feedback = new QLabel("Outputs are off until enabled."); feedback->setObjectName("dmxOutputFeedback"); feedback->setWordWrap(true);
    layout->addWidget(feedback);
    connect(panel, &pxs::dmx::OutputsPanel::outputsChanged, this, [this](const QList<pxs::dmx::Output> &outputs) { changed(outputs); });
    reload();
}
void OutputPanel::reload() { panel->setOutputs(network.outputs); feedback->setText(network.enabled() ? "Outputs enabled" : "Outputs are off until enabled."); }
void OutputPanel::changed(const QList<pxs::dmx::Output> &outputs) {
    // A patched universe that the output would send past the protocol's last one
    for (const auto &o : outputs) {
        for (int channel : engine.channels.keys()) for (int address : engine.dmxAddresses(channel)) {
            const int offset = (address - 1) / 512 + 1 - o.firstSourceUniverse;
            if (offset >= 0 && (!o.universeCount || offset < o.universeCount) && o.firstUniverse + offset > o.highestUniverse()) {
                feedback->setText(o.name + ": patched universes exceed the protocol range"); return;
            }
        }
    }
    QString error;
    if (!network.applyOutputs(outputs, &error)) { feedback->setText(error); return; }
    edited();
    feedback->setText(network.enabled() ? "Outputs enabled" : "Outputs off");
}
