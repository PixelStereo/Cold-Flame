#pragma once
#include <QtWidgets>
#include <functional>

// The gutter starts drags; cue cells retain their normal editing behavior.
class StepGripHeader : public QHeaderView {
    QPoint press;
    int pressedRow = -1;
  public:
    std::function<void(int)> selectRow, dragRow;
    explicit StepGripHeader(QWidget *parent = nullptr) : QHeaderView(Qt::Vertical, parent) {
        setFixedWidth(20);
        setCursor(Qt::OpenHandCursor);
        setStatusTip("Drag the dotted handle to reorder this step. Step IDs and links stay unchanged.");
        viewport()->setStatusTip(statusTip());
    }
  protected:
    void paintSection(QPainter *p, const QRect &rect, int) const override {
        p->save(); p->fillRect(rect, QColor("#1d1f20"));
        p->setPen(Qt::NoPen); p->setBrush(QColor("#909995"));
        for (int x : {-3, 3}) for (int y : {-5, 0, 5})
            p->drawEllipse(QPointF(rect.center() + QPoint(x, y)), 1.2, 1.2);
        p->restore();
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() != Qt::LeftButton) return;
        press = event->position().toPoint(); pressedRow = logicalIndexAt(press.y());
        if (pressedRow >= 0 && selectRow) selectRow(pressedRow);
        setCursor(Qt::ClosedHandCursor); event->accept();
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (pressedRow < 0 || !(event->buttons() & Qt::LeftButton)) return;
        if ((event->position().toPoint() - press).manhattanLength() < QApplication::startDragDistance()) return;
        const int row = pressedRow; pressedRow = -1;
        if (dragRow) dragRow(row);
        setCursor(Qt::OpenHandCursor);
    }
    void mouseReleaseEvent(QMouseEvent *) override { pressedRow = -1; setCursor(Qt::OpenHandCursor); }
};

class StepTable : public QTableWidget {
    QString context, token = QUuid::createUuid().toString();
    int insertion = -1;
    QPoint dragPoint;
    QTimer scrollTimer;
    QWidget *gripViewport = nullptr;
    static constexpr const char *Mime = "application/x-cold-flame-step";
    QString draggedStep(const QMimeData *mime) const {
        if (!mime->hasFormat(Mime)) return {};
        QDataStream stream(mime->data(Mime));
        QString source, id; stream >> source >> id;
        return stream.status() == QDataStream::Ok && source == token ? id : QString();
    }
    int insertionAt(QPoint point) const {
        int row = rowAt(point.y());
        if (row < 0) return point.y() < 0 ? 0 : rowCount();
        return row + (point.y() >= rowViewportPosition(row) + rowHeight(row) / 2 ? 1 : 0);
    }
    void clearDrop() { insertion = -1; scrollTimer.stop(); viewport()->update(); }
  public:
    std::function<void(int, int)> reorder;
    explicit StepTable(QWidget *parent = nullptr) : QTableWidget(0, 15, parent) {
        auto grip = new StepGripHeader(this); setVerticalHeader(grip);
        gripViewport = grip->viewport(); gripViewport->setAcceptDrops(true);
        gripViewport->installEventFilter(this);
        grip->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(grip, &QHeaderView::customContextMenuRequested, this, [this, grip](QPoint point) {
            int row = grip->logicalIndexAt(point.y());
            if (row >= 0) emit customContextMenuRequested(QPoint(2, rowViewportPosition(row) + rowHeight(row) / 2));
        });
        grip->selectRow = [this](int row) { selectRow(row); };
        grip->dragRow = [this](int row) {
            if (!item(row, 0)) return;
            auto drag = new QDrag(this); drag->setMimeData(stepMimeData(row));
            QPixmap preview(170, 30); preview.fill(QColor("#303a36"));
            QPainter painter(&preview); painter.setPen(QColor("#f3fff9"));
            painter.drawText(preview.rect().adjusted(8, 0, -4, 0), Qt::AlignVCenter,
                "Step " + item(row, 0)->text() + " · Cue " + item(row, 1)->text());
            painter.end(); drag->setPixmap(preview); drag->exec(Qt::MoveAction);
            drag->deleteLater(); clearDrop();
        };
        setAcceptDrops(true); setDragDropMode(QAbstractItemView::DropOnly);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        setCornerButtonEnabled(false);
        scrollTimer.setInterval(50);
        connect(&scrollTimer, &QTimer::timeout, this, [this] {
            int delta = dragPoint.y() < 22 ? -14 : dragPoint.y() > viewport()->height() - 22 ? 14 : 0;
            if (!delta) return;
            verticalScrollBar()->setValue(verticalScrollBar()->value() + delta);
            insertion = insertionAt(dragPoint); viewport()->update();
        });
    }
    void setSequenceContext(QString sequence) {
        if (context == sequence) return;
        context = sequence; token = QUuid::createUuid().toString(); clearDrop();
    }
    QMimeData *stepMimeData(int row) const {
        auto mime = new QMimeData; QByteArray data; QDataStream stream(&data, QIODevice::WriteOnly);
        stream << token << (item(row, 0) ? item(row, 0)->text() : QString()); mime->setData(Mime, data);
        return mime;
    }
  protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object == gripViewport) {
            // A straight vertical drag may remain over the grips. Treat it as a sheet drop.
            if (event->type() == QEvent::DragLeave) {
                clearDrop(); event->accept(); return true;
            }
            if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove || event->type() == QEvent::Drop) {
                auto original = static_cast<QDropEvent *>(event);
                QPoint point = viewport()->mapFromGlobal(gripViewport->mapToGlobal(original->position().toPoint())); point.setX(4);
                auto acceptResult = [&](QDropEvent &mapped) {
                    original->setDropAction(mapped.dropAction()); original->setAccepted(mapped.isAccepted());
                };
                if (event->type() == QEvent::DragEnter) {
                    QDragEnterEvent mapped(point, original->possibleActions(), original->mimeData(), original->buttons(), original->modifiers());
                    dragEnterEvent(&mapped); acceptResult(mapped);
                } else if (event->type() == QEvent::DragMove) {
                    QDragMoveEvent mapped(point, original->possibleActions(), original->mimeData(), original->buttons(), original->modifiers());
                    dragMoveEvent(&mapped); acceptResult(mapped);
                } else {
                    QDropEvent mapped(point, original->possibleActions(), original->mimeData(), original->buttons(), original->modifiers());
                    dropEvent(&mapped); acceptResult(mapped);
                }
                return true;
            }
        }
        return QTableWidget::eventFilter(object, event);
    }
    void dragEnterEvent(QDragEnterEvent *event) override {
        if (draggedStep(event->mimeData()).isEmpty()) { event->ignore(); return; }
        event->setDropAction(Qt::MoveAction); event->accept(); scrollTimer.start();
    }
    void dragMoveEvent(QDragMoveEvent *event) override {
        if (draggedStep(event->mimeData()).isEmpty()) { clearDrop(); event->ignore(); return; }
        dragPoint = event->position().toPoint(); insertion = insertionAt(dragPoint);
        event->setDropAction(Qt::MoveAction); event->accept(); viewport()->update();
    }
    void dragLeaveEvent(QDragLeaveEvent *event) override { clearDrop(); event->accept(); }
    void dropEvent(QDropEvent *event) override {
        const QString id = draggedStep(event->mimeData());
        if (id.isEmpty()) { clearDrop(); event->ignore(); return; }
        int source = -1;
        for (int row = 0; row < rowCount(); ++row)
            if (item(row, 0) && item(row, 0)->text() == id) source = row;
        int destination = insertionAt(event->position().toPoint());
        if (destination > source) --destination;
        clearDrop();
        if (source < 0 || destination < 0 || destination >= rowCount()) { event->ignore(); return; }
        if (source != destination && reorder) reorder(source, destination);
        event->setDropAction(Qt::MoveAction); event->accept();
    }
    void paintEvent(QPaintEvent *event) override {
        QTableWidget::paintEvent(event);
        if (insertion < 0) return;
        int y = insertion < rowCount() ? rowViewportPosition(insertion)
            : rowCount() ? rowViewportPosition(rowCount() - 1) + rowHeight(rowCount() - 1) : 0;
        QPainter painter(viewport()); painter.setPen(QPen(QColor("#b7cfc1"), 2));
        painter.drawLine(0, qBound(1, y, viewport()->height() - 1), viewport()->width(), qBound(1, y, viewport()->height() - 1));
    }
};
