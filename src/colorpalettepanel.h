#pragma once
#include "fixturecolor.h"
#include <QtWidgets>
class ColorPalettePanel : public QWidget {
    Flame::Engine &engine;
    std::function<void()> changed, liveChanged;
    QTableWidget *table;
    QLabel *feedback;
    int selected() const;
    void editColor();
  public:
    ColorPalettePanel(Flame::Engine &, std::function<void()> changed,
                      std::function<void()> liveChanged, QWidget *parent = nullptr);
    void reload(int select = -1);
    void assignToCue(QString cue = {});
};
