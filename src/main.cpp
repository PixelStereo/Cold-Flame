#include "blinddialog.h"
#include "channeldelegate.h"
#include "editing.h"
#include "tabledelegate.h"
#include "engine.h"
#include "gdtf.h"
#include "network.h"
#include "outputpanel.h"
#include "numberdrag.h"
#include "cuelibrary.h"
#include "patchdialog.h"
#include <QtWidgets>
#include <cmath>
using namespace Flame;
class Console : public QMainWindow {
    Engine engine;
    EditHistory history;
    QCheckBox *cueOnly;
    Network network;
    QTimer timer, channelFitTimer;
    QElapsedTimer elapsed;
    QListWidget *channels, *palettes;
    QTabBar *sequences;
    QTabWidget *workspaceTabs;
    QScrollArea *subBank;
    OutputPanel *outputPanel;
    QTabWidget *channelTabs;
    QComboBox *channelPages;
    int channelPage = 0;
    PatchDialog *patchPanel;
    QTableWidget *cues;
    QLabel *playback;
    QProgressBar *progress;
    QLineEdit *command;
    QLabel *status, *selection, *outputStatus;
    CueLibraryPanel *libraryPanel;
    QWidget *sequencePage;
    QLabel *hoverHelp = nullptr;
    QSpinBox *paletteId;
    QSlider *level;
    QString path;
    bool dirty = false;
    int selectedSequence = 0;
    bool pixelRunning = false;
    QList<int> pixelChannels;
    double pixelPhase = 0, pixelRate = 0.25, pixelSpread = 1;
    QPushButton *button(QString text, QLayout *layout, std::function<void()> action,
                        QString style = {}) {
        auto b = new QPushButton(text);
        if (!style.isEmpty())
            b->setStyleSheet(style);
        layout->addWidget(b);
        connect(b, &QPushButton::clicked, this, action);
        return b;
    }
    void changed() {
        history.commit(engine);
        dirty = true;
        setWindowModified(true);
        refresh();
    }
    void message(QString text) { status->setText(text); }
    void refresh() {
        outputStatus->setText(!network.enabled ? "DMX OFF" : !network.lastError.isEmpty() ? "DMX ERROR" : "DMX ON");
        outputStatus->setToolTip(network.lastError);
        auto out = engine.output();
        QSignalBlocker cueBlock(cues);
        // Rendering-only roles must not cause QListView to recompute its icon layout.
        // Structural changes remain in rebuild(), with normal model notifications.
        QSignalBlocker channelModelBlock(channels->model());
        bool repaintChannels = false;
        for (int i = 0; i < channels->count(); ++i) {
            auto item = channels->item(i);
            int id = item->data(Qt::UserRole).toInt();
            double v = out.value(id);
            const auto valueText = QString("%1\n%2\n%3 %")
                                       .arg(id, 2, 10, QChar('0'))
                                       .arg(engine.channels[id])
                                       .arg(v, 0, 'f', 0);
            const bool manual = engine.manual.contains(id);
            const QString flag = engine.parked.contains(id)     ? "PARK"
                                 : engine.captured.contains(id) ? "CAPTURE"
                                                                : "";
            repaintChannels |= item->text() != valueText ||
                               item->data(Qt::UserRole + 1).toDouble() != v ||
                               item->data(Qt::UserRole + 2).toBool() != manual ||
                               item->data(Qt::UserRole + 4).toString() != flag;
            item->setText(valueText);
            item->setData(Qt::UserRole + 1, v);
            item->setData(Qt::UserRole + 2, engine.manual.contains(id));
            item->setData(Qt::UserRole + 4, engine.parked.contains(id)     ? "PARK"
                                            : engine.captured.contains(id) ? "CAPTURE"
                                                                           : "");
            item->setToolTip(QString("Channel %1 · %2").arg(id).arg(engine.channels[id]));
        }
        if (repaintChannels)
            channels->viewport()->update();
        selection->setText(QString("%1 channels selected  •  %2 manual values")
                               .arg(engine.selection.size())
                               .arg(engine.manual.size()));
        auto &s = engine.sequences[selectedSequence];
        cues->setRowCount(s.cues.size());
        for (int i = 0; i < s.cues.size(); ++i) {
            auto c = s.cues[i];
            QStringList vals{c.number,
                             engine.cueLabel(c),
                             QString::number(c.time, 'g', 5),
                             c.down < 0 ? "= Up" : QString::number(c.down, 'g', 5),
                             QString::number(c.wait, 'g', 5),
                             c.link,
                             QString::number(engine.cueValues(c).size()),
                             QString::number(c.channelTimes.size()),
                             i == s.current ? (s.paused                 ? "PAUSED"
                                               : s.elapsed < s.duration ? "FADING"
                                                                        : "ACTIVE")
                                            : (i == engine.nextCueIndex(selectedSequence) ? "NEXT" : "")};
            QStringList flags;
            if (c.follow)
                flags << "F" + QString::number(*c.follow);
            if (c.hang)
                flags << "H" + QString::number(*c.hang);
            if (!c.execute.isEmpty())
                flags << "Exec " + c.execute;
            if (!c.after.isEmpty())
                flags << "After " + c.after;
            if (c.block)
                flags << "B";
            if (c.asserted)
                flags << "A";
            if (!flags.isEmpty())
                vals[8] += " · " + flags.join(" · ");
            vals << (c.reference.isEmpty() ? c.number + " (inline)" : c.reference);
            for (int j = 0; j < vals.size(); ++j) {
                const int column = j == 9 ? 1 : j == 0 ? 0 : j + 1;
                auto item = cues->item(i, column);
                if (!item) {
                    item = new QTableWidgetItem;
                    cues->setItem(i, column, item);
                }
                item->setToolTip(QString("Follow: %1 · Hang: %2\nExecute: %3\nAfter cue: %4")
                                     .arg(c.follow ? QString::number(*c.follow) : "—",
                                          c.hang ? QString::number(*c.hang) : "—", c.execute,
                                          c.after));
                if (j >= 6 || j == 0)
                    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                if (item->text() != vals[j])
                    item->setText(vals[j]);
                item->setForeground(QColor(i == s.current ? "#c2e5d3" : "#e5e5e5"));
            }
        }
        QString current = s.current >= 0
                              ? "Step " + s.cues[s.current].number + " · Cue " +
                                (s.cues[s.current].reference.isEmpty() ? s.cues[s.current].number : s.cues[s.current].reference) + " / " + engine.cueLabel(s.cues[s.current])
                              : "Standby";
        int nextIndex = engine.nextCueIndex(selectedSequence);
        QString next = nextIndex >= 0 ? "Step " + s.cues[nextIndex].number + " · Cue " +
            (s.cues[nextIndex].reference.isEmpty() ? s.cues[nextIndex].number : s.cues[nextIndex].reference) : "—";
        if (s.pendingCue.isEmpty() && s.current >= 0 && !s.cues[s.current].link.isEmpty())
            next = "Step " + s.cues[s.current].link;

        playback->setText(
            QString("SEQ %1   /   %2     →     NEXT %3").arg(s.number).arg(current, next));
        if (engine.linkedPlaybacks.contains(selectedSequence))
            playback->setText(playback->text() + "   ·   " +
                              engine.linkedPlaybacks[selectedSequence].name);
        progress->setValue(s.duration <= 0 ? 0 : qMin(1000, qRound(1000 * s.elapsed / s.duration)));
    }
    void rebuild() {
        QSignalBlocker channelBlock(channels);
        channels->clear();
        QSignalBlocker pageBlock(channelPages);
        const int pages = engine.channels.isEmpty() ? 1 : (engine.channels.lastKey() - 1) / Engine::ChannelsPerPage + 1;
        channelPage = qBound(0, channelPage, pages - 1);
        channelPages->clear();
        for (int page = 0; page < pages; ++page)
            channelPages->addItem(QString("%1–%2").arg(page * Engine::ChannelsPerPage + 1).arg((page + 1) * Engine::ChannelsPerPage));
        channelPages->setCurrentIndex(channelPage);
        for (auto i = engine.channels.begin(); i != engine.channels.end(); ++i) {
            if ((i.key() - 1) / Engine::ChannelsPerPage != channelPage) continue;
            auto item = new QListWidgetItem;
            item->setData(Qt::UserRole, i.key());
            item->setToolTip(i.value());
            item->setTextAlignment(Qt::AlignCenter);
            channels->addItem(item);
            item->setSelected(engine.selection.contains(i.key()));
        }
        QSignalBlocker tabsBlock(sequences);
        QList<int> order;
        for (int i = 0; i < sequences->count(); ++i)
            order << sequences->tabText(i).section(" · ", 0, 0).toInt();
        while (sequences->count()) sequences->removeTab(0);
        for (const auto &seq : engine.sequences)
            if (!order.contains(seq.number)) order << seq.number;
        int activeTab = 0;
        selectedSequence = qBound(0, selectedSequence, int(engine.sequences.size()) - 1);
        for (int number : order)
            for (int i = 0; i < engine.sequences.size(); ++i)
                if (engine.sequences[i].number == number) {
                    int tab = sequences->addTab(QString("%1 · %2").arg(number).arg(engine.sequences[i].name));
                    sequences->setTabData(tab, i);
                    if (i == selectedSequence) activeTab = tab;
                }
        sequences->setCurrentIndex(activeTab);
        rebuildSubmasters();
        palettes->clear();
        for (auto i = engine.palettes.begin(); i != engine.palettes.end(); ++i) {
            auto item =
                new QListWidgetItem(QString("IP %1\n%2 channels").arg(i.key()).arg(i->size()));
            item->setData(Qt::UserRole, i.key());
            palettes->addItem(item);
        }
        libraryPanel->reload();
        fitChannels();
        refresh();
    }
    void record(bool manualOnly = false) {
        const QString number = engine.nextStoredCueNumber();
        if (!engine.storeCue(number, {}, manualOnly)) { message("Cue number limit reached"); return; }
        changed();
        libraryPanel->reload(number);
        workspaceTabs->setCurrentWidget(libraryPanel);
        message("Stored cue " + number + " · select Insert into sequence to create a step");
    }
    void updateStep() {
        int index = cues->currentRow();
        if (index < 0) return;
        auto step = engine.sequences[selectedSequence].cues[index];
        if (step.reference.isEmpty())
            engine.record(selectedSequence, step.number, step.label, step.time, cueOnly->isChecked());
        else if (cueOnly->isChecked()) {
            for (auto it = engine.manual.begin(); it != engine.manual.end(); ++it)
                engine.editBlind(selectedSequence, index, it.key(), it.value(), true);
        } else {
            engine.updateStoredCue(step.reference, engine.manual);
        }
        changed(); libraryPanel->reload(step.reference);
    }
    void insertCue(QString number, int position = -1) {
        int row = engine.insertStoredCue(selectedSequence, number, position);
        if (row < 0) return;
        changed(); libraryPanel->reload(number); cues->selectRow(row);
        workspaceTabs->setCurrentWidget(sequencePage);
        message(QString("Inserted cue %1 as step %2").arg(number, engine.sequences[selectedSequence].cues[row].number));
    }
    void chooseCue() {
        if (engine.cueLibrary.isEmpty()) { workspaceTabs->setCurrentWidget(libraryPanel); message("Record a cue in the library first"); return; }
        QStringList choices;
        auto numbers = engine.cueLibrary.keys();
        std::sort(numbers.begin(), numbers.end(), [](QString a, QString b) { return a.toDouble() < b.toDouble(); });
        for (const auto &number : numbers) choices << number + " · " + engine.cueLibrary[number].label;
        bool ok;
        QString choice = QInputDialog::getItem(this, "Insert stored cue", "Cue · inserted before selection, or appended if no step selected", choices, 0, false, &ok);
        if (ok) insertCue(choice.section(" · ", 0, 0), cues->currentRow());
    }
    void linkStep() {
        const int row = cues->currentRow(); if (row < 0) return;
        QStringList labels{"No link"}, targets{QString()};
        for (const auto &seq : engine.sequences)
            for (const auto &step : seq.cues) {
                const QString target = QString::number(seq.number) + "/" + step.number;
                labels << QString("Sequence %1 · Step %2 · Cue %3 · %4").arg(seq.number).arg(step.number,
                    step.reference.isEmpty() ? step.number : step.reference, engine.cueLabel(step));
                targets << target;
            }
        bool ok;
        auto current = engine.sequences[selectedSequence].cues[row].link;
        if (!current.isEmpty() && !current.contains('/')) current = QString::number(engine.sequences[selectedSequence].number) + "/" + current;
        QString chosen = QInputDialog::getItem(this, "Link to sequence step", "Choose the exact occurrence", labels, qMax(0, targets.indexOf(current)), false, &ok);
        if (ok) { engine.sequences[selectedSequence].cues[row].link = targets[labels.indexOf(chosen)]; changed(); }
    }
    void fitChannels() {
        if (!channels)
            return;
        const auto size = channels->viewport()->size();
        int columns = qMax(1, size.width() / 64);
        int fittingColumns = (channels->count() + qMax(1, size.height() / 38) - 1) / qMax(1, size.height() / 38);
        columns = qMax(columns, qMin(qMax(1, size.width() / 54), fittingColumns));
        const QSize cell(qMax(52, (size.width() - 6) / columns), 38);
        if (cell != channels->gridSize())
            channels->setGridSize(cell);
    }
    void channelTiming() {
        int row = cues->currentRow();
        if (row < 0 || engine.selection.isEmpty()) {
            message("Select a cue and channels to assign individual timing");
            return;
        }
        auto &cue = engine.sequences[selectedSequence].cues[row];
        QDialog d(this);
        d.setWindowTitle("Selected channels · individual timing");
        QFormLayout form(&d);
        QDoubleSpinBox up, down, wait;
        for (auto spin : {&up, &down, &wait}) {
            spin->setRange(0, 3600);
            spin->setDecimals(2);
            spin->setSuffix(" s");
        }
        auto timing = cue.channelTimes.value(
            *engine.selection.begin(),
            ChannelTiming{cue.time, cue.down < 0 ? cue.time : cue.down, cue.wait});
        up.setValue(timing.up);
        down.setValue(timing.down);
        wait.setValue(timing.wait);
        form.addRow("Up", &up);
        form.addRow("Down", &down);
        form.addRow("Wait before fade", &wait);
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        auto reset = buttons.addButton("Use cue timing", QDialogButtonBox::ResetRole);
        form.addRow(&buttons);
        connect(&buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(&buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        connect(reset, &QPushButton::clicked, &d, [&] {
            for (int c : engine.selection)
                cue.channelTimes.remove(c);
            changed();
            d.reject();
        });
        if (d.exec() == QDialog::Accepted) {
            for (int c : engine.selection)
                cue.channelTimes[c] = {up.value(), down.value(), wait.value()};
            changed();
        }
    }
    void cueTriggers() {
        int row = cues->currentRow();
        if (row < 0) {
            message("Select a cue first");
            return;
        }
        auto &cue = engine.sequences[selectedSequence].cues[row];
        QDialog d(this);
        d.setWindowTitle("Cue triggers · Link / Follow / Hang / Execute");
        QFormLayout form(&d);
        QLineEdit follow(cue.follow ? QString::number(*cue.follow) : ""),
            hang(cue.hang ? QString::number(*cue.hang) : ""), execute(cue.execute),
            after(cue.after);
        form.addRow("Follow · seconds from start", &follow);
        form.addRow("Hang · seconds after completion", &hang);
        form.addRow("Execute · step or sequence/step at start", &execute);
        form.addRow("After · step at completion (extension)", &after);
        auto info = new QLabel(
            "Follow and Hang are mutually exclusive. Execute uses the destination sequence. After "
            "cue leaves the main cursor unchanged; it is a Cold Flame extension, not Eos Link.");
        info->setWordWrap(true);
        form.addRow(info);
        QLabel error;
        error.setWordWrap(true);
        form.addRow(&error);
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form.addRow(&buttons);
        connect(&buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        connect(&buttons, &QDialogButtonBox::accepted, &d, [&] {
            if (!follow.text().isEmpty() && !hang.text().isEmpty()) {
                error.setText("Use Follow or Hang, not both.");
                return;
            }
            std::optional<double> f, h;
            for (auto pair : {qMakePair(&follow, &f), qMakePair(&hang, &h)})
                if (!pair.first->text().isEmpty()) {
                    bool ok;
                    double n = pair.first->text().toDouble(&ok);
                    if (!ok || !std::isfinite(n) || (pair.first == &follow && n < 0)) {
                        error.setText("Invalid timing value");
                        return;
                    }
                    *pair.second = n;
                }
            for (auto target : {execute.text(), after.text()})
                if (!target.isEmpty()) {
                    auto parts = target.split('/');
                    int seq = selectedSequence;
                    QString number = target;
                    if (parts.size() == 2) {
                        seq = -1;
                        number = parts[1];
                        for (int i = 0; i < engine.sequences.size(); ++i)
                            if (engine.sequences[i].number == parts[0].toInt())
                                seq = i;
                    }
                    bool found = false;
                    if (seq >= 0)
                        for (const auto &c : engine.sequences[seq].cues)
                            if (c.number == number)
                                found = true;
                    if (!found) {
                        error.setText("Trigger target does not exist");
                        return;
                    }
                }
            cue.follow = f;
            cue.hang = h;
            cue.execute = execute.text();
            cue.after = after.text();
            changed();
            d.accept();
        });
        d.exec();
    }
    void editSubmaster(int index) {
        QDialog dialog(this);
        dialog.setWindowTitle("Edit submaster · stored intensities");
        dialog.resize(520, 640);
        QVBoxLayout layout(&dialog);
        QLineEdit name(engine.submasters[index].name);
        layout.addWidget(new QLabel("Name"));
        layout.addWidget(&name);
        QTableWidget table(engine.channels.size(), 3);
        table.setObjectName("submasterEditor");
        table.setItemDelegate(new TableDelegate(&table));
        table.setHorizontalHeaderLabels({"Include", "Channel", "Level %"});
        table.horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table.verticalHeader()->setDefaultSectionSize(30);
        int row = 0;
        for (auto it = engine.channels.begin(); it != engine.channels.end(); ++it, ++row) {
            auto include = new QTableWidgetItem;
            include->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            include->setCheckState(engine.submasters[index].values.contains(it.key()) ? Qt::Checked : Qt::Unchecked);
            include->setData(Qt::UserRole, it.key());
            table.setItem(row, 0, include);
            auto channel = new QTableWidgetItem(QString("%1 · %2").arg(it.key()).arg(it.value()));
            channel->setFlags(Qt::ItemIsEnabled);
            table.setItem(row, 1, channel);
            table.setItem(row, 2, new QTableWidgetItem(QString::number(engine.submasters[index].values.value(it.key()))));
        }
        layout.addWidget(&table);
        QLabel error;
        layout.addWidget(&error);
        QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        layout.addWidget(&buttons);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            Values values;
            for (int r = 0; r < table.rowCount(); ++r) {
                if (table.item(r, 0)->checkState() != Qt::Checked) continue;
                bool ok;
                double value = table.item(r, 2)->text().toDouble(&ok);
                if (!ok || !std::isfinite(value) || value < 0 || value > 100) {
                    error.setText("Included levels must be between 0 and 100.");
                    return;
                }
                values[table.item(r, 0)->data(Qt::UserRole).toInt()] = value;
            }
            engine.submasters[index].name = name.text().trimmed();
            engine.submasters[index].values = values;
            changed();
            dialog.accept();
        });
        if (dialog.exec() == QDialog::Accepted) rebuildSubmasters();
    }
    void rebuildSubmasters() {
        auto content = new QWidget;
        auto grid = new QGridLayout(content);
        grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        for (int i = 0; i < engine.submasters.size(); ++i) {
            const auto &sub = engine.submasters[i];
            auto card = new QGroupBox(QString("SUB %1 · %2").arg(sub.number).arg(sub.name));
            card->setMinimumWidth(220);
            auto layout = new QVBoxLayout(card);
            layout->addWidget(new QLabel(QString("HTP intensity · %1 channels").arg(sub.values.size())));
            auto value = new QLabel(QString::number(qRound(sub.level * 100)) + " %");
            layout->addWidget(value);
            auto fader = new QSlider(Qt::Horizontal);
            fader->setObjectName(QString("subFader%1").arg(sub.number));
            fader->setRange(0, 100);
            fader->setValue(qRound(sub.level * 100));
            layout->addWidget(fader);
            connect(fader, &QSlider::valueChanged, this, [this, i, value](int v) {
                engine.submasters[i].level = v / 100.;
                value->setText(QString::number(v) + " %");
                refresh();
            });
            auto controls = new QHBoxLayout;
            button("Out", controls, [fader] { fader->setValue(0); });
            button("Full", controls, [fader] { fader->setValue(100); });
            layout->addLayout(controls);
            auto edit = new QHBoxLayout;
            button("Edit…", edit, [this, i] { editSubmaster(i); });
            button("Update", edit, [this, i] {
                // Updating stored content must not record this submaster's own contribution.
                const double level = engine.submasters[i].level;
                engine.submasters[i].level = 0;
                auto live = engine.output(false, false);
                engine.submasters[i].level = level;
                Values values;
                if (engine.selection.isEmpty()) values = live;
                else for (int ch : engine.selection) values[ch] = live.value(ch);
                engine.submasters[i].values = values;
                changed();
                QTimer::singleShot(0, this, [this] { rebuildSubmasters(); });
                message("Submaster updated from live output · selected channels, or all if none selected");
            });
            layout->addLayout(edit);
            button("Delete", layout, [this, i] {
                engine.submasters.removeAt(i);
                changed();
                QTimer::singleShot(0, this, [this] { rebuildSubmasters(); });
            });
            grid->addWidget(card, i / 2, i % 2);
        }
        if (engine.submasters.isEmpty())
            grid->addWidget(new QLabel("No submasters yet. Set a look, then Record submaster."), 0, 0);
        if (auto previous = subBank->takeWidget()) previous->deleteLater();
        subBank->setWidget(content);
    }
    bool save() {
        if (path.isEmpty())
            path = QFileDialog::getSaveFileName(this, "Save show", {}, "Show (*.cfl)");
        if (path.isEmpty())
            return false;
        QSaveFile f(path);
        auto show = engine.json();
        show["outputs"] = network.outputJson();
        if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(show).toJson()) < 0 ||
            !f.commit()) {
            message("Save failed: " + f.errorString());
            return false;
        }
        dirty = false;
        setWindowModified(false);
        message("Show saved: " + path);
        return true;
    }
    bool discard() {
        if (!dirty)
            return true;
        auto a =
            QMessageBox::question(this, "Unsaved show", "Save changes?",
                                  QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        return a == QMessageBox::Discard || (a == QMessageBox::Save && save());
    }
    bool loadShow(const QJsonObject &show, QString *error) {
        Engine next;
        Network validation;
        if (!next.load(show, error) || !validation.loadOutputs(show["outputs"].toArray(), error))
            return false;
        engine = next;
        network.applyOutputs(validation.outputs);
        outputPanel->reload();
        patchPanel->reload();
        return true;
    }
    void open() {
        if (!discard())
            return;
        QString p = QFileDialog::getOpenFileName(this, "Open", {}, "Show (*.cfl)");
        if (p.isEmpty())
            return;
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly)) {
            message(f.errorString());
            return;
        }
        QJsonParseError parse;
        auto doc = QJsonDocument::fromJson(f.readAll(), &parse);
        QString error;
        if (parse.error != QJsonParseError::NoError || !loadShow(doc.object(), &error)) {
            message("Cannot open show: " + error);
            return;
        }
        pixelRunning = false;
        path = p;
        dirty = false;
        setWindowModified(false);
        selectedSequence = 0;
        channelPage = 0;
        history.reset(engine);
        rebuild();
        message("Show loaded · outputs disabled");
    }
    void outputSettings() { channelTabs->setCurrentWidget(outputPanel); }
    void patch() { channelTabs->setCurrentWidget(patchPanel); }

  protected:
    QString controlHelp(QWidget *widget) const {
        if (!widget->statusTip().isEmpty()) return widget->statusTip();
        if (!widget->toolTip().isEmpty()) return widget->toolTip();
        auto button = qobject_cast<QAbstractButton *>(widget);
        if (!button) return {};
        const QString text = button->text().remove('&');
        static const QMap<QString, QString> descriptions{
            {"RECORD", "Store the live look in the cue library. Insert it into a sequence separately."},
            {"Record Only", "Store manual values as a reusable cue, leaving other channels unstored."},
            {"UPDATE", "Update the selected step's shared cue from manual values. Cue Only makes a local step edit."},
            {"SET NEXT", "Queue the selected step for the next GO without changing live output or stored links."},
            {"GO TO", "Play the selected step now, including its tracked values."},
            {"Insert cue…", "Insert a stored cue before the selected step, or append if no step is selected."},
            {"Cue library", "Browse, edit and reuse stored cues independently of sequence order."},
            {"Link to step…", "Choose the exact sequence and step to play next; repeated cues have different step IDs."},
            {"↑", "Move the selected step earlier. Its ID and all links remain unchanged."},
            {"↓", "Move the selected step later. Its ID and all links remain unchanged."},
            {"Remove step", "Remove this occurrence from the sequence. Its stored cue remains in the library."},
            {"▶  GO", "Play the pending step of the selected sequence. Shortcut: Ctrl+Space."},
            {"↶ BACK", "Play the preceding step of the selected sequence."},
            {"Ⅱ PAUSE / RESUME", "Pause or resume the selected sequence's fade."},
            {"RELEASE SEQUENCE", "Release this sequence's output so other sources can contribute."},
            {"RELEASE MANUAL", "Release manual values except captured channels, revealing playback and submasters."},
            {"CAPTURE", "Protect selected manual channels from playback changes. Click again to release capture."},
            {"PARK", "Lock selected channel outputs. Park bypasses master and blackout and is excluded from recording."},
            {"UNPARK", "Remove Park from selected channels."},
            {"All", "Select all channels on the current page. Selection on other pages is retained."},
            {"+ Page", "Add the next 120 channels and assign unused DMX addresses."},
            {"0", "Set selected channels to zero."}, {"FULL", "Set selected channels to 100 percent."},
            {"BLACKOUT", "Toggle blackout. Parked channels retain their parked output."},
            {"EXECUTE", "Run the command entered in the command line."},
            {"+", "Create a new sequence."}, {"−", "Delete the selected sequence. At least one sequence must remain."},
            {"Priority…", "Set playback priority: 1 is lowest, 10 is highest."},
            {"BLIND", "Edit sequence step intensities without recalling them live. Referenced cues receive local overrides."},
            {"Block", "Protect this step's tracked values against upstream edits."},
            {"Assert", "Make this step take back its tracked channel values when played."},
            {"Channel timing…", "Set individual up, down and delay times for selected channels in this step."},
            {"Triggers…", "Edit Follow, Hang, Execute and completion triggers for this step."},
            {"+ Record submaster", "Record selected channels, or all live channels if none are selected, into a new HTP submaster."},
            {"Out", "Set this submaster's fader to zero."}, {"Full", "Set this submaster's fader to 100 percent."},
            {"Edit…", "Edit this submaster's name, channel membership and stored levels."},
            {"Update", "Replace submaster content from selected live channels, excluding its own contribution."},
            {"Delete", "Delete this submaster and remove its live contribution. Show edit Undo restores its definition."},
            {"Record palette", "Store selected channels in the chosen intensity palette."},
            {"Apply palette", "Apply the chosen intensity palette to selected channels by reference."},
            {"+ Output", "Add a disabled DMX output draft. Configure it, enable On, then Apply outputs."},
            {"− Output", "Remove the selected output from the draft. Apply outputs to stop and remove it."},
            {"Apply outputs", "Validate and activate all output settings. Enabled routes start sending DMX."},
            {"Stop all", "Stop every DMX route immediately and terminate active sACN streams."},
            {"Batch…", "Show or hide controls for adding and repatching a channel range."},
            {"Full view…", "Open the patch in a larger editor."},
            {"Apply patch", "Validate and apply all patch changes together."},
            {"Revert draft", "Discard unapplied patch edits and restore the current patch."},
            {"Add / repatch range", "Add or repatch the specified channel range in the patch draft."},
            {"Close", "Close this editor. Changes already saved in Blind remain stored."},
            {"Save", "Validate and save this editor's changes."}, {"Cancel", "Close this editor without applying its changes."},
            {"OK", "Apply this editor's settings."}, {"Use cue timing", "Remove selected channels' timing overrides."}
        };
        if (descriptions.contains(text)) return descriptions.value(text);
        if (button->isCheckable()) return "Toggle " + text + ".";
        return "Activate " + text + ".";
    }
    bool eventFilter(QObject *o, QEvent *e) override {
        auto widget = qobject_cast<QWidget *>(o);
        const bool owned = widget && (widget == this || isAncestorOf(widget));
        if (owned && e->type() == QEvent::Polish)
            if (auto spin = qobject_cast<QAbstractSpinBox *>(widget)) enableNumberDrag(spin);
        if (owned && hoverHelp) {
            if (o == channels->viewport() && e->type() == QEvent::MouseMove) {
                auto event = static_cast<QMouseEvent *>(e);
                auto item = channels->itemAt(event->position().toPoint());
                if (item) hoverHelp->setText(item->toolTip());
            }
            if (e->type() == QEvent::ToolTip) {
                QString help = controlHelp(widget);
                if (auto view = qobject_cast<QAbstractItemView *>(widget->parentWidget())) {
                    const auto index = view->indexAt(static_cast<QHelpEvent *>(e)->pos());
                    if (index.isValid()) help = index.data(Qt::ToolTipRole).toString().replace('\n', " · ");
                }
                if (!help.isEmpty()) hoverHelp->setText(help);
                return true; // Context help belongs in the footer, never in floating bubbles.
            }
            if (e->type() == QEvent::Enter || e->type() == QEvent::FocusIn) {
                const QString help = controlHelp(widget);
                if (!help.isEmpty()) hoverHelp->setText(help);
            } else if (e->type() == QEvent::Leave && qobject_cast<QAbstractButton *>(widget))
                hoverHelp->clear();
        }
        if (channels && o == channels && e->type() == QEvent::Resize)
            channelFitTimer.start(0);
        return QMainWindow::eventFilter(o, e);
    }
    void closeEvent(QCloseEvent *e) override {
        if (discard())
            e->accept();
        else
            e->ignore();
    }

  public:
    ~Console() override { qApp->removeEventFilter(this); }
    Console(QString showFile = {}) {
        setWindowTitle("COLD FLAME[*] — Lighting workspace");
        resize(1440, 940);
        setMinimumSize(1100, 720);
        setStyleSheet(R"(
 QWidget{background:#171819;color:#e5e5e5;font:12px 'DejaVu Sans';}
 QLabel{background:transparent;}
 QLabel#title{font-size:23px;font-weight:700;letter-spacing:2px;color:#fafafa;}
 QLabel#section{color:#96999d;font-size:11px;font-weight:700;letter-spacing:1px;padding:5px 0;}
 QLabel#playback{background:#242628;border-left:3px solid #b7cfc1;padding:14px;color:#c2e5d3;font-size:15px;font-weight:600;}
 QPushButton{background:#292c2e;border:1px solid #404346;border-radius:4px;padding:9px 13px;}
 QPushButton:hover{background:#35393b;border-color:#9cb8af;} QPushButton:checked{background:#314c44;}
 QLineEdit,QDoubleSpinBox,QSpinBox,QComboBox{background:#222426;border:1px solid #393c3f;border-radius:3px;padding:7px;selection-background-color:#3c665e;}
 QTabBar::tab{background:#292c2e;padding:9px;border:1px solid #404346;} QTabBar::tab:selected{background:#303a36;color:#f3fff9;}
 QCheckBox::indicator,QAbstractItemView::indicator{width:13px;height:13px;border:1px solid #6e9b8c;border-radius:2px;background:#222426;}
 QCheckBox::indicator:checked,QAbstractItemView::indicator:checked{background:#b7cfc1;border-color:#b7cfc1;}
 QGroupBox{border:1px solid #404346;border-radius:5px;margin-top:12px;padding-top:15px;}
 QListWidget,QTableWidget{background:#1d1f20;alternate-background-color:#202223;border:1px solid #36393b;border-radius:4px;outline:0;}
 QListWidget::item{padding:10px;border-bottom:1px solid #303234;} QListWidget::item:selected{background:#303a36;color:#f3fff9;}
 QTableWidget::item{padding:4px;border-bottom:1px solid #303234;} QTableWidget::item:selected{background:#303a36;color:#f3fff9;}
 QHeaderView::section{background:#303234;color:#b7babe;border:0;border-bottom:1px solid #404346;padding:9px 5px;font-size:10px;font-weight:700;}
 QTableWidget{gridline-color:#303234;background:#000;} QMenuBar,QMenu{background:#242628;} QMenu::item:selected{background:#3c4842;}
 QSlider::groove:horizontal{height:5px;background:#383d3a;} QSlider::handle:horizontal{background:#b7cfc1;width:15px;margin:-6px 0;border-radius:3px;}
 QProgressBar{border:0;background:#232725;max-height:4px;} QProgressBar::chunk{background:#b7cfc1;} QScrollBar:vertical{background:#1d1f20;width:10px;margin:0;} QScrollBar::handle:vertical{background:#393c3f;min-height:24px;border-radius:4px;} QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;} QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:none;}
 )");
        auto editMenu = menuBar()->addMenu("Edit");
        editMenu
            ->addAction("Undo show edit", this,
                        [this] {
                            if (history.undo(engine)) {
                                dirty = true;
                                setWindowModified(true);
                                patchPanel->reload();
                                rebuild();
                                message("Show edit undone · live transport preserved");
                            }
                        })
            ->setShortcut(QKeySequence::Undo);
        editMenu
            ->addAction("Redo show edit", this,
                        [this] {
                            if (history.redo(engine)) {
                                dirty = true;
                                setWindowModified(true);
                                patchPanel->reload();
                                rebuild();
                                message("Show edit restored · live transport preserved");
                            }
                        })
            ->setShortcut(QKeySequence::Redo);
        auto file = menuBar()->addMenu("Show");
        file->addAction("Open…", this, [this] { open(); })->setShortcut(QKeySequence::Open);
        file->addAction("Save", this, [this] { save(); })->setShortcut(QKeySequence::Save);
        file->addAction("Save as…", this, [this] {
            QString old = path;
            path.clear();
            if (!save())
                path = old;
        });
        file->addAction("Patch…", this, [this] { patch(); });
        file->addAction("Network outputs…", this, [this] { outputSettings(); });
        auto root = new QWidget;
        setCentralWidget(root);
        auto layout = new QVBoxLayout(root);
        layout->setContentsMargins(20, 16, 20, 14);
        layout->setSpacing(12);
        auto body = new QHBoxLayout;
        body->setSpacing(16);
        layout->addLayout(body, 1);
        auto label = [](QString text) {
            auto l = new QLabel(text);
            l->setObjectName("section");
            return l;
        };
        auto workspace = new QVBoxLayout;
        workspace->setSpacing(12);
        body->addLayout(workspace, 1);
        playback = new QLabel;
        playback->setObjectName("playback");
        workspace->addWidget(playback);
        progress = new QProgressBar;
        progress->setRange(0, 1000);
        progress->setTextVisible(false);
        workspace->addWidget(progress);
        auto split = new QSplitter(Qt::Horizontal);
        workspace->addWidget(split, 1);
        auto stage = new QWidget;
        auto stageLayout = new QVBoxLayout(stage);
        stageLayout->setContentsMargins(0, 0, 8, 0);
        auto bar = new QHBoxLayout;
        bar->addWidget(label("CHANNELS · LIVE OUTPUT"));
        bar->addStretch();
        channelPages = new QComboBox;
        channelPages->setObjectName("channelPages");
        bar->addWidget(channelPages);
        connect(channelPages, &QComboBox::currentIndexChanged, this, [this](int page) {
            if (page < 0) return;
            channelPage = page; rebuild();
        });
        button("+ Page", bar, [this] {
            int page = engine.addChannelPage();
            if (page < 0) { message("Channel limit reached"); return; }
            channelPage = page; changed(); rebuild(); patchPanel->reload();
        })->setObjectName("addChannelPageButton");
        button("All", bar, [this] { channels->selectAll(); });
        stageLayout->addLayout(bar);
        channels = new QListWidget;
        channels->setObjectName("channelList");
        channels->setViewMode(QListView::IconMode);
        channels->setResizeMode(QListView::Fixed);
        channels->setUniformItemSizes(true);
        // A permanent gutter prevents scrollbar visibility from changing available width.
        channels->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        channels->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        channels->setMovement(QListView::Static);
        channels->setSpacing(0);
        channels->setItemDelegate(new ChannelDelegate(channels));
        channels->setSelectionMode(QAbstractItemView::ExtendedSelection);
        channels->installEventFilter(this);
        channels->setMouseTracking(true);
        channelFitTimer.setSingleShot(true);
        connect(&channelFitTimer, &QTimer::timeout, this, [this] { fitChannels(); });
        stageLayout->addWidget(channels, 1);
        selection = new QLabel;
        stageLayout->addWidget(selection);
        auto overrides = new QHBoxLayout;
        button("CAPTURE", overrides, [this] {
            engine.toggleCapture();
            refresh();
        });
        button("PARK", overrides, [this] {
            engine.parkSelection(true);
            refresh();
        });
        button("UNPARK", overrides, [this] {
            engine.parkSelection(false);
            refresh();
        });
        stageLayout->addLayout(overrides);
        auto levels = new QHBoxLayout;
        levels->addWidget(label("LEVEL"));
        level = new QSlider(Qt::Horizontal);
        level->setRange(0, 100);
        levels->addWidget(level, 1);
        button("0", levels, [this] {
            engine.level(0);
            refresh();
        });
        button("FULL", levels, [this] {
            engine.level(100);
            refresh();
        });
        stageLayout->addLayout(levels);
        button("RELEASE MANUAL", stageLayout, [this] {
            for (auto it = engine.manual.begin(); it != engine.manual.end();)
                if (!engine.captured.contains(it.key()))
                    it = engine.manual.erase(it);
                else
                    ++it;
            refresh();
        });
        channelTabs = new QTabWidget;
        channelTabs->setObjectName("channelTabs");
        channelTabs->setMovable(true);
        channelTabs->addTab(stage, "Channels");
        patchPanel = new PatchDialog(engine, channelTabs, true, [this] {
            changed(); rebuild(); message("Patch applied");
        });
        channelTabs->addTab(patchPanel, "Patch");
        split->addWidget(channelTabs);
        auto cuePanel = new QWidget;
        auto cueLayout = new QVBoxLayout(cuePanel);
        cueLayout->setContentsMargins(8, 0, 0, 0);
        workspaceTabs = new QTabWidget;
        workspaceTabs->setObjectName("workspaceTabs");
        workspaceTabs->setMovable(true);
        cueLayout->addWidget(workspaceTabs, 1);
        sequencePage = new QWidget;
        auto sequenceLayout = new QVBoxLayout(sequencePage);
        workspaceTabs->addTab(sequencePage, "Sequences");
        auto sequenceTools = new QHBoxLayout;
        sequences = new QTabBar;
        sequences->setObjectName("sequenceTabs");
        sequences->setMovable(true);
        sequences->setExpanding(false);
        sequenceTools->addWidget(sequences, 1);
        button("+", sequenceTools, [this] {
            selectedSequence = engine.addSequence();
            changed();
            rebuild();
        })->setObjectName("newSequenceButton");
        button("−", sequenceTools, [this] {
            if (engine.removeSequence(selectedSequence)) {
                selectedSequence = qMin(selectedSequence, int(engine.sequences.size()) - 1);
                changed();
                rebuild();
            } else message("Keep at least one sequence. Delete its cues to clear it.");
        })->setObjectName("deleteSequenceButton");
        sequenceLayout->addLayout(sequenceTools);
        button("Priority…", sequenceTools, [this] {
            bool ok;
            int priority = QInputDialog::getInt(this, "Sequence priority", "1 = lowest / 10 = highest",
                engine.sequences[selectedSequence].priority, 1, 10, 1, &ok);
            if (ok) { engine.sequences[selectedSequence].priority = priority; changed(); }
        });
        cues = new QTableWidget(0, 10);
        cues->setObjectName("cueSheet");
        cues->setItemDelegate(new TableDelegate(cues));
        cues->verticalHeader()->setDefaultSectionSize(32);
        cues->setHorizontalHeaderLabels(
            {"STEP", "CUE", "NAME", "UP s", "DOWN s", "WAIT s", "LINK STEP", "VALUES", "INDIV.", "STATE"});
        cues->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        cues->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        cues->verticalHeader()->hide();
        cues->setAlternatingRowColors(true);
        cues->setSelectionBehavior(QAbstractItemView::SelectRows);
        cues->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
        sequenceLayout->addWidget(cues, 1);
        auto edit = new QHBoxLayout;
        button("RECORD", edit, [this] { record(); })->setObjectName("recordButton");
        button("UPDATE", edit, [this] { updateStep(); });
        button("SET NEXT", edit, [this] {
            if (engine.setNextCue(selectedSequence, cues->currentRow())) {
                refresh();
                message("Next GO will play the selected cue. Current output and stored links are unchanged.");
            }
        })->setObjectName("setNextButton");
        button("GO TO", edit, [this] {
            int i = cues->currentRow();
            if (i >= 0) {
                engine.go(selectedSequence, i);
                refresh();
            }
        })->setObjectName("gotoButton");
        sequenceLayout->addLayout(edit);
        auto steps = new QHBoxLayout;
        button("Insert cue…", steps, [this] { chooseCue(); })->setObjectName("insertCueButton");
        button("Cue library", steps, [this] { workspaceTabs->setCurrentWidget(libraryPanel); });
        button("↑", steps, [this] {
            int row = cues->currentRow();
            if (engine.moveStep(selectedSequence, row, row - 1)) { changed(); cues->selectRow(row - 1); }
        })->setObjectName("moveStepUpButton");
        button("↓", steps, [this] {
            int row = cues->currentRow();
            if (engine.moveStep(selectedSequence, row, row + 1)) { changed(); cues->selectRow(row + 1); }
        })->setObjectName("moveStepDownButton");
        button("Link to step…", steps, [this] { linkStep(); })->setObjectName("linkStepButton");
        sequenceLayout->addLayout(steps);
        auto modes = new QHBoxLayout;
        cueOnly = new QCheckBox("Cue Only");
        modes->addWidget(cueOnly);
        button("Record Only", modes, [this] { record(true); });
        button("BLIND", modes, [this] {
            BlindDialog dialog(engine, selectedSequence, [this] { changed(); }, this);
            dialog.exec();
        });
        sequenceLayout->addLayout(modes);
        auto extra = new QHBoxLayout;
        button("Channel timing…", extra, [this] { channelTiming(); });
        button("Triggers…", extra, [this] { cueTriggers(); });
        button("Block", extra, [this] {
            int i = cues->currentRow();
            if (i >= 0) {
                engine.setBlock(selectedSequence, i,
                                !engine.sequences[selectedSequence].cues[i].block);
                changed();
            }
        });
        button("Assert", extra, [this] {
            int i = cues->currentRow();
            if (i >= 0) {
                auto &c = engine.sequences[selectedSequence].cues[i];
                c.asserted = !c.asserted;
                changed();
            }
        });
        button("Remove step", extra, [this] {
            if (engine.removeStep(selectedSequence, cues->currentRow())) { changed(); libraryPanel->reload(); }
        })->setObjectName("removeStepButton");
        sequenceLayout->addLayout(extra);
        auto subPage = new QWidget;
        auto subLayout = new QVBoxLayout(subPage);
        subLayout->addWidget(new QLabel("SUBMASTERS · HTP INTENSITY"));
        auto recordSub = button("+ Record submaster", subLayout, [this] {
            engine.recordSubmaster(); changed(); rebuildSubmasters();
        });
        recordSub->setObjectName("recordSubmasterButton");
        subBank = new QScrollArea;
        subBank->setWidgetResizable(true);
        subLayout->addWidget(subBank, 1);
        workspaceTabs->addTab(subPage, "Submasters");
        auto palettePage = new QWidget;
        auto paletteLayout = new QVBoxLayout(palettePage);
        palettes = new QListWidget;
        palettes->setViewMode(QListView::IconMode);
        palettes->setResizeMode(QListView::Adjust);
        palettes->setMovement(QListView::Static);
        palettes->setGridSize(QSize(170, 80));
        paletteLayout->addWidget(palettes, 3);
        paletteId = new QSpinBox;
        paletteId->setRange(1, 9999);
        paletteId->setPrefix("IP  ");
        paletteLayout->addWidget(paletteId);
        button("Record palette", paletteLayout, [this] {
            engine.recordPalette(paletteId->value());
            changed();
            rebuild();
        });
        button("Apply palette", paletteLayout, [this] {
            engine.applyPalette(paletteId->value());
            refresh();
        });
        workspaceTabs->addTab(palettePage, "Palettes");
        libraryPanel = new CueLibraryPanel(engine, [this] { changed(); }, [this](QString number) { insertCue(number); });
        workspaceTabs->addTab(libraryPanel, "Cue library");
        outputPanel = new OutputPanel(network, engine, [this] {
            dirty = true; setWindowModified(true);
            message(network.enabled ? "DMX outputs enabled" : "DMX outputs off");
        });
        channelTabs->addTab(outputPanel, "Network");
        split->addWidget(cuePanel);
        split->setSizes({560, 640});
        auto transport = new QHBoxLayout;
        button("↶ BACK", transport, [this] {
            engine.back(selectedSequence);
            refresh();
        });
        button("Ⅱ PAUSE / RESUME", transport, [this] {
            auto &s = engine.sequences[selectedSequence];
            s.paused = !s.paused;
            refresh();
        });
        button("RELEASE SEQUENCE", transport, [this] {
            engine.release(selectedSequence);
            refresh();
        });
        transport->addStretch();
        auto go = button(
            "▶  GO", transport,
            [this] {
                engine.go(selectedSequence);
                refresh();
            },
            "background:#b7cfc1;color:#171819;font-size:21px;font-weight:bold;padding:16px 70px;");
        go->setObjectName("goButton");
        go->setShortcut(QKeySequence("Ctrl+Space"));
        workspace->addLayout(transport);
        auto bottom = new QHBoxLayout;
        command = new QLineEdit;
        command->setPlaceholderText("Channel 1 Thru 12 At 50  ·  Press Enter to execute");
        bottom->addWidget(command, 1);
        button("EXECUTE", bottom, [this] { execute(); });
        auto bo = button("BLACKOUT", bottom, [this] {
            engine.blackout = !engine.blackout;
            refresh();
        });
        bo->setCheckable(true);
        layout->addLayout(bottom);
        status = new QLabel("Ready · outputs disabled · Ctrl+Space: GO · Ctrl+S: save");
        auto footer = new QHBoxLayout;
        footer->addWidget(status, 1);
        outputStatus = new QLabel("DMX OFF"); outputStatus->setObjectName("dmxStatus");
        footer->addWidget(outputStatus);
        hoverHelp = new QLabel; hoverHelp->setObjectName("hoverHelp");
        hoverHelp->setWordWrap(true); hoverHelp->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        hoverHelp->setFixedHeight(40); hoverHelp->setMinimumWidth(340); hoverHelp->setMaximumWidth(600);
        footer->addWidget(hoverHelp, 1);
        layout->addLayout(footer);
        qApp->installEventFilter(this);
        for (auto spin : findChildren<QAbstractSpinBox *>()) enableNumberDrag(spin);
        connect(cues, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
            int row = item->row();
            const int column = item->column();
            int col = column == 1 ? 9 : column == 0 ? 0 : column - 1;
            if (row < 0 || row >= engine.sequences[selectedSequence].cues.size() || col >= 6)
                return;
            auto &cue = engine.sequences[selectedSequence].cues[row];
            QString text = item->text().trimmed();
            bool ok = true;
            double n = text.toDouble(&ok);
            if (col == 1) {
                if (cue.reference.isEmpty()) cue.label = text.isEmpty() ? "cue-" + cue.number : text;
                else {
                    engine.cueLibrary[cue.reference].label = text.isEmpty() ? "cue-" + cue.reference : text;
                    libraryPanel->reload(cue.reference);
                }
            } else if (col == 0) {
                auto &list = engine.sequences[selectedSequence].cues;
                if (!ok || n <= 0 || n > 9999.999 ||
                    (row > 0 && n <= list[row - 1].number.toDouble()) ||
                    (row + 1 < list.size() && n >= list[row + 1].number.toDouble())) {
                    message("Cue number must be positive, unique and between its neighbours");
                    refresh();
                    return;
                }
                if (cue.label == "cue-" + cue.number)
                    cue.label = "cue-" + text;
                QString qualified =
                    QString::number(engine.sequences[selectedSequence].number) + "/" + cue.number;
                for (int s = 0; s < engine.sequences.size(); ++s)
                    for (auto &other : engine.sequences[s].cues)
                        for (auto field : {&other.link, &other.execute, &other.after}) {
                            if (*field == qualified)
                                *field =
                                    QString::number(engine.sequences[selectedSequence].number) +
                                    "/" + text;
                            else if (s == selectedSequence && *field == cue.number)
                                *field = text;
                        }
                if (engine.sequences[selectedSequence].pendingCue == cue.number)
                    engine.sequences[selectedSequence].pendingCue = text;
                cue.number = text;
            } else if (col == 5) {
                if (!text.isEmpty()) {
                    int source = selectedSequence;
                    QString number = text;
                    if (text.contains('/')) {
                        auto parts = text.split('/');
                        source = -1;
                        if (parts.size() == 2)
                            for (int i = 0; i < engine.sequences.size(); ++i)
                                if (engine.sequences[i].number == parts[0].toInt())
                                    source = i;
                        number = parts.last();
                    }
                    bool found = false;
                    if (source >= 0)
                        for (const auto &other : engine.sequences[source].cues)
                            if (other.number == number)
                                found = true;
                    if (!found) {
                        message("Link target must exist: step ID or sequence/step ID");
                        refresh();
                        return;
                    }
                }
                cue.link = text;
            } else if (col == 3 && (text == "= Up" || text.isEmpty())) {
                cue.down = -1;
            } else {
                if (!ok || !std::isfinite(n) || n < 0 || n > 3600) {
                    message("Timing must be between 0 and 3600 seconds");
                    refresh();
                    return;
                }
                if (col == 2)
                    cue.time = n;
                else if (col == 3)
                    cue.down = n;
                else if (col == 4)
                    cue.wait = n;
            }
            changed();
        });
        connect(command, &QLineEdit::returnPressed, this, [this] { execute(); });
        connect(channels, &QListWidget::itemSelectionChanged, this, [this] {
            for (int row = 0; row < channels->count(); ++row)
                engine.selection.remove(channels->item(row)->data(Qt::UserRole).toInt());
            for (auto i : channels->selectedItems())
                engine.selection.insert(i->data(Qt::UserRole).toInt());
            refresh();
        });
        connect(level, &QSlider::valueChanged, this, [this](int v) {
            engine.level(v);
            refresh();
        });
        connect(sequences, &QTabBar::currentChanged, this, [this](int tab) {
            int i = sequences->tabData(tab).toInt();
            if (i >= 0) {
                selectedSequence = i;
                refresh();
            }
        });
        connect(sequences, &QTabBar::tabBarDoubleClicked, this, [this](int tab) {
            if (tab < 0) return;
            bool ok;
            auto &seq = engine.sequences[selectedSequence];
            QString name = QInputDialog::getText(this, "Sequence name", "Name", QLineEdit::Normal,
                                                 seq.name, &ok);
            if (ok) {
                seq.name = name.isEmpty() ? QString("sequence-%1").arg(seq.number) : name;
                changed();
                rebuild();
            }
        });
        connect(palettes, &QListWidget::itemClicked, this, [this](QListWidgetItem *i) {
            paletteId->setValue(i->data(Qt::UserRole).toInt());
            engine.applyPalette(paletteId->value());
            refresh();
        });
        // OSC is opt-in, bound only to loopback. Enable from the menu.
        auto tools = menuBar()->addMenu("Programming");
        tools->addAction("Select active channels", this, [this] {
            command->setText("select active");
            execute();
        });
        tools->addAction("Select manual channels", this, [this] {
            command->setText("select manual");
            execute();
        });
        tools->addAction("Record group…", this, [this] {
            bool ok;
            int id = QInputDialog::getInt(this, "Group", "Number", 1, 1, 9999, 1, &ok);
            if (ok && !engine.selection.isEmpty()) {
                Values g;
                for (int c : engine.selection)
                    g[c] = 0;
                engine.groups[id] = g;
                changed();
            }
        });
        tools->addAction("Recall group…", this, [this] {
            bool ok;
            int id = QInputDialog::getInt(this, "Group", "Number", 1, 1, 9999, 1, &ok);
            if (ok && engine.groups.contains(id)) {
                QSignalBlocker b(channels);
                engine.selection.clear();
                for (auto i = engine.groups[id].begin(); i != engine.groups[id].end(); ++i)
                    engine.selection.insert(i.key());
                for (int i = 0; i < channels->count(); ++i)
                    channels->item(i)->setSelected(
                        engine.selection.contains(channels->item(i)->data(Qt::UserRole).toInt()));
                refresh();
            }
        });
        tools->addAction("Programming keypad", this, [this] {
            auto d = new QDialog(this);
            d->setAttribute(Qt::WA_DeleteOnClose);
            d->setWindowTitle("Programming surface / HID prototype");
            auto grid = new QGridLayout(d);
            QStringList keys = {"Channel", "Group",   "Intensity", "Cue", "Sequence", "Record",
                                "Update",  "Release", "1",         "2",   "3",        "Thru",
                                "4",       "5",       "6",         "At",  "7",        "8",
                                "9",       "Full",    "0",         ".",   "Clear",    "Enter"};
            for (int i = 0; i < keys.size(); ++i) {
                auto key = keys[i];
                auto b = new QPushButton(key);
                b->setStatusTip(key == "Enter" ? "Execute the command line." : key == "Clear" ? "Clear the command line." :
                    key == "Record" ? "Store the current look in the cue library." : key == "Update" ? "Update the selected sequence step or its shared cue." :
                    key == "Release" ? "Release manual levels except captured channels." : "Append " + key + " to the command line.");
                grid->addWidget(b, i / 4, i % 4);
                connect(b, &QPushButton::clicked, this, [this, key] {
                    if (key == "Enter")
                        execute();
                    else if (key == "Clear")
                        command->clear();
                    else if (key == "Record")
                        record();
                    else if (key == "Release") {
                        for (auto it = engine.manual.begin(); it != engine.manual.end();)
                            if (!engine.captured.contains(it.key()))
                                it = engine.manual.erase(it);
                            else
                                ++it;
                        refresh();
                    } else if (key == "Update") {
                        int row = cues->currentRow();
                        if (row >= 0) {
                            updateStep();
                        }
                    } else {
                        command->insert(key.size() == 1 ? key : " " + key + " ");
                    }
                });
            }
            d->show();
        });
        auto pixels = menuBar()->addMenu("Pixel mapping");
        pixels->addAction("Start wave on selected channels", this, [this] {
            pixelChannels = engine.selection.values();
            std::sort(pixelChannels.begin(), pixelChannels.end());
            if (pixelChannels.isEmpty()) {
                message("Select channels to map");
                return;
            }
            for (int c : pixelChannels)
                engine.manual.remove(c);
            pixelPhase = 0;
            pixelRunning = true;
            message("1D intensity mapping active · ascending channel order");
        });
        pixels->addAction("Speed…", this, [this] {
            bool ok;
            double v = QInputDialog::getDouble(this, "Mapping", "Cycles per second", pixelRate,
                                               0.01, 20, 2, &ok);
            if (ok)
                pixelRate = v;
        });
        pixels->addAction("Spread…", this, [this] {
            bool ok;
            double v =
                QInputDialog::getDouble(this, "Mapping", "Number of waves across the selection",
                                        pixelSpread, 0, 20, 2, &ok);
            if (ok)
                pixelSpread = v;
        });
        pixels->addAction("Stop / release", this, [this] {
            pixelRunning = false;
            engine.pixels.clear();
            refresh();
        });
        auto fixtures = menuBar()->addMenu("Fixtures");
        fixtures->addAction("Inspect GDTF profile…", this, [this] {
            QString p = QFileDialog::getOpenFileName(this, "GDTF profile", {}, "GDTF (*.gdtf)");
            if (p.isEmpty())
                return;
            QFile f(p);
            if (!f.open(QIODevice::ReadOnly) || f.size() > 64 * 1024 * 1024) {
                message("Profile cannot be read or is too large");
                return;
            }
            FixtureProfile profile;
            QString error;
            if (!readGdtf(f.readAll(), profile, error)) {
                message(error);
                return;
            }
            QString text = profile.manufacturer + " / " + profile.name + "\n\n";
            for (auto m : profile.modes)
                text += m.name + " · " + QString::number(m.footprint) + " addresses\n" +
                        m.attributes.join(", ") + "\n\n";
            text += "Inspection only in this version. Intelligent parameter patching "
                    "is not yet implemented.";
            QMessageBox::information(this, "GDTF profile", text);
        });
        auto integration = menuBar()->addMenu("Interaction");
        auto osc = integration->addAction("Local OSC :9000");
        osc->setCheckable(true);
        connect(osc, &QAction::toggled, this, [this, osc](bool enabled) {
            if (enabled && !network.receiver.bind(QHostAddress::LocalHost, 9000)) {
                message(network.receiver.errorString());
                osc->setChecked(false);
            } else if (!enabled)
                network.receiver.close();
        });
        connect(&network.receiver, &QUdpSocket::readyRead, this, [this] {
            while (network.receiver.hasPendingDatagrams()) {
                auto p = network.receiver.receiveDatagram().data();
                QString a;
                float v;
                if (!oscDecode(p, a, v))
                    continue;
                auto m = QRegularExpression("^/coldflame/sequence/(\\d+)/go$").match(a);
                if (m.hasMatch() && v > 0) {
                    for (int i = 0; i < engine.sequences.size(); ++i)
                        if (engine.sequences[i].number == m.captured(1).toInt())
                            engine.go(i);
                }
                if (a == "/coldflame/master")
                    engine.master = qBound(0., double(v), 1.);
                if (a == "/coldflame/blackout")
                    engine.blackout = v > 0;
            }
        });
        if (!showFile.isEmpty()) {
            QFile f(showFile);
            QString error;
            if (f.open(QIODevice::ReadOnly) &&
                loadShow(QJsonDocument::fromJson(f.readAll()).object(), &error))
                path = showFile;
            else
                message("Could not load show: " + error);
        }
        history.reset(engine);
        rebuild();
        elapsed.start();
        timer.setInterval(23);
        connect(&timer, &QTimer::timeout, this, [this] {
            double dt = elapsed.restart() / 1000.;
            engine.tick(dt);
            if (pixelRunning) {
                pixelPhase += dt * pixelRate;
                for (int i = 0; i < pixelChannels.size(); ++i)
                    engine.pixels[pixelChannels[i]] =
                        50 + 50 * std::sin(6.28318530718 *
                                           (pixelPhase -
                                            pixelSpread * i / qMax(1, int(pixelChannels.size()))));
            }
            network.send(engine);
            refresh();
            if (!network.lastError.isEmpty())
                message(network.lastError);
        });
        timer.start();
    }
    void execute() {
        QString error;
        auto before = engine.json();
        engine.commandSequence = selectedSequence;

        engine.cueOnlyMode = cueOnly->isChecked();
        if (engine.command(command->text(), &error)) {
            if (engine.json() != before) {
                changed();
                libraryPanel->reload();
            }
            selectedSequence = engine.commandSequence;
            for (int tab = 0; tab < sequences->count(); ++tab)
                if (sequences->tabData(tab).toInt() == selectedSequence)
                    sequences->setCurrentIndex(tab);
            if (engine.commandCue >= 0)
                cues->selectRow(engine.commandCue);
            {
                QSignalBlocker block(channels);
                for (int i = 0; i < channels->count(); ++i)
                    channels->item(i)->setSelected(
                        engine.selection.contains(channels->item(i)->data(Qt::UserRole).toInt()));
            }
            message("Command executed");
        } else
            message(error);
        refresh();
    }
};
#ifndef FLAME_NO_MAIN
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("Cold Flame");
    QString showFile;
    for (const auto &arg : app.arguments().mid(1))
        if (arg.endsWith(".cfl"))
            showFile = arg;
    Console w(showFile);
    w.show();
    if (app.arguments().contains("--screenshot"))
        QTimer::singleShot(200, [&w] { w.grab().save("cold-flame-preview.png"); });
    if (app.arguments().contains("--patch-screenshot"))
        QTimer::singleShot(50, [&w] {
            w.findChild<QTabWidget *>("channelTabs")->setCurrentIndex(1);
            QTimer::singleShot(150, [&w] { w.grab().save("patch-preview.png"); });
        });
    if (app.arguments().contains("--network-screenshot"))
        QTimer::singleShot(50, [&w] {
            auto tabs = w.findChild<QTabWidget *>("channelTabs");
            tabs->setCurrentIndex(2);
            w.findChild<QPushButton *>("addDmxOutput")->click();
            QTimer::singleShot(150, [&w] { w.grab().save("network-preview.png"); });
        });
    if (app.arguments().contains("--library-screenshot"))
        QTimer::singleShot(50, [&w] {
            w.findChild<QTabWidget *>("workspaceTabs")->setCurrentIndex(3);
            QTimer::singleShot(150, [&w] { w.grab().save("cue-library-preview.png"); });
        });
    if (app.arguments().contains("--smoke-test"))
        QTimer::singleShot(300, &app, &QApplication::quit);
    return app.exec();
}

#endif
