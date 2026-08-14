#ifndef __INC_FORMAT_CV_H
#define __INC_FORMAT_CV_H

#include "spdlog/spdlog.h"
#include "opencv2/opencv.hpp"


namespace std {
template<>
struct formatter<cv::Mat> : std::formatter<std::string> {
    auto format(const cv::Mat &val, std::format_context &ctx) const -> decltype(ctx.out()) {
        const std::string str = std::format("[{}x{},{}]", val.cols, val.rows, reinterpret_cast<const void *>(&val));
        return std::formatter<std::string>::format(str, ctx);
    }
};

template<>
struct formatter<cv::Range> {
    formatter<int> formatter_;
    template <typename FormatContext>
    auto format(cv::Range const &val, FormatContext &ctx) const -> decltype(ctx.out()) {
        formatter<std::string> formatter;
        formatter.format("[", ctx);
        formatter_.format(val.start, ctx);
        formatter.format(",", ctx);
        formatter_.format(val.end, ctx);
        formatter.format("]", ctx);
        return ctx.out();
    }

    template <typename ParseContext>
    auto parse(ParseContext &ctx) -> decltype(ctx.begin()) {
        return formatter_.parse(ctx);
    }
};

template<typename T>
struct formatter<cv::Rect_<T>> {
    formatter<T> formatter_;
    template <typename FormatContext>
    auto format(cv::Rect_<T> const &val, FormatContext &ctx) const -> decltype(ctx.out()) {
        formatter<std::string> formatter;
        formatter.format("[", ctx);
        formatter_.format(val.x, ctx);
        formatter.format(",", ctx);
        formatter_.format(val.y, ctx);
        formatter.format(",", ctx);
        formatter_.format(val.width, ctx);
        formatter.format(",", ctx);
        formatter_.format(val.height, ctx);
        formatter.format("]", ctx);
        return ctx.out();
    }

    template <typename ParseContext>
    auto parse(ParseContext &ctx) -> decltype(ctx.begin()) {
        return formatter_.parse(ctx);
    }
};

template<typename T>
struct formatter<cv::Point_<T>> {
    formatter<T> formatter_;
    template <typename FormatContext>
    auto format(cv::Point_<T> const &val, FormatContext &ctx) const -> decltype(ctx.out()) {
        formatter<std::string> formatter;
        formatter.format("[", ctx);
        formatter_.format(val.x, ctx);
        formatter.format(",", ctx);
        formatter_.format(val.y, ctx);
        formatter.format("]", ctx);
        return ctx.out();
    }

    template <typename ParseContext>
    auto parse(ParseContext &ctx) -> decltype(ctx.begin()) {
        return formatter_.parse(ctx);
    }
};

template<typename T>
struct formatter<cv::Size_<T>> {
    formatter<T> formatter_;
    template <typename FormatContext>
    auto format(const cv::Size_<T> val, FormatContext &ctx) const -> decltype(ctx.out()) {
        formatter<std::string> formatter;
        formatter.format("[", ctx);
        formatter_.format(val.width, ctx);
        formatter.format(",", ctx);
        formatter_.format(val.height, ctx);
        formatter.format("]", ctx);
        return ctx.out();
    }

    template <typename ParseContext>
    auto parse(ParseContext &ctx) -> decltype(ctx.begin()) {
        return formatter_.parse(ctx);
    }
};

template<>
struct formatter<cv::Size2i> : formatter<string_view> {
    auto format(const cv::Size2i &val, format_context &ctx) const -> decltype(ctx.out()) {
        const auto str = std::format("[{}, {}]", val.width, val.height);
        return formatter<string_view>::format(str, ctx);
    }
};

// template<>
// struct formatter<cv::Size2l> : formatter<string_view> {
//     auto format(const cv::Size2l &val, format_context &ctx) const -> decltype(ctx.out()) {
//         const auto str = std::format("[{}, {}]", val.width, val.height);
//         return formatter<string_view>::format(str, ctx);
//     }
// };
//
// template<>
// struct formatter<cv::Size2f> : formatter<string_view> {
//     auto format(const cv::Size2f &val, format_context &ctx) const -> decltype(ctx.out()) {
//         const auto str = std::format("[{:.3f}, {:.3f}]", val.width, val.height);
//         return formatter<string_view>::format(str, ctx);
//     }
// };
//
// template<>
// struct formatter<cv::Size2d> : formatter<string_view> {
//     auto format(const cv::Size2d &val, format_context &ctx) const -> decltype(ctx.out()) {
//         const auto str = std::format("[{:.3f}, {:.3f}]", val.width, val.height);
//         return formatter<string_view>::format(str, ctx);
//     }
// };
//
// template<>
// struct formatter<cv::Point2i> : formatter<string_view> {
//     auto format(const cv::Point2i &val, format_context &ctx) const -> decltype(ctx.out()) {
//         const auto str = std::format("[{}, {}]", val.x, val.y);
//         return formatter<string_view>::format(str, ctx);
//     }
// };
//
// template<>
// struct formatter<cv::Point2l> : formatter<string_view> {
//     auto format(const cv::Point2l &val, format_context &ctx) const -> decltype(ctx.out()) {
//         const auto str = std::format("[{}, {}]", val.x, val.y);
//         return formatter<string_view>::format(str, ctx);
//     }
// };
//
// template<>
// struct formatter<cv::Point2f> : formatter<string_view> {
//     auto format(const cv::Point2f &val, format_context &ctx) const -> decltype(ctx.out()) {
//         const auto str = std::format("[{:.3f}, {:.3f}]", val.x, val.y);
//         return formatter<string_view>::format(str, ctx);
//     }
// };
//
// template<>
// struct formatter<cv::Point2d> : formatter<string_view> {
//     auto format(const cv::Point2d &val, format_context &ctx) const -> decltype(ctx.out()) {
//         const auto str = std::format("[{:.3f}, {:.3f}]", val.x, val.y);
//         return formatter<string_view>::format(str, ctx);
//     }
// };

template<>
struct formatter<cv::MatShape> : formatter<string_view> {
    auto format(const cv::MatShape &val, format_context &ctx) const -> decltype(ctx.out()) {
        std::stringstream ss;
        const cv::Size size = val();
        ss << "[" << size.width << ", " << size.height << "]";
        return formatter<string_view>::format(ss.str(), ctx);
    }
};

template<>
struct formatter<cv::MatStep> : std::formatter<std::string> {
    auto format(const cv::MatStep &val, std::format_context &ctx) const -> decltype(ctx.out()) {
        std::stringstream ss;
        ss << "[";
        for (auto i = 0; i < cv::MatShape::MAX_DIMS; ++i) {
            if (val[i] == 0) { break; }
            if (i != 0) { ss << ", "; }
            ss << val[i];
        }
        ss << "]";
        return std::formatter<std::string>::format(ss.str(), ctx);
    }
};

//inline std::ostream &operator<<(std::ostream &os, const std::vector<int64_t> &v) {
//    os << "[";
//    for (int i = 0; i < v.size(); ++i) {
//        os << v[i];
//        if (i != v.size() - 1) {
//            os << ", ";
//        }
//    }
//    os << "]";
//    return os;
//}

template<typename T>
struct formatter<std::vector<T>> : formatter<string_view> {
    auto format(const std::vector<T> &val, format_context &ctx) const -> decltype(ctx.out()) {
        std::stringstream ss;
        ss << "[";
        for (size_t i = 0; i < val.size(); ++i) {
            if (i != 0) { ss << ", "; }
            ss << val[i];
        }
        ss << "]";
        return formatter<string_view>::format(ss.str(), ctx);
    }
};

//template<>
//struct formatter<std::vector<int64_t>> : formatter<string_view> {
//    auto format(const std::vector<int64_t> &val, format_context &ctx) const -> decltype(ctx.out()) {
//        std::stringstream ss;
//        for (size_t i = 0; i < val.size(); ++i) {
//            if (i != 0) { ss << ", "; }
//            ss << val[i];
//        }
//        ss << "]";
//        return formatter<string_view>::format(ss.str(), ctx);
//    }
//};

} //namespace std
#endif //__INC_FORMAT_CV_H