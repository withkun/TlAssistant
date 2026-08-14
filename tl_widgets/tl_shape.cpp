#include "tl_shape.h"

#include "common/base64.h"
#include "common/format_qt.h"
#include "common/np_utils.h"
#include "spdlog/spdlog.h"

#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QUuid>


static const QSet<QString> ShapeType{
    "polygon",
    "rectangle",
    "oriented_rectangle",
    "point",
    "line",
    "circle",
    "linestrip",
    "points",
    "mask",
};

static const QSet<QString> POLYLINE_SHAPE_TYPES{ "polygon", "linestrip" };


TlShape::TlShape(const QString &label,
                 const QString &shape_type,
                 const int32_t group_id,
                 const QString &description,
                 const cv::Mat &mask,
                 const QList<QPointF> &points,
                 const bool closed) {
    this->label_                = label;
    this->group_id_             = group_id;
    this->shape_type_           = shape_type;
    this->flags_                = {};
    this->description_          = description;
    this->mask_                 = mask;
    this->points_               = points;
    this->point_labels_         = {};
    this->other_data_           = {};
    this->closed_               = closed;
    this->visible_              = true;

    this->_post_init_();
}

TlShape::TlShape(const QString &shape_type, const QList<QPointF> &points, const QList<int32_t> &point_labels, const bool closed) {
    this->shape_type_           = shape_type;
    this->points_               = points;
    this->point_labels_         = point_labels;
    this->closed_               = closed;

    this->_post_init_();
}

void TlShape::_post_init_() {
    if (!ShapeType.contains(this->shape_type_))
        throw std::invalid_argument("Unexpected shape_type: " + this->shape_type_.toStdString());
    //self.points = np.asarray(self.points, dtype=np.float64).reshape(-1, 2)
    //self.point_labels = np.asarray(self.point_labels, dtype=np.int_).reshape(-1)
    if (this->point_labels_.empty() && !this->points_.empty())
        this->point_labels_.resize(this->points_.size(), 1);
    this->uuid_ = QUuid::createUuid().toString();
}

bool TlShape::can_add_point() const {
    return POLYLINE_SHAPE_TYPES.contains(shape_type_);
}

void TlShape::insert_point(int32_t i, const QPointF &point, int32_t label) {
    this->points_.insert(i, point);
    this->point_labels_.insert(i, label);
}

bool TlShape::can_remove_point() const {
    if (!this->can_add_point())
        return false;
    if (this->shape_type_ == "polygon" && this->points_.size() <= 3)
        return false;
    if (this->shape_type_ == "linestrip" && this->points_.size() <= 2)
        return false;
    return true;
}

void TlShape::remove_point(int32_t i) {
    if (!this->can_remove_point()) {
        SPDLOG_WARN(
            "Cannot remove point from: shape_type={}, len(points)={}",
            this->shape_type_,
            this->points_.size()
        );
        return;
    }
    this->points_.remove(i);
    this->point_labels_.remove(i);
}

void TlShape::move_vertex(const int32_t i, const QPointF &pos) {
    this->points_[i] = pos;
}

void TlShape::translate(const QPointF &offset) {
    std::ranges::for_each(this->points_, [&](auto &p) { p += offset; });
}

TlShape TlShape::copy() const {
    // 直接复制对象的全部内存内容, 生成完全相同的独立副本.
    return *this;
}

int32_t nearest_index_within_epsilon(
    const QList<double> &distances, const float epsilon
) {
    const auto nearest = np::argmin(distances);
    if (nearest != None && distances[nearest] > epsilon)
        return None;
    return nearest;
}

int32_t nearest_vertex_index(
    const TlShape &shape,
    const QPointF &point,
    const float scale,
    const float epsilon
) {
    QList<QPointF> points;
    if (QKey{"mask", "point"}.contains(shape.shape_type_) || shape.points_.empty())
        return None;
    std::ranges::for_each(shape.points_, [&](const QPointF &p) { points.push_back((p - point) * scale); });
    const auto distances = utils::distance(points);
    return nearest_index_within_epsilon(distances, epsilon);
}

int32_t nearest_edge_index(
    const TlShape &shape,
    const QPointF &point,
    const float scale,
    const float epsilon
) {
    if (shape.points_.empty())
        return None;
    const auto scaled_point = point * scale;    // 缩放后的目标点.
    const auto scaled_points = shape.points_ | std::views::transform([&](auto &p) { return p * scale; }) | std::ranges::to<QList<QPointF>>();
    const auto starts = np::roll(scaled_points, 1);
    const auto segments = std::views::zip(scaled_points, starts) | std::views::transform([](const auto &z) { return std::get<0>(z) - std::get<1>(z); }) | std::ranges::to<std::vector<QPointF>>();
    const auto length_squared = segments | std::views::transform([](const auto &p) { return p.x()*p.x()+p.y()*p.y(); }) | std::ranges::to<std::vector<float>>();
    const auto t = std::views::zip(starts, segments, length_squared) | std::views::transform([&scaled_point](const auto &z) {
        const auto &[v1, v2, v3] = z;
        return np::clip(((scaled_point-v1).x() * v2.x() + (scaled_point-v1).y() * v2.y()) / (v3 == 0 ? 1.0 : v3),
                        0.0,
                        1.0
    ); }) | std::ranges::to<std::vector<float>>();
    const auto projections = std::views::zip(starts, t, segments) | std::views::transform([](const auto &z) { return std::get<0>(z) + std::get<1>(z) * std::get<2>(z); }) | std::ranges::to<std::vector<QPointF>>();
    const auto distances = projections | std::views::transform([&scaled_point](auto &p) { return utils::distance(scaled_point - p); }) | std::ranges::to<QList<double>>();
    return nearest_index_within_epsilon(distances, epsilon);
}

int32_t nearest_rotation_point_index(
    const TlShape &shape,
    const QPointF &point,
    const float scale,
    const float epsilon
) {
    if (shape.shape_type_ != "oriented_rectangle" || shape.points_.size() != 4)
        return None;
    const auto handles = std::views::zip(shape.points_, np::roll(shape.points_, 1)) | std::views::transform([](const auto &p) { return (std::get<0>(p) + std::get<1>(p)) / 2; }) | std::ranges::to<QList<QPointF>>();
    const auto distances = handles | std::views::transform([&point, scale](const auto &p) { return utils::distance(p - point) * scale; }) | std::ranges::to<QList<double>>();
    return nearest_index_within_epsilon(distances, epsilon);
}

QPointF get_rotation_handle(const TlShape &shape, int32_t index) {
    if (shape.shape_type_ != "oriented_rectangle" || shape.points_.size() != 4)
        throw std::invalid_argument(
            std::format("Rotation handles are only defined for 4-point oriented rectangles, "
            "got shape_type={}, len(points)={}", shape.shape_type_.toStdString(), shape.points_.size())
        );
    return (shape.points_[index] + shape.points_[(index - 1 + 4) % 4]) / 2;
}

QPointF oriented_rectangle_center(const TlShape &shape) {
    if (shape.shape_type_ != "oriented_rectangle")
        throw std::invalid_argument(
            std::format("Center is only defined for oriented rectangles, got {}", shape.shape_type_.toStdString())
        );
    if (shape.points_.size() != 4)
        throw std::invalid_argument(
            std::format("Oriented rectangle center requires 4 points, got {}", shape.points_.size())
        );
    return (shape.points_[0] + shape.points_[2]) / 2;
}

constexpr float ARROW_HEAD_BACK_OFFSET = 0.22f;
constexpr float ARROW_HALF_LENGTH = 5.0f;
const QList<QPointF> ORIENTED_RECTANGLE_ARROW_TEMPLATE {
    QPointF(ARROW_HEAD_BACK_OFFSET, -0.5) * ARROW_HALF_LENGTH,
    QPointF(1.0,                     0.0) * ARROW_HALF_LENGTH,
    QPointF(ARROW_HEAD_BACK_OFFSET,  0.5) * ARROW_HALF_LENGTH,
    QPointF(-1.0,                    0.0) * ARROW_HALF_LENGTH,
};


QList<QPointF> oriented_rectangle_arrow_points(const TlShape &shape) {
    const auto center = oriented_rectangle_center(shape);
    const auto direction = shape.points_[1] - shape.points_[0];
    const auto angle = float(std::atan2(direction.y(), direction.x()));
    QList<QPointF> rotated_points = rotate_points_around_origin(
            ORIENTED_RECTANGLE_ARROW_TEMPLATE, angle
        );
    std::ranges::for_each(rotated_points, [&](auto &p) { p += center; });
    return rotated_points;
}

void rotate(
    TlShape &shape,
    const QPointF &center,
    const float angle,
    const QList<QPointF> &source_points
) {
    if (shape.shape_type_ != "oriented_rectangle")
        throw std::invalid_argument(
            "Shape rotation is only supported for oriented rectangles, "
            "got " + shape.shape_type_.toStdString()
        );
    auto points = source_points.empty() ? shape.points_ : source_points;
    if (points.size() != 4 || shape.points_.size() != 4)
        throw std::invalid_argument(
            std::format("Shape rotation requires 4 points, got "
                        "len(source_points)={}, len(shape.points)={}", points.size(), shape.points_.size())
        );
    std::ranges::for_each(points, [&](auto &p) { p -= center; });
    auto rotated = rotate_points_around_origin(points, angle);
    std::ranges::for_each(rotated, [&](auto &p) { p += center; });
    shape.points_ = rotated;
}

QList<QPointF> rotate_points_around_origin(
    const QList<QPointF> &points,
    const float angle
) {
    const auto cos_a = std::cos(angle);
    const auto sin_a = std::sin(angle);
    const auto rotation = points | std::views::transform([&](auto &p) { return QPointF(p.x() * cos_a - p.y() * sin_a, p.x() * sin_a + p.y() * cos_a); }) | std::ranges::to<QList<QPointF>>();
    return rotation;
}

TlShape TlShape::clone() const {
    // 直接复制对象的全部内存内容, 生成标签不同的独立副本.
    TlShape shape = *this;
    shape.uuid_ = QUuid::createUuid().toString();
    return shape;
}

TlShape::TlShape(const TlShape &shape) {
    this->set_value(shape);
}

void TlShape::set_value(const TlShape &shape) {
    this->label_                      = shape.label_;
    this->group_id_                   = shape.group_id_;
    this->points_                     = shape.points_;
    this->point_labels_               = shape.point_labels_;
    this->shape_type_                 = shape.shape_type_;
    this->flags_                      = shape.flags_;
    this->description_                = shape.description_;
    this->other_data_                 = shape.other_data_;
    this->mask_                       = shape.mask_;
    this->closed_                     = shape.closed_;

    this->uuid_                       = shape.uuid_;
}