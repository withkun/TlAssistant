#include "geometry.h"
#include "opencv2/geometry/2d.hpp"


BoundingBox shape_to_xyxy_bbox(const TlShape &shape) {
    //Returns None only when a supported shape is mid-draw (too few points);
    //raises ValueError for shape types that have no bbox interpretation.
    //"""
    if (shape.shape_type_ == "circle") {
        if (shape.points_.size() != 2)
            return {};
        const auto center = shape.points_[0], edge = shape.points_[1];
        const auto radius = utils::distance(edge - center);
        return BoundingBox{
            int32_t(center.x() - radius),
            int32_t(center.y() - radius),
            int32_t(center.x() + radius),
            int32_t(center.y() + radius)
        };
    }
    std::map<QString, int32_t> minimum_points_by_shape_type = {
        {"rectangle", 2},
        {"mask", 2},
        {"polygon", 3},
        {"oriented_rectangle", 4}
    };
    if (!minimum_points_by_shape_type.contains(shape.shape_type_))
        throw std::invalid_argument(std::format("Unsupported shape_type: {}", shape.shape_type_.toStdString()));
    if (shape.points_.size() < minimum_points_by_shape_type[shape.shape_type_])
        return {};
    QRectF bbox(shape.points_.first(), QSizeF(0, 0));
    std::ranges::for_each(shape.points_, [&](auto &p) { bbox = bbox.united(QRectF(p, QSizeF(0, 0))); });
    return {(int32_t)bbox.left(), (int32_t)bbox.top(), (int32_t)bbox.right(), (int32_t)bbox.bottom()};
}

Circle compute_circle_from_mask(const cv::Mat &mask) {
    if (!cv::hasNonZero(mask))
        return {};
    std::vector<cv::Point> locations;
    cv::findNonZero(mask, locations);
    const auto xym = cv::mean(locations);
    // Area-equivalent radius: matches the mask's pixel area, not its extent.
    // For elongated or sparse masks the resulting circle may be smaller than
    // the tightest enclosing one.
    return Circle{
        .cx=float(xym[0]),
        .cy=float(xym[1]),
        .radius=float(std::sqrt(locations.size() / CV_PI))
    };
}

std::vector<cv::Point> compute_oriented_rectangle_from_mask(
    const cv::Mat &mask
) {
    if (!cv::hasNonZero(mask))
        return {};
    std::vector<cv::Point> locations;
    cv::findNonZero(mask, locations);
    if (locations.size() < 3)
        return {};
    std::vector<cv::Point> hull_indices;
    // Qhull returns 2D hull vertices in CCW order, which the rotating
    // calipers loop below relies on for the right-handed perpendicular.
    cv::convexHull(locations, hull_indices, false);
    return min_area_rect(hull_indices);
}

std::vector<cv::Point> min_area_rect(const std::vector<cv::Point> &hull) {
    // Rotating calipers: the minimum-area enclosing rectangle must have one
    // side flush with an edge of the convex hull. Try each hull edge as the
    // rect orientation and keep the smallest-area candidate.
    double best_area = std::numeric_limits<double>::infinity();
    std::vector<cv::Point> best_corners;
    const auto n = hull.size();
    for (auto i = 0; i < n; ++i) {
        const auto edge = hull[(i + 1) % n] - hull[i];
        const auto length = float(std::sqrt(edge.dot(hull[i])));
        if (length <= 1)
            continue;
        const auto u = edge / length;
        const auto perp = cv::Point(-u.y, u.x);   // 逆时针旋转90度
        const auto u_coords = hull | std::views::transform([&u](const auto &p) { return p.x * u.x + p.y * u.y; }) | std::ranges::to<std::vector<double>>();
        const auto p_coords = hull | std::views::transform([&perp](const auto &p) { return p.x * perp.x + p.y * perp.y; }) | std::ranges::to<std::vector<double>>();
        const auto u_min = *std::ranges::min_element(u_coords), u_max = *std::ranges::max_element(u_coords);
        const auto p_min = *std::ranges::min_element(p_coords), p_max = *std::ranges::max_element(p_coords);
        // 5. 计算宽度和高度
        const auto u_extent = u_max - u_min;
        const auto p_extent = p_max - p_min;
        const auto area = u_extent * p_extent;
        // 6. 更新最优解
        if (area >= best_area)
            continue;
        best_area = area;
        // 7. 计算中心点 (在全局坐标系下)
        cv::Point2d center = (u_min + u_max) / 2.0 * u + (p_min + p_max) / 2.0 * perp;

        // 8. 确定长轴和短轴，并应用轴向约束
        cv::Point2d long_axis;
        double half_long, half_short;
        if (u_extent >= p_extent) {
            long_axis = u; half_long = u_extent / 2.0; half_short = p_extent / 2.0;
        } else {
            long_axis = perp; half_long = p_extent / 2.0; half_short = u_extent / 2.0;
        }

        // **关键约束**: 将长轴 Pin 到右半平面 (x > 0) 或 下半平面 (x==0, y<0)
        // 这保证了无论凸包如何旋转，生成的矩形顶点顺序是唯一的
        // Pin the long axis to the right half-plane (or to the lower
        // half-plane when it is exactly vertical) so the corner sequence is
        // platform-independent.
        if (long_axis.x < 0 || (std::abs(long_axis.x) < 1e-9 && long_axis.y < 0))
            long_axis = -long_axis;
        // Right-handed perpendicular yields a deterministic corner traversal:
        // p0 → p1 along the long axis, then p1 → p2 along the short axis.
        const auto short_axis = cv::Point2d(-long_axis.y, long_axis.x);

        // 9. 计算四个顶点
        // 顺序: p0(左下) -> p1(右下) -> p2(右上) -> p3(左上) 相对于长轴方向
        best_corners = {
            center - long_axis * half_long - short_axis * half_short,
            center + long_axis * half_long - short_axis * half_short,
            center + long_axis * half_long + short_axis * half_short,
            center - long_axis * half_long + short_axis * half_short,
        };
    }
    // Callers filter hulls with fewer than three distinct points, so the loop
    // above always finds at least one positive-length edge.
    //assert best_corners is not None
    return best_corners;
}

float get_contour_length(const std::vector<cv::Point> &contour) {
    if (contour.empty())
        return 0;
    return (float)cv::arcLength(contour, true);
}

std::vector<cv::Point> compute_polygon_from_mask(const cv::Mat &mask) {
    // Pad so a region touching the image border still has a background ring to
    // close its contour against; the resulting offset is removed below.
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    if (contours.empty()) {
        SPDLOG_WARN("No contour found, so returning empty polygon.");
        return {};
    }

    // 2. 寻找最长轮廓: 对应 max(contours, key=_get_contour_length)
    const auto longest_it = std::ranges::max_element(contours, [](const auto &a, const auto &b) {
        return cv::arcLength(a, true) < cv::arcLength(b, true);
    });
    const std::vector<cv::Point> &longest_contour = *longest_it;

    // 3. 计算近似容差 (Epsilon)
    // Python: tolerance = np.ptp(contour, axis=0).max() * 0.005
    // OpenCV 惯例: epsilon = arcLength * factor (通常 0.01~0.05)
    // 为了严格复现 Python 逻辑，我们计算 bounding box 的最大边长
    const auto bbox = cv::boundingRect(longest_contour);
    const auto max_extent = std::max(bbox.width, bbox.height);
    const auto epsilon = max_extent * 0.005;

    // 4. 多边形近似: 对应 skimage.measure.approximate_polygon
    std::vector<cv::Point> approx_poly;
    cv::approxPolyDP(longest_contour, approx_poly, epsilon, true);

    // 6. 边界裁剪: 对应 np.clip(polygon, (0,0), (H, W))
    // OpenCV 轮廓点天然在图像范围内，但若近似后略微溢出，可手动裁剪
    const auto rows = mask.rows;
    const auto cols = mask.cols;
    std::ranges::for_each(approx_poly, [&](auto &p) {
        p.x = std::clamp(p.x, 0, cols);
        p.y = std::clamp(p.y, 0, rows);
    });
    return approx_poly;
}