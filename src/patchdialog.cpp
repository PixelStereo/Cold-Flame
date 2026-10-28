#include "tabledelegate.h"
#include "patchdialog.h"
PatchDialog::PatchDialog(Flame::Engine &e, QWidget *parent, bool embedded, std::function<void()> onApply)
    : QDialog(parent), engine(e), embedded(embedded), onApply(std::move(onApply)) {
    if (embedded) setWindowFlags(Qt::Widget);
    setWindowTitle("Patch · Channel overview");
    setObjectName("patchDialog");
    resize(1340, 1000);
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(embedded ? 6 : 24, embedded ? 6 : 20, embedded ? 6 : 24, embedded ? 6 : 20);
    layout->setSpacing(embedded ? 5 : 12);
    auto title = new QLabel("PATCH  /  CHANNEL OVERVIEW");
    title->setObjectName("title");
    layout->addWidget(title);
    title->setVisible(!embedded);
    summary = new QLabel;
    layout->addWidget(summary);
    auto toolbar = new QHBoxLayout;
    filter = new QLineEdit;
    filter->setPlaceholderText("Filter by channel, label or address…");
    toolbar->addWidget(filter);
    auto hint =
        new QLabel("Double-click a cell to edit  ·  Addressing is 1-based within each universe");
    if (!embedded) toolbar->addWidget(hint);
    else hint->deleteLater();
    layout->addLayout(toolbar);
    if (embedded) {
        auto expand = new QPushButton("Full view…");
        toolbar->addWidget(expand);
        connect(expand, &QPushButton::clicked, this, [this] {
            PatchDialog dialog(engine, this);
            if (dialog.exec() == QDialog::Accepted) {
                reload();
                if (this->onApply) this->onApply();
            }
        });
    }
    auto sheets = new QHBoxLayout;
    layout->addLayout(sheets, 1);
    for (int b = 0; b < 1; ++b) {
        auto table = new QTableWidget(0, 5);
        table->setItemDelegate(new TableDelegate(table));
        table->setObjectName(QString("patchBank%1").arg(b));
        table->setHorizontalHeaderLabels({"CH", "Label", "DMX targets*", "Fixture", "Output"});
        table->verticalHeader()->hide();
        table->verticalHeader()->setMinimumSectionSize(20);
        table->verticalHeader()->setDefaultSectionSize(embedded ? 20 : 22);
        if (embedded) table->setStyleSheet("QTableWidget::item{padding:0px 3px;}");
        table->setAlternatingRowColors(true);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        table->setColumnWidth(1, 110);
        if (embedded) {
            table->hideColumn(3); table->hideColumn(4);
            table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
            table->setColumnWidth(1, 140);
            table->horizontalHeader()->setMinimumSectionSize(35);
        }
        banks << table;
        sheets->addWidget(table);
        connect(table, &QTableWidget::itemChanged, this, [this] { validate(); });
    }
    auto note = new QLabel("* Use universe.address lists: 22.013, 3.018, 3.512. Universes are mapped to network outputs in the Network tab. Fixture support: 8-bit dimmers.");
    note->setWordWrap(true);
    if (!embedded) layout->addWidget(note);
    else { filter->setToolTip(note->text()); note->deleteLater(); }
    auto batchWidget = new QWidget;
    auto batch = new QGridLayout(batchWidget);
    int field = 0;
    auto spin = [&](QString label, int lo, int hi, int value) {
        int col = embedded ? field % 2 : field;
        int row = embedded ? field / 2 : 0;
        batch->addWidget(new QLabel(label), row, col * 2);
        auto s = new QSpinBox;
        s->setRange(lo, hi);
        s->setValue(value);
        batch->addWidget(s, row, col * 2 + 1);
        ++field;
        return s;
    };
    first = spin("First channel", 1, 10000, e.channels.isEmpty() ? 1 : e.channels.lastKey() + 1);
    count = spin("Count", 1, 10000, 1);
    universe = spin("Universe", 1, 32768, 1);
    address = spin("Address", 1, 512, 1);
    auto add = new QPushButton("Add / repatch range");
    batch->addWidget(add, embedded ? 2 : 0, embedded ? 0 : 8, 1, embedded ? 4 : 1);
    layout->addWidget(batchWidget);
    if (embedded) {
        batchWidget->hide();
        auto toggle = new QPushButton("Batch…");
        toolbar->addWidget(toggle);
        connect(toggle, &QPushButton::clicked, batchWidget, [batchWidget] { batchWidget->setVisible(!batchWidget->isVisible()); });
    }
    connect(add, &QPushButton::clicked, this, [this] {
        if (first->value() + count->value() - 1 > 10000 ||
            (universe->value() - 1) * 512 + address->value() + count->value() - 1 > 32768 * 512) {
            error->setText("Range exceeds the supported channel or address limit.");
            return;
        }
        for (int i = 0; i < count->value(); ++i) {
            int id = first->value() + i,
                absolute = (universe->value() - 1) * 512 + address->value() + i;
            bool found = false;
            for (auto table : banks)
                for (int r = 0; r < table->rowCount(); ++r)
                    if (table->item(r, 0)->text().toInt() == id) {
                        QSignalBlocker block(table);
                        table->item(r, 2)->setText(
                            QString("%1.%2")
                                .arg((absolute - 1) / 512 + 1)
                                .arg((absolute - 1) % 512 + 1, 3, 10, QChar('0')));
                        found = true;
                    }
            if (!found)
                appendRow(banks[0], id,
                          QString("Channel %1").arg(id), absolute);
        }
        validate();
    });
    error = new QLabel;
    error->setObjectName("patchError");
    error->setStyleSheet("color:#e0c998;");
    error->setWordWrap(true);
    summary->setWordWrap(true);
    layout->addWidget(error);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText("Apply patch");
    layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Save)->setObjectName("applyPatchButton");
    if (embedded) buttons->button(QDialogButtonBox::Cancel)->setText("Revert draft");
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (apply()) {
            if (this->onApply) this->onApply();
            if (!this->embedded) accept();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, [this] {
        if (this->embedded) reload(); else reject();
    });
    connect(filter, &QLineEdit::textChanged, this, [this](QString text) {
        for (auto table : banks)
            for (int r = 0; r < table->rowCount(); ++r) {
                QString row;
                for (int c = 0; c < 3; ++c)
                    row += table->item(r, c)->text() + " ";
                table->setRowHidden(r, !row.contains(text, Qt::CaseInsensitive));
            }
    });
    populate();
    validate();
}
void PatchDialog::appendRow(QTableWidget *t, int id, QString name, int absolute) {
    QSignalBlocker block(t);
    int r = t->rowCount();
    t->insertRow(r);
    QStringList targets;
    targets << QString("%1.%2")
                   .arg((absolute - 1) / 512 + 1)
                   .arg((absolute - 1) % 512 + 1, 3, 10, QChar('0'));
    for (int a : engine.additionalPatch.value(id))
        targets
            << QString("%1.%2").arg((a - 1) / 512 + 1).arg((a - 1) % 512 + 1, 3, 10, QChar('0'));
    QStringList fields{QString::number(id), name, targets.join(", "), "Dimmer · 8 bit",
                       QString::number(engine.output().value(id), 'f', 0) + " %"};
    for (int c = 0; c < fields.size(); ++c) {
        auto item = new QTableWidgetItem(fields[c]);
        if (c == 0 || c >= 3)
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        t->setItem(r, c, item);
    }
}
void PatchDialog::reload() {
    for (auto table : banks) { QSignalBlocker block(table); table->setRowCount(0); }
    filter->clear();
    populate(); validate();
}
void PatchDialog::populate() {
    for (auto i = engine.channels.begin(); i != engine.channels.end(); ++i)
        appendRow(banks[0], i.key(), i.value(), engine.patch.value(i.key()));
}
static QList<int> parseTargets(QString text, bool &ok) {
    QList<int> result;
    ok = true;
    auto tokens = text.split(',', Qt::KeepEmptyParts);
    for (auto token : tokens) {
        auto m = QRegularExpression("^\\s*(\\d+)\\.(\\d+)\\s*$").match(token);
        int u = m.captured(1).toInt(), a = m.captured(2).toInt();
        if (!m.hasMatch() || u < 1 || u > 32768 || a < 1 || a > 512) {
            ok = false;
            return {};
        }
        result << ((u - 1) * 512 + a);
    }
    return result;
}
void PatchDialog::validate() {
    QMap<int, int> owners;
    QSet<int> ids, universes;
    QStringList errors;
    int rows = 0, outputs = 0;
    for (auto table : banks) {
        QSignalBlocker block(table);
        for (int r = 0; r < table->rowCount(); ++r) {
            ++rows;
            bool valid;
            int id = table->item(r, 0)->text().toInt();
            auto targets = parseTargets(table->item(r, 2)->text(), valid);
            QString issue;
            if (!valid)
                issue = "Use universe.address, separated by commas (e.g. 22.013, 3.018, 3.512)";
            else if (id < 1 || id > 10000 || ids.contains(id))
                issue = "Invalid or duplicate channel ID";
            ids.insert(id);
            for (int a : targets) {
                ++outputs;
                universes.insert((a - 1) / 512 + 1);
                if (owners.contains(a))
                    issue = QString("DMX %1.%2 already assigned to channel %3")
                                .arg((a - 1) / 512 + 1)
                                .arg((a - 1) % 512 + 1)
                                .arg(owners[a]);
                owners[a] = id;
            }
            for (int c = 0; c < 3; ++c)
                table->item(r, c)->setBackground(issue.isEmpty() ? QBrush()
                                                                 : QBrush(QColor("#443c2c")));
            if (!issue.isEmpty())
                errors << issue;
        }
    }
    summary->setText(
        QString("%1 CHANNELS    /    %2 DMX OUTPUTS    /    %3 UNIVERSES    /    %4 ERRORS")
            .arg(rows)
            .arg(outputs)
            .arg(universes.size())
            .arg(errors.size()));
    error->setText(errors.isEmpty()
                       ? (embedded ? "" : "Patch valid · changes apply together when you click Apply patch.")
                       : errors.first());
    error->setProperty("valid", errors.isEmpty());
}
bool PatchDialog::apply() {
    validate();
    if (!error->property("valid").toBool())
        return false;
    QMap<int, QString> names;
    QMap<int, int> patch;
    QMap<int, QList<int>> extra;
    for (auto table : banks)
        for (int r = 0; r < table->rowCount(); ++r) {
            int id = table->item(r, 0)->text().toInt();
            bool valid;
            auto targets = parseTargets(table->item(r, 2)->text(), valid);
            names[id] = table->item(r, 1)->text();
            patch[id] = targets.takeFirst();
            extra[id] = targets;
        }
    for (int id : engine.channels.keys())
        if (!names.contains(id)) {
            error->setText("Existing channel IDs must be preserved");
            return false;
        }
    engine.channels = names;
    engine.patch = patch;
    engine.additionalPatch = extra;
    return true;
}
