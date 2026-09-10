#ifndef __INC_SHAPE_LIST_H
#define __INC_SHAPE_LIST_H

#include <QListView>
#include <QStandardItem>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QTextDocument>
#include <QPainter>

#include "tl_shape.h"


extern const int32_t LABEL_COLOR_ROLE;

QString format_shape_label(const TlShape &shape);

class TrailingColorDotDelegate: public QStyledItemDelegate {
public:
    explicit TrailingColorDotDelegate(QObject *parent=nullptr) : QStyledItemDelegate(parent) {}

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    static QSize default_size_hint();

    static const char *DOT_;

private:
    QTextDocument                      *doc_{nullptr};
};

class LabelListItem : public QStandardItem {
public:
    explicit LabelListItem(const QString &text="", const TlShape &shape={});

    LabelListItem *clone() const override;

    void set_shape(const TlShape &shape);
    void set_label(const QString &text, const std::tuple<int, int, int> &color);
    TlShape shape() const;
};

// ShapeItemModel -> QStandardItemModel -> QAbstractItemModel -> QObject
class ListItemModel : public QStandardItemModel {
    Q_OBJECT
public:
    bool removeRows(int row, int count, const QModelIndex &parent) override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) override;

signals:
    void item_dropped();
};

class ItemSnapshot {
public:
    // A persistent index, not the item itself: the model owns the item and
    // deletes it on row removal, which would leave a dead wrapper here.
    QPersistentModelIndex               index;
    Qt::CheckState                      check_state;
};

// QListView是列表形式的展示控件
// QListWidget继承自QListView, 是表格形式的展示控件
// 本质区别: QListView基于Model(需要自己建模), QListWidget基于Item
class LabelListWidget : public QListView {
    Q_OBJECT
public:
    explicit LabelListWidget(QWidget *parent=nullptr);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

signals:
    void item_double_clicked(LabelListItem *item);
    void item_selection_changed(const QList<LabelListItem *> &selected, const QList<LabelListItem *> &deselect);
    void item_changed(LabelListItem *item);
    void item_dropped();

private:
    ListItemModel                          *model_{};
    QList<ItemSnapshot>                     press_snapshot_;

public:
    void on_item_dropped();
    void on_item_changed(QStandardItem *item);
    void on_item_selection_changed(const QItemSelection &selected, const QItemSelection &deselected);
    void on_item_double_clicked(const QModelIndex &index);
    QList<LabelListItem *> selected_items();
    QList<LabelListItem *> selection_at_press();
    LabelListItem *resolve_item(const QPersistentModelIndex &index);
    void scroll_to_item(LabelListItem *item);
    void add_item(LabelListItem *item);
    void remove_item(LabelListItem *item);
    void select_item(LabelListItem *item);
    LabelListItem *find_item_by_shape(const TlShape &shape);

    void clear();
    int32_t len() const;
    QList<LabelListItem *> items() const;
    bool empty() const {
        return this->model_->rowCount() == 0;
    }
};
#endif //__INC_SHAPE_LIST_H