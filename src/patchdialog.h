#pragma once
#include "engine.h"
#include <QtWidgets>
// Changes remain in the dialog until the entire patch passes validation.
class PatchDialog : public QDialog {
    Flame::Engine &engine;
    bool embedded;
    std::function<void()> onApply;
    QList<QTableWidget *> banks;
    QLabel *summary, *error;
    QLineEdit *filter;
    QSpinBox *first, *count, *universe, *address;
    void appendRow(QTableWidget *table, int channel, QString name, int absolute);
    void populate();
    void validate();

  public:
    explicit PatchDialog(Flame::Engine &engine, QWidget *parent = nullptr, bool embedded = false,
                         std::function<void()> onApply = {});
    void reload();
    bool apply();
};
