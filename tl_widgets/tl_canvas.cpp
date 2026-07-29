#include "tl_canvas.h"

#include "common/format_qt.h"
#include "common/numpy_utils.h"
#include "config/app_config.h"
#include "tl_modules/polygon_from_mask.h"

#include <QWheelEvent>
#include <QApplication>


bool download_ai_model(const std::string &name, Canvas *) {
    return true;
}

const double MOVE_SPEED     = 5.0f;

std::set<QString> _CreateMode {
    "polygon",
    "rectangle",
    "oriented_rectangle",
    "circle",
    "line",
    "point",
    "linestrip",
    "ai_points_to_shape",
    "ai_box_to_shape",
};

std::set<QString> _AI_CREATE_MODES {
    "ai_points_to_shape",
    "ai_box_to_shape",
};


std::map<QString, QString> _CREATE_MODE_TO_SHAPE_TYPE {     // {CreateMode, ShapeType}
    {"polygon",             "polygon"             },
    {"rectangle",           "rectangle"           },
    {"oriented_rectangle",  "oriented_rectangle"  },
    {"circle",              "circle"              },
    {"line",                "line"                },
    {"point",               "point"               },
    {"linestrip",           "linestrip"           },
    {"ai_points_to_shape",  "points"              },
    {"ai_box_to_shape",     "rectangle"           },
};


// Modes whose seed point cannot be reinterpreted as the start of another mode.
// `point` finalizes on click so never has a partial shape; AI modes carry
// per-point positive/negative labels. Every other mode in _CreateMode shares a
// 1-click anchor and is seed-compatible by default — new modes participate
// unless explicitly listed here.
std::set<QString> _SEED_INCOMPATIBLE_CREATE_MODES {
    "point",
    "ai_points_to_shape",
    "ai_box_to_shape",
};


Canvas::Canvas(float epsilon,
               const QString &double_click,
               int32_t num_backups,
               const QMap<QString, bool> &crosshair,
               bool allow_out_of_bounds_points) : QWidget() {
    this->mode_                     = CanvasMode::EDIT;

    // polygon, rectangle, line, or point
    this->create_mode_               = "polygon";

    this->fill_drawing_             = false;

    this->show_labels_              = false;

    this->prevPoint_                = QPointF();
    this->prevMovePoint_            = QPointF();
    this->offsets_                  = { QPointF(), QPointF() };

    this->dragging_start_pos_       = QPointF();
    this->is_dragging_              = false;
    this->is_dragging_enabled_      = false;

    this->ai_assist_thread_         = std::unique_ptr<AiAssistThread>(new AiAssistThread(this));
    this->sam_session_model_name_   = AppConfig::instance().ai_assist_name_;
    this->sam_session_              = nullptr;
    this->ai_output_format_         = "polygon";

    //def __init__(self, *args, **kwargs):
    this->epsilon_                  = epsilon;
    this->double_click_             = double_click;
    if (!QKey{"", "close"}.contains(this->double_click_))
        throw std::invalid_argument(
            "Unexpected value for double_click event: " + double_click.toStdString()
        );
    this->num_backups_              = num_backups;
    this->crosshair_                = crosshair.size() == 8 ?
        crosshair :
        QMap<QString, bool> {
            { "polygon",            false },
            { "rectangle",          true  },
            { "oriented_rectangle", false },
            { "circle",             false },
            { "line",               false },
            { "point",              false },
            { "linestrip",          false },
            { "ai_points_to_shape", false },
            { "ai_box_to_shape",    true  },
        };

    this->resetState();

    // self.line represents:
    //   - createMode == 'polygon': edge from last point to current
    //   - createMode == 'rectangle': diagonal line of the rectangle
    //   - createMode == 'line': the line
    //   - createMode == 'point': the point
    this->line_                     = TlShape();
    this->prevPoint_                = QPointF();
    this->prevMovePoint_            = QPointF();
    this->offsets_                  = { QPointF(), QPointF() };
    this->scale_                    = 1.0;
    this->sam_session_              = nullptr;
    this->visible_                  = {};
    this->hideBackround_            = false;
    this->hideBackround1_           = false;
    this->snapping_                 = true;
    this->hShapeIsSelected_         = false;
    this->painter_;
    this->dragging_start_pos_       = QPointF();
    this->is_dragging_              = false;
    this->is_dragging_enabled_      = false;
    // Menus
    // 0: right-click without selection and dragging of shapes
    // 1: right-click with selection and dragging of shapes
    this->context_menus_ = {
        .without_selection = new QMenu(),
        .with_selection = new QMenu()
    };
    this->setMouseTracking(true);
    this->setFocusPolicy(Qt::WheelFocus);
}

bool Canvas::fillDrawing() const {
    return this->fill_drawing_;
}

void Canvas::set_fill_drawing(bool value) {
    this->fill_drawing_ = value;
}

//@property
bool Canvas::is_drawing() const {
    return static_cast<bool>(this->current_);
}

//@property
QString Canvas::createMode() const {
    return this->create_mode_;
}

//@createMode.setter
void Canvas::createMode(const QString &value) {
    if (!QKey{
        "polygon",
        "rectangle",
        "circle",
        "line",
        "point",
        "linestrip",
        "ai_points_to_shape",
        "ai_box_to_shape",
        }.contains(value)) {
        throw std::invalid_argument("Unsupported createMode: " + value.toStdString());
    }
    this->create_mode_ = value;
}

std::string Canvas::get_ai_model_name() {
    return this->sam_session_model_name_;
}

void Canvas::set_ai_model_name(const std::string &model_name) {
    this->sam_session_model_name_ = model_name;
    AppConfig::instance().ai_assist_name_ = model_name;
}

void Canvas::set_ai_output_format(const std::string &output_format) {
    ai_output_format_ = output_format;
}

SamSession &Canvas::get_osam_session() {
    if (
        this->sam_session_ == nullptr ||
        this->sam_session_->model_name() != this->sam_session_model_name_
    ) {
        this->sam_session_ = std::make_unique<SamSession>(this->sam_session_model_name_);
    }
    return *this->sam_session_;
}

QList<TlShape> Canvas::shapes_from_points_ai(
    const QList<QPointF> &points, const QList<int32_t> &labels
) {
    const auto image = utils::PixmapToMat(pixmap_);
    std::vector<cv::Point2f> coords_points;
    std::ranges::for_each(points, [&](const auto &v) { coords_points.push_back(cv::Point2f(v.x(), v.y())); });
    std::vector<float> coords_labels;
    std::ranges::for_each(labels, [&](const auto &v) { coords_labels.push_back(v); });

    GenerateResponse response = get_osam_session().run(
        image,  // type: ignore[arg-type]
        pixmap_hash_,
        coords_points,
        coords_labels
    );
    return shapes_from_ai_response(
        response,
        ai_output_format_
    );
}

QList<TlShape> Canvas::shapes_from_bbox_ai(const QList<QPointF> &bbox_points) {
    if (bbox_points.size() != 2)
        throw std::invalid_argument("Expected 2 points for bbox AI, got {len(bbox_points)}");
    const auto image = utils::PixmapToMat(pixmap_);
    std::vector<cv::Point2f> coords_points;
    std::ranges::transform(bbox_points, std::back_inserter(coords_points), [](const auto &v) { return cv::Point2f(v.x(), v.y()); });
    std::vector<float> coords_labels{2, 3};

    GenerateResponse response = get_osam_session().run(
        image,  //# type: ignore[arg-type]
        pixmap_hash_,
        coords_points,
        //# point_labels: 2=box corner, 3=opposite box corner (SAM convention)
        coords_labels
    );
    return shapes_from_ai_response(
        response,
        ai_output_format_
    );
}

void Canvas::storeShapes() {
    QList<TlShape> shapesBackup;
    std::ranges::for_each(shapes_, [&shapesBackup](const auto &shape) { shapesBackup.append(shape); });
    while (shapesBackups_.length() > num_backups_) {
        shapesBackups_.pop_front();
    }
    shapesBackups_.append(shapesBackup);
}

//@property
bool Canvas::can_restore_shape() {
    // We save the state AFTER each edit (not before) so for an
    // edit to be undoable, we expect the CURRENT and the PREVIOUS state
    // to be in the undo stack.
    if (shapesBackups_.length() < 2) {
        return false;
    }
    return true;
}

void Canvas::restore_last_shape() {
    // This does _part_ of the job of restoring shapes.
    // The complete process is also done in app.py::undoShapeEdit
    // and app.py::loadShapes and our own Canvas::loadShapes function.
    if (!can_restore_shape()) {
        return;
    }
    shapesBackups_.pop_back();  //latest

    // The application will eventually call Canvas.loadShapes which will
    // push this right back onto the stack.
    auto shapesBackup = shapesBackups_.back(); shapesBackups_.pop_back();
    shapes_ = { shapesBackup };
    selectedShapes_ = {};
    for (auto &shape : shapes_) {
        shape.selected_ = false;
    }
    this->update();
}

void Canvas::enterEvent(QEnterEvent *event) {
    overrideCursor(cursor_);
    update_status({});
}

void Canvas::leaveEvent(QEvent *event) {
    if (set_highlight(None, None, None)) {
        this->update();
    }
    restoreCursor();
    update_status({});
}

void Canvas::focusOutEvent(QFocusEvent *event) {
    restoreCursor();
    update_status({});
}

bool Canvas::isVisible(const TlShape &shape) {
    const auto it = visible_.find(shape.key());
    return it != visible_.end() ? it.value() : true;
}

bool Canvas::drawing() {
    return mode_ == CanvasMode::CREATE;
}

bool Canvas::editing() {
    return mode_ == CanvasMode::EDIT;
}

void Canvas::set_editing(bool value) {
    mode_ = value ? CanvasMode::EDIT : CanvasMode::CREATE;
    if (mode_ == CanvasMode::EDIT) {
        // CREATE -> EDIT
        repaint();  // clear crosshair
    } else {
        // EDIT -> CREATE
        bool need_update = set_highlight(
            None, None, None
        );
        need_update |= deSelectShape();
        if (need_update) {
            update();
        }
    }
}

bool Canvas::set_highlight(
    int32_t hShape, int32_t hEdge, int32_t hVertex
    ) {
    bool need_update = hShape != None;
    if (hShape_ != None) {
        shapes_[hShape_].highlightClear();
        need_update = true;
    }
    // NOTE: Store last highlighted for adding/removing points.
    this->lasthShape_  = hShape  == None ? hShape_  : hShape;
    this->lasthVertex_ = hVertex == None ? hVertex_ : hVertex;
    this->lasthEdge_   = hEdge   == None ? hEdge_   : hEdge;
    this->hShape_      = hShape;
    this->hVertex_     = hVertex;
    this->hEdge_       = hEdge;
    return need_update;
}

bool Canvas::selectedVertex() {
    return hVertex_ != None;
}

bool Canvas::selectedEdge() {
    return hEdge_ != None;
}

void Canvas::update_status(const std::list<QString> &extra_messages) {
    QStringList messages;
    if (drawing()) {
        messages.append(tr("Creating %1").arg(create_mode_));
        messages.append(get_create_mode_message());
        if (current_) {
            messages.append(tr("ESC to cancel"));
        }
        if (can_close_shape()) {
            messages.append(tr("Enter or Space to finalize"));
        }
    } else {
        //assert self.editing();
        messages.append(tr("Editing shapes"));
    }
    for (const auto &s : extra_messages) {
        messages.append(s);
    }
    emit statusUpdated(" • " + messages.join(""));
}

QString Canvas::get_create_mode_message() {
    //assert self.drawing()
    bool isNew = !this->current_;
    if (create_mode_ == "ai_points_to_shape") {
        return tr(
            "Click points to include or Shift+Click to exclude."
            " Ctrl+LeftClick ends creation."
        );
    }
    if (create_mode_ == "ai_box_to_shape") {
        if (isNew)
            return tr("Click first corner of bbox for AI segmentation");
        else
            return tr("Click opposite corner to segment object");
    }
    if (create_mode_ == "line") {
        if (isNew)
            return tr("Click start point for line");
        else
            return tr("Click end point for line");
    }
    if (create_mode_ == "linestrip") {
        if (isNew)
            return tr("Click start point for linestrip");
        else
            return tr(
                "Click next point or finish by Ctrl/Cmd+Click for linestrip"
            );
    }
    if (create_mode_ == "circle") {
        if (isNew)
            return tr("Click center point for circle");
        else
            return tr("Click point on circumference for circle");
    }
    if (create_mode_ == "rectangle") {
        if (isNew)
            return tr("Click first corner for rectangle");
        else
            return tr("Click opposite corner for rectangle (Shift for square)");
    }
    if (create_mode_ == "oriented_rectangle") {
        if (isNew)
            return tr("Click first corner for oriented rectangle");
        //assert self._current is not None
        if (current_.points_.size() == 1)
            return tr("Click second corner to set orientation");
        return tr("Click third corner to close oriented rectangle");
    }
    return tr("Click to add point");
}

void Canvas::mouseMoveEvent(QMouseEvent *event) {
    // Update line with last point and current coordinates.
    // Python中的 localPos 已废弃‌, 推荐使用 position 替代, 其功能完全相同
    QPointF pos;
    try {
        pos = transformPos(event->position());
    } catch (...) {
        return;
    }
    emit mouseMoved(pos);

    prevMovePoint_ = pos;

    bool is_shift_pressed = event->modifiers() & Qt::ShiftModifier;

    if (is_dragging_) {
        overrideCursor(Qt::OpenHandCursor);
        QPointF delta = pos - dragging_start_pos_;
        emit scrollRequest(static_cast<int>(delta.x()), Qt::Horizontal);
        emit scrollRequest(static_cast<int>(delta.y()), Qt::Vertical);
        return;
    }

    // Polygon drawing.
    if (drawing()) {
        if (QKey{"ai_points_to_shape", "ai_box_to_shape"}.contains(create_mode_)) {
            line_.shape_type("points");
        } else {
            line_.shape_type(create_mode_);
        }

        overrideCursor(Qt::CrossCursor);
        if (!current_) {
            update();  // draw crosshair
            update_status({});
            return;
        }

        if (outOfPixmap(pos)) {
            // Don't allow the user to draw outside the pixmap.
            // Project the point to the pixmap's edges.
            pos = compute_intersection_edges_image(
                current_[-1], pos, pixmap_.size()
            );
        } else if (
            snapping_ &&
            current_.size() > 1 &&
            create_mode_ == "polygon" &&
            closeEnough(pos, current_[0]))
        {
            // Attract line to starting point and
            // colorise to alert the user.
            pos = current_[0];
            overrideCursor(Qt::PointingHandCursor);
            current_.highlightVertex(0, TlShape::NEAR_VERTEX);
        }
        if (QKey{"polygon", "linestrip"}.contains(create_mode_)) {
            line_.points_ = { current_[-1], pos };
            line_.point_labels_ = { 1, 1 };
        } else if (QKey{"ai_points_to_shape", "ai_box_to_shape"}.contains(create_mode_)) {
            line_.points_ = { current_.points_.back(), pos };
            line_.point_labels_ = {
                current_.point_labels_.back(),
                is_shift_pressed ? 0 : 1,
            };
        } else if (create_mode_ == "rectangle") {
            if (is_shift_pressed) {
                prevMovePoint_ = pos = snap_cursor_pos_for_square(  // override
                    pos, current_[0]
                );
            }
            line_.points_ = { current_[0], pos };
            line_.point_labels_ = { 1, 1 };
            line_.close();
        } else if (create_mode_ == "oriented_rectangle") {
            const auto origin = (
                current_.points_.size() == 1 ? current_.points_[0] : current_.points_[1]
            );
            line_.points_ = {origin, pos};
            line_.point_labels_ = { 1, 1 };
            //line_.close();
        } else if (create_mode_ == "circle") {
            line_.points_ = { current_[0], pos };
            line_.point_labels_ = { 1, 1 };
            line_.shape_type("circle");
        } else if (create_mode_ == "line") {
            line_.points_ = { current_[0], pos };
            line_.point_labels_ = { 1, 1 };
            line_.close();
        } else if (create_mode_ == "point") {
            line_.points_ = { current_[0] };
            line_.point_labels_ = { 1 };
            line_.close();
        }
        assert(line_.points_.size() == line_.point_labels_.size());
        update();
        update_status({});
        return;
    }

    // Polygon copy moving.
    if (Qt::RightButton & event->buttons()) {
        if (!selectedShapesCopy_.empty() && !prevPoint_.isNull()) {
            overrideCursor(Qt::ClosedHandCursor);
            QList<int32_t> indexes(selectedShapesCopy_.size());
            std::iota(indexes.begin(), indexes.end(), 0); // 使用 std::iota 填充序列, 从0开始
            boundedMoveShapes(selectedShapesCopy_, indexes, pos);
            update();
        } else if (!selectedShapes_.empty()) {
            selectedShapesCopy_ = {};
            std::ranges::transform(selectedShapes_, std::back_inserter(selectedShapesCopy_), [this](int32_t idx){ return shapes_[idx]; });
            update();
        }
        update_status({});
        return;
    }

    // Polygon/Vertex moving.
    if (Qt::LeftButton & event->buttons()) {
        if (selectedVertex()) {
            //assert self.hVertex is not None
            //assert self.hShape is not None
            boundedMoveVertex(
                shapes_[hShape_], hVertex_, pos, is_shift_pressed
            );
            update();
            movingShape_ = true;
        } else if (!selectedShapes_.empty() && !prevPoint_.isNull()) {
            overrideCursor(Qt::ClosedHandCursor);
            boundedMoveShapes(shapes_, selectedShapes_, pos);
            update();
            movingShape_ = true;
        }
        return;
    }

    // Just hovering over the canvas, 2 possibilities:
    // - Highlight shapes
    // - Highlight vertex
    // Update shape/vertex fill and tooltip value accordingly.
    std::list<QString> status_messages;
    highlight_hover_shape(pos, status_messages);
    emit vertexSelected(hVertex_ != None);
    update_status(status_messages);
}

void Canvas::highlight_hover_shape(QPointF pos, std::list<QString> &status_messages) {
    std::vector<int32_t> ordered_shapes;
    if (hShape_ != None) { ordered_shapes.push_back(hShape_); }
    for (int32_t idx = shapes_.size() - 1; idx >= 0; --idx) { if (isVisible(shapes_[idx]) && idx != hShape_) { ordered_shapes.push_back(idx); } }
    //ordered_shapes: list[Shape] = ([self.hShape] if self.hShape else []) + [
    //    s for s in reversed(self.shapes) if self.isVisible(s) and s != self.hShape
    //]

    for (auto [idx, shape] : ordered_shapes | std::views::transform([this](int32_t i) { return std::make_pair(i, shapes_[i]); })) {
        auto index = shape.nearestVertex(pos, epsilon_);
        if (index != None) {
            set_highlight(idx, None, index);
            shape.highlightVertex(index, shape.MOVE_VERTEX);
            overrideCursor(Qt::PointingHandCursor);
            status_messages.push_back(tr("Click & drag to move point"));
            if (shape.canRemovePoint())
                status_messages.push_back(
                    tr("ALT + SHIFT + Click to delete point")
                );
            this->update();
            return;
        }
    }

    for (auto [idx, shape] : ordered_shapes | std::views::transform([this](int32_t i) { return std::make_pair(i, shapes_[i]); })) {
        auto index_edge = shape.nearestEdge(pos, epsilon_);
        if (index_edge != None && shape.canAddPoint()) {
            set_highlight(idx, index_edge, None);
            overrideCursor(Qt::PointingHandCursor);
            status_messages.push_back(tr("ALT + Click to create point on shape"));
            this->update();
            return;
        }
    }

    for (auto [idx, shape] : ordered_shapes | std::views::transform([this](int32_t i) { return std::make_pair(i, shapes_[i]); })) {
        if (shape.containsPoint(pos)) {
            set_highlight(idx, None, None);
            status_messages.push_back(
                tr("Click & drag to move shape")
            );
            status_messages.push_back(
                tr("Right-click & drag to copy shape")
            );
            overrideCursor(Qt::OpenHandCursor);
            this->update();
            return;
        }
    }

    restoreCursor();
    if (set_highlight(None, None, None)) {
        this->update();
    }
}

void Canvas::addPointToEdge() {
    auto shape = lasthShape_;
    auto index = lasthEdge_;
    auto point = prevMovePoint_;
    if (shape == None || index == None || point.isNull()) {
        return;
    }
    const auto saved = shapes_[shape];
    shapes_[shape].insertPoint(index, point);
    shapes_[shape].highlightVertex(index, TlShape::MOVE_VERTEX);
    hShape_ = shape;
    hVertex_ = index;
    hEdge_ = None;
    movingShape_ = true;
}

void Canvas::removeSelectedPoint() {
    auto shape = lasthShape_;
    auto index = lasthVertex_;
    if (shape == None || index == None) {
        return;
    }
    shapes_[shape].removePoint(index);
    shapes_[shape].highlightClear();
    hShape_ = shape;
    lasthVertex_ = None;
    movingShape_ = true;  // Save changes
}

void Canvas::mousePressEvent(QMouseEvent *event) {
    QPointF pos = transformPos(event->position());

    bool is_shift_pressed = event->modifiers() & Qt::ShiftModifier;

    if (event->button() == Qt::LeftButton) {
        if (drawing()) {
            if (current_) {
                // Add point to existing shape.
                if (create_mode_ == "polygon") {
                    current_.addPoint(line_[1]);
                    line_[0] = current_[-1];
                    if (current_.isClosed()) {
                        finalize();
                    }
                } else if (create_mode_ == "oriented_rectangle") {
                    if (current_.points_.size() == 4) {
                        finalize();
                    } else {
                        //assert len(current.points) == 1;
                        lock_oriented_rectangle_first_edge(current_);
                    }
                } else if (QKey{"rectangle", "circle", "line"}.contains(create_mode_)) {
                    assert(current_.points_.size() == 1);
                    current_.points_ = line_.points_;
                    finalize();
                } else if (create_mode_ == "linestrip") {
                    current_.addPoint(line_[1]);
                    line_[0] = current_[-1];
                    if (event->modifiers() == Qt::ControlModifier) {
                        finalize();
                    }
                } else if (QKey{"ai_points_to_shape", "ai_box_to_shape"}.contains(create_mode_)) {
                    current_.addPoint(
                        line_.points_[1],
                        line_.point_labels_[1]
                    );
                    line_.points_[0] = current_.points_.back();
                    line_.point_labels_[0] = current_.point_labels_.back();
                    if (event->modifiers() & Qt::ControlModifier) {
                        finalize();
                    }
                }
            } else if (!outOfPixmap(pos)) {
                if (QKey{"ai_points_to_shape", "ai_box_to_shape"}.contains(create_mode_)) {
                    if (!download_ai_model(this->sam_session_model_name_, this)) {
                        return;
                    }
                }

                // Create new shape.
                QString initial_shape_type;
                if (create_mode_ == "ai_points_to_shape") {
                    initial_shape_type = "points";
                } else if (create_mode_ == "ai_box_to_shape") {
                    initial_shape_type = "points";
                } else {
                    initial_shape_type = create_mode_;
                }
                current_ = TlShape("", TlShape::line_color, initial_shape_type);
                current_.addPoint(pos, is_shift_pressed ? 0 : 1);
                if (create_mode_ == "point") {
                    finalize();
                } else if (
                    QKey{"ai_points_to_shape", "ai_box_to_shape"}.contains(create_mode_)
                    && (event->modifiers() & Qt::ControlModifier)
                ) {
                    finalize();
                } else {
                    if (create_mode_ == "circle")
                        current_.shape_type("circle");
                    line_.points_ = {pos, pos};
                    if (QKey{"ai_points_to_shape", "ai_box_to_shape"}.contains(create_mode_) && is_shift_pressed) {
                        line_.point_labels_ = {0, 0};
                    } else
                        line_.point_labels_ = {1, 1};
                    setHiding();
                    emit drawingPolygon(true);
                    update();
                }
            }
        } else if (editing()) {
            if (selectedEdge() && event->modifiers() == Qt::AltModifier) {
                addPointToEdge();       // 增加节点
            } else if (selectedVertex() && event->modifiers() == (
                Qt::AltModifier | Qt::ShiftModifier
            )) {
                removeSelectedPoint();  // 删除节点
            }
            auto group_mode = event->modifiers() == Qt::ControlModifier;
            selectShapePoint(pos, group_mode);
            prevPoint_ = pos;
            update();
        }
    } else if (event->button() == Qt::RightButton && editing()) {
        auto group_mode = event->modifiers() == Qt::ControlModifier;
        if (selectedShapes_.empty() || (
             hShape_ != None && !selectedShapes_.contains(hShape_)
        )) {
            selectShapePoint(pos, group_mode);
            update();
        }
        prevPoint_ = pos;
    } else if (event->button() == Qt::MiddleButton && is_dragging_enabled_) {
        overrideCursor(Qt::OpenHandCursor);
        dragging_start_pos_ = pos;
        is_dragging_ = true;
    }
    update_status({});
}

void Canvas::lock_oriented_rectangle_first_edge(TlShape &current) {
    auto first_corner = line_.points_[0];
    auto second_corner = line_.points_[1];
    current_.points_ = {first_corner, second_corner, second_corner, first_corner};
    current_.point_labels_ = {1, 1, 1, 1};

    line_.points_ = { second_corner };
    line_.point_labels_ = {1};
}

void Canvas::unlock_oriented_rectangle_first_edge(TlShape &current) {
    auto anchor = current.points_[0];
    current_.points_ = { anchor };
    current_.point_labels_ = { 1 };
    line_.points_ = { anchor, anchor };
}

void Canvas::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::RightButton) {
        auto *menu = selectedShapesCopy_.size() > 0 ? context_menus_.with_selection : context_menus_.without_selection;
        restoreCursor();
        if (!menu->exec(mapToGlobal(event->pos())) && !selectedShapesCopy_.empty()) {
            // Cancel the move by deleting the shadow copy.
            selectedShapesCopy_ = {};
            repaint();
        }
    } else if (event->button() == Qt::LeftButton) {
        if (editing()) {
            if (
                hShape_ != None &&
                hShapeIsSelected_ &&
                !movingShape_
            ) {
                QList<int32_t> selected_shapes;
                std::ranges::for_each(selectedShapes_, [&](auto &x){ if (x != hShape_) { selected_shapes.push_back(x); } });
                emit selectionChanged(selected_shapes);
            }
        }
    } else if (event->button() == Qt::MiddleButton) {
        is_dragging_ = false;
        restoreCursor();
    }

    if (movingShape_ && hShape_ != None) {
        auto index = hShape_;
        if (shapesBackups_.back()[index].points_ != shapes_[index].points_) {
            storeShapes();
            emit shapeMoved();
        }

        movingShape_ = false;
    }
    update_status({});
}

bool Canvas::end_move(bool copy) {
    assert(!selectedShapes_.empty() && !selectedShapesCopy_.empty());
    assert(selectedShapesCopy_.size() == selectedShapes_.size());
    if (copy) {
        for (const auto &&[i, shape] : selectedShapesCopy_ | std::views::enumerate) {
            shapes_.append(shape);
            shapes_[selectedShapes_[i]].selected_ = false;
            selectedShapes_[i] = shapes_.count() - 1;
        }
    } else {
        for (const auto &&[i, shape] : selectedShapesCopy_ | std::views::enumerate) {
            shapes_[selectedShapes_[i]].points_ = shape.points_;
        }
    }
    selectedShapesCopy_ = {};
    update();
    storeShapes();
    return true;
}

void Canvas::hideBackroundShapes(bool value) {
    hideBackround_ = value;
    if (!selectedShapes_.empty()) {
        // Only hide other shapes if there is a current selection.
        // Otherwise the user will not be able to select a shape.
        setHiding(true);
        update();
    }
}

void Canvas::setHiding(bool enable) {
    hideBackround1_ = enable ? hideBackround_ : false;
}

bool Canvas::can_close_shape() {
    if (!drawing())
        return false;
    if (!current_)
        return false;
    if (QKey{"ai_points_to_shape", "ai_box_to_shape"}.contains(create_mode_))
        return true;
    if (create_mode_ == "linestrip")
        return current_.size() >= 2;
    if (create_mode_ == "oriented_rectangle") {
        // Points 2 and 3 are seeded as duplicates of points 1 and 0 after
        // the first edge is locked; mouse movement reprojects them. Treat
        // the shape as closeable only once the third corner has moved.
        return (
            current_.points_.size() == 4
            && current_.points_[2] != current_.points_[1]
        );
    }
    return current_.size() >= 3;
}

void Canvas::mouseDoubleClickEvent(QMouseEvent *event) {
    if (double_click_ != "close") {
        return;
    }

    if (can_close_shape()) {
        finalize();
    }
}

void Canvas::selectShapes(const QList<TlShape> &shapes) {
    setHiding();

    QList<int32_t> indexes;
    std::ranges::for_each(shapes, [&](auto &shape) { indexes.push_back(shapes_.indexOf(shape)); });
    emit selectionChanged(indexes);
    update();
}

void Canvas::selectShapePoint(const QPointF &point, bool multiple_selection_mode) {
    // Select the first shape created which contains this point.
    if (hVertex_ != None) {
        //assert self.hShape is not None
        shapes_[hShape_].highlightVertex(hVertex_, TlShape::MOVE_VERTEX);
    } else {
        //shape: Shape
        for (int32_t idx = shapes_.size() - 1; idx >= 0; --idx) {
            auto &shape = shapes_[idx];
            if (isVisible(shape) && shape.containsPoint(point)) {
                setHiding();
                if (!selectedShapes_.contains(idx)) {
                    if (multiple_selection_mode) {
                        auto select_shapes = selectedShapes_;
                        select_shapes.append(idx);
                        emit selectionChanged(select_shapes);
                    } else {
                        emit selectionChanged({idx});
                    }
                    hShapeIsSelected_ = false;
                } else {
                    hShapeIsSelected_ = true;
                }
                calculateOffsets(point);
                return;
            }
        }
    }
    if (deSelectShape())
        update();
}

void Canvas::calculateOffsets(const QPointF &point) {
    if (selectedShapes_.empty()) {
        offsets_ = { QPointF(0.0, 0.0), QPointF(0.0, 0.0) };
        return;
    }

    double left   = pixmap_.width();
    double top    = pixmap_.height();
    double right  = 0.;
    double bottom = 0.;
    for (const auto rect : selectedShapes_ | std::views::transform([this](int32_t i) { return shapes_[i].boundingRect(); })) {
        left    = std::min(left, rect.left());
        top     = std::min(top, rect.top());
        right   = std::max(right, rect.right());
        bottom  = std::max(bottom, rect.bottom());
    }
    offsets_ = {
        QPointF(left - point.x(), top - point.y()),
        QPointF(right - point.x(), bottom - point.y())
    };
}

void Canvas::boundedMoveVertex(
    TlShape &shape, int32_t vertex_index, QPointF pos, bool is_shift_pressed
) {
    if (vertex_index >= shape.points_.size()) {
        SPDLOG_WARN(
            "vertex_index is out of range: vertex_index={}, len(points)={}",
            vertex_index,
            shape.points_.size()
        );
        return;
    }
    if (shape.shape_type_ == "oriented_rectangle") {
        //bounded_move_oriented_rectangle_vertex(
        //    shape=shape, vertex_index=vertex_index, pos=pos
        //);
        return;
    }

    if (outOfPixmap(pos)) {
        pos = compute_intersection_edges_image(
            shape[vertex_index], pos, pixmap_.size()
        );
    }
    if (is_shift_pressed && shape.shape_type() == "rectangle")
        pos = snap_cursor_pos_for_square(
            pos, shape[1 - vertex_index]
        );

    shape.moveVertex(vertex_index, pos);
}

bool Canvas::boundedMoveShapes(QList<TlShape> &shapes, const QList<int32_t> &indexes, QPointF pos) {
    if (outOfPixmap(pos)) {
        return false;
    }
    auto tl = pos + offsets_[0];
    if (outOfPixmap(tl)) {
        pos -= QPointF(std::min(0., tl.x()), std::min(0., tl.y()));
    }
    auto br = pos + offsets_[1];
    if (outOfPixmap(br)) {
        pos += QPointF(
            std::min(0., pixmap_.width() - br.x()),
            std::min(0., pixmap_.height() - br.y())
        );
    }

    auto dp = pos - prevPoint_;
    if (dp.isNull())
        return false;

    for (const auto &idx : indexes) {
        shapes[idx].moveBy(dp);
    }
    prevPoint_ = pos;
    return true;
}

bool Canvas::deSelectShape() {
    bool need_update = false;
    if (!selectedShapes_.empty()) {
        setHiding(false);
        emit selectionChanged({});
        hShapeIsSelected_ = false;
        need_update = true;
    }
    return need_update;
}

QList<TlShape> Canvas::deleteSelected() {
    QList<TlShape> deleted_shapes = {};
    if (!selectedShapes_.empty()) {
        std::ranges::for_each(selectedShapes_, [&](auto idx){ deleted_shapes.push_back(shapes_[idx]); });
        for (auto &shape : deleted_shapes) {
            SPDLOG_INFO("deleteSelected, removeOne: {}", shape.label_);
            shapes_.removeOne(shape);
        }
        storeShapes();
        selectedShapes_ = {};
        update();
    }
    return deleted_shapes;
}

void Canvas::deleteShape(const TlShape &shape) {
    if (const auto idx = shapes_.indexOf(shape); selectedShapes_.count(idx)) {
        selectedShapes_.removeOne(idx);
    }
    if (shapes_.contains(shape)) {
        shapes_.removeOne(shape);
    }
    storeShapes();
    update();
}

void Canvas::paintEvent(QPaintEvent *event) {
    if (pixmap_.isNull()) {
        QWidget::paintEvent(event);
        return;
    }

    auto &p = painter_;
    p.begin(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    p.scale(scale_, scale_);
    p.translate(offsetToCenter());

    p.drawPixmap(0, 0, pixmap_);

    p.scale(1 / scale_, 1 / scale_);

    // draw crosshair
    if (
        crosshair_[create_mode_] &&
        drawing() &&
        !prevMovePoint_.isNull() &&
        !outOfPixmap(prevMovePoint_))
    {
        p.setPen(QColor(0, 0, 0));
        p.drawLine(
            0,
            prevMovePoint_.y() * scale_,
            pixmap_.width() * scale_ - 1,
            prevMovePoint_.y() * scale_
        );
        p.drawLine(
            prevMovePoint_.x() * scale_,
            0,
            prevMovePoint_.x() * scale_,
            pixmap_.height() * scale_ - 1
        );
    }

    TlShape::scale_ = scale_;
    for (auto &&[idx, shape] : shapes_ | std::views::enumerate) {
        if ((shape.selected_ || !hideBackround_) && isVisible(shape)) {
            shape.fill_ = (shape.selected_ || idx == hShape_);
            shape.paint(p);
        }
    }

    if (current_) {
        current_.paint(p);
        assert(line_.points_.size() == line_.point_labels_.size());
        line_.paint(p);
    }

    if (!selectedShapesCopy_.empty()) {
        for (auto &s : selectedShapesCopy_) {
            s.paint(p);
        }
    }

    if (!current_ || !QKey{
        "polygon",
        "ai_points_to_shape",
        "ai_box_to_shape"}.contains(create_mode_))
    {
        p.end();
        if (current_)
            current_.highlightClear();
        return;
    }

    auto drawing_shape = current_.copy();
    if (create_mode_ == "polygon") {
        if (fillDrawing() && current_.points_.size() >= 2) {
            //assert drawing_shape.fill_color is not None
            if (drawing_shape.fill_color_.alpha() == 0) {
                SPDLOG_WARN(
                    "fill_drawing=true, but fill_color is transparent,"
                    " so forcing to be opaque."
                );
                drawing_shape.fill_color_.setAlpha(64);
            }
            drawing_shape.addPoint(line_[1]);
        }
    } else if (QKey{"ai_points_to_shape", "ai_box_to_shape"}.contains(create_mode_)) {
        drawing_shape.addPoint(
            line_.points_[1],
            line_.point_labels_[1]
        );
        submit_shape_with_ai(
            drawing_shape.points_,
            drawing_shape.point_labels_
        );
        //if shapes:
        //    drawing_shape = shapes[0]
    }
    drawing_shape.fill_ = fillDrawing();
    drawing_shape.selected_ = fillDrawing();
    drawing_shape.paint(p);

    {
        std::lock_guard<std::mutex> lock{mutex_};
        for (auto &shape : ai_assist_shapes_) {
            shape.paint(p);
        }
    }
    p.end();
    if (current_)
        current_.highlightClear();
}

void Canvas::render_canvas() {
    ///self._palette_cache.clear()
    ///painter: QtGui.QPainter = self._painter
    ///painter.begin(self)
    ///try:
    ///    self._setup_world_transform(painter=painter)
    ///    for layer in self._render_layers():
    ///        layer(painter)
    ///finally:
    ///    painter.end()
}

void Canvas::setup_world_transform(QPainter &painter) {
    ///for hint in (
    ///    QtGui.QPainter.RenderHint.Antialiasing,
    ///    QtGui.QPainter.RenderHint.SmoothPixmapTransform,
    ///):
    ///    painter.setRenderHint(hint);
    ///painter.translate(self._compute_image_origin_offset() * self.scale);
}

void Canvas::render_layers() {
    ///// Order is z-order, back-to-front.
    ///return (
    ///    self._draw_pixmap_layer,
    ///    self._draw_crosshair_layer,
    ///    self._draw_committed_shapes_layer,
    ///    self._draw_active_shape_layer,
    ///    self._draw_drag_copy_layer,
    ///    self._draw_preview_overlay_layer,
    ///);
}

void Canvas::draw_pixmap_layer(QPainter &painter) {
    ///target = QtCore.QRectF(
    ///    0.0,
    ///    0.0,
    ///    self.pixmap.width() * self.scale,
    ///    self.pixmap.height() * self.scale,
    ///);
    ///painter.drawPixmap(target, self.pixmap, QtCore.QRectF(self.pixmap.rect()));
}

void Canvas::draw_crosshair_layer(QPainter &painter) {
    ///cursor: QPointF | None = self._prev_move_point;
    ///if not self._should_draw_crosshair(cursor=cursor):
    ///    return;
    ///assert cursor is not None;
    ///painter.setPen(self.palette().color(QtGui.QPalette.ColorRole.WindowText));
    ///cx = int(cursor.x() * self.scale);
    ///cy = int(cursor.y() * self.scale);
    ///if self._allow_out_of_bounds_points:
    ///    // The cursor may be in the margin around the image, so span the whole
    ///    // viewport instead of stopping the lines at the image edge.
    ///    offset = self._compute_image_origin_offset() * self.scale;
    ///    area = super().size();
    ///    left = int(-offset.x());
    ///    top = int(-offset.y());
    ///    right = int(-offset.x() + area.width());
    ///    bottom = int(-offset.y() + area.height());
    ///else:
    ///    left = top = 0;
    ///    right = int(self.pixmap.width() * self.scale) - 1;
    ///    bottom = int(self.pixmap.height() * self.scale) - 1;
    ///painter.drawLine(left, cy, right, cy);
    ///painter.drawLine(cx, top, cx, bottom);
}

bool Canvas::should_draw_crosshair(QPointF &cursor) {
    ///if self.mode != _CanvasMode.CREATE:
    ///    return False;
    ///if not self._crosshair[self._create_mode]:
    ///    return False;
    ///if cursor is None:
    ///    return False;
    ///return not self._should_constrain_to_pixmap(cursor);
    return false;
}

void Canvas::draw_committed_shapes_layer(QPainter &painter) {
    ///for shape in self.shapes:
    ///    if not shape.visible:
    ///        continue;
    ///    context = self._render_context(
    ///        shape=shape, highlighted=shape is self.hovered_shape
    ///    );
    ///    render_shape(painter=painter, shape=shape, context=context);
}

void Canvas::draw_active_shape_layer(QPainter &painter) {
    ///if self._current is None:
    ///    return;
    ///assert len(self._line.points) == len(self._line.point_labels);
    ///self._render_draft(painter=painter, draft=self._current, highlighted=True);
    ///self._render_draft(painter=painter, draft=self._line, highlighted=False);
}

void Canvas::draw_drag_copy_layer(QPainter &painter) {
    ///for copy_shape in self._selected_shapes_copy:
    ///    context = ShapeRenderContext(
    ///        scale=self.scale,
    ///        palette=self._resolve_palette(copy_shape.label),
    ///        point_size=self._point_size,
    ///        point_type=self._point_type,
    ///        selected=True,
    ///        fill=True,
    ///        highlight=None,
    ///        rotation_highlight=None,
    ///        show_label=self._show_labels,
    ///    );
    ///    render_shape(painter=painter, shape=copy_shape, context=context);
}

void Canvas::draw_preview_overlay_layer(QPainter &painter) {
    ///preview = self._build_preview_shape();
    ///if preview is None:
    ///    return;
    ///context = self._draft_render_context(
    ///    selected=self._fill_drawing,
    ///    fill=self._fill_drawing,
    ///    highlight=None,
    ///    rotation_highlight=None,
    ///);
    ///render_shape(painter=painter, shape=preview, context=context);
}

QPointF Canvas::transformPos(QPointF point) {
    // Convert from widget-logical coordinates to painter-logical ones.
    return point / scale_ - offsetToCenter();
}

void Canvas::enableDragging(bool enabled) {
    is_dragging_enabled_ = enabled;
}

QPointF Canvas::offsetToCenter() {
    auto s = scale_;
    auto area = QWidget::size();
    float w = pixmap_.width() * s, h = pixmap_.height() * s;
    float aw = area.width(), ah = area.height();
    float x = (aw > w) ? ((aw - w) / (2 * s)) : 0.;
    float y = (ah > h) ? ((ah - h) / (2 * s)) : 0.;
    return QPointF(x, y);
}

bool Canvas::outOfPixmap(const QPointF &p) {
    auto w = pixmap_.width(), h = pixmap_.height();
    return !(0 <= p.x() && p.x() <= w && 0 <= p.y() && p.y() <= h);
}

void Canvas::finalize() {
    assert(current_);
    QList<TlShape> new_shapes;
    if (QKey{"ai_points_to_shape", "ai_box_to_shape"}.contains(create_mode_)) {
        std::lock_guard<std::mutex> lock{mutex_};
        new_shapes = ai_assist_shapes_;
    } else {
        current_.close();
        new_shapes = { current_ };
    }

    if (new_shapes.empty()) {
        current_.clear();
        ai_assist_points_.clear();
        ai_assist_shapes_.clear();
        return;
    }

    shapes_.append(new_shapes);
    storeShapes();
    current_.clear();
    ai_assist_points_.clear();
    ai_assist_shapes_.clear();
    setHiding(false);
    emit newShape();
    update();
}

bool Canvas::closeEnough(const QPointF &p1, const QPointF &p2) {
    // d = distance(p1 - p2)
    // m = (p1-p2).manhattanLength()
    // print "d %.2f, m %d, %.2f" % (d, m, d - m)
    // divide by scale to allow more precision when zoomed in
    return utils::distance(p1 - p2) < (epsilon_ / scale_);
}

// These two, along with a call to adjustSize are required for the
// scroll area.
QSize Canvas::sizeHint() const {
    return minimumSizeHint();
}

QSize Canvas::minimumSizeHint() const {
    if (pixmap_.isNull()) {
        return QWidget::minimumSizeHint();
    }

    QSize min_size = scale_ * pixmap_.size();
    if (is_dragging_enabled_) {
        // When drag buffer should be enabled, add a bit of buffer around the image
        // This lets dragging the image around have a bit of give on the edges
        min_size = 1.167 * min_size;
    }
    return min_size;
}

void Canvas::wheelEvent(QWheelEvent *event) {
    Qt::KeyboardModifiers mods = event->modifiers();
    QPoint delta = event->angleDelta();
    if (Qt::ControlModifier == mods) {
        // Ctrl + 滚轮向上滚动, 放大
        // Ctrl + 滚轮向下滚动, 缩小
        emit zoomRequest(delta.y(), event->position());
    } else {
        // 滚轮向上滚动, 上移
        // 滚轮向下滚动, 下移
        emit scrollRequest(delta.x(), Qt::Horizontal);
        emit scrollRequest(delta.y(), Qt::Vertical);
    }
    event->accept();
}

void Canvas::moveByKeyboard(QPointF offset) {
    if (!selectedShapes_.empty()) {
        boundedMoveShapes(shapes_, selectedShapes_, prevPoint_ + offset);
        update();
        movingShape_ = true;
    }
}

void Canvas::keyPressEvent(QKeyEvent *event) {
    const auto modifiers = event->modifiers();
    const auto key = event->key();
    if (drawing()) {
        if (key == Qt::Key_Escape && current_) {
            current_.clear();
            ai_assist_points_.clear();
            ai_assist_shapes_.clear();
            emit drawingPolygon(false);
            update();
        } else if (
            (key == Qt::Key_Return || key == Qt::Key_Space) &&
            can_close_shape()
        ) {
            finalize();
        } else if (modifiers == Qt::AltModifier) {
            snapping_ = false;
        }
    } else if (editing()) {
        if (key == Qt::Key_Up) {
            moveByKeyboard(QPointF(0.0, -MOVE_SPEED));
        } else if (key == Qt::Key_Down) {
            moveByKeyboard(QPointF(0.0, MOVE_SPEED));
        } else if (key == Qt::Key_Left) {
            moveByKeyboard(QPointF(-MOVE_SPEED, 0.0));
        } else if (key == Qt::Key_Right) {
            moveByKeyboard(QPointF(MOVE_SPEED, 0.0));
        } else if (event->matches(QKeySequence::SelectAll)) {
            selectShapes(shapes_);
        }
    }
    update_status({});
}

void Canvas::keyReleaseEvent(QKeyEvent *event) {
    const auto modifiers = event->modifiers();
    if (drawing()) {
        if (modifiers == Qt::NoModifier) {
            snapping_ = true;
        }
    } else if (editing()) {
        if (
            movingShape_ &&
            !selectedShapes_.empty() &&
            selectedShapes_[0] < shapes_.size()
        ) {
            const auto index = selectedShapes_[0];
            if (shapesBackups_.back()[index].points_ != shapes_[index].points_) {
                storeShapes();
                emit shapeMoved();
            }

            movingShape_ = false;
        }
    }
}

QList<TlShape> Canvas::setLastLabel(const QString &text, int32_t group_id, const QString &description, const QMap<QString, bool> &flags) {
    assert(text);
    QList<TlShape> shapes;
    for (auto &shape : shapes_ | std::views::reverse) {
        if (!shape.label_.isEmpty())
            break;
        shape.label_ = text;
        shape.flags_ = flags;
        shape.group_id_ = group_id;
        shape.description_ = description;
        shapes.append(shape);
    }
    //shapes.reverse()
    //for (auto &shape : shapes | std::views::reverse) {
    //    shape.label_ = text;
    //    shape.flags_ = flags;
    //}
    shapesBackups_.pop_back();
    storeShapes();
    return shapes;
}

void Canvas::undoLastLine() {
    assert(self.shapes);
    if (QKey{"ai_points_to_shape", "ai_box_to_shape"}.contains(create_mode_)) {
        // Remove all unlabeled shapes at the tail (added by AI in one shot)
        while (!shapes_.empty() && shapes_.back().label_.isEmpty())
            shapes_.pop_back();
        current_.clear();
        ai_assist_points_.clear();
        ai_assist_shapes_.clear();
        emit drawingPolygon(false);
        update();
        return;
    }
    current_ = shapes_.back(); shapes_.pop_back();
    current_.setOpen();
    current_.restoreShapeRaw();
    if (QKey{"polygon", "linestrip"}.contains(create_mode_)) {
        line_.points_ = { current_[-1], current_[0] };
    } else if (QKey{"rectangle", "line", "circle"}.contains(create_mode_)) {
        current_.points_ = { current_.points_[0], current_.points_[1] };
    } else if (create_mode_ == "point") {
        current_.clear();
    } else {
        //assert self.create_mode == "oriented_rectangle"
    }
    emit drawingPolygon(true);
}

void Canvas::undoLastPoint() {
    if (!current_ || current_.isClosed()) {
        return;
    }
    if (create_mode_ == "oriented_rectangle" && current_.points_.size() == 4) {
        unlock_oriented_rectangle_first_edge(current_);
        update();
        return;
    }
    current_.popPoint();
    if (current_.size() > 0) {
        line_[0] = current_[-1];
    } else {
        current_.clear();
        emit drawingPolygon(false);
    }
    update();
}

void Canvas::loadPixmap(const QPixmap &pixmap, const QString &filename, bool clear_shapes) {
    pixmap_ = pixmap;
    pixmap_hash_ = std::hash<QString>{}(filename);

    if (clear_shapes) {
         shapes_.clear();
    }
    update();
}

void Canvas::loadShapes(const QList<TlShape> &shapes, bool replace) {
    if (replace) {
        shapes_ = shapes;
    } else {
        shapes_.append(shapes);
    }
    storeShapes();
    current_.clear();
    ai_assist_points_.clear();
    ai_assist_shapes_.clear();
    hShape_ = None;
    hVertex_ = None;
    hEdge_ = None;
    update();
}

void Canvas::setShapeVisible(const TlShape &shape, bool value) {
    visible_[shape.key()] = value;
    update();
}

void Canvas::overrideCursor(const QCursor &cursor) {
    if (cursor_ == cursor) {
        return;
    }
    restoreCursor();
    cursor_ = cursor;
    QApplication::setOverrideCursor(cursor);
}

void Canvas::restoreCursor() {
    this->cursor_ = Qt::ArrowCursor;
    QApplication::restoreOverrideCursor();
}

void Canvas::resetState() {
    this->restoreCursor();
    this->pixmap_ = QPixmap();
    this->pixmap_hash_ = None;
    this->shapes_.clear();
    this->shapesBackups_.clear();
    this->movingShape_ = false;
    this->selectedShapes_.clear();
    this->selectedShapesCopy_.clear();
    this->current_.clear();
    this->ai_assist_points_.clear();
    this->ai_assist_shapes_.clear();
    this->hShape_ = None;
    this->lasthShape_ = None;
    this->hVertex_ = None;
    this->lasthVertex_ = None;
    this->hEdge_ = None;
    this->lasthEdge_ = None;
    this->update();
}

TlShape Canvas::shape_from_annotation(
    const Annotation &annotation,
    const std::string &output_format
) {
    if (annotation.mask.empty()) {
        SPDLOG_WARN("No annotation mask returned");
        return {};
    }

    auto &mask = annotation.mask;

    if (create_mode_ == "ai_box_to_shape") {
        int32_t x1, y1, x2, y2;
        if (annotation.bbox.isNone()) {
            const cv::Rect bbox = utils::masks_to_bboxes(mask);
            x1 = bbox.x,              y1 = bbox.y;
            x2 = bbox.x + bbox.width, y2 = bbox.y + bbox.height;
        } else {
            x1 = annotation.bbox.x1, y1 = annotation.bbox.y1;
            x2 = annotation.bbox.x2, y2 = annotation.bbox.y2;
        }
        TlShape shape;
        shape.setShapeRefined(
            "mask",
            {QPointF(x1, y1), QPointF(x2, y2)},
            {1, 1},
            mask(cv::Rect(x1, y1, x2-x1, y2-y1)).clone()
        );
        shape.close();
        return shape;
    } else if (create_mode_ == "ai_points_to_shape") {
        auto points = measure::compute_polygon_from_mask(mask);
        if (points.size() < 2)
            return {};
        if (!annotation.bbox.isNone()) {
            auto &bb = annotation.bbox;
            std::ranges::for_each(points, [&](auto &point) { point.x += bb.x1; point.y += bb.y1; });
        }

        QList<QPointF> point_coords;
        point_coords.reserve(points.size());
        std::ranges::for_each(points, [&](const auto &v) { point_coords.push_back(QPointF(v.x, v.y)); });
        QList<int32_t> point_labels(points.size(), 1);

        TlShape shape;
        shape.setShapeRefined(
            "polygon",
            point_coords,
            point_labels
        );
        shape.close();
        return shape;
    }
    throw std::invalid_argument("Unsupported output_format: " + output_format);
}

QList<TlShape> Canvas::shapes_from_ai_response(
    GenerateResponse &response,
    const std::string &output_format
) {
    if (!QList<std::string>{"polygon", "mask"}.contains(output_format)) {
        throw std::invalid_argument(
            "output_format must be 'polygon' or 'mask', not " + output_format
        );
    }

    if (response.annotations.empty()) {
        SPDLOG_WARN("No annotations returned");
        return {};
    }

    // 根据score从大到小排序.
    std::ranges::sort(response.annotations, [](const auto &a, const auto &b) { return a.score > b.score; });
    //annotations = sorted(
    //    response.annotations,
    //    key=lambda a: a.score if a.score is not None else 0,
    //    reverse=True,
    //)

    QList<TlShape> shapes;
    for (auto &annotation : response.annotations) {
        auto shape = shape_from_annotation(
            annotation, output_format
        );
        if (shape) {
            shapes.append(shape);
        }
    }
    return shapes;
}

QPointF Canvas::snap_cursor_pos_for_square(QPointF pos, QPointF opposite_vertex) {
    QPointF pos_from_opposite = pos - opposite_vertex;
    float square_size = std::min(abs(pos_from_opposite.x()), abs(pos_from_opposite.y()));
    return opposite_vertex + QPointF(
        np::sign(pos_from_opposite.x()) * square_size,
        np::sign(pos_from_opposite.y()) * square_size
    );
}

QPointF Canvas::compute_intersection_edges_image(
    const QPointF &p1, const QPointF &p2, const QSize &image_size
) {
    // Cycle through each image edge in clockwise fashion,
    // and find the one intersecting the current line segment.
    // http://paulbourke.net/geometry/lineline2d/
    const std::vector<QPointF> points = {
        {0., 0.},
        {image_size.width() * 1., 0.},
        {image_size.width() * 1., image_size.height() * 1.},
        {0., image_size.height() * 1.},
    };
    // x1, y1 should be in the pixmap, x2, y2 should be out of the pixmap
    auto x1 = std::min(std::max(p1.x(), 0.), image_size.width() * 1.);
    auto y1 = std::min(std::max(p1.y(), 0.), image_size.height() * 1.);
    auto x2 = p2.x(), y2 = p2.y();
    //d, i, (x, y) = std::min(compute_intersection_edges((x1, y1), (x2, y2), points))
    const auto results = compute_intersection_edges(QPointF(x1, y1), QPointF(x2, y2), points);
    if (results.empty()) {   // 无交点 -- 调用前判断过, 这里肯定是有交点的.
        return QPointF(-1, -1);
    }
    const auto minVal = *std::ranges::min_element(results, [](const auto &a, const auto &b) { return std::get<0>(a) < std::get<0>(b); });
    const auto d = std::get<0>(minVal);
    const auto i = std::get<1>(minVal);
    const auto x = std::get<2>(minVal).x(), y = std::get<2>(minVal).y();

    const auto x3 = points[i].x(), y3 = points[i].y();
    const auto x4 = points[(i+1)%4].x(), y4 = points[(i+1)%4].y();
    if ((x, y) == (x1, y1)) {
        // Handle cases where previous point is on one of the edges.
        if (x3 == x4) {
            return QPointF(x3, std::min(std::max(0., p2.y()), std::max(y3, y4)));
        } else {  // y3 == y4
            return QPointF(std::min(std::max(0., p2.x()), std::max(x3, x4)), y3);
        }
    }
    return QPointF(x, y);
}

std::vector<std::tuple<qreal, int32_t, QPointF>> Canvas::compute_intersection_edges(
    const QPointF &point1,
    const QPointF &point2,
    const std::vector<QPointF> &points
) {
    //"""Find intersecting edges.
    //
    //For each edge formed by `points', yield the intersection
    //with the line segment `(x1,y1) - (x2,y2)`, if it exists.
    //Also return the distance of `(x2,y2)' to the middle of the
    //edge along with its index, so that the one closest can be chosen.
    std::vector<std::tuple<qreal, int32_t, QPointF>> results;
    const auto x1 = point1.x(), y1 = point1.y();
    const auto x2 = point2.x(), y2 = point2.y();
    for (int32_t i = 0; i < 4; ++i) {
        const auto x3 = points[i].x(), y3 = points[i].y();
        const auto x4 = points[(i+1)%4].x(), y4 = points[(i+1)%4].y();
        const auto denom = (y4 - y3) * (x2 - x1) - (x4 - x3) * (y2 - y1);
        const auto nua = (x4 - x3) * (y1 - y3) - (y4 - y3) * (x1 - x3);
        const auto nub = (x2 - x1) * (y1 - y3) - (y2 - y1) * (x1 - x3);
        if (abs(denom) < 1e-9)  // 平行或重合
            // This covers two cases:
            //   nua == nub == 0: Coincident
            //   otherwise: Parallel
            continue;
        const auto ua = nua / denom, ub = nub / denom;
        if ((0 <= ua && ua <= 1) && (0 <= ub && ub <= 1)) {     // 验证交点有效性
            const auto x = x1 + ua * (x2 - x1);
            const auto y = y1 + ua * (y2 - y1);
            const auto m = QPointF((x3 + x4) / 2, (y3 + y4) / 2);
            const auto d = utils::distance(m - QPointF(x2, y2));
            //yield d, i, (x, y)
            results.emplace_back(std::make_tuple(d, i, QPointF(x,y)));
        }
    }
    return results;
}

//
// User-assisted function.
//
Canvas::~Canvas() {
    if (ai_assist_thread_) {
        ai_assist_thread_.reset();
    }
}

void Canvas::update_shape_info(const TlShape &shape) {
    for (auto &s : shapes_) {
        if (s == shape) {
            s.label_                = shape.label_;
            s.flags_                = shape.flags_;
            s.group_id_             = shape.group_id_;
            s.description_          = shape.description_;

            s.line_color_           = shape.line_color_;
            s.vertex_fill_color_    = shape.vertex_fill_color_;
            s.hvertex_fill_color_   = shape.hvertex_fill_color_;
            s.fill_color_           = shape.fill_color_;
            s.select_line_color_    = shape.select_line_color_;
            s.select_fill_color_    = shape.select_fill_color_;
        }
    }
}

// AI辅助需要加载模型与图像编码耗时较长, 需要防止GUI界面假死, 这里进行异步处理拆分.
void Canvas::submit_shape_with_ai(const QList<QPointF> &points, const QList<int32_t> &labels) {
    if (ai_assist_points_ == points) {
        return;
    }

    if (ai_assist_thread_->Submit(points, labels)) {
        ai_assist_points_ = points;
    }
}

void Canvas::update_shape_with_ai(const QList<QPointF> &points, const QList<int32_t> &labels) {
    emit aiAssistSubmit();

    QList<TlShape> new_shapes = shapes_from_points_ai(points, labels);
    {
        std::lock_guard<std::mutex> lock{mutex_};
        ai_assist_shapes_.swap(new_shapes);
    }

    emit aiAssistFinish();
    this->update();
}
