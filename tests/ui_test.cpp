#define FLAME_NO_MAIN
#include "../src/main.cpp"
#include <QtTest>
class UiTests : public QObject {
    Q_OBJECT
    static void dropStep(StepTable *table, int source, int beforeRow, bool gutter = false) {
        std::unique_ptr<QMimeData> mime(table->stepMimeData(source));
        const int y = beforeRow < table->rowCount() ? table->rowViewportPosition(beforeRow) + 1
            : table->rowViewportPosition(table->rowCount() - 1) + table->rowHeight(table->rowCount() - 1) + 2;
        QPoint point(8, y);
        QWidget *target = gutter ? table->verticalHeader()->viewport() : table->viewport();
        QDragEnterEvent enter(point, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &enter); QVERIFY(enter.isAccepted());
        QDragMoveEvent move(point, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &move); QVERIFY(move.isAccepted());
        QDropEvent drop(point, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &drop); QVERIFY(drop.isAccepted());
    }
    static void removeStepByContext(Console &window, QTableWidget *table) {
        QTimer::singleShot(0, &window, [&window] {
            auto menu = window.findChild<QMenu *>("stepContextMenu");
            if (!menu) return;
            auto action = menu->actions().first();
            menu->setActiveAction(action);
            QTest::keyClick(menu, Qt::Key_Return);
        });
        QPoint point = table->visualItemRect(table->item(table->currentRow(), 0)).center();
        table->customContextMenuRequested(point);
    }
  private slots:
    void fixturePatchParameterEditorAndRecall() {
        Engine e; PatchDialog patch(e); auto table = patch.findChild<QTableWidget *>("patchBank0");
        auto mode = patch.findChild<QComboBox *>("fixtureMode1"); QVERIFY(mode);
        mode->setCurrentIndex(mode->findData("moving-rgbw"));
        QVERIFY(!patch.apply()); QCOMPARE(e.fixtures.value(1).profile, QString("dimmer8"));
        QVERIFY(table->item(0, 2)->background().color() == QColor("#443c2c"));
        QVERIFY(table->item(1, 2)->background().color() == QColor("#443c2c")); // Both owners are highlighted.
        table->item(0, 2)->setText("2.001, 3.001"); QVERIFY(patch.apply());
        QCOMPARE(e.fixtureMode(1).footprint, 11);
        table->item(0, 2)->setText("2.510"); QVERIFY(!patch.apply());
        table->item(0, 2)->setText(""); QVERIFY(patch.apply()); QVERIFY(e.dmxAddresses(1).isEmpty());
        table->item(0, 2)->setText("2.001"); QVERIFY(patch.apply());
        ParameterPanel panel(e, [] {}); panel.show(); panel.refresh();
        auto parameters = panel.findChild<QTableWidget *>("parameterTable"); QCOMPARE(parameters->rowCount(), 9);
        int pan = e.parameterKey(1, 0); auto panField = panel.findChild<QDoubleSpinBox *>(QString("parameterLevel%1").arg(pan)); QVERIFY(panField);
        QCOMPARE(panField->value(), 50.); panField->setValue(80.); QCOMPARE(e.manual[pan].level, 80.);
        QVERIFY(e.storeCue("10")); e.insertStoredCue(0, "10"); e.sequences[0].cues[0].time = 0;
        panField->setValue(10.); e.go(0); panel.refresh(); QCOMPARE(panField->value(), 80.);
        mode->setCurrentIndex(mode->findData("rgb")); QVERIFY(!patch.apply()); QCOMPARE(e.fixtures[1].profile, QString("moving-rgbw"));
    }
    void trackingViewEditingAndTrace() {
        Engine e; e.manual[1] = {20, 0}; e.record(0, "10", "First", 0); e.manual.clear(); e.record(0, "20", "Second", 0);
        int changes = 0; BlindDialog tracking(e, 0, [&] { ++changes; }); tracking.show();
        auto sheet = tracking.findChild<QTableWidget *>("blindSheet"); QCOMPARE(sheet->item(0, 0)->data(Qt::UserRole + 1).toString(), QString("M"));
        QCOMPARE(sheet->item(0, 1)->data(Qt::UserRole + 1).toString(), QString("T"));
        tracking.findChild<QCheckBox *>("trackingTrace")->setChecked(true);
        sheet->item(0, 1)->setText("70"); QCOMPARE(e.tracked(0, 0)[1], 70.); QCOMPARE(e.tracked(0, 1)[1], 70.); QCOMPARE(changes, 1);
        tracking.findChild<QCheckBox *>("trackingTrace")->setChecked(false);
        sheet->item(0, 1)->setText("40"); QCOMPARE(e.tracked(0, 0)[1], 70.); QCOMPARE(e.tracked(0, 1)[1], 40.);
        sheet->item(0, 1)->setText(""); QCOMPARE(e.tracked(0, 1)[1], 70.); QCOMPARE(sheet->item(0, 1)->data(Qt::UserRole + 1).toString(), QString("T"));
        e.selection = {1}; tracking.findChild<QComboBox *>("trackingScope")->setCurrentIndex(1); QCOMPARE(sheet->rowCount(), 1);
        e.setChannelBlock(0, 1, 1, true); tracking.reload(0); QCOMPARE(sheet->item(0, 1)->data(Qt::UserRole + 1).toString(), QString("B"));
    }
    void balancedButtonLayout_data() {
        QTest::addColumn<QSize>("windowSize"); QTest::newRow("desktop") << QSize(1440, 940); QTest::newRow("compact") << QSize(1100, 720);
    }
    void balancedButtonLayout() {
        QFETCH(QSize, windowSize); Console window; window.resize(windowSize); window.show(); QCoreApplication::processEvents();
        auto record = window.findChild<QPushButton *>("recordButton"), insert = window.findChild<QPushButton *>("insertCueButton"), next = window.findChild<QPushButton *>("setNextButton");
        QVERIFY(record->height() >= 32); QVERIFY(record->height() <= 42);
        QCOMPARE(insert->mapTo(&window, QPoint()).y(), next->mapTo(&window, QPoint()).y());
        QVERIFY(record->mapTo(&window, QPoint()).y() < insert->mapTo(&window, QPoint()).y());
        auto sheet = window.findChild<QTableWidget *>("cueSheet"); QVERIFY(sheet->height() > 100);
        for (auto control : {record, insert, next}) {
            QVERIFY(control->width() >= control->fontMetrics().horizontalAdvance(control->text()) + 20);
            QVERIFY(control->mapTo(&window, control->rect().bottomRight()).x() < window.width());
        }
        window.findChild<QPushButton *>("trackingButton")->click();
        QCOMPARE(window.findChild<QTabWidget *>("workspaceTabs")->currentWidget()->objectName(), QString("blindDialog"));
    }
    void patchOverviewAndAtomicValidation() {
        Engine e;
        PatchDialog dialog(e);
        auto a = dialog.findChild<QTableWidget *>("patchBank0");
        QVERIFY(a);
        QCOMPARE(a->rowCount(), 120);
        QCOMPARE(dialog.findChildren<QTableWidget *>().size(), 1);
        dialog.show();
        QCoreApplication::processEvents();
        QVERIFY(a->columnWidth(2) > a->columnWidth(1));
        a->item(0, 2)->setText("1.002");
        QVERIFY(!dialog.apply());
        QCOMPARE(e.patch[1], 1);
        a->item(0, 2)->setText("1.200");
        a->item(0, 1)->setText("Front wash");
        QVERIFY(dialog.apply());
        QCOMPARE(e.patch[1], 200);
        QCOMPARE(e.channels[1], QString("Front wash"));
        a->item(11, 2)->setText("22.013, 3.018, 3.512");
        QVERIFY(dialog.apply());
        QCOMPARE(e.dmxAddresses(12),
                 QList<int>({(22 - 1) * 512 + 13, (3 - 1) * 512 + 18, (3 - 1) * 512 + 512}));
    }
    void recordDefaultsAndRecall() {
        Console window;
        window.show();
        auto command = window.findChildren<QLineEdit *>();
        QLineEdit *input = nullptr;
        for (auto edit : command)
            if (edit->placeholderText().startsWith("Channel"))
                input = edit;
        QVERIFY(input);
        QVERIFY(!window.findChild<QDoubleSpinBox *>("recordFade"));
        auto record = window.findChild<QPushButton *>("recordButton");
        auto table = window.findChild<QTableWidget *>("cueSheet");
        auto list = window.findChild<QListWidget *>("channelList");
        QCOMPARE(table->columnCount(), 15);
        QCOMPARE(table->horizontalHeaderItem(1)->text(), QString("CUE"));
        QVERIFY(!table->isColumnHidden(1));
        input->setText("1 at 20");
        window.execute();
        record->click();
        QCOMPARE(table->rowCount(), 0);
        window.findChild<QPushButton *>("insertStoredCueButton")->click();
        table->item(0, 3)->setText("0");
        QCOMPARE(table->item(0, 0)->text(), QString("1"));
        QCOMPARE(table->item(0, 1)->text(), QString("10"));
        QCOMPARE(table->item(0, 2)->text(), QString("cue-10"));
        input->setText("1 at 80");
        window.execute();
        record->click();
        window.findChild<QPushButton *>("insertStoredCueButton")->click();
        table->item(1, 3)->setText("0");
        QCOMPARE(table->item(1, 0)->text(), QString("2"));
        QCOMPARE(table->item(1, 1)->text(), QString("20"));
        QCOMPARE(table->item(1, 2)->text(), QString("cue-20"));
        table->selectRow(0);
        window.findChild<QPushButton *>("gotoButton")->click();
        QVERIFY(list->item(0)->text().contains("20 %"));
        table->selectRow(1);
        window.findChild<QPushButton *>("gotoButton")->click();
        QVERIFY(list->item(0)->text().contains("80 %"));
        window.findChild<QPushButton *>("newSequenceButton")->click();
        auto sequences = window.findChild<QTabBar *>("sequenceTabs");
        QVERIFY(sequences->tabText(1).contains("sequence-2"));
        sequences->moveTab(1, 0);
        sequences->setCurrentIndex(1);
        QCOMPARE(table->rowCount(), 2);
        table->selectRow(0);
        window.findChild<QPushButton *>("setNextButton")->click();
        QVERIFY(table->item(0, 9)->text().contains("NEXT"));
        window.findChild<QPushButton *>("goButton")->click();
        QVERIFY(list->item(0)->text().contains("20 %"));
        window.findChild<QPushButton *>("deleteSequenceButton")->click();
        QCOMPARE(sequences->count(), 1);
        // Avoid the unsaved-show prompt during test teardown.
    }
    void repeatedCueUiChoosesExactOccurrence() {
        Console window;
        window.show();
        QLineEdit *input = nullptr;
        for (auto edit : window.findChildren<QLineEdit *>())
            if (edit->placeholderText().startsWith("Channel")) input = edit;
        QVERIFY(input);
        input->setText("1 at 25"); window.execute();
        window.findChild<QPushButton *>("recordButton")->click();
        auto insert = window.findChild<QPushButton *>("insertStoredCueButton");
        insert->click(); insert->click(); insert->click();
        auto table = window.findChild<QTableWidget *>("cueSheet");
        QCOMPARE(table->rowCount(), 3);
        for (int row = 0; row < 3; ++row) {
            QCOMPARE(table->item(row, 1)->text(), QString("10"));
            QCOMPARE(table->item(row, 0)->text(), QString::number(row + 1));
        }
        table->selectRow(0);
        QTimer::singleShot(0, &window, [&] {
            auto dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto combo = dialog->findChild<QComboBox *>();
            if (!combo) { dialog->reject(); return; }
            combo->setCurrentIndex(3); // No link, then steps 1, 2, 3.
            dialog->accept();
        });
        window.findChild<QPushButton *>("linkStepButton")->click();
        QCOMPARE(table->item(0, 6)->text(), QString("1/3"));
        auto library = window.findChild<QTableWidget *>("storedCueTable");
        library->item(0, 0)->setText("15");
        QCOMPARE(table->item(2, 1)->text(), QString("15"));
        QCOMPARE(table->item(2, 2)->text(), QString("cue-15"));
        table->selectRow(2);
        dropStep(dynamic_cast<StepTable *>(table), 2, 1);
        QCOMPARE(table->item(1, 0)->text(), QString("3"));
        QCOMPARE(table->item(0, 6)->text(), QString("1/3"));
        removeStepByContext(window, table);
        QCOMPARE(table->rowCount(), 2); QVERIFY(table->item(0, 6)->text().isEmpty());
        QCOMPARE(library->rowCount(), 1);
    }
    void liveChannelModesAndSelectedMembership() {
        Console window; window.show();
        QLineEdit *input = nullptr;
        for (auto edit : window.findChildren<QLineEdit *>())
            if (edit->placeholderText().startsWith("Channel")) input = edit;
        QVERIFY(input);
        auto execute = [&](QString text) { input->setText(text); window.execute(); };
        auto list = window.findChild<QListWidget *>("channelList");
        auto used = window.findChild<QPushButton *>("channelViewUSED");
        auto edit = window.findChild<QPushButton *>("channelViewEDIT");
        auto all = window.findChild<QPushButton *>("channelViewALL");
        QCOMPARE(list->count(), 120); used->click(); QCOMPARE(list->count(), 0);
        execute("1 at 50"); execute("2 at 0");
        QCOMPARE(list->count(), 1); QCOMPARE(list->item(0)->data(Qt::UserRole).toInt(), 1);
        execute("record only cue 10"); execute("insert cue 10");
        auto table = window.findChild<QTableWidget *>("cueSheet");
        edit->click(); table->selectRow(0);
        QTRY_COMPARE(list->count(), 2); // Stored zero is part of the selected cue.
        QCOMPARE(list->item(1)->data(Qt::UserRole).toInt(), 2);
        execute("3 at 80"); used->click(); QCOMPARE(list->count(), 2);
        execute("2 at 30");
        window.findChild<QPushButton *>("recordSubmasterButton")->click();
        execute("select manual"); execute("release");
        QCOMPARE(list->count(), 0);
        window.findChild<QSlider *>("subFader1")->setValue(100);
        QCOMPARE(list->count(), 1); QCOMPARE(list->item(0)->data(Qt::UserRole).toInt(), 2);
        window.findChild<QSlider *>("subFader1")->setValue(0); QCOMPARE(list->count(), 0);
        table->item(0, 3)->setText("0"); table->selectRow(0);
        window.findChild<QPushButton *>("gotoButton")->click();
        QCOMPARE(list->count(), 1); QCOMPARE(list->item(0)->data(Qt::UserRole).toInt(), 1);
        edit->click(); QCOMPARE(list->count(), 2);
        all->click(); QCOMPARE(list->count(), 120);
        window.findChild<QPushButton *>("addChannelPageButton")->click();
        execute("121 at 70"); used->click();
        QCOMPARE(list->count(), 2); // Active channels from both 120-channel pages.
        QCOMPARE(list->item(1)->data(Qt::UserRole).toInt(), 121);
        auto tabs = window.findChild<QTabWidget *>("workspaceTabs");
        auto library = window.findChild<QTableWidget *>("storedCueTable");
        tabs->setCurrentWidget(library->parentWidget()); edit->click();
        QCOMPARE(list->count(), 2); QCOMPARE(list->item(1)->data(Qt::UserRole).toInt(), 2);
    }
    void inlineTriggersFlagsAndDragValidation() {
        Console window; window.show();
        QLineEdit *input = nullptr;
        for (auto edit : window.findChildren<QLineEdit *>())
            if (edit->placeholderText().startsWith("Channel")) input = edit;
        QVERIFY(input);
        input->setText("1 at 40"); window.execute();
        window.findChild<QPushButton *>("recordButton")->click();
        auto insert = window.findChild<QPushButton *>("insertStoredCueButton"); insert->click(); insert->click();
        auto table = dynamic_cast<StepTable *>(window.findChild<QTableWidget *>("cueSheet")); QVERIFY(table);
        QVERIFY(!table->verticalHeader()->isHidden());
        QVERIFY(table->columnViewportPosition(14) < table->columnViewportPosition(0));
        QVERIFY(!window.findChild<QPushButton *>("moveStepUpButton"));
        QVERIFY(!window.findChild<QPushButton *>("removeStepButton"));
        for (auto button : window.findChildren<QPushButton *>())
            QVERIFY(button->text() != "Cue library" && button->text() != "Triggers…" && button->text() != "Block" && button->text() != "Assert");
        table->item(0, 10)->setText("3"); QCOMPARE(table->item(0, 10)->text(), QString("3"));
        table->item(0, 11)->setText("-1"); QVERIFY(table->item(0, 10)->text().isEmpty());
        QCOMPARE(table->item(0, 11)->text(), QString("-1"));
        table->item(0, 10)->setText("-2"); QVERIFY(table->item(0, 10)->text().isEmpty());
        table->item(0, 12)->setText("999"); QVERIFY(table->item(0, 12)->text().isEmpty());
        table->item(0, 12)->setText("1/2"); QCOMPARE(table->item(0, 12)->text(), QString("1/2"));
        table->item(0, 13)->setText("2"); QCOMPARE(table->item(0, 13)->text(), QString("2"));
        auto block = window.findChild<QPushButton *>("blockStep1");
        auto assertFlag = window.findChild<QPushButton *>("assertStep1");
        QVERIFY(block); QVERIFY(assertFlag); block->click(); assertFlag->click();
        QVERIFY(block->isChecked()); QVERIFY(assertFlag->isChecked());
        dropStep(table, 0, 2, true);
        QCOMPARE(table->item(1, 0)->text(), QString("1"));
        QVERIFY(window.findChild<QPushButton *>("blockStep1")->isChecked());
        std::unique_ptr<QMimeData> stale(table->stepMimeData(0)); table->setSequenceContext("another sequence");
        QDragEnterEvent event(QPoint(10, 10), Qt::MoveAction, stale.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(table->viewport(), &event); QVERIFY(!event.isAccepted());
    }
    void contextualFooterAndNumberDragging() {
        Console window;
        window.show(); QCoreApplication::processEvents();
        QVERIFY(!window.findChild<QPushButton *>("patchButton"));
        QVERIFY(!window.findChild<QDoubleSpinBox *>("recordFade"));
        for (auto label : window.findChildren<QLabel *>())
            QVERIFY(label->text() != "COLD FLAME");
        auto button = window.findChild<QPushButton *>("setNextButton");
        QEnterEvent enter(QPointF(2, 2), QPointF(2, 2), button->mapToGlobal(QPoint(2, 2)));
        QApplication::sendEvent(button, &enter);
        auto help = window.findChild<QLabel *>("hoverHelp");
        QVERIFY(window.findChild<QSpinBox *>("dmxOutputUniverse")->property("numberDragInstalled").toBool());
        QVERIFY(help->text().contains("next GO"));
        const QSize size = help->size();
        QHelpEvent tooltip(QEvent::ToolTip, QPoint(2, 2), button->mapToGlobal(QPoint(2, 2)));
        QApplication::sendEvent(button, &tooltip);
        QVERIFY(!QToolTip::isVisible()); QCOMPARE(help->size(), size);
        QEvent leave(QEvent::Leave); QApplication::sendEvent(button, &leave);
        QVERIFY(help->text().isEmpty());
        QWidget panel;
        QVBoxLayout layout(&panel);
        QDoubleSpinBox spin;
        layout.addWidget(&spin);
        spin.setDecimals(2); spin.setRange(0, 1000); spin.setValue(20);
        spin.setSingleStep(1); enableNumberDrag(&spin);
        panel.setFocusPolicy(Qt::StrongFocus); panel.show(); QCoreApplication::processEvents();
        panel.activateWindow(); panel.setFocus(); spin.clearFocus();
        QVERIFY(!spin.hasFocus());
        auto edit = spin.findChild<QLineEdit *>(); QVERIFY(edit);
        QPoint start(10, edit->height() / 2);
        QTest::mousePress(edit, Qt::LeftButton, Qt::NoModifier, start);
        auto move = [&](int dx, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
            QPoint point = start + QPoint(dx, 0);
            QMouseEvent event(QEvent::MouseMove, point, edit->mapToGlobal(point), Qt::NoButton, Qt::LeftButton, modifiers);
            QApplication::sendEvent(edit, &event);
        };
        move(5); move(15);
        QTest::mouseRelease(edit, Qt::LeftButton, Qt::NoModifier, start + QPoint(15, 0));
        QCOMPARE(spin.value(), 30.); QVERIFY(!spin.hasFocus());
        QTest::mouseClick(edit, Qt::LeftButton, Qt::NoModifier, start);
        QVERIFY(spin.hasFocus()); QVERIFY(edit->hasSelectedText());
    }
    void submasterContentEditorAndFader() {
        Console window;
        window.show();
        QLineEdit *command = nullptr;
        for (auto edit : window.findChildren<QLineEdit *>())
            if (edit->placeholderText().startsWith("Channel")) command = edit;
        QVERIFY(command);
        command->setText("1 at 80"); window.execute();
        auto tabs = window.findChild<QTabWidget *>("workspaceTabs");
        tabs->setCurrentIndex(1);
        window.findChild<QPushButton *>("recordSubmasterButton")->click();
        for (auto button : window.findChildren<QPushButton *>())
            if (button->text() == "RELEASE MANUAL") button->click();
        auto fader = window.findChild<QSlider *>("subFader1");
        fader->setValue(50);
        auto channels = window.findChild<QListWidget *>("channelList");
        QCOMPARE(channels->item(0)->data(Qt::UserRole + 1).toDouble(), 40.);
        QTimer::singleShot(0, &window, [&] {
            auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto editor = dialog->findChild<QTableWidget *>("submasterEditor");
            if (!editor) { dialog->reject(); return; }
            editor->item(0, 2)->setText("60");
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        });
        for (auto button : window.findChildren<QPushButton *>())
            if (button->text() == "Edit…") { button->click(); break; }
        QCOMPARE(channels->item(0)->data(Qt::UserRole + 1).toDouble(), 30.);
    }
    void channelPagesContainOneHundredTwentyChannels() {
        Console window;
        window.show();
        auto list = window.findChild<QListWidget *>("channelList");
        auto pages = window.findChild<QComboBox *>("channelPages");
        QCOMPARE(list->count(), 120); QCOMPARE(pages->count(), 1);
        window.findChild<QPushButton *>("addChannelPageButton")->click();
        QCOMPARE(pages->count(), 2); QCOMPARE(list->count(), 120);
        QCOMPARE(list->item(0)->data(Qt::UserRole).toInt(), 121);
        QCOMPARE(list->item(119)->data(Qt::UserRole).toInt(), 240);
        pages->setCurrentIndex(0);
        QCOMPARE(list->item(0)->data(Qt::UserRole).toInt(), 1);
        QCOMPARE(list->count(), 120);
        QVERIFY(!list->item(0)->toolTip().contains("DMX"));
        auto tabs = window.findChild<QTabWidget *>("channelTabs");
        QCOMPARE(tabs->tabText(0), QString("Channels"));
        QCOMPARE(tabs->tabText(1), QString("Patch"));
        QCOMPARE(tabs->tabText(2), QString("Network"));
    }
    void outputPanelConfiguration() {
        Network network; Engine engine;
        int changes = 0;
        OutputPanel panel(network, engine, [&] { ++changes; });
        panel.show();
        panel.findChild<QPushButton *>("addDmxOutput")->click();
        auto table = panel.findChild<QTableWidget *>("dmxOutputsTable");
        QCOMPARE(table->rowCount(), 1);
        auto addresses = panel.findChild<QLineEdit *>("dmxOutputAddresses");
        addresses->setText("bad address");
        QVERIFY(!panel.apply()); QCOMPARE(changes, 0);
        addresses->setText("127.0.0.1");
        panel.findChild<QSpinBox *>("dmxOutputUniverse")->setValue(42);
        QVERIFY(panel.apply()); QCOMPARE(network.outputs[0].firstUniverse, 42);
        QVERIFY(!network.enabled);
        panel.findChild<QPushButton *>("addDmxOutput")->click();
        QCOMPARE(table->rowCount(), 2);
        panel.findChild<QPushButton *>("removeDmxOutput")->click();
        QVERIFY(panel.apply()); QCOMPARE(network.outputs.size(), 1);
        QCOMPARE(network.outputs[0].firstUniverse, 42);
    }
    void tableEditorsAndSubmasterBank() {
        Console window;
        window.show();
        QCoreApplication::processEvents();
        auto tabs = window.findChild<QTabWidget *>("workspaceTabs");
        QVERIFY(tabs->isMovable());
        tabs->setCurrentIndex(1);
        window.findChild<QPushButton *>("recordSubmasterButton")->click();
        auto fader = window.findChild<QSlider *>("subFader1");
        QVERIFY(fader);
        fader->setValue(60);
        QCOMPARE(fader->value(), 60);
        Engine engine;
        PatchDialog patch(engine, &window);
        patch.show();
        QCoreApplication::processEvents();
        auto table = patch.findChild<QTableWidget *>("patchBank0");
        table->editItem(table->item(0, 1));
        QCoreApplication::processEvents();
        auto editor = table->findChild<QLineEdit *>();
        QVERIFY(editor);
        QStyleOptionFrame option;
        option.initFrom(editor);
        const auto contents = editor->style()->subElementRect(QStyle::SE_LineEditContents, &option, editor);
        QVERIFY2(contents.height() >= editor->fontMetrics().height(), "Editor clips its text vertically");
    }
    void blindSpreadsheetEditsStoredOnly() {
        Engine e;
        e.selection = {1};
        e.level(30);
        e.record(0, "10", "", 0);
        e.manual.clear();
        e.go(0);
        int changes = 0;
        BlindDialog dialog(e, 0, [&] { ++changes; });
        auto table = dialog.findChild<QTableWidget *>("blindSheet");
        QVERIFY(table);
        table->item(0, 0)->setText("80");
        QCOMPARE(changes, 1);
        QCOMPARE(e.tracked(0, 0)[1], 80.);
        QCOMPARE(e.output()[1], 30.);
    }
    void channelGridRemainsStable_data() {
        QTest::addColumn<QSize>("windowSize");
        QTest::newRow("desktop") << QSize(1440, 940);
        QTest::newRow("compact") << QSize(1100, 720);
        QTest::newRow("fractional-columns") << QSize(1291, 853);
    }
    void channelGridRemainsStable() {
        QFETCH(QSize, windowSize);
        Console window;
        window.resize(windowSize);
        window.show();
        auto list = window.findChild<QListWidget *>("channelList");
        QLineEdit *input = nullptr;
        for (auto edit : window.findChildren<QLineEdit *>())
            if (edit->placeholderText().startsWith("Channel"))
                input = edit;
        QVERIFY(input);
        input->setText("1 at 0");
        window.execute();
        QTest::qWait(100);
        QList<QRect> bounds;
        for (int i = 0; i < list->count(); ++i)
            bounds << list->visualItemRect(list->item(i));
        for (const auto &r : bounds)
            QCOMPARE(r.size(), list->gridSize());
        auto viewport = list->viewport()->size();
        QSignalSpy changes(list->model(), &QAbstractItemModel::dataChanged);
        for (int level : {100, 9, 50, 0, 99}) {
            input->setText(QString("1 at %1").arg(level));
            window.execute();
            QTest::qWait(60);
            QCOMPARE(list->viewport()->size(), viewport);
            for (int i = 0; i < bounds.size(); ++i)
                QCOMPARE(list->visualItemRect(list->item(i)), bounds[i]);
        }
        // Level updates repaint tiles; they must not notify QListView to lay out its model again.
        QCOMPARE(changes.size(), 0);
    }
    void channelGridStaysPutDuringFade() {
        Console window;
        window.show();
        auto list = window.findChild<QListWidget *>("channelList");
        auto record = window.findChild<QPushButton *>("recordButton");
        auto cues = window.findChild<QTableWidget *>("cueSheet");
        QLineEdit *input = nullptr;
        for (auto edit : window.findChildren<QLineEdit *>())
            if (edit->placeholderText().startsWith("Channel"))
                input = edit;
        QVERIFY(input);
        input->setText("1 at 0");
        window.execute();
        record->click();
        window.findChild<QPushButton *>("insertStoredCueButton")->click();
        cues->item(0, 3)->setText("0");
        input->setText("1 at 100");
        window.execute();
        record->click();
        window.findChild<QPushButton *>("insertStoredCueButton")->click();
        cues->selectRow(0);
        window.findChild<QPushButton *>("gotoButton")->click();
        QTest::qWait(80);
        QList<QRect> rects;
        for (int i = 0; i < list->count(); ++i)
            rects << list->visualItemRect(list->item(i));
        window.findChild<QPushButton *>("goButton")->click();
        QTest::qWait(500);
        double value = list->item(0)->data(Qt::UserRole + 1).toDouble();
        QVERIFY(value > 0 && value < 100);
        for (int i = 0; i < rects.size(); ++i)
            QCOMPARE(list->visualItemRect(list->item(i)), rects[i]);
    }
    void commandAndSelection() {
        Console window;
        window.show();
        QLineEdit *command = nullptr;
        for (auto edit : window.findChildren<QLineEdit *>())
            if (edit->placeholderText().startsWith("Channel"))
                command = edit;
        QVERIFY(command);
        command->setFocus();
        QTest::keyClicks(command, "Channel 1 Thru 4 At 50");
        QTest::keyClick(command, Qt::Key_Return);
        bool selection = false;
        for (auto list : window.findChildren<QListWidget *>())
            if (list->count() == 120) {
                QCOMPARE(list->selectedItems().size(), 4);
                QVERIFY(list->item(0)->text().contains("50 %"));
                selection = true;
            }
        QVERIFY(selection);
        for (auto b : window.findChildren<QPushButton *>())
            if (b->text() == "RELEASE MANUAL")
                QTest::mouseClick(b, Qt::LeftButton);
        for (auto list : window.findChildren<QListWidget *>())
            if (list->count() == 120)
                QVERIFY(list->item(0)->text().contains("0 %"));
    }
};
QTEST_MAIN(UiTests)
#include "ui_test.moc"
