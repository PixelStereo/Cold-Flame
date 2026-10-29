#pragma once
#include "engine.h"
#include <QtWidgets>
class BlindDialog : public QDialog {
    Flame::Engine &engine;
    int sequence;
    QTableWidget *sheet;
    QCheckBox *cueOnly, *trace, *forceTrace;
    QComboBox *scope, *sequencePicker;
    QLabel *feedback;
    std::function<void()> onChange;
    void populate();
  public:
    std::function<void(int)> sequenceSelected;
    BlindDialog(Flame::Engine &engine, int sequence, std::function<void()> onChange,
                QWidget *parent = nullptr, bool embedded = false);
    void reload(int sequence);
};
