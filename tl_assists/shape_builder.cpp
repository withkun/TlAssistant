#include "shape_builder.h"


static TlShape build_shape(
    const std::string &shape_type,
    const QList<QPointF> &points,
    const cv::Mat &mask,
    const std::string &label,
    const std::string &description
) {
    return TlShape{
       QString::fromStdString(label),
       QString::fromStdString(shape_type),
       None,
       QString::fromStdString(description),
       mask,
       points,
       true,
    };
}

TlShape shape_from_detection(
    const Detection &detection,
    const std::string &shape_type
) {
    if (shape_type == "rectangle") {
        if (!detection.bbox)
            return {};
        const auto &[xmin, ymin, xmax, ymax] = detection.bbox;
        return build_shape(
            "rectangle",
            {QPointF(xmin, ymin), QPointF(xmax, ymax)},
            {},
            detection.label,
            detection.description
        );
    }
    if (shape_type == "polygon") {
        if (detection.mask.empty())
            return {};
        auto polygon = compute_polygon_from_mask(detection.mask);
        if (detection.bbox)
            std::ranges::for_each(polygon, [&](auto &p) {
                p.x += detection.bbox.x1; p.y += detection.bbox.y1;
            });
        if (polygon.size() < 3)
            return {};
        return build_shape(
            "polygon",
            polygon | std::views::transform([](auto &p) { return QPointF(p.x, p.y); }) | std::ranges::to<QList<QPointF>>(),
            {},
            detection.label,
            detection.description
        );
    }
    if (shape_type == "mask") {
        if (!detection.bbox || detection.mask.empty())
            return {};
        if (!cv::hasNonZero(detection.mask))
            return {};
        const auto xmin = int(detection.bbox.x1);
        const auto ymin = int(detection.bbox.y1);
        const auto xmax = int(detection.bbox.x2);
        const auto ymax = int(detection.bbox.y2);
        return build_shape(
            "mask",
            {QPointF(xmin, ymin), QPointF(xmax, ymax)},
            detection.mask,
            detection.label,
            detection.description
        );
    }
    if (shape_type == "circle") {
        const auto circle = circle_for_detection(detection);
        if (!circle)
            return {};
        return build_shape(
            "circle",
            {
                QPointF(circle.cx, circle.cy),
                QPointF(circle.cx + circle.radius, circle.cy)
            },
            {},
            detection.label,
            detection.description
        );
    }
    if (shape_type == "oriented_rectangle") {
        const auto corners = oriented_rectangle_for_detection(detection);
        if (corners.empty())
            return {};
        return build_shape(
            "oriented_rectangle",
            corners,
            {},
            detection.label,
            detection.description
        );
    }
    throw std::invalid_argument("Unsupported shape_type: " + shape_type);
}

QList<QPointF> oriented_rectangle_for_detection(
    const Detection &detection
) {
    if (!detection.mask.empty()) {
        const auto corners = compute_oriented_rectangle_from_mask(detection.mask);
        if (!corners.empty()) {
            cv::Point offset_xy = (
                detection.bbox ? cv::Point(detection.bbox.x1, detection.bbox.y1) : cv::Point(0.0, 0.0)
            );
            return corners | std::views::transform([&offset_xy](auto &p) { return QPointF(p.x + offset_xy.x, p.y + offset_xy.y); }) | std::ranges::to<QList<QPointF>>();
        }
    }
    if (detection.bbox) {
        const auto &[xmin, ymin, xmax, ymax] = detection.bbox;
        return QList<QPointF>{
            QPointF(xmin, ymin), QPointF(xmax, ymin), QPointF(xmax, ymax), QPointF(xmin, ymax)
        };
    }
    return {};
}

Circle circle_for_detection(const Detection &detection) {
    if (!detection.mask.empty()) {
        const auto circle = compute_circle_from_mask(detection.mask);
        if (circle) {
            const QPointF offset_xy = (
                detection.bbox ? QPointF(detection.bbox.x1, detection.bbox.y1) : QPointF(0.0, 0.0)
            );
            return Circle{
                .cx=circle.cx + (float)offset_xy.x(),
                .cy=circle.cy + (float)offset_xy.y(),
                .radius=circle.radius
            };
        }
    }
    if (detection.bbox) {
        // Inscribed in bbox when no usable mask is available.
        const auto [xmin, ymin, xmax, ymax] = detection.bbox;
        const auto radius = std::min(xmax - xmin, ymax - ymin) / 2;
        if (radius > 0)
            return Circle{.cx=float(xmin + xmax) / 2, .cy=float(ymin + ymax) / 2, .radius=float(radius)};
    }
    return {};
}

// Output formats that drop a bbox-only detection (the builder returns None when
// the mask is absent). Derived from _shape_from_detection so it cannot drift; the
// probe mirrors the runtime warning condition (a box but no mask).
//MASK_REQUIRED_SHAPE_TYPES: Final[frozenset[AiOutputFormat]] = frozenset(
//    shape_type
//    for shape_type in typing.get_args(AiOutputFormat)
//    if _shape_from_detection(
//        detection=Detection(bbox=(0, 0, 1, 1), mask=None), shape_type=shape_type
//    )
//    is None
//)


QList<TlShape> shapes_from_detections(
    const QList<Detection> &detections,
    const std::string &shape_type
) {
    QList<TlShape> shapes;
    for (auto &detection : detections) {
        auto shape = shape_from_detection(detection, shape_type);
        if (shape)
            shapes.append(shape);
    }
    return shapes;
}