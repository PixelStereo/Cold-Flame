#pragma once
#include "engine.h"
#include <QtWidgets>
class ParameterPanel : public QWidget {
    Flame::Engine &engine;
    QComboBox *channel;
    QTableWidget *table;
    QLabel *description;
    std::function<void()> onLiveChange;
    int shown = 0;
    void populate(int id);
  public:
    ParameterPanel(Flame::Engine &engine, std::function<void()> onLiveChange, QWidget *parent = nullptr);
    void reload();
    void refresh();
    void selectChannel(int id);
};
