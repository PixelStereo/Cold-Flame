#include "outputpanel.h"
#include "tabledelegate.h"
using namespace Flame;
OutputPanel::OutputPanel(Network &n, const Engine &e, std::function<void()> changed, QWidget *parent)
    : QWidget(parent), network(n), engine(e), edited(std::move(changed)) {
    setObjectName("dmxOutputs");
    auto layout = new QVBoxLayout(this);
    auto tools = new QHBoxLayout;
    auto add = new QPushButton("+ Output"); add->setObjectName("addDmxOutput");
    auto remove = new QPushButton("− Output"); remove->setObjectName("removeDmxOutput");
    auto applyButton = new QPushButton("Apply outputs"); applyButton->setObjectName("applyDmxOutputs");
    auto stop = new QPushButton("Stop all"); stop->setObjectName("stopDmxOutputs");
    tools->addWidget(add); tools->addWidget(remove); tools->addStretch();
    tools->addWidget(applyButton); tools->addWidget(stop); layout->addLayout(tools);
    table = new QTableWidget(0, 4); table->setObjectName("dmxOutputsTable");
    table->setHorizontalHeaderLabels({"On", "Output", "Protocol", "Status"});
    table->setItemDelegate(new TableDelegate(table));
    table->verticalHeader()->hide(); table->verticalHeader()->setDefaultSectionSize(30);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setMinimumHeight(90); table->setMaximumHeight(120);
    layout->addWidget(table);
    auto scroll = new QScrollArea; scroll->setWidgetResizable(true);
    auto editor = new QWidget; form = new QFormLayout(editor);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    name = new QLineEdit; name->setObjectName("dmxOutputName");
    protocol = new QComboBox; protocol->addItems({"Art-Net", "sACN"}); protocol->setObjectName("dmxOutputProtocol");
    interface = new QComboBox; interface->setObjectName("dmxOutputInterface");
    mode = new QComboBox; mode->setObjectName("dmxOutputMode");
    addresses = new QLineEdit; addresses->setObjectName("dmxOutputAddresses");
    addresses->setPlaceholderText("2.0.0.10, 2.0.0.11");
    source = new QLineEdit; source->setObjectName("dmxOutputSourceName");
    auto spin = [](int low, int high, QString object) {
        auto s = new QSpinBox; s->setRange(low, high); s->setKeyboardTracking(false); s->setObjectName(object); return s;
    };
    patchFirst = spin(1, 32768, "dmxOutputPatchFirst");
    wireFirst = spin(0, 63999, "dmxOutputUniverse");
    count = spin(0, 32768, "dmxOutputCount"); count->setSpecialValueText("All patched universes");
    rate = spin(1, 44, "dmxOutputRate"); rate->setSuffix(" Hz");
    priority = spin(0, 200, "dmxOutputPriority");
    port = spin(0, 65535, "dmxOutputPort"); port->setSpecialValueText("Protocol default");
    transmission = new QComboBox; transmission->addItems({"Full (512 channels)", "Partial (used channels)"}); transmission->setObjectName("dmxOutputTransmission");
    form->addRow("Name", name); form->addRow("Protocol", protocol);
    form->addRow("Network interface", interface); form->addRow("Destination mode", mode);
    form->addRow("Unicast IPv4 addresses", addresses); form->addRow("Port", port);
    form->addRow("First patch universe", patchFirst); form->addRow("First network universe", wireFirst);
    form->addRow("Universe count", count); form->addRow("Refresh rate", rate); form->addRow("Frames", transmission);
    form->addRow("sACN priority", priority); form->addRow("sACN source name", source);
    scroll->setWidget(editor); layout->addWidget(scroll, 1);
    route = new QLabel; route->setWordWrap(true); layout->addWidget(route);
    feedback = new QLabel("Outputs are off until enabled and applied."); feedback->setWordWrap(true); layout->addWidget(feedback);
    auto changedField = [this] { if (!updating) { store(); pending = true; feedback->setText("Unapplied changes · Apply outputs to activate this configuration"); } };
    for (auto edit : {name, addresses, source}) connect(edit, &QLineEdit::textEdited, this, changedField);
    for (auto spin : {patchFirst, wireFirst, count, rate, priority, port}) connect(spin, &QSpinBox::valueChanged, this, changedField);
    connect(interface, &QComboBox::activated, this, changedField);
    connect(transmission, &QComboBox::activated, this, changedField);
    connect(mode, &QComboBox::activated, this, [this, changedField] { changedField(); updateProtocol(); });
    connect(protocol, &QComboBox::activated, this, [this, changedField] { updateProtocol(); changedField(); });
    connect(table, &QTableWidget::currentCellChanged, this, [this](int row) { store(); select(row); });
    connect(table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (updating || item->column() != 0) return;
        draft[item->row()].enabled = item->checkState() == Qt::Checked;
        pending = true; feedback->setText("Unapplied changes · Apply outputs to activate");
    });
    connect(add, &QPushButton::clicked, this, [this] {
        store(); auto output = pxs::dmx::Output::defaults(pxs::dmx::Protocol::ArtNet); output.name = QString("Output %1").arg(draft.size() + 1);
        output.enabled = false; output.rate = 44;
        draft << output; pending = true; rows(); table->selectRow(draft.size() - 1);
        feedback->setText("New output is off. Configure it, tick On, then Apply outputs.");
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (selected < 0) return;
        draft.removeAt(selected); selected = -1; pending = true; rows();
        feedback->setText("Output removed from draft · Apply outputs to stop and remove it");
    });
    connect(applyButton, &QPushButton::clicked, this, [this] { apply(); });
    connect(stop, &QPushButton::clicked, this, [this] {
        network.stop();
        reload(); edited(); feedback->setText("All outputs stopped");
    });
    auto timer = new QTimer(this); timer->setInterval(400);
    connect(timer, &QTimer::timeout, this, [this] { if (isVisible()) states(); }); timer->start();
    reload();
}
void OutputPanel::store() {
    if (updating || selected < 0 || selected >= draft.size()) return;
    auto &o = draft[selected];
    using namespace pxs::dmx;
    o.name = name->text().trimmed(); o.protocol = protocol->currentText() == "Art-Net" ? Protocol::ArtNet : Protocol::Sacn;
    o.delivery = mode->currentText() == "Unicast" ? Delivery::Unicast : Delivery::Group;
    o.interfaceAddress = interface->currentData().toString();
    o.destinations = addresses->text().split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts);
    o.sourceName = source->text().trimmed(); o.priority = priority->value(); o.rate = rate->value();
    o.port = port->value(); o.transmission = transmission->currentIndex() ? Transmission::Partial : Transmission::Full;
    o.firstSourceUniverse = patchFirst->value(); o.firstUniverse = wireFirst->value(); o.universeCount = count->value();
    const QString protocolName = o.protocol == Protocol::ArtNet ? "Art-Net" : "sACN";
    if (table->item(selected, 1)) {
        QSignalBlocker block(table);
        table->item(selected, 1)->setText(o.name); table->item(selected, 2)->setText(protocolName);
    }
    route->setText(QString("Patch %1 → network %2 · %3 · Only patched universes are transmitted.")
        .arg(o.firstSourceUniverse).arg(o.firstUniverse)
        .arg(o.protocol == Protocol::ArtNet ? "Art-Net: 0–32767" : "sACN: 1–63999"));
}
void OutputPanel::updateProtocol() {
    const bool art = protocol->currentText() == "Art-Net";
    QSignalBlocker block(mode);
    const bool unicast = mode->currentText() == "Unicast";
    mode->clear(); mode->addItems({art ? "Broadcast" : "Multicast", "Unicast"});
    mode->setCurrentIndex(unicast ? 1 : 0);
    wireFirst->setRange(art ? 0 : 1, art ? 32767 : 63999);
    addresses->setEnabled(unicast); form->labelForField(addresses)->setEnabled(unicast);
    for (auto w : {static_cast<QWidget *>(priority), static_cast<QWidget *>(source)}) {
        w->setVisible(!art); form->labelForField(w)->setVisible(!art);
    }
}
void OutputPanel::select(int row) {
    updating = true; selected = row;
    const bool valid = row >= 0 && row < draft.size();
    for (int i = 0; i < form->count(); ++i)
        if (auto widget = form->itemAt(i)->widget()) widget->setEnabled(valid);
    if (!valid) { updating = false; return; }
    const auto &o = draft[row];
    name->setText(o.name); protocol->setCurrentText(o.protocol == pxs::dmx::Protocol::ArtNet ? "Art-Net" : "sACN");
    interface->clear(); interface->addItem("Automatic · system routing", "");
    for (const auto &card : QNetworkInterface::allInterfaces()) {
        if (!(card.flags() & QNetworkInterface::IsUp)) continue;
        for (const auto &entry : card.addressEntries())
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol)
                interface->addItem(card.humanReadableName() + " · " + entry.ip().toString(), entry.ip().toString());
    }
    int at = interface->findData(o.interfaceAddress);
    if (at < 0) { interface->addItem(o.interfaceAddress + " · unavailable", o.interfaceAddress); at = interface->count() - 1; }
    interface->setCurrentIndex(at);
    const bool art = o.protocol == pxs::dmx::Protocol::ArtNet;
    const QString modeName = o.delivery == pxs::dmx::Delivery::Unicast ? "Unicast" : art ? "Broadcast" : "Multicast";
    mode->clear(); mode->addItem(modeName); updateProtocol(); mode->setCurrentText(modeName);
    addresses->setText(o.destinations.join(", ")); source->setText(o.sourceName);
    priority->setValue(o.priority); rate->setValue(int(o.rate)); port->setValue(o.port);
    transmission->setCurrentIndex(o.transmission == pxs::dmx::Transmission::Partial ? 1 : 0);
    patchFirst->setValue(o.firstSourceUniverse); wireFirst->setValue(o.firstUniverse); count->setValue(o.universeCount);
    updating = false; store();
}
void OutputPanel::rows() {
    updating = true; QSignalBlocker block(table);
    table->setRowCount(draft.size());
    for (int i = 0; i < draft.size(); ++i) {
        auto on = new QTableWidgetItem; on->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        on->setCheckState(draft[i].enabled ? Qt::Checked : Qt::Unchecked); table->setItem(i, 0, on);
        for (int c = 1; c < 4; ++c) {
            auto item = new QTableWidgetItem(c == 1 ? draft[i].name : c == 2 ? QString(draft[i].protocol == pxs::dmx::Protocol::ArtNet ? "Art-Net" : "sACN") : QString("Off"));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable); table->setItem(i, c, item);
        }
    }
    updating = false; selected = -1;
    if (!draft.isEmpty()) { table->setCurrentCell(0, 1); select(0); } else select(-1);
    states();
}
void OutputPanel::reload() { selected = -1; draft = network.outputs; pending = false; rows(); }
bool OutputPanel::apply() {
    store();
    for (const auto &o : draft) {
        const int max = o.highestUniverse();
        for (int channel : engine.channels.keys()) for (int address : engine.dmxAddresses(channel)) {
            int offset = (address - 1) / 512 + 1 - o.firstSourceUniverse;
            if (offset >= 0 && (!o.universeCount || offset < o.universeCount) && o.firstUniverse + offset > max) {
                feedback->setText(o.name + ": patched universes exceed the protocol range"); return false;
            }
        }
    }
    QString error;
    if (!network.applyOutputs(draft, &error)) { feedback->setText(error); return false; }
    pending = false; edited(); states();
    feedback->setText(network.enabled() ? "Applied · outputs enabled" : "Applied · outputs off");
    return true;
}
void OutputPanel::states() {
    QSignalBlocker block(table);
    for (int i = 0; i < table->rowCount(); ++i) {
        auto state = network.state(i);
        const bool failed = state.level == pxs::dmx::OutputState::Error;
        QString text = pending ? "Unapplied draft" : !network.enabled() || !draft[i].enabled ? "Off"
            : failed ? "Error: " + state.text
            : state.packets ? QString("Sending · %1 packets").arg(state.packets) : "Waiting for frame";
        table->item(i, 3)->setText(text);
        table->item(i, 3)->setToolTip(text);
        table->item(i, 3)->setForeground(failed && !pending ? QColor("#e0c998") : QColor("#e5e5e5"));
    }
}
