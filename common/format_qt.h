#ifndef __INC_FORMAT_QT_H
#define __INC_FORMAT_QT_H

#include <QSize>
#include <QSizeF>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QColor>

#include "spdlog/spdlog.h"

inline std::ostream &operator<<(std::ostream &out, const QString &val) {
    return out << val.toStdString();
}

namespace std {
template<>
struct formatter<QString> : formatter<string_view> {
    auto format(const QString &val, format_context &ctx) const -> decltype(ctx.out()) {
        return formatter<string_view>::format(val.toStdString(), ctx);
    }
};

template<typename Point, typename Type>
struct formatterPoint : formatter<string_view> {
    auto format(const Point &val, format_context &ctx) const -> decltype(ctx.out()) {
        std::stringstream ss;
        ss << "[" << val.x() << ", " << val.y() << "]";
        return formatter<string_view>::format(ss.str(), ctx);
    }
};
template<> struct formatter<QPoint> : formatterPoint<QPoint, int> {};
template<> struct formatter<QPointF> : formatterPoint<QPointF, qreal> {};

template<typename Size, typename Type>
struct formatterSize : formatter<string_view> {
    auto format(const Size &val, format_context &ctx) const -> decltype(ctx.out()) {
        std::stringstream ss;
        ss << "[" << val.width() << ", " << val.height() << "]";
        return formatter<string_view>::format(ss.str(), ctx);
    }
};
template<> struct formatter<QSize> : formatterSize<QSize, int> {};
template<> struct formatter<QSizeF> : formatterSize<QSizeF, qreal> {};

template<typename Rect, typename Type>
struct formatterRect : formatter<string_view> {
    auto format(const Rect &val, format_context &ctx) const -> decltype(ctx.out()) {
        std::stringstream ss;
        ss << "[" << val.x() << ", " << val.y() << ", " << val.width() << ", " << val.height() << "]";
        return formatter<string_view>::format(ss.str(), ctx);
    }
};
template<> struct formatter<QRect> : formatterRect<QRect, int> {};
template<> struct formatter<QRectF> : formatterRect<QRectF, qreal> {};

template<>
struct formatter<QColor> : formatter<string_view> {
    auto format(const QColor &val, format_context &ctx) const -> decltype(ctx.out()) {
        std::stringstream ss;
        ss << "[" << val.red() << ", " << val.green() << ", " << val.blue() << "]";
        return formatter<string_view>::format(ss.str(), ctx);
    }
};
} //namespace std
#endif //__INC_FORMAT_QT_H