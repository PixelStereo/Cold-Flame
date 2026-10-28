#pragma once
#include "network.h"
#include <QtWidgets>
#include <functional>
class OutputPanel : public QWidget {
    Flame::Network &network;
    const Flame::Engine &engine;
    std::function<void()> edited;
    QList<Flame::DmxOutput> draft;
    QTableWidget *table;
    QComboBox *protocol, *mode, *interface;
    QLineEdit *name, *addresses, *source;
    QSpinBox *patchFirst, *wireFirst, *count, *rate, *priority;
    QLabel *feedback, *route;
    QFormLayout *form;
    bool updating = false, pending = false;
    int selected = -1;
    void store();
    void select(int row);
    void rows();
    void updateProtocol();
    void states();
  public:
    OutputPanel(Flame::Network &, const Flame::Engine &, std::function<void()> edited, QWidget *parent = nullptr);
    void reload();
    bool apply();
};
