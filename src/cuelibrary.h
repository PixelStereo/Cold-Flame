#pragma once
#include "engine.h"
#include <QtWidgets>
#include <functional>
class CueLibraryPanel : public QWidget {
    Flame::Engine &engine;
    std::function<void()> changed;
    std::function<void(QString)> insert;
    QTableWidget *table;
    QLabel *feedback;
    void edit();
    QString selected() const;
  public:
    std::function<void(QString)> assignColor;
    CueLibraryPanel(Flame::Engine &, std::function<void()> changed,
                    std::function<void(QString)> insert, QWidget *parent = nullptr);
    QString selectedCue() const { return selected(); }
    void reload(QString select = {});
};
