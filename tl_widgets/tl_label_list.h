#ifndef __INC_LABEL_LIST_H
#define __INC_LABEL_LIST_H

#include <QListWidget>


class EscapableQListWidget : public QListWidget {
    Q_OBJECT
protected:
    void keyPressEvent(QKeyEvent *keyEvent) override;
};

class LabelList : public EscapableQListWidget {
    Q_OBJECT
public:
    explicit LabelList();

    QListWidgetItem *find_label_item(const QString &label);
    void add_label_item(const QString &label, const std::vector<int32_t> &color);

protected:
    void mousePressEvent(QMouseEvent *mouseEvent) override;
};
#endif //__INC_LABEL_LIST_H