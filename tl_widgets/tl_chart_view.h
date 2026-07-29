#ifndef __INC_CHART_VIEW_H
#define __INC_CHART_VIEW_H

#include <QtCharts>


class TlChartView : public QChartView {
    Q_OBJECT
public:
    TlChartView(QChart *chart, QWidget *parent = nullptr);

    void mouseMoveEvent(QMouseEvent *event) override;

    void mouseReleaseEvent(QMouseEvent *event) override ;

    void mousePressEvent(QMouseEvent *event) override;

signals:
    void sgl_recoverRange(QChartView *);

private:
    bool is_Pressed_{true};
};
#endif //__INC_CHART_VIEW_H