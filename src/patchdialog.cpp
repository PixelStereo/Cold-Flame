#include "tabledelegate.h"
#include "patchdialog.h"
using namespace Flame;
static QString targetText(int absolute) {
    return absolute > 0 ? QString("%1.%2").arg((absolute - 1) / 512 + 1).arg((absolute - 1) % 512 + 1, 3, 10, QChar('0')) : QString();
}
static FixtureAssignment assignment(QComboBox *combo) {
    return {combo->currentData().toString(), combo->currentData(Qt::UserRole + 1).toInt()};
}
PatchDialog::PatchDialog(Engine &e, QWidget *parent, bool embedded, std::function<void()> onApply)
    : QDialog(parent), engine(e), embedded(embedded), onApply(std::move(onApply)), profiles(e.fixtureProfiles) {
    if (embedded) setWindowFlags(Qt::Widget);
    setWindowTitle("Patch · Fixtures and addresses"); setObjectName("patchDialog"); resize(1340, 1000);
    auto layout = new QVBoxLayout(this); layout->setContentsMargins(embedded ? 8 : 24, 8, embedded ? 8 : 24, 8); layout->setSpacing(8);
    summary = new QLabel; summary->setWordWrap(true); layout->addWidget(summary);
    auto toolbar = new QHBoxLayout; toolbar->setSpacing(8);
    filter = new QLineEdit; filter->setPlaceholderText("Filter channel, label, fixture or address…"); toolbar->addWidget(filter, 1);
    auto batchToggle = new QPushButton("Batch…"); toolbar->addWidget(batchToggle);
    auto more = new QPushButton("Patch tools ▾"); more->setObjectName("patchToolsButton"); toolbar->addWidget(more);
    auto menu = new QMenu(more);
    menu->addAction("Import GDTF profile…", this, [this] { importProfile(); });
    menu->addAction("Export selected fixture as GDTF…", this, [this] { exportProfile(); }); menu->addSeparator();
    menu->addAction("Import patch…", this, [this] { importPatch(); });
    menu->addAction("Export applied patch…", this, [this] { exportPatch(); });
    if (embedded) {
        menu->addSeparator(); menu->addAction("Full view…", this, [this] {
            PatchDialog dialog(engine, this);
            if (dialog.exec() == QDialog::Accepted) { reload(); if (this->onApply) this->onApply(); }
        });
    }
    more->setMenu(menu); more->setStatusTip("Import and export GDTF fixture profiles or transfer an applied patch."); layout->addLayout(toolbar);
    auto table = new QTableWidget(0, 5); banks << table; table->setItemDelegate(new TableDelegate(table));
    table->setObjectName("patchBank0"); table->setHorizontalHeaderLabels({"CH", "Label", "DMX start addresses", "Fixture / mode", "Output"});
    table->verticalHeader()->hide(); table->verticalHeader()->setMinimumSectionSize(28); table->verticalHeader()->setDefaultSectionSize(30);
    table->setAlternatingRowColors(true); table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive); table->setColumnWidth(1, embedded ? 100 : 180);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive); table->setColumnWidth(3, embedded ? 170 : 280);
    if (embedded) table->hideColumn(4);
    table->setStyleSheet("QTableWidget::item{padding:1px 4px;} QComboBox{padding:3px 5px;border-radius:0;}");
    layout->addWidget(table, 1);
    auto note = new QLabel("Each address is a fixture start: 22.013, 3.018. Blank = unpatched. The entire footprint is reserved; fixtures cannot cross universes.");
    note->setWordWrap(true); layout->addWidget(note);
    auto batchWidget = new QWidget; auto batch = new QGridLayout(batchWidget); batch->setContentsMargins(0, 4, 0, 4);
    auto spin = [&](QString label, int lo, int hi, int value, int row, int col) {
        batch->addWidget(new QLabel(label), row, col); auto field = new QSpinBox;
        field->setRange(lo, hi); field->setValue(value); batch->addWidget(field, row, col + 1); return field;
    };
    first = spin("First channel", 1, 10000, e.channels.isEmpty() ? 1 : e.channels.lastKey() + 1, 0, 0);
    count = spin("Count", 1, 10000, 1, 0, 2);
    universe = spin("Universe", 1, 63999, 1, 1, 0); address = spin("Address", 1, 512, 1, 1, 2);
    batchFixture = new QComboBox; batchFixture->setObjectName("batchFixtureMode"); fillModes(batchFixture);
    batch->addWidget(new QLabel("Fixture / mode"), 2, 0); batch->addWidget(batchFixture, 2, 1, 1, 3);
    auto add = new QPushButton("Add / repatch range"); batch->addWidget(add, 3, 0, 1, 4); layout->addWidget(batchWidget);
    batchWidget->setVisible(!embedded); connect(batchToggle, &QPushButton::clicked, this, [batchWidget] { batchWidget->setVisible(!batchWidget->isVisible()); });
    error = new QLabel; error->setObjectName("patchError"); error->setStyleSheet("color:#e0c998;"); error->setWordWrap(true); layout->addWidget(error);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel); layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Save)->setText("Apply patch"); buttons->button(QDialogButtonBox::Save)->setObjectName("applyPatchButton");
    if (embedded) buttons->button(QDialogButtonBox::Cancel)->setText("Revert draft");
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (apply()) { if (this->onApply) this->onApply(); if (!this->embedded) accept(); }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, [this] { if (this->embedded) reload(); else reject(); });
    connect(table, &QTableWidget::itemChanged, this, [this] { validate(); });
    connect(filter, &QLineEdit::textChanged, this, [this](QString text) {
        auto t = banks[0];
        for (int r = 0; r < t->rowCount(); ++r) {
            QString haystack; for (int c = 0; c < 3; ++c) haystack += t->item(r, c)->text() + ' ';
            haystack += qobject_cast<QComboBox *>(t->cellWidget(r, 3))->currentText();
            t->setRowHidden(r, !haystack.contains(text, Qt::CaseInsensitive));
        }
    });
    connect(add, &QPushButton::clicked, this, [this] {
        if (first->value() + count->value() - 1 > 10000) { error->setText("Channel range exceeds 10000"); return; }
        const auto selected = assignment(batchFixture); auto mode = profiles.value(selected.profile).modes.value(selected.mode);
        QString issue; if (!validateMode(mode, issue)) { error->setText(issue); return; }
        int absolute = (universe->value() - 1) * 512 + address->value(); QList<int> starts;
        for (int i = 0; i < count->value(); ++i) {
            if ((absolute - 1) % 512 + mode.footprint > 512) absolute += 512 - ((absolute - 1) % 512);
            if (absolute + mode.footprint - 1 > 63999 * 512) { error->setText("Range exceeds universe 63999"); return; }
            starts << absolute; absolute += mode.footprint;
        }
        auto t = banks[0];
        for (int i = 0; i < starts.size(); ++i) {
            int id = first->value() + i, row = -1;
            for (int r = 0; r < t->rowCount(); ++r) if (t->item(r, 0)->text().toInt() == id) row = r;
            if (row < 0) appendRow(t, id, QString("Channel %1").arg(id), starts[i], selected);
            else { QSignalBlocker block(t); t->item(row, 2)->setText(targetText(starts[i])); fillModes(qobject_cast<QComboBox *>(t->cellWidget(row, 3)), selected); }
        }
        validate();
    });
    populate(); validate();
}
void PatchDialog::fillModes(QComboBox *combo, FixtureAssignment selected) {
    QSignalBlocker blocker(combo); combo->clear(); int current = 0;
    for (auto it = profiles.begin(); it != profiles.end(); ++it) for (int m = 0; m < it->modes.size(); ++m) {
        const auto &mode = it->modes[m]; const int row = combo->count();
        QString label = it->name + (it->manufacturer == "Generic" && mode.name == "Standard" ? "" : " / " + mode.name);
        combo->addItem(QString("%1 · %2ch%3").arg(label).arg(mode.footprint).arg(mode.unsupported.isEmpty() ? "" : " · unsupported"), it.key());
        combo->setItemData(row, m, Qt::UserRole + 1); combo->setItemData(row, mode.unsupported, Qt::ToolTipRole);
        if (selected.profile == it.key() && selected.mode == m) current = row;
    }
    combo->setCurrentIndex(current);
}
void PatchDialog::appendRow(QTableWidget *t, int id, QString name, int absolute, FixtureAssignment mode, QList<int> extra) {
    QSignalBlocker block(t); int r = t->rowCount(); t->insertRow(r);
    QStringList targets; if (absolute > 0) targets << targetText(absolute); for (int a : extra) targets << targetText(a);
    QStringList fields{QString::number(id), name, targets.join(", "), "", QString::number(engine.output().value(id), 'f', 0) + " %"};
    for (int c = 0; c < fields.size(); ++c) { auto item = new QTableWidgetItem(fields[c]); if (c == 0 || c >= 3) item->setFlags(item->flags() & ~Qt::ItemIsEditable); t->setItem(r, c, item); }
    auto combo = new QComboBox; combo->setObjectName(QString("fixtureMode%1").arg(id)); fillModes(combo, mode); t->setCellWidget(r, 3, combo);
    connect(combo, &QComboBox::currentIndexChanged, this, [this] { validate(); });
}
void PatchDialog::populate() {
    for (auto i = engine.channels.begin(); i != engine.channels.end(); ++i)
        appendRow(banks[0], i.key(), i.value(), engine.patch.value(i.key()), engine.fixtures.value(i.key()), engine.additionalPatch.value(i.key()));
}
void PatchDialog::reload() {
    profiles = engine.fixtureProfiles; fillModes(batchFixture);
    { QSignalBlocker blocker(banks[0]); banks[0]->setRowCount(0); }
    filter->clear(); populate(); validate();
}
bool PatchDialog::buildDraft(Engine &draft, QString &issue) const {
    draft = engine; draft.fixtureProfiles = profiles; draft.patch.clear(); draft.additionalPatch.clear();
    QSet<int> ids; auto t = banks[0];
    for (int r = 0; r < t->rowCount(); ++r) {
        int id = t->item(r, 0)->text().toInt();
        if (id < 1 || id > 10000 || ids.contains(id)) { issue = "Invalid or duplicate channel ID"; return false; } ids.insert(id);
        const auto mode = assignment(qobject_cast<QComboBox *>(t->cellWidget(r, 3)));
        if (!engine.canChangeFixture(id, mode)) { issue = QString("Channel %1 has stored or live fixture parameter data. Repatch its addresses or use a new channel for another mode.").arg(id); return false; }
        draft.channels[id] = t->item(r, 1)->text(); draft.fixtures[id] = mode;
        const QString text = t->item(r, 2)->text().trimmed(); if (text.isEmpty()) continue;
        QList<int> addresses;
        for (const auto &token : text.split(',', Qt::KeepEmptyParts)) {
            const auto m = QRegularExpression("^\\s*(\\d+)\\.(\\d+)\\s*$").match(token);
            const int u = m.captured(1).toInt(), a = m.captured(2).toInt();
            if (!m.hasMatch() || u < 1 || u > 63999 || a < 1 || a > 512) { issue = QString("Channel %1: use universe.address lists, or leave blank to unpatch").arg(id); return false; }
            addresses << ((u - 1) * 512 + a);
        }
        draft.patch[id] = addresses.takeFirst(); draft.additionalPatch[id] = addresses;
    }
    for (int id : engine.channels.keys()) if (!ids.contains(id)) { issue = "Existing channel IDs must be preserved"; return false; }
    return true;
}
void PatchDialog::validate() {
    Engine draft; QString issue; bool parsed = buildDraft(draft, issue);
    const auto issues = parsed ? draft.patchErrors() : QMap<int, QString>{};
    QSet<int> universes; int slotCount = 0, patched = 0; auto table = banks[0];
    for (int channel : draft.channels.keys()) {
        if (!draft.dmxAddresses(channel).isEmpty()) ++patched;
        for (int a : draft.occupiedAddresses(channel)) { ++slotCount; universes.insert((a - 1) / 512 + 1); }
    }
    { QSignalBlocker block(table);
        for (int r = 0; r < table->rowCount(); ++r) {
            int id = table->item(r, 0)->text().toInt();
            for (int c = 0; c < 3; ++c) table->item(r, c)->setBackground(issues.contains(id) ? QBrush(QColor("#443c2c")) : QBrush());
        }
    }
    if (parsed && !issues.isEmpty()) issue = issues.first();
    summary->setText(QString("%1 CHANNELS · %2 PATCHED · %3 DMX SLOTS · %4 UNIVERSES").arg(table->rowCount()).arg(patched).arg(slotCount).arg(universes.size()));
    error->setText(issue); error->setProperty("valid", parsed && issues.isEmpty());
    findChild<QPushButton *>("applyPatchButton")->setEnabled(parsed && issues.isEmpty());
}
bool PatchDialog::apply() {
    validate(); if (!error->property("valid").toBool()) return false;
    Engine draft; QString issue; if (!buildDraft(draft, issue)) return false;
    engine.channels = draft.channels; engine.patch = draft.patch; engine.additionalPatch = draft.additionalPatch;
    engine.fixtures = draft.fixtures; engine.fixtureProfiles = profiles; return true;
}
void PatchDialog::importProfile() {
    QString path = QFileDialog::getOpenFileName(this, "Import fixture profile", {}, "GDTF (*.gdtf)"); if (path.isEmpty()) return;
    QFile file(path); FixtureProfile profile; QString issue;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024) { error->setText("Cannot read profile or file is too large"); return; }
    if (!readGdtf(file.readAll(), profile, issue)) { error->setText(issue); return; }
    profiles[QUuid::createUuid().toString(QUuid::WithoutBraces)] = profile;
    auto t = banks[0];
    for (int r = 0; r < t->rowCount(); ++r) { auto combo = qobject_cast<QComboBox *>(t->cellWidget(r, 3)); fillModes(combo, assignment(combo)); }
    fillModes(batchFixture, assignment(batchFixture)); validate();
    error->setText("Imported " + profile.manufacturer + " / " + profile.name + ". Choose a mode in the patch or Batch. Apply patch to save the profile library.");
}
void PatchDialog::exportProfile() {
    auto t = banks[0]; int row = t->currentRow(); if (row < 0) { error->setText("Select a fixture row to export its profile"); return; }
    const auto profile = profiles.value(assignment(qobject_cast<QComboBox *>(t->cellWidget(row, 3))).profile);
    QString path = QFileDialog::getSaveFileName(this, "Export fixture profile", profile.name + ".gdtf", "GDTF (*.gdtf)"); if (path.isEmpty()) return;
    QSaveFile file(path); if (!file.open(QIODevice::WriteOnly)) { error->setText(file.errorString()); return; }
    const auto bytes = writeGdtf(profile);
    if (file.write(bytes) != bytes.size() || !file.commit()) error->setText(file.errorString()); else error->setText("GDTF profile exported");
}
void PatchDialog::exportPatch() {
    QString path = QFileDialog::getSaveFileName(this, "Export applied patch", "patch.cfp", "Cold Flame patch (*.cfp)"); if (path.isEmpty()) return;
    auto json = engine.json(); QJsonObject patch{{"format", "cold-flame-patch"}, {"version", 1}, {"channels", json["channels"]}, {"fixtureProfiles", json["fixtureProfiles"]}};
    QSaveFile file(path); auto bytes = QJsonDocument(patch).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) error->setText(file.errorString()); else error->setText("Applied patch exported");
}
void PatchDialog::importPatch() {
    QString path = QFileDialog::getOpenFileName(this, "Import patch", {}, "Cold Flame patch (*.cfp)"); if (path.isEmpty()) return;
    QFile file(path); if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024) { error->setText("Cannot read patch or file is too large"); return; }
    auto object = QJsonDocument::fromJson(file.readAll()).object();
    if (object["format"] != "cold-flame-patch" || object["version"] != 1 || !object["channels"].isArray()) { error->setText("Invalid patch file"); return; }
    auto json = engine.json(); json["channels"] = object["channels"]; json["fixtureProfiles"] = object["fixtureProfiles"];
    Engine draft; QString issue;
    if (!draft.load(json, &issue)) { error->setText("Patch is invalid, overlaps, or is incompatible with stored show data"); return; }
    for (int id : engine.channels.keys()) {
        if (!draft.channels.contains(id) || !engine.canChangeFixture(id, draft.fixtures.value(id))) { error->setText("Import must preserve existing channels and stored parameter identities"); return; }
        const auto old = engine.fixtures.value(id), next = draft.fixtures.value(id);
        if (old.profile == next.profile && fixtureJson(engine.fixtureProfiles.value(old.profile)) != fixtureJson(draft.fixtureProfiles.value(next.profile))) { error->setText("Profile ID collision: import into a new show or use a distinct GDTF profile"); return; }
    }
    profiles = draft.fixtureProfiles; fillModes(batchFixture);
    { QSignalBlocker blocker(banks[0]); banks[0]->setRowCount(0); }
    for (int id : draft.channels.keys()) appendRow(banks[0], id, draft.channels[id], draft.patch.value(id), draft.fixtures.value(id), draft.additionalPatch.value(id));
    validate(); error->setText("Patch imported as a draft. Review it, then Apply patch.");
}
