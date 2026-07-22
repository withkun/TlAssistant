#include "tl_shape.h"

#include "base64.h"
#include "numpy_utils.h"
#include "spdlog/spdlog.h"
#include "base/format_qt.h"

#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QUuid>


// 类变量  ->  成员变量
// 对类变量的修改会影响所有实例(除非在方法中修改为局部变量), 而对实例变量的修改只影响该特定实例
// The following class variables influence the drawing of all shape objects.
QColor TlShape::line_color              = QColor(0, 255, 0, 128);
QColor TlShape::fill_color              = QColor(0, 0, 0, 64);
QColor TlShape::vertex_fill_color       = QColor(0, 255, 0, 255);
QColor TlShape::select_line_color       = QColor(255, 255, 255, 255);
QColor TlShape::select_fill_color       = QColor(0, 255, 0, 64);
QColor TlShape::hvertex_fill_color      = QColor(255, 255, 255, 255);

// Default handle style, size, and zoom scale
int32_t TlShape::point_type_            = P_ROUND;
int32_t TlShape::point_size_            = 8;
float   TlShape::scale_                 = 1.0;

QColor TlShape::current_vertex_fill_color;

TlShape::TlShape(const QString &label,
                 const QColor &line_color,
                 const QString &shape_type,
                 const QMap<QString, bool> &flags,
                 int32_t group_id,
                 const QString &description,
                 const cv::Mat &mask) {
    this->label_                      = label;
    this->group_id_                   = group_id;
    this->points_                     = {};
    this->point_labels_               = {};
    this->shape_type                  (shape_type);
    this->shape_raw_                  ;
    this->points_raw_                 ;
    this->shape_type_raw_             ;
    this->fill_                       = false;
    this->selected_                   = false;
    this->flags_                      = flags;
    this->description_                = description;
    this->other_data_                 = {};
    this->mask_                       = mask;

    this->highlightIndex_             = None;
    this->highlightMode_              = NEAR_VERTEX;
    this->highlight_sizes_            = { {NEAR_VERTEX, 4}, {MOVE_VERTEX, 1.5} };
    this->highlight_shapes_           = {
        { NEAR_VERTEX, P_ROUND },
        { MOVE_VERTEX, P_SQUARE }
    };

    this->closed_                     = false;

    // Per-instance line color override (used for the pending line).
    this->line_color_                 = line_color;
    this->fill_color_                 = TlShape::fill_color;
    this->select_line_color_          = TlShape::select_line_color;
    this->select_fill_color_          = TlShape::select_fill_color;
    this->vertex_fill_color_          = TlShape::vertex_fill_color;
    this->hvertex_fill_color_         = TlShape::hvertex_fill_color;
    this->current_vertex_fill_color_  = TlShape::current_vertex_fill_color;

    this->uuid_                       = QUuid::createUuid().toString();
}

QPointF TlShape::scale_point(const QPointF &point) const {
    // 展示缩放: 这里需要使用Canvas设置的全局变量, 其余计算使用局部变量始终保持为1.
    return { point.x() * TlShape::scale_, point.y() * TlShape::scale_ };
}

void TlShape::setShapeRefined(
    const QString &shape_type,
    const QList<QPointF> &points,
    const QList<int32_t> &point_labels,
    const cv::Mat &mask) {
    this->shape_raw_      = std::tie(this->shape_type_, this->points_, this->point_labels_);
    this->shape_type      (shape_type);
    this->points_         = points;
    this->point_labels_   = point_labels;
    this->mask_           = mask;
    this->close();  //  Closed for AI shape.
}

void TlShape::restoreShapeRaw() {
    if (std::get<1>(this->shape_raw_).empty()) {
        return;
    }
    this->shape_type    (std::get<0>(shape_raw_));
    this->points_       = std::get<1>(shape_raw_);
    this->point_labels_ = std::get<2>(shape_raw_);
    std::get<1>(this->shape_raw_).clear();
}

//@property
QString TlShape::shape_type() const {
    return this->shape_type_;
}

//@shape_type.setter
void TlShape::shape_type(QString value) {
    if (value.isEmpty()) {
        value = "polygon";
    }
    if (!QKey{"polygon",
              "rectangle",
              "oriented_rectangle",
              "point",
              "line",
              "circle",
              "linestrip",
              "points",
              "mask",
              }.contains(value)) {
        throw std::invalid_argument("Unexpected shape_type: " + value.toStdString());
    }
    this->shape_type_ = value;
}

void TlShape::close() {
    this->closed_ = true;
}

void TlShape::addPoint(const QPointF &point, int32_t label) {
    if (!this->points_.empty() && this->points_[0] == point) {
        this->close();
    } else {
        this->points_.append(point);
        this->point_labels_.append(label);
    }
}

bool TlShape::canAddPoint() const {
    return QKey{"polygon", "linestrip"}.contains(this->shape_type_);
}

QPointF TlShape::popPoint() {
    if (!this->points_.empty()) {
        if (!this->point_labels_.empty()) {
            this->point_labels_.pop_back();
        }
        auto p = this->points_.back();
        this->points_.pop_back();
        return p;
    }
    return QPointF();
}

void TlShape::insertPoint(int32_t i, const QPointF &point, int32_t label) {
    this->points_.insert(i, point);
    this->point_labels_.insert(i, label);
}

bool TlShape::canRemovePoint() const {
    if (!this->canAddPoint()) {
        return false;
    }

    if (this->shape_type_ == "polygon" && this->points_.size() <= 3) {
        return false;
    }

    if (this->shape_type_ == "linestrip" && this->points_.size() <= 2) {
        return false;
    }

    return true;
}

void TlShape::removePoint(int32_t i) {
    if (!this->canRemovePoint()) {
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

bool TlShape::isClosed() const {
    return this->closed_;
}

void TlShape::setOpen() {
    this->closed_ = false;
}

void TlShape::paint(QPainter &painter) {
    if (this->mask_.empty() && this->points_.empty()) {
        return;
    }

    auto color = this->selected_ ? this->select_line_color_ : this->line_color_;
    auto pen = QPen(color);
    // Try using integer sizes for smoother drawing(?)
    pen.setWidth(this->PEN_WIDTH);
    painter.setPen(pen);

    if (this->shape_type_ == "mask" && !mask_.empty()) {
        cv::Mat image_to_draw = cv::Mat::zeros({mask_.cols, mask_.rows}, CV_8UC4);
        int32_t r, g, b, a;
        if (selected_) {
            this->select_fill_color_.getRgb(&r, &g, &b, &a);
        } else {
            this->fill_color_.getRgb(&r, &g, &b, &a);
        }
        image_to_draw.setTo(cv::Scalar(r, g, b, a), mask_);
        auto qimage = QImage::fromData(utils::img_arr_to_data(image_to_draw));
        qimage = qimage.scaled(
            qimage.size() * scale_,
            Qt::IgnoreAspectRatio,
            Qt::SmoothTransformation
        );

        painter.drawImage(this->scale_point(this->points_[0]), qimage);

        auto line_path = QPainterPath();
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(mask_, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        for (auto contour : contours) {
            auto [x, y] = points_[0];
            line_path.moveTo(
                this->scale_point(QPointF(contour[0].x+x, contour[0].y+y))
            );
            for (auto idx = 1; idx < contour.size(); ++idx) {
                line_path.lineTo(
                    this->scale_point(QPointF(contour[idx].x+x, contour[idx].y+y))
                );
            }
        }
        painter.drawPath(line_path);
    }

    if (!this->points_.empty()) {
        auto line_path = QPainterPath();
        auto vrtx_path = QPainterPath();
        auto negative_vrtx_path = QPainterPath();

        if (QKey{"rectangle", "mask"}.contains(this->shape_type_)) {
            assert(this->points_.size() == 1 || this->points_.size() == 2);
            if (this->points_.size() == 2) {
                auto rectangle = QRectF(
                    this->scale_point(this->points_[0]),
                    this->scale_point(this->points_[1])
                );
                line_path.addRect(rectangle);
            }
            if (this->shape_type_ == "rectangle") {
                for (auto i = 0; i < points_.size(); ++i) {
                    this->drawVertex(vrtx_path, i);
                }
            }
        } else if (this->shape_type_ == "circle") {
            assert(points_.size() == 1 || points_.size() == 2);
            if (points_.size() == 2) {
                auto radius = utils::distance(
                    this->scale_point(this->points_[0] - this->points_[1])
                );
                line_path.addEllipse(
                    this->scale_point(this->points_[0]), radius, radius
                );
            }
            for (auto i = 0; i < points_.size(); ++i) {
                this->drawVertex(vrtx_path, i);
            }
        } else if (this->shape_type_ == "linestrip") {
            line_path.moveTo(this->scale_point(points_[0]));
            for (auto i = 0; i < points_.size(); ++i) {
                line_path.lineTo(this->scale_point(points_[i]));
                this->drawVertex(vrtx_path, i);
            }
        } else if (this->shape_type_ == "points") {
            assert(points_.size() == point_labels_.size());
            for (auto i = 0; i < point_labels_.size(); ++i) {
                if (point_labels_[i] == 1) {
                    this->drawVertex(vrtx_path, i);
                } else {
                    this->drawVertex(negative_vrtx_path, i);
                }
            }
        } else {
            line_path.moveTo(this->scale_point(this->points_[0]));
            // Uncommenting the following line will draw 2 paths
            // for the 1st vertex, and make it non-filled, which
            // may be desirable.
            // self.drawVertex(vrtx_path, 0)

            for (auto i = 0; i < points_.size(); ++i) {
                line_path.lineTo(scale_point(points_[i]));
                this->drawVertex(vrtx_path, i);
            }
            if (this->isClosed()) {
                line_path.lineTo(scale_point(points_[0]));
            }
        }
        painter.drawPath(line_path);
        if (vrtx_path.length() > 0) {
            painter.drawPath(vrtx_path);
            painter.fillPath(vrtx_path, current_vertex_fill_color_);
        }
        if (fill_ && !QKey{"line",
                           "linestrip",
                           "points",
                           "mask",
                           }.contains(this->shape_type_)) {
            color = selected_ ? select_fill_color_ : fill_color_;
            painter.fillPath(line_path, color);
        }

        pen.setColor(QColor(255, 0, 0, 255));
        painter.setPen(pen);
        painter.drawPath(negative_vrtx_path);
        painter.fillPath(negative_vrtx_path, QColor(255, 0, 0, 255));
    }
}

void TlShape::drawVertex(QPainterPath &path, int32_t i) {
    double d = point_size_;
    auto vertex_shape = point_type_;
    const auto pos  = scale_point(points_[i]);

    bool is_highlighted = highlightIndex_ != None && highlightIndex_ == i;
    if (is_highlighted) {
        d *= highlight_sizes_[highlightMode_];
        vertex_shape = highlight_shapes_[highlightMode_];
    }
    current_vertex_fill_color_ = (
        this->highlightIndex_ != None ? hvertex_fill_color_ : vertex_fill_color_
    );

    double half = d / 2.0;
    if (vertex_shape == P_SQUARE) {
        path.addRect(pos .x() - half, pos .y() - half, d, d);
    } else if (vertex_shape == P_ROUND) {
        path.addEllipse(pos , half, half);
    } else {
        throw std::invalid_argument("unsupported vertex shape");
    }
}

int32_t TlShape::nearestVertex(QPointF point, float epsilon) const {
    auto min_distance = std::numeric_limits<double>::max();
    int32_t min_i = None;
    point = QPointF(point.x() * scale_, point.y() * scale_);
    for (auto i = 0; i < points_.size(); ++i) {
        auto p = QPointF(points_[i].x() * scale_, points_[i].y() * scale_);
        auto dist = utils::distance(p - point);
        if ((dist <= epsilon) && (dist < min_distance)) {
            min_distance = dist;
            min_i = i;
        }
    }
    return min_i;
}

int32_t TlShape::nearestEdge(QPointF point, float epsilon) const {
    auto min_distance = std::numeric_limits<double>::max();
    auto post_i = None;
    point = scale_point(point);
    for (auto i = 0; i < points_.size(); ++i) {
        auto start = scale_point((i > 0) ? points_[i - 1] : points_[points_.size() - 1]);
        auto end = scale_point(points_[i]);
        auto line = QLineF{start, end};
        auto dist = utils::distanceToLine(point, line);
        if (dist <= epsilon && dist < min_distance) {
            min_distance = dist;
            post_i = i;
        }
    }
    return post_i;
}

bool TlShape::containsPoint(QPointF point) {
    if (QKey{"line", "linestrip", "points"}.contains(this->shape_type_)) {
        return false;
    }
    if (this->shape_type_ == "point") {
        if (this->points_.empty())
            return false;
        return utils::distance(point - this->points_[0]) <= this->point_size_ / 2.0;
    }
    if (!this->mask_.empty()) {
        const int32_t raw_y = static_cast<int32_t>(round(point.y() - this->points_[0].y()));
        const int32_t raw_x = static_cast<int32_t>(round(point.x() - this->points_[0].x()));
        if (
            raw_y < 0
            || raw_y >= this->mask_.rows
            || raw_x < 0
            || raw_x >= this->mask_.cols
        )
            return false;
        return mask_.at<bool>(raw_y, raw_x);
    }
    return makePath().contains(point);
}

QPainterPath TlShape::makePath() const {
    QPainterPath path;
    if (QKey{"rectangle", "mask"}.contains(this->shape_type_)) {
        path = QPainterPath();
        if (points_.size() == 2) {
            path.addRect(QRectF(points_[0], points_[1]));
        }
    } else if (this->shape_type_ == "circle") {
        path = QPainterPath();
        if (points_.size() == 2) {
            auto radius = utils::distance(points_[0] - points_[1]);
            path.addEllipse(points_[0], radius, radius);
        }
    } else {
        path = QPainterPath(points_[0]);
        for (auto i = 1; i < points_.size(); ++i) {
            path.lineTo(points_[i]);
        }
    }
    return path;
}

QRectF TlShape::boundingRect() const {
    return makePath().boundingRect();
}

void TlShape::moveBy(const QPointF &offset) {
    for (auto &p : points_) { p += offset; }
}

void TlShape::moveVertex(int32_t i, const QPointF &pos) {
    points_[i] = pos;
}

void TlShape::highlightVertex(int32_t i, int32_t action) {
    highlightIndex_ = i;
    highlightMode_ = action;
}

void TlShape::highlightClear() {
    highlightIndex_ = None;
}

TlShape TlShape::copy() const {
    return *this;
}

int32_t TlShape::size() const {
    return static_cast<int32_t>(points_.size());
}

//def __getitem__(self, key):
//    return self.points[key]

//def __setitem__(self, key, value):
//    self.points[key] = value

QString TlShape::key() const {
    return this->uuid_;
}

TlShape TlShape::clone() const {
    TlShape shape = *this;
    shape.uuid_ = QUuid::createUuid().toString();
    return shape;
}

TlShape::TlShape(const TlShape &shape) {
    this->SetValue(shape);
}

void TlShape::SetValue(const TlShape &shape) {
    this->label_                      = shape.label_;
    this->group_id_                   = shape.group_id_;
    this->points_                     = shape.points_;
    this->point_labels_               = shape.point_labels_;
    this->shape_type_                 = shape.shape_type_;
    this->shape_raw_                  = shape.shape_raw_;
    this->points_raw_                 = shape.points_raw_;
    this->shape_type_raw_             = shape.shape_type_raw_;
    this->fill_                       = shape.fill_;
    this->selected_                   = shape.selected_;
    this->flags_                      = shape.flags_;
    this->description_                = shape.description_;
    this->other_data_                 = shape.other_data_;
    this->mask_                       = shape.mask_;
    this->highlightIndex_             = shape.highlightIndex_;
    this->highlightMode_              = shape.highlightMode_;
    this->highlight_shapes_           = shape.highlight_shapes_;
    this->closed_                     = shape.closed_;

    this->line_color_                 = shape.line_color_;
    this->fill_color_                 = shape.fill_color_;
    this->select_line_color_          = shape.select_line_color_;
    this->select_fill_color_          = shape.select_fill_color_;
    this->vertex_fill_color_          = shape.vertex_fill_color_;
    this->hvertex_fill_color_         = shape.hvertex_fill_color_;
    //this->point_type_                 = shape.point_type_;
    //this->point_size_                 = shape.point_size_;
    //this->scale_                      = shape.scale_;
    this->current_vertex_fill_color_  = shape.current_vertex_fill_color_;
    this->uuid_                       = shape.uuid_;
}

void TlShape::clear() {
    label_.clear();
    points_.clear();
    point_labels_.clear();
    shape_type_.clear();
}

QPointF &TlShape::operator[](int32_t index) {
    if (index >= 0) {
        return points_[index];
    }
    return points_[points_.size() + index];
}

TlShape &TlShape::operator=(const TlShape &shape) {
    if (this != &shape) {
        SetValue(shape);
    }
    return *this;
}

bool TlShape::operator==(const TlShape &shape) const {
    return (uuid_ == shape.uuid_);
}

bool TlShape::operator!=(const TlShape &shape) const {
    return !(*this == shape);
}

bool TlShape::operator<(const TlShape &shape) const {
    return (uuid_ < shape.uuid_);
}

TlShape::operator bool() const {
    return !points_.empty();
}