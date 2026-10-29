#include "parameterpanel.h"
#include "numberdrag.h"
using namespace Flame;
ParameterPanel::ParameterPanel(Engine &e, std::function<void()> onLiveChange, QWidget *parent)
    : QWidget(parent), engine(e), onLiveChange(std::move(onLiveChange)) {
    auto layout = new QVBoxLayout(this); channel = new QComboBox; channel->setObjectName("parameterChannel"); layout->addWidget(channel);
    description = new QLabel; description->setWordWrap(true); layout->addWidget(description);
    auto note = new QLabel("Live parameter control · values are DMX percentages. Record / Update stores these values with the cue. RGB profiles without a dimmer use a virtual intensity."); note->setWordWrap(true); layout->addWidget(note);
    table = new QTableWidget(0, 4); table->setObjectName("parameterTable"); table->setHorizontalHeaderLabels({"Parameter", "Resolution", "Level %", "Source"});
    table->verticalHeader()->hide(); table->verticalHeader()->setDefaultSectionSize(40);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(table, 1); auto actions = new QHBoxLayout;
    auto release = new QPushButton("Release fixture"); auto home = new QPushButton("Home parameters"); actions->addWidget(release); actions->addWidget(home); actions->addStretch(); layout->addLayout(actions);
    release->setStatusTip("Release this fixture's manual values, retaining captured controls."); home->setStatusTip("Set non-intensity parameters to their GDTF defaults in manual control.");
    connect(channel, &QComboBox::currentIndexChanged, this, [this] { populate(channel->currentData().toInt()); });
    connect(release, &QPushButton::clicked, this, [this] {
        for (int key : engine.controlIds()) if (engine.channelForControl(key) == shown && !engine.captured.contains(key)) engine.manual.remove(key);
        this->onLiveChange(); refresh();
    });
    connect(home, &QPushButton::clicked, this, [this] {
        for (int key : engine.controlIds()) if (key < 0 && engine.channelForControl(key) == shown) engine.manual[key] = {engine.controlDefault(key), 0};
        this->onLiveChange(); refresh();
    });
    reload();
}
void ParameterPanel::reload() {
    int id = channel->currentData().toInt(); { QSignalBlocker block(channel); channel->clear();
        for (int key : engine.channels.keys()) channel->addItem(QString("%1 · %2").arg(key).arg(engine.channels[key]), key);
        int index = channel->findData(id); channel->setCurrentIndex(index < 0 ? 0 : index);
    }
    populate(channel->currentData().toInt());
}
void ParameterPanel::selectChannel(int id) { int index = channel->findData(id); if (index >= 0) channel->setCurrentIndex(index); }
void ParameterPanel::populate(int id) {
    shown = id; const auto assignment = engine.fixtures.value(id); const auto profile = engine.fixtureProfiles.value(assignment.profile); const auto mode = engine.fixtureMode(id);
    description->setText(profile.manufacturer + " / " + profile.name + " / " + mode.name + " · " + QString::number(mode.footprint) + " DMX slots");
    table->setRowCount(0); QList<int> keys{id}; for (int i = 0; i < mode.parameters.size(); ++i) if (!mode.parameters[i].intensity) keys << engine.parameterKey(id, i);
    for (int key : keys) {
        int row = table->rowCount(); table->insertRow(row); QString resolution = "Virtual"; int decimals = 2;
        for (int i = 0; i < mode.parameters.size(); ++i) if (engine.parameterKey(id, i) == key) { resolution = QString::number(mode.parameters[i].offsets.size() * 8) + " bit"; decimals = mode.parameters[i].offsets.size() * 2; }
        auto label = new QTableWidgetItem(engine.controlLabel(key).section(" · ", 2)); label->setData(Qt::UserRole, key); table->setItem(row, 0, label);
        table->setItem(row, 1, new QTableWidgetItem(resolution)); table->setItem(row, 3, new QTableWidgetItem);
        for (int col : {0, 1, 3}) table->item(row, col)->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        auto value = new QDoubleSpinBox; value->setObjectName(QString("parameterLevel%1").arg(key)); value->setRange(0, 100); value->setDecimals(decimals); value->setSingleStep(0.1); value->setMinimumWidth(110);
        enableNumberDrag(value); value->setStatusTip(engine.controlLabel(key) + " · drag horizontally, Shift for fine control, click to type"); table->setCellWidget(row, 2, value);
        connect(value, &QDoubleSpinBox::valueChanged, this, [this, key](double level) { engine.manual[key] = {level, 0}; this->onLiveChange(); refresh(); });
    }
    refresh();
}
void ParameterPanel::refresh() {
    if (!isVisible()) return;
    auto values = engine.output(false, false);
    for (int row = 0; row < table->rowCount(); ++row) {
        int key = table->item(row, 0)->data(Qt::UserRole).toInt(); auto spin = qobject_cast<QDoubleSpinBox *>(table->cellWidget(row, 2));
        if (!spin->hasFocus()) { QSignalBlocker block(spin); spin->setValue(values.value(key, engine.controlDefault(key))); }
        table->item(row, 3)->setText(engine.manual.contains(key) ? "Manual" : "Playback / home");
    }
}
