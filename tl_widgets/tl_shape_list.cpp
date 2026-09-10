#include "tl_shape_list.h"

#include "spdlog/spdlog.h"
#include "common/format_qt.h"

#include <QApplication>
#include <QAbstractTextDocumentLayout>


const int32_t LABEL_COLOR_ROLE = Qt::ItemDataRole::UserRole + 1;

QString format_shape_label(const TlShape &shape) {
    //assert shape.label is not None
    QString text = shape.label_;
    if (shape.group_id_ != None)
        text += QString(" (%1)").arg(shape.group_id_);
    //enabled_flags = [key for key, value in (shape.flags or {}).items() if value];
    //if enabled_flags:
    //    text += f" [{', '.join(enabled_flags)}]";
    return text;
}


const char *TrailingColorDotDelegate::DOT_ = " ●";

QSize TrailingColorDotDelegate::sizeHint(
    const QStyleOptionViewItem &option,
    const QModelIndex &index
) const {
    auto size = QStyledItemDelegate::sizeHint(option, index);
    if (index.data(LABEL_COLOR_ROLE).canConvert<QColor>()) {
        size.setWidth(
            size.width() + option.fontMetrics.horizontalAdvance(DOT_)
        );
    }
    return size;
}

void TrailingColorDotDelegate::paint(
    QPainter *painter,
    const QStyleOptionViewItem &option,
    const QModelIndex &index
) const {
    const auto color = index.data(LABEL_COLOR_ROLE);
    if (!color.canConvert<QColor>()) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    auto *opt = new QStyleOptionViewItem(option);
    this->initStyleOption(opt, index);
    const auto *widget_style = (
        opt->widget ? opt->widget->style() : QApplication::style()
    );
    auto text_rect = widget_style->subElementRect(
        QStyle::SubElement::SE_ItemViewItemText, opt
    );
    const auto text_margin = (
        widget_style->pixelMetric(
            QStyle::PixelMetric::PM_FocusFrameHMargin, nullptr, opt->widget
        )
        + 1
    );
    const auto dot_width = opt->fontMetrics.horizontalAdvance(DOT_);
    const auto available_width = std::max(0, text_rect.width() - 2 * text_margin - dot_width);

    // The dot is painted here rather than appended to opt.text, so that Qt
    // never draws the glyph in the text color underneath it: the two draws
    // land a subpixel apart and the one below shows as a fringe.
    opt->text = opt->fontMetrics.elidedText(
        opt->text, opt->textElideMode, available_width
    );
    widget_style->drawControl(
        QStyle::ControlElement::CE_ItemViewItem, opt, painter, opt->widget
    );

    auto dot_rect = QRect(text_rect);
    dot_rect.setLeft(
        text_rect.left() + text_margin + opt->fontMetrics.horizontalAdvance(opt->text)
    );
    dot_rect.setWidth(dot_width);

    painter->save();
    painter->setFont(opt->font);
    painter->setPen(color.value<QColor>());
    painter->drawText(dot_rect, opt->displayAlignment, DOT_);
    painter->restore();
}

LabelListItem::LabelListItem(const QString &text, const TlShape &shape) : QStandardItem() {
    this->setText(text);
    this->set_shape(shape);

    this->setCheckable(true);
    this->setCheckState(
        !shape || shape.visible_
        ? Qt::CheckState::Checked
        : Qt::CheckState::Unchecked
    );
    this->setEditable(false);
}

LabelListItem *LabelListItem::clone() const {
    auto item = new LabelListItem(this->text(), this->shape());
    item->setData(this->data(LABEL_COLOR_ROLE), LABEL_COLOR_ROLE);
    return item;
}

void LabelListItem::set_shape(const TlShape &shape) {
    this->setData(QVariant(), Qt::UserRole);    // clear first: check equal in setData.
    this->setData(QVariant::fromValue(shape), Qt::ItemDataRole::UserRole);
}

void LabelListItem::set_label(const QString &text, const std::tuple<int, int, int> &color) {
    this->setText(text);
    this->setData(QColor(std::get<0>(color), std::get<1>(color), std::get<2>(color)), LABEL_COLOR_ROLE);
}

TlShape LabelListItem::shape() const {
    return this->data(Qt::ItemDataRole::UserRole).value<TlShape>();
}

//def __hash__(self):
//    return id(self)
//
//def __repr__(self):
//    return '{}("{}")'.format(self.__class__.__name__, self.text())


bool ListItemModel::removeRows(
    const int row,
    const int count,
    const QModelIndex &parent
) {
    const auto ret = QStandardItemModel::removeRows(row, count, parent);
    emit this->item_dropped();
    return ret;
}

bool ListItemModel::dropMimeData(
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

LabelListWidget::LabelListWidget(QWidget *parent) : QListView(parent) {
    this->setWindowFlags(Qt::WindowType::Window);

    this->model_ = new ListItemModel();
    this->model_->setItemPrototype(new LabelListItem());
    this->QListView::setModel(this->model_);

    this->setItemDelegate(new TrailingColorDotDelegate(this));
    this->setSelectionMode(
        QAbstractItemView::SelectionMode::ExtendedSelection     // 选中模式
    );
    this->setDragDropMode(QAbstractItemView::DragDropMode::InternalMove);
    this->setDefaultDropAction(Qt::DropAction::MoveAction);

    QObject::connect(this, &LabelListWidget::doubleClicked, this, &LabelListWidget::on_item_double_clicked);
    QObject::connect(this->selectionModel(), &QItemSelectionModel::selectionChanged, this, &LabelListWidget::on_item_selection_changed);
    QObject::connect(this->model_, &ListItemModel::item_dropped, this, &LabelListWidget::on_item_dropped);
    QObject::connect(this->model_, &ListItemModel::itemChanged, this, &LabelListWidget::on_item_changed);

    this->press_snapshot_ = {};
}

void LabelListWidget::mousePressEvent(QMouseEvent *e) {
    this->press_snapshot_ = this->selected_items() | std::views::transform([this](auto &item) {
        return ItemSnapshot{
            .index=QPersistentModelIndex(this->model_->indexFromItem(item)),
            .check_state=item->checkState()
        };
    }) | std::ranges::to<QList<ItemSnapshot>>();

    QListView::mousePressEvent(e);
}

void LabelListWidget::mouseReleaseEvent(QMouseEvent *e) {
    QListView::mouseReleaseEvent(e);

    // Restore the multi-selection only when a checkbox toggle collapsed it.
    // A plain row click should narrow the selection to one row.
    bool check_state_changed = false;
    QList<LabelListItem *> items_at_press = {};
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

QList<LabelListItem *> LabelListWidget::selection_at_press() {
    return this->press_snapshot_
        | std::views::transform([this](const auto &snap) { return this->resolve_item(snap.index); })
        | std::views::filter([](const auto &item) { return item != nullptr; })
        | std::ranges::to<QList<LabelListItem *>>();
}

LabelListItem *LabelListWidget::resolve_item(
    const QPersistentModelIndex &index
) {
    if (!index.isValid())
        return nullptr;
    return dynamic_cast<LabelListItem *>(this->model_->itemFromIndex(index));
}

int32_t LabelListWidget::len() const {
    return this->model_->rowCount();
}

//def __getitem__(self, i: int) -> LabelListWidgetItem:
//    return cast(LabelListWidgetItem, self._model.item(i))
//
//def __iter__(self) -> Iterator[LabelListWidgetItem]:
//    for i in range(len(self)):
//        yield self[i]
//
//@property
//def item_dropped(self) -> QtCore.SignalInstance:
//    return self._model.item_dropped
//
//@property
//def item_changed(self) -> QtCore.SignalInstance:
//    return self._model.itemChanged

QList<LabelListItem *> LabelListWidget::items() const {
    return std::views::iota(0, this->model_->rowCount())
        | std::views::transform([this](const auto &i) { return dynamic_cast<LabelListItem *>(this->model_->item(i)); })
        | std::ranges::to<QList<LabelListItem *>>();
}

void LabelListWidget::on_item_dropped() {
    emit this->item_dropped();
}

void LabelListWidget::on_item_changed(QStandardItem *item) {
    emit this->item_changed(dynamic_cast<LabelListItem *>(item));
}

void LabelListWidget::on_item_selection_changed(
    const QItemSelection &selected,
    const QItemSelection &deselected
) {
    QList<LabelListItem *> selected_items = selected.indexes() | std::views::transform([this](const auto &i){ return static_cast<LabelListItem *>(this->model_->itemFromIndex(i)); }) | std::ranges::to<QList<LabelListItem *>>();
    QList<LabelListItem *> deselected_items = deselected.indexes() | std::views::transform([this](const auto &i){ return static_cast<LabelListItem *>(this->model_->itemFromIndex(i)); }) | std::ranges::to<QList<LabelListItem *>>();
    emit this->item_selection_changed(selected_items, deselected_items);
}

void LabelListWidget::on_item_double_clicked(const QModelIndex &index) {
    emit this->item_double_clicked(dynamic_cast<LabelListItem *>(this->model_->itemFromIndex(index)));
}

QList<LabelListItem *> LabelListWidget::selected_items() {
    return this->selectedIndexes()
        | std::views::transform([this](const auto &idx) { return static_cast<LabelListItem *>(this->model_->itemFromIndex(idx)); })
        | std::ranges::to<QList<LabelListItem *>>();
}

void LabelListWidget::scroll_to_item(LabelListItem *item) {
    this->scrollTo(this->model_->indexFromItem(item));
}

void LabelListWidget::add_item(LabelListItem *item) {
    if (item == nullptr)
        throw std::invalid_argument("item must be LabelListWidgetItem");
    this->model_->setItem(this->model_->rowCount(), 0, item);
}

void LabelListWidget::remove_item(LabelListItem *item) {
    const auto index = this->model_->indexFromItem(item);
    this->model_->removeRows(index.row(), 1, QModelIndex());
}

void LabelListWidget::select_item(LabelListItem *item) {
    const auto index = this->model_->indexFromItem(item);
    selectionModel()->select(
        index, QItemSelectionModel::SelectionFlag::Select
    );
}

LabelListItem *LabelListWidget::find_item_by_shape(const TlShape &shape) {
    for (auto row = 0; row < this->model_->rowCount(); ++row) {
        auto *s_it = this->model_->item(row, 0);
        auto *item = dynamic_cast<LabelListItem *>(s_it);
        if (item->shape() == shape)
            return item;
    }
    throw std::runtime_error("cannot find shape: {shape}"); //.format(shape));
}

void LabelListWidget::clear() {
    this->model_->clear();
}
