#include "cuelibrary.h"
#include "tabledelegate.h"
#include <cmath>
using namespace Flame;
CueLibraryPanel::CueLibraryPanel(Engine &e, std::function<void()> editCallback,
    std::function<void(QString)> insertCallback, QWidget *parent)
    : QWidget(parent), engine(e), changed(std::move(editCallback)), insert(std::move(insertCallback)) {
    auto layout = new QVBoxLayout(this);
    auto hint = new QLabel("Stored cues are shared looks. Insert the same cue into any sequence more than once. "
                           "Each occurrence has its own step number and timing.");
    hint->setWordWrap(true); layout->addWidget(hint);
    table = new QTableWidget(0, 4); table->setObjectName("storedCueTable");
    table->setHorizontalHeaderLabels({"CUE", "NAME", "VALUES", "USES"});
    table->setItemDelegate(new TableDelegate(table));
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->hide(); table->verticalHeader()->setDefaultSectionSize(32);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(table, 1);
    auto actions = new QGridLayout;
    auto add = [&](QString text, int row, int col, std::function<void()> action, QString object, QString help) {
        auto button = new QPushButton(text); button->setObjectName(object); button->setStatusTip(help);
        button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        actions->addWidget(button, row, col, Qt::AlignLeft); connect(button, &QPushButton::clicked, this, action);
    };
    add("Record look", 0, 0, [this] {
        auto number = engine.nextStoredCueNumber();
        if (engine.storeCue(number)) { reload(number); changed(); }
    }, "recordLibraryCueButton", "Record the live look as the next stored cue. This does not insert a sequence step.");
    add("Record Only", 0, 1, [this] {
        auto number = engine.nextStoredCueNumber();
        if (engine.storeCue(number, {}, true)) { reload(number); changed(); }
    }, "recordLibraryOnlyButton", "Record manual values only as a new stored cue.");
    add("Insert into sequence", 1, 0, [this] { if (!selected().isEmpty()) insert(selected()); },
        "insertStoredCueButton", "Append the selected stored cue to the selected sequence. Repeated uses share the same look.");
    add("Edit content…", 1, 1, [this] { edit(); }, "editStoredCueButton",
        "Edit stored channel values and membership. Every referencing step uses the new look on its next playback.");
    add("Update from manual", 2, 0, [this] {
        auto number = selected();
        if (engine.updateStoredCue(number, engine.manual)) { reload(number); changed(); }
    }, "updateStoredCueButton", "Merge manual channel values into this shared cue, retaining other stored values.");
    add("Delete stored cue", 2, 1, [this] {
        if (engine.deleteStoredCue(selected())) { reload(); changed(); }
        else feedback->setText("Remove this cue's steps from sequences before deleting the stored cue.");
    }, "deleteStoredCueButton", "Delete an unused cue. Remove its sequence steps first if it is still referenced.");
    layout->addLayout(actions); feedback = new QLabel; feedback->setWordWrap(true); layout->addWidget(feedback);
    connect(table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        const QString number = item->data(Qt::UserRole).toString();
        if (!engine.cueLibrary.contains(number)) return;
        if (item->column() == 0) {
            if (!engine.renameStoredCue(number, item->text())) { feedback->setText("Use a unique cue number between 0.001 and 9999.999."); reload(number); return; }
            const QString renamed = QString::number(item->text().toDouble(), 'g', 12);
            reload(renamed); changed();
        } else if (item->column() == 1) {
            engine.cueLibrary[number].label = item->text().isEmpty() ? "cue-" + number : item->text();
            changed();
        }
    });
    reload();
}
QString CueLibraryPanel::selected() const {
    auto item = table->item(table->currentRow(), 0);
    return item ? item->data(Qt::UserRole).toString() : QString();
}
void CueLibraryPanel::reload(QString select) {
    if (select.isEmpty()) select = selected();
    QSignalBlocker block(table);
    QList<QString> numbers = engine.cueLibrary.keys();
    std::sort(numbers.begin(), numbers.end(), [](const QString &a, const QString &b) { return a.toDouble() < b.toDouble(); });
    table->setRowCount(numbers.size());
    int row = 0;
    for (const auto &number : numbers) {
        const auto &cue = engine.cueLibrary[number]; int uses = 0;
        for (const auto &sequence : engine.sequences)
            for (const auto &step : sequence.cues) if (step.reference == number) ++uses;
        QStringList fields{number, cue.label, QString::number(cue.values.size()), QString::number(uses)};
        for (int col = 0; col < fields.size(); ++col) {
            auto item = new QTableWidgetItem(fields[col]); item->setData(Qt::UserRole, number);
            if (col >= 2) item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            table->setItem(row, col, item);
        }
        if (number == select) table->selectRow(row);
        ++row;
    }
    if (table->currentRow() < 0 && row) table->selectRow(0);
}
void CueLibraryPanel::edit() {
    QString number = selected();
    if (!engine.cueLibrary.contains(number)) return;
    QDialog dialog(this); dialog.setWindowTitle("Stored cue " + number + " · shared content"); dialog.resize(560, 680);
    QVBoxLayout layout(&dialog);
    auto note = new QLabel("Saving updates every use of this cue. Running fades keep their current targets.");
    note->setWordWrap(true); layout.addWidget(note);
    QTableWidget editor(engine.controlIds().size(), 3); editor.setObjectName("storedCueEditor");
    editor.setHorizontalHeaderLabels({"Include", "Channel", "Level %"});
    editor.setItemDelegate(new TableDelegate(&editor)); editor.verticalHeader()->setDefaultSectionSize(30);
    editor.horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    const auto values = engine.cueLibrary[number].values;
    int row = 0;
    for (int channel : engine.controlIds()) {
        auto check = new QTableWidgetItem;
        check->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        check->setCheckState(values.contains(channel) ? Qt::Checked : Qt::Unchecked);
        check->setData(Qt::UserRole, channel); editor.setItem(row, 0, check);
        auto label = new QTableWidgetItem(engine.controlLabel(channel));
        label->setFlags(Qt::ItemIsEnabled); editor.setItem(row, 1, label);
        editor.setItem(row++, 2, new QTableWidgetItem(QString::number(engine.resolve(channel, values.value(channel)))));
    }
    layout.addWidget(&editor); QLabel error; layout.addWidget(&error);
    QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel); layout.addWidget(&buttons);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        QMap<int, Value> content;
        for (int r = 0; r < editor.rowCount(); ++r) {
            if (editor.item(r, 0)->checkState() != Qt::Checked) continue;
            bool ok; double level = editor.item(r, 2)->text().toDouble(&ok);
            if (!ok || !std::isfinite(level) || level < 0 || level > 100) { error.setText("Levels must be between 0 and 100."); return; }
            int channel = editor.item(r, 0)->data(Qt::UserRole).toInt();
            content[channel] = values.contains(channel) && engine.resolve(channel, values[channel]) == level
                ? values[channel] : Value{level, 0};
        }
        engine.updateStoredCue(number, content, true); changed(); dialog.accept();
    });
    if (dialog.exec() == QDialog::Accepted) reload(number);
}
