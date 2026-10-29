#pragma once
#include "gdtf.h"
#include <QtWidgets>

class FixtureEditor : public QDialog {
    Flame::FixtureProfile draft;
    int current = 0;
    QListWidget *modes;
    QLineEdit *manufacturer, *name, *modeName;
    QTableWidget *parameters;
    QLabel *error, *footprint;
    QDialogButtonBox *buttons;
    bool reading = false;
    bool readMode(Flame::FixtureMode &mode, QString &issue) const;
    bool readProfile(Flame::FixtureProfile &profile, QString &issue) const;
    void loadMode(int index);
    void validate();
    void addParameter(Flame::FixtureParameter parameter);
  public:
    explicit FixtureEditor(Flame::FixtureProfile source = {}, QWidget *parent = nullptr);
    Flame::FixtureProfile profile() const;
};
