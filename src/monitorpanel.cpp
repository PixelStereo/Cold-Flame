#include "monitorpanel.h"
#include "network.h"
MonitorPanel::MonitorPanel(Flame::Network &n, QWidget *parent) : QWidget(parent), network(n) {
    setObjectName("networkMonitor");
    monitor.setOwnCid(network.cid.toRfc4122()); // the console's own sACN streams are its own
    monitor.setOwnLabel("Cold Flame output");
    auto layout = new QVBoxLayout(this);
    auto tools = new QHBoxLayout;
    interface = new QComboBox; interface->setObjectName("monitorInterface"); interface->setMinimumWidth(220);
    interfaces();
    protocol = new QComboBox; protocol->setObjectName("monitorProtocol"); protocol->addItems({"Art-Net", "sACN"});
    universe = new QSpinBox; universe->setObjectName("monitorUniverse"); universe->setRange(0, 32767); universe->setKeyboardTracking(false);
    merge = new QComboBox; merge->setObjectName("monitorMerge"); merge->addItems({"HTP", "LTP"});
    merge->setToolTip("How several sources of the universe are merged: highest level, or last to change");
    listen = new QPushButton("Start listening"); listen->setObjectName("monitorListen"); listen->setCheckable(true);
    listen->setToolTip("Listen to the network on that adapter. Off until asked.");
    activeOnly = new QToolButton; activeOnly->setObjectName("monitorActiveOnly"); activeOnly->setText("Active channels only");
    activeOnly->setCheckable(true); activeOnly->setToolTip("Only the channels above zero, keeping their DMX addresses");
    tools->addWidget(interface, 1); tools->addWidget(protocol); tools->addWidget(new QLabel("Universe")); tools->addWidget(universe);
    tools->addWidget(merge); tools->addWidget(activeOnly); tools->addWidget(listen);
    layout->addLayout(tools);
    grid = new pxs::dmx::ChannelGrid;
    grid->setEditable(false); // a monitor: click a channel to read it, nothing to send from here
    layout->addWidget(grid, 1);
    universes = new QLabel; universes->setObjectName("monitorUniverses"); universes->setWordWrap(true);
    status = new QLabel("Not listening"); status->setObjectName("monitorStatus");
    layout->addWidget(universes); layout->addWidget(status);
    connect(listen, &QPushButton::toggled, this, [this](bool on) { if (on) start(); else stop(); });
    connect(activeOnly, &QToolButton::toggled, grid, &pxs::dmx::ChannelGrid::setActiveOnly);
    connect(protocol, &QComboBox::currentIndexChanged, this, [this](int sacn) {
        universe->setRange(sacn ? 1 : 0, sacn ? 63999 : 32767);
        if (listen->isChecked()) { QString error; monitor.listenTo(sacn ? universe->value() : 0, &error); }
    });
    connect(universe, &QSpinBox::valueChanged, this, [this](int number) {
        if (listen->isChecked() && protocol->currentIndex()) { QString error; monitor.listenTo(number, &error); }
    });
    connect(&monitor, &pxs::dmx::Monitor::problem, this, [this](const QString &text) { status->setText(text); });
    connect(&refresh, &QTimer::timeout, this, &MonitorPanel::render);
}
QString MonitorPanel::key() const { return QString("%1/%2").arg(protocol->currentIndex()).arg(universe->value()); }
void MonitorPanel::interfaces() {
    interface->clear();
    for (const auto &n : QNetworkInterface::allInterfaces()) {
        if (!n.flags().testFlag(QNetworkInterface::IsUp)) continue;
        QStringList addresses;
        for (const auto &entry : n.addressEntries())
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) addresses << entry.ip().toString();
        if (addresses.isEmpty()) continue;
        interface->addItem(QString("%1 · %2%3").arg(n.humanReadableName(), addresses.join(", "),
                           n.flags().testFlag(QNetworkInterface::IsLoopBack) ? " [local]" : ""), n.index());
    }
    for (int i = 0; i < interface->count(); ++i)
        if (!QNetworkInterface::interfaceFromIndex(interface->itemData(i).toInt()).flags().testFlag(QNetworkInterface::IsLoopBack)) {
            interface->setCurrentIndex(i);
            break;
        }
}
void MonitorPanel::start() {
    const auto adapter = QNetworkInterface::interfaceFromIndex(interface->currentData().toInt());
    QString error;
    if (interface->currentIndex() < 0 || !monitor.start(adapter, &error)) {
        status->setText("Cannot listen: " + (error.isEmpty() ? QString("no network interface") : error));
        QSignalBlocker blocker(listen); listen->setChecked(false);
        return;
    }
    if (protocol->currentIndex()) monitor.listenTo(universe->value(), &error);
    interface->setEnabled(false);
    listen->setText("Stop listening");
    held.clear();
    refresh.start(33);
    render();
}
void MonitorPanel::stop() {
    refresh.stop();
    monitor.stop();
    interface->setEnabled(true);
    listen->setText("Start listening");
    status->setText("Not listening");
    universes->clear();
    render();
}
void MonitorPanel::render() {
    monitor.prune();
    const bool sacn = protocol->currentIndex();
    const auto mode = merge->currentIndex() ? pxs::dmx::MergeMode::LTP : pxs::dmx::MergeMode::HTP;
    bool live = false;
    QByteArray levels = monitor.merged(sacn, universe->value(), mode, false, &live);
    if (!levels.isEmpty()) held[key()] = levels; else levels = held.value(key());
    pxs::dmx::ChannelGrid::State state;
    state.received = levels.size() == 512 ? levels : QByteArray();
    const QByteArray others = monitor.merged(sacn, universe->value(), mode, true);
    state.others = others.size() == 512 ? others : QByteArray();
    grid->setState(state);
    // The universes heard, and the sources of the one shown
    QMap<QString, int> heard;
    int sources = 0;
    for (const auto &s : monitor.streams()) {
        if (!monitor.live(s)) continue;
        ++heard[QString("%1 U%2").arg(s.frame.sacn ? "sACN" : "Art-Net").arg(s.frame.universe)];
        if (s.frame.sacn == sacn && s.frame.universe == universe->value()) ++sources;
    }
    QStringList list;
    for (auto i = heard.cbegin(); i != heard.cend(); ++i) list << QString("%1 (%2)").arg(i.key()).arg(i.value());
    universes->setText(monitor.listening() ? (list.isEmpty() ? QString("No source heard yet") : "Heard: " + list.join(" · ")) : QString());
    if (monitor.listening()) {
        const auto stats = monitor.stats();
        status->setText(QString("%1 · %2 live source%3 on this universe · %4 datagrams, %5 unsupported")
                            .arg(live ? "Live" : (state.received.isEmpty() ? "No signal" : "Stale"))
                            .arg(sources).arg(sources == 1 ? "" : "s").arg(stats.datagrams).arg(stats.invalid));
    }
}
