#pragma once
#include "engine.h"
#include <QtWidgets>
class PatchDialog : public QDialog {
    Flame::Engine &engine;
    bool embedded;
    std::function<void()> onApply;
    QList<QTableWidget *> banks;
    QMap<QString, Flame::FixtureProfile> profiles;
    QLabel *summary, *error;
    QLineEdit *filter;
    QSpinBox *first, *count, *universe, *address;
    QComboBox *batchFixture;
    void appendRow(QTableWidget *table, int channel, QString name, int absolute,
                   Flame::FixtureAssignment assignment = {}, QList<int> extra = {});
    void populate();
    void validate();
    void fillModes(QComboBox *combo, Flame::FixtureAssignment selected = {});
    bool buildDraft(Flame::Engine &draft, QString &issue) const;
    void importProfile();
    void exportProfile();
    void importPatch();
    void exportPatch();
  public:
    explicit PatchDialog(Flame::Engine &engine, QWidget *parent = nullptr, bool embedded = false,
                         std::function<void()> onApply = {});
    void reload();
    bool apply();
};
