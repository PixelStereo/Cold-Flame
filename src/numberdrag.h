#pragma once
#include <QtWidgets>
#include <cmath>
// Adapted from NyxMap src/ui/Widgets.cpp. Copyright (c) 2025-2026 PIXEL STEREO.
// MIT license: licenses/NyxMap-MIT.txt.
namespace Flame {
class NumberDrag : public QObject
{
public:
    NumberDrag(QAbstractSpinBox *spin, QLineEdit *edit) : QObject(spin), m_spin(spin), m_edit(edit)
    {
        edit->installEventFilter(this);
        spin->installEventFilter(this); // its focus (the text field's is given by it, past the filters)
        edit->setCursor(Qt::SizeHorCursor); // an I-beam only once editing
        spin->setCursor(Qt::SizeHorCursor);
        // A press must not give the focus by itself (Qt does it before any filter sees the press): the release
        // decides, a click edits the text, a drag does not. Tab still reaches the field.
        spin->setFocusPolicy(Qt::TabFocus);
        edit->setFocusPolicy(Qt::TabFocus);
    }

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (o == m_spin) {
            if (e->type() == QEvent::FocusIn) { m_edit->setCursor(Qt::IBeamCursor); m_spin->setCursor(Qt::IBeamCursor); return false; }
            if (e->type() == QEvent::FocusOut) { m_edit->setCursor(Qt::SizeHorCursor); m_spin->setCursor(Qt::SizeHorCursor); return false; }
            // Styled padding belongs to the spin box, outside its line edit.
            // Apply the same click/drag behavior to the entire visible field.
        }
        switch (e->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        case QEvent::MouseButtonRelease: break;
        default: return false;
        }
        auto *me = static_cast<QMouseEvent *>(e);
        // Editing the text: the mouse places the cursor and selects, as usual
        if (!m_pressed && (m_spin->hasFocus() || m_edit->hasFocus() || !m_spin->isEnabled() || m_spin->isReadOnly())) return false;
        const QPointF pos = me->globalPosition();
        if (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::MouseButtonDblClick) {
            if (me->button() != Qt::LeftButton) return false;
            m_pressed = true;
            m_dragging = false;
            m_start = pos;
            m_last = pos.x();
            m_carry = 0;
            return true; // neither focus nor text cursor yet
        }
        if (!m_pressed) return false;
        if (e->type() == QEvent::MouseMove) {
            if (!m_dragging) {
                if (std::abs(pos.x() - m_start.x()) < 3) return true;
                m_dragging = true;
                m_last = pos.x();
            }
            slide(pos.x() - m_last, me->modifiers());
            m_last = pos.x();
            return true;
        }
        // Release: a click without moving edits the text, all of it selected; a drag leaves the field as it was
        m_pressed = false;
        if (!m_dragging) {
            m_spin->setFocus(Qt::MouseFocusReason);
            m_spin->selectAll();
        }
        m_dragging = false;
        return true;
    }

private:
    void slide(double dx, Qt::KeyboardModifiers mods)
    {
        double factor = 1.0;
        if (mods & Qt::ShiftModifier) factor = 0.1;
        if (mods & Qt::ControlModifier) factor = 10.0;
        if (auto *d = qobject_cast<QDoubleSpinBox *>(m_spin)) {
            m_carry += dx * d->singleStep() * factor;
            const double unit = std::pow(10.0, -d->decimals());
            const double move = std::trunc(m_carry / unit) * unit; // whole units of what is shown
            if (move != 0) {
                m_carry -= move;
                d->setValue(d->value() + move);
            }
        } else if (auto *i = qobject_cast<QSpinBox *>(m_spin)) {
            m_carry += dx * i->singleStep() * factor;
            const int move = int(std::trunc(m_carry));
            if (move != 0) {
                m_carry -= move;
                i->setValue(i->value() + move);
            }
        }
    }

    QAbstractSpinBox *m_spin;
    QLineEdit *m_edit;
    QPointF m_start;
    double m_last = 0, m_carry = 0;
    bool m_pressed = false, m_dragging = false;
};

inline void enableNumberDrag(QAbstractSpinBox *spin) {
    if (spin->property("nativeNumberEditing").toBool()) return;
    if (spin->property("numberDragInstalled").toBool()) return;
    if (auto edit = spin->findChild<QLineEdit *>()) {
        new NumberDrag(spin, edit);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setProperty("numberDragInstalled", true);
        spin->setToolTip("Drag sideways to adjust · Shift: fine · Ctrl/Command: coarse · Click to type");
    }
}
} // namespace Flame
