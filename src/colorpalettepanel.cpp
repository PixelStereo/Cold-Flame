#include "colorpalettepanel.h"
#include "tabledelegate.h"
using namespace Flame;
ColorPalettePanel::ColorPalettePanel(Engine &e, std::function<void()> changed,
    std::function<void()> liveChanged, QWidget *parent)
    : QWidget(parent), engine(e), changed(std::move(changed)), liveChanged(std::move(liveChanged)) {
    auto layout = new QVBoxLayout(this);
    auto note = new QLabel("Choosing a fixture color creates a shared color palette. Record / Update saves its reference in the cue; intensity stays separate. Editing a palette changes every reference on future recalls.");
    note->setWordWrap(true); layout->addWidget(note);
    table = new QTableWidget(0, 4); table->setObjectName("colorPaletteTable");
    table->setHorizontalHeaderLabels({"CP", "COLOR", "NAME", "REFERENCES"});
    table->setItemDelegate(new TableDelegate(table)); table->verticalHeader()->hide(); table->verticalHeader()->setDefaultSectionSize(32);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(table, 1);
    auto actions = new QHBoxLayout;
    auto apply = new QPushButton("Apply to selection"); apply->setObjectName("applyColorPaletteButton");
    auto edit = new QPushButton("Edit color…"); edit->setObjectName("editColorPaletteButton");
    auto remove = new QPushButton("Delete unused"); remove->setObjectName("deleteColorPaletteButton");
    auto assign = new QPushButton("Assign to cue…"); assign->setObjectName("assignColorPaletteButton");
    actions->addWidget(apply); actions->addWidget(assign); actions->addStretch(); layout->addLayout(actions);
    auto editing = new QHBoxLayout; editing->addWidget(edit); editing->addWidget(remove); editing->addStretch(); layout->addLayout(editing);
    assign->setStatusTip("Assign this palette directly to a stored cue, changing color references and retaining intensity.");
    connect(assign, &QPushButton::clicked, this, [this] { assignToCue(); });
    feedback = new QLabel; feedback->setWordWrap(true); layout->addWidget(feedback);
    apply->setStatusTip("Apply this color palette to selected RGB/RGBW/CMY fixtures, leaving intensity unchanged.");
    edit->setStatusTip("Change the shared palette. Referencing cues use its new color on future recalls; running fade targets stay unchanged.");
    remove->setStatusTip("Delete a palette only when no stored or manual data references it.");
    connect(apply, &QPushButton::clicked, this, [this] {
        const int id = selected(); int count = 0;
        if (id < 1) return;
        for (int channel : engine.selection) count += engine.applyColorPalette(channel, id);
        this->liveChanged(); feedback->setText(count ? QString("CP %1 applied to %2 fixtures").arg(id).arg(count) : "Select color-capable channels first");
    });
    connect(edit, &QPushButton::clicked, this, [this] { editColor(); });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (engine.deleteColorPalette(selected())) { reload(); this->changed(); }
        else feedback->setText("This palette is referenced. Remove its stored references and release manual color before deleting it.");
    });
    connect(table, &QTableWidget::cellDoubleClicked, this, [this](int, int column) { if (column == 1) editColor(); });
    connect(table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (item->column() != 2) return;
        int id = item->data(Qt::UserRole).toInt();
        auto color = engine.colorPalette(id); color.name = item->text();
        if (engine.updateColorPalette(id, color)) this->changed();
    });
    reload();
}
int ColorPalettePanel::selected() const {
    auto item = table->item(table->currentRow(), 0); return item ? item->data(Qt::UserRole).toInt() : -1;
}
void ColorPalettePanel::reload(int select) {
    if (select < 1) select = selected();
    QSignalBlocker block(table); table->setRowCount(0);
    for (auto it = engine.colorPalettes.begin(); it != engine.colorPalettes.end(); ++it) {
        if (it.key() <= 0) continue;
        int uses = 0;
        auto count = [&](const auto &values) { for (const auto &v : values) if (v.colorPalette == it.key()) ++uses; };
        count(engine.manual);
        for (const auto &cue : engine.cueLibrary) count(cue.values);
        for (const auto &seq : engine.sequences) for (const auto &step : seq.cues) { count(step.values); count(step.blocked); }
        int row = table->rowCount(); table->insertRow(row);
        const QColor color = QColor::fromRgbF(it->red, it->green, it->blue);
        QStringList fields{QString::number(it.key()), color.name(), it->name, QString::number(uses)};
        for (int column = 0; column < fields.size(); ++column) {
            auto item = new QTableWidgetItem(fields[column]); item->setData(Qt::UserRole, it.key());
            if (column != 2) item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            if (column == 1) item->setData(Qt::DecorationRole, color);
            table->setItem(row, column, item);
        }
        if (it.key() == select) table->selectRow(row);
    }
    if (table->currentRow() < 0 && table->rowCount()) table->selectRow(0);
    for (const auto &name : {"applyColorPaletteButton", "editColorPaletteButton", "deleteColorPaletteButton", "assignColorPaletteButton"})
        if (auto button = findChild<QPushButton *>(name)) button->setEnabled(table->rowCount() > 0);
}
void ColorPalettePanel::editColor() {
    const int id = selected(); if (id < 1) return;
    auto palette = engine.colorPalette(id);
    const bool automaticName = palette.name == QColor::fromRgbF(palette.red, palette.green, palette.blue).name();
    QColorDialog dialog(QColor::fromRgbF(palette.red, palette.green, palette.blue), this);
    dialog.setObjectName("editColorPaletteDialog"); dialog.setOption(QColorDialog::DontUseNativeDialog);
    dialog.setWindowTitle(QString("Edit CP %1 · every reference").arg(id));
    if (dialog.exec() != QDialog::Accepted) return;
    auto color = dialog.currentColor(); if (automaticName) palette.name = color.name(); palette.red = color.redF(); palette.green = color.greenF(); palette.blue = color.blueF();
    if (engine.updateColorPalette(id, palette)) { reload(id); changed(); liveChanged(); }
}

void ColorPalettePanel::assignToCue(QString preferred) {
    if (engine.colorPalettes.isEmpty() || engine.cueLibrary.isEmpty()) {
        feedback->setText("Create a color palette and a stored cue first."); return;
    }
    QDialog dialog(this); dialog.setObjectName("assignCueColorDialog"); dialog.setWindowTitle("Assign color palette to stored cue");
    QVBoxLayout layout(&dialog); QFormLayout form;
    QComboBox cue, palette; cue.setObjectName("assignColorCue"); palette.setObjectName("assignColorPalette");
    auto numbers = engine.cueLibrary.keys(); std::sort(numbers.begin(), numbers.end(), [](QString a, QString b) { return a.toDouble() < b.toDouble(); });
    for (const auto &number : numbers) cue.addItem(number + " · " + engine.cueLibrary[number].label, number);
    int row = cue.findData(preferred); if (row >= 0) cue.setCurrentIndex(row);
    for (auto it = engine.colorPalettes.begin(); it != engine.colorPalettes.end(); ++it)
        if (it.key() > 0) palette.addItem(QString("CP %1 · %2").arg(it.key()).arg(it->name), it.key());
    row = palette.findData(selected()); if (row >= 0) palette.setCurrentIndex(row);
    form.addRow("Stored cue", &cue); form.addRow("Color palette", &palette); layout.addLayout(&form);
    QCheckBox selection("Selected fixtures only"); selection.setObjectName("assignColorSelection");
    selection.setChecked(!engine.selection.isEmpty()); selection.setEnabled(!engine.selection.isEmpty()); layout.addWidget(&selection);
    QLabel note("Changes color only, for every occurrence of this stored cue. With no fixture selection, it targets color-capable fixtures already stored in the cue."); note.setWordWrap(true); layout.addWidget(&note);
    QLabel error; error.setWordWrap(true); layout.addWidget(&error);
    QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel); layout.addWidget(&buttons);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        int count = engine.assignColorPaletteToCue(cue.currentData().toString(), palette.currentData().toInt(), selection.isChecked() ? engine.selection : QSet<int>{});
        if (!count) { error.setText("No eligible RGB/RGBW/CMY fixtures in this target."); return; }
        changed(); reload(palette.currentData().toInt());
        feedback->setText(QString("Palette assigned to %1 fixtures · cue intensity retained").arg(count)); dialog.accept();
    });
    dialog.exec();
}
