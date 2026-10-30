#pragma once
#include "engine.h"
#include <QtWidgets>

// The closed picker stores six quick choices and its current custom choice only.
// Search builds one catalogue on demand, rather than duplicating it in every patch row.
class FixturePicker : public QComboBox {
    Q_OBJECT
    const QMap<QString, Flame::FixtureProfile> *profiles;
  public:
    explicit FixturePicker(const QMap<QString, Flame::FixtureProfile> &profiles, QWidget *parent = nullptr);
    void setFixture(Flame::FixtureAssignment assignment, bool notify = true);
    Flame::FixtureAssignment fixture() const;
    void showPopup() override;
  protected:
    void wheelEvent(QWheelEvent *event) override { event->ignore(); }
};
