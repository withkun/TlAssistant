#ifndef __INC_SHAPE_H
#define __INC_SHAPE_H

#include "utilities.h"

#include <QObject>
#include <QPointF>
#include <QRect>
#include <QColor>
#include <QMap>

class TlShape : public QObject {
    Q_OBJECT
public:
    TlShape(const QString &label="",
            const QColor &line_color=TlShape::line_color,
            const QString &shape_type="polygon",
            const QMap<QString, bool> &flags={},
            int32_t group_id=None,
            const QString &description="",
            const cv::Mat &mask=cv::Mat());

public:   // 类变量:
    // Render handles as squares
    const static int32_t P_SQUARE       = 0;
    // Render handles as circles
    const static int32_t P_ROUND        = 1;

    // Flag for the handles we would move if dragging
    const static int32_t MOVE_VERTEX    = 0;
    // Flag for all other handles on the current shape
    const static int32_t NEAR_VERTEX    = 1;
    //
    const static int32_t PEN_WIDTH      = 2;

    // The following class variables influence the drawing of all shape objects.
    static QColor               line_color;
    static QColor               fill_color;
    static QColor               vertex_fill_color;
    static QColor               select_line_color;
    static QColor               select_fill_color;
    static QColor               hvertex_fill_color;

    static QColor               current_vertex_fill_color;

    static int32_t              point_type_;    // = P_ROUND
    static int32_t              point_size_;    // = 8
    static float                scale_;         // = 1.

private:      // 实例变量
    friend class Canvas;
    friend class MainWindow;
    QColor                      line_color_;
    QColor                      fill_color_;
    QColor                      select_line_color_;
    QColor                      select_fill_color_;
    QColor                      vertex_fill_color_;
    QColor                      hvertex_fill_color_;
    QColor                      current_vertex_fill_color_;

public:
    QString                     label_;
    int32_t                     group_id_{None};
    QList<QPointF>              points_;
    QString                     shape_type_;
    QMap<QString, bool>         flags_;
    QString                     description_;
    cv::Mat                     mask_;
    QMap<QString, QString>      other_data_;

private:
    QList<int32_t>              point_labels_;
    std::tuple<QString, QList<QPointF>, QList<int32_t>> shape_raw_;
    bool                        points_raw_{false};
    bool                        shape_type_raw_{false};
    bool                        fill_{false};
    bool                        selected_{false};

    int32_t                     highlightIndex_{None};
    int32_t                     highlightMode_{NEAR_VERTEX};
    std::map<int32_t, float>    highlight_sizes_;
    std::map<int32_t, int32_t>  highlight_shapes_;

    bool                        closed_{false};

    QString                     uuid_;

public:
    QPointF scale_point(const QPointF &point) const;
    void setShapeRefined(const QString &shape_type, const QList<QPointF> &points, const QList<int32_t> &point_labels, const cv::Mat &mask=cv::Mat());
    void restoreShapeRaw();
    [[nodiscard]] QString shape_type() const;
    void shape_type(QString value);
    void close();
    void addPoint(const QPointF &point, int32_t label=1);
    bool canAddPoint() const;
    QPointF popPoint();
    void insertPoint(int32_t i, const QPointF &point, int32_t label=1);
    bool canRemovePoint() const;
    void removePoint(int32_t i);
    bool isClosed() const;
    void setOpen();
    void paint(QPainter &painter);
    void drawVertex(QPainterPath &path, int32_t i);
    int32_t nearestVertex(QPointF point, float epsilon) const;
    int32_t nearestEdge(QPointF point, float epsilon) const;
    bool containsPoint(QPointF point);
    [[nodiscard]] QPainterPath makePath() const;
    [[nodiscard]] QRectF boundingRect() const;
    void moveBy(const QPointF &offset);
    void moveVertex(int32_t i, const QPointF &pos);
    void highlightVertex(int32_t i, int32_t action);
    void highlightClear();
    [[nodiscard]] TlShape copy() const;

    [[nodiscard]] QString key() const;

    TlShape(const TlShape &shape);
    void SetValue(const TlShape &shape);

    [[nodiscard]] TlShape clone() const;
    [[nodiscard]] int32_t size() const;
    void clear();

    QPointF &operator[](int32_t index);

    TlShape &operator=(const TlShape &shape);
    bool operator==(const TlShape &shape) const;
    bool operator!=(const TlShape &shape) const;
    bool operator<(const TlShape &shape) const;
    explicit operator bool() const;
};
#endif //__INC_SHAPE_H