#include "shape_render.h"
#include <QPainter>


static constexpr int32_t PEN_WIDTH = 2;

//@property
float VertexHighlight::size_factor() const {
    return QMap<QString, float>{
        {"move", 1.5f},
        {"near", 4.0f}
    }[mode_];
}

//@property
QString VertexHighlight::point_type() const {
    //match self.mode:
    if (mode_ == "move")
        return "square";
    if (mode_ == "near")
        return "round";
    return mode_;
}

//@classmethod
Palette Palette::from_rgb(const std::vector<int32_t> &rgb) {
    const int32_t r = rgb[0], g = rgb[1], b = rgb[2];
    return Palette{
        .line_ = QColor(r, g, b),
        .fill_ = QColor(r, g, b, 128),
        .select_line_ = QColor(255, 255, 255),
        .select_fill_ = QColor(r, g, b, 155),
        .vertex_fill_ = QColor(r, g, b),
        .hvertex_fill_ = QColor(255, 255, 255),
    };
}

void render_shape(
    QPainter &painter, const TlShape &shape, const ShapeRenderContext &context
) {
    if (shape.mask_.empty() && shape.points_.empty())
        return;

    const auto &palette = context.palette_;
    const auto &color = context.selected_ ? palette.select_line_ : palette.line_;
    auto pen = QPen(color);
    pen.setWidth(PEN_WIDTH);
    painter.setPen(pen);

    if (shape.shape_type_ == "mask" && !shape.mask_.empty())
        paint_shape_mask(painter, shape, context);

    if (!shape.points_.empty())
        paint_shape_points(painter, shape, context);

    if (context.show_label_)
        paint_shape_label(painter, shape, context);
}

void paint_shape_label(
    QPainter &painter,
    const TlShape &shape,
    const ShapeRenderContext &context
) {
    if (shape.label_.isEmpty() || shape.points_.empty())
        return;
    // Anchor at the points' top-left so the text stays close to the shape and
    // tracks zoom/pan; lift it by the pen width to clear the outline stroke.
    qreal top_left_x = std::numeric_limits<float>::max(), top_left_y = std::numeric_limits<float>::max();
    std::ranges::for_each(shape.points_, [&](auto &p) { top_left_x = std::min(top_left_x, p.x()); top_left_y = std::min(top_left_y, p.y()); });
    painter.setPen(QPen(context.palette_.line_));
    painter.drawText(
        QPointF(top_left_x * context.scale_, top_left_y * context.scale_ - PEN_WIDTH),
        shape.label_
    );
}

void paint_shape_mask(
    QPainter &painter,
    const TlShape &shape,
    const ShapeRenderContext &context
) {
    int32_t r, g, b, a;
    std::vector<std::vector<cv::Point>> contours;
    //assert shape.mask is not None
    (context.selected_ ? context.palette_.select_fill_ : context.palette_.fill_).getRgb(&r, &g, &b, &a);
    cv::Mat image_to_draw = cv::Mat::zeros({shape.mask_.cols, shape.mask_.rows}, CV_8UC4);
    image_to_draw.setTo(cv::Scalar(r, g, b, a), shape.mask_);
    const auto qimage = QImage::fromData(utils::img_arr_to_data(image_to_draw));
    const auto origin = shape.points_[0];
    const auto target_top_left = origin * context.scale_;
    const auto target_rect = QRectF(
        target_top_left.x(),
        target_top_left.y(),
        qimage.width() * context.scale_,
        qimage.height() * context.scale_
    );
    painter.drawImage(target_rect, qimage);

    auto path = QPainterPath();
    cv::findContours(shape.mask_, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    for (const auto &contour : contours) {
        const auto [x, y] = shape.points_[0];
        path.moveTo(QPointF(contour[0].x+x, contour[0].y+y) * context.scale_);
        for (auto idx = 1; idx < contour.size(); ++idx) {
            path.lineTo(QPointF(contour[idx].x+x, contour[idx].y+y) * context.scale_);
        }
    }
    painter.drawPath(path);
}

void paint_shape_points(
    QPainter &painter,
    const TlShape &shape,
    const ShapeRenderContext &context
) {
    auto &palette = context.palette_;
    auto paths = build_shape_points_paths(shape, context);

    painter.drawPath(paths.line_);
    paint_filled_vertices(
        painter,
        paths.vertices_,
        static_cast<bool>(context.highlight_),
        palette
    );
    paint_filled_vertices(
        painter,
        paths.rotation_vertices_,
        static_cast<bool>(context.rotation_highlight_),
        palette
    );
    if (context.fill_ && !QKey{"line", "linestrip", "points", "mask"}.contains(shape.shape_type_)) {
        const auto fill = context.selected_ ? palette.select_fill_ : palette.fill_;
        painter.fillPath(paths.line_, fill);
    }
    if (paths.orientation_arrow_.length() > 0) {
        auto arrow_pen = QPen(palette.vertex_fill_);
        arrow_pen.setWidth(PEN_WIDTH);
        painter.setPen(arrow_pen);
        painter.drawPath(paths.orientation_arrow_);
    }
    if (paths.negative_vertices_.length() > 0) {
        auto neg_color = QColor(255, 0, 0, 255);
        auto neg_pen = QPen(neg_color);
        neg_pen.setWidth(PEN_WIDTH);
        painter.setPen(neg_pen);
        painter.drawPath(paths.negative_vertices_);
        painter.fillPath(paths.negative_vertices_, neg_color);
    }
}

void paint_filled_vertices(
    QPainter &painter,
    const QPainterPath &path,
    const bool highlighted,
    const Palette &palette
) {
    if (path.length() == 0)
        return;
    const auto &fill = highlighted ? palette.hvertex_fill_ : palette.vertex_fill_;
    painter.drawPath(path);
    painter.fillPath(path, fill);
}

std::pair<float, QString> resolve_vertex_style(
    const VertexHighlight &highlight,
    const int32_t vertex_index,
    const int32_t default_size,
    const QString &default_point_type
) {
    if (highlight && highlight.index_ == vertex_index)
        return { default_size * highlight.size_factor(), highlight.point_type() };
    return { default_size, default_point_type };
}

void build_shape_point_path(
    QPainterPath &path,
    const TlShape &shape,
    const ShapeRenderContext &context,
    const int32_t vertex_index
) {
    const auto [size, point_type] = resolve_vertex_style(
        context.highlight_,
        vertex_index,
        context.point_size_,
        context.point_type_
    );
    const auto pos = shape.points_[vertex_index] * context.scale_;
    draw_vertex(path, pos, size, point_type);
}

void build_shape_rotation_point_path(
    QPainterPath &path,
    const TlShape &shape,
    const ShapeRenderContext &context,
    const int32_t vertex_index
) {
    const auto [size, point_type] = resolve_vertex_style(
        context.rotation_highlight_,
        vertex_index,
        context.point_size_,
        context.point_type_
    );
    const auto handle = get_rotation_handle(shape, vertex_index);
    const auto pos = handle * context.scale_;
    draw_vertex(path, pos, size, point_type);
}

void draw_vertex(
    QPainterPath &path,
    const QPointF &pos,
    const float size,
    const QString &point_type      //: Literal["square", "round"],
) {
    const auto half = size / 2.0f;
    if (point_type == "square")
        path.addRect({pos.x() - half, pos.y() - half, size, size});
    else if (point_type == "round")
        path.addEllipse(pos, half, half);
    else
        throw std::invalid_argument("Unsupported vertex shape: " + point_type.toStdString());
}

void build_shape_oriented_rectangle_arrow_path(
    QPainterPath &path, const TlShape &shape, const float scale
) {
    const auto points = oriented_rectangle_arrow_points(shape);
    const auto head_right=points[0]*scale, tip=points[1]*scale, head_left=points[2]*scale, tail=points[3]*scale;
    path.moveTo(head_right);
    path.lineTo(tip);
    path.lineTo(head_left);
    path.moveTo(tail);
    path.lineTo(tip);
}

ShapePaths build_shape_points_paths(
    const TlShape &shape,
    const ShapeRenderContext &context
) {
    auto paths = ShapePaths();
    auto scale = context.scale_;
    auto points = shape.points_;
    if (QKey{"rectangle", "mask"}.contains(shape.shape_type_)) {
        //assert len(points) in [1, 2]
        if (points.size() == 2)
            paths.line_.addRect(
                QRectF(
                    points[0] * scale,
                    points[1] * scale
                )
            );
        if (shape.shape_type_ == "rectangle")
            for (auto i = 0; i < points.size(); ++i)
                build_shape_point_path(
                    paths.vertices_, shape, context, i
                );
    } else if (shape.shape_type_ == "oriented_rectangle") {
        //assert len(points) in [1, 2, 4]
        if (points.size() == 4) {
            paths.line_.moveTo(points[0] * scale);
            for (auto i = 0; i < points.size(); ++i) {
                paths.line_.lineTo(points[i] * scale);
                build_shape_point_path(
                    paths.vertices_, shape, context, i
                );
            }
            paths.line_.lineTo(points[0] * scale);
            for (auto i = 0; i < points.size(); ++i)
                build_shape_rotation_point_path(
                    paths.rotation_vertices_,
                    shape,
                    context,
                    i
                );
            build_shape_oriented_rectangle_arrow_path(
                paths.orientation_arrow_, shape, scale
            );
        } else if (points.size() == 2) {
            paths.line_.moveTo(points[0] * scale);
            paths.line_.lineTo(points[1] * scale);
            for (auto i = 0; i < 2; ++i)
                build_shape_point_path(
                    paths.vertices_, shape, context, i
                );
        }
    } else if (shape.shape_type_ == "circle") {
        //assert len(points) in [1, 2]
        if (points.size() == 2) {
            const auto radius = utils::distance((points[0] - points[1]) * scale);
            paths.line_.addEllipse(points[0] * scale, radius, radius);
        }
        for (auto i = 0; i < points.size(); ++i)
            build_shape_point_path(
                paths.vertices_, shape, context, i
            );
    } else if (shape.shape_type_ == "linestrip") {
        paths.line_.moveTo(points[0] * scale);
        for (auto i = 0; i < points.size(); ++i) {
            paths.line_.lineTo(points[i] * scale);
            build_shape_point_path(
                paths.vertices_, shape, context, i
            );
        }
    } else if (shape.shape_type_ == "points") {
        //assert len(points) == len(shape.point_labels)
        for (const auto &&[i, point_label] : shape.point_labels_ | std::views::enumerate) {
            auto &path = point_label == 1 ? paths.vertices_ : paths.negative_vertices_;
            build_shape_point_path(
                path, shape, context, i
            );
        }
    } else {
        paths.line_.moveTo(points[0] * scale);
        for (auto i = 0; i < points.size(); ++i) {
            paths.line_.lineTo(points[i] * scale);
            build_shape_point_path(
                paths.vertices_, shape, context, i
            );
        }
        if (shape.closed_)
            paths.line_.lineTo(points[0] * scale);
    }
    return paths;
}

bool is_hit_by_point(
    const TlShape &shape,
    const QPointF &point,
    const float scale,
    const int32_t point_size,
    const float epsilon
) {
    if (QKey{"line", "linestrip"}.contains(shape.shape_type_))
        return (
            nearest_edge_index(shape, point, scale, epsilon)
            != None
        );
    if (shape.shape_type_ == "points")
        return false;
    if (shape.shape_type_ == "point") {
        if (shape.points_.empty())
            return false;
        return utils::distance((point - shape.points_[0]) * scale) <= point_size / 2.f;
    }
    if (!shape.mask_.empty()) {
        const auto raw_y = int32_t(point.y() - shape.points_[0].y());
        const auto raw_x = int32_t(point.x() - shape.points_[0].x());
        if (
            raw_y < 0
            || raw_y >= shape.mask_.rows
            || raw_x < 0
            || raw_x >= shape.mask_.cols
        )
            return false;
        return bool(shape.mask_.at<int8_t>(raw_y, raw_x));
    }
    return build_image_path(shape).contains(point);
}

QRectF shape_bounds(const TlShape &shape) {
    return build_image_path(shape).boundingRect();
}

QPainterPath build_image_path(const TlShape &shape) {
    const auto &points = shape.points_;
    auto out = QPainterPath();
    if (QKey{"rectangle", "mask"}.contains(shape.shape_type_)) {
        if (points.size() == 2)
            out.addRect(
                QRectF(points[0], points[1])
            );
    } else if (shape.shape_type_ == "circle") {
        if (points.size() == 2) {
            const auto radius = utils::distance(points[0] - points[1]);
            out.addEllipse(points[0], radius, radius);
        }
    } else if (shape.shape_type_ == "oriented_rectangle") {
        if (points.size() == 4) {
            out.moveTo(points[0]);
            for (auto i = 1; i < points.size(); ++i)
                out.lineTo(points[i]);
            out.lineTo(points[0]);
        }
    } else {
        if (!points.empty()) {
            out.moveTo(points[0]);
            for (auto i = 1; i < points.size(); ++i)
                out.lineTo(points[i]);
        }
    }
    return out;
}