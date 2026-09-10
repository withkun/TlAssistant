#ifndef __INC_LABEL_LIST_H
#define __INC_LABEL_LIST_H

#include <QListWidget>


class EscapableListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit EscapableListWidget(QWidget *parent=nullptr) : QListWidget(parent) {}

protected:
    void keyPressEvent(QKeyEvent *keyEvent) override;
};

class UniqueLabelList : public EscapableListWidget {
    Q_OBJECT
public:
    explicit UniqueLabelList(QWidget *parent=nullptr);

    QListWidgetItem *find_label_item(const QString &label);
    void add_label_item(const QString &label, const std::tuple<int, int, int> &color);

protected:
    void mousePressEvent(QMouseEvent *mouseEvent) override;
};
#endif //__INC_LABEL_LIST_H