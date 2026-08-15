#ifndef __INC_SHAPE_H
#define __INC_SHAPE_H

#include "qt_utils.h"

#include <QObject>
#include <QPointF>
#include <QRect>
#include <QColor>
#include <QMap>


class TlShape {
public:
    TlShape(const QString &label="",
            const QString &shape_type="polygon",
            int32_t group_id=None,
            const QString &description="",
            const cv::Mat &mask=cv::Mat(),
            const QList<QPointF> &points={},
            bool closed=false);
    TlShape(const QString &shape_type, const QList<QPointF> &points, const QList<int32_t> &point_labels, bool closed);

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

private:
    friend class Canvas;
    QString                     uuid_;
    void _post_init_();

public:
    bool can_add_point() const;
    void insert_point(int32_t i, const QPointF &point, int32_t label=1);
    bool can_remove_point() const;
    void remove_point(int32_t i);
    void move_vertex(int32_t i, const QPointF &pos);
    void translate(const QPointF &offset);

    TlShape copy() const;
    TlShape clone() const;

    QPointF &operator[](const int32_t index) {
        return (index >= 0) ?  points_[index] : points_[points_.size() + index];
    }

    bool operator==(const TlShape &shape) const {
        return uuid_ == shape.uuid_;
    }

    bool operator<(const TlShape &shape) const {
        return uuid_ < shape.uuid_;
    }

    explicit operator bool() const {
        return !points_.empty();
    }
};


int32_t nearest_index_within_epsilon(const QList<double> &distances, float epsilon);

int32_t nearest_vertex_index(const TlShape &shape, const QPointF &point, float scale, float epsilon);

int32_t nearest_edge_index(const TlShape &shape, const QPointF &point, float scale, float epsilon);

int32_t nearest_rotation_point_index(const TlShape &shape, const QPointF &point, float scale, float epsilon);

QPointF get_rotation_handle(const TlShape &shape, int32_t index);

QPointF oriented_rectangle_center(const TlShape &shape);

QList<QPointF> oriented_rectangle_arrow_points(const TlShape &shape);

void rotate(TlShape &shape, const QPointF &center, float angle, const QList<QPointF> &source_points);

QList<QPointF> rotate_points_around_origin(const QList<QPointF> &points, float angle);

#endif //__INC_SHAPE_H