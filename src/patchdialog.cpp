#include "tabledelegate.h"
#include "patchdialog.h"
#include "fixtureeditor.h"
#include "fixturepicker.h"
#include <algorithm>
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
    auto define = new QPushButton("Define fixture…"); define->setObjectName("defineFixtureButton"); toolbar->addWidget(define);
    connect(define, &QPushButton::clicked, this, [this] { defineFixture(); });
    auto more = new QPushButton("Patch tools ▾"); more->setObjectName("patchToolsButton"); toolbar->addWidget(more);
    auto menu = new QMenu(more);
    auto editDefinition = menu->addAction("Edit / copy selected fixture…", this, [this] { defineFixture(true); });
    editDefinition->setObjectName("editFixtureDefinitionAction"); menu->addSeparator();
    menu->addAction("Import fixtures… (GDTF / OFL JSON)", this, [this] { importProfile(); });
    menu->addAction("Export selected fixture as GDTF…", this, [this] { exportProfile(); });
    menu->addAction("Export selected fixture as OFL JSON…", this, [this] { exportProfile(true); }); menu->addSeparator();
    menu->addAction("Import patch…", this, [this] { importPatch(); });
    menu->addAction("Export applied patch…", this, [this] { exportPatch(); });
    if (embedded) {
        menu->addSeparator(); menu->addAction("Full view…", this, [this] {
            PatchDialog dialog(engine, this);
            if (dialog.exec() == QDialog::Accepted) { reload(); if (this->onApply) this->onApply(); }
        });
    }
    more->setMenu(menu); more->setStatusTip("Import and export GDTF or OFL JSON fixture profiles or transfer an applied patch."); layout->addLayout(toolbar);
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
    first->setObjectName("patchFirstChannel"); count->setObjectName("patchChannelCount");
    universe->setObjectName("patchUniverse"); address->setObjectName("patchStartAddress");
    batchFixture = new FixturePicker(profiles); batchFixture->setObjectName("batchFixtureMode"); fillModes(batchFixture);
    batch->addWidget(new QLabel("Fixture / mode"), 2, 0); batch->addWidget(batchFixture, 2, 1, 1, 3);
    auto add = new QPushButton("Add / repatch range"); add->setObjectName("patchRangeButton"); batch->addWidget(add, 3, 0, 1, 4); layout->addWidget(batchWidget);
    batchWidget->setVisible(!embedded); connect(batchToggle, &QPushButton::clicked, this, [batchWidget] { batchWidget->setVisible(!batchWidget->isVisible()); });
    error = new QLabel; error->setObjectName("patchError"); error->setStyleSheet("color:#e0c998;"); error->setWordWrap(true); layout->addWidget(error);
    auto resolve = new QPushButton("Unpatch conflicting outputs…"); resolve->setObjectName("resolvePatchConflictsButton");
    resolve->setStatusTip("Preview the fixture starts that overlap the patch you just entered. Confirm to remove those starts from the draft.");
    layout->addWidget(resolve, 0, Qt::AlignLeft);
    connect(resolve, &QPushButton::clicked, this, [this] { resolveConflicts(); });
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel); layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Save)->setText("Apply patch"); buttons->button(QDialogButtonBox::Save)->setObjectName("applyPatchButton");
    if (embedded) buttons->button(QDialogButtonBox::Cancel)->setText("Revert draft");
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (apply()) { if (this->onApply) this->onApply(); if (!this->embedded) accept(); }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, [this] { if (this->embedded) reload(); else reject(); });
    connect(table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (item->column() == 2) patchTargets = {banks[0]->item(item->row(), 0)->text().toInt()};
        validate();
    });
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
        patchTargets.clear(); for (int i = 0; i < starts.size(); ++i) patchTargets.insert(first->value() + i);
        validate();
    });
    populate(); validate();
}
void PatchDialog::fillModes(QComboBox *combo, FixtureAssignment selected) {
    static_cast<FixturePicker *>(combo)->setFixture(selected, false);
}
void PatchDialog::appendRow(QTableWidget *t, int id, QString name, int absolute, FixtureAssignment mode, QList<int> extra) {
    QSignalBlocker block(t); int r = t->rowCount(); t->insertRow(r);
    QStringList targets; if (absolute > 0) targets << targetText(absolute); for (int a : extra) targets << targetText(a);
    QStringList fields{QString::number(id), name, targets.join(", "), "", QString::number(engine.output().value(id), 'f', 0) + " %"};
    for (int c = 0; c < fields.size(); ++c) { auto item = new QTableWidgetItem(fields[c]); if (c == 0 || c >= 3) item->setFlags(item->flags() & ~Qt::ItemIsEditable); t->setItem(r, c, item); }
    auto combo = new FixturePicker(profiles); combo->setObjectName(QString("fixtureMode%1").arg(id)); fillModes(combo, mode); t->setCellWidget(r, 3, combo);
    connect(combo, &QComboBox::currentIndexChanged, this, [this, id] { patchTargets = {id}; validate(); });
}
void PatchDialog::populate() {
    for (auto i = engine.channels.begin(); i != engine.channels.end(); ++i)
        appendRow(banks[0], i.key(), i.value(), engine.patch.value(i.key()), engine.fixtures.value(i.key()), engine.additionalPatch.value(i.key()));
}
void PatchDialog::reload() {
    patchTargets.clear();
    profiles = engine.fixtureProfiles; fillModes(batchFixture);
    { QSignalBlocker blocker(banks[0]); banks[0]->setRowCount(0); }
    filter->clear(); populate(); validate();
}
bool PatchDialog::buildDraft(Engine &draft, QString &issue) const {
    // A profile ID also identifies the ordering of stored parameter keys.
    // Never reinterpret a fixture already present in the active show.
    QSet<QString> checked;
    for (int channel : engine.channels.keys()) {
        const QString id = engine.fixtures.value(channel).profile;
        if (checked.contains(id)) continue;
        checked.insert(id);
        auto proposed = profiles.value(id), existing = engine.fixtureProfiles.value(id);
        const bool sameArchive = proposed.source == existing.source && proposed.oflSource == existing.oflSource;
        proposed.source.clear(); existing.source.clear(); proposed.oflSource.clear(); existing.oflSource.clear();
        if (!sameArchive || fixtureJson(proposed) != fixtureJson(existing)) {
            issue = QString("Channel %1 uses a changed fixture definition. Save the definition as a copy.").arg(channel); return false;
        }
    }
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
    QMap<int, QList<int>> remaining; QStringList details;
    findChild<QPushButton *>("resolvePatchConflictsButton")->setVisible(parsed && !issues.isEmpty() && conflictPlan(draft, remaining, details));
}
bool PatchDialog::conflictPlan(const Engine &draft, QMap<int, QList<int>> &remaining, QStringList &details) const {
    QList<QPair<int, int>> requested;
    for (int channel : patchTargets) {
        const auto mode = draft.fixtureMode(channel); QString issue;
        if (!validateMode(mode, issue)) return false;
        for (int start : draft.dmxAddresses(channel)) {
            if (start < 1 || start > 63999 * 512 || (start - 1) % 512 + mode.footprint > 512) return false;
            requested << qMakePair(start, start + mode.footprint - 1);
        }
    }
    if (requested.isEmpty()) return false;
    std::sort(requested.begin(), requested.end());
    for (int i = 1; i < requested.size(); ++i) if (requested[i].first <= requested[i - 1].second) return false;
    for (int channel : draft.channels.keys()) {
        if (patchTargets.contains(channel)) continue;
        QStringList removed; QList<int> kept;
        const int footprint = draft.fixtureMode(channel).footprint;
        if (footprint < 1) continue;
        for (int start : draft.dmxAddresses(channel)) {
            auto match = std::lower_bound(requested.cbegin(), requested.cend(), start,
                [](const QPair<int, int> &range, int value) { return range.second < value; });
            if (match != requested.cend() && match->first <= start + footprint - 1) removed << targetText(start);
            else kept << start;
        }
        if (!removed.isEmpty()) {
            remaining[channel] = kept;
            QStringList preserved; for (int start : kept) preserved << targetText(start);
            details << QString("CH %1 · %2\nUnpatch: %3\nKeep: %4").arg(channel).arg(draft.channels.value(channel),
                removed.join(", "), preserved.isEmpty() ? "No remaining outputs" : preserved.join(", "));
        }
    }
    return !remaining.isEmpty();
}
void PatchDialog::resolveConflicts() {
    Engine draft; QString issue; QMap<int, QList<int>> remaining; QStringList details;
    if (!buildDraft(draft, issue) || !conflictPlan(draft, remaining, details)) return;
    QDialog dialog(this); dialog.setObjectName("patchConflictDialog"); dialog.setWindowTitle("Resolve DMX patch conflicts"); dialog.resize(620, 440);
    QVBoxLayout layout(&dialog);
    QStringList targets; auto ordered = patchTargets.values(); std::sort(ordered.begin(), ordered.end());
    for (int id : ordered) targets << QString::number(id);
    auto note = new QLabel("Keep the new patch for channels " + targets.join(", ") +
        ". Remove the conflicting fixture starts listed below. Channels, cues and other outputs are retained. Apply patch makes the draft live.");
    note->setWordWrap(true); layout.addWidget(note);
    QPlainTextEdit preview; preview.setReadOnly(true); preview.setPlainText(details.join("\n\n")); layout.addWidget(&preview, 1);
    QDialogButtonBox buttons(QDialogButtonBox::Cancel); auto confirm = buttons.addButton("Unpatch conflicting outputs", QDialogButtonBox::AcceptRole);
    confirm->setObjectName("confirmUnpatchConflicts"); buttons.button(QDialogButtonBox::Cancel)->setDefault(true); layout.addWidget(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    auto table = banks[0];
    { QSignalBlocker block(table);
        for (int row = 0; row < table->rowCount(); ++row) {
            const int id = table->item(row, 0)->text().toInt(); if (!remaining.contains(id)) continue;
            QStringList starts; for (int start : remaining[id]) starts << targetText(start);
            table->item(row, 2)->setText(starts.join(", "));
        }
    }
    validate();
}
bool PatchDialog::apply() {
    validate(); if (!error->property("valid").toBool()) return false;
    Engine draft; QString issue; if (!buildDraft(draft, issue)) return false;
    engine.channels = draft.channels; engine.patch = draft.patch; engine.additionalPatch = draft.additionalPatch;
    engine.fixtures = draft.fixtures; engine.fixtureProfiles = profiles; return true;
}
void PatchDialog::importProfile() {
    const auto paths = QFileDialog::getOpenFileNames(this, "Import fixture profiles", {}, "Fixture profiles (*.gdtf *.json);;GDTF (*.gdtf);;Open Fixture Library (*.json)");
    if (paths.isEmpty()) return;
    int imported = 0; QStringList failures; importingBatch = true;
    for (const auto &path : paths) {
        if (importFixtureFile(path)) ++imported;
        else if (failures.size() < 10) failures << QFileInfo(path).fileName() + ": " + error->text();
    }
    importingBatch = false;
    const QString lastReport = error->text();
    if (imported) refreshFixturePickers(lastImported);
    if (paths.size() > 1) error->setText(QString("%1 / %2 profiles imported into the draft. Apply patch to save the library.").arg(imported).arg(paths.size()) +
        (failures.isEmpty() ? QString() : "\n" + failures.join("\n")));
    else error->setText(lastReport);
}
bool PatchDialog::importFixtureFile(const QString &path) {
    QFile file(path); FixtureProfile profile; QString issue;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024) { error->setText("Cannot read profile or file is too large"); return false; }
    const auto bytes = file.readAll(); const bool ofl = QFileInfo(path).suffix().compare("json", Qt::CaseInsensitive) == 0;
    QString manufacturer;
    if (ofl) {
        const auto json = QJsonDocument::fromJson(bytes).object();
        QString key = json["manufacturerKey"].toString(); if (key.isEmpty()) key = QFileInfo(path).dir().dirName();
        QFile names(QFileInfo(path).dir().filePath("manufacturers.json"));
        if (!names.exists()) names.setFileName(QFileInfo(path).dir().filePath("../manufacturers.json"));
        if (names.open(QIODevice::ReadOnly) && names.size() <= 4 * 1024 * 1024)
            manufacturer = QJsonDocument::fromJson(names.readAll()).object()[key].toObject()["name"].toString();
    }
    if (!(ofl ? readOfl(bytes, profile, issue, manufacturer) : readGdtf(bytes, profile, issue))) { error->setText(issue); return false; }
    QString id; bool duplicate = false;
    for (auto it = profiles.cbegin(); it != profiles.cend(); ++it)
        if ((ofl ? it->oflSource == bytes : it->source == bytes) && it->manufacturer == profile.manufacturer) { id = it.key(); duplicate = true; break; }
    if (id.isEmpty()) { id = QUuid::createUuid().toString(QUuid::WithoutBraces); profiles[id] = profile; }
    int supported = 0, selected = 0, rawModes = 0;
    for (int i = 0; i < profile.modes.size(); ++i) {
        if (profile.modes[i].unsupported.isEmpty()) { if (!supported) selected = i; ++supported; }
        if (!profile.modes[i].warnings.isEmpty()) ++rawModes;
    }
    lastImported = {id, selected}; if (!importingBatch) refreshFixturePickers(lastImported);
    error->setText(QString("%1 %2 / %3 · %4 / %5 modes available%6. Search fixtures to inspect mode details. Apply patch to save the library.")
        .arg(duplicate ? "Already imported:" : "Imported:", profile.manufacturer, profile.name).arg(supported).arg(profile.modes.size())
        .arg(rawModes ? QString(" · %1 modes include raw DMX controls").arg(rawModes) : QString()));
    return true;
}
void PatchDialog::refreshFixturePickers(FixtureAssignment selected) {
    auto table = banks[0];
    for (int r = 0; r < table->rowCount(); ++r) { auto combo = qobject_cast<QComboBox *>(table->cellWidget(r, 3)); fillModes(combo, assignment(combo)); }
    fillModes(batchFixture, selected); validate();
}
void PatchDialog::exportProfile(bool ofl) {
    auto t = banks[0]; const int row = t->currentRow(); if (row < 0) { error->setText("Select a fixture row to export its profile"); return; }
    const auto profile = profiles.value(assignment(qobject_cast<QComboBox *>(t->cellWidget(row, 3))).profile);
    const QString name = QString(profile.name).replace(QRegularExpression(R"([<>:"/\\|?*])"), "_");
    const auto path = QFileDialog::getSaveFileName(this, ofl ? "Export OFL JSON fixture" : "Export GDTF fixture", name + (ofl ? ".json" : ".gdtf"), ofl ? "Open Fixture Library (*.json)" : "GDTF (*.gdtf)");
    if (!path.isEmpty()) exportFixtureFile(path, ofl);
}
bool PatchDialog::exportFixtureFile(const QString &path, bool ofl) {
    auto table = banks[0]; const int row = table->currentRow();
    if (row < 0) { error->setText("Select a fixture row to export its profile"); return false; }
    const auto profile = profiles.value(assignment(qobject_cast<QComboBox *>(table->cellWidget(row, 3))).profile);
    QString issue; const auto bytes = ofl ? writeOfl(profile, issue) : writeGdtf(profile);
    if (bytes.isEmpty()) { error->setText(issue.isEmpty() ? "Cannot export this definition as GDTF. Check its modes and define reserved trailing slots with NoFeature parameters." : issue); return false; }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { error->setText(file.errorString()); return false; }
    error->setText(ofl ? "OFL JSON profile exported" : "GDTF profile exported"); return true;
}
void PatchDialog::defineFixture(bool copySelected) {
    FixtureProfile seed; QString id; bool selectedModeOnly = false;
    if (copySelected) {
        auto table = banks[0]; const int row = table->currentRow();
        if (row < 0) { error->setText("Select a fixture row to edit or copy its definition."); return; }
        id = assignment(qobject_cast<QComboBox *>(table->cellWidget(row, 3))).profile;
        seed = profiles.value(id);
        const int selectedMode = assignment(qobject_cast<QComboBox *>(table->cellWidget(row, 3))).mode;
        if (!seed.modes.value(selectedMode).unsupported.isEmpty()) {
            error->setText("Select a supported mode, or use Define fixture to create a new definition."); return;
        }
        bool hasUnsupported = false; for (const auto &mode : seed.modes) hasUnsupported |= !mode.unsupported.isEmpty();
        if (hasUnsupported) { seed.modes = {seed.modes[selectedMode]}; selectedModeOnly = true; }
        bool copy = genericFixtures().contains(id) || !seed.source.isEmpty() || !seed.oflSource.isEmpty();
        for (int channel : engine.channels.keys()) copy |= engine.fixtures.value(channel).profile == id;
        if (copy) { id.clear(); seed.name += " copy"; }
    }
    FixtureEditor editor(seed, this);
    if (copySelected && id.isEmpty()) editor.setWindowTitle("Fixture definition · new copy");
    if (selectedModeOnly) editor.setWindowTitle("Fixture definition · copy of selected supported mode");
    if (editor.exec() != QDialog::Accepted) return;
    if (id.isEmpty()) id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    profiles[id] = editor.profile();
    refreshFixturePickers({id, 0});
    error->setText("Definition saved in the patch draft. Select its mode for a channel or Batch, then Apply patch.");
}
void PatchDialog::exportPatch() {
    QString path = QFileDialog::getSaveFileName(this, "Export applied patch", "patch.cfp", "Cold Flame patch (*.cfp)"); if (path.isEmpty()) return;
    auto json = engine.json(); QJsonObject patch{{"format", "cold-flame-patch"}, {"version", 2}, {"channels", json["channels"]}, {"fixtureProfiles", json["fixtureProfiles"]}};
    QSaveFile file(path); auto bytes = QJsonDocument(patch).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) error->setText(file.errorString()); else error->setText("Applied patch exported");
}
void PatchDialog::importPatch() {
    QString path = QFileDialog::getOpenFileName(this, "Import patch", {}, "Cold Flame patch (*.cfp)"); if (path.isEmpty()) return;
    QFile file(path); if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024) { error->setText("Cannot read patch or file is too large"); return; }
    auto object = QJsonDocument::fromJson(file.readAll()).object();
    if (object["format"] != "cold-flame-patch" || (object["version"] != 1 && object["version"] != 2) || !object["channels"].isArray()) { error->setText("Invalid patch file"); return; }
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
