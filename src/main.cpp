#include "engine.h"
#include "gdtf.h"
#include "network.h"
#include <QtWidgets>
#include <cmath>
using namespace Flame;
class Console : public QMainWindow {
    Engine engine;
    Network network;
    QTimer timer;
    QElapsedTimer elapsed;
    QListWidget *channels, *sequences, *palettes;
    QTableWidget *cues;
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
        dirty = true;
        setWindowModified(true);
        refresh();
    }
    void message(QString text) { status->setText(text); }
    void refresh() {
        auto out = engine.output();
        for (int i = 0; i < channels->count(); ++i) {
            auto item = channels->item(i);
            int id = item->data(Qt::UserRole).toInt();
            double v = out.value(id);
            item->setText(QString("%1\n%2\n%3 %")
                              .arg(id, 2, 10, QChar('0'))
                              .arg(engine.channels[id])
                              .arg(v, 0, 'f', 0));
            item->setBackground(v > 0 ? QColor::fromHsv(38, 120, 55 + int(v * 1.3))
                                      : QColor("#17232f"));
        }
        selection->setText(QString("%1 channels selected  •  %2 manual values")
                               .arg(engine.selection.size())
                               .arg(engine.manual.size()));
        auto &s = engine.sequences[selectedSequence];
        cues->setRowCount(s.cues.size());
        for (int i = 0; i < s.cues.size(); ++i) {
            auto c = s.cues[i];
            QStringList vals{c.number, c.label, QString::number(c.time, 'f', 1) + " s",
                             QString::number(c.values.size()), i == s.current ? "● RUNNING" : ""};
            for (int j = 0; j < vals.size(); ++j) {
                auto item = cues->item(i, j);
                if (!item) {
                    item = new QTableWidgetItem;
                    cues->setItem(i, j, item);
                }
                item->setText(vals[j]);
                item->setForeground(QColor(i == s.current ? "#61dfbc" : "#d8e3eb"));
            }
        }
    }
    void rebuild() {
        channels->clear();
        for (auto i = engine.channels.begin(); i != engine.channels.end(); ++i) {
            auto item = new QListWidgetItem;
            item->setData(Qt::UserRole, i.key());
            item->setTextAlignment(Qt::AlignCenter);
            channels->addItem(item);
        }
        sequences->blockSignals(true);
        sequences->clear();
        for (int i = 0; i < engine.sequences.size(); ++i)
            sequences->addItem(QString("%1   %2").arg(i + 1).arg(engine.sequences[i].name));
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
        refresh();
    }
    void record() {
        bool ok;
        QString n = QInputDialog::getText(this, "Record cue", "Number (e.g. 12.5)",
                                          QLineEdit::Normal, "", &ok);
        if (!ok)
            return;
        double number = n.toDouble(&ok);
        if (!ok || number <= 0 || number > 9999.999) {
            message("Invalid cue number");
            return;
        }
        n = QString::number(number, 'f', 3);
        while (n.endsWith('0'))
            n.chop(1);
        if (n.endsWith('.'))
            n.chop(1);
        QString label =
            QInputDialog::getText(this, "Cue " + n, "Label", QLineEdit::Normal, "", &ok);
        if (!ok)
            return;
        engine.record(selectedSequence, n, label, fade->value());
        changed();
        message("Cue recorded · Release to clear manual values");
    }
    bool save() {
        if (path.isEmpty())
            path = QFileDialog::getSaveFileName(this, "Save show", {},
                                                "Show (*.cfl)");
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
        bool ok;
        int id = QInputDialog::getInt(this, "Patch", "Channel", 1, 1, 10000, 1, &ok);
        if (!ok)
            return;
        int address = QInputDialog::getInt(this, "Patch", "Absolute address (second universe starts at 513)",
                                           engine.patch.value(id, 1), 1, 32768 * 512, 1, &ok);
        if (!ok)
            return;
        for (auto i = engine.patch.begin(); i != engine.patch.end(); ++i)
            if (i.key() != id && i.value() == address) {
                message("Address already in use");
                return;
            }
        engine.channels[id] = QString("Channel %1").arg(id);
        engine.patch[id] = address;
        changed();
        rebuild();
    }

  protected:
    void closeEvent(QCloseEvent *e) override {
        if (discard())
            e->accept();
        else
            e->ignore();
    }

  public:
    Console() {
        setWindowTitle("COLD FLAME[*] — Lighting workspace");
        resize(1440, 940);
        setStyleSheet(
            "QWidget{background:#101923;color:#d8e3eb;font:13px 'DejaVu Sans';} "
            "QFrame#panel{background:#15212d;border:1px solid #293847;border-radius:10px;} "
            "QLabel#title{font-size:24px;font-weight:bold;color:#f4f8fb;} "
            "QPushButton{background:#223342;border:1px solid "
            "#354b5d;border-radius:5px;padding:10px;} QPushButton:hover{background:#345267;} "
            "QLineEdit,QDoubleSpinBox,QSpinBox,QComboBox{background:#0b141d;border:1px solid "
            "#3a5062;padding:8px;border-radius:4px;} "
            "QListWidget,QTableWidget{background:#101923;border:0;outline:0;} "
            "QListWidget::item{padding:12px;border:1px solid "
            "#273a49;margin:3px;border-radius:5px;} QListWidget::item:selected{border:2px solid "
            "#6ce1be;color:white;} "
            "QHeaderView::section{background:#223342;padding:9px;border:0;color:#8ca5b9;} "
            "QTableWidget{gridline-color:#233343;} "
            "QSlider::groove:horizontal{height:6px;background:#2c4050;} "
            "QSlider::handle:horizontal{background:#6ce1be;width:16px;margin:-6px "
            "0;border-radius:8px;} QMenuBar,QMenu{background:#15212d;}");
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
        layout->setContentsMargins(22, 18, 22, 18);
        layout->setSpacing(16);
        auto top = new QHBoxLayout;
        auto title = new QLabel("COLD FLAME   /   LIVE");
        title->setObjectName("title");
        top->addWidget(title);
        top->addStretch();
        top->addWidget(new QLabel("THEATRE  ·  DANCE  ·  CONCERT     /     0.1 DEV"));
        button("NETWORK", top, [this] { outputSettings(); });
        layout->addLayout(top);
        auto body = new QHBoxLayout;
        layout->addLayout(body, 1);
        auto left = new QVBoxLayout;
        body->addLayout(left, 2);
        left->addWidget(new QLabel("SEQUENCES / PLAYBACKS"));
        sequences = new QListWidget;
        sequences->setMaximumHeight(140);
        left->addWidget(sequences);
        button("+ New sequence", left, [this] {
            bool ok;
            auto name = QInputDialog::getText(this, "Sequence", "Name", QLineEdit::Normal, "", &ok);
            if (ok && !name.isEmpty()) {
                Sequence s;
                s.name = name;
                engine.sequences << s;
                selectedSequence = engine.sequences.size() - 1;
                changed();
                rebuild();
            }
        });
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
        left->addWidget(new QLabel("INTENSITY PALETTES / REFERENCES"));
        palettes = new QListWidget;
        left->addWidget(palettes, 1);
        paletteId = new QSpinBox;
        paletteId->setRange(1, 9999);
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
        auto center = new QVBoxLayout;
        body->addLayout(center, 6);
        auto bar = new QHBoxLayout;
        bar->addWidget(new QLabel("CHANNELS / LIVE OUTPUT"));
        bar->addStretch();
        button("Select all", bar, [this] { channels->selectAll(); });
        button("Patch +", bar, [this] { patch(); });
        center->addLayout(bar);
        channels = new QListWidget;
        channels->setViewMode(QListView::IconMode);
        channels->setResizeMode(QListView::Adjust);
        channels->setGridSize(QSize(112, 95));
        channels->setSelectionMode(QAbstractItemView::ExtendedSelection);
        center->addWidget(channels, 3);
        selection = new QLabel;
        center->addWidget(selection);
        auto levels = new QHBoxLayout;
        levels->addWidget(new QLabel("LEVEL"));
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
        button("RELEASE MANUAL", levels, [this] {
            engine.manual.clear();
            refresh();
        });
        center->addLayout(levels);
        center->addWidget(new QLabel("CUES / TRACKING"));
        cues = new QTableWidget(0, 5);
        cues->setHorizontalHeaderLabels({"Cue", "Label", "Fade", "Values", "Status"});
        cues->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        cues->verticalHeader()->hide();
        cues->setSelectionBehavior(QAbstractItemView::SelectRows);
        cues->setEditTriggers(QAbstractItemView::NoEditTriggers);
        center->addWidget(cues, 2);
        auto edit = new QHBoxLayout;
        fade = new QDoubleSpinBox;
        fade->setRange(0, 3600);
        fade->setValue(2);
        fade->setSuffix(" s");
        edit->addWidget(fade);
        button("RECORD", edit, [this] { record(); });
        button("UPDATE", edit, [this] {
            int i = cues->currentRow();
            if (i < 0)
                return;
            auto c = engine.sequences[selectedSequence].cues[i];
            engine.record(selectedSequence, c.number, c.label, fade->value());
            changed();
        });
        button("GO TO", edit, [this] {
            int i = cues->currentRow();
            if (i >= 0) {
                engine.go(selectedSequence, i);
                refresh();
            }
        });
        button("DELETE", edit, [this] {
            int i = cues->currentRow();
            if (i < 0)
                return;
            if (QMessageBox::question(this, "Delete", "Delete the selected cue?") ==
                QMessageBox::Yes) {
                engine.release(selectedSequence);
                engine.sequences[selectedSequence].cues.removeAt(i);
                changed();
            }
        });
        center->addLayout(edit);
        auto transport = new QHBoxLayout;
        button("↶ BACK", transport, [this] {
            engine.back(selectedSequence);
            refresh();
        });
        button("Ⅱ PAUSE / RESUME", transport, [this] {
            auto &s = engine.sequences[selectedSequence];
            s.paused = !s.paused;
            message(s.paused ? "Sequence paused" : "Sequence resumed");
        });
        auto go = button(
            "▶ GO", transport,
            [this] {
                engine.go(selectedSequence);
                refresh();
            },
            "background:#246e5c;color:white;font-size:20px;font-weight:bold;padding:18px 48px;");
        go->setShortcut(QKeySequence("Ctrl+Space"));
        button("RELEASE SEQUENCE", transport, [this] {
            engine.release(selectedSequence);
            refresh();
        });
        center->addLayout(transport);
        auto bottom = new QHBoxLayout;
        command = new QLineEdit;
        command->setPlaceholderText("Channel 1 Thru 12 At 50  ·  Press Enter to execute");
        bottom->addWidget(command, 1);
        button("EXECUTE", bottom, [this] { execute(); });
        auto bo = button("BLACKOUT", bottom, [this] {
            engine.blackout = !engine.blackout;
            message(engine.blackout ? "BLACKOUT ACTIVE" : "Blackout released");
            refresh();
        });
        bo->setCheckable(true);
        layout->addLayout(bottom);
        status = new QLabel(
            "Ready · network outputs disabled · Ctrl+Space: GO · Ctrl+S: save");
        layout->addWidget(status);
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
        connect(palettes, &QListWidget::itemClicked, this, [this](QListWidgetItem *i) {
            paletteId->setValue(i->data(Qt::UserRole).toInt());
            engine.applyPalette(paletteId->value());
            refresh();
        });
        // OSC is opt-in, bound only to loopback. Enable from the menu.
        auto tools = menuBar()->addMenu("Programming");
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
                        engine.manual.clear();
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
            double v = QInputDialog::getDouble(this, "Mapping", "Number of waves across the selection",
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
                if (m.hasMatch() && v > 0)
                    engine.go(m.captured(1).toInt() - 1);
                if (a == "/coldflame/master")
                    engine.master = qBound(0., double(v), 1.);
                if (a == "/coldflame/blackout")
                    engine.blackout = v > 0;
            }
        });
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
        if (engine.command(command->text(), &error)) {
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
    Console w;
    w.show();
    if (app.arguments().contains("--screenshot"))
        QTimer::singleShot(200, [&w] { w.grab().save("cold-flame-preview.png"); });
    if (app.arguments().contains("--smoke-test"))
        QTimer::singleShot(300, &app, &QApplication::quit);
    return app.exec();
}

#endif
