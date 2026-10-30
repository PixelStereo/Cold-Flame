#include "cardbank.h"
#include "palettereferences.h"
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
#include "steptable.h"
#include "patchdialog.h"
#include "fixturepicker.h"
#include "parameterpanel.h"
#include "fixturecolor.h"
#include "colorpalettepanel.h"
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
    ParameterPanel *parameterPanel = nullptr;
    ColorPalettePanel *colorPalettePanel = nullptr;
    void rebuildColorPalettes() { if (colorPalettePanel) colorPalettePanel->reload(); }
    BlindDialog *trackingPanel = nullptr;
    StepTable *cues;
    enum ChannelView { AllChannels, UsedChannels, EditChannels };
    ChannelView channelView = AllChannels;
    QList<int> shownChannels;
    int matchingChannels = 0;
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
        b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        if (!style.isEmpty())
            b->setStyleSheet(style);
        layout->addWidget(b);
        connect(b, &QPushButton::clicked, this, action);
        return b;
    }
    void changed() {
        history.commit(engine);
        rebuildColorPalettes();
        dirty = true;
        setWindowModified(true);
        if (trackingPanel) trackingPanel->reload(selectedSequence);
        refresh();
    }
    void message(QString text) { status->setText(text); }
    void updateChannelPage(const Values &out) {
        QList<int> matching;
        QSet<int> members;
        if (channelView == EditChannels) {
            if (workspaceTabs->currentWidget() == libraryPanel) {
                const auto values = engine.cueLibrary.value(libraryPanel->selectedCue()).values;
                for (int channel : values.keys()) members.insert(engine.channelForControl(channel));
            } else {
                const auto &steps = engine.sequences[selectedSequence].cues;
                const int row = cues->currentRow();
                if (row >= 0 && row < steps.size()) {
                    for (int channel : engine.cueValues(steps[row]).keys()) members.insert(engine.channelForControl(channel));
                    for (int channel : steps[row].blocked.keys()) members.insert(engine.channelForControl(channel));
                }
            }
        }
        for (int channel : engine.channels.keys())
            if (channelView == AllChannels || (channelView == UsedChannels && out.value(channel) > 0) ||
                (channelView == EditChannels && members.contains(channel))) matching << channel;
        matchingChannels = matching.size();
        const int pages = channelView == AllChannels
            ? (engine.channels.isEmpty() ? 1 : (engine.channels.lastKey() - 1) / Engine::ChannelsPerPage + 1)
            : qMax(1, int((matching.size() + Engine::ChannelsPerPage - 1) / Engine::ChannelsPerPage));
        channelPage = qBound(0, channelPage, pages - 1);
        QStringList labels;
        for (int page = 0; page < pages; ++page) {
            const int first = page * Engine::ChannelsPerPage + 1;
            labels << (channelView == AllChannels ? QString("%1–%2").arg(first).arg(first + Engine::ChannelsPerPage - 1)
                : matching.isEmpty() ? "0 / 0" : QString("%1–%2 / %3").arg(first)
                    .arg(qMin((page + 1) * Engine::ChannelsPerPage, int(matching.size()))).arg(matching.size()));
        }
        bool newLabels = channelPages->count() != labels.size();
        for (int i = 0; !newLabels && i < labels.size(); ++i) newLabels |= channelPages->itemText(i) != labels[i];
        {
            QSignalBlocker block(channelPages);
            if (newLabels) { channelPages->clear(); channelPages->addItems(labels); }
            channelPages->setCurrentIndex(channelPage);
        }
        QList<int> visible;
        if (channelView == AllChannels) {
            for (int channel : matching)
                if ((channel - 1) / Engine::ChannelsPerPage == channelPage) visible << channel;
        } else visible = matching.mid(channelPage * Engine::ChannelsPerPage, Engine::ChannelsPerPage);
        if (visible == shownChannels) return;
        shownChannels = visible;
        QSignalBlocker block(channels);
        channels->clear();
        for (int channel : visible) {
            auto item = new QListWidgetItem;
            item->setData(Qt::UserRole, channel); item->setTextAlignment(Qt::AlignCenter);
            channels->addItem(item); item->setSelected(engine.selection.contains(channel));
        }
        fitChannels();
    }
    void refresh() {
        outputStatus->setText(!network.enabled ? "DMX OFF" : !network.lastError.isEmpty() ? "DMX ERROR" : "DMX ON");
        outputStatus->setToolTip(network.lastError);
        auto out = engine.output();
        updateChannelPage(out);
        QSignalBlocker cueBlock(cues);
        // Rendering-only roles must not cause QListView to recompute its icon layout.
        // Structural changes remain in rebuild(), with normal model notifications.
        QSignalBlocker channelModelBlock(channels->model());
        bool repaintChannels = false;
        QMap<int, QStringList> inhibitors;
        for (const auto &sub : engine.submasters)
            if (sub.mode == Submaster::Mode::Inhibitive && sub.level < 1)
                for (int channel : sub.values.keys())
                    inhibitors[channel] << QString("Sub %1 · %2%").arg(sub.number).arg(qRound(sub.level * 100));
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
                                 : inhibitors.contains(id)      ? "INHIBIT"
                                                                : "";
            const QColor color = fixtureColor(engine, id, &out);
            bool colorManual = false;
            for (int key : colorControls(engine, id).keys()) colorManual |= engine.manual.contains(key);
            repaintChannels |= item->data(Qt::UserRole + 5).value<QColor>() != color ||
                               item->data(Qt::UserRole + 6).toBool() != colorManual || item->text() != valueText ||
                               item->data(Qt::UserRole + 1).toDouble() != v ||
                               item->data(Qt::UserRole + 2).toBool() != manual ||
                               item->data(Qt::UserRole + 4).toString() != flag;
            item->setText(valueText);
            item->setData(Qt::UserRole + 1, v);
            item->setData(Qt::UserRole + 2, engine.manual.contains(id));
            item->setData(Qt::UserRole + 4, flag);
            item->setData(Qt::UserRole + 5, color);
            item->setData(Qt::UserRole + 6, colorManual);
            item->setToolTip(QString("Channel %1 · %2%3").arg(id).arg(engine.channels[id],
                color.isValid() ? " · Click its color swatch, double-click or right-click to choose a color" : "") +
                (inhibitors.contains(id) && !engine.parked.contains(id) ? " · Inhibited by " + inhibitors[id].join(", ") : ""));
        }
        if (repaintChannels)
            channels->viewport()->update();
        if (parameterPanel) parameterPanel->refresh();
        selection->setText(QString("%1 shown / %2 matching · %3 selected · %4 manual")
                               .arg(channels->count()).arg(matchingChannels)
                               .arg(engine.selection.size()).arg(engine.manual.size()));
        for (const auto &sub : engine.submasters) {
            if (auto fader = subBank->findChild<QSlider *>(QString("subFader%1").arg(sub.number))) {
                QSignalBlocker block(fader);
                if (!fader->isSliderDown()) fader->setValue(qRound(sub.level * 100));
            }
            if (auto label = subBank->findChild<QLabel *>(QString("subLevel%1").arg(sub.number))) {
                const QString phase = sub.phase == Submaster::Phase::Up ? " · Bump in" : sub.phase == Submaster::Phase::Down ? " · Bump out"
                    : sub.phase == Submaster::Phase::Dwell ? (sub.dwell == Submaster::Dwell::Timed ? " · Timed dwell" : " · Holding") : "";
                label->setText(QString::number(qRound(sub.level * 100)) + " %" + phase);
            }
        }
        auto &s = engine.sequences[selectedSequence];
        cues->setSequenceContext(QString::number(s.number));
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
            vals << (c.reference.isEmpty() ? c.number + " (inline)" : c.reference)
                 << (c.follow ? QString::number(*c.follow) : QString())
                 << (c.hang ? QString::number(*c.hang) : QString()) << c.execute << c.after;
            for (int j = 0; j < vals.size(); ++j) {
                const int column = j >= 10 ? j : j == 9 ? 1 : j == 0 ? 0 : j + 1;
                auto item = cues->item(i, column);
                if (!item) {
                    item = new QTableWidgetItem;
                    cues->setItem(i, column, item);
                }
                item->setToolTip(QString("Follow: %1 · Hang: %2\nExecute: %3\nAfter step: %4\n%5 stored channels · %6 individual timings")
                                     .arg(c.follow ? QString::number(*c.follow) : "—",
                                          c.hang ? QString::number(*c.hang) : "—", c.execute,
                                          c.after).arg(engine.cueValues(c).size()).arg(c.channelTimes.size()));
                if (j == 1) item->setToolTip(engine.cueLabel(c));
                if ((j >= 6 && j <= 9) || j == 0)
                    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                if (item->text() != vals[j])
                    item->setText(vals[j]);
                item->setForeground(QColor(i == s.current ? "#c2e5d3" : "#e5e5e5"));
            }
            auto references = c.blocked;
            const auto instructions = engine.cueValues(c);
            for (auto it = instructions.cbegin(); it != instructions.cend(); ++it) references[it.key()] = it.value();
            const auto palettes = paletteReferences(engine, references, &c);
            auto paletteItem = cues->item(i, 15);
            if (!paletteItem) {
                paletteItem = new QTableWidgetItem;
                paletteItem->setFlags(paletteItem->flags() & ~Qt::ItemIsEditable);
                cues->setItem(i, 15, paletteItem);
            }
            if (paletteItem->text() != palettes.text) paletteItem->setText(palettes.text);
            paletteItem->setToolTip(palettes.help);
            paletteItem->setForeground(QColor(i == s.current ? "#c2e5d3" : "#e5e5e5"));
            stepFlags(i, s, c);
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
        rebuildColorPalettes();
        if (parameterPanel) parameterPanel->reload();
        if (trackingPanel) trackingPanel->reload(selectedSequence);
        fitChannels();
        refresh();
    }
    void record(bool manualOnly = false) {
        const QString number = engine.nextStoredCueNumber();
        if (!engine.storeCue(number, {}, manualOnly)) { message("Cue number limit reached"); return; }
        libraryPanel->reload(number);
        workspaceTabs->setCurrentWidget(libraryPanel);
        changed();
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
            QMap<int, Value> shared;
            QSet<int> localColors;
            auto localKeys = step.values.keys(); for (int key : step.suppressed) localKeys << key;
            for (int key : localKeys) {
                const int channel = engine.channelForControl(key);
                if (engine.colorControls(channel).keys().contains(key)) localColors.insert(channel);
            }
            for (auto it = engine.manual.begin(); it != engine.manual.end(); ++it)
                if (step.values.contains(it.key()) || step.suppressed.contains(it.key()) ||
                    (localColors.contains(engine.channelForControl(it.key())) && engine.colorControls(engine.channelForControl(it.key())).keys().contains(it.key())))
                    engine.editBlind(selectedSequence, index, it.key(), it.value());
                else shared[it.key()] = it.value();
            engine.updateStoredCue(step.reference, shared);
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
    void chooseChannelColor(int channel) {
        const auto controls = colorControls(engine, channel);
        if (!controls.available()) return;
        QMap<int, Value> before;
        for (int key : controls.keys()) if (engine.manual.contains(key)) before[key] = engine.manual.value(key);
        QColorDialog dialog(fixtureColor(engine, channel), this);
        dialog.setOption(QColorDialog::DontUseNativeDialog);
        dialog.setObjectName("fixtureColorDialog");
        dialog.setWindowTitle(QString("Channel %1 · Color").arg(channel));
        connect(&dialog, &QColorDialog::currentColorChanged, this, [this, channel](QColor color) {
            setFixtureColor(engine, channel, color, false); refresh();
        });
        if (dialog.exec() == QDialog::Accepted) {
            if (!setFixtureColor(engine, channel, dialog.currentColor())) message("Cannot create a color palette");
            else {
                changed();
                if (colorPalettePanel) colorPalettePanel->reload(engine.manual.value(controls.keys().first()).colorPalette);
            }
        }
        else for (int key : controls.keys()) {
            if (before.contains(key)) engine.manual[key] = before.value(key);
            else engine.manual.remove(key);
        }
        refresh();
    }
    void fitChannels() {
        if (!channels)
            return;
        const auto size = channels->viewport()->size();
        int columns = qMax(1, size.width() / 64);
        int fittingColumns = (channels->count() + qMax(1, size.height() / 38) - 1) / qMax(1, size.height() / 38);
        // Use the actual minimum tile width, with the same gutter as the cell
        // calculation. Native style margins can put the viewport just below 11 * 54.
        const int maximumColumns = qMax(1, (size.width() - 6) / 52);
        columns = qBound(1, qMax(columns, fittingColumns), maximumColumns);
        const QSize cell(qMax(52, (size.width() - 6) / columns), 38);
        if (cell != channels->gridSize())
            channels->setGridSize(cell);
    }
    void channelTiming() {
        int row = cues->currentRow();
        if (row < 0 || engine.selection.isEmpty()) {
            message("Select a step and channels to assign individual timing");
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
        auto reset = buttons.addButton("Use step timing", QDialogButtonBox::ResetRole);
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
    bool validStepTarget(QString target) const {
        if (target.isEmpty()) return true;
        const auto parts = target.split('/');
        if (parts.size() > 2) return false;
        int sequence = selectedSequence;
        QString step = target;
        if (parts.size() == 2) {
            sequence = -1; step = parts[1];
            for (int i = 0; i < engine.sequences.size(); ++i)
                if (QString::number(engine.sequences[i].number) == parts[0]) sequence = i;
        }
        bool ok; const double number = step.toDouble(&ok);
        if (!ok || sequence < 0 || !std::isfinite(number)) return false;
        for (const auto &candidate : engine.sequences[sequence].cues)
            if (candidate.number.toDouble() == number) return true;
        return false;
    }
    void stepFlags(int row, const Sequence &sequence, const Cue &step) {
        const QString binding = QString::number(sequence.number) + "/" + step.number;
        auto cell = cues->cellWidget(row, 14);
        if (!cell || cell->property("binding").toString() != binding) {
            cell = new QWidget;
            cell->setProperty("binding", binding); cell->setStyleSheet("background:transparent;");
            auto layout = new QVBoxLayout(cell); layout->setContentsMargins(2, 1, 2, 1); layout->setSpacing(1);
            for (bool assertFlag : {false, true}) {
                auto control = new QPushButton(assertFlag ? "A" : "B", cell);
                control->setObjectName((assertFlag ? "assertStep" : "blockStep") + step.number);
                control->setCheckable(true); control->setFixedSize(24, 14);
                control->setStyleSheet("QPushButton{background:#292c2e;border:1px solid #404346;"
                    "padding:0px;min-height:0px;border-radius:2px;font-size:9px;}"
                    "QPushButton:checked{background:#314c44;border-color:#6e9b8c;color:#ebfff6;}");
                control->setStatusTip(assertFlag ? "Assert: take back this step's tracked values when it plays."
                    : "Block: protect this step's tracked values against upstream edits.");
                layout->addWidget(control, 0, Qt::AlignHCenter);
                connect(control, &QPushButton::clicked, this, [this, sequenceNumber = sequence.number, id = step.number, assertFlag](bool checked) {
                    for (int s = 0; s < engine.sequences.size(); ++s)
                        if (engine.sequences[s].number == sequenceNumber)
                            for (int index = 0; index < engine.sequences[s].cues.size(); ++index)
                                if (engine.sequences[s].cues[index].number == id) {
                                    if (s == selectedSequence) cues->selectRow(index);
                                    if (assertFlag) engine.sequences[s].cues[index].asserted = checked;
                                    else engine.setBlock(s, index, checked);
                                    changed(); return;
                                }
                });
            }
            cues->setCellWidget(row, 14, cell);
        }
        cell->findChild<QPushButton *>("blockStep" + step.number)->setChecked(step.block);
        cell->findChild<QPushButton *>("assertStep" + step.number)->setChecked(step.asserted);
    }
    void removeSelectedStep() {
        if (engine.removeStep(selectedSequence, cues->currentRow())) { changed(); libraryPanel->reload(); }
    }
    void editSubmaster(int index) {
        QDialog dialog(this);
        dialog.setObjectName("submasterDialog");
        dialog.setWindowTitle("Edit submaster · stored intensities");
        dialog.resize(520, 640);
        QVBoxLayout layout(&dialog);
        QLineEdit name(engine.submasters[index].name);
        layout.addWidget(new QLabel("Name"));
        layout.addWidget(&name);
        QComboBox mode; mode.setObjectName("submasterMode"); mode.addItems({"Additive · HTP intensity", "Inhibitive · intensity master"});
        mode.setCurrentIndex(engine.submasters[index].mode == Submaster::Mode::Inhibitive ? 1 : 0);
        QComboBox dwell; dwell.setObjectName("submasterDwell"); dwell.addItems({"Manual · while held", "Hold · press again to release", "Timed"});
        dwell.setCurrentIndex(int(engine.submasters[index].dwell));
        QDoubleSpinBox up, down, duration;
        for (auto spin : {&up, &down, &duration}) { spin->setRange(0, 86400); spin->setDecimals(2); spin->setSuffix(" s"); enableNumberDrag(spin); }
        up.setObjectName("submasterUp"); down.setObjectName("submasterDown"); duration.setObjectName("submasterDwellTime");
        up.setValue(engine.submasters[index].upTime); down.setValue(engine.submasters[index].downTime); duration.setValue(engine.submasters[index].dwellTime);
        QFormLayout properties; properties.addRow("Mode", &mode); properties.addRow("Bump dwell", &dwell);
        QHBoxLayout times; times.addWidget(new QLabel("Up")); times.addWidget(&up); times.addWidget(new QLabel("Dwell")); times.addWidget(&duration); times.addWidget(new QLabel("Down")); times.addWidget(&down);
        properties.addRow("Bump timing", &times); layout.addLayout(&properties);
        duration.setEnabled(dwell.currentIndex() == 2);
        connect(&dwell, &QComboBox::currentIndexChanged, &dialog, [&](int value) { duration.setEnabled(value == 2); });
        auto membership = new QLabel("Inhibitive mode uses channel membership only. Its home is Full; Bump fades to Out.");
        membership->setWordWrap(true); membership->setVisible(mode.currentIndex() == 1); layout.addWidget(membership);
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
        table.setColumnHidden(2, mode.currentIndex() == 1);
        connect(&mode, &QComboBox::currentIndexChanged, &dialog, [&](int value) {
            table.setColumnHidden(2, value == 1); membership->setVisible(value == 1);
        });
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
                double value = mode.currentIndex() == 1 ? (ok = true, 100.) : table.item(r, 2)->text().toDouble(&ok);
                if (!ok || !std::isfinite(value) || value < 0 || value > 100) {
                    error.setText("Included levels must be between 0 and 100.");
                    return;
                }
                values[table.item(r, 0)->data(Qt::UserRole).toInt()] = value;
            }
            auto &sub = engine.submasters[index];
            const auto nextMode = mode.currentIndex() == 1 ? Submaster::Mode::Inhibitive : Submaster::Mode::Additive;
            if (sub.mode != nextMode) { sub.mode = nextMode; engine.setSubmasterLevel(index, sub.home()); }
            else engine.setSubmasterLevel(index, sub.level); // Editing timing cancels the current bump without jumping.
            sub.dwell = Submaster::Dwell(dwell.currentIndex());
            sub.upTime = up.value(); sub.downTime = down.value(); sub.dwellTime = duration.value();
            sub.name = name.text().trimmed();
            engine.submasters[index].values = values;
            changed();
            dialog.accept();
        });
        if (dialog.exec() == QDialog::Accepted) rebuildSubmasters();
    }
    void rebuildSubmasters() {
        // A rebuilt bank must not leave a momentary button held after its widget disappears.
        for (int i = 0; i < engine.submasters.size(); ++i)
            if (engine.submasters[i].dwell == Submaster::Dwell::Manual && engine.submasters[i].pressed)
                engine.bumpSubmaster(i, false);
        auto content = new CardBank(subBank);
        for (int i = 0; i < engine.submasters.size(); ++i) {
            const auto &sub = engine.submasters[i];
            auto card = new QGroupBox(QString("SUB %1").arg(sub.number), content);
            card->setObjectName(QString("subCard%1").arg(sub.number));
            card->setMinimumWidth(280);
            auto layout = new QVBoxLayout(card);
            layout->setContentsMargins(12, 18, 12, 12); layout->setSpacing(6);
            auto name = new QLabel(sub.name); name->setTextFormat(Qt::PlainText); name->setWordWrap(true);
            name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            layout->addWidget(name);
            layout->addWidget(new QLabel(QString("%1 · %2 channels").arg(sub.mode == Submaster::Mode::Inhibitive ? "INHIBITIVE" : "HTP intensity").arg(sub.values.size())));
            const QString dwellText = sub.dwell == Submaster::Dwell::Timed ? QString::number(sub.dwellTime) + " s"
                : sub.dwell == Submaster::Dwell::Hold ? "Hold" : "Manual";
            layout->addWidget(new QLabel(QString("Bump  %1 s / %2 / %3 s").arg(sub.upTime).arg(dwellText).arg(sub.downTime)));
            auto value = new QLabel(QString::number(qRound(sub.level * 100)) + " %");
            value->setObjectName(QString("subLevel%1").arg(sub.number));
            layout->addWidget(value);
            auto fader = new QSlider(Qt::Horizontal);
            fader->setObjectName(QString("subFader%1").arg(sub.number));
            fader->setRange(0, 100);
            fader->setValue(qRound(sub.level * 100));
            layout->addWidget(fader);
            connect(fader, &QSlider::valueChanged, this, [this, i, value](int v) {
                engine.setSubmasterLevel(i, v / 100.);
                value->setText(QString::number(v) + " %");
                refresh();
            });
            auto actions = new QGridLayout; actions->setSpacing(6);
            auto action = [&](QString text, int row, int col, QString object, std::function<void()> callback) {
                auto control = new QPushButton(text, card);
                control->setObjectName(object + QString::number(sub.number));
                control->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
                actions->addWidget(control, row, col);
                connect(control, &QPushButton::clicked, this, callback);
                return control;
            };
            for (int col = 0; col < 3; ++col) actions->setColumnStretch(col, 1);
            action("Out", 0, 0, "subOut", [this, i] { engine.setSubmasterLevel(i, 0); refresh(); });
            action("Full", 0, 1, "subFull", [this, i] { engine.setSubmasterLevel(i, 1); refresh(); });
            auto bump = action("BUMP", 0, 2, "subBump", [] {});
            bump->setStyleSheet("QPushButton{background:#314c44;border-color:#6e9b8c;} QPushButton:hover{background:#3a5b50;}");
            bump->setObjectName(QString("subBump%1").arg(sub.number));
            bump->setStatusTip("Bump uses Up / Dwell / Down timing. Manual: hold the button. Hold: press again to release. Inhibitive bumps go to Out.");
            connect(bump, &QPushButton::pressed, this, [this, i] { engine.bumpSubmaster(i, true); refresh(); });
            connect(bump, &QPushButton::released, this, [this, i] { engine.bumpSubmaster(i, false); refresh(); });
            action("Edit…", 1, 0, "editSub", [this, i] { editSubmaster(i); });
            action("Update", 1, 1, "updateSub", [this, i] {
                // Updating stored content must not record this submaster's own contribution.
                const double level = engine.submasters[i].level;
                engine.submasters[i].level = engine.submasters[i].home();
                auto live = engine.output(false, false);
                engine.submasters[i].level = level;
                Values values;
                if (engine.selection.isEmpty()) { for (int key : engine.channels.keys()) values[key] = live.value(key); }
                else for (int ch : engine.selection) values[ch] = live.value(ch);
                engine.submasters[i].values = values;
                changed();
                QTimer::singleShot(0, this, [this] { rebuildSubmasters(); });
                message("Submaster updated from live output · selected channels, or all if none selected");
            });
            action("Delete", 1, 2, "deleteSub", [this, i] {
                engine.submasters.removeAt(i);
                changed();
                QTimer::singleShot(0, this, [this] { rebuildSubmasters(); });
            });
            layout->addLayout(actions);
            // Equal stretches alone retain unequal native minimum sizes on Windows.
            int actionWidth = 0;
            for (int item = 0; item < actions->count(); ++item) {
                auto control = actions->itemAt(item)->widget(); control->ensurePolished();
                actionWidth = qMax(actionWidth, control->minimumSizeHint().width());
            }
            for (int col = 0; col < 3; ++col) actions->setColumnMinimumWidth(col, actionWidth);
            content->addCard(card);
        }
        if (engine.submasters.isEmpty())
            content->addCard(new QLabel("No submasters yet. Set a look, then Record submaster."));
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
            {"Link…", "Choose the exact sequence and step to play next; repeated cues have different step IDs."},
            {"Remove step", "Remove this occurrence from the sequence. Its stored cue remains in the library."},
            {"▶  GO", "Play the pending step of the selected sequence. Shortcut: Ctrl+Space."},
            {"↶ BACK", "Play the preceding step of the selected sequence."},
            {"Ⅱ PAUSE / RESUME", "Pause or resume the selected sequence's fade."},
            {"RELEASE SEQUENCE", "Release this sequence's output so other sources can contribute."},
            {"RELEASE MANUAL", "Release manual values except captured channels, revealing playback and submasters."},
            {"CAPTURE", "Protect selected manual channels from playback changes. Click again to release capture."},
            {"PARK", "Lock selected channel outputs. Park bypasses master and blackout and is excluded from recording."},
            {"UNPARK", "Remove Park from selected channels."},
            {"Select all", "Select all displayed channels on the current page. Hidden channel selections are retained."},
            {"+ Page", "Add the next 120 channels and assign unused DMX addresses."},
            {"0", "Set selected channels to zero."}, {"FULL", "Set selected channels to 100 percent."},
            {"BLACKOUT", "Toggle blackout. Parked channels retain their parked output."},
            {"EXECUTE", "Run the command entered in the command line."},
            {"+", "Create a new sequence."}, {"−", "Delete the selected sequence. At least one sequence must remain."},
            {"Priority…", "Set playback priority: 1 is lowest, 10 is highest."},
            {"BLIND", "Edit sequence step intensities without recalling them live. Referenced cues receive local overrides."},
            {"Block", "Protect this step's tracked values against upstream edits."},
            {"Assert", "Make this step take back its tracked channel values when played."},
            {"Tracking", "Edit channel and fixture parameter moves across sequence steps, with Track, Block and Trace."},
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
            {"OK", "Apply this editor's settings."}, {"Use step timing", "Remove selected channels' timing overrides."}
        };
        if (descriptions.contains(text)) return descriptions.value(text);
        if (button->isCheckable()) return "Toggle " + text + ".";
        return "Activate " + text + ".";
    }
    bool eventFilter(QObject *o, QEvent *e) override {
        auto widget = qobject_cast<QWidget *>(o);
        const bool owned = widget && (widget == this || isAncestorOf(widget));
        if (o == this && e->type() == QEvent::WindowDeactivate) {
            for (int i = 0; i < engine.submasters.size(); ++i)
                if (engine.submasters[i].dwell == Submaster::Dwell::Manual && engine.submasters[i].pressed)
                    engine.bumpSubmaster(i, false);
            refresh();
        }
        if (owned && e->type() == QEvent::Polish)
            if (auto spin = qobject_cast<QAbstractSpinBox *>(widget)) enableNumberDrag(spin);
        if (owned && o == channels->viewport() && e->type() == QEvent::MouseButtonRelease) {
            auto event = static_cast<QMouseEvent *>(e);
            if (event->button() == Qt::LeftButton) {
                auto item = channels->itemAt(event->position().toPoint());
                if (item && ChannelDelegate::colorSwatch(channels->visualItemRect(item)).adjusted(-3, -3, 3, 3).contains(event->position().toPoint())) {
                    const int id = item->data(Qt::UserRole).toInt();
                    if (colorControls(engine, id).available()) QTimer::singleShot(0, this, [this, id] { chooseChannelColor(id); });
                }
            }
        }
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
    Console(QString showFile = {}, bool startupOutput = true) {
        setWindowTitle("COLD FLAME[*] — Lighting workspace");
        resize(1440, 940);
        setMinimumSize(1100, 720);
        setStyleSheet(R"(
 QWidget{background:#171819;color:#e5e5e5;font:12px 'DejaVu Sans';}
 QLabel{background:transparent;}
 QLabel#title{font-size:23px;font-weight:700;letter-spacing:2px;color:#fafafa;}
 QLabel#section{color:#96999d;font-size:11px;font-weight:700;letter-spacing:1px;padding:5px 0;}
 QLabel#playback{background:#242628;border-left:3px solid #b7cfc1;padding:14px;color:#c2e5d3;font-size:15px;font-weight:600;}
 QPushButton{background:#292c2e;border:1px solid #404346;border-radius:4px;padding:7px 11px;min-height:18px;}
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
 QScrollBar:horizontal{background:#1d1f20;height:10px;margin:0;} QScrollBar::handle:horizontal{background:#393c3f;min-width:24px;border-radius:4px;} QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{width:0;} QScrollBar::add-page:horizontal,QScrollBar::sub-page:horizontal{background:none;}
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
        bar->addWidget(label("CHANNELS"));
        auto viewModes = new QButtonGroup(this);
        const QStringList modeNames{"ALL", "USED", "EDIT"};
        const QStringList modeHelp{
            "Show every patched channel, in pages of 120.",
            "Show only channels whose live output is above zero, including manual, cue and submaster output.",
            "Show channels stored in the selected step or library cue, including zero levels and local overrides."
        };
        for (int mode = 0; mode < modeNames.size(); ++mode) {
            auto control = button(modeNames[mode], bar, [this, mode] {
                channelView = static_cast<ChannelView>(mode); channelPage = 0; refresh();
            });
            control->setObjectName("channelView" + modeNames[mode]); control->setCheckable(true);
            control->setStatusTip(modeHelp[mode]); viewModes->addButton(control, mode);
            if (mode == 0) control->setChecked(true);
        }
        bar->addStretch();
        channelPages = new QComboBox;
        channelPages->setObjectName("channelPages");
        bar->addWidget(channelPages);
        connect(channelPages, &QComboBox::currentIndexChanged, this, [this](int page) {
            if (page < 0) return;
            channelPage = page; refresh();
        });
        button("+ Page", bar, [this] {
            int page = engine.addChannelPage();
            if (page < 0) { message("Channel limit reached"); return; }
            channelView = AllChannels;
            findChild<QPushButton *>("channelViewALL")->setChecked(true);
            channelPage = page; changed(); rebuild(); patchPanel->reload();
        })->setObjectName("addChannelPageButton");
        button("Select all", bar, [this] { channels->selectAll(); });
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
        overrides->addStretch();
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
        sequenceLayout->setSpacing(10);
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
        cues = new StepTable;
        cues->reorder = [this](int source, int destination) {
            if (engine.moveStep(selectedSequence, source, destination)) {
                changed(); cues->selectRow(destination);
                message("Step reordered · IDs, links and active playback preserved");
            }
        };
        cues->setObjectName("cueSheet");
        cues->setItemDelegate(new TableDelegate(cues));
        cues->verticalHeader()->setDefaultSectionSize(32);
        cues->setHorizontalHeaderLabels(
            {"STEP", "CUE", "NAME", "UP", "DOWN", "WAIT", "LINK", "VALUES", "INDIV.", "STATE", "FOLLOW", "HANG", "EXEC", "AFTER", "B/A", "PALETTES"});
        cues->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        cues->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
        cues->setColumnWidth(2, 128); // Preserve legible cue names; scroll at narrow widths.
        cues->horizontalHeader()->setSectionResizeMode(14, QHeaderView::Fixed); cues->setColumnWidth(14, 30);
        cues->horizontalHeader()->moveSection(14, 0);
        cues->horizontalHeader()->setSectionResizeMode(15, QHeaderView::Interactive);
        cues->setColumnWidth(15, 210);
        cues->horizontalHeader()->moveSection(cues->horizontalHeader()->visualIndex(15), 4);
        cues->horizontalHeaderItem(15)->setToolTip("Stored palette references: Cue = shared cue; Step = local override or block. Hover a cell for affected controls.");
        cues->horizontalHeader()->viewport()->setStatusTip("Times are in seconds. Follow/Hang are mutually exclusive. Link, Execute and After target step IDs.");
        cues->hideColumn(7); cues->hideColumn(8); // Counts remain in row help, leaving space for editable triggers.
        cues->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(cues, &QTableWidget::customContextMenuRequested, this, [this](QPoint point) {
            const int row = cues->rowAt(point.y()); if (row < 0) return;
            cues->selectRow(row);
            QMenu menu(cues); menu.setObjectName("stepContextMenu");
            auto remove = menu.addAction("Remove step");
            remove->setStatusTip("Remove this occurrence and its incoming links. Its stored cue remains available. Undo restores it.");
            connect(&menu, &QMenu::hovered, this, [this](QAction *action) { hoverHelp->setText(action->statusTip()); });
            QAction *assign = nullptr;
            const auto step = engine.sequences[selectedSequence].cues[row];
            if (!step.reference.isEmpty()) assign = menu.addAction("Assign color palette to stored cue…");
            auto action = menu.exec(cues->viewport()->mapToGlobal(point));
            if (action == remove) removeSelectedStep();
            else if (assign && action == assign) colorPalettePanel->assignToCue(step.reference);
        });
        auto removeKey = new QShortcut(QKeySequence::Delete, cues);
        removeKey->setContext(Qt::WidgetShortcut);
        connect(removeKey, &QShortcut::activated, this, [this] { removeSelectedStep(); });
        cues->verticalHeader()->show();
        cues->setAlternatingRowColors(true);
        cues->setSelectionBehavior(QAbstractItemView::SelectRows);
        cues->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
        sequenceLayout->addWidget(cues, 1);
        auto edit = new QHBoxLayout;
        button("RECORD", edit, [this] { record(); })->setObjectName("recordButton");
        button("UPDATE", edit, [this] { updateStep(); })->setObjectName("updateStepButton");
        button("Record Only", edit, [this] { record(true); });
        edit->addStretch();
        cueOnly = new QCheckBox("Cue Only"); cueOnly->setObjectName("cueOnlyMode"); edit->addWidget(cueOnly);
        sequenceLayout->addLayout(edit);
        auto steps = new QHBoxLayout;
        button("Insert cue…", steps, [this] { chooseCue(); })->setObjectName("insertCueButton");
        button("Link…", steps, [this] { linkStep(); })->setObjectName("linkStepButton");
        steps->addStretch();
        button("SET NEXT", steps, [this] {
            if (engine.setNextCue(selectedSequence, cues->currentRow())) {
                refresh();
                message("Next GO will play the selected cue. Current output and stored links are unchanged.");
            }
        })->setObjectName("setNextButton");
        button("GO TO", steps, [this] {
            int i = cues->currentRow();
            if (i >= 0) {
                engine.go(selectedSequence, i);
                refresh();
            }
        })->setObjectName("gotoButton");
        sequenceLayout->addLayout(steps);
        auto extra = new QHBoxLayout;
        button("Tracking", extra, [this] { workspaceTabs->setCurrentWidget(trackingPanel); })->setObjectName("trackingButton");
        button("Channel timing…", extra, [this] { channelTiming(); });
        extra->addStretch();
        sequenceLayout->addLayout(extra);
        auto subPage = new QWidget;
        auto subLayout = new QVBoxLayout(subPage);
        subLayout->addWidget(new QLabel("SUBMASTERS · INTENSITY PLAYBACK"));
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
        auto paletteTabs = new QTabWidget; paletteTabs->setObjectName("paletteTabs");
        paletteLayout->addWidget(paletteTabs);
        auto intensityPage = new QWidget;
        auto intensityLayout = new QVBoxLayout(intensityPage);
        palettes = new QListWidget;
        palettes->setViewMode(QListView::IconMode);
        palettes->setResizeMode(QListView::Adjust);
        palettes->setMovement(QListView::Static);
        palettes->setGridSize(QSize(170, 80));
        intensityLayout->addWidget(palettes, 3);
        paletteId = new QSpinBox;
        paletteId->setRange(1, 9999);
        paletteId->setPrefix("IP  ");
        intensityLayout->addWidget(paletteId);
        button("Record palette", intensityLayout, [this] {
            engine.recordPalette(paletteId->value());
            changed();
            rebuild();
        });
        button("Apply palette", intensityLayout, [this] {
            engine.applyPalette(paletteId->value());
            refresh();
        });
        colorPalettePanel = new ColorPalettePanel(engine, [this] { changed(); }, [this] { refresh(); });
        paletteTabs->addTab(colorPalettePanel, "Color");
        paletteTabs->addTab(intensityPage, "Intensity");
        workspaceTabs->addTab(palettePage, "Palettes");
        libraryPanel = new CueLibraryPanel(engine, [this] { changed(); }, [this](QString number) { insertCue(number); });
        libraryPanel->assignColor = [this](QString number) { colorPalettePanel->assignToCue(number); };
        workspaceTabs->addTab(libraryPanel, "Cue library");
        trackingPanel = new BlindDialog(engine, selectedSequence, [this] { changed(); }, workspaceTabs, true);
        trackingPanel->sequenceSelected = [this](int index) {
            selectedSequence = index;
            for (int tab = 0; tab < sequences->count(); ++tab) if (sequences->tabData(tab).toInt() == index) sequences->setCurrentIndex(tab);
            refresh();
        };
        workspaceTabs->addTab(trackingPanel, "Tracking");
        outputPanel = new OutputPanel(network, engine, [this] {
            dirty = true; setWindowModified(true);
            message(network.enabled ? "DMX outputs enabled" : "DMX outputs off");
        });
        channelTabs->addTab(outputPanel, "Network");
        parameterPanel = new ParameterPanel(engine, [this] { refresh(); }, channelTabs);
        parameterPanel->paletteChanged = [this] { changed(); };
        channelTabs->addTab(parameterPanel, "Parameters");
        connect(channels, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
            const int id = item->data(Qt::UserRole).toInt();
            if (colorControls(engine, id).available()) chooseChannelColor(id);
            else { parameterPanel->selectChannel(id); channelTabs->setCurrentWidget(parameterPanel); }
        });
        channels->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(channels, &QListWidget::customContextMenuRequested, this, [this](QPoint point) {
            auto item = channels->itemAt(point); if (!item) return;
            const int id = item->data(Qt::UserRole).toInt();
            QMenu menu(channels); menu.setObjectName("channelContextMenu");
            QAction *color = nullptr;
            if (colorControls(engine, id).available()) color = menu.addAction("Choose color…");
            auto parameters = menu.addAction("Fixture parameters…");
            auto action = menu.exec(channels->viewport()->mapToGlobal(point));
            if (color && action == color) chooseChannelColor(id);
            else if (action == parameters) { parameterPanel->selectChannel(id); channelTabs->setCurrentWidget(parameterPanel); }
        });
        connect(channelTabs, &QTabWidget::currentChanged, this, [this] {
            if (channelTabs->currentWidget() == parameterPanel) {
                if (!engine.selection.isEmpty()) parameterPanel->selectChannel(*std::min_element(engine.selection.begin(), engine.selection.end()));
                parameterPanel->refresh();
            }
        });
        split->addWidget(cuePanel);
        split->setSizes({520, 720});
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
            "background:#b7cfc1;color:#171819;font-size:19px;font-weight:bold;padding:12px 38px;");
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
            if (row < 0 || row >= engine.sequences[selectedSequence].cues.size()) return;
            if (column >= 10 && column <= 13) {
                auto &step = engine.sequences[selectedSequence].cues[row];
                const QString text = item->text().trimmed();
                if (column <= 11) {
                    std::optional<double> value;
                    if (!text.isEmpty()) {
                        bool ok; const double seconds = text.toDouble(&ok);
                        if (!ok || !std::isfinite(seconds) || (column == 10 && seconds < 0)) {
                            message("Follow must be non-negative; Hang may be negative. Enter seconds or clear the cell."); refresh(); return;
                        }
                        value = seconds;
                    }
                    if (column == 10) { step.follow = value; if (value) step.hang.reset(); }
                    else { step.hang = value; if (value) step.follow.reset(); }
                } else {
                    if (!validStepTarget(text)) { message("Target must be an existing step ID or sequence/step ID"); refresh(); return; }
                    if (column == 12) step.execute = text; else step.after = text;
                }
                changed(); return;
            }
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
        connect(cues, &QTableWidget::currentCellChanged, this, [this] { refresh(); });
        connect(workspaceTabs, &QTabWidget::currentChanged, this, [this] { if (workspaceTabs->currentWidget() == trackingPanel) trackingPanel->reload(selectedSequence); refresh(); });
        connect(libraryPanel->findChild<QTableWidget *>("storedCueTable"), &QTableWidget::itemSelectionChanged,
            this, [this] { refresh(); });
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
                if (trackingPanel) trackingPanel->reload(selectedSequence);
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
        fixtures->addAction("Define fixture…", this, [this] { channelTabs->setCurrentIndex(1); patchPanel->defineFixture(); });
        fixtures->addAction("Patch fixtures / import and export…", this, [this] { patch(); });
        fixtures->addAction("Live parameters…", this, [this] { channelTabs->setCurrentWidget(parameterPanel); });
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
            text += "Patch tools in the Patch tab can import this profile, select a supported mode and export GDTF.\n";
            for (const auto &mode : profile.modes) if (!mode.unsupported.isEmpty()) text += mode.name + ": " + mode.unsupported + "\n";
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
        if (startupOutput) {
            QString error;
            if (!network.startDefaultArtNet(&error)) message("Cannot start Art-Net: " + error);
            else message("Ready · Art-Net broadcast active · Ctrl+Space: GO · Ctrl+S: save");
            outputPanel->reload();
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
    if (app.arguments().contains("--fixture-search-screenshot"))
        QTimer::singleShot(50, [&w] {
            w.findChild<QTabWidget *>("channelTabs")->setCurrentIndex(1);
            auto picker = w.findChild<FixturePicker *>("fixtureMode1");
            if (!picker) return;
            QTimer::singleShot(100, &w, [&w] {
                if (auto dialog = w.findChild<QDialog *>("fixtureSearchDialog")) {
                    dialog->findChild<QLineEdit *>("fixtureSearch")->setText("RGB");
                    dialog->grab().save("fixture-search-preview.png"); dialog->reject();
                }
            });
            picker->showPopup();
        });
    if (app.arguments().contains("--network-screenshot"))
        QTimer::singleShot(50, [&w] {
            auto tabs = w.findChild<QTabWidget *>("channelTabs");
            tabs->setCurrentIndex(2);
            w.findChild<QPushButton *>("addDmxOutput")->click();
            QTimer::singleShot(150, [&w] { w.grab().save("network-preview.png"); });
        });
    if (app.arguments().contains("--multicell-screenshot"))
        QTimer::singleShot(50, [&w] {
            auto sheet = w.findChild<QTableWidget *>("cueSheet"); sheet->selectRow(0); w.findChild<QPushButton *>("gotoButton")->click();
            w.findChild<QTabWidget *>("channelTabs")->setCurrentIndex(3);
            auto channels = w.findChild<QComboBox *>("parameterChannel"); channels->setCurrentIndex(channels->findData(101));
            w.findChild<QComboBox *>("parameterCell")->setCurrentIndex(2);
            QTimer::singleShot(150, &w, [&w] { w.grab().save("multicell-preview.png"); });
        });
    if (app.arguments().contains("--fixture-editor-screenshot"))
        QTimer::singleShot(50, [&w] {
            w.findChild<QTabWidget *>("channelTabs")->setCurrentIndex(1);
            auto patch = static_cast<PatchDialog *>(w.findChild<QDialog *>("patchDialog"));
            auto table = patch->findChild<QTableWidget *>("patchBank0");
            for (int row = 0; row < table->rowCount(); ++row)
                if (qobject_cast<QComboBox *>(table->cellWidget(row, 3))->currentData().toString() == "moving-rgbw") { table->selectRow(row); break; }
            QTimer::singleShot(100, &w, [&w] {
                if (auto editor = w.findChild<QDialog *>("fixtureEditor")) { editor->grab().save("fixture-editor-preview.png"); editor->reject(); }
            });
            patch->defineFixture(table->currentRow() >= 0);
        });
    if (app.arguments().contains("--submasters-screenshot"))
        QTimer::singleShot(50, [&w] {
            w.findChild<QTabWidget *>("workspaceTabs")->setCurrentIndex(1);
            QTimer::singleShot(150, [&w] { w.grab().save("submasters-preview.png"); });
        });
    if (app.arguments().contains("--library-screenshot"))
        QTimer::singleShot(50, [&w] {
            w.findChild<QTabWidget *>("workspaceTabs")->setCurrentIndex(3);
            QTimer::singleShot(150, [&w] { w.grab().save("cue-library-preview.png"); });
        });
    if (app.arguments().contains("--fixtures-screenshot"))
        QTimer::singleShot(50, [&w] {
            auto left = w.findChild<QTabWidget *>("channelTabs");
            auto selector = w.findChild<QComboBox *>("parameterChannel");
            selector->setCurrentIndex(qMax(0, selector->findData(102)));
            for (int i = 0; i < left->count(); ++i) if (left->tabText(i) == "Parameters") left->setCurrentIndex(i);
            auto right = w.findChild<QTabWidget *>("workspaceTabs");
            for (int i = 0; i < right->count(); ++i) if (right->tabText(i) == "Tracking") right->setCurrentIndex(i);
            w.findChild<QComboBox *>("trackingScope")->setCurrentIndex(2);
            QTimer::singleShot(150, [&w] { w.grab().save("fixtures-preview.png"); });
        });
    if (app.arguments().contains("--color-palettes-screenshot"))
        QTimer::singleShot(50, [&w] {
            auto tabs = w.findChild<QTabWidget *>("workspaceTabs");
            for (int i = 0; i < tabs->count(); ++i) if (tabs->tabText(i) == "Palettes") tabs->setCurrentIndex(i);
            QTimer::singleShot(150, [&w] { w.grab().save("color-palettes-preview.png"); });
        });
    if (app.arguments().contains("--color-screenshot"))
        QTimer::singleShot(50, [&w] {
            auto list = w.findChild<QListWidget *>("channelList");
            for (int i = 0; i < list->count(); ++i) {
                auto item = list->item(i);
                if (!item->data(Qt::UserRole + 5).value<QColor>().isValid()) continue;
                list->scrollToItem(item);
                QTimer::singleShot(150, [&w] {
                    if (auto dialog = w.findChild<QColorDialog *>("fixtureColorDialog")) {
                        dialog->grab().save("color-preview.png"); dialog->reject();
                    }
                });
                list->itemDoubleClicked(item); break;
            }
        });
    if (app.arguments().contains("--smoke-test"))
        QTimer::singleShot(300, &app, &QApplication::quit);
    return app.exec();
}

#endif
