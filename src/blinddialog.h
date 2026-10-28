#pragma once
#include "engine.h"
#include <QtWidgets>
class BlindDialog : public QDialog {
    Flame::Engine &engine;
    int sequence;
    QTableWidget *sheet;
    QCheckBox *cueOnly;
    std::function<void()> onChange;
    void populate();

  public:
    BlindDialog(Flame::Engine &engine, int sequence, std::function<void()> onChange,
                QWidget *parent = nullptr);
};
