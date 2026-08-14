#include "tl_label_list.h"
#include "tl_shape_list.h"

#include <format>
#include <QLabel>
#include <QMouseEvent>


void EscapableQListWidget::keyPressEvent(QKeyEvent *keyEvent) {
    QListWidget::keyPressEvent(keyEvent);
    if (keyEvent->key() == Qt::Key::Key_Escape)
        this->clearSelection();
}

LabelList::LabelList() {
    this->setItemDelegate(new HTMLDelegate(this));
}

void LabelList::mousePressEvent(QMouseEvent *mouseEvent) {
    QListWidget::mousePressEvent(mouseEvent);
    if (!this->indexAt(mouseEvent->position().toPoint()).isValid())
        this->clearSelection();
}

QListWidgetItem *LabelList::find_label_item(const QString &label) {
    for (auto row = 0; row < this->count(); ++row) {
        auto *item = this->item(row);
        if (item->data(Qt::ItemDataRole::UserRole) == label)
            return item;
    }
    return nullptr;
}

void LabelList::add_label_item(const QString &label, const std::vector<int32_t> &color) {
    if (this->find_label_item(label)) {
        std::cerr << "[LabelList::add_label_item] " << label.toStdString() << " already exists" << std::endl;
        throw std::logic_error(std::format("Item for label '{}' already exists", label.toStdString()));
    }

    auto *item = new QListWidgetItem();
    item->setData(Qt::ItemDataRole::UserRole, label);  // for find_label_item
    item->setText(format_label_with_color_dot(label, color));
    this->addItem(item);
}