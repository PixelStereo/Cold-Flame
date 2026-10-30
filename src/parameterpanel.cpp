#include "parameterpanel.h"
#include "numberdrag.h"
#include "fixturecolor.h"
using namespace Flame;
ParameterPanel::ParameterPanel(Engine &e, std::function<void()> onLiveChange, QWidget *parent)
    : QWidget(parent), engine(e), onLiveChange(std::move(onLiveChange)) {
    auto layout = new QVBoxLayout(this); channel = new QComboBox; channel->setObjectName("parameterChannel"); layout->addWidget(channel);
    description = new QLabel; description->setWordWrap(true); layout->addWidget(description);
    auto scope = new QHBoxLayout;
    cell = new QComboBox; cell->setObjectName("parameterCell"); scope->addWidget(cell, 1);
    color = new QPushButton("Choose color…"); color->setObjectName("parameterCellColor"); scope->addWidget(color); layout->addLayout(scope);
    cell->setStatusTip("Select the whole fixture, its master, or an individual numbered cell/group.");
    color->setStatusTip("Choose a color for the selected scope, preserving its intensity. Cancel restores previous values.");
    masterLevel = new QLabel; masterLevel->setObjectName("parameterMasterLevel"); layout->addWidget(masterLevel);
    connect(cell, &QComboBox::currentIndexChanged, this, [this] { populate(shown); });
    connect(color, &QPushButton::clicked, this, [this] {
        const QString part = cell->currentData().toString(); const auto controls = engine.colorControls(shown, part);
        if (!controls.available()) return;
        const auto previous = engine.manual;
        QColorDialog dialog(fixtureColor(engine, shown, nullptr, part), this);
        dialog.setObjectName("parameterColorDialog"); dialog.setOption(QColorDialog::DontUseNativeDialog);
        dialog.setWindowTitle(part == "*" ? "Whole fixture color" : "Cell color · " + part);
        connect(&dialog, &QColorDialog::currentColorChanged, &dialog, [this, part](QColor value) {
            setFixtureColor(engine, shown, value, false, part); this->onLiveChange(); refresh();
        });
        const bool accepted = dialog.exec() == QDialog::Accepted;
        for (int key : controls.keys()) {
            if (previous.contains(key)) engine.manual[key] = previous.value(key); else engine.manual.remove(key);
        }
        if (accepted) {
            setFixtureColor(engine, shown, dialog.currentColor(), true, part);
            if (paletteChanged) paletteChanged();
        }
        this->onLiveChange(); refresh();
    });
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
        for (int key : engine.controlIds()) if (key < 0 && engine.channelForControl(key) == shown) engine.manual[key] = {engine.controlDefault(key), 0, engine.colorComponent(key, 0) >= 0 ? 0 : -1};
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
    const QString previous = cell->currentData().toString();
    const auto parts = engine.fixtureCells(id);
    { QSignalBlocker block(cell); cell->clear(); cell->addItem("Master + all cells", "*"); cell->addItem("Master only", "");
        for (int i = 0; i < parts.size(); ++i) cell->addItem(QString("%1 · %2").arg(i + 1).arg(parts[i]), parts[i]);
        cell->setCurrentIndex(qMax(0, cell->findData(previous)));
    }
    cell->setVisible(!parts.isEmpty());
    masterLevel->setVisible(!parts.isEmpty());
    const QString part = cell->currentData().toString(); color->setEnabled(engine.colorControls(id, part).available());
    table->setRowCount(0); QList<int> keys;
    if (part == "*" || part.isEmpty()) keys << id;
    for (int i = 0; i < mode.parameters.size(); ++i)
        if (engine.parameterKey(id, i) != id && (part == "*" || mode.parameters[i].cell == part)) keys << engine.parameterKey(id, i);
    for (int key : keys) {
        int row = table->rowCount(); table->insertRow(row); QString resolution = "Virtual"; int decimals = 2;
        for (int i = 0; i < mode.parameters.size(); ++i) if (engine.parameterKey(id, i) == key) { resolution = mode.parameters[i].offsets.isEmpty() ? "Virtual" : QString::number(mode.parameters[i].offsets.size() * 8) + " bit"; decimals = qMax(2, int(mode.parameters[i].offsets.size()) * 2); }
        QString text = "Intensity";
        if (key < 0) {
            const auto &parameter = mode.parameters[(-qint64(key) - 1) % 512];
            text = (part == "*" && !parameter.cell.isEmpty() ? parameter.cell + " / " : QString()) + parameter.attribute;
        }
        auto label = new QTableWidgetItem(text); label->setData(Qt::UserRole, key); label->setToolTip(engine.controlLabel(key)); table->setItem(row, 0, label);
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
    masterLevel->setText(QString("Fixture master: %1% · Cell intensities are relative").arg(values.value(shown), 0, 'f', 1));
    for (int row = 0; row < table->rowCount(); ++row) {
        int key = table->item(row, 0)->data(Qt::UserRole).toInt(); auto spin = qobject_cast<QDoubleSpinBox *>(table->cellWidget(row, 2));
        if (!spin->hasFocus()) { QSignalBlocker block(spin); spin->setValue(values.value(key, engine.controlDefault(key))); }
        const auto reference = engine.recordedValue(key, values);
        QString source = engine.manual.contains(key) ? "Manual" : "Playback / home";
        if (reference.colorPalette > 0) source = QString("CP %1 · ").arg(reference.colorPalette) + source;
        table->item(row, 3)->setText(source);
    }
}
