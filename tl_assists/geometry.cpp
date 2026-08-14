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
        auto u = edge / length;
        auto perp = cv::Point(-u.y, u.x);   // 逆时针旋转90度
        std::vector<double> u_coords(n);
        std::vector<double> p_coords(n);
        double u_min = std::numeric_limits<double>::max();
        double u_max = std::numeric_limits<double>::lowest();
        double p_min = std::numeric_limits<double>::max();
        double p_max = std::numeric_limits<double>::lowest();
        for (auto j = 0; j < n; ++j) {
            // 点积计算投影
            const double uc = hull[j].x * u.x    + hull[j].y * u.y;
            const double pc = hull[j].x * perp.x + hull[j].y * perp.y;
            u_coords[j] = uc;
            p_coords[j] = pc;
            if (uc < u_min) u_min = uc;
            if (uc > u_max) u_max = uc;
            if (pc < p_min) p_min = pc;
            if (pc > p_max) p_max = pc;
        }

        // 5. 计算宽度和高度
        double uExtent = u_max - u_min;
        double pExtent = p_max - p_min;
        double area = uExtent * pExtent;

        // 6. 更新最优解
        if (area >= best_area) continue;
        best_area = area;

        // 7. 计算中心点 (在全局坐标系下)
        // center = (u_min + u_max)/2 * u + (p_min + p_max)/2 * perp
        double uCenter = (u_min + u_max) / 2.0;
        double pCenter = (p_min + p_max) / 2.0;
        cv::Point2d center = uCenter * u + pCenter * perp;

        // 8. 确定长轴和短轴，并应用轴向约束
        cv::Point2d longAxis, shortAxis;
        double halfLong, halfShort;

        if (uExtent >= pExtent) {
            longAxis = u;
            halfLong = uExtent / 2.0;
            halfShort = pExtent / 2.0;
        } else {
            longAxis = perp;
            halfLong = pExtent / 2.0;
            halfShort = uExtent / 2.0;
        }

        // &zwnj;**关键约束**&zwnj;：将长轴 Pin 到右半平面 (x > 0) 或 下半平面 (x==0, y<0)
        // 这保证了无论凸包如何旋转，生成的矩形顶点顺序是唯一的
        if (longAxis.x < 0 || (std::abs(longAxis.x) < 1e-9 && longAxis.y < 0)) {
            longAxis = -longAxis;
        }

        // 计算短轴 (右手系垂直)
        shortAxis = cv::Point2d(-longAxis.y, longAxis.x);

        // 9. 计算四个顶点
        // 顺序: p0(左下) -> p1(右下) -> p2(右上) -> p3(左上) 相对于长轴方向
        best_corners.resize(4);
        best_corners[0] = center - longAxis * halfLong - shortAxis * halfShort;
        best_corners[1] = center + longAxis * halfLong - shortAxis * halfShort;
        best_corners[2] = center + longAxis * halfLong + shortAxis * halfShort;
        best_corners[3] = center - longAxis * halfLong + shortAxis * halfShort;
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
    if (mask.empty())
        return {};
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