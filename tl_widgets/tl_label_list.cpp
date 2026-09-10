#include "tl_label_list.h"
#include "tl_shape_list.h"

#include <format>
#include <QLabel>
#include <QMouseEvent>


void EscapableListWidget::keyPressEvent(QKeyEvent *keyEvent) {
    QListWidget::keyPressEvent(keyEvent);
    if (keyEvent->key() == Qt::Key::Key_Escape)
        this->clearSelection();
}

UniqueLabelList::UniqueLabelList(QWidget *parent)
    : EscapableListWidget(parent) {
    this->setItemDelegate(new TrailingColorDotDelegate(this));
}

void UniqueLabelList::mousePressEvent(QMouseEvent *mouseEvent) {
    EscapableListWidget::mousePressEvent(mouseEvent);
    if (!this->indexAt(mouseEvent->position().toPoint()).isValid())
        this->clearSelection();
}

QListWidgetItem *UniqueLabelList::find_label_item(const QString &label) {
    for (auto row = 0; row < this->count(); ++row) {
        auto *item = this->item(row);
        if (item && item->data(Qt::ItemDataRole::UserRole) == label)
            return item;
    }
    return nullptr;
}

void UniqueLabelList::add_label_item(const QString &label, const std::tuple<int, int, int> &color) {
    if (this->find_label_item(label))
        throw std::logic_error(std::format("Item for label '{}' already exists", label.toStdString()));

    auto *item = new QListWidgetItem();
    item->setData(Qt::ItemDataRole::UserRole, label);  // for find_label_item
    item->setData(LABEL_COLOR_ROLE, QColor(std::get<0>(color), std::get<1>(color), std::get<2>(color)));
    item->setText(label);
    this->addItem(item);
}