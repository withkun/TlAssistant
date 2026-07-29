#include "polygon_from_mask.h"
#include "common/numpy_utils.h"

#include <stack>


namespace measure {
// tolerance: 表示原始轮廓点到近似多边形的最大允许距离‌(单位与输入坐标一致)
std::vector<cv::Point2d> approximate_polygon(const std::vector<cv::Point2d> &coords, double tolerance) {
    if (tolerance <= 0) {
        return coords;
    }

    auto chain = std::vector<bool>(coords.size(), false);
    // pre-allocate distance array for all points
    auto dists = std::vector<double>(coords.size(), 0.0);
    chain[0] = true;
    chain[coords.size() - 1] = true;
    auto pos_stack = std::stack<std::pair<size_t, size_t>>{};
    pos_stack.emplace(0, coords.size() - 1);

    while (!pos_stack.empty()) {
        auto [s_idx, e_idx] = pos_stack.top();
        pos_stack.pop();

        // determine properties of current line segment
        const double r0 = coords[s_idx].x, c0 = coords[s_idx].y;  // 起点坐标 (行, 列)
        const double r1 = coords[e_idx].x, c1 = coords[e_idx].y;  // 终点坐标 (行, 列)
        const double dr = r1 - r0;                                // 线段在行方向的增量
        const double dc = c1 - c0;                                // 线段在列方向的增量
        const double segment_angle = -std::atan2(dr, dc);         // 线段的法线方向与截距(距离原点的垂直距离)
        const double segment_dist  = c0 * std::sin(segment_angle) + r0 * std::cos(segment_angle);

        // select points in-between line segment
        double max_dist = 0.0;
        size_t max_idx = std::numeric_limits<size_t>::min();
        for (size_t i = s_idx + 1; i < e_idx; ++i) {
            const cv::Point2d &p = coords[i];
            const double dr0 = p.x - r0, dc0 = p.y - c0;            // 起点向量(行, 列)
            const double dr1 = p.x - r1, dc1 = p.y - c1;            // 终点向量(行, 列)
            const double projected_length0 =  dr0 * dr + dc0 * dc;  // 向量 p->r0 投影到 r0->r1
            const double projected_length1 = -dr1 * dr - dc1 * dc;  // 向量 p->r1 投影到 r0->r1
            if (projected_length0 > 0 && projected_length1 > 0) {
                // 垂足落在线段内部, 使用垂直距离.
                dists[i] = std::abs(p.x * std::cos(segment_angle) + p.y * std::sin(segment_angle) - segment_dist);
            } else {
                // 垂足落在线段外部, 取到端点的欧氏距离.
                dists[i] = std::min(std::sqrt(dr0 * dr0 + dc0 * dc0), std::sqrt(dr1 * dr1 + dc1 * dc1));
            }

            if (dists[i] > max_dist) {
                max_dist = dists[i];
                max_idx = i;
            }
        }

        if (max_dist > tolerance) {
            // select point with maximum distance to line
            pos_stack.emplace(s_idx, max_idx);
            pos_stack.emplace(max_idx, e_idx);
            chain[max_idx] = true;
        }
    }

    std::vector<cv::Point2d> polygon;
    for (size_t i = 0; i < coords.size(); ++i) {
        if (chain[i]) {
            polygon.push_back(coords[i]);
        }
    }
    return polygon;
}

std::vector<cv::Point2d> compute_polygon_from_mask(const cv::Mat &mask) {
    // 查找轮廓
    std::vector<std::vector<cv::Point2i>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) {
        return {};
    }

    // 寻找最长的轮廓
    std::vector<cv::Point2d> coords;
    const size_t max_len_idx = std::distance(contours.begin(), std::ranges::max_element(contours, [](auto &a, auto &b) { return a.size() < b.size(); }));
    std::ranges::transform(contours[max_len_idx], std::back_inserter(coords), [](auto &p) { return p; });
    while (coords.size() > 2 && coords.front() == coords.back()) {
        coords.pop_back();
    }

    //constexpr float POLYGON_APPROX_TOLERANCE = 0.004;
    constexpr float POLYGON_APPROX_TOLERANCE = 0.007;
    const double ptp = np::ptp_max(coords);
    const double tolerance = ptp * POLYGON_APPROX_TOLERANCE;

    std::vector<cv::Point2d> polygon = approximate_polygon(coords, tolerance);

    const cv::Point2d lower(0, 0);
    const cv::Point2d upper(mask.cols, mask.rows);
    std::ranges::transform(polygon, polygon.begin(), [&](const auto &p) {
        return np::clip(p, lower, upper);
    });

    // drop last point that if duplicate with first point
    while (polygon.size() > 2 && polygon.front() == polygon.back()) {
        polygon.pop_back();
    }

    return polygon;
}
} //namespace measure