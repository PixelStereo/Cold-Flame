#include "blinddialog.h"
#include "channeldelegate.h"
#include "editing.h"
#include "engine.h"
#include "gdtf.h"
#include "network.h"
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
    QListWidget *channels, *sequences, *palettes;
    QTableWidget *cues;
    QLabel *playback;
    QProgressBar *progress;
    QLineEdit *command;
    QLabel *status, *selection;
    QDoubleSpinBox *fade;
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
            item->setData(Qt::UserRole + 3,
                          QString("%1.%2")
                              .arg((engine.patch.value(id) - 1) / 512 + 1)
                              .arg((engine.patch.value(id) - 1) % 512 + 1, 3, 10, QChar('0')));
            QStringList destinations;
            for (int a : engine.dmxAddresses(id))
                destinations << QString("%1.%2")
                                    .arg((a - 1) / 512 + 1)
                                    .arg((a - 1) % 512 + 1, 3, 10, QChar('0'));
            item->setToolTip(engine.channels[id] + "\nDMX: " + destinations.join(", "));
            if (destinations.size() > 1)
                item->setData(Qt::UserRole + 3,
                              destinations.first() + QString(" +%1").arg(destinations.size() - 1));
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
                             c.label,
                             QString::number(c.time, 'g', 5),
                             c.down < 0 ? "= Up" : QString::number(c.down, 'g', 5),
                             QString::number(c.wait, 'g', 5),
                             c.link,
                             QString::number(c.values.size()),
                             QString::number(c.channelTimes.size()),
                             i == s.current ? (s.paused                 ? "PAUSED"
                                               : s.elapsed < s.duration ? "FADING"
                                                                        : "ACTIVE")
                                            : (i == s.current + 1 ? "NEXT" : "")};
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
            for (int j = 0; j < vals.size(); ++j) {
                auto item = cues->item(i, j);
                if (!item) {
                    item = new QTableWidgetItem;
                    cues->setItem(i, j, item);
                }
                item->setToolTip(QString("Follow: %1 · Hang: %2\nExecute: %3\nAfter cue: %4")
                                     .arg(c.follow ? QString::number(*c.follow) : "—",
                                          c.hang ? QString::number(*c.hang) : "—", c.execute,
                                          c.after));
                if (j >= 6)
                    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                if (item->text() != vals[j])
                    item->setText(vals[j]);
                item->setForeground(QColor(i == s.current ? "#61dfbc" : "#d8e3eb"));
            }
        }
        QString current = s.current >= 0
                              ? s.cues[s.current].number + "  /  " + s.cues[s.current].label
                              : "Standby";
        QString next = s.current + 1 < s.cues.size() ? s.cues[s.current + 1].number : "—";
        if (s.current >= 0 && !s.cues[s.current].link.isEmpty())
            next = s.cues[s.current].link;

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
        for (auto i = engine.channels.begin(); i != engine.channels.end(); ++i) {
            auto item = new QListWidgetItem;
            item->setData(Qt::UserRole, i.key());
            item->setToolTip(i.value());
            item->setTextAlignment(Qt::AlignCenter);
            channels->addItem(item);
            item->setSelected(engine.selection.contains(i.key()));
        }
        sequences->blockSignals(true);
        sequences->clear();
        for (int i = 0; i < engine.sequences.size(); ++i)
            sequences->addItem(
                QString("%1   %2").arg(engine.sequences[i].number).arg(engine.sequences[i].name));
        selectedSequence = qBound(0, selectedSequence, int(engine.sequences.size()) - 1);
        sequences->setCurrentRow(selectedSequence);
        sequences->blockSignals(false);
        palettes->clear();
        for (auto i = engine.palettes.begin(); i != engine.palettes.end(); ++i) {
            auto item =
                new QListWidgetItem(QString("IP %1   ·   %2 channels").arg(i.key()).arg(i->size()));
            item->setData(Qt::UserRole, i.key());
            palettes->addItem(item);
        }
        fitChannels();
        refresh();
    }
    void record(bool manualOnly = false) {
        QString number = engine.nextCueNumber(selectedSequence);
        if (manualOnly)
            engine.record(selectedSequence, number, "cue-" + number, fade->value(),
                          cueOnly->isChecked());
        else
            engine.recordLook(selectedSequence, number, "cue-" + number, fade->value(),
                              cueOnly->isChecked());
        changed();
        cues->selectRow(engine.sequences[selectedSequence].cues.size() - 1);
        message("Recorded cue-" + number + " · double-click the cue sheet to edit name or timing");
    }
    void fitChannels() {
        if (!channels)
            return;
        int n = channels->count();
        if (!n)
            return;
        auto size = channels->viewport()->size();
        int columns = qMax(
            1, int(std::ceil(std::sqrt(double(n) * size.width() / qMax(1, size.height()) * 0.8))));
        int rows = (n + columns - 1) / columns;
        const QSize cell(qMax(62, (size.width() - 6) / columns),
                         qMax(54, (size.height() - 6) / rows));
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
        form.addRow("Execute · cue or sequence/cue at start", &execute);
        form.addRow("After cue · completion trigger (extension)", &after);
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
    void submasters() {
        QDialog d(this);
        d.setWindowTitle("Submasters · HTP intensity");
        d.resize(720, 400);
        QVBoxLayout layout(&d);
        auto hint = new QLabel("Record selected channels (all live values if none selected). "
                               "Submasters combine by highest intensity.");
        hint->setWordWrap(true);
        layout.addWidget(hint);
        auto content = new QWidget;
        auto bank = new QVBoxLayout(content);
        auto scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setWidget(content);
        layout.addWidget(scroll);
        auto addRow = [&](int i) {
            auto row = new QHBoxLayout;
            auto name = new QLineEdit(engine.submasters[i].name);
            row->addWidget(name);
            connect(name, &QLineEdit::editingFinished, this, [this, name, i] {
                engine.submasters[i].name = name->text();
                changed();
            });
            auto fader = new QSlider(Qt::Horizontal);
            fader->setRange(0, 100);
            fader->setValue(qRound(engine.submasters[i].level * 100));
            row->addWidget(fader, 1);
            auto value = new QLabel(QString::number(fader->value()) + " %");
            row->addWidget(value);
            connect(fader, &QSlider::valueChanged, this, [this, i, value](int v) {
                engine.submasters[i].level = v / 100.;
                value->setText(QString::number(v) + " %");
                refresh();
            });
            bank->addLayout(row);
        };
        for (int i = 0; i < engine.submasters.size(); ++i)
            addRow(i);
        auto record = new QPushButton("+ Record submaster");
        layout.addWidget(record);
        auto release = new QPushButton("Release manual values");
        layout.addWidget(release);
        connect(release, &QPushButton::clicked, this, [this] {
            for (auto it = engine.manual.begin(); it != engine.manual.end();)
                if (!engine.captured.contains(it.key()))
                    it = engine.manual.erase(it);
                else
                    ++it;
            refresh();
        });
        connect(record, &QPushButton::clicked, &d, [&] {
            engine.recordSubmaster();
            changed();
            addRow(engine.submasters.size() - 1);
        });
        d.exec();
    }
    bool save() {
        if (path.isEmpty())
            path = QFileDialog::getSaveFileName(this, "Save show", {}, "Show (*.cfl)");
        if (path.isEmpty())
            return false;
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(engine.json()).toJson()) < 0 ||
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
        if (parse.error != QJsonParseError::NoError || !engine.load(doc.object(), &error)) {
            message("Cannot open show: " + error);
            return;
        }
        network.enabled = false;
        pixelRunning = false;
        path = p;
        dirty = false;
        setWindowModified(false);
        selectedSequence = 0;
        history.reset(engine);
        rebuild();
        message("Show loaded · outputs disabled");
    }
    void outputSettings() {
        QDialog d(this);
        d.setWindowTitle("Network outputs");
        QFormLayout l(&d);
        QComboBox proto;
        proto.addItems({"Art-Net", "sACN"});
        proto.setCurrentText(network.protocol);
        QLineEdit host(network.host);
        QSpinBox universe;
        universe.setRange(0, 63999);
        universe.setValue(network.firstUniverse);
        QCheckBox enable("Enable outputs (44 Hz)");
        enable.setChecked(network.enabled);
        l.addRow("Protocol", &proto);
        l.addRow("IP or multicast (sACN)", &host);
        l.addRow("First universe (Art-Net: zero-based)", &universe);
        l.addRow(&enable);
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        l.addRow(&buttons);
        connect(&buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(&buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        if (d.exec() == QDialog::Accepted) {
            network.protocol = proto.currentText();
            network.host = host.text();
            network.firstUniverse = universe.value();
            network.enabled = enable.isChecked();
            message(network.enabled ? "Outputs enabled" : "Outputs disabled");
        }
    }
    void patch() {
        PatchDialog dialog(engine, this);
        if (dialog.exec() == QDialog::Accepted) {
            changed();
            rebuild();
            message("Patch applied");
        }
    }

  protected:
    bool eventFilter(QObject *o, QEvent *e) override {
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
    Console(QString showFile = {}) {
        setWindowTitle("COLD FLAME[*] — Lighting workspace");
        resize(1440, 940);
        setMinimumSize(1100, 720);
        setStyleSheet(R"(
 QWidget{background:#111820;color:#dce5ed;font:12px 'DejaVu Sans';}
 QLabel#title{font-size:23px;font-weight:700;letter-spacing:2px;color:#f5e5ca;}
 QLabel#section{color:#98aabc;font-size:11px;font-weight:700;letter-spacing:1px;padding:5px 0;}
 QLabel#playback{background:#253747;border-left:3px solid #efbc72;padding:14px;color:#f6dfb9;font-size:15px;font-weight:600;}
 QPushButton{background:#25323f;border:1px solid #3a4a5b;border-radius:4px;padding:9px 13px;}
 QPushButton:hover{background:#354b5e;border-color:#a7bdcb;} QPushButton:checked{background:#8b3c34;}
 QLineEdit,QDoubleSpinBox,QSpinBox,QComboBox{background:#1a2632;border:1px solid #3a4b5a;border-radius:3px;padding:7px;selection-background-color:#526a7c;}
 QListWidget,QTableWidget{background:#18222d;alternate-background-color:#1e2b37;border:1px solid #2c3a48;border-radius:4px;outline:0;}
 QListWidget::item{padding:10px;border-bottom:1px solid #293847;} QListWidget::item:selected{background:#354a59;color:#f6d39e;}
 QTableWidget::item{padding:4px;border-bottom:1px solid #2a3845;} QTableWidget::item:selected{background:#344b5c;color:#fff0db;}
 QHeaderView::section{background:#293847;color:#acbdca;border:0;border-bottom:1px solid #53606a;padding:9px 5px;font-size:10px;font-weight:700;}
 QTableWidget{gridline-color:#293847;} QMenuBar,QMenu{background:#1b2733;} QMenu::item:selected{background:#3a4c5e;}
 QSlider::groove:horizontal{height:5px;background:#3a4855;} QSlider::handle:horizontal{background:#efbc72;width:15px;margin:-6px 0;border-radius:3px;}
 QProgressBar{border:0;background:#26333f;max-height:4px;} QProgressBar::chunk{background:#efbc72;} QScrollBar:vertical{background:#18222d;width:10px;margin:0;} QScrollBar::handle:vertical{background:#3a4b5a;min-height:24px;border-radius:4px;} QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;} QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:none;}
 )");
        auto editMenu = menuBar()->addMenu("Edit");
        editMenu
            ->addAction("Undo show edit", this,
                        [this] {
                            if (history.undo(engine)) {
                                dirty = true;
                                setWindowModified(true);
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
        auto top = new QHBoxLayout;
        auto title = new QLabel("COLD FLAME");
        title->setObjectName("title");
        top->addWidget(title);
        top->addWidget(new QLabel("  /  LIGHTING WORKSPACE"));
        top->addStretch();
        button("PATCH", top, [this] { patch(); })->setObjectName("patchButton");
        button("SUBMASTERS", top, [this] { submasters(); });
        button("NETWORK", top, [this] { outputSettings(); });
        layout->addLayout(top);
        auto body = new QHBoxLayout;
        body->setSpacing(16);
        layout->addLayout(body, 1);
        auto label = [](QString text) {
            auto l = new QLabel(text);
            l->setObjectName("section");
            return l;
        };
        auto side = new QWidget;
        side->setFixedWidth(195);
        auto left = new QVBoxLayout(side);
        left->setContentsMargins(0, 0, 0, 0);
        left->setSpacing(8);
        body->addWidget(side);
        left->addWidget(label("01 / SEQUENCES"));
        sequences = new QListWidget;
        sequences->setObjectName("sequenceList");
        left->addWidget(sequences, 2);
        button("+ New sequence", left, [this] {
            selectedSequence = engine.addSequence();
            changed();
            rebuild();
        })->setObjectName("newSequenceButton");
        button("Sequence priority", left, [this] {
            bool ok;
            int p =
                QInputDialog::getInt(this, "Priority", "1 = lowest / 10 = highest",
                                     engine.sequences[selectedSequence].priority, 1, 10, 1, &ok);
            if (ok) {
                engine.sequences[selectedSequence].priority = p;
                changed();
            }
        });
        left->addSpacing(12);
        left->addWidget(label("02 / INTENSITY PALETTES"));
        palettes = new QListWidget;
        left->addWidget(palettes, 3);
        paletteId = new QSpinBox;
        paletteId->setRange(1, 9999);
        paletteId->setPrefix("IP  ");
        left->addWidget(paletteId);
        button("Record palette", left, [this] {
            engine.recordPalette(paletteId->value());
            changed();
            rebuild();
        });
        button("Apply palette", left, [this] {
            engine.applyPalette(paletteId->value());
            refresh();
        });
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
        bar->addWidget(label("03 / CHANNELS · LIVE OUTPUT"));
        bar->addStretch();
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
        split->addWidget(stage);
        auto cuePanel = new QWidget;
        auto cueLayout = new QVBoxLayout(cuePanel);
        cueLayout->setContentsMargins(8, 0, 0, 0);
        cueLayout->addWidget(label("04 / CUE SHEET · DOUBLE-CLICK TO EDIT"));
        cues = new QTableWidget(0, 9);
        cues->setObjectName("cueSheet");
        cues->setHorizontalHeaderLabels(
            {"CUE", "NAME", "UP s", "DOWN s", "WAIT s", "LINK", "VALUES", "INDIV.", "STATE"});
        cues->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        cues->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        cues->verticalHeader()->hide();
        cues->setAlternatingRowColors(true);
        cues->setSelectionBehavior(QAbstractItemView::SelectRows);
        cues->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
        cueLayout->addWidget(cues, 1);
        auto edit = new QHBoxLayout;
        fade = new QDoubleSpinBox;
        fade->setObjectName("recordFade");
        fade->setRange(0, 3600);
        fade->setValue(2);
        fade->setSuffix(" s");
        edit->addWidget(fade);
        button("RECORD", edit, [this] { record(); })->setObjectName("recordButton");
        button("UPDATE", edit, [this] {
            int i = cues->currentRow();
            if (i < 0)
                return;
            auto c = engine.sequences[selectedSequence].cues[i];
            engine.record(selectedSequence, c.number, c.label, c.time, cueOnly->isChecked());
            changed();
        });
        button("GO TO", edit, [this] {
            int i = cues->currentRow();
            if (i >= 0) {
                engine.go(selectedSequence, i);
                refresh();
            }
        })->setObjectName("gotoButton");
        cueLayout->addLayout(edit);
        auto modes = new QHBoxLayout;
        cueOnly = new QCheckBox("Cue Only");
        modes->addWidget(cueOnly);
        button("Record Only", modes, [this] { record(true); });
        button("BLIND", modes, [this] {
            BlindDialog dialog(engine, selectedSequence, [this] { changed(); }, this);
            dialog.exec();
        });
        cueLayout->addLayout(modes);
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
        button("Delete cue", extra, [this] {
            int i = cues->currentRow();
            if (i < 0)
                return;
            if (QMessageBox::question(this, "Delete", "Delete selected cue?") == QMessageBox::Yes) {
                auto number = engine.sequences[selectedSequence].cues[i].number;
                engine.release(selectedSequence);
                engine.sequences[selectedSequence].cues.removeAt(i);
                auto qualified =
                    QString::number(engine.sequences[selectedSequence].number) + "/" + number;
                for (int source = 0; source < engine.sequences.size(); ++source)
                    for (auto &cue : engine.sequences[source].cues)
                        for (auto field : {&cue.link, &cue.execute, &cue.after})
                            if (*field == qualified ||
                                (source == selectedSequence && *field == number))
                                field->clear();
                changed();
            }
        });
        cueLayout->addLayout(extra);
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
            "background:#e6b775;color:#18212b;font-size:21px;font-weight:bold;padding:16px 70px;");
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
        layout->addWidget(status);
        connect(cues, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
            int row = item->row(), col = item->column();
            if (row < 0 || row >= engine.sequences[selectedSequence].cues.size() || col >= 6)
                return;
            auto &cue = engine.sequences[selectedSequence].cues[row];
            QString text = item->text().trimmed();
            bool ok = true;
            double n = text.toDouble(&ok);
            if (col == 1) {
                cue.label = text.isEmpty() ? "cue-" + cue.number : text;
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
                        message("Link target must exist: cue number or sequence/cue");
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
            engine.selection.clear();
            for (auto i : channels->selectedItems())
                engine.selection.insert(i->data(Qt::UserRole).toInt());
            refresh();
        });
        connect(level, &QSlider::valueChanged, this, [this](int v) {
            engine.level(v);
            refresh();
        });
        connect(sequences, &QListWidget::currentRowChanged, this, [this](int i) {
            if (i >= 0) {
                selectedSequence = i;
                refresh();
            }
        });
        connect(sequences, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
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
                            auto c = engine.sequences[selectedSequence].cues[row];
                            engine.record(selectedSequence, c.number, c.label, fade->value());
                            changed();
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
                engine.load(QJsonDocument::fromJson(f.readAll()).object(), &error))
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
        engine.defaultFade = fade->value();
        engine.cueOnlyMode = cueOnly->isChecked();
        if (engine.command(command->text(), &error)) {
            if (engine.json() != before)
                changed();
            selectedSequence = engine.commandSequence;
            sequences->setCurrentRow(selectedSequence);
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
            QTimer::singleShot(200, [] {
                for (QWidget *widget : QApplication::topLevelWidgets())
                    if (widget->objectName() == "patchDialog") {
                        widget->grab().save("patch-preview.png");
                        qobject_cast<QDialog *>(widget)->reject();
                    }
            });
            auto b = w.findChild<QPushButton *>("patchButton");
            b->click();
        });
    if (app.arguments().contains("--smoke-test"))
        QTimer::singleShot(300, &app, &QApplication::quit);
    return app.exec();
}

#endif
