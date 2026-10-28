#pragma once
#include <QtWidgets>
class ChannelDelegate : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override {
        if (auto view = qobject_cast<const QListView *>(parent()))
            if (view->gridSize().isValid())
                return view->gridSize();
        return QSize(76, 62);
    }
    void paint(QPainter *p, const QStyleOptionViewItem &o, const QModelIndex &i) const override {
        p->save();
        QRect r = o.rect.adjusted(3, 3, -3, -3);
        bool selected = o.state & QStyle::State_Selected;
        double value = i.data(Qt::UserRole + 1).toDouble();
        bool manual = i.data(Qt::UserRole + 2).toBool();
        p->setRenderHint(QPainter::Antialiasing);
        p->setBrush(QColor(selected ? "#303a36" : "#1d1f20"));
        p->setPen(QPen(QColor(selected ? "#b7cfc1" : "#383d3a"), selected ? 2 : 1));
        p->drawRoundedRect(r, 5, 5);
        QFont font = o.font;
        font.setPixelSize(13);
        font.setBold(true);
        p->setFont(font);
        p->setPen(QColor("#f0f4f1"));
        p->drawText(QRect(r.left() + 5, r.top() + 2, r.width() - 10, 15),
                    Qt::AlignLeft | Qt::AlignVCenter, i.data(Qt::UserRole).toString());
        font.setPixelSize(11);
        p->setFont(font);
        p->setPen(QColor(value > 0 ? "#b7cfc1" : "#909995"));
        p->drawText(QRect(r.left() + 4, r.top() + 15, r.width() - 9, 12),
                    Qt::AlignRight | Qt::AlignVCenter, QString::number(value, 'f', 0));
        font.setPixelSize(9);
        font.setBold(false);
        p->setFont(font);
        p->setPen(QColor(manual ? "#e0c998" : "#a3b0a8"));
        const QString flag = i.data(Qt::UserRole + 4).toString();
        if (!flag.isEmpty())
            p->drawText(QRect(r.left() + 4, r.top() + 15, r.width() - 8, 12), Qt::AlignLeft, flag == "PARK" ? "P" : "C");
        else if (manual) {
            p->setBrush(QColor("#e0c998")); p->setPen(Qt::NoPen);
            p->drawEllipse(QPointF(r.right() - 7, r.top() + 10), 2, 2);
        }
        QRectF track(r.left() + 8, r.bottom() - 3, r.width() - 16, 2);
        p->fillRect(track, QColor("#383d3a"));
        track.setWidth(track.width() * value / 100.);
        p->fillRect(track, QColor("#b7cfc1"));
        p->restore();
    }
};
