#pragma once
#include <QtWidgets>

// Reflow existing cards when the viewport changes. Resizing must not recreate
// playback buttons or interrupt a held/timed submaster.
class CardBank : public QWidget {
    QScrollArea *scroll;
    QGridLayout *grid;
    QList<QWidget *> cards;
    int columns = 0;
    void reflow(bool force = false) {
        const auto margins = grid->contentsMargins();
        const int available = scroll->viewport()->width() - margins.left() - margins.right();
        int minimumCardWidth = 280;
        for (auto card : cards) minimumCardWidth = qMax(minimumCardWidth, qMax(card->minimumWidth(), card->minimumSizeHint().width()));
        const int next = qMax(1, qMin(int(cards.size()), (available + grid->horizontalSpacing()) /
            (minimumCardWidth + grid->horizontalSpacing())));
        if (next == columns && !force) return;
        for (int col = 0; col < columns; ++col) grid->setColumnStretch(col, 0);
        while (auto item = grid->takeAt(0)) delete item;
        columns = next;
        for (int col = 0; col < columns; ++col) grid->setColumnStretch(col, 1);
        for (int i = 0; i < cards.size(); ++i) grid->addWidget(cards[i], i / columns, i % columns);
        grid->invalidate(); updateGeometry();
    }
  public:
    explicit CardBank(QScrollArea *area) : QWidget(area->viewport()), scroll(area), grid(new QGridLayout(this)) {
        setObjectName("submasterCards");
        grid->setContentsMargins(10, 8, 10, 8); grid->setSpacing(12);
        grid->setAlignment(Qt::AlignTop);
        scroll->viewport()->installEventFilter(this);
    }
    void addCard(QWidget *card) {
        cards << card;
        reflow(true);
    }
  protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object == scroll->viewport() && event->type() == QEvent::Resize) reflow();
        return QWidget::eventFilter(object, event);
    }
};
