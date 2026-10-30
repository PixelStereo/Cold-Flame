#include "parameterpanel.h"
#include "fixturecolor.h"
using namespace Flame;
namespace {
// Keep the editor independent of native spin-button geometry, including macOS's
// NoButtons layout. The text area must occupy the visible field, not a native inset.
class ParameterNumber : public QDoubleSpinBox {
    void fitText() {
        lineEdit()->setGeometry(rect().adjusted(6, 2, -6, -2));
    }
  public:
    ParameterNumber() {
        setProperty("nativeNumberEditing", true); setButtonSymbols(QAbstractSpinBox::NoButtons);
        setStyleSheet("QDoubleSpinBox{padding:0px;min-height:30px;border:1px solid #393c3f;border-radius:3px;background:#222426;color:#e5e5e5;}");
        lineEdit()->setStyleSheet("QLineEdit{padding:0px;margin:0px;border:0px;min-height:0px;background:transparent;color:#e5e5e5;selection-background-color:#3c665e;selection-color:#ffffff;}");
        lineEdit()->setTextMargins(0, 0, 0, 0);
    }
  protected:
    bool event(QEvent *event) override {
        const bool result = QDoubleSpinBox::event(event);
        switch (event->type()) {
        case QEvent::Resize: case QEvent::Show: case QEvent::Polish:
        case QEvent::StyleChange: case QEvent::FontChange: case QEvent::LayoutRequest:
            fitText(); break;
        default: break;
        }
        return result;
    }
};
// Let the number field use native focus/text input. The grip alone handles dragging.
class ParameterGrip : public QWidget {
    QDoubleSpinBox *field;
    bool pressed = false;
    double previous = 0, accumulated = 0;
    void adjust(QMouseEvent *event) {
        const double factor = event->modifiers() & Qt::ShiftModifier ? 0.1 : event->modifiers() & Qt::ControlModifier ? 10. : 1.;
        const double position = event->globalPosition().x();
        accumulated = qBound(field->minimum(), accumulated + (position - previous) * field->singleStep() * factor, field->maximum());
        previous = position; field->setValue(accumulated); event->accept();
    }
  public:
    ParameterGrip(QDoubleSpinBox *field, QWidget *parent) : QWidget(parent), field(field) {
        setFixedWidth(20); setCursor(Qt::SizeHorCursor); setFocusPolicy(Qt::NoFocus);
        setAccessibleName("Drag parameter value");
        setStatusTip("Drag sideways to adjust the value. Shift: fine; Ctrl/Command: coarse. Click the number to type, then press Enter.");
    }
  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this); painter.setPen(Qt::NoPen); painter.setBrush(palette().color(QPalette::Text));
        painter.setOpacity(0.55);
        for (int x : {7, 13}) for (int dy : {-5, 0, 5}) painter.drawEllipse(QPointF(x, height() / 2. + dy), 1., 1.);
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() != Qt::LeftButton || !field->isEnabled() || field->isReadOnly()) { event->ignore(); return; }
        field->interpretText(); field->clearFocus();
        pressed = true; previous = event->globalPosition().x(); accumulated = field->value(); event->accept();
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (!pressed || !(event->buttons() & Qt::LeftButton)) { pressed = false; event->ignore(); return; }
        adjust(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() != Qt::LeftButton || !pressed) { event->ignore(); return; }
        adjust(event); pressed = false; event->accept();
    }
    bool event(QEvent *event) override {
        if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::WindowDeactivate || event->type() == QEvent::Hide) pressed = false;
        return QWidget::event(event);
    }
};
QString parameterName(const FixtureParameter &parameter) {
    if (parameter.intensity) return parameter.cell.isEmpty() ? "Intensity" : "Cell intensity";
    static const QMap<QString, QString> names{{"ColorAdd_R", "Red"}, {"ColorAdd_G", "Green"}, {"ColorAdd_B", "Blue"}, {"ColorAdd_W", "White"},
        {"ColorAdd_WW", "Warm white"}, {"ColorAdd_CW", "Cool white"}, {"ColorAdd_A", "Amber"}, {"ColorAdd_UV", "UV"},
        {"ColorSub_C", "Cyan"}, {"ColorSub_M", "Magenta"}, {"ColorSub_Y", "Yellow"}, {"Shutter1", "Shutter"}, {"Gobo1", "Gobo"}, {"Focus1", "Focus"}};
    return names.value(parameter.attribute, parameter.attribute);
}
}
ParameterPanel::ParameterPanel(Engine &e, std::function<void()> onLiveChange, QWidget *parent)
    : QWidget(parent), engine(e), onLiveChange(std::move(onLiveChange)) {
    auto layout = new QVBoxLayout(this); channel = new QComboBox; channel->setObjectName("parameterChannel"); layout->addWidget(channel);
    description = new QLabel; description->setTextFormat(Qt::PlainText); description->setWordWrap(true); layout->addWidget(description);
    auto scope = new QHBoxLayout;
    cell = new QComboBox; cell->setObjectName("parameterCell"); scope->addWidget(cell, 1);
    color = new QPushButton("Choose color…"); color->setObjectName("parameterCellColor"); scope->addWidget(color); layout->addLayout(scope);
    cell->setStatusTip("Select the whole fixture, its master, or an individual numbered cell/group.");
    color->setStatusTip("Choose a color for the selected scope, preserving its intensity. Cancel restores previous values.");
    masterLevel = new QLabel; masterLevel->setObjectName("parameterMasterLevel"); masterLevel->setTextFormat(Qt::PlainText); masterLevel->setWordWrap(true); layout->addWidget(masterLevel);
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
    auto note = new QLabel("Click a value, type 0–100 and press Enter. Drag the dotted grip to adjust live. Intensity controls brightness; other values are DMX percentages. Record / Update saves the look to a cue."); note->setWordWrap(true); layout->addWidget(note);
    table = new QTableWidget(0, 4); table->setObjectName("parameterTable"); table->setHorizontalHeaderLabels({"Control", "Resolution", "Value (%)", "Source"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setStyleSheet("QWidget#parameterValueCell{background:transparent;}");
    table->verticalHeader()->hide(); table->verticalHeader()->setDefaultSectionSize(40);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    // Live values/source labels must not move the input fields under the pointer.
    for (int col : {2, 3}) table->horizontalHeader()->setSectionResizeMode(col, QHeaderView::Interactive);
    table->setColumnWidth(2, 142); table->setColumnWidth(3, 130);
    layout->addWidget(table, 1); auto actions = new QHBoxLayout;
    auto release = new QPushButton("Release manual"); auto home = new QPushButton("Home parameters"); actions->addWidget(release); actions->addWidget(home); actions->addStretch(); layout->addLayout(actions);
    release->setObjectName("releaseFixtureParameters");
    release->setStatusTip("Release this fixture's manual values, retaining captured controls."); home->setStatusTip("Restore color, position, beam and cell controls to home, keeping master intensity.");
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
    masterLevel->setVisible(true);
    const QString part = cell->currentData().toString(); color->setEnabled(engine.colorControls(id, part).available());
    table->setRowCount(0); QList<int> keys;
    if (part == "*" || part.isEmpty()) keys << id;
    for (int i = 0; i < mode.parameters.size(); ++i)
        if (engine.parameterKey(id, i) != id && (part == "*" || mode.parameters[i].cell == part)) keys << engine.parameterKey(id, i);
    for (int key : keys) {
        int row = table->rowCount(); table->insertRow(row); QString resolution = "Virtual"; int decimals = 2;
        for (int i = 0; i < mode.parameters.size(); ++i) if (engine.parameterKey(id, i) == key) { resolution = mode.parameters[i].offsets.isEmpty() ? "Virtual" : QString::number(mode.parameters[i].offsets.size() * 8) + " bit"; decimals = qMax(2, int(mode.parameters[i].offsets.size()) * 2); }
        QString text = parts.isEmpty() ? "Intensity" : "Master intensity";
        if (key < 0) {
            const auto &parameter = mode.parameters[(-qint64(key) - 1) % 512];
            text = (part == "*" && !parameter.cell.isEmpty() ? parameter.cell + " / " : QString()) + parameterName(parameter);
        }
        auto label = new QTableWidgetItem(text); label->setData(Qt::UserRole, key); label->setToolTip(engine.controlLabel(key)); table->setItem(row, 0, label);
        table->setItem(row, 1, new QTableWidgetItem(resolution)); table->setItem(row, 3, new QTableWidgetItem);
        for (int col : {0, 1, 3}) table->item(row, col)->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        auto holder = new QWidget; holder->setObjectName("parameterValueCell");
        auto valueLayout = new QHBoxLayout(holder); valueLayout->setContentsMargins(2, 2, 2, 2); valueLayout->setSpacing(3);
        auto value = new ParameterNumber;
        value->setObjectName(QString("parameterLevel%1").arg(key)); value->setRange(0, 100); value->setDecimals(decimals); value->setSingleStep(0.1); value->setMinimumWidth(106);
        value->setKeyboardTracking(false); value->setFocusPolicy(Qt::StrongFocus); value->setAlignment(Qt::AlignRight);
        value->setAccessibleName(engine.controlLabel(key) + " value, percent");
        value->setStatusTip(engine.controlLabel(key) + " · click to type 0–100, then press Enter. Drag the dotted grip to adjust live.");
        auto grip = new ParameterGrip(value, holder); grip->setObjectName(QString("parameterGrip%1").arg(key));
        valueLayout->addWidget(grip); valueLayout->addWidget(value, 1); table->setCellWidget(row, 2, holder);
        connect(value, &QDoubleSpinBox::valueChanged, this, [this, key](double level) { engine.manual[key] = {level, 0}; this->onLiveChange(); refresh(); });
    }
    refresh();
}
void ParameterPanel::refresh() {
    if (!isVisible()) return;
    auto values = engine.output(false, false, false);
    const auto output = engine.output(); QStringList states;
    if (engine.parked.contains(shown)) states << "Parked";
    else {
        if (engine.blackout) states << "Blackout";
        if (engine.master < 1) states << QString("Grand master %1%").arg(engine.master * 100., 0, 'f', 0);
        for (const auto &sub : engine.submasters) if (sub.mode == Submaster::Mode::Inhibitive && sub.level < 1 && sub.values.contains(shown))
            states << QString("Inhibited by Sub %1").arg(sub.number);
    }
    if (values.value(shown) == 0 && engine.colorControls(shown).available()) states << "Raise intensity to see color changes";
    if (!engine.fixtureCells(shown).isEmpty()) states << "Cell intensity is relative to the master";
    masterLevel->setText(QString("Intensity %1% · Output %2%%3").arg(values.value(shown), 0, 'f', 1).arg(output.value(shown), 0, 'f', 1)
        .arg(states.isEmpty() ? QString() : " · " + states.join(" · ")));
    for (int row = 0; row < table->rowCount(); ++row) {
        int key = table->item(row, 0)->data(Qt::UserRole).toInt(); auto spin = table->cellWidget(row, 2)->findChild<QDoubleSpinBox *>();
        const auto edit = spin->findChild<QLineEdit *>();
        if (!spin->hasFocus() && (!edit || !edit->hasFocus())) { QSignalBlocker block(spin); spin->setValue(values.value(key, engine.controlDefault(key))); }
        const auto reference = engine.recordedValue(key, values);
        QString source = engine.manual.contains(key) ? "Manual" : "Playback / home";
        if (reference.colorPalette > 0) source = QString("CP %1 · ").arg(reference.colorPalette) + source;
        table->item(row, 3)->setText(source); table->item(row, 3)->setToolTip(source);
    }
}
