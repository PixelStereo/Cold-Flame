#pragma once
#include <QtWidgets>
// Dense table editors need their own padding, independent of command-line controls.
class TableDelegate : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override {
        auto editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (editor)
            editor->setStyleSheet("QLineEdit,QSpinBox,QDoubleSpinBox{padding:0px 3px;"
                                 "margin:0px;min-height:0px;border-radius:0px;}");
        return editor;
    }
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                              const QModelIndex &) const override {
        editor->setGeometry(option.rect.adjusted(1, 1, -1, -1));
    }
};
