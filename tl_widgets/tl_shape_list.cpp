#include "tl_shape_list.h"

#include "spdlog/spdlog.h"
#include "common/format_qt.h"

#include <QApplication>
#include <QAbstractTextDocumentLayout>


QString format_label_with_color_dot(const QString &text, const std::vector<int32_t> &color) {
    const int32_t r = color[0], g = color[1], b = color[2];
    return QString("%1 <font color=\"#%2%3%4\">●</font>").arg(text.toHtmlEscaped())
               .arg(r, 2, 16, '0').arg(g, 2, 16, '0').arg(b, 2, 16, '0');
}

QString format_shape_label(const TlShape &shape, const std::vector<int32_t> &fill_rgb) {
    //assert shape.label is not None
    QString text = shape.label_;
    if (shape.group_id_ != None)
        text += QString(" (%2)").arg(shape.group_id_);
    //enabled_flags = [key for key, value in (shape.flags or {}).items() if value];
    //if enabled_flags:
    //    text += f" [{', '.join(enabled_flags)}]";
    return format_label_with_color_dot(text, fill_rgb);
}

HTMLDelegate::HTMLDelegate(QObject *parent) : QStyledItemDelegate(parent) {
    doc_ = new QTextDocument(this);
}

void HTMLDelegate::paint(
    QPainter *painter,
    const QStyleOptionViewItem &option,
    const QModelIndex &index
) const {
    painter->save();

    auto *options = new QStyleOptionViewItem(option);

    initStyleOption(options, index);
    doc_->setHtml(options->text);
    options->text = "";

    const auto *style = (
        options->widget == nullptr ? QApplication::style() : options->widget->style()
    );
    style->drawControl(QStyle::CE_ItemViewItem, options, painter);

    auto ctx = QAbstractTextDocumentLayout::PaintContext();

    if (option.state & QStyle::State_Selected) {
        ctx.palette.setColor(
            QPalette::Text,
            option.palette.color(QPalette::Active, QPalette::HighlightedText)
        );
    } else {
        ctx.palette.setColor(
            QPalette::Text,
            option.palette.color(QPalette::Active, QPalette::Text)
        );
    }

    auto textRect = style->subElementRect(QStyle::SE_ItemViewItemText, options);

    if (index.column() != 0) {
        textRect.adjust(5, 0, 0, 0);
    }

    const int32_t margin_shift = 4;
    int32_t margin = (option.rect.height() - options->fontMetrics.height()) / 2;
    margin = margin - margin_shift;
    textRect.setTop(textRect.top() + margin);

    painter->translate(textRect.topLeft());
    painter->setClipRect(textRect.translated(-textRect.topLeft()));
    doc_->documentLayout()->draw(painter, ctx);

    painter->restore();
}

QSize HTMLDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
    const int32_t margin_shift = 4;
    return QSize(
        static_cast<int32_t>(doc_->idealWidth()),
        static_cast<int32_t>(doc_->size().height() - margin_shift)
    );
}

ShapeListItem::ShapeListItem(const QString &text, const TlShape &shape) : QStandardItem() {
    setShape(shape);
    InitItem(text);
}

void ShapeListItem::InitItem(const QString &text) {
    this->setText(text);
    this->setCheckable(true);
    this->setCheckState(Qt::Checked);
    this->setEditable(false);
    this->setTextAlignment(Qt::AlignBottom);
}

ShapeListItem *ShapeListItem::clone() const {
    return new ShapeListItem(text(), this->shape());
}

void ShapeListItem::setShape(const TlShape &shape) {
    this->setData(QVariant(), Qt::UserRole);    // clear first: check equal in setData.
    this->setData(QVariant::fromValue(shape), Qt::UserRole);
}

TlShape ShapeListItem::shape() const {
    return this->data(Qt::UserRole).value<TlShape>();
}

//def __hash__(self):
//    return id(self)
//
//def __repr__(self):
//    return '{}("{}")'.format(self.__class__.__name__, self.text())


bool ShapeItemModel::removeRows(int row, int count, const QModelIndex &parent) {
    const auto ret = QStandardItemModel::removeRows(row, count, parent);
    emit itemDropped();
    return ret;
}

bool ShapeItemModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) {
    // NOTE: By default, PyQt will overwrite items when dropped on them, so we need
    // to adjust the row/parent to insert after the item instead.
    //
    // If row is -1, we're dropping on an item (which would overwrite)
    // Instead, we want to insert after it
    QModelIndex parent_ = parent;
    if (row == -1 && parent.isValid()) {
        row = parent.row() + 1;
        parent_ = parent.parent();
    }

    // If still -1, append to end
    if (row == -1) {
        row = this->rowCount(parent_);
    }

    return QStandardItemModel::dropMimeData(data, action, row, column, parent_);
}

ShapeListView::ShapeListView(QWidget *parent) : QListView(parent) {
    this->selectedItems_.clear();

    this->setWindowFlags(Qt::Window);

    this->model_ = new ShapeItemModel();
    this->model_->setItemPrototype(new ShapeListItem(""));
    this->setModel(this->model_);

    this->setItemDelegate(new HTMLDelegate(this));
    this->setSelectionMode(QAbstractItemView::ExtendedSelection);  // 选中模式
    this->setDragDropMode(QAbstractItemView::InternalMove);
    this->setDefaultDropAction(Qt::MoveAction);

    QObject::connect(this, &ShapeListView::doubleClicked, this, &ShapeListView::itemDoubleClickedEvent);
    QObject::connect(this->selectionModel(), &QItemSelectionModel::selectionChanged, this, &ShapeListView::itemSelectionChangedEvent);
    QObject::connect(this->model_, &ShapeItemModel::itemDropped, this, &ShapeListView::itemDroppedEvent);
    QObject::connect(this->model_, &ShapeItemModel::itemChanged, this, &ShapeListView::itemChangedEvent);
}

int32_t ShapeListView::len() const {
    return this->model_->rowCount();
}

QList<ShapeListItem *> ShapeListView::items() const {
    QList<ShapeListItem *> items;
    for (auto row = 0; row < model_->rowCount(); ++row) {
        items.push_back(static_cast<ShapeListItem *>(model_->item(row)));
    }
    return items;
}

//void TlShapeList::__getitem__(i) {
//}

//void TlShapeList::__iter__() {
//}

void ShapeListView::itemDroppedEvent() {
    emit item_dropped();
}

void ShapeListView::itemChangedEvent(QStandardItem *item) {
    emit item_changed(static_cast<ShapeListItem *>(item));
}

void ShapeListView::itemSelectionChangedEvent(const QItemSelection &selected, const QItemSelection &deselect) {
    QList<ShapeListItem *> selected1, deselect1;
    const QList<QModelIndex> sel_indexes = selected.indexes(), des_indexes = deselect.indexes();
    std::ranges::transform(sel_indexes, std::back_inserter(selected1), [this](const auto &idx) { return static_cast<ShapeListItem *>(this->model_->itemFromIndex(idx)); });
    std::ranges::transform(des_indexes, std::back_inserter(deselect1), [this](const auto &idx) { return static_cast<ShapeListItem *>(this->model_->itemFromIndex(idx)); });
    emit item_selection_changed(selected1, deselect1);
}

void ShapeListView::itemDoubleClickedEvent(const QModelIndex &index) {
    emit item_double_clicked(static_cast<ShapeListItem *>(this->model_->itemFromIndex(index)));
}

QList<ShapeListItem *> ShapeListView::selected_items() {
    //return [self.model().itemFromIndex(i) for i in self.selectedIndexes()]
    QList<ShapeListItem *> selected;
    const QList<QModelIndex> indexes = selectedIndexes();
    std::ranges::transform(indexes, std::back_inserter(selected), [this](const auto &idx) { return static_cast<ShapeListItem *>(this->model_->itemFromIndex(idx)); });
    return selected;
}

void ShapeListView::scroll_to_item(ShapeListItem *item) {
    this->scrollTo(this->model_->indexFromItem(item));
}

void ShapeListView::add_item(ShapeListItem *item) {
    if (item == nullptr) {
        throw std::invalid_argument("item must be LabelListWidgetItem");
    }
    this->model_->setItem(this->model_->rowCount(), 0, item);
    item->setSizeHint(this->itemDelegate()->sizeHint(QStyleOptionViewItem(), QModelIndex()));
}

void ShapeListView::removeItem(ShapeListItem *item) {
    const auto index = this->model_->indexFromItem(item);
    this->model_->removeRows(index.row(), 1, QModelIndex());
}

void ShapeListView::select_item(ShapeListItem *item) {
    const auto index = this->model_->indexFromItem(item);
    selectionModel()->select(index, QItemSelectionModel::Select);
}

ShapeListItem *ShapeListView::find_item_by_shape(const TlShape &shape) {
    for (auto row = 0; row < this->model_->rowCount(); ++row) {
        auto *const item = dynamic_cast<ShapeListItem *>(this->model_->item(row, 0));
        if (item->shape() == shape) {
            return item;
        }
    }
    throw std::runtime_error("cannot find shape: {}"); //.format(shape));
}

void ShapeListView::clear() {
    this->model_->clear();
    this->selectedItems_.clear();
}

bool ShapeListView::empty() const {
    return this->model_->rowCount() == 0;
}