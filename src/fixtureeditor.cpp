#include "fixtureeditor.h"
#include "tabledelegate.h"
using namespace Flame;

FixtureEditor::FixtureEditor(FixtureProfile source, QWidget *parent) : QDialog(parent), draft(std::move(source)) {
    setWindowTitle("Fixture definition"); setObjectName("fixtureEditor"); resize(980, 650);
    if (draft.modes.isEmpty()) { draft = genericFixtures().value("dimmer8"); draft.manufacturer = "Custom"; draft.name = "New fixture"; }
    draft.source.clear(); draft.oflSource.clear();
    auto layout = new QVBoxLayout(this);
    auto fields = new QFormLayout;
    manufacturer = new QLineEdit(draft.manufacturer); manufacturer->setObjectName("fixtureManufacturer");
    name = new QLineEdit(draft.name); name->setObjectName("fixtureName");
    fields->addRow("Manufacturer", manufacturer); fields->addRow("Fixture name", name); layout->addLayout(fields);
    auto body = new QHBoxLayout;
    auto modePanel = new QVBoxLayout;
    modePanel->addWidget(new QLabel("DMX MODES"));
    modes = new QListWidget; modes->setObjectName("fixtureModes"); modes->setMaximumWidth(190); modePanel->addWidget(modes, 1);
    auto modeActions = new QHBoxLayout;
    auto addMode = new QPushButton("+"); addMode->setObjectName("addFixtureMode"); addMode->setStatusTip("Add a new DMX mode with a single Dimmer parameter.");
    auto copyMode = new QPushButton("Copy"); copyMode->setObjectName("copyFixtureMode");
    auto removeMode = new QPushButton("−"); removeMode->setObjectName("removeFixtureMode");
    addMode->setFixedWidth(36); removeMode->setFixedWidth(36);
    for (auto button : {addMode, copyMode, removeMode}) modeActions->addWidget(button);
    modePanel->addLayout(modeActions); body->addLayout(modePanel);
    auto detail = new QVBoxLayout;
    modeName = new QLineEdit; modeName->setObjectName("fixtureModeName");
    auto modeHeading = new QFormLayout; modeHeading->addRow("Mode name", modeName); detail->addLayout(modeHeading);
    footprint = new QLabel; footprint->setObjectName("fixtureFootprint"); detail->addWidget(footprint);
    parameters = new QTableWidget(0, 6); parameters->setObjectName("fixtureParameters");
    parameters->setHorizontalHeaderLabels({"ATTRIBUTE", "SIZE", "DMX OFFSETS", "HOME (DMX / % virtual)", "SNAP", "CELL"});
    parameters->setItemDelegate(new TableDelegate(parameters)); parameters->verticalHeader()->setDefaultSectionSize(34);
    parameters->setStyleSheet("QTableWidget::item{padding:1px 4px;} QComboBox{padding:2px 4px;} QComboBox QLineEdit{padding:0px;}");
    parameters->setSelectionBehavior(QAbstractItemView::SelectRows); parameters->setSelectionMode(QAbstractItemView::SingleSelection);
    parameters->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    parameters->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    parameters->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive); parameters->setColumnWidth(2, 140);
    parameters->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive); parameters->setColumnWidth(3, 120);
    detail->addWidget(parameters, 1);
    auto paramActions = new QHBoxLayout;
    auto add = new QPushButton("+ Parameter"); add->setObjectName("addFixtureParameter");
    auto remove = new QPushButton("− Parameter"); remove->setObjectName("removeFixtureParameter");
    paramActions->addWidget(add); paramActions->addWidget(remove); paramActions->addStretch(); detail->addLayout(paramActions);
    auto note = new QLabel("Offsets are relative to the fixture start, most significant byte first: 1 = 8-bit; 1,2 = 16-bit. "
        "Dimmer without offsets is virtual (Home 0–100%). Cell names group independent controls; blank is the master. NoFeature reserves a slot.");
    note->setWordWrap(true); detail->addWidget(note); body->addLayout(detail, 1); layout->addLayout(body, 1);
    error = new QLabel; error->setObjectName("fixtureDefinitionError"); error->setWordWrap(true); error->setStyleSheet("color:#e0c998;"); layout->addWidget(error);
    buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel); layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Save)->setText("Save definition");
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { FixtureProfile result; QString issue; if (readProfile(result, issue)) accept(); else error->setText(issue); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    for (const auto &mode : draft.modes) modes->addItem(mode.name);
    connect(modes, &QListWidget::currentRowChanged, this, [this](int index) {
        if (reading || index < 0) return;
        FixtureMode previous; QString issue;
        if (!readMode(previous, issue)) { QSignalBlocker block(modes); modes->setCurrentRow(current); error->setText(issue); return; }
        draft.modes[current] = previous; loadMode(index);
    });
    auto appendMode = [this](bool copy) {
        FixtureMode previous; QString issue; if (!readMode(previous, issue)) { error->setText(issue); return; }
        draft.modes[current] = previous;
        FixtureMode next = copy ? previous : genericFixtures().value("dimmer8").modes.first();
        QString base = copy ? previous.name + " copy" : "Mode"; next.name = base;
        auto exists = [&](QString text) { for (const auto &mode : draft.modes) if (mode.name == text) return true; return false; };
        for (int suffix = 2; exists(next.name); ++suffix) next.name = base + " " + QString::number(suffix);
        draft.modes << next;
        { QSignalBlocker block(modes); modes->addItem(next.name); }
        loadMode(draft.modes.size() - 1);
    };
    connect(addMode, &QPushButton::clicked, this, [appendMode] { appendMode(false); });
    connect(copyMode, &QPushButton::clicked, this, [appendMode] { appendMode(true); });
    connect(removeMode, &QPushButton::clicked, this, [this] {
        if (draft.modes.size() <= 1) { error->setText("Keep at least one DMX mode."); return; }
        draft.modes.removeAt(current);
        { QSignalBlocker block(modes); delete modes->takeItem(current); }
        loadMode(qMin(current, int(draft.modes.size()) - 1));
    });
    connect(add, &QPushButton::clicked, this, [this] {
        FixtureMode mode; QString issue;
        if (parameters->rowCount() && !readMode(mode, issue)) { error->setText(issue); return; }
        if (mode.footprint >= 512) { error->setText("The mode already reaches slot 512."); return; }
        addParameter({"NoFeature", {mode.footprint + 1}, 0}); validate();
    });
    connect(remove, &QPushButton::clicked, this, [this] { int row = parameters->currentRow(); if (row >= 0) { parameters->removeRow(row); validate(); } });
    connect(parameters, &QTableWidget::itemChanged, this, [this] { validate(); });
    for (auto field : {manufacturer, name, modeName}) connect(field, &QLineEdit::textChanged, this, [this] { validate(); });
    loadMode(0);
}

void FixtureEditor::addParameter(FixtureParameter parameter) {
    QSignalBlocker block(parameters);
    const int row = parameters->rowCount(); parameters->insertRow(row);
    auto attribute = new QComboBox; attribute->setEditable(true);
    attribute->addItems({"Dimmer", "Pan", "Tilt", "ColorAdd_R", "ColorAdd_G", "ColorAdd_B", "ColorAdd_W", "ColorAdd_A", "ColorAdd_UV",
        "ColorSub_C", "ColorSub_M", "ColorSub_Y", "Color1", "Gobo1", "Zoom", "Focus1", "Iris", "Shutter1", "NoFeature"});
    attribute->setCurrentText(parameter.attribute); parameters->setCellWidget(row, 0, attribute);
    auto size = new QTableWidgetItem; size->setFlags(size->flags() & ~Qt::ItemIsEditable); parameters->setItem(row, 1, size);
    QStringList offsets; for (int offset : parameter.offsets) offsets << QString::number(offset);
    parameters->setItem(row, 2, new QTableWidgetItem(offsets.join(',')));
    quint64 maximum = parameter.offsets.isEmpty() ? 100 : parameter.offsets.size() > 4 ? 255 : (quint64(1) << (8 * parameter.offsets.size())) - 1;
    parameters->setItem(row, 3, new QTableWidgetItem(QString::number(qRound64(parameter.home * double(maximum) / 100.))));
    auto snap = new QTableWidgetItem; snap->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    snap->setCheckState(parameter.snap ? Qt::Checked : Qt::Unchecked); parameters->setItem(row, 4, snap);
    parameters->setItem(row, 5, new QTableWidgetItem(parameter.cell));
    connect(attribute, &QComboBox::currentTextChanged, this, [this] { validate(); });
    parameters->selectRow(row);
}

bool FixtureEditor::readMode(FixtureMode &mode, QString &issue) const {
    if (!draft.modes[current].unsupported.isEmpty()) { issue = draft.modes[current].unsupported; return false; }
    mode = {}; mode.name = modeName->text().trimmed();
    for (int row = 0; row < parameters->rowCount(); ++row) {
        FixtureParameter parameter;
        parameter.attribute = qobject_cast<QComboBox *>(parameters->cellWidget(row, 0))->currentText().trimmed();
        if (parameter.attribute.isEmpty() || parameter.attribute.contains('.') || parameter.attribute.contains(QRegularExpression("[\\x00-\\x1f]"))) {
            issue = QString("Parameter %1: enter an attribute name without dots or control characters.").arg(row + 1); return false;
        }
        parameter.cell = parameters->item(row, 5)->text().trimmed();
        parameter.intensity = parameter.attribute == "Dimmer"; parameter.snap = parameters->item(row, 4)->checkState() == Qt::Checked;
        const auto text = parameters->item(row, 2)->text();
        if (!(text.trimmed().isEmpty() && parameter.intensity) && !QRegularExpression("^\\s*\\d+(?:\\s*,\\s*\\d+){0,3}\\s*$").match(text).hasMatch()) { issue = QString("Parameter %1: enter 1–4 comma-separated DMX offsets.").arg(row + 1); return false; }
        if (!text.trimmed().isEmpty()) for (const auto &part : text.split(',')) { bool ok; int offset = part.trimmed().toInt(&ok);
            if (!ok || offset < 1 || offset > 512) { issue = QString("Parameter %1: offsets must be between 1 and 512.").arg(row + 1); return false; }
            parameter.offsets << offset; mode.footprint = qMax(mode.footprint, offset);
        }
        bool ok; const auto home = parameters->item(row, 3)->text().trimmed().toULongLong(&ok);
        const quint64 maximum = parameter.offsets.isEmpty() ? 100 : (quint64(1) << (8 * parameter.offsets.size())) - 1;
        if (!ok || home > maximum) { issue = QString("Parameter %1: Home must be between 0 and %2.").arg(row + 1).arg(maximum); return false; }
        parameter.home = double(home) * 100. / double(maximum);
        mode.parameters << parameter; mode.attributes << parameter.attribute;
    }
    return validateMode(mode, issue);
}
bool FixtureEditor::readProfile(FixtureProfile &profile, QString &issue) const {
    profile = draft; profile.manufacturer = manufacturer->text().trimmed(); profile.name = name->text().trimmed(); profile.source.clear(); profile.oflSource.clear();
    if (profile.manufacturer.isEmpty() || profile.name.isEmpty()) { issue = "Enter a manufacturer and fixture name."; return false; }
    if (!readMode(profile.modes[current], issue)) return false;
    for (auto &mode : profile.modes) ensureCellControls(mode);
    QSet<QString> names;
    for (const auto &mode : profile.modes) {
        if (!validateMode(mode, issue)) return false;
        if (names.contains(mode.name.toCaseFolded())) { issue = "Use a different name for each mode."; return false; }
        names.insert(mode.name.toCaseFolded());
    }
    return true;
}
void FixtureEditor::loadMode(int index) {
    reading = true; current = index;
    { QSignalBlocker block(modes); modes->setCurrentRow(index); }
    modeName->setText(draft.modes[index].name); parameters->setRowCount(0);
    for (const auto &parameter : draft.modes[index].parameters) addParameter(parameter);
    reading = false; validate();
}
void FixtureEditor::validate() {
    if (reading) return;
    FixtureProfile result; QString issue; const bool valid = readProfile(result, issue);
    error->setText(issue); buttons->button(QDialogButtonBox::Save)->setEnabled(valid);
    { QSignalBlocker block(modes); modes->item(current)->setText(modeName->text()); }
    FixtureMode mode; QString unused; const bool modeValid = readMode(mode, unused);
    footprint->setText(modeValid ? QString("%1 DMX slots · %2 parameters").arg(mode.footprint).arg(mode.parameters.size()) : "Check DMX offsets and home values below");
    QSignalBlocker block(parameters);
    for (int row = 0; row < parameters->rowCount(); ++row) {
        const int bytes = parameters->item(row, 2)->text().split(',').size();
        parameters->item(row, 1)->setText(parameters->item(row, 2)->text().trimmed().isEmpty() ? "Virtual" : bytes >= 1 && bytes <= 4 ? QString::number(bytes * 8) + " bit" : "—");
    }
}
FixtureProfile FixtureEditor::profile() const { FixtureProfile result; QString issue; return readProfile(result, issue) ? result : FixtureProfile{}; }
