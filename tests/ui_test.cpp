#define FLAME_NO_MAIN
#include "../src/main.cpp"
#include <QtTest>
class UiTests : public QObject {
    Q_OBJECT
  private slots:
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
            if (list->count() == 48) {
                QCOMPARE(list->selectedItems().size(), 4);
                QVERIFY(list->item(0)->text().contains("50 %"));
                selection = true;
            }
        QVERIFY(selection);
        for (auto b : window.findChildren<QPushButton *>())
            if (b->text() == "RELEASE MANUEL")
                QTest::mouseClick(b, Qt::LeftButton);
        for (auto list : window.findChildren<QListWidget *>())
            if (list->count() == 48)
                QVERIFY(list->item(0)->text().contains("0 %"));
    }
};
QTEST_MAIN(UiTests)
#include "ui_test.moc"
