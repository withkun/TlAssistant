#ifndef __INC_SHAPE_LIST_H
#define __INC_SHAPE_LIST_H

#include <QListView>
#include <QStandardItem>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QTextDocument>
#include <QPainter>

#include "tl_shape.h"


QString format_label_with_color_dot(const QString &text, const std::vector<int32_t> &color);
QString format_shape_label(const TlShape &shape, const std::vector<int32_t> &fill_rgb);

class HTMLDelegate: public QStyledItemDelegate {
public:
    explicit HTMLDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    static QSize default_size_hint();

private:
    QTextDocument                      *doc_{nullptr};
};

class ShapeListItem : public QStandardItem {
public:
    ShapeListItem(const QString &text, const TlShape &shape={});

    ShapeListItem *clone() const override;
    void set_shape(const TlShape &shape);
    TlShape shape() const;
};

// ShapeItemModel -> QStandardItemModel -> QAbstractItemModel -> QObject
class ShapeItemModel : public QStandardItemModel {
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
class ShapeListView : public QListView {
    Q_OBJECT
public:
    explicit ShapeListView(QWidget *parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

signals:
    void item_dropped();
    void item_changed(ShapeListItem *item);
    void item_double_clicked(ShapeListItem *item);
    void item_selection_changed(const QList<ShapeListItem *> &selected, const QList<ShapeListItem *> &deselect);

public slots:

private:
    ShapeItemModel                     *model_{};
    QList<ItemSnapshot>                 press_snapshot_;

public:
    void on_item_dropped();
    void on_item_changed(QStandardItem *item);
    void on_item_selection_changed(const QItemSelection &selected, const QItemSelection &deselected);
    void on_item_double_clicked(const QModelIndex &index);
    QList<ShapeListItem *> selected_items();
    QList<ShapeListItem *> selection_at_press();
    ShapeListItem *resolve_item(const QPersistentModelIndex &index);
    void scroll_to_item(ShapeListItem *item);
    void add_item(ShapeListItem *item);
    void removeItem(ShapeListItem *item);
    void select_item(ShapeListItem *item);
    ShapeListItem *find_item_by_shape(const TlShape &shape);

    void clear();
    int32_t len() const;
    QList<ShapeListItem *> items() const;
    bool empty() const {
        return this->model_->rowCount() == 0;
    }
};
#endif //__INC_SHAPE_LIST_H