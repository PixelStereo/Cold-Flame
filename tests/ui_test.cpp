#define FLAME_NO_MAIN
#include "../src/main.cpp"
#include <QtTest>
class UiTests : public QObject {
    Q_OBJECT
  private slots:
    void patchOverviewAndAtomicValidation() {
        Engine e;
        PatchDialog dialog(e);
        auto a = dialog.findChild<QTableWidget *>("patchBank0");
        QVERIFY(a);
        QCOMPARE(a->rowCount(), 60);
        QCOMPARE(dialog.findChildren<QTableWidget *>().size(), 1);
        dialog.show();
        QCoreApplication::processEvents();
        QVERIFY(a->columnWidth(2) > a->columnWidth(1));
        a->item(0, 2)->setText("1.002");
        QVERIFY(!dialog.apply());
        QCOMPARE(e.patch[1], 1);
        a->item(0, 2)->setText("1.100");
        a->item(0, 1)->setText("Front wash");
        QVERIFY(dialog.apply());
        QCOMPARE(e.patch[1], 100);
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
        auto fade = window.findChild<QDoubleSpinBox *>("recordFade");
        fade->setValue(0);
        auto record = window.findChild<QPushButton *>("recordButton");
        auto table = window.findChild<QTableWidget *>("cueSheet");
        auto list = window.findChild<QListWidget *>("channelList");
        input->setText("1 at 20");
        window.execute();
        record->click();
        QCOMPARE(table->item(0, 0)->text(), QString("10"));
        QCOMPARE(table->item(0, 1)->text(), QString("cue-10"));
        input->setText("1 at 80");
        window.execute();
        record->click();
        QCOMPARE(table->item(1, 0)->text(), QString("20"));
        QCOMPARE(table->item(1, 1)->text(), QString("cue-20"));
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
        QVERIFY(table->item(0, 8)->text().contains("NEXT"));
        window.findChild<QPushButton *>("goButton")->click();
        QVERIFY(list->item(0)->text().contains("20 %"));
        window.findChild<QPushButton *>("deleteSequenceButton")->click();
        QCOMPARE(sequences->count(), 1);
        // Avoid the unsaved-show prompt during test teardown.
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
    void channelPagesContainSixtyChannels() {
        Console window;
        window.show();
        auto list = window.findChild<QListWidget *>("channelList");
        auto pages = window.findChild<QComboBox *>("channelPages");
        QCOMPARE(list->count(), 60); QCOMPARE(pages->count(), 1);
        window.findChild<QPushButton *>("addChannelPageButton")->click();
        QCOMPARE(pages->count(), 2); QCOMPARE(list->count(), 60);
        QCOMPARE(list->item(0)->data(Qt::UserRole).toInt(), 61);
        QCOMPARE(list->item(59)->data(Qt::UserRole).toInt(), 120);
        pages->setCurrentIndex(0);
        QCOMPARE(list->item(0)->data(Qt::UserRole).toInt(), 1);
        QCOMPARE(list->count(), 60);
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
        auto fade = window.findChild<QDoubleSpinBox *>("recordFade");
        auto record = window.findChild<QPushButton *>("recordButton");
        auto cues = window.findChild<QTableWidget *>("cueSheet");
        QLineEdit *input = nullptr;
        for (auto edit : window.findChildren<QLineEdit *>())
            if (edit->placeholderText().startsWith("Channel"))
                input = edit;
        QVERIFY(input);
        fade->setValue(0);
        input->setText("1 at 0");
        window.execute();
        record->click();
        fade->setValue(2);
        input->setText("1 at 100");
        window.execute();
        record->click();
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
            if (list->count() == 60) {
                QCOMPARE(list->selectedItems().size(), 4);
                QVERIFY(list->item(0)->text().contains("50 %"));
                selection = true;
            }
        QVERIFY(selection);
        for (auto b : window.findChildren<QPushButton *>())
            if (b->text() == "RELEASE MANUAL")
                QTest::mouseClick(b, Qt::LeftButton);
        for (auto list : window.findChildren<QListWidget *>())
            if (list->count() == 60)
                QVERIFY(list->item(0)->text().contains("0 %"));
    }
};
QTEST_MAIN(UiTests)
#include "ui_test.moc"
