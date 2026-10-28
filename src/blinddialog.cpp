#include "tabledelegate.h"
#include "blinddialog.h"
#include <cmath>
BlindDialog::BlindDialog(Flame::Engine &engine, int sequence, std::function<void()> change,
                         QWidget *parent)
    : QDialog(parent), engine(engine), sequence(sequence), onChange(change) {
    setWindowTitle("Blind · Stored cue spreadsheet");
    setObjectName("blindDialog");
    resize(1100, 780);
    auto layout = new QVBoxLayout(this);
    auto title = new QLabel(QString("BLIND  /  %1 · %2")
                                .arg(engine.sequences[sequence].number)
                                .arg(engine.sequences[sequence].name));
    title->setObjectName("title");
    layout->addWidget(title);
    auto note =
        new QLabel("Double-click a level to edit stored data. Changes save immediately; live "
                   "output is not recalled. Warm = blocked · sage = move · grey = tracked.");
    note->setWordWrap(true);
    layout->addWidget(note);
    cueOnly = new QCheckBox("Cue Only · keep the next cue unchanged");
    layout->addWidget(cueOnly);
    sheet = new QTableWidget;
    sheet->setObjectName("blindSheet");
    sheet->setItemDelegate(new TableDelegate(sheet));
    sheet->setAlternatingRowColors(true);
    sheet->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    sheet->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    layout->addWidget(sheet, 1);
    connect(sheet, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        int channel = item->data(Qt::UserRole).toInt(), cue = item->column();
        bool ok;
        double value = item->text().toDouble(&ok);
        if (ok && std::isfinite(value) && value >= 0 && value <= 100) {
            this->engine.editBlind(this->sequence, cue, channel, {value, 0}, cueOnly->isChecked());
            onChange();
        }
        populate();
    });
    auto close = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(close);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    populate();
}
void BlindDialog::populate() {
    QSignalBlocker block(sheet);
    auto &seq = engine.sequences[sequence];
    sheet->setRowCount(engine.channels.size());
    sheet->setColumnCount(seq.cues.size());
    QStringList headers;
    for (const auto &c : seq.cues)
        headers << c.number + " / " + c.label;
    sheet->setHorizontalHeaderLabels(headers);
    QStringList rows;
    for (auto it = engine.channels.begin(); it != engine.channels.end(); ++it)
        rows << QString::number(it.key()) + " · " + it.value();
    sheet->setVerticalHeaderLabels(rows);
    for (int col = 0; col < seq.cues.size(); ++col) {
        auto values = engine.tracked(sequence, col);
        int row = 0;
        for (int channel : engine.channels.keys()) {
            auto item = sheet->item(row, col);
            if (!item) {
                item = new QTableWidgetItem;
                sheet->setItem(row, col, item);
            }
            item->setText(QString::number(values.value(channel), 'g', 6));
            item->setData(Qt::UserRole, channel);
            item->setForeground(QColor(seq.cues[col].blocked.contains(channel)  ? "#e0c998"
                                       : seq.cues[col].values.contains(channel) ? "#b7cfc1"
                                                                                : "#909995"));
            ++row;
        }
    }
}
