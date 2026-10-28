#pragma once
#include <QtWidgets>
class ChannelDelegate : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &o, const QModelIndex &i) const override {
        p->save();
        QRect r = o.rect.adjusted(3, 3, -3, -3);
        bool selected = o.state & QStyle::State_Selected;
        double value = i.data(Qt::UserRole + 1).toDouble();
        bool manual = i.data(Qt::UserRole + 2).toBool();
        p->setRenderHint(QPainter::Antialiasing);
        p->setBrush(QColor(selected ? "#283d4c" : "#1c252f"));
        p->setPen(QPen(QColor(selected ? "#efbc72" : "#34414d"), selected ? 2 : 1));
        p->drawRoundedRect(r, 5, 5);
        QFont font = o.font;
        font.setPointSize(12);
        font.setBold(true);
        p->setFont(font);
        p->setPen(QColor("#e8edf2"));
        p->drawText(r.adjusted(9, 4, -7, -26), Qt::AlignLeft | Qt::AlignVCenter,
                    i.data(Qt::UserRole).toString());
        font.setPointSize(10);
        p->setFont(font);
        p->setPen(QColor(value > 0 ? "#efbc72" : "#748494"));
        p->drawText(r.adjusted(4, 4, -8, -26), Qt::AlignRight | Qt::AlignVCenter,
                    QString::number(value, 'f', 0));
        font.setPointSize(8);
        font.setBold(false);
        p->setFont(font);
        p->setPen(QColor(manual ? "#f0b780" : "#8b9eac"));
        p->drawText(r.adjusted(8, 24, -6, -6), Qt::AlignLeft | Qt::AlignVCenter,
                    manual ? "MANUAL" : i.data(Qt::UserRole + 3).toString());
        QRectF track(r.left() + 8, r.bottom() - 6, r.width() - 16, 2);
        p->fillRect(track, QColor("#34414d"));
        track.setWidth(track.width() * value / 100.);
        p->fillRect(track, QColor("#efbc72"));
        p->restore();
    }
};
