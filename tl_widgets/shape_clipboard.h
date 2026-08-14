#ifndef __INC_SHAPE_CLIPBOARD_H
#define __INC_SHAPE_CLIPBOARD_H

#include <QWidget>
#include "tl_shape.h"


class ShapeClipboard : public QObject {
    Q_OBJECT
public:
    ShapeClipboard(QObject *parent = nullptr);

    void store(const QList<TlShape> &shapes);
    QList<TlShape> paste() const;

signals:
    void availability_changed(bool changed);

private:
    QList<TlShape>  buffer_;
};
#endif //__INC_SHAPE_CLIPBOARD_H