#ifndef __INC_SHAPE_RENDER_H
#define __INC_SHAPE_RENDER_H

#include "tl_shape.h"
#include "qt_utils.h"
#include <QPainterPath>


//@dataclasses.dataclass(frozen=True)
class VertexHighlight {
public:
    int32_t     index_{None};
    QString     mode_;                   // Literal["move", "near"]

    float size_factor() const;
    QString point_type() const;

    explicit operator bool() const {
        return index_ != None && !mode_.isEmpty();
    }
};

// @dataclasses.dataclass(frozen=True)
class Palette {
public:
    QColor line_;
    QColor fill_;
    QColor select_line_;
    QColor select_fill_;
    QColor vertex_fill_;
    QColor hvertex_fill_;

    //@classmethod
    static Palette from_rgb(const std::vector<int32_t> &rgb);

    explicit operator bool() const {
        return !line_.isValid() || !fill_.isValid() ||
            !select_line_.isValid() || !select_fill_.isValid() ||
            !vertex_fill_.isValid() || !hvertex_fill_.isValid();
    }
};

//@dataclasses.dataclass(frozen=True)
class ShapeRenderContext {
public:
    float               scale_;
    Palette             palette_;
    int                 point_size_;
    QString             point_type_;    //: Literal["square", "round"]
    bool                selected_;
    bool                fill_;
    VertexHighlight     highlight_;
    VertexHighlight     rotation_highlight_;
    bool                show_label_{false};
};

//@dataclasses.dataclass(frozen=True)
class ShapePaths {
public:
    QPainterPath        line_;
    QPainterPath        vertices_;
    QPainterPath        negative_vertices_;
    QPainterPath        rotation_vertices_;
    QPainterPath        orientation_arrow_;
};


void render_shape(QPainter &painter, const TlShape &shape, const ShapeRenderContext &context);

void paint_shape_label(QPainter &painter, const TlShape &shape, const ShapeRenderContext &context);

void paint_shape_mask(QPainter &painter, const TlShape &shape, const ShapeRenderContext &context);

void paint_shape_points(QPainter &painter, const TlShape &shape, const ShapeRenderContext &context);

void paint_filled_vertices(QPainter &painter, const QPainterPath &path, bool highlighted, const Palette &palette);

std::pair<float, QString> resolve_vertex_style(const VertexHighlight &highlight, int32_t vertex_index, int32_t default_size, const QString &default_point_type);

void build_shape_point_path(QPainterPath &path, const TlShape &shape, const ShapeRenderContext &context, int32_t vertex_index);

void build_shape_rotation_point_path(QPainterPath &path, const TlShape &shape, const ShapeRenderContext &context, int32_t vertex_index);

void draw_vertex(QPainterPath &path, const QPointF &pos, float size, const QString &point_type);

void build_shape_oriented_rectangle_arrow_path(QPainterPath &path, const TlShape &shape, float scale);

ShapePaths build_shape_points_paths(const TlShape &shape, const ShapeRenderContext &context);

bool is_hit_by_point(const TlShape &shape, const QPointF &point, float scale, int32_t point_size, float epsilon);

QRectF shape_bounds(const TlShape &shape);

QPainterPath build_image_path(const TlShape &shape);

#endif //__INC_SHAPE_RENDER_H