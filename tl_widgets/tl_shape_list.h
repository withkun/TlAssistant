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

private:
    QTextDocument      *doc_{nullptr};
};

class ShapeListItem : public QStandardItem {
public:
    explicit ShapeListItem(const QString &text) { InitItem(text); };
    ShapeListItem(const QString &text, const TlShape &shape);

    ShapeListItem *clone() const override;
    void setShape(const TlShape &shape);
    TlShape shape() const;

private:
    void InitItem(const QString &text);
};

// ShapeItemModel -> QStandardItemModel -> QAbstractItemModel -> QObject
class ShapeItemModel : public QStandardItemModel {
    Q_OBJECT
public:
    bool removeRows(int row, int count, const QModelIndex &parent) override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) override;

signals:
    void itemDropped();
};

// QListView是列表形式的展示控件
// QListWidget继承自QListView, 是表格形式的展示控件
// 本质区别: QListView基于Model(需要自己建模), QListWidget基于Item
class ShapeListView : public QListView {
    Q_OBJECT
public:
    explicit ShapeListView(QWidget *parent = nullptr);

signals:
    void item_dropped();
    void item_changed(ShapeListItem *item);
    void item_double_clicked(ShapeListItem *item);
    void item_selection_changed(const QList<ShapeListItem *> &selected, const QList<ShapeListItem *> &deselect);

public slots:

private:
    QList<ShapeListItem *>      selectedItems_;
    ShapeItemModel             *model_{nullptr};

public:
    //void __init__();
    int32_t len() const;
    QList<ShapeListItem *> items() const;
    //void __iter__();

    void itemDroppedEvent();
    void itemChangedEvent(QStandardItem *item);
    void itemSelectionChangedEvent(const QItemSelection &selected, const QItemSelection &deselect);
    void itemDoubleClickedEvent(const QModelIndex &index);
    QList<ShapeListItem *> selected_items();
    void scroll_to_item(ShapeListItem *item);
    void add_item(ShapeListItem *item);
    void removeItem(ShapeListItem *item);
    void select_item(ShapeListItem *item);
    ShapeListItem *find_item_by_shape(const TlShape &shape);
    void clear();

    bool empty() const;
};
#endif //__INC_SHAPE_LIST_H