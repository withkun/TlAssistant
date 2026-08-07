#include "tl_canvas.h"

#include "common/format_qt.h"
#include "common/np_utils.h"
#include "config/app_config.h"
#include "tl_modules/polygon_from_mask.h"

#include <QApplication>
#include <QAbstractScrollArea>
#include <QWheelEvent>
#include <memory>


bool download_ai_model(const std::string &name, Canvas *) {
    return true;
}

const static std::vector<int32_t> _DEFAULT_SHAPE_RGB{0, 255, 0};
const static Palette _DEFAULT_PALETTE = Palette::from_rgb(_DEFAULT_SHAPE_RGB);


//@dataclasses.dataclass(frozen=True)
//class _DraftShape:
//    """In-progress shape held in QPointF while drawing, before it is committed
//    to a Qt-free numpy `Shape` at finalize time. Immutable so each edit returns a
//    new draft and the canvas reassigns ``self._line`` / ``self._current`` as a
//    whole: state changes happen at assignment boundaries that are easy to follow,
//    rather than in-place mutation threaded through the drawing methods."""
//
//    shape_type: ShapeType = "polygon"
//    points: tuple[QPointF, ...] = ()
//    point_labels: tuple[int, ...] = ()
//    closed: bool = False
//
DraftShape DraftShape::close() {
    closed_ = true;
    return *this;
}

DraftShape DraftShape::open() {
    closed_ = false;
    return *this;
}

DraftShape DraftShape::add_point(
    const QPointF &point, const int32_t label, const bool autoclose
) {
    if (autoclose && !points_.empty() && points_[0] == point) {
        closed_ = true;
        return *this;
    }

    points_.push_back(point);
    point_labels_.push_back(label);
    return *this;
}

DraftShape DraftShape::pop_point() {
    if (points_.empty()) {
        return *this;
    }

    points_.pop_back();
    point_labels_.pop_back();
    return *this;
}

TlShape draft_to_shape(const DraftShape &draft) {
    return TlShape{
        draft.shape_type_,
        draft.points_,
        draft.point_labels_,
        draft.closed_
    };
}

DraftShape shape_to_draft(const TlShape &shape) {
    return DraftShape{
        shape.shape_type_,
        shape.points_,
        shape.point_labels_,
        shape.closed_
    };
}

constexpr float MOVE_SPEED     = 5.0f;

const static std::set<QString> CreateMode {
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

const static std::set<QString> AI_CREATE_MODES {
    "ai_points_to_shape",
    "ai_box_to_shape",
};

static std::map<QString, QString> CREATE_MODE_TO_SHAPE_TYPE {     // Final[dict[_CreateMode, ShapeType]]
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

static std::set<QString> POLYLINE_SHAPE_TYPES{ "polygon", "linestrip" };

// Modes whose seed point cannot be reinterpreted as the start of another mode.
// `point` finalizes on click so never has a partial shape; AI modes carry
// per-point positive/negative labels. Every other mode in _CreateMode shares a
// 1-click anchor and is seed-compatible by default — new modes participate
// unless explicitly listed here.
const static std::set<QString> SEED_INCOMPATIBLE_CREATE_MODES {
    "point",
    "ai_points_to_shape",
    "ai_box_to_shape",
};


Canvas::Canvas(float epsilon,
               const QString &double_click,
               int32_t num_backups,
               const QMap<QString, bool> &crosshair,
               bool allow_out_of_bounds_points) : QWidget() {
    this->pixmap_                       = {};   // QtGui.QPixmap
    this->pixmap_hash_                  = {};   // int | None
    this->cursor_                       = {};   // CursorRole
    this->shapes_                       = {};   // list[Shape]
    this->shape_backups_                = {};   // collections.deque[list[Shape]]
    this->is_moving_shape_              = {};   // bool
    this->selected_shapes_              = {};   // list[Shape]
    this->selected_shapes_copy_         = {};   // list[Shape]
    this->current_                      = {};   // _DraftShape | None
    this->hovered_shape_                = {};   // Shape | None
    this->last_hovered_shape_           = {};   // Shape | None
    this->hovered_vertex_               = {};   // int | None
    this->last_hovered_vertex_          = {};   // int | None
    this->hovered_edge_                 = {};   // int | None
    this->last_hovered_edge_            = {};   // int | None
    this->hovered_rotation_             = {};   // int | None

    this->mode_                         = CanvasMode::EDIT;

    this->create_mode_                  = "polygon";

    this->fill_drawing_                 = false;

    this->show_labels_                  = false;

    this->prev_point_                   = QPointF();
    this->prev_move_point_              = QPointF();
    this->drag_anchor_                  = {};     // : tuple[QPointF, QRectF]
    this->rotation_center_              = {};     // : np.ndarray
    this->rotation_initial_angle_       = {};     // : float
    this->rotation_original_points_     = {};     // : np.ndarray

    this->pan_anchor_                   = {};     // : QPointF | None

    this->highlight_                    = {};     // : VertexHighlight | None
    this->rotation_highlight_           = {};     // : VertexHighlight | None
    this->color_resolver_               = {};     // : Callable[[str], tuple[int, int, int]] | None
    this->point_size_                   = {};     // : int
    this->point_type_                   = {};     // : Literal["square", "round"]
    this->draft_palette_                = {};     // : Palette
    this->palette_cache_                = {};     // : dict[str, Palette]

    this->sam_session_                  = nullptr;

    //def __init__(self, *args: Any, **kwargs: Any) -> None:  # noqa: ANN401
    this->epsilon_                      = epsilon;
    this->double_click_                 = double_click;
    if (!QKey{"", "close"}.contains(this->double_click_))
        throw std::invalid_argument(
            "Unexpected value for double_click event: " + double_click.toStdString()
        );
    this->num_backups_                  = num_backups;
    this->allow_out_of_bounds_points_   = allow_out_of_bounds_points;
    this->crosshair_                    = crosshair.size() == 8 ?
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

    this->cursor_                       = CursorRole::DEFAULT;
    this->reset_state();

    // self._line represents:
    //   - create_mode == 'polygon': edge from last point to current
    //   - create_mode == 'rectangle': diagonal line of the rectangle
    //   - create_mode == 'line': the line
    //   - create_mode == 'point': the point
    this->line_                         = TlShape();
    this->prev_point_                   = QPointF();
    this->prev_move_point_              = QPointF();
    this->drag_anchor_                  = {QPointF(), QRectF()};
    this->rotation_center_              = {};
    this->rotation_initial_angle_       = 0.0f;
    this->rotation_original_points_     = {};
    this->scale_                        = 1.0;
    this->ai_assist_thread_             = std::make_unique<AiAssistThread>(this);
    this->ai_inference_failed_          = false;
    this->snapping_                     = true;
    this->hovered_shape_is_selected_    = false;
    this->painter_                      ;
    this->pan_anchor_                   = {};
    this->color_resolver_               = {};
    this->point_size_                   = 8;
    this->point_type_                   = "round";
    this->draft_palette_                = _DEFAULT_PALETTE;
    this->palette_cache_                = {};
    this->context_menus_ = {
        .without_selection_             = new QMenu(),
        .with_selection_                = new QMenu()
    };
    this->context_menu_origin_          = QPoint();
    this->setMouseTracking(true);
    this->setFocusPolicy(Qt::FocusPolicy::WheelFocus);

    this->sam_session_model_name_       = AppConfig::instance().ai_assist_name_;
    this->ai_output_format_             = "polygon";

    //****
    this->offsets_                      = { QPointF(), QPointF() };
    this->visible_                      = {};
    this->hideBackround_                = false;
    this->hideBackround1_               = false;
    this->dragging_start_pos_           = QPointF();
    this->is_dragging_                  = false;
    this->is_dragging_enabled_          = false;
}

void Canvas::set_fill_drawing(bool value) {
    this->fill_drawing_ = value;
}

void Canvas::set_show_labels(bool value) {
    this->show_labels_ = value;
}

void Canvas::set_allow_out_of_bounds_points(bool value) {
    this->allow_out_of_bounds_points_ = value;
}

void Canvas::set_color_resolver(const fColorResolver &resolver) {
    this->color_resolver_ = resolver;
}

void Canvas::set_point_size(int32_t point_size) {
    this->point_size_ = point_size;
}

Palette Canvas::resolve_palette(const QString &label) {
    if (label.isEmpty() || this->color_resolver_ == nullptr)
        return _DEFAULT_PALETTE;
    // Auto colors depend on the live label ordering, so the palette cannot
    // be cached on the shape. Memoize within a single paint pass instead:
    // many shapes share a few labels, so this collapses the per-shape
    // resolution into one lookup per distinct label per frame.
    auto palette = this->palette_cache_[label];
    if (palette.empty()) {
        palette = Palette::from_rgb(this->color_resolver_(label));
        this->palette_cache_[label] = palette;
    }
    return palette;
}

void Canvas::set_draft_palette(const Palette &palette) {
    this->draft_palette_ = palette;
}

void Canvas::highlight_vertex(int32_t index, const QString &mode) {
    this->highlight_ = VertexHighlight(index, mode);
    this->rotation_highlight_ = {};
}

void Canvas::highlight_rotation_point(
    int32_t index, const QString &mode      //: Literal["move", "near"]
) {
    this->rotation_highlight_ = VertexHighlight(index, mode);
    this->highlight_ = {};
}

void Canvas::clear_highlight_state() {
    this->highlight_ = {};
    this->rotation_highlight_ = {};
}

ShapeRenderContext Canvas::render_context(const TlShape &shape, const bool highlighted) {
    const bool selected = false; //selected_shapes_.contains(shape);
    return ShapeRenderContext{
        .scale_=this->scale_,
        .palette_=this->resolve_palette(shape.label_),
        .point_size_=this->point_size_,
        .point_type_=this->point_type_,
        .selected_=selected,
        .fill_=selected, // || shape is this->hovered_shape_,
        .highlight_=highlighted ? this->highlight_ : VertexHighlight(),
        .rotation_highlight_=highlighted ? this->rotation_highlight_ : VertexHighlight{},
        .show_label_=this->show_labels_,
    };
}

ShapeRenderContext Canvas::draft_render_context(
    bool selected,
    const bool fill,
    const VertexHighlight &highlight,
    const VertexHighlight &rotation_highlight
) {
    return ShapeRenderContext{
        .scale_=this->scale_,
        .palette_=this->draft_palette_,
        .point_size_=this->point_size_,
        .point_type_=this->point_type_,
        .selected_=selected,
        .fill_=fill,
        .highlight_=highlight,
        .rotation_highlight_=rotation_highlight,
    };
}

//@property
bool Canvas::is_drawing() const {
    return static_cast<bool>(this->current_);
}

//@property
QString Canvas::create_mode() const {
    return this->create_mode_;
}

//@create_mode.setter
void Canvas::create_mode(const QString &value) {
    if (!CreateMode.contains(value)) {
        throw std::invalid_argument("Unsupported create_mode: " + value.toStdString());
    }
    auto new_mode = value;
    if (new_mode == this->create_mode_)
        return;
    auto old_mode = this->create_mode_;
    // Update the mode before reconciling so any signals fired from a cancel
    // observe the new mode rather than the one being left behind.
    this->create_mode_ = new_mode;
    this->reconcile_partial_shape_on_mode_switch(
        old_mode, new_mode
    );
}

void Canvas::reconcile_partial_shape_on_mode_switch(
    const QString &old_mode, const QString &new_mode
) {
    if (!this->current_)
        return;
    if (
        this->current_.points_.size() != 1
        || SEED_INCOMPATIBLE_CREATE_MODES.contains(old_mode)
        || SEED_INCOMPATIBLE_CREATE_MODES.contains(new_mode)
    ) {
        this->cancel_current_shape();
        return;
    }
    // Shape type is identity, not state: construct fresh shapes rather than
    // mutating in place. The prior mode's _update_drawing_line left
    // _line.points as a valid [seed, cursor] pair — carry it forward so a
    // click before the next mouseMoveEvent extends at the real cursor.
    const auto seed_point = this->current_.points_[0];
    const auto seed_label = this->current_.point_labels_[0];
    this->current = DraftShape{.shape_type_=new_mode}.add_point(
        seed_point, seed_label
    );
    this->line_.shape_type_ = new_mode;
    this->update();
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

QList<TlShape> Canvas::shapes_from_ai_points(
    QList<QPointF> &points, QList<int32_t> &point_labels
) {
    //image: np.ndarray = _utils.img_qt_to_arr(img_qt=this->pixmap.toImage())
    //return this->_ai_assist_session.propose_shapes(
    //    image=image[:, :, :3],
    //    image_id=str(this->_pixmap_hash),
    //    points=np.array([[p.x(), p.y()] for p in points]),
    //    point_labels=np.array(point_labels),
    //    existing_shapes=this->shapes,
    //)
    return {};
}

void Canvas::report_inference_failure(const QString &error) {
    this->ai_inference_failed_ = true;
    SPDLOG_ERROR("AI inference failed");
    emit this->inference_failed("AI inference failed: " + error);
}

void Canvas::backup_shapes() {
    QList<TlShape> shapesBackup;
    std::ranges::for_each(this->shapes_, [&shapesBackup](const auto &shape) { shapesBackup.append(shape); });
    while (this->shape_backups_.length() > this->num_backups_) {
        this->shape_backups_.pop_front();
    }
    this->shape_backups_.append(shapesBackup);
}

//@property
bool Canvas::can_restore_shape() {
    // The latest entry on the backup stack mirrors the current state, so
    // at least one prior entry must exist for an undo to be meaningful.
    return this->shape_backups_.size() >= 2;
}

void Canvas::restore_last_shape() {
    // Undo coordinates with app.py::undo_shape_edit, app.py::load_shapes,
    // and Canvas::load_shapes; this method only adjusts the backup stack.
    if (!this->can_restore_shape())
        return;
    this->shape_backups_.pop_back();  // discard current state

    // load_shapes (called downstream by the application) will re-push
    // this entry as the new current state.
    auto shapesBackup = this->shape_backups_.back(); this->shape_backups_.pop_back();
    this->shapes_ = { shapesBackup };
    this->selected_shapes_ = {};
    for (auto &shape : this->shapes_) {
        shape.selected_ = false;
    }
    this->update();
}

void Canvas::enterEvent(QEnterEvent *event) {
    this->apply_cursor(this->cursor_);
    this->update_status({});
}

void Canvas::leaveEvent(QEvent *event) {
    if (this->set_highlight(
        None,
        None,
        None,
        None
    )) {
        this->update();
    }
    this->release_cursor();
    this->update_status({});
}

void Canvas::focusOutEvent(QFocusEvent *event) {
    this->release_cursor();
    this->update_status({});
}

void Canvas::set_editing(bool value) {
    this->mode_ = value ? CanvasMode::EDIT : CanvasMode::CREATE;
    if (this->mode_ == CanvasMode::EDIT) {
        // CREATE -> EDIT
        this->update();  // clear crosshair
    } else {
        // EDIT -> CREATE
        bool need_update = this->set_highlight(
            None,
            None,
            None,
            None
        );
        need_update |= this->deselect_shape();
        if (need_update) {
            this->update();
        }
    }
}

bool Canvas::set_highlight(
    int32_t hovered_shape,
    int32_t hovered_edge,
    int32_t hovered_vertex,
    int32_t hovered_rotation
) {
    int32_t previous_shape = this->hovered_shape_;
    bool need_update = hovered_shape != None;
    if (this->hovered_shape_ != None) {
        this->shapes_[this->hovered_shape_].highlightClear();
        need_update = true;
    }
    // NOTE: Store last highlighted for adding/removing points.
    this->last_hovered_shape_   = (
        hovered_shape  == None ? this->hovered_shape_  : hovered_shape
    );
    this->last_hovered_vertex_  = (
        hovered_vertex == None ? this->hovered_vertex_ : hovered_vertex
    );
    this->last_hovered_edge_    = (
        hovered_edge   == None ? this->hovered_edge_   : hovered_edge
    );
    this->hovered_shape_    = hovered_shape;
    this->hovered_vertex_   = hovered_vertex;
    this->hovered_edge_     = hovered_edge;
    this->hovered_rotation_ = hovered_rotation;
    return need_update;
}

bool Canvas::is_vertex_selected() const {
    return this->hovered_vertex_ != None;
}

bool Canvas::is_edge_selected() const {
    return this->hovered_edge_ != None;
}

bool Canvas::is_rotation_point_selected() const {
    return this->hovered_rotation_ != None;
}

void Canvas::update_status(const QList<QString> &extra_messages) {
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
    emit status_updated(" • " + messages.join(""));
}

QString Canvas::get_create_mode_message() {
    //assert self.drawing()
    bool is_new = !this->current_;
    if (create_mode_ == "ai_points_to_shape") {
        return tr(
            "Click points to include or Shift+Click to exclude."
            " Ctrl+LeftClick ends creation."
        );
    }
    if (create_mode_ == "ai_box_to_shape") {
        if (is_new)
            return tr("Click first corner of bbox for AI segmentation");
        else
            return tr("Click opposite corner to segment object");
    }
    if (create_mode_ == "line") {
        if (is_new)
            return tr("Click start point for line");
        else
            return tr("Click end point for line");
    }
    if (create_mode_ == "linestrip") {
        if (is_new)
            return tr("Click start point for linestrip");
        else
            return tr(
                "Click next point or finish by Ctrl/Cmd+Click for linestrip"
            );
    }
    if (create_mode_ == "circle") {
        if (is_new)
            return tr("Click center point for circle");
        else
            return tr("Click point on circumference for circle");
    }
    if (create_mode_ == "rectangle") {
        if (is_new)
            return tr("Click first corner for rectangle");
        else
            return tr("Click opposite corner for rectangle (Shift for square)");
    }
    if (create_mode_ == "oriented_rectangle") {
        if (is_new)
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
        pos = transform_point_widget_to_image(event->position());
    } catch (...) {
        return;
    }
    emit mouse_moved(pos);

    prev_move_point_ = pos;

    bool is_shift_pressed = event->modifiers() & Qt::ShiftModifier;

    if (is_dragging_) {
        apply_cursor(CursorRole::GRAB);
        QPointF delta = pos - dragging_start_pos_;
        emit scroll_request(static_cast<int>(delta.x()), Qt::Horizontal);
        emit scroll_request(static_cast<int>(delta.y()), Qt::Vertical);
        return;
    }

    // Polygon drawing.
    if (drawing()) {
        if (QKey{"ai_points_to_shape", "ai_box_to_shape"}.contains(create_mode_)) {
            line_.shape_type("points");
        } else {
            line_.shape_type(create_mode_);
        }

        apply_cursor(CursorRole::DRAW);
        if (!current_) {
            update();  // draw crosshair
            update_status({});
            return;
        }

        if (is_out_of_pixmap(pos)) {
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
            apply_cursor(CursorRole::HANDLE);
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
                prev_move_point_ = pos = snap_cursor_pos_for_square(  // override
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
        if (!selected_shapes_copy_.empty() && !prev_point_.isNull()) {
            apply_cursor(CursorRole::MOVE);
            drag_shapes(selected_shapes_copy_, pos);
            update();
        } else if (!selected_shapes_.empty()) {
            selected_shapes_copy_ = {};
            std::ranges::transform(selected_shapes_, std::back_inserter(selected_shapes_copy_), [this](int32_t idx){ return shapes_[idx]; });
            update();
        }
        update_status({});
        return;
    }

    // Polygon/Vertex moving.
    if (Qt::LeftButton & event->buttons()) {
        if (is_vertex_selected()) {
            //assert self.hVertex is not None
            //assert self.hShape is not None
            bounded_move_vertex(
                shapes_[hovered_shape_], hovered_vertex_, pos, is_shift_pressed
            );
            update();
            is_moving_shape_ = true;
        } else if (!selected_shapes_.empty() && !prev_point_.isNull()) {
            apply_cursor(CursorRole::MOVE);
            drag_shapes(shapes_, pos, selected_shapes_);
            update();
            is_moving_shape_ = true;
        }
        return;
    }

    // Just hovering over the canvas, 2 possibilities:
    // - Highlight shapes
    // - Highlight vertex
    // Update shape/vertex fill and tooltip value accordingly.
    QList<QString> status_messages;
    highlight_hover_shape(pos, status_messages);
    emit vertex_selected(hovered_vertex_ != None);
    update_status(status_messages);
}

void Canvas::dispatch_pointer_move(const QPointF &pos, QMouseEvent *event) {
    if (!this->pan_anchor_.isNull()) {
        this->advance_pan(event);
        return;
    }
    if (this->mode_ == CanvasMode::CREATE) {
        this->track_drawing_cursor(pos, event);
        return;
    }
    const auto buttons = event->buttons();
    if (buttons & Qt::MouseButton::RightButton) {
        this->continue_right_button_drag(pos);
        return;
    }
    if (buttons & Qt::MouseButton::LeftButton) {
        this->continue_left_button_drag(pos, event);
        return;
    }
    this->refresh_hover_state(pos);
}

void Canvas::advance_pan(QMouseEvent *event) {
    //assert self._pan_anchor is not None
    // Use screen coordinates so the anchor does not drift when our own
    // pan emit shifts the canvas widget under the scroll area — a
    // widget-local frame would oscillate and cause juggling.
    auto cursor = QPointF(this->mapToGlobal(event->position().toPoint()));
    auto step = cursor - this->pan_anchor_;
    this->pan_anchor_ = cursor;
    emit this->pan_request(QPoint(int(step.x()), int(step.y())));
}

void Canvas::track_drawing_cursor(QPointF pos, QMouseEvent *event) {
    auto desired_line_shape_type = CREATE_MODE_TO_SHAPE_TYPE[this->create_mode_];
    if (this->line_.shape_type_ != desired_line_shape_type)
        this->line_.shape_type_ = (
            desired_line_shape_type
        );
    this->apply_cursor(CursorRole::DRAW);
    if (!this->current_) {
        this->update();
        this->update_status({});
        return;
    }
    auto is_shift_pressed = bool(event->modifiers() & Qt::KeyboardModifier::ShiftModifier);
    pos = this->project_drawing_pos_into_image(pos);
    this->update_drawing_line(pos, is_shift_pressed);
    //assert len(self._line.points) == len(self._line.point_labels)
    this->update();
    this->update_status({});
}

QPointF Canvas::project_drawing_pos_into_image(const QPointF &pos) {
    auto current = this->current_;
    //assert current is not None
    if (this->create_mode_ == "oriented_rectangle" && current.points_.size() == 4) {
        // The second click only locks the orientation of the first edge,
        // not its length. The third-corner cursor drives parallelogram
        // completion through the diagonal anchor at points[0], so the
        // clicked points[1] slides along the locked axis as the cursor
        // changes the rectangle's extent in that direction.
        constexpr int32_t MOVING_CORNER_INDEX = 2;
        const auto new_corners = reproject_oriented_rectangle_corners(
            current.points_,
            MOVING_CORNER_INDEX,
            pos,
            this->pixmap_.size(),
            this->allow_out_of_bounds_points_
        );
        this->current_.points_ = new_corners;
        return this->current_.points_[MOVING_CORNER_INDEX];
    }
    if (this->should_constrain_to_pixmap(pos))
        return compute_intersection_edges_image(
            current[-1], pos, this->pixmap_.size()
        );
    if (!this->cursor_should_snap_to_polygon_origin(pos))
        return pos;
    this->apply_cursor(CursorRole::HANDLE);
    this->highlight_vertex(0, "near");
    return current[0];
}

bool Canvas::cursor_should_snap_to_polygon_origin(const QPointF &pos) {
    if (!this->snapping_)
        return false;
    if (this->create_mode_ != "polygon")
        return false;
    const auto current = this->current_;
    if (!current || current.points_.size() <= 1)
        return false;
    const auto origin = current.points_[0];
    return is_within_pick_threshold(
        pos,
        origin,
        this->scale_,
        this->epsilon_
    );
}

void Canvas::refresh_hover_state(QPointF pos) {
    QList<QString> status_messages;
    this->highlight_hover_shape(pos, status_messages);
    emit this->vertex_selected(this->hovered_vertex_ != None);
    emit this->edge_selected(this->hovered_edge_ != None);
    this->update_status(status_messages);
}

QPointF Canvas::update_drawing_line(QPointF pos, const bool is_shift_pressed) {
    auto current = this->current_;
    //assert current is not None
    const auto mode = this->create_mode_;
    const auto size = current.points_.size();
    if (POLYLINE_SHAPE_TYPES.contains(mode)) {
        this->line_.points_ = { current.points_[size-1], pos };
        this->line_.point_labels_ = { 1, 1 };
    } else if (mode == "ai_points_to_shape") {
        this->line_.points_ = { current.points_[size-1], pos };
        this->line_.point_labels_ = { current.point_labels_[size-1], is_shift_pressed ? 0 : 1 };
    } else if (QKey{"rectangle", "ai_box_to_shape"}.contains(mode)) {
        if (is_shift_pressed) {
            pos = snap_cursor_pos_for_square(
                pos, current.points_[0]
            );
            this->prev_move_point_ = pos;
        }
        this->line_.points_ = { current.points_[0], pos };
        this->line_.point_labels_ = { 1, 1 };
        this->line_.closed_ = true;
    } else if (mode == "oriented_rectangle") {
        auto origin = (
            current.points_.size() == 1 ? current.points_[0] : current.points_[1]
        );
        this->line_.points_ = { origin, pos };
        this->line_.point_labels_ = { 1, 1 };
    } else if (mode == "circle") {
        this->line_.points_ = { current.points_[0], pos };
        this->line_.point_labels_ = { 1, 1 };
    } else if (mode == "line") {
        this->line_.points_ = { current.points_[0], pos };
        this->line_.point_labels_ = { 1, 1 };
        this->line_.closed_ = true;
    } else if (mode == "point") {
        this->line_.points_ = { current.points_[0] };
        this->line_.point_labels_ = { 1 };
        this->line_.closed_ = true;
    }
    return pos;
}

void Canvas::continue_right_button_drag(QPointF pos) {
    if (!this->selected_shapes_copy_.empty()) {
        this->apply_cursor(CursorRole::MOVE);
        this->drag_shapes(this->selected_shapes_copy_, pos);
        this->update();
    } else if (!this->selected_shapes_.empty()) {
        std::ranges::for_each(this->selected_shapes_, [this](const auto &i) { this->selected_shapes_copy_.append(this->shapes_[i].copy()); });
        this->update();
    }
    this->update_status({});
}

void Canvas::continue_left_button_drag(
    const QPointF &pos, QMouseEvent *event
) {
    const auto is_shift_pressed = bool(event->modifiers() & Qt::KeyboardModifier::ShiftModifier);
    if (this->is_vertex_selected()) {
        this->drag_hovered_vertex(pos, is_shift_pressed);
        return;
    }
    if (this->is_rotation_point_selected()) {
        this->drag_hovered_rotation_point(pos);
        return;
    }
    if (this->selected_shapes_.empty())
        return;
    this->drag_selected_shapes(pos);
}

void Canvas::drag_hovered_vertex(QPointF pos, bool is_shift_pressed) {
    //assert self._hovered_vertex is not None
    //assert self.hovered_shape is not None
    ///this->bounded_move_vertex(
    ///    this->hovered_shape_,
    ///    this->hovered_vertex_,
    ///    pos,
    ///    is_shift_pressed
    ///);
    this->update();
    this->is_moving_shape_ = true;
}

void Canvas::drag_hovered_rotation_point(QPointF pos) {
    //assert self.hovered_shape is not None
    //assert len(self._rotation_original_points) > 0, (
    //    "_capture_rotation_anchors must be called before dragging"
    //)
    const auto current_angle = utils::direction_angle(
        this->rotation_center_, pos
    );
    //rotate(
    //    this->shapes_[this->hovered_shape_],
    //    this->rotation_center_,
    //    current_angle - this->rotation_initial_angle_,
    //    this->rotation_original_points_
    //);
    this->update();
    this->is_moving_shape_ = true;
}

void Canvas::capture_rotation_anchors() {
    //assert self.hovered_shape is not None
    //assert self._hovered_rotation is not None
    const auto handle = get_rotation_handle(
        this->shapes_[this->hovered_shape_], this->hovered_rotation_
    );
    this->rotation_center_ = oriented_rectangle_center(
        this->shapes_[this->hovered_shape_]
    );
    this->rotation_initial_angle_ = utils::direction_angle(
        this->rotation_center_, handle
    );
    this->rotation_original_points_ = this->shapes_[this->hovered_shape_].points_;
}

void Canvas::drag_selected_shapes(QPointF pos) {
    this->apply_cursor(CursorRole::MOVE);
    this->drag_shapes(this->shapes_, pos, this->selected_shapes_);
    this->update();
    this->is_moving_shape_ = true;
}

void Canvas::highlight_hover_shape(const QPointF &pos, QList<QString> &status_messages) {
    const HitTarget target = find_hover_target(
        this->shapes_,
        pos,
        this->scale_,
        this->epsilon_,
        this->point_size_,
        this->shapes_[this->hovered_shape_]
    );

    if (!target) {
        this->release_cursor();
        if (this->set_highlight(
            None,
            None,
            None,
            None
        ))
            this->update();
        return;
    }
    if (target.kind == HitKind::VERTEX) {
        //assert target.index is not None
        //this->set_highlight(
        //    target.shape,
        //    None,
        //    target.index,
        //    None
        //);
        this->highlight_vertex(target.index, "move");
        this->apply_cursor(CursorRole::HANDLE);
        status_messages.append(tr("Click & drag to move point"));
        if (target.shape.can_remove_point())
            status_messages.append(tr("ALT + SHIFT + Click to delete point"));
        this->update();
        return;
    }
    if (target.kind == HitKind::ROTATION_HANDLE) {
        //assert target.index is not None
        //this->set_highlight(
        //    target.shape,
        //    None,
        //    None,
        //    target.index
        //);
        this->highlight_rotation_point(target.index, "move");
        this->apply_cursor(CursorRole::HANDLE);
        status_messages.append(tr("Click & drag to rotate the shape"));
        this->update();
        return;
    }
    if (target.kind == HitKind::EDGE) {
        //assert target.index is not None
        //this->set_highlight(
        //    target.shape,
        //    target.index,
        //    None,
        //    None
        //);
        this->apply_cursor(CursorRole::HANDLE);
        status_messages.append(tr("ALT + Click to create point on shape"));
        this->update();
        return;
    }
    if (target.kind == HitKind::BODY) {
        //this->set_highlight(
        //    target.shape,
        //    None,
        //    None,
        //    None
        //);
        status_messages.append(
            {
                tr("Click & drag to move shape"),
                tr("Right-click & drag to copy shape"),
            }
        );
        this->apply_cursor(CursorRole::GRAB);
        this->update();
        return;
    }
    //typing.assert_never(target.kind);
}

void Canvas::_highlight_hover_shape(const QPointF &pos, QList<QString> &status_messages) {
    std::vector<int32_t> ordered_shapes;
    if (hovered_shape_ != None) { ordered_shapes.push_back(hovered_shape_); }
    for (int32_t idx = shapes_.size() - 1; idx >= 0; --idx) { if (isVisible(shapes_[idx]) && idx != hovered_shape_) { ordered_shapes.push_back(idx); } }
    //ordered_shapes: list[Shape] = ([this->hShape] if this->hShape else []) + [
    //    s for s in reversed(this->shapes) if this->isVisible(s) and s != this->hShape
    //]

    for (auto [idx, shape] : ordered_shapes | std::views::transform([this](int32_t i) { return std::make_pair(i, shapes_[i]); })) {
        auto index = shape.nearestVertex(pos, epsilon_);
        if (index != None) {
            set_highlight(idx, None, index, None);
            shape.highlightVertex(index, shape.MOVE_VERTEX);
            apply_cursor(CursorRole::HANDLE);
            status_messages.push_back(tr("Click & drag to move point"));
            if (shape.can_remove_point())
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
            set_highlight(idx, index_edge, None, None);
            apply_cursor(CursorRole::HANDLE);
            status_messages.push_back(tr("ALT + Click to create point on shape"));
            this->update();
            return;
        }
    }

    for (auto [idx, shape] : ordered_shapes | std::views::transform([this](int32_t i) { return std::make_pair(i, shapes_[i]); })) {
        if (shape.containsPoint(pos)) {
            set_highlight(idx, None, None, None);
            status_messages.push_back(
                tr("Click & drag to move shape")
            );
            status_messages.push_back(
                tr("Right-click & drag to copy shape")
            );
            apply_cursor(CursorRole::GRAB);
            this->update();
            return;
        }
    }

    release_cursor();
    if (set_highlight(None, None, None, None)) {
        this->update();
    }
}

void Canvas::add_point_to_edge() {
    auto shape = this->last_hovered_shape_;
    auto index = this->last_hovered_edge_;
    auto point = this->prev_move_point_;
    if (shape == None || index == None || point.isNull()) {
        return;
    }
    const auto saved = this->shapes_[shape];
    this->shapes_[shape].insert_point(index, point);
    this->shapes_[shape].highlightVertex(index, TlShape::MOVE_VERTEX);
    this->hovered_shape_ = shape;
    this->hovered_vertex_ = index;
    this->hovered_edge_ = None;
    this->is_moving_shape_ = true;
    // Repaint now; otherwise the edit is invisible until the next mouse move.
    this->update();
}

bool Canvas::remove_selected_point() {
    auto shape = this->last_hovered_shape_;
    auto index = this->last_hovered_vertex_;
    if (shape == None || index == None) {
        return false;
    }
    this->shapes_[shape].remove_point(index);
    this->shapes_[shape].highlightClear();
    // Drop the hovered vertex and selection so the press that deleted the
    // point cannot also drag the adjacent vertex (#968) or the whole shape.
    this->hovered_shape_ = shape;
    this->last_hovered_vertex_ = None;
    this->is_moving_shape_ = true;  // Save changes
    // Repaint now; otherwise the edit is invisible until the next mouse move.
    this->update();
    return true;
}

void Canvas::mousePressEvent(QMouseEvent *event) {
    QPointF pos = transform_point_widget_to_image(event->position());

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
                        lock_oriented_rectangle_first_edge(this->current);
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
            } else if (!is_out_of_pixmap(pos)) {
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
                    emit drawing_polygon(true);
                    update();
                }
            }
        } else if (editing()) {
            if (is_edge_selected() && event->modifiers() == Qt::AltModifier) {
                add_point_to_edge();       // 增加节点
            } else if (is_vertex_selected() && event->modifiers() == (
                Qt::AltModifier | Qt::ShiftModifier
            )) {
                remove_selected_point();  // 删除节点
            }
            auto group_mode = event->modifiers() == Qt::ControlModifier;
            select_shape_point(pos, group_mode);
            prev_point_ = pos;
            update();
        }
    } else if (event->button() == Qt::RightButton && editing()) {
        auto group_mode = event->modifiers() == Qt::ControlModifier;
        if (selected_shapes_.empty() || (
             hovered_shape_ != None && !selected_shapes_.contains(hovered_shape_)
        )) {
            select_shape_point(pos, group_mode);
            update();
        }
        prev_point_ = pos;
    } else if (event->button() == Qt::MiddleButton && is_dragging_enabled_) {
        apply_cursor(CursorRole::GRAB);
        dragging_start_pos_ = pos;
        is_dragging_ = true;
    }
    this->update_status({});
}

void Canvas::dispatch_pointer_press(QPointF pos, QMouseEvent *event) {
    auto button = event->button();
    if (button == Qt::MouseButton::LeftButton) {
        this->press_left(pos, event);
        return;
    }
    if (button == Qt::MouseButton::RightButton && this->mode_ == CanvasMode::EDIT) {
        this->press_right(pos, event);
        return;
    }
    if (
        button == Qt::MouseButton::MiddleButton
        && this->is_image_overflowing_viewport()
    )
        this->begin_pan(event);
}

void Canvas::press_left(const QPointF &pos, QMouseEvent *event) {
    const auto is_shift_pressed = bool(event->modifiers() & Qt::KeyboardModifier::ShiftModifier);
    if (this->mode_ == CanvasMode::CREATE) {
        this->press_left_while_drawing(
            pos, event, is_shift_pressed
        );
        return;
    }
    if (this->mode_ == CanvasMode::EDIT)
        this->press_left_while_editing(pos, event);
}

void Canvas::press_left_while_drawing(
    const QPointF &pos,
    QMouseEvent *event,
    const bool is_shift_pressed
) {
    if (this->current_) {
        this->extend_current_shape(this->current, event);
        return;
    }
    if (this->should_constrain_to_pixmap(pos))
        return;
    this->start_new_shape(pos, event, is_shift_pressed);
}

void Canvas::extend_current_shape(
    DraftShape current, QMouseEvent *event
) {
    const auto mode = this->create_mode_;
    const auto modifiers = event->modifiers();
    if (mode == "polygon") {
        current = current.add_point(this->line_.points_[1], true);
        this->current = current;
        this->line_.points_  = { current.points_[current.points_.size()-1] };
        this->line_.points_.append(this->line_.points_.begin() + 1, this->line_.points_.end());
        if (current.closed_)
            this->finalize();
    } else if (mode == "oriented_rectangle") {
        if (current.points_.size() == 4) {
            this->finalize();
        } else {
            //assert len(current.points) == 1
            this->lock_oriented_rectangle_first_edge(current);
        }
    } else if (QKey{"rectangle", "circle", "line", "ai_box_to_shape"}.contains(mode)) {
       //assert len(current.points) == 1
       this->current_.points_ = this->line_.points_;
       this->finalize();
    } else if (mode == "linestrip") {
        current = current.add_point(this->line_.points_[1]);
        this->current = current;
        this->line_.points_  = { current.points_[current.points_.size()-1] };
        this->line_.points_.append(this->line_.points_.begin() + 1, this->line_.points_.end());
        if (modifiers == Qt::KeyboardModifier::ControlModifier)
            this->finalize();
    } else if (mode == "ai_points_to_shape") {
        current = current.add_point(
            this->line_.points_[1], this->line_.point_labels_[1]
        );
        this->current = current;
        this->line_.points_  = { current.points_[current.points_.size()-1] };
        this->line_.points_.append(this->line_.points_.begin() + 1, this->line_.points_.end());
        this->line_.point_labels_ = { current.point_labels_[current.point_labels_.size()-1] };
        this->line_.point_labels_.append(this->line_.point_labels_.begin() + 1, this->line_.point_labels_.end());
        if (modifiers & Qt::KeyboardModifier::ControlModifier)
            this->finalize();
    }
}

void Canvas::lock_oriented_rectangle_first_edge(const DraftShape &current) {
    auto first_corner = this->line_.points_[0];
    auto second_corner = this->line_.points_[1];
    this->current = current;
    this->current.points_ = {
        first_corner,
        second_corner,
        second_corner,
        first_corner,
    };
    this->current.point_labels_ = {1, 1, 1, 1};
    QList<QPointF> points(this->line_.points_.begin() + 1, this->line_.points_.end());
    this->line_.points_ = { second_corner };
    this->line_.points_.append(points);
}

void Canvas::unlock_oriented_rectangle_first_edge(const DraftShape &current) {
    auto anchor = current.points_[0];
    this->current.points_ = { anchor };
    this->current.point_labels_ = { current.point_labels_[0] };
    this->line_.points_ = { anchor, anchor };
}

void Canvas::start_new_shape(
    const QPointF &pos,
    QMouseEvent *event,
    bool is_shift_pressed
) {
    const auto mode = this->create_mode_;
    if (QKey{"ai_points_to_shape", "ai_box_to_shape"}.contains(mode) && !download_ai_model(
        this->get_ai_model_name(), this
    ))
        return;

    this->current = DraftShape{
        .shape_type_=CREATE_MODE_TO_SHAPE_TYPE[mode]
    }.add_point(pos, is_shift_pressed ? 0 : 1);

    if (mode == "point") {
        this->finalize();
        return;
    }
    if (
        mode == "ai_points_to_shape"
        && event->modifiers() & Qt::KeyboardModifier::ControlModifier
    ) {
        this->finalize();
        return;
    }

    this->line_.points_ = { pos, pos };
    this->line_.point_labels_ = (
        mode == "ai_points_to_shape" && is_shift_pressed ? QList{ 0, 0 } :  QList{ 1, 1 }
    );
    emit this->drawing_polygon(true);
    this->update();
}

void Canvas::press_left_while_editing(const QPointF &pos, QMouseEvent *event) {
    const auto modifiers = event->modifiers();
    if (this->maybe_modify_polygon_topology(modifiers))
        // remove_selected_point already repainted; just consume the press.
        return;
    if (this->is_rotation_point_selected())
        this->capture_rotation_anchors();
    this->select_shape_point(
        pos,
        modifiers == Qt::KeyboardModifier::ControlModifier
    );
    this->prev_point_ = pos;
    this->update();
}

bool Canvas::maybe_modify_polygon_topology(Qt::KeyboardModifiers modifiers) {
    // Returns True only when the press is consumed as a terminal edit (a point
    // removal), so the caller skips point selection and starts no drag. Adding
    // a point intentionally falls through so the new vertex can be dragged.
    if (this->is_edge_selected() && modifiers == Qt::KeyboardModifier::AltModifier) {
        this->add_point_to_edge();
        return false;
    }
    if (this->is_vertex_selected() && modifiers == (
        Qt::KeyboardModifier::AltModifier | Qt::KeyboardModifier::ShiftModifier
    ))
        return this->remove_selected_point();
    return false;
}

void Canvas::press_right(QPointF pos, QMouseEvent *event) {
    if (should_reselect_on_right_press(
        this->selected_shapes_, this->hovered_shape_
    )) {
        this->select_shape_point(
            pos,
            event->modifiers()
            == Qt::KeyboardModifier::ControlModifier
        );
        this->update();
    }
    this->prev_point_ = pos;
}

void Canvas::begin_pan(QMouseEvent *event) {
    this->apply_cursor(CursorRole::GRAB);
    this->pan_anchor_ = this->mapToGlobal(event->position().toPoint());
}

void Canvas::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::RightButton) {
        auto *menu = selected_shapes_copy_.size() > 0 ? context_menus_.with_selection_ : context_menus_.without_selection_;
        release_cursor();
        if (!menu->exec(mapToGlobal(event->pos())) && !selected_shapes_copy_.empty()) {
            // Cancel the move by deleting the shadow copy.
            selected_shapes_copy_ = {};
            repaint();
        }
    } else if (event->button() == Qt::LeftButton) {
        if (editing()) {
            if (
                hovered_shape_ != None &&
                hovered_shape_is_selected_ &&
                !is_moving_shape_
            ) {
                QList<int32_t> selected_shapes;
                std::ranges::for_each(selected_shapes_, [&](auto &x){ if (x != hovered_shape_) { selected_shapes.push_back(x); } });
                emit selection_changed(selected_shapes);
            }
        }
    } else if (event->button() == Qt::MiddleButton) {
        is_dragging_ = false;
        release_cursor();
    }

    if (is_moving_shape_ && hovered_shape_ != None) {
        auto index = hovered_shape_;
        if (shape_backups_.back()[index].points_ != shapes_[index].points_) {
            backup_shapes();
            emit shape_moved();
        }

        is_moving_shape_ = false;
    }
    update_status({});
}

void Canvas::dispatch_pointer_release(QMouseEvent *event) {
    const auto button = event->button();
    if (button == Qt::MouseButton::RightButton) {
        this->release_right(event);
        return;
    }
    if (button == Qt::MouseButton::LeftButton) {
        this->release_left();
        return;
    }
    if (button == Qt::MouseButton::MiddleButton)
        this->finish_pan();
}

void Canvas::release_right(QMouseEvent *event) {
    QAction *triggered{};
    auto *const menu = this->context_menus_.menu_for(
        !this->selected_shapes_copy_.empty()
    );
    this->release_cursor();
    this->context_menu_origin_ = this->mapToGlobal(event->position().toPoint());
    try {
        triggered = menu->exec(this->context_menu_origin_);  // type: ignore
    } catch (...) {}
    this->context_menu_origin_ = QPoint();
    if (triggered)
        return;
    if (this->selected_shapes_copy_.empty())
        return;
    this->selected_shapes_copy_.clear();
    this->update();
}

void Canvas::release_left() {
    if (this->mode_ != CanvasMode::EDIT)
        return;
    if (this->hovered_shape_ == None)
        return;
    if (!this->hovered_shape_is_selected_)
        return;
    if (this->is_moving_shape_)
        return;
    QList<int32_t> selected_shapes;
    std::ranges::for_each(this->selected_shapes_, [&](auto &s) { if (s != this->hovered_shape_) selected_shapes.push_back(s); });
    emit this->selection_changed(selected_shapes);
}

void Canvas::finish_pan() {
    // Reset state and cursor unconditionally so a stray middle-button
    // release can never leave a grab cursor stuck on screen.
    this->pan_anchor_ = {};
    this->release_cursor();
}

bool Canvas::is_image_overflowing_viewport() {
    if (this->pixmap_.isNull())
        return false;
    auto *const viewport = this->scroll_viewport();
    if (viewport == nullptr)
        return false;
    const auto scaled_w = this->pixmap_.width() * this->scale_;
    const auto scaled_h = this->pixmap_.height() * this->scale_;
    return scaled_w > viewport->width() || scaled_h > viewport->height();
}

QWidget *Canvas::scroll_viewport() {
    // Walk up the parent chain to the enclosing scroll area and return
    // its viewport. Returning None when no scroll area is found lets
    // callers degrade gracefully if the canvas is reparented (e.g. into
    // a splitter or a test harness).
    QWidget *node = this->parentWidget();
    while (node != nullptr) {
        if (qobject_cast<QAbstractScrollArea *>(node))
            return qobject_cast<QAbstractScrollArea *>(node)->viewport();
        node = node->parentWidget();
    }
    return nullptr;
}

void Canvas::commit_pending_shape_move() {
    auto moved = pick_pending_moved_shape(
        this->is_moving_shape_,
        this->shapes_[this->hovered_shape_],
        this->shapes_
    );
    if (!moved)
        return;
    auto index = this->shapes_.indexOf(moved);
    if (
        this->shape_backups_[-1][index].points_ != this->shapes_[index].points_
    ) {
        this->backup_shapes();
        emit this->shape_moved();
    }
    this->is_moving_shape_ = false;
}

bool Canvas::end_move(bool copy) {
    assert(!selectedShapes_.empty() && !selectedShapesCopy_.empty());
    assert(selectedShapesCopy_.size() == selectedShapes_.size());
    if (copy) {
        for (const auto &&[i, shape] : selected_shapes_copy_ | std::views::enumerate) {
            shapes_.append(shape);
            shapes_[selected_shapes_[i]].selected_ = false;
            selected_shapes_[i] = shapes_.count() - 1;
        }
    } else {
        for (const auto &&[i, shape] : selected_shapes_copy_ | std::views::enumerate) {
            shapes_[selected_shapes_[i]].points_ = shape.points_;
        }
    }
    this->selected_shapes_copy_ = {};
    this->update();
    this->backup_shapes();
    return true;
}

void Canvas::apply_copy_move() {
    for (auto &&[i, clone] : this->selected_shapes_copy_ | std::views::enumerate) {
        this->shapes_.append(clone);
        this->selected_shapes_.push_back(i);
    }
}

void Canvas::apply_in_place_move() {
    //for (auto &&[original, clone] :  std::views::zip(this->selected_shapes_, this->selected_shapes_copy_))
    //    original.points_ = clone.points_;
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

void Canvas::select_shapes(const QList<TlShape> &shapes) {
    setHiding();

    QList<int32_t> indexes;
    std::ranges::for_each(shapes, [&](auto &shape) { indexes.push_back(shapes_.indexOf(shape)); });
    emit selection_changed(indexes);
    update();
}

void Canvas::select_shape_point(
    const QPointF &point, bool multiple_selection_mode
) {
    // Select the first shape created which contains this point.
    if (hovered_vertex_ != None) {
        //assert this->hShape is not None
        shapes_[hovered_shape_].highlightVertex(hovered_vertex_, TlShape::MOVE_VERTEX);
    } else {
        //shape: Shape
        for (int32_t idx = shapes_.size() - 1; idx >= 0; --idx) {
            auto &shape = shapes_[idx];
            if (isVisible(shape) && shape.containsPoint(point)) {
                setHiding();
                if (!selected_shapes_.contains(idx)) {
                    if (multiple_selection_mode) {
                        auto select_shapes = selected_shapes_;
                        select_shapes.append(idx);
                        emit selection_changed(select_shapes);
                    } else {
                        emit selection_changed({idx});
                    }
                    hovered_shape_is_selected_ = false;
                } else {
                    hovered_shape_is_selected_ = true;
                }
                calculateOffsets(point);
                return;
            }
        }
    }
    if (deselect_shape())
        update();
}

TlShape Canvas::find_shape_at_point(QPointF point) {
    //query = np.array([point.x(), point.y()])
    for (auto &shape : this->shapes_ | std::views::reverse)
        if (shape.visible_ && is_hit_by_point(
            shape,
            point,
            this->scale_,
            this->point_size_,
            this->epsilon_
        ))
            return shape;
    return {};
}

void Canvas::record_drag_anchor(QPointF click) {
    if (this->selected_shapes_.empty()) {
        this->drag_anchor_ = { QPointF(), QRectF() };
        return;
    }
    auto bounds = shape_bounds(this->shapes_[this->selected_shapes_[0]]);
    for (auto i = 1; i < this->selected_shapes_.size(); ++i)
        bounds = bounds.united(shape_bounds(this->shapes_[i]));
    this->drag_anchor_ = { bounds.topLeft() - click, bounds };
}

void Canvas::bounded_move_vertex(
    TlShape &shape,
    int32_t vertex_index,
    QPointF pos,
    bool is_shift_pressed
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
        bounded_move_oriented_rectangle_vertex(
            shape, vertex_index, pos
        );
        return;
    }

    if (is_out_of_pixmap(pos)) {
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

void Canvas::bounded_move_oriented_rectangle_vertex(
    TlShape &shape, int32_t vertex_index, const QPointF &pos
) {
    //assert len(shape.points) == 4
    //corners = tuple(QPointF(*point) for point in shape.points)
    auto new_corners = reproject_oriented_rectangle_corners(
        shape.points_,
        vertex_index,
        pos,
        this->pixmap_.size(),
        this->allow_out_of_bounds_points_
    );
    for (auto &&[i, corner] : new_corners | std::views::enumerate)
        shape.move_vertex(i, corner);
}

bool Canvas::drag_shapes(QList<TlShape> &shapes, QPointF pos, const QList<int32_t> &indexes) {
    if (is_out_of_pixmap(pos)) {
        return false;
    }
    auto tl = pos + offsets_[0];
    if (is_out_of_pixmap(tl)) {
        pos -= QPointF(std::min(0., tl.x()), std::min(0., tl.y()));
    }
    auto br = pos + offsets_[1];
    if (is_out_of_pixmap(br)) {
        pos += QPointF(
            std::min(0., pixmap_.width() - br.x()),
            std::min(0., pixmap_.height() - br.y())
        );
    }

    const auto dp = pos - prev_point_;
    if (dp.isNull())
        return false;

    if (indexes.empty()) {
        QList<int32_t> indexes(selected_shapes_copy_.size());
        std::iota(indexes.begin(), indexes.end(), 0); // 使用 std::iota 填充序列, 从0开始
    } else {

    }

    for (const auto &idx : indexes) {
        shapes[idx].moveBy(dp);
    }
    prev_point_ = pos;
    return true;
}

bool Canvas::deselect_shape() {
    bool need_update = false;
    if (!selected_shapes_.empty()) {
        setHiding(false);
        emit selection_changed({});
        hovered_shape_is_selected_ = false;
        need_update = true;
    }
    return need_update;
}

QList<TlShape> Canvas::delete_selected() {
    QList<TlShape> deleted_shapes = {};
    if (!selected_shapes_.empty()) {
        std::ranges::for_each(selected_shapes_, [&](auto idx){ deleted_shapes.push_back(shapes_[idx]); });
        for (auto &shape : deleted_shapes) {
            SPDLOG_INFO("deleteSelected, removeOne: {}", shape.label_);
            shapes_.removeOne(shape);
        }
        backup_shapes();
        selected_shapes_ = {};
        update();
    }
    return deleted_shapes;
}

void Canvas::delete_shape(const TlShape &shape) {
    if (const auto idx = shapes_.indexOf(shape); selected_shapes_.count(idx)) {
        selected_shapes_.removeOne(idx);
    }
    if (shapes_.contains(shape)) {
        shapes_.removeOne(shape);
    }
    backup_shapes();
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
    p.translate(compute_image_origin_offset());

    p.drawPixmap(0, 0, pixmap_);

    p.scale(1 / scale_, 1 / scale_);

    // draw crosshair
    if (
        crosshair_[create_mode_] &&
        drawing() &&
        !prev_move_point_.isNull() &&
        !is_out_of_pixmap(prev_move_point_))
    {
        p.setPen(QColor(0, 0, 0));
        p.drawLine(
            0,
            prev_move_point_.y() * scale_,
            pixmap_.width() * scale_ - 1,
            prev_move_point_.y() * scale_
        );
        p.drawLine(
            prev_move_point_.x() * scale_,
            0,
            prev_move_point_.x() * scale_,
            pixmap_.height() * scale_ - 1
        );
    }

    TlShape::scale_ = scale_;
    for (auto &&[idx, shape] : shapes_ | std::views::enumerate) {
        if ((shape.selected_ || !hideBackround_) && isVisible(shape)) {
            shape.fill_ = (shape.selected_ || idx == hovered_shape_);
            shape.paint(p);
        }
    }

    if (current_) {
        current_.paint(p);
        assert(line_.points_.size() == line_.point_labels_.size());
        line_.paint(p);
    }

    if (!selected_shapes_copy_.empty()) {
        for (auto &s : selected_shapes_copy_) {
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
    this->palette_cache_.clear();
    auto &painter = this->painter_;
    painter.begin(this);
    try {
        this->setup_world_transform(painter);
        for (const auto &layer : this->render_layers())
            layer(painter);
    } catch (...) {}
    painter.end();
}

void Canvas::setup_world_transform(QPainter &painter) {
    for (auto &hint : {
        QPainter::RenderHint::Antialiasing,
        QPainter::RenderHint::SmoothPixmapTransform
    })
        painter.setRenderHint(hint);
    painter.translate(this->compute_image_origin_offset() * this->scale_);
}

QList<std::function<void(QPainter &)>> Canvas::render_layers() {
    // Order is z-order, back-to-front.
    return {
        [this](QPainter &p) { this->draw_pixmap_layer(p); },
        [this](QPainter &p) { this->draw_crosshair_layer(p); },
        [this](QPainter &p) { this->draw_committed_shapes_layer(p); },
        [this](QPainter &p) { this->draw_active_shape_layer(p); },
        [this](QPainter &p) { this->draw_drag_copy_layer(p); },
        [this](QPainter &p) { this->draw_preview_overlay_layer(p); }
    };
}

void Canvas::draw_pixmap_layer(QPainter &painter) {
    auto target = QRectF(
        0.0,
        0.0,
        this->pixmap_.width() * this->scale_,
        this->pixmap_.height() * this->scale_
    );
    painter.drawPixmap(target, this->pixmap_, QRectF(this->pixmap_.rect()));
}

void Canvas::draw_crosshair_layer(QPainter &painter) {
    auto cursor = this->prev_move_point_;
    if (!this->should_draw_crosshair(cursor))
        return;
    //assert cursor is not None;
    painter.setPen(this->palette().color(QPalette::ColorRole::WindowText));
    auto cx = int(cursor.x() * this->scale_);
    auto cy = int(cursor.y() * this->scale_);
    int32_t left, right, top, bottom;
    if (this->allow_out_of_bounds_points_) {
        // The cursor may be in the margin around the image, so span the whole
        // viewport instead of stopping the lines at the image edge.
        auto offset = this->compute_image_origin_offset() * this->scale_;
        auto area = this->size();
        left = int(-offset.x());
        top = int(-offset.y());
        right = int(-offset.x() + area.width());
        bottom = int(-offset.y() + area.height());
    } else {
        left = top = 0;
        right = int(this->pixmap_.width() * this->scale_) - 1;
        bottom = int(this->pixmap_.height() * this->scale_) - 1;
    }
    painter.drawLine(left, cy, right, cy);
    painter.drawLine(cx, top, cx, bottom);
}

bool Canvas::should_draw_crosshair(QPointF &cursor) {
    if (this->mode_ != CanvasMode::CREATE)
        return false;
    if (!this->crosshair_[this->create_mode_])
        return false;
    if (cursor == QPointF())
        return false;
    return !this->should_constrain_to_pixmap(cursor);
}

void Canvas::draw_committed_shapes_layer(QPainter &painter) {
    for (auto &&[idx, shape] : this->shapes_ | std::views::enumerate) {
        if (!shape.visible_)
            continue;
        auto context = this->render_context(
            shape, idx == this->hovered_shape_
        );
        render_shape(painter, shape, context);
    }
}

void Canvas::draw_active_shape_layer(QPainter &painter) {
    if (!this->current_)
        return;
    //assert len(this->_line.points) == len(this->_line.point_labels);
    this->render_draft(painter, this->current, true);
    this->render_draft(painter, this->line, false);
}

void Canvas::draw_drag_copy_layer(QPainter &painter) {
    for (auto &copy_shape : this->selected_shapes_copy_) {
        auto context = ShapeRenderContext{
            .scale_=this->scale_,
            .palette_=this->resolve_palette(copy_shape.label_),
            .point_size_=this->point_size_,
            .point_type_=this->point_type_,
            .selected_=true,
            .fill_=true,
            .highlight_={},
            .rotation_highlight_={},
            .show_label_=this->show_labels_
        };
        render_shape(painter, copy_shape, context);
    }
}

void Canvas::draw_preview_overlay_layer(QPainter &painter) {
    auto preview = this->build_preview_shape();
    if (!preview)
        return;
    const auto context = this->draft_render_context(
        this->fill_drawing_,
        this->fill_drawing_,
        {},
        {}
    );
    render_shape(painter, preview, context);
}

void Canvas::render_draft(
    QPainter &painter, DraftShape draft, bool highlighted
) {
    const auto shape = draft_to_shape(draft);
    const auto context = this->draft_render_context(
        false,
        false,
        highlighted ? this->highlight_ : VertexHighlight{},
        highlighted ? this->rotation_highlight_ : VertexHighlight{}
    );
    render_shape(painter, shape, context);
}

TlShape Canvas::build_preview_shape() {
    if (!this->current_)
        return {};
    if (this->create_mode_ == "polygon")
        return this->build_polygon_preview(this->current);
    if (this->create_mode_ == "ai_points_to_shape")
        return this->build_ai_points_preview(this->current);
    return {};
}

TlShape Canvas::build_polygon_preview(DraftShape current) {
    auto preview = current;
    if (this->fill_drawing_ && preview.points_.size() >= 2)
        preview = preview.add_point(this->line_.points_[1], true);
    return draft_to_shape(preview);
}

TlShape Canvas::build_ai_points_preview(DraftShape current) {
    auto preview = current.add_point(
        this->line_.points_[1],
        this->line_.point_labels_[1]
    );
    QList<TlShape> ai_shapes;
    try {
        ai_shapes = this->shapes_from_ai_points(
            preview.points_,
            preview.point_labels_
        );
    } catch (const std::exception &e) {
        // This runs inside paintEvent on every repaint, so a persistently
        // failing model would report on every frame. Report once; a later
        // success re-arms the report.
        if (!this->ai_inference_failed_)
            this->report_inference_failure(e.what());
        return draft_to_shape(preview);
    }
    this->ai_inference_failed_ = false;
    if (!ai_shapes.empty())
        return ai_shapes[0];
    return draft_to_shape(preview);
}

QPointF Canvas::transform_point_widget_to_image(QPointF point) {
    const auto origin = compute_image_origin_offset();
    const auto image_x = point.x() / this->scale_ - origin.x();
    const auto image_y = point.y() / this->scale_ - origin.y();
    return QPointF(image_x, image_y);
}

QPointF Canvas::compute_image_origin_offset() {
    auto s = scale_;
    auto area = QWidget::size();
    float w = pixmap_.width() * s, h = pixmap_.height() * s;
    float aw = area.width(), ah = area.height();
    float x = (aw > w) ? ((aw - w) / (2 * s)) : 0.;
    float y = (ah > h) ? ((ah - h) / (2 * s)) : 0.;
    return QPointF(x, y);
}

bool Canvas::is_out_of_pixmap(const QPointF &p) {
    return is_out_of_image(p, this->pixmap_.size());
}

bool Canvas::should_constrain_to_pixmap(QPointF point) {
    return !this->allow_out_of_bounds_points_ && this->is_out_of_pixmap(point);
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
    backup_shapes();
    current_.clear();
    ai_assist_points_.clear();
    ai_assist_shapes_.clear();
    setHiding(false);
    emit new_shape();
    update();
}

//def _build_new_shapes_from_ai_inference(self) -> list[Shape]:
//    assert this->_current is not None
//    if this->create_mode == "ai_points_to_shape":
//        return this->_shapes_from_ai_points(
//            points=this->_current.points,
//            point_labels=this->_current.point_labels,
//        )
//    if this->create_mode == "ai_box_to_shape":
//        # point_labels: 2=box corner, 3=opposite box corner (SAM convention)
//        return this->_shapes_from_ai_points(
//            points=_normalize_bbox_points(bbox_points=this->_current.points),
//            point_labels=[2, 3],
//        )
//    raise AssertionError(f"unreachable: {this->create_mode}")

void Canvas::reset_after_shape_creation() {
    this->current_.clear();
    emit new_shape();
    this->update();
}

void Canvas::cancel_current_shape() {
    this->current_.clear();
    emit drawing_polygon(false);
    this->update();
}

// Required by QScrollArea: it queries these to compute the
// scrollable viewport whenever adjustSize() is called.
QSize Canvas::compute_canvas_size() {
    if (this->pixmap_.isNull())
        return QWidget::minimumSizeHint();
    const auto scaled_w = static_cast<int>(this->pixmap_.width() * this->scale_);
    const auto scaled_h = static_cast<int>(this->pixmap_.height() * this->scale_);
    const auto viewport = this->scroll_viewport();
    if (viewport == nullptr)
        return {scaled_w, scaled_h};
    const auto slack_w = compute_overscroll_slack(scaled_w, viewport->width());;
    const auto slack_h = compute_overscroll_slack(scaled_h, viewport->height());
    return {scaled_w + slack_w, scaled_h + slack_h};
}

QSize Canvas::sizeHint() const {
    return minimumSizeHint();
}

QSize Canvas::minimumSizeHint() const {
    if (pixmap_.isNull()) {
        return QWidget::minimumSizeHint();
    }

    QSize min_size = scale_ * pixmap_.size();
    if (is_dragging_enabled_) {
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
        emit zoom_request(delta.y(), event->position());
    } else {
        // 滚轮向上滚动, 上移
        // 滚轮向下滚动, 下移
        emit scroll_request(delta.x(), Qt::Horizontal);
        emit scroll_request(delta.y(), Qt::Vertical);
    }
    event->accept();
}

void Canvas::move_by_keyboard(const QPointF &offset) {
    if (!selected_shapes_.empty()) {
        drag_shapes(shapes_, prev_point_ + offset, selected_shapes_);
        update();
        is_moving_shape_ = true;
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
            emit drawing_polygon(false);
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
            move_by_keyboard(QPointF(0.0, -MOVE_SPEED));
        } else if (key == Qt::Key_Down) {
            move_by_keyboard(QPointF(0.0, MOVE_SPEED));
        } else if (key == Qt::Key_Left) {
            move_by_keyboard(QPointF(-MOVE_SPEED, 0.0));
        } else if (key == Qt::Key_Right) {
            move_by_keyboard(QPointF(MOVE_SPEED, 0.0));
        } else if (event->matches(QKeySequence::SelectAll)) {
            select_shapes(shapes_);
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
            is_moving_shape_ &&
            !selected_shapes_.empty() &&
            selected_shapes_[0] < shapes_.size()
        ) {
            const auto index = selected_shapes_[0];
            if (shape_backups_.back()[index].points_ != shapes_[index].points_) {
                backup_shapes();
                emit shape_moved();
            }

            is_moving_shape_ = false;
        }
    }
}

QList<TlShape> Canvas::set_last_label(const QString &text, int32_t group_id, const QString &description, const QMap<QString, bool> &flags) {
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
    for (auto &shape : shapes | std::views::reverse) {
        shape.label_ = text;
        shape.flags_ = flags;
    }
    shape_backups_.pop_back();
    backup_shapes();
    return shapes;
}

void Canvas::undo_last_line() {
    //assert(self.shapes)
    if (QKey{"ai_points_to_shape", "ai_box_to_shape"}.contains(create_mode_)) {
        // Remove all unlabeled shapes at the tail (added by AI in one shot)
        while (!shapes_.empty() && shapes_.back().label_.isEmpty())
            shapes_.pop_back();
        current_.clear();
        ai_assist_points_.clear();
        ai_assist_shapes_.clear();
        emit drawing_polygon(false);
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
    emit drawing_polygon(true);
}

void Canvas::undo_last_point() {
    if (!current_ || current_.isClosed()) {
        return;
    }
    if (create_mode_ == "oriented_rectangle" && current_.points_.size() == 4) {
        unlock_oriented_rectangle_first_edge(this->current);
        update();
        return;
    }
    current_.popPoint();
    if (current_.size() > 0) {
        line_[0] = current_[-1];
    } else {
        current_.clear();
        emit drawing_polygon(false);
    }
    update();
}

void Canvas::reset_interaction_state() {
    current_.clear();
    ai_assist_points_.clear();
    ai_assist_shapes_.clear();

    this->hovered_shape_ = None;
    this->hovered_vertex_ = None;
    this->hovered_edge_ = None;
    this->hovered_rotation_ = None;
    this->clear_highlight_state();
}

void Canvas::load_pixmap(const QPixmap &pixmap, const QString &filename, bool clear_shapes) {
    pixmap_ = pixmap;
    pixmap_hash_ = std::hash<QString>{}(filename);
    // A new image is a fresh inference context that should surface its own
    // first failure rather than staying muted by the prior image's latch.
    this->ai_inference_failed_ = false;
    if (clear_shapes)
        this->shapes_.clear();
    this->update();
}

void Canvas::load_shapes(const QList<TlShape> &shapes, bool replace) {
    if (replace) {
        shapes_ = shapes;
    } else {
        shapes_.append(shapes);
    }
    this->backup_shapes();
    this->reset_interaction_state();
    this->update();
}

void Canvas::set_shape_visible(const TlShape &shape, bool value) {
    visible_[shape.key()] = value;
    update();
}

void Canvas::apply_cursor(const CursorRole role) {
    if (role == this->cursor_)
        return;
    auto shape = cursor_shape_for(role);
    // Push on first apply; swap the top of the stack we already own afterwards.
    if (this->cursor_ == CursorRole::DEFAULT)
        QApplication::setOverrideCursor(shape);
    else
        QApplication::changeOverrideCursor(shape);
    this->cursor_ = role;
}

void Canvas::release_cursor() {
    if (this->cursor_ == CursorRole::DEFAULT)
        return;
    this->cursor_ = CursorRole::DEFAULT;
    QApplication::restoreOverrideCursor();
}

void Canvas::reset_state() {
    this->release_cursor();
    this->pixmap_ = QPixmap();
    this->pixmap_hash_ = None;
    this->shapes_.clear();
    this->shape_backups_.clear();
    this->is_moving_shape_ = false;
    this->selected_shapes_.clear();
    this->selected_shapes_copy_.clear();
    this->current_.clear();
    this->ai_assist_points_.clear();
    this->ai_assist_shapes_.clear();
    this->highlight_ = {};
    this->rotation_highlight_ = {};
    this->hovered_shape_ = None;
    this->last_hovered_shape_ = None;
    this->hovered_vertex_ = None;
    this->last_hovered_vertex_ = None;
    this->hovered_edge_ = None;
    this->last_hovered_edge_ = None;
    this->hovered_rotation_ = None;
    this->update();
}

bool Canvas::is_degenerate_draft(const DraftShape &draft) {
    const auto points = draft.points_;
    const auto shape_type = draft.shape_type_;
    if (shape_type == "polygon")
        return std::set<QPointF>{points.begin(), points.end()}.size() < 3;
    if (shape_type == "linestrip")
        return std::set<QPointF>{points.begin(), points.end()}.size() < 2;
    if (shape_type == "rectangle")
        return (
            points.size() != 2
            || points[0].x() == points[1].x()
            || points[0].y() == points[1].y()
        );
    if (QKey{"circle", "line"}.contains(shape_type ))
        return points.size() != 2 || points[0] == points[1];
    if (shape_type == "oriented_rectangle")
        return points.size() != 4 || points[0] == points[1] || points[1] == points[2];
    return false;
}

QList<QPointF> Canvas::normalize_bbox_points(QList<QPointF> bbox_points) {
    if (bbox_points.size() != 2)
        throw std::invalid_argument(std::format("Expected 2 points for bbox, got {}", bbox_points.size()));

    const auto p1 = bbox_points[0], p2 = bbox_points[1];
    const auto xmin = std::min(p1.x(), p2.x());
    const auto ymin = std::min(p1.y(), p2.y());
    const auto xmax = std::max(p1.x(), p2.x());
    const auto ymax = std::max(p1.y(), p2.y());
    return {QPointF(xmin, ymin), QPointF(xmax, ymax)};
}

QPointF Canvas::snap_cursor_pos_for_square(QPointF pos, QPointF opposite_vertex) {
    QPointF pos_from_opposite = pos - opposite_vertex;
    float square_size = std::min(abs(pos_from_opposite.x()), abs(pos_from_opposite.y()));
    return opposite_vertex + QPointF(
        np::sign(pos_from_opposite.x()) * square_size,
        np::sign(pos_from_opposite.y()) * square_size
    );
}

int32_t Canvas::compute_overscroll_slack(int32_t scaled, int32_t viewport) {
    // Floor (viewport // 8) keeps middle-drag pan responsive at slight
    // overflow; without it, scroll range equals the overflow and a
    // 2-px-overflowing image feels locked under the cursor. The floor
    // reintroduces a viewport/16 image shift at the threshold, 4x smaller
    // than the original viewport/4 jump. Cap (viewport // 2) lets each
    // image edge be panned to the viewport center but no further.
    if (scaled <= viewport)
        return 0;
    return std::max(viewport / 8, std::min(viewport / 2, scaled - viewport));
}

QPointF Canvas::compute_intersection_edges_image(
    const QPointF &p1, const QPointF &p2, const QSize &image_size
) {
    const auto width = static_cast<double>(image_size.width());
    const auto height = static_cast<double>(image_size.height());

    const auto start_x = np::clip(p1.x(), 0.0, width);
    const auto start_y = np::clip(p1.y(), 0.0, height);
    const auto delta_x = p2.x() - start_x;
    const auto delta_y = p2.y() - start_y;

    // Liang-Barsky line clipping.
    const std::list<std::pair<double, double>> boundary_pairs{
        {start_x, -delta_x},
        {width - start_x, delta_x},
        {start_y, -delta_y},
        {height - start_y, delta_y}
    };
    auto t_exit = 1.0;
    for (const auto &[numerator, denominator] : boundary_pairs) {
        if (denominator > 0.0)
            t_exit = std::min(t_exit, numerator / denominator);
    }

    if (t_exit > 0.0)
        return {start_x + t_exit * delta_x, start_y + t_exit * delta_y};

    // t_exit == 0: start is on a boundary, p2 is exterior — slide along the edge.
    if (start_x <= 0.0 || start_x >= width)
        return {start_x, np::clip(p2.y(), 0.0, height)};
    return {np::clip(p2.x(), 0.0, width), start_y};
}

bool Canvas::should_reselect_on_right_press(
    const QList<int32_t> &selected_shapes, int32_t hovered_shape
) {
    if (selected_shapes.empty())
        return true;
    if (!hovered_shape)
        return false;
    return !selected_shapes.contains(hovered_shape);
}

TlShape Canvas::pick_pending_moved_shape(
    bool is_moving_shape, TlShape hovered_shape, QList<TlShape> shapes
) {
    if (!is_moving_shape)
        return {};
    if (!hovered_shape)
        return {};
    if (!shapes.contains(hovered_shape))
        return {};
    return hovered_shape;
}

QPointF Canvas::opposite_corner_in_parallelogram(
    QPointF opposite_to, QPointF neighbor1, QPointF neighbor2
) {
    return neighbor1 + neighbor2 - opposite_to;
}

QPair<QPointF, QPointF> Canvas::project_oriented_rectangle_corners(
    QPointF anchor, QPointF edge_axis, QPointF moving
) {
    auto perp = utils::project_point_on_perpendicular_line(
        moving, edge_axis, anchor
    );
    auto para = opposite_corner_in_parallelogram(
        perp, anchor, moving
    );
    return {perp, para};
}

bool Canvas::is_out_of_image(QPointF point, QSize image_size) {
    return (
        point.x() < 0
        || point.y() < 0
        || point.x() > image_size.width()
        || point.y() > image_size.height()
    );
}

QList<QPointF> Canvas::reproject_oriented_rectangle_corners(
    QList<QPointF> corners,
    int32_t vertex_index,
    QPointF pos,
    QSize image_size,
    bool allow_out_of_bounds
) {
    //Given a 4-corner oriented rectangle and a dragged corner, return the new
    //corner positions: the dragged corner and its two neighbors move so the shape
    //stays a parallelogram, clipped to the image unless out-of-bounds points are
    //allowed; the opposite anchor is fixed.
    QPointF anchor = corners[(vertex_index - 2) % 4];
    QPointF edge_axis = corners[(vertex_index - 1) % 4];
    QPointF moving = pos;
    auto [adjacent_perp, adjacent_para] = project_oriented_rectangle_corners(
        anchor, edge_axis, moving
    );

    if (!allow_out_of_bounds) {
        if (is_out_of_image(moving, image_size)) {
            auto edge_a = compute_intersection_edges_image(
                adjacent_perp, moving, image_size
            );
            auto edge_b = compute_intersection_edges_image(
                adjacent_para, moving, image_size
            );
            moving = utils::project_point_on_line(
                moving, edge_a, edge_b
            );
            auto [adjacent_perp1, adjacent_para1] = project_oriented_rectangle_corners(
                anchor, adjacent_para, moving
            );
            adjacent_perp = adjacent_perp1; adjacent_para = adjacent_para1;
        }
        if (is_out_of_image(adjacent_perp, image_size)) {
            adjacent_perp = compute_intersection_edges_image(
                anchor, adjacent_perp, image_size
            );
            moving = opposite_corner_in_parallelogram(
                anchor, adjacent_perp, adjacent_para
            );
        }
        if (is_out_of_image(adjacent_para, image_size)) {
            adjacent_para = compute_intersection_edges_image(
                anchor, adjacent_para, image_size
            );
            moving = opposite_corner_in_parallelogram(
                anchor, adjacent_perp, adjacent_para
            );
        }
    }
    QList<QPointF> new_corners = corners;
    new_corners[vertex_index] = moving;
    new_corners[(vertex_index + 1) % 4] = adjacent_perp;
    new_corners[(vertex_index - 1) % 4] = adjacent_para;
    return new_corners;
}

//
// User-assisted function.
//
Canvas::~Canvas() {
    if (ai_assist_thread_) {
        ai_assist_thread_.reset();
    }
}

bool Canvas::isVisible(const TlShape &shape) {
    const auto it = this->visible_.find(shape.key());
    return it != this->visible_.end() ? it.value() : true;
}

bool Canvas::drawing() {
    return this->mode_ == CanvasMode::CREATE;
}

bool Canvas::fillDrawing() const {
    return this->fill_drawing_;
}

bool Canvas::editing() {
    return this->mode_ == CanvasMode::EDIT;
}

bool Canvas::closeEnough(const QPointF &p1, const QPointF &p2) {
    // d = distance(p1 - p2)
    // m = (p1-p2).manhattanLength()
    // print "d %.2f, m %d, %.2f" % (d, m, d - m)
    // divide by scale to allow more precision when zoomed in
    return utils::distance(p1 - p2) < (epsilon_ / scale_);
}

void Canvas::enableDragging(bool enabled) {
    is_dragging_enabled_ = enabled;
}

void Canvas::calculateOffsets(const QPointF &point) {
    if (selected_shapes_.empty()) {
        offsets_ = { QPointF(0.0, 0.0), QPointF(0.0, 0.0) };
        return;
    }

    double left   = pixmap_.width();
    double top    = pixmap_.height();
    double right  = 0.;
    double bottom = 0.;
    for (const auto rect : selected_shapes_ | std::views::transform([this](int32_t i) { return shapes_[i].boundingRect(); })) {
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

void Canvas::hideBackroundShapes(bool value) {
    hideBackround_ = value;
    if (!selected_shapes_.empty()) {
        // Only hide other shapes if there is a current selection.
        // Otherwise the user will not be able to select a shape.
        setHiding(true);
        update();
    }
}

void Canvas::setHiding(bool enable) {
    hideBackround1_ = enable ? hideBackround_ : false;
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

QPointF Canvas::_compute_intersection_edges_image1(
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
