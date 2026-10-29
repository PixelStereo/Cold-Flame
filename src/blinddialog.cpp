#include "tabledelegate.h"
#include "blinddialog.h"
#include <cmath>
class TrackingDelegate : public TableDelegate {
  public:
    using TableDelegate::TableDelegate;
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override {
        TableDelegate::initStyleOption(option, index);
        option->text += "  " + index.data(Qt::UserRole + 1).toString();
    }
};
BlindDialog::BlindDialog(Flame::Engine &engine, int sequence, std::function<void()> change,
                         QWidget *parent, bool embedded)
    : QDialog(parent), engine(engine), sequence(sequence), onChange(change) {
    if (embedded) setWindowFlags(Qt::Widget);
    setWindowTitle("Tracking · Sequence channels and parameters"); setObjectName("blindDialog"); resize(1100, 780);
    auto layout = new QVBoxLayout(this); layout->setSpacing(8);
    auto note = new QLabel("Blind editing saves immediately without recalling output. M = move · T = track · B = block · — = home. Right-click for tracking actions.");
    note->setWordWrap(true); layout->addWidget(note);
    sequencePicker = new QComboBox; sequencePicker->setObjectName("trackingSequence"); layout->addWidget(sequencePicker);
    connect(sequencePicker, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0) return;
        this->sequence = sequencePicker->itemData(index).toInt();
        if (sequenceSelected) sequenceSelected(this->sequence);
        populate();
    });
    auto bar = new QHBoxLayout; scope = new QComboBox; scope->setObjectName("trackingScope");
    scope->addItems({"All channels", "Selected channels", "Used in sequence"}); bar->addWidget(scope);
    cueOnly = new QCheckBox("Cue Only"); cueOnly->setStatusTip("Preserve the following step when editing or removing a move."); bar->addWidget(cueOnly);
    trace = new QCheckBox("Trace edit"); trace->setObjectName("trackingTrace"); trace->setStatusTip("Edit the previous move or block supplying this value, stopping at that block."); bar->addWidget(trace);
    forceTrace = new QCheckBox("Trace zeros"); forceTrace->setObjectName("trackingTraceZeros"); forceTrace->setEnabled(false); forceTrace->setStatusTip("Force a newly active level backwards through zero or null tracks, like Eos Trace Trace."); bar->addWidget(forceTrace); bar->addStretch(); layout->addLayout(bar);
    connect(trace, &QCheckBox::toggled, forceTrace, &QCheckBox::setEnabled);
    sheet = new QTableWidget; sheet->setObjectName("blindSheet"); sheet->setItemDelegate(new TrackingDelegate(sheet));
    sheet->setAlternatingRowColors(true); sheet->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    sheet->horizontalHeader()->setDefaultSectionSize(140); sheet->verticalHeader()->setDefaultSectionSize(30);
    sheet->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed); layout->addWidget(sheet, 1);
    feedback = new QLabel; feedback->setWordWrap(true); layout->addWidget(feedback);
    connect(scope, &QComboBox::currentIndexChanged, this, [this] { populate(); });
    connect(sheet, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        int key = item->data(Qt::UserRole).toInt(), step = item->column();
        QString text = item->text().trimmed(); if (text.endsWith('M') || text.endsWith('T') || text.endsWith('B')) text.chop(1);
        bool ok; double value = text.trimmed().toDouble(&ok);
        if (text.trimmed().isEmpty()) {
            this->engine.trackFromPrevious(this->sequence, step, key, cueOnly->isChecked()); onChange(); feedback->setText("Move removed · previous instruction now tracks into this step");
        } else if (ok && std::isfinite(value) && value >= 0 && value <= 100) {
            if (trace->isChecked()) this->engine.traceEdit(this->sequence, step, key, {value, 0}, forceTrace->isChecked(), cueOnly->isChecked());
            else this->engine.editBlind(this->sequence, step, key, {value, 0}, cueOnly->isChecked());
            onChange(); feedback->setText("Stored · running fade targets remain unchanged");
        } else feedback->setText("Enter 0–100, or clear the cell to track from the previous step");
        populate();
    });
    sheet->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(sheet, &QTableWidget::customContextMenuRequested, this, [this](QPoint point) {
        auto item = sheet->itemAt(point); if (!item) return;
        if (!item->isSelected()) { sheet->clearSelection(); item->setSelected(true); }
        QMenu menu(sheet); menu.setObjectName("trackingContextMenu");
        auto track = menu.addAction("Track from previous"); auto block = menu.addAction("Block channel / parameter"); auto unblock = menu.addAction("Unblock channel / parameter"); menu.addSeparator(); auto source = menu.addAction("Go to source instruction");
        auto selected = menu.exec(sheet->viewport()->mapToGlobal(point)); if (!selected) return;
        if (selected == source) {
            int col = this->engine.traceSource(this->sequence, item->column(), item->data(Qt::UserRole).toInt());
            if (col >= 0) { sheet->setCurrentCell(item->row(), col); sheet->scrollToItem(sheet->currentItem()); }
            else feedback->setText("No previous instruction · parameter uses its home value");
            return;
        }
        const auto items = sheet->selectedItems();
        for (auto cell : items) {
            int key = cell->data(Qt::UserRole).toInt();
            if (selected == track) this->engine.trackFromPrevious(this->sequence, cell->column(), key, cueOnly->isChecked());
            else this->engine.setChannelBlock(this->sequence, cell->column(), key, selected == block);
        }
        onChange(); populate();
    });
    if (!embedded) {
        auto close = new QDialogButtonBox(QDialogButtonBox::Close); layout->addWidget(close);
        connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }
    reload(sequence);
}
void BlindDialog::reload(int next) {
    sequence = next;
    { QSignalBlocker blocker(sequencePicker); sequencePicker->clear();
        for (int index = 0; index < engine.sequences.size(); ++index) {
            const auto &seq = engine.sequences[index];
            sequencePicker->addItem(QString("Sequence %1 · %2").arg(seq.number).arg(seq.name), index);
        }
        sequencePicker->setCurrentIndex(sequence);
    }
    populate();
}
void BlindDialog::populate() {
    if (sequence < 0 || sequence >= engine.sequences.size()) return;
    QSignalBlocker block(sheet); auto &seq = engine.sequences[sequence];
    QList<int> keys; QSet<int> used;
    if (scope->currentIndex() == 2) for (const auto &c : seq.cues) {
        const auto values = engine.cueValues(c); for (int key : values.keys()) used.insert(key); for (int key : c.blocked.keys()) used.insert(key);
    }
    for (int key : engine.controlIds())
        if (scope->currentIndex() == 0 || (scope->currentIndex() == 1 && engine.selection.contains(engine.channelForControl(key))) || (scope->currentIndex() == 2 && used.contains(key))) keys << key;
    sheet->setRowCount(keys.size()); sheet->setColumnCount(seq.cues.size()); QStringList headers;
    for (const auto &c : seq.cues) headers << "Step " + c.number + " · Cue " + (c.reference.isEmpty() ? c.number : c.reference) + "\n" + engine.cueLabel(c);
    sheet->setHorizontalHeaderLabels(headers); QStringList rows;
    for (int key : keys) rows << engine.controlLabel(key); sheet->setVerticalHeaderLabels(rows);
    for (int col = 0; col < seq.cues.size(); ++col) {
        auto values = engine.tracked(sequence, col); const auto moves = engine.cueValues(seq.cues[col]);
        for (int row = 0; row < keys.size(); ++row) {
            int key = keys[row]; auto item = sheet->item(row, col); if (!item) { item = new QTableWidgetItem; sheet->setItem(row, col, item); }
            const bool blocked = seq.cues[col].blocked.contains(key), move = moves.contains(key), present = values.contains(key);
            item->setText(QString::number(values.value(key, engine.controlDefault(key)), 'g', 6));
            item->setData(Qt::UserRole, key); item->setData(Qt::UserRole + 1, blocked ? "B" : move ? "M" : present ? "T" : "—");
            const int source = engine.traceSource(sequence, col, key);
            item->setToolTip(QString("%1 · %2%3").arg(engine.controlLabel(key), blocked ? "Block" : move ? "Move" : present ? "Tracked" : "Home value",
                source >= 0 ? " · source step " + seq.cues[source].number : ""));
            const auto reference = engine.trackedValue(sequence, col, key);
            if (reference.colorPalette > 0) item->setToolTip(item->toolTip() + QString(" · CP %1 · %2").arg(reference.colorPalette).arg(engine.colorPalette(reference.colorPalette).name));
            item->setForeground(QColor(blocked ? "#e0c998" : move ? "#b7cfc1" : "#909995"));
            item->setTextAlignment(Qt::AlignCenter);
        }
    }
    feedback->setText(QString("Sequence %1 · %2 controls · %3 steps").arg(seq.number).arg(keys.size()).arg(seq.cues.size()));
}
