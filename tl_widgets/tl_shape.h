#ifndef __INC_SHAPE_H
#define __INC_SHAPE_H

#include "qt_utils.h"

#include <QObject>
#include <QPointF>
#include <QRect>
#include <QColor>
#include <QMap>


class Shape {
public:
    QString                     label_;
    int32_t                     group_id_{-1};
    QString                     shape_type_{"polygon"};
    QMap<QString, bool>         flags_;
    QString                     description_;
    cv::Mat                     mask_;
    QList<QPointF>              points_;
    QList<int32_t>              point_labels_;
    QMap<QString, QByteArray>   other_data_;
    bool                        closed_{false};
    bool                        visible_{true};


    void post_init();

    bool can_add_point() const;
    void insert_point(int32_t i, const QPointF &point, int32_t label);
    bool can_remove_point() const;
    void remove_point(int32_t i);
    void move_vertex(int32_t i, const QPointF &pos);
    void translate(const QPointF &offset);
    Shape copy() const;
};


class TlShape : public QObject {
    Q_OBJECT
public:
    TlShape(const QString &label="",
            const QColor &line_color=TlShape::line_color,
            const QString &shape_type="polygon",
            const QMap<QString, bool> &flags={},
            int32_t group_id=None,
            const QString &description="",
            const cv::Mat &mask=cv::Mat(),
            const QList<QPointF> &points={},
            bool closed=false);

    TlShape(const QString &shape_type, const QList<QPointF> &points, const QList<int32_t> &point_labels, bool closed);

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
    QList<int32_t>              point_labels_;
    QString                     shape_type_;
    QMap<QString, bool>         flags_;
    QString                     description_;
    cv::Mat                     mask_;
    QMap<QString, QString>      other_data_;
    bool                        closed_{false};
    bool                        visible_{true};

private:
    std::tuple<QString, QList<QPointF>, QList<int32_t>> shape_raw_;
    bool                        points_raw_{false};
    bool                        shape_type_raw_{false};
    bool                        fill_{false};
    bool                        selected_{false};

    int32_t                     highlightIndex_{None};
    int32_t                     highlightMode_{NEAR_VERTEX};
    std::map<int32_t, float>    highlight_sizes_;
    std::map<int32_t, int32_t>  highlight_shapes_;

    QString                     uuid_;

public:
    QPointF scale_point(const QPointF &point) const;
    void setShapeRefined(const QString &shape_type, const QList<QPointF> &points, const QList<int32_t> &point_labels, const cv::Mat &mask=cv::Mat());
    void restoreShapeRaw();
    QString shape_type() const;
    void shape_type(QString value);
    void close();
    void addPoint(const QPointF &point, int32_t label=1);
    bool canAddPoint() const;
    QPointF popPoint();
    bool can_add_point() const;
    void insert_point(int32_t i, const QPointF &point, int32_t label=1);
    bool can_remove_point() const;
    void remove_point(int32_t i);
    void move_vertex(int32_t i, QPointF pos);
    void translate(QPointF offset);
    bool isClosed() const;
    void setOpen();
    void paint(QPainter &painter);
    void drawVertex(QPainterPath &path, int32_t i);
    int32_t nearestVertex(QPointF point, float epsilon) const;
    int32_t nearestEdge(QPointF point, float epsilon) const;
    bool containsPoint(QPointF point);
    QPainterPath makePath() const;
    QRectF boundingRect() const;
    void moveBy(const QPointF &offset);
    void moveVertex(int32_t i, const QPointF &pos);
    void highlightVertex(int32_t i, int32_t action);
    void highlightClear();
    TlShape copy() const;

    QString key() const;

    TlShape(const TlShape &shape);
    void SetValue(const TlShape &shape);

    TlShape clone() const;
    int32_t size() const;
    void clear();

    QPointF &operator[](int32_t index);

    TlShape &operator=(const TlShape &shape);
    bool operator==(const TlShape &shape) const;
    bool operator!=(const TlShape &shape) const;
    bool operator<(const TlShape &shape) const;
    explicit operator bool() const;
};


int32_t nearest_index_within_epsilon(QList<double> distances, float epsilon);

int32_t nearest_vertex_index(const TlShape &shape, const QPointF &point, float scale, float epsilon);

int32_t nearest_edge_index(const TlShape &shape, const QPointF &point, float scale, float epsilon);

int32_t nearest_rotation_point_index(const TlShape &shape, const QPointF &point, float scale, float epsilon);

QPointF get_rotation_handle(const TlShape &shape, int32_t index);

QPointF oriented_rectangle_center(const TlShape &shape);

QList<QPointF> oriented_rectangle_arrow_points(const TlShape &shape);

void rotate(TlShape &shape, const QPointF &center, float angle, const QList<QPointF> &source_points);

QList<QPointF> rotate_points_around_origin(QList<QPointF> points, float angle);

#endif //__INC_SHAPE_H