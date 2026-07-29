#ifndef __INC_NUMPY_UTILS_H
#define __INC_NUMPY_UTILS_H

#include <vector>
#include <algorithm>
#include "opencv2/opencv.hpp"

namespace np {
// 返回元素的符号，即1（正数），0（零），或-1（负数）
template<typename T>
int32_t sign(const T &x) {
    return x > 0 ? 1 : x < 0 ? -1 : 0;
}

template<typename T>
T clip(T v, T lower, T upper) {
    return std::min(std::max(v, lower), upper);
}

template<typename T>
std::vector<T> clip(const std::vector<T> &v, T lower, T upper) {
    std::vector<T> clipped;
    clipped.reserve(v.size());
    std::ranges::transform(v, std::back_inserter(clipped), [lower, upper](const auto &x) { return clip(x, lower, upper); });
    return clipped;
}

template<typename T>
cv::Point_<T> clip(const cv::Point_<T> &v, const cv::Point_<T> &lower, const cv::Point_<T> &upper) {
    const auto x = std::min(std::max(v.x, lower.x), upper.x);
    const auto y = std::min(std::max(v.y, lower.y), upper.y);
    return cv::Point_<T>{x, y};
}

template<typename T>
std::vector<T> ptp(const std::vector<cv::Point_<T>> &contour) {
    T x_min = std::numeric_limits<T>::max(), x_max = std::numeric_limits<T>::min();
    T y_min = std::numeric_limits<T>::max(), y_max = std::numeric_limits<T>::min();
    std::ranges::for_each(contour, [&](auto &p) {
        if (p.x < x_min) { x_min = p.x; }
        if (p.x > x_max) { x_max = p.x; }
        if (p.y < y_min) { y_min = p.y; }
        if (p.y > y_max) { y_max = p.y; }
    });

    return std::vector<T>{x_max - x_min, y_max - y_min};
}

template<typename T>
T ptp_min(const std::vector<cv::Point_<T>> &coords) {
    auto x_min = std::numeric_limits<T>::max(), x_max = std::numeric_limits<T>::min();
    auto y_min = std::numeric_limits<T>::max(), y_max = std::numeric_limits<T>::min();
    std::ranges::for_each(coords, [&](const auto &p) {
        if (p.x < x_min) { x_min = p.x; }
        if (p.x > x_max) { x_max = p.x; }
        if (p.y < y_min) { y_min = p.y; }
        if (p.y > y_max) { y_max = p.y; }
    });
    return std::min(x_max - x_min, y_max - y_min);
}

template<typename T>
T ptp_max(const std::vector<cv::Point_<T>> &coords) {
    auto x_min = std::numeric_limits<T>::max(), x_max = std::numeric_limits<T>::min();
    auto y_min = std::numeric_limits<T>::max(), y_max = std::numeric_limits<T>::min();
    std::ranges::for_each(coords, [&](const auto &p) {
        if (p.x < x_min) { x_min = p.x; }
        if (p.x > x_max) { x_max = p.x; }
        if (p.y < y_min) { y_min = p.y; }
        if (p.y > y_max) { y_max = p.y; }
    });
    return std::max(x_max - x_min, y_max - y_min);
}

} //namespace np
#endif //__INC_NUMPY_UTILS_H