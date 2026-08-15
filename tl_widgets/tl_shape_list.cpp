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
        text += QString(" (%1)").arg(shape.group_id_);
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
    auto *opt = new QStyleOptionViewItem(option);
    this->initStyleOption(opt, index);

    auto html = opt->text;
    opt->text = "";

    const auto *widget_style = (
        opt->widget ? opt->widget->style() : QApplication::style()
    );
    widget_style->drawControl(QStyle::ControlElement::CE_ItemViewItem, opt, painter);

    QColor text_color;
    QTextDocument doc;
    if (opt->state & QStyle::StateFlag::State_Selected) {
        text_color = opt->palette.color(
            QPalette::ColorGroup::Active, QPalette::ColorRole::HighlightedText
        );
    } else {
        text_color = opt->palette.color(
            QPalette::ColorGroup::Active, QPalette::ColorRole::Text
        );
    }
    doc.setDefaultStyleSheet(QString("body { color: %1; }").arg(text_color.name()));
    doc.setHtml(QString("<body>%1</body>").arg(html));

    auto text_rect = widget_style->subElementRect(
        QStyle::SubElement::SE_ItemViewItemText, opt
    );
    if (index.column() != 0) {
        text_rect.adjust(5, 0, 0, 0);
    }
    // opt.text was emptied above, so some styles (e.g. Adwaita) return a
    // text sub-rect too narrow for the rendered HTML and clip the label.
    // Widen it to the document's ideal width so the text stays visible.
    text_rect.setWidth(std::max(text_rect.width(), (int32_t)std::ceil(doc_->idealWidth())));

    constexpr int32_t VERT_FUDGE = 4;
    int32_t margin = (option.rect.height() - opt->fontMetrics.height()) / 2 - VERT_FUDGE;
    text_rect.setTop(text_rect.top() + margin);

    painter->save();
    painter->translate(text_rect.topLeft());
    painter->setClipRect(text_rect.translated(-text_rect.topLeft()));
    doc.drawContents(painter);
    painter->restore();
}

QSize HTMLDelegate::sizeHint(
    const QStyleOptionViewItem &option,
    const QModelIndex &index
) const {
    constexpr int32_t VERT_FUDGE = 4;
    auto *opt = new QStyleOptionViewItem(option);
    this->initStyleOption(opt, index);
    QTextDocument doc;
    doc.setHtml(opt->text);
    const auto height = int32_t(doc.size().height()) - VERT_FUDGE;
    return {int32_t(doc.idealWidth()), height};
}

QSize HTMLDelegate::default_size_hint() {
    constexpr int32_t VERT_FUDGE = 4;
    QTextDocument doc;
    const auto height = int32_t(doc.size().height()) - VERT_FUDGE;
    return {int32_t(doc.idealWidth()), height};
}

ShapeListItem::ShapeListItem(const QString &text, const TlShape &shape) : QStandardItem() {
    this->setText(text);
    this->set_shape(shape);

    this->setCheckable(true);
    this->setCheckState(
        !shape || shape.visible_
        ? Qt::CheckState::Checked
        : Qt::CheckState::Unchecked
    );
    this->setEditable(false);
    this->setTextAlignment(Qt::AlignmentFlag::AlignBottom);
}

ShapeListItem *ShapeListItem::clone() const {
    return new ShapeListItem(this->text(), this->shape());
}

void ShapeListItem::set_shape(const TlShape &shape) {
    this->setData(QVariant(), Qt::UserRole);    // clear first: check equal in setData.
    this->setData(QVariant::fromValue(shape), Qt::ItemDataRole::UserRole);
}

TlShape ShapeListItem::shape() const {
    return this->data(Qt::ItemDataRole::UserRole).value<TlShape>();
}

//def __hash__(self):
//    return id(self)
//
//def __repr__(self):
//    return '{}("{}")'.format(self.__class__.__name__, self.text())


bool ShapeItemModel::removeRows(
    const int row,
    const int count,
    const QModelIndex &parent
) {
    const auto ret = QStandardItemModel::removeRows(row, count, parent);
    emit this->item_dropped();
    return ret;
}

bool ShapeItemModel::dropMimeData(
    const QMimeData *data,
    const Qt::DropAction action,
    int row,
    int column,
    const QModelIndex &parent
) {
    // NOTE: By default, PyQt will overwrite items when dropped on them, so we need
    // to adjust the row/parent to insert after the item instead.
    QModelIndex _parent = parent;
    // If row is -1, we're dropping on an item (which would overwrite)
    // Instead, we want to insert after it
    if (row == -1 && parent.isValid()) {
        row = parent.row() + 1;
        _parent = parent.parent();
    }

    // If still -1, append to end
    if (row == -1)
        row = this->rowCount(_parent);

    return QStandardItemModel::dropMimeData(data, action, row, column, _parent);
}

ShapeListView::ShapeListView(QWidget *parent) : QListView(parent) {
    this->setWindowFlags(Qt::WindowType::Window);

    this->model_ = new ShapeItemModel();
    this->model_->setItemPrototype(new ShapeListItem(""));
    this->setModel(this->model_);

    this->setItemDelegate(new HTMLDelegate(this));
    this->setSelectionMode(
        QAbstractItemView::SelectionMode::ExtendedSelection     // 选中模式
    );
    this->setDragDropMode(QAbstractItemView::DragDropMode::InternalMove);
    this->setDefaultDropAction(Qt::DropAction::MoveAction);

    QObject::connect(this, &ShapeListView::doubleClicked, this, &ShapeListView::on_item_double_clicked);
    QObject::connect(this->selectionModel(), &QItemSelectionModel::selectionChanged, this, &ShapeListView::on_item_selection_changed);
    QObject::connect(this->model_, &ShapeItemModel::item_dropped, this, &ShapeListView::on_item_dropped);
    QObject::connect(this->model_, &ShapeItemModel::itemChanged, this, &ShapeListView::on_item_changed);

    this->press_snapshot_ = {};
}

void ShapeListView::mousePressEvent(QMouseEvent *e) {
    this->press_snapshot_ = this->selected_items() | std::views::transform([this](auto &item) {
        return ItemSnapshot{
            .index=QPersistentModelIndex(this->model_->indexFromItem(item)),
            .check_state=item->checkState()
        };
    }) | std::ranges::to<QList<ItemSnapshot>>();

    QListView::mousePressEvent(e);
}

void ShapeListView::mouseReleaseEvent(QMouseEvent *e) {
    QListView::mouseReleaseEvent(e);

    // Restore the multi-selection only when a checkbox toggle collapsed it.
    // A plain row click should narrow the selection to one row.
    bool check_state_changed = false;
    QList<ShapeListItem *> items_at_press = {};
    for (const auto &snap : this->press_snapshot_) {
        auto *item = this->resolve_item(snap.index);
        if (item == nullptr)
            continue;
        items_at_press.append(item);
        check_state_changed |= (item->checkState() != snap.check_state);
    }
    if (
        check_state_changed
        && items_at_press.size() > 1
        && std::set(this->selected_items().begin(), this->selected_items().end()) != std::set(items_at_press.begin(), items_at_press.end())
    ) {
        this->selectionModel()->clearSelection();
        for (const auto &item : items_at_press)
            this->selectionModel()->select(
                this->model_->indexFromItem(item),
                QItemSelectionModel::SelectionFlag::Select
            );
    }

    this->press_snapshot_ = {};
}

QList<ShapeListItem *> ShapeListView::selection_at_press() {
    return this->press_snapshot_
        | std::views::transform([this](const auto &snap) { return this->resolve_item(snap.index); })
        | std::views::filter([](const auto &item) { return item != nullptr; })
        | std::ranges::to<QList<ShapeListItem *>>();
}

ShapeListItem *ShapeListView::resolve_item(
    const QPersistentModelIndex &index
) {
    if (!index.isValid())
        return nullptr;
    return dynamic_cast<ShapeListItem *>(this->model_->itemFromIndex(index));
}

int32_t ShapeListView::len() const {
    return this->model_->rowCount();
}

QList<ShapeListItem *> ShapeListView::items() const {
    return std::views::iota(0, this->model_->rowCount())
        | std::views::transform([this](const auto &i) { return dynamic_cast<ShapeListItem *>(this->model_->item(i)); })
        | std::ranges::to<QList<ShapeListItem *>>();
}

//def __getitem__(self, i: int) -> LabelListWidgetItem:
//    return cast(LabelListWidgetItem, self._model.item(i))
//
//def __iter__(self) -> Iterator[LabelListWidgetItem]:
//    for i in range(len(self)):
//        yield self[i]

void ShapeListView::on_item_dropped() {
    emit this->item_dropped();
}

void ShapeListView::on_item_changed(QStandardItem *item) {
    emit this->item_changed(dynamic_cast<ShapeListItem *>(item));
}

void ShapeListView::on_item_selection_changed(
    const QItemSelection &selected,
    const QItemSelection &deselected
) {
    QList<ShapeListItem *> selected_items = selected.indexes() | std::views::transform([this](const auto &i){ return static_cast<ShapeListItem *>(this->model_->itemFromIndex(i)); }) | std::ranges::to<QList<ShapeListItem *>>();
    QList<ShapeListItem *> deselected_items = deselected.indexes() | std::views::transform([this](const auto &i){ return static_cast<ShapeListItem *>(this->model_->itemFromIndex(i)); }) | std::ranges::to<QList<ShapeListItem *>>();
    emit this->item_selection_changed(selected_items, deselected_items);
}

void ShapeListView::on_item_double_clicked(const QModelIndex &index) {
    emit this->item_double_clicked(dynamic_cast<ShapeListItem *>(this->model_->itemFromIndex(index)));
}

QList<ShapeListItem *> ShapeListView::selected_items() {
    //return [self.model().itemFromIndex(i) for i in self.selectedIndexes()]
    return this->selectedIndexes()
        | std::views::transform([this](const auto &idx) { return static_cast<ShapeListItem *>(this->model_->itemFromIndex(idx)); })
        | std::ranges::to<QList<ShapeListItem *>>();
}

void ShapeListView::scroll_to_item(ShapeListItem *item) {
    this->scrollTo(this->model_->indexFromItem(item));
}

void ShapeListView::add_item(ShapeListItem *item) {
    if (item == nullptr)
        throw std::invalid_argument("item must be LabelListWidgetItem");
    this->model_->setItem(this->model_->rowCount(), 0, item);
    auto *delegate = dynamic_cast<HTMLDelegate *>(this->itemDelegate());
    item->setSizeHint(delegate->default_size_hint());
}

void ShapeListView::removeItem(ShapeListItem *item) {
    const auto index = this->model_->indexFromItem(item);
    this->model_->removeRows(index.row(), 1, QModelIndex());
}

void ShapeListView::select_item(ShapeListItem *item) {
    const auto index = this->model_->indexFromItem(item);
    selectionModel()->select(
        index, QItemSelectionModel::SelectionFlag::Select
    );
}

ShapeListItem *ShapeListView::find_item_by_shape(const TlShape &shape) {
    for (auto row = 0; row < this->model_->rowCount(); ++row) {
        auto *s_it = this->model_->item(row, 0);
        auto *item = dynamic_cast<ShapeListItem *>(s_it);
        if (item->shape() == shape)
            return item;
    }
    throw std::runtime_error("cannot find shape: {shape}"); //.format(shape));
}

void ShapeListView::clear() {
    this->model_->clear();
}
