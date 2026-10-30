#include "fixturepicker.h"
#include <algorithm>
using namespace Flame;
namespace {
const QStringList Quick{"dimmer8", "rgb", "rgbw", "rgbd", "dimmer16", "moving-rgbw"};
QString label(const FixtureProfile &profile, const FixtureMode &mode) {
    return profile.name + (profile.manufacturer == "Generic" && mode.name == "Standard" ? QString() : " / " + mode.name) + QString(" · %1ch").arg(mode.footprint);
}
}
FixturePicker::FixturePicker(const QMap<QString, FixtureProfile> &library, QWidget *parent) : QComboBox(parent), profiles(&library) {
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); setMinimumContentsLength(12);
    setStatusTip("Choose Dimmer, RGB or RGBW, or search imported fixtures by manufacturer, name and DMX mode.");
    setAccessibleName("Fixture and DMX mode"); setFixture({}, false);
}
FixtureAssignment FixturePicker::fixture() const { return {currentData().toString(), currentData(Qt::UserRole + 1).toInt()}; }
void FixturePicker::setFixture(FixtureAssignment selected, bool notify) {
    const auto previous = fixture();
    {
        QSignalBlocker blocker(this); clear();
        auto append = [&](QString id, int m) {
            if (!profiles->contains(id) || m < 0 || m >= profiles->value(id).modes.size()) return;
            const auto &profile = (*profiles)[id]; const auto &mode = profile.modes[m];
            addItem(label(profile, mode), id); setItemData(count() - 1, m, Qt::UserRole + 1);
            setItemData(count() - 1, mode.unsupported, Qt::ToolTipRole);
        };
        for (const auto &id : Quick) append(id, 0);
        int row = -1;
        for (int i = 0; i < count(); ++i) if (itemData(i).toString() == selected.profile && itemData(i, Qt::UserRole + 1).toInt() == selected.mode) row = i;
        if (row < 0 && profiles->contains(selected.profile) && selected.mode >= 0 && selected.mode < profiles->value(selected.profile).modes.size()) {
            append(selected.profile, selected.mode); row = count() - 1;
        }
        setCurrentIndex(row < 0 ? 0 : row);
    }
    if (notify && (previous.profile != fixture().profile || previous.mode != fixture().mode)) emit currentIndexChanged(currentIndex());
}
void FixturePicker::showPopup() {
    QDialog dialog(this); dialog.setObjectName("fixtureSearchDialog"); dialog.setWindowTitle("Choose fixture and mode"); dialog.resize(860, 600);
    auto layout = new QVBoxLayout(&dialog); layout->setContentsMargins(16, 16, 16, 16); layout->setSpacing(10);
    auto quick = new QHBoxLayout;
    const QStringList titles{"Dimmer", "RGB", "RGBW", "Dimmer + RGB", "16-bit dimmer", "Moving RGBW"};
    FixtureAssignment selected = fixture();
    for (int i = 0; i < Quick.size(); ++i) {
        auto button = new QPushButton(titles[i]); button->setObjectName("quickFixture_" + Quick[i]); button->setAutoDefault(false);
        const auto profile = profiles->value(Quick[i]); QString issue;
        button->setEnabled(!profile.modes.isEmpty() && validateMode(profile.modes[0], issue));
        if (!profile.modes.isEmpty()) button->setStatusTip(label(profile, profile.modes[0]));
        connect(button, &QPushButton::clicked, &dialog, [&, i] { selected = {Quick[i], 0}; dialog.accept(); }); quick->addWidget(button);
    }
    layout->addLayout(quick);
    auto search = new QLineEdit; search->setObjectName("fixtureSearch"); search->setClearButtonEnabled(true);
    search->setPlaceholderText("Search manufacturer, fixture or mode…"); search->setAccessibleName("Search fixture library"); layout->addWidget(search);
    auto count = new QLabel; count->setObjectName("fixtureSearchCount"); layout->addWidget(count);
    auto table = new QTableWidget(0, 5); table->setObjectName("fixtureSearchResults"); table->setHorizontalHeaderLabels({"MANUFACTURER", "FIXTURE", "MODE", "DMX", "STATUS"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->hide(); table->verticalHeader()->setDefaultSectionSize(34); table->setAlternatingRowColors(true); table->setWordWrap(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive); table->setColumnWidth(0, 150);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive); table->setColumnWidth(2, 170); layout->addWidget(table, 1);
    auto details = new QPlainTextEdit; details->setObjectName("fixtureSearchDetails"); details->setReadOnly(true); details->setMaximumHeight(110); layout->addWidget(details);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); buttons->button(QDialogButtonBox::Ok)->setText("Use fixture"); layout->addWidget(buttons);
    struct Entry { QString id; int mode; QString manufacturer, name, modeName, haystack; int footprint; bool supported; QString details; };
    QList<Entry> catalogue;
    for (auto it = profiles->cbegin(); it != profiles->cend(); ++it) for (int m = 0; m < it->modes.size(); ++m) {
        const auto &mode = it->modes[m]; QString issue; const bool supported = validateMode(mode, issue);
        QSet<QString> cells; for (const auto &p : mode.parameters) if (!p.cell.isEmpty()) cells.insert(p.cell);
        QString description = it->manufacturer + " / " + it->name + " / " + mode.name;
        if (supported) description += QString("\n%1 DMX slots · %2 controls · %3 cells").arg(mode.footprint).arg(mode.parameters.size()).arg(cells.size());
        else description += "\nUnavailable: " + issue;
        if (!mode.warnings.isEmpty()) description += "\n" + mode.warnings.join("\n");
        catalogue << Entry{it.key(), m, it->manufacturer, it->name, mode.name, (it->manufacturer + " " + it->name + " " + mode.name).toCaseFolded(), mode.footprint, supported, description};
    }
    std::stable_sort(catalogue.begin(), catalogue.end(), [](const Entry &a, const Entry &b) { return QString::compare(a.manufacturer + a.name + a.modeName, b.manufacturer + b.name + b.modeName, Qt::CaseInsensitive) < 0; });
    auto selectedEntry = [&]() -> const Entry * { int row = table->currentRow(); if (row < 0) return nullptr; return &catalogue[table->item(row, 0)->data(Qt::UserRole).toInt()]; };
    auto updateSelection = [&] {
        auto entry = selectedEntry(); buttons->button(QDialogButtonBox::Ok)->setEnabled(entry && entry->supported);
        details->setPlainText(entry ? entry->details : "No matching fixtures. Import a GDTF or OFL JSON profile from Patch tools.");
    };
    auto refresh = [&] {
        QSignalBlocker blocker(table); table->setRowCount(0); int matches = 0, chosen = -1;
        const auto terms = search->text().toCaseFolded().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        for (int i = 0; i < catalogue.size(); ++i) {
            const auto &entry = catalogue[i];
            if (terms.isEmpty()) {
                if (!(Quick.contains(entry.id) && entry.mode == 0) && !(entry.id == selected.profile && entry.mode == selected.mode)) continue;
            } else {
                bool match = true; for (const auto &term : terms) match &= entry.haystack.contains(term); if (!match) continue;
            }
            ++matches; if (table->rowCount() >= 200) continue;
            int row = table->rowCount(); table->insertRow(row);
            const QStringList fields{entry.manufacturer, entry.name, entry.modeName, QString::number(entry.footprint), entry.supported ? (profiles->value(entry.id).modes[entry.mode].warnings.isEmpty() ? "Ready" : "Raw controls") : "Unavailable"};
            for (int c = 0; c < fields.size(); ++c) { auto item = new QTableWidgetItem(fields[c]); item->setData(Qt::UserRole, i); table->setItem(row, c, item); }
            if (entry.id == selected.profile && entry.mode == selected.mode) chosen = row;
        }
        count->setText(terms.isEmpty() ? QString("Quick choices · search %1 fixture modes in your show library").arg(catalogue.size()) : QString("%1 matches%2").arg(matches).arg(matches > 200 ? " · first 200 shown, refine your search" : ""));
        if (table->rowCount()) table->selectRow(chosen < 0 ? 0 : chosen);
        updateSelection();
    };
    auto use = [&] { const auto entry = selectedEntry(); if (entry && entry->supported) { selected = {entry->id, entry->mode}; dialog.accept(); } };
    connect(search, &QLineEdit::textChanged, &dialog, refresh);
    connect(search, &QLineEdit::returnPressed, &dialog, use);
    auto down = new QShortcut(QKeySequence(Qt::Key_Down), search); down->setContext(Qt::WidgetShortcut);
    connect(down, &QShortcut::activated, &dialog, [&] { if (table->rowCount()) table->setFocus(); });
    connect(table, &QTableWidget::itemSelectionChanged, &dialog, updateSelection);
    connect(table, &QTableWidget::cellDoubleClicked, &dialog, [&](int, int) { use(); });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, use); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    refresh(); search->setFocus();
    if (dialog.exec() == QDialog::Accepted) setFixture(selected);
}
