#ifndef __INC_ZOOM_WIDGET_H
#define __INC_ZOOM_WIDGET_H

#include <QSpinBox>


class ZoomWidget : public QSpinBox {
    Q_OBJECT
public:
    explicit ZoomWidget(int32_t value = 100, QWidget *parent = nullptr);

    QSize minimumSizeHint() const override;
};
#endif //__INC_ZOOM_WIDGET_H