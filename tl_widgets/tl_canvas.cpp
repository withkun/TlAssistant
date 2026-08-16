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

static const std::vector<int32_t> _DEFAULT_SHAPE_RGB{0, 255, 0};
static const Palette _DEFAULT_PALETTE = Palette::from_rgb(_DEFAULT_SHAPE_RGB);


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
    this->closed_ = true;
    return *this;
}

DraftShape DraftShape::open() {
    this->closed_ = false;
    return *this;
}

DraftShape DraftShape::add_point(
    const QPointF &point, const int32_t label, const bool autoclose
) {
    if (autoclose && !this->points_.empty() && this->points_[0] == point) {
        this->closed_ = true;
        return *this;
    }

    this->points_.push_back(point);
    this->point_labels_.push_back(label);
    return *this;
}

DraftShape DraftShape::pop_point() {
    if (this->points_.empty())
        return *this;
    this->points_.pop_back();
    this->point_labels_.pop_back();
    return *this;
}

void DraftShape::clear() {
    this->points_.clear();
    this->point_labels_.clear();
    this->shape_type_.clear();
    this->closed_ = false;
}

static TlShape draft_to_shape(const DraftShape &draft) {
    return TlShape{
        draft.shape_type_,
        draft.points_,
        draft.point_labels_,
        draft.closed_
    };
}

static DraftShape shape_to_draft(const TlShape &shape) {
    return DraftShape{
        shape.shape_type_,
        shape.points_,
        shape.point_labels_,
        shape.closed_
    };
}

constexpr float MOVE_SPEED     = 5.0f;

static const std::set<QString> CreateMode {
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

static const std::set<QString> AI_CREATE_MODES {
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
static const std::set<QString> SEED_INCOMPATIBLE_CREATE_MODES {
    "point",
    "ai_points_to_shape",
    "ai_box_to_shape",
};


Canvas::Canvas(const float epsilon,
               const QString &double_click,
               const int32_t num_backups,
               const QMap<QString, bool> &crosshair,
               const bool allow_out_of_bounds_points) : QWidget() {
    this->pixmap_                       = QPixmap();
    this->pixmap_hash_                  = {};
    this->cursor_                       = {};
    this->shapes_                       = {};
    this->shape_backups_                = {};
    this->is_moving_shape_              = {};
    this->selected_shapes_              = {};
    this->selected_shapes_copy_         = {};
    this->current_                      = DraftShape();
    this->hovered_shape_                = {};
    this->last_hovered_shape_           = {};
    this->hovered_vertex_               = {};
    this->last_hovered_vertex_          = {};
    this->hovered_edge_                 = {};
    this->last_hovered_edge_            = {};
    this->hovered_rotation_             = {};

    this->mode_                         = CanvasMode::EDIT;

    this->create_mode_                  = "polygon";

    this->fill_drawing_                 = false;

    this->show_labels_                  = false;

    this->prev_point_                   = QPointF();
    this->prev_move_point_              = QPointF();
    this->drag_anchor_                  = QPair{QPointF(), QRectF()};
    this->rotation_center_              = QPointF();
    this->rotation_initial_angle_       = 0.f;
    this->rotation_original_points_     = {};

    this->pan_anchor_                   = QPointF();

    this->highlight_                    = VertexHighlight();
    this->rotation_highlight_           = VertexHighlight();
    this->color_resolver_               = {};
    this->point_size_                   = {};
    this->point_type_                   = {};
    this->draft_palette_                = Palette();
    this->palette_cache_                = QMap<QString, Palette>{};

    this->ai_assist_session_            = nullptr;

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
    this->line_                         = DraftShape();
    this->prev_point_                   = QPointF();
    this->prev_move_point_              = QPointF();
    this->drag_anchor_                  = QPair{ QPointF(), QRectF() };
    this->rotation_center_              = QPointF(0, 0);
    this->rotation_initial_angle_       = 0.f;
    this->rotation_original_points_     = {};
    this->scale_                        = 1.0;
    this->ai_assist_session_            = std::make_unique<AiAssistSession>(this);
    this->ai_inference_failed_          = false;
    this->snapping_                     = true;
    this->hovered_shape_is_selected_    = false;
    this->painter_                      ;
    this->pan_anchor_                   = QPointF();
    this->color_resolver_               = {};
    this->point_size_                   = 8;
    this->point_type_                   = "round";
    this->draft_palette_                = _DEFAULT_PALETTE;
    this->palette_cache_                = QMap<QString, Palette>{};
    this->context_menus_                = ContextMenuPair{
        .without_selection_             = new QMenu(),
        .with_selection_                = new QMenu()
    };
    this->context_menu_origin_          = QPoint();
    this->setMouseTracking(true);
    this->setFocusPolicy(Qt::FocusPolicy::WheelFocus);
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

void Canvas::set_color_resolver(
    const fColorResolver &resolver
) {
    this->color_resolver_ = resolver;
}

void Canvas::set_point_size(const int32_t point_size) {
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
    if (!palette) {
        palette = Palette::from_rgb(this->color_resolver_(label));
        this->palette_cache_[label] = palette;
    }
    return palette;
}

void Canvas::set_draft_palette(const Palette &palette) {
    this->draft_palette_ = palette;
}

void Canvas::highlight_vertex(const int32_t index, const QString &mode) {
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

ShapeRenderContext Canvas::render_context(const TlShape &shape, const int32_t index, const bool highlighted) {
    const bool selected = selected_shapes_.contains(index);
    return ShapeRenderContext{
        .scale_=this->scale_,
        .palette_=this->resolve_palette(shape.label_),
        .point_size_=this->point_size_,
        .point_type_=this->point_type_,
        .selected_=selected,
        .fill_=selected || index == this->hovered_shape_,
        .highlight_=highlighted ? this->highlight_ : VertexHighlight(),
        .rotation_highlight_=highlighted ? this->rotation_highlight_ : VertexHighlight{},
        .show_label_=this->show_labels_,
    };
}

ShapeRenderContext Canvas::draft_render_context(
    const bool selected,
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
    return !this->current_.points_.empty();
}

//@property
QString Canvas::create_mode() const {
    return this->create_mode_;
}

//@create_mode.setter
void Canvas::create_mode(const QString &value) {
    if (!CreateMode.contains(value))
        throw std::invalid_argument("Unsupported create_mode: " + value.toStdString());
    const auto &new_mode = value;
    if (new_mode == this->create_mode_)
        return;
    const auto old_mode = this->create_mode_;
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
    this->current_ = DraftShape{.shape_type_=new_mode}.add_point(
        seed_point, seed_label
    );
    this->line_.shape_type_ = new_mode;
    this->update();
}

std::string Canvas::get_ai_model_name() {
    return this->ai_assist_session_->model_name_;
}

void Canvas::set_ai_model_name(const std::string &model_name) {
    this->ai_assist_session_->model_name_ = model_name;
    AppConfig::instance().ai_assist_name_ = model_name;
}

void Canvas::set_ai_output_format(const std::string &output_format) {
    this->ai_assist_session_->output_format_ = output_format;
}

QList<TlShape> Canvas::shapes_from_ai_points(
    const QList<QPointF> &points, const QList<int32_t> &point_labels
) {
    //image: np.ndarray = _utils.img_qt_to_arr(img_qt=this->pixmap.toImage())
    return this->ai_assist_session_->submit_propose_shapes(
        this->pixmap_,
        this->pixmap_hash_,
        points,
        point_labels,
        this->shapes_
    );
}

void Canvas::report_inference_failure(const QString &error) {
    this->ai_inference_failed_ = true;
    SPDLOG_ERROR("AI inference failed: {}", error.toStdString());
    emit this->inference_failed("AI inference failed: " + error);
}

void Canvas::backup_shapes() {
    while (this->shape_backups_.length() > this->num_backups_) { this->shape_backups_.pop_front(); }
    QList<TlShape> shapes = this->shapes_ | std::views::transform([](const auto &s) { return s.copy(); }) | std::ranges::to<QList<TlShape>>();
    this->shape_backups_.append(shapes);
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
    this->shapes_ = this->shape_backups_.back(); this->shape_backups_.pop_back();
    this->selected_shapes_.clear();
    this->update();
}

void Canvas::enterEvent(QEnterEvent *event) {
    this->apply_cursor(this->cursor_);
    this->update_status();
}

void Canvas::leaveEvent(QEvent *event) {
    if (this->set_highlight(
        None,
        None,
        None,
        None
    ))
        this->update();
    this->release_cursor();
    this->update_status();
}

void Canvas::focusOutEvent(QFocusEvent *event) {
    this->release_cursor();
    this->update_status();
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
        if (need_update)
            this->update();
    }
}

bool Canvas::set_highlight(
    const int32_t hovered_shape,
    const int32_t hovered_edge,
    const int32_t hovered_vertex,
    const int32_t hovered_rotation
) {
    const int32_t previous_shape = this->hovered_shape_;
    bool need_update = hovered_shape != None;
    if (previous_shape != None) {
        this->clear_highlight_state();
        need_update = true;
    }
    // NOTE: Store last highlighted for adding/removing points.
    this->last_hovered_shape_   = (
        hovered_shape  == None ? previous_shape        : hovered_shape
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
    QList<QString> messages;
    if (this->mode_ == CanvasMode::CREATE) {
        messages.append(tr("Creating %1").arg(create_mode_));
        messages.append(get_create_mode_message());
        if (this->current_) {
            messages.append(tr("ESC to cancel"));
        }
        if (this->can_close_shape()) {
            messages.append(tr("Enter or Space to finalize"));
        }
    } else {
        //assert self.mode == _CanvasMode.EDIT
        messages.append(tr("Editing shapes"));
    }
    if (!extra_messages.empty())
        messages.append(extra_messages);
    emit status_updated(" • " + messages.join(""));
}

QString Canvas::get_create_mode_message() {
    //assert self.mode == _CanvasMode.CREATE
    const bool is_new = !this->current_;
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
    QPointF pos;
    try {
        pos = this->transform_point_widget_to_image(event->position());
    } catch (...) {
        return;
    }
    emit this->mouse_moved(pos);
    this->prev_move_point_ = pos;
    this->dispatch_pointer_move(pos, event);
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
    const auto cursor = QPointF(this->mapToGlobal(event->position().toPoint()));
    const auto step = cursor - this->pan_anchor_;
    this->pan_anchor_ = cursor;
    emit this->pan_request(QPoint(int(step.x()), int(step.y())));
}

void Canvas::track_drawing_cursor(QPointF pos, QMouseEvent *event) {
    const auto desired_line_shape_type = CREATE_MODE_TO_SHAPE_TYPE[this->create_mode_];
    if (this->line_.shape_type_ != desired_line_shape_type)
        this->line_.shape_type_ = (
            desired_line_shape_type
        );
    this->apply_cursor(CursorRole::DRAW);
    if (!this->current_) {
        this->update();
        this->update_status();
        return;
    }
    const auto is_shift_pressed = bool(event->modifiers() & Qt::KeyboardModifier::ShiftModifier);
    pos = this->project_drawing_pos_into_image(pos);
    this->update_drawing_line(pos, is_shift_pressed);
    //assert len(self._line.points) == len(self._line.point_labels)
    this->update();
    this->update_status();
}

QPointF Canvas::project_drawing_pos_into_image(const QPointF &pos) {
    const auto current = this->current_;
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

void Canvas::refresh_hover_state(const QPointF &pos) {
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
    if (POLYLINE_SHAPE_TYPES.contains(mode)) {
        this->line_.points_ = { current[-1], pos };
        this->line_.point_labels_ = { 1, 1 };
    } else if (mode == "ai_points_to_shape") {
        const auto num = current.points_.size();
        this->line_.points_ = { current.points_[num-1], pos };
        this->line_.point_labels_ = { current.point_labels_[num-1], is_shift_pressed ? 0 : 1 };
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

void Canvas::continue_right_button_drag(const QPointF &pos) {
    if (!this->selected_shapes_copy_.empty()) {
        this->apply_cursor(CursorRole::MOVE);
        this->drag_shapes(this->selected_shapes_copy_, pos);
        this->update();
    } else if (!this->selected_shapes_.empty()) {
        this->selected_shapes_copy_ = this->selected_shapes_ | std::views::transform([this](auto i) { return this->shapes_[i]; }) | std::ranges::to<QList<TlShape>>();
        this->update();
    }
    this->update_status();
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

void Canvas::drag_hovered_vertex(const QPointF &pos, const bool is_shift_pressed) {
    //assert self._hovered_vertex is not None
    //assert self.hovered_shape is not None
    auto &hovered_shape = this->shapes_[this->hovered_shape_];
    this->bounded_move_vertex(
        hovered_shape,
        this->hovered_vertex_,
        pos,
        is_shift_pressed
    );
    this->update();
    this->is_moving_shape_ = true;
}

void Canvas::drag_hovered_rotation_point(const QPointF &pos) {
    //assert self.hovered_shape is not None
    //assert len(self._rotation_original_points) > 0, (
    //    "_capture_rotation_anchors must be called before dragging"
    //)
    auto &hovered_shape = this->shapes_[this->hovered_shape_];
    const auto current_angle = utils::direction_angle(
        this->rotation_center_, pos
    );
    rotate(
        hovered_shape,
        this->rotation_center_,
        current_angle - this->rotation_initial_angle_,
        this->rotation_original_points_
    );
    this->update();
    this->is_moving_shape_ = true;
}

void Canvas::capture_rotation_anchors() {
    //assert self.hovered_shape is not None
    //assert self._hovered_rotation is not None
    const auto &hovered_shape = this->shapes_[this->hovered_shape_];
    const auto handle = get_rotation_handle(
        hovered_shape, this->hovered_rotation_
    );
    this->rotation_center_ = oriented_rectangle_center(
        hovered_shape
    );
    this->rotation_initial_angle_ = utils::direction_angle(
        this->rotation_center_, handle
    );
    this->rotation_original_points_ = hovered_shape.points_;
}

void Canvas::drag_selected_shapes(const QPointF &pos) {
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
        this->hovered_shape_
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
        this->set_highlight(
            target.shape,
            None,
            target.index,
            None
        );
        this->highlight_vertex(target.index, "move");
        this->apply_cursor(CursorRole::HANDLE);
        status_messages.append(tr("Click & drag to move point"));
        if (this->shapes_[target.shape].can_remove_point())
            status_messages.append(tr("ALT + SHIFT + Click to delete point"));
        this->update();
        return;
    }
    if (target.kind == HitKind::ROTATION_HANDLE) {
        //assert target.index is not None
        this->set_highlight(
            target.shape,
            None,
            None,
            target.index
        );
        this->highlight_rotation_point(target.index, "move");
        this->apply_cursor(CursorRole::HANDLE);
        status_messages.append(tr("Click & drag to rotate the shape"));
        this->update();
        return;
    }
    if (target.kind == HitKind::EDGE) {
        //assert target.index is not None
        this->set_highlight(
            target.shape,
            target.index,
            None,
            None
        );
        this->apply_cursor(CursorRole::HANDLE);
        status_messages.append(tr("ALT + Click to create point on shape"));
        this->update();
        return;
    }
    if (target.kind == HitKind::BODY) {
        this->set_highlight(
            target.shape,
            None,
            None,
            None
        );
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
    throw std::logic_error("typing.assert_never(target.kind)");
}

void Canvas::add_point_to_edge() {
    const auto shape = this->last_hovered_shape_;
    const auto index = this->last_hovered_edge_;
    const auto point = this->prev_move_point_;
    if (shape == None || index == None || point.isNull())
        return;
    this->shapes_[shape].insert_point(index, point);
    this->highlight_vertex(index, "move");
    this->hovered_shape_ = shape;
    this->hovered_vertex_ = index;
    this->hovered_edge_ = None;
    this->is_moving_shape_ = true;
    // Repaint now; otherwise the edit is invisible until the next mouse move.
    this->update();
}

bool Canvas::remove_selected_point() {
    const auto shape = this->last_hovered_shape_;
    const auto index = this->last_hovered_vertex_;
    if (shape == None || index == None || !this->shapes_[shape].can_remove_point())
        return false;
    this->shapes_[shape].remove_point(index);
    this->clear_highlight_state();
    // Drop the hovered vertex and selection so the press that deleted the
    // point cannot also drag the adjacent vertex (#968) or the whole shape.
    this->deselect_shape();
    this->hovered_shape_ = shape;
    this->hovered_vertex_ = None;
    this->last_hovered_vertex_ = None;
    this->is_moving_shape_ = true;  // commit the removal on release
    // Repaint now; otherwise the edit is invisible until the next mouse move.
    this->update();
    return true;
}

void Canvas::mousePressEvent(QMouseEvent *event) {
    const QPointF pos = this->transform_point_widget_to_image(event->position());
    this->dispatch_pointer_press(pos, event);
    this->update_status();
}

void Canvas::dispatch_pointer_press(const QPointF &pos, QMouseEvent *event) {
    const auto button = event->button();
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
        this->extend_current_shape(this->current_, event);
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
        current = current.add_point(this->line_.points_[1], 1, true);
        this->current_ = current;
        //self._line = dataclasses.replace(
        //    self._line, points=(current.points[-1],) + self._line.points[1:]
        this->line_.points_[0]  = current[-1];
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
       this->current_ = current;
       this->current_.points_ = this->line_.points_;
       this->finalize();
    } else if (mode == "linestrip") {
        current = current.add_point(this->line_.points_[1]);
        this->current_ = current;
        //self._line = dataclasses.replace(
        //    self._line, points=(current.points[-1],) + self._line.points[1:]
        this->line_.points_[0] = current[-1];
        if (modifiers == Qt::KeyboardModifier::ControlModifier)
            this->finalize();
    } else if (mode == "ai_points_to_shape") {
        current = current.add_point(
            this->line_.points_[1], this->line_.point_labels_[1]
        );
        this->current_ = current;
        this->line_.points_[0] = current.points_[current.points_.size()-1];
        this->line_.point_labels_[0] = current.point_labels_[current.point_labels_.size()-1];
        if (modifiers & Qt::KeyboardModifier::ControlModifier)
            this->finalize();
    }
}

void Canvas::lock_oriented_rectangle_first_edge(const DraftShape &current) {
    auto first_corner = this->line_.points_[0];
    auto second_corner = this->line_.points_[1];
    this->current_ = current;
    this->current_.points_ = {
        first_corner,
        second_corner,
        second_corner,
        first_corner,
    };
    this->current_.point_labels_ = {1, 1, 1, 1};
    //self._line = dataclasses.replace(
    //    self._line, points=(second_corner,) + self._line.points[1:]
    this->line_.points_[0] = second_corner;
}

void Canvas::unlock_oriented_rectangle_first_edge(const DraftShape &current) {
    auto anchor = current.points_[0];
    this->current_ = current;
    this->current_.points_ = { anchor };
    this->current_.point_labels_ = { current.point_labels_[0] };
    this->line_.points_ = { anchor, anchor };
}

void Canvas::start_new_shape(
    const QPointF &pos,
    QMouseEvent *event,
    const bool is_shift_pressed
) {
    const auto mode = this->create_mode_;
    if (QKey{"ai_points_to_shape", "ai_box_to_shape"}.contains(mode) && !download_ai_model(
        this->get_ai_model_name(), this
    ))
        return;

    this->current_ = DraftShape{
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

bool Canvas::maybe_modify_polygon_topology(const Qt::KeyboardModifiers modifiers) {
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

void Canvas::press_right(const QPointF &pos, QMouseEvent *event) {
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
    this->dispatch_pointer_release(event);
    this->commit_pending_shape_move();
    this->update_status();
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
    auto *const menu = this->context_menus_.menu_for(
        !this->selected_shapes_copy_.empty()
    );
    this->release_cursor();
    this->context_menu_origin_ = this->mapToGlobal(event->position().toPoint());
    QAction *triggered{};
    try {
        triggered = menu->exec(this->context_menu_origin_);  // type: ignore
    } catch (...) {}
    this->context_menu_origin_ = {};
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
    std::ranges::for_each(this->selected_shapes_, [&](auto &i) { if (i != this->hovered_shape_) selected_shapes.push_back(i); });
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

QWidget *Canvas::scroll_viewport() const {
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
    const auto moved = pick_pending_moved_shape(
        this->is_moving_shape_,
        this->hovered_shape_,
        this->shapes_
    );
    if (!moved)
        return;
    const auto index = this->shapes_.indexOf(moved);
    if (
        this->shape_backups_.back()[index].points_ != this->shapes_[index].points_
    ) {
        this->backup_shapes();
        emit this->shape_moved();
    }
    this->is_moving_shape_ = false;
}

bool Canvas::end_move(const bool copy) {
    //assert self.selected_shapes and self._selected_shapes_copy
    //assert len(self._selected_shapes_copy) == len(self.selected_shapes)
    if (copy)
        this->apply_copy_move();
    else
        this->apply_in_place_move();
    this->selected_shapes_copy_.clear();
    this->update();
    this->backup_shapes();
    return true;
}

void Canvas::apply_copy_move() {
    for (const auto &&[i, shape] : this->selected_shapes_copy_ | std::views::enumerate) {
        this->shapes_.append(shape.clone());
        this->selected_shapes_[i] = this->shapes_.count() - 1;
    }
}

void Canvas::apply_in_place_move() {
    for (auto &&[original, clone] : std::views::zip(this->selected_shapes_, this->selected_shapes_copy_)) {
        shapes_[original].points_ = clone.points_;
    }
}

bool Canvas::can_close_shape() {
    if (this->mode_ != CanvasMode::CREATE)
        return false;
    if (!current_)
        return false;
    if (this->create_mode_ == "ai_points_to_shape")
        return true;
    if (create_mode_ == "linestrip")
        return this->current_.size() >= 2;
    if (this->create_mode_ == "oriented_rectangle") {
        // Points 2 and 3 are seeded as duplicates of points 1 and 0 after
        // the first edge is locked; mouse movement reprojects them. Treat
        // the shape as closeable only once the third corner has moved.
        return (
            this->current_.points_.size() == 4
            && this->current_.points_[2] != this->current_.points_[1]
        );
    }
    return this->current_.points_.size() >= 3;
}

void Canvas::mouseDoubleClickEvent(QMouseEvent *event) {
    if (this->double_click_ != "close")
        return;
    if (!this->can_close_shape())
        return;
    this->finalize();
}

void Canvas::select_shapes(const QList<TlShape> &shapes) {
    const QList<int32_t> indexes = shapes | std::views::transform([this](const auto &s) { return this->shapes_.indexOf(s); }) | std::ranges::to<QList<int32_t>>();;
    emit this->selection_changed(indexes);
    this->update();
}

void Canvas::select_shape_point(
    const QPointF &point, const bool multiple_selection_mode
) {
    if (this->hovered_vertex_ != None) {
        //assert self.hovered_shape is not None
        this->highlight_vertex(this->hovered_vertex_, "move");
        if (this->deselect_shape())
            this->update();
        return;
    }

    const auto clicked_shape = this->find_shape_at_point(point);
    if (clicked_shape == None) {
        if (this->deselect_shape())
            this->update();
        return;
    }

    const auto already_selected = this->selected_shapes_.contains(clicked_shape);
    if (already_selected) {
        this->hovered_shape_is_selected_ = true;
    } else {
        const auto new_selection = (multiple_selection_mode ?
            this->selected_shapes_ + QList{clicked_shape} :
            QList{clicked_shape}
        );
        emit this->selection_changed(new_selection);
        this->hovered_shape_is_selected_ = false;
    }
    this->record_drag_anchor(point);
}

int32_t Canvas::find_shape_at_point(const QPointF &point) const {
    //query = np.array([point.x(), point.y()])
    for (auto &&[index, shape] : this->shapes_ | std::views::enumerate)
        if (shape.visible_ && is_hit_by_point(
            shape,
            point,
            this->scale_,
            this->point_size_,
            this->epsilon_
        ))
            return index;
    return None;
}

void Canvas::record_drag_anchor(const QPointF &click) {
    if (this->selected_shapes_.empty()) {
        this->drag_anchor_ = { QPointF(), QRectF() };
        return;
    }
    auto bounds = shape_bounds(this->shapes_[this->selected_shapes_[0]]);
    for (auto i = 1; i < this->selected_shapes_.size(); ++i)
        bounds = bounds.united(shape_bounds(this->shapes_[this->selected_shapes_[i]]));
    this->drag_anchor_ = { bounds.topLeft() - click, bounds };
}

void Canvas::bounded_move_vertex(
    TlShape &shape,
    const int32_t vertex_index,
    QPointF pos,
    const bool is_shift_pressed
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
        this->bounded_move_oriented_rectangle_vertex(
            shape, vertex_index, pos
        );
        return;
    }

    if (this->should_constrain_to_pixmap(pos)) {
        pos = compute_intersection_edges_image(
            shape[vertex_index], pos, this->pixmap_.size()
        );
    }
    if (is_shift_pressed && shape.shape_type_ == "rectangle")
        pos = snap_cursor_pos_for_square(
            pos, shape[1 - vertex_index]
        );

    shape.move_vertex(vertex_index, pos);
}

void Canvas::bounded_move_oriented_rectangle_vertex(
    TlShape &shape, const int32_t vertex_index, const QPointF &pos
) {
    //assert len(shape.points) == 4
    const auto &corners = shape.points_;    //tuple(QPointF(*point) for point in shape.points)
    const auto new_corners = reproject_oriented_rectangle_corners(
        corners,
        vertex_index,
        pos,
        this->pixmap_.size(),
        this->allow_out_of_bounds_points_
    );
    for (auto &&[i, corner] : new_corners | std::views::enumerate)
        shape.move_vertex(i, corner);
}

bool Canvas::drag_shapes(QList<TlShape> &shapes, const QPointF &cursor, const QList<int32_t> &indexes) {
    if (this->should_constrain_to_pixmap(cursor))
        return false;

    auto [rel_tl, bounds] = this->drag_anchor_;
    auto target = cursor + rel_tl;
    if (!this->allow_out_of_bounds_points_) {
        const auto pw = float(this->pixmap_.width());
        const auto ph = float(this->pixmap_.height());
        target.setX(std::max(0.0, target.x()));
        target.setY(std::max(0.0, target.y()));
        target.setX(std::min(target.x(), pw - bounds.width()));
        target.setY(std::min(target.y(), ph - bounds.height()));
    }

    const auto new_cursor = target - rel_tl;
    const auto delta = new_cursor - this->prev_point_;
    if (delta.isNull())
        return false;

    if (indexes.empty()) {
        for (auto &shape : shapes)
            shape.translate(delta);
    } else {
        for (auto &i : indexes)
            shapes[i].translate(delta);
    }
    this->prev_point_ = new_cursor;
    return true;
}

bool Canvas::deselect_shape() {
    if (this->selected_shapes_.empty())
        return false;
    emit this->selection_changed({});
    this->hovered_shape_is_selected_ = false;
    return true;
}

QList<TlShape> Canvas::delete_selected() {
    if (this->selected_shapes_.empty())
        return {};
    const auto removed = this->selected_shapes_ | std::views::transform([this](auto i) { return this->shapes_[i]; }) | std::ranges::to<QList<TlShape>>();;
    std::ranges::for_each(removed, [this](auto &shape) { this->shapes_.removeOne(shape); });
    this->backup_shapes();
    this->selected_shapes_.clear();
    this->update();
    return removed;
}

void Canvas::delete_shape(const TlShape &shape) {
    const auto idx = this->shapes_.indexOf(shape);
    if (this->selected_shapes_.contains(idx))
        this->selected_shapes_.removeOne(idx);
    this->shapes_.removeAt(idx);
    this->backup_shapes();
    this->update();
}

void Canvas::paintEvent(QPaintEvent *event) {
    if (this->pixmap_.isNull()) {
        QWidget::paintEvent(event);
        return;
    }
    this->render_canvas();
    if (this->current_)
        this->clear_highlight_state();
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

bool Canvas::should_draw_crosshair(const QPointF &cursor) {
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
            shape, idx, idx == this->hovered_shape_
        );
        render_shape(painter, shape, context);
    }
}

void Canvas::draw_active_shape_layer(QPainter &painter) {
    if (!this->current_)
        return;
    //assert len(this->_line.points) == len(this->_line.point_labels);
    this->render_draft(painter, this->current_, true);
    this->render_draft(painter, this->line_, false);
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
    const auto preview = this->build_preview_shape();
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
    QPainter &painter, const DraftShape &draft, const bool highlighted
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
        return this->build_polygon_preview(this->current_);
    if (this->create_mode_ == "ai_points_to_shape")
        return this->build_ai_points_preview(this->current_);
    return {};
}

TlShape Canvas::build_polygon_preview(const DraftShape &current) {
    auto preview = current;
    if (this->fill_drawing_ && preview.points_.size() >= 2)
        preview = preview.add_point(this->line_.points_[1], 1, true);
    return draft_to_shape(preview);
}

TlShape Canvas::build_ai_points_preview(DraftShape current) {
    const auto preview = current.add_point(
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

QPointF Canvas::transform_point_widget_to_image(const QPointF &point) {
    const auto origin = this->compute_image_origin_offset();
    const auto image_x = point.x() / this->scale_ - origin.x();
    const auto image_y = point.y() / this->scale_ - origin.y();
    return {image_x, image_y};
}

QPointF Canvas::compute_image_origin_offset() {
    const auto area = QWidget::size();
    const float scaled_w = this->pixmap_.width() * this->scale_;
    const float scaled_h = this->pixmap_.height() * this->scale_;
    const float slack_w = std::max(area.width() - scaled_w, 0.0f);
    const float slack_h = std::max(area.height() - scaled_h, 0.0f);
    return QPointF(slack_w, slack_h) / (2.0 * this->scale_);
}

bool Canvas::is_out_of_pixmap(const QPointF &p) {
    return is_out_of_image(p, this->pixmap_.size());
}

bool Canvas::should_constrain_to_pixmap(const QPointF &point) {
    return !this->allow_out_of_bounds_points_ && this->is_out_of_pixmap(point);
}

void Canvas::finalize() {
    //assert self._current is not None
    QList<TlShape> new_shapes;
    if (AI_CREATE_MODES.contains(this->create_mode_)) {
        try {
            new_shapes = this->build_new_shapes_from_ai_inference();
        } catch (std::exception &e) {
            this->report_inference_failure(e.what());
            this->cancel_current_shape();
            return;
        }
        this->ai_inference_failed_ = false;
        if (new_shapes.empty()) {
            emit this->inference_produced_no_shapes();
            this->cancel_current_shape();
            return;
        }
    } else {
        this->current_ = this->current_.close();
        if (is_degenerate_draft(this->current_)) {
            emit this->degenerate_shape_rejected();
            this->cancel_current_shape();
            return;
        }
        new_shapes = { draft_to_shape(this->current_) };
    }

    this->shapes_.append(new_shapes);
    this->backup_shapes();
    this->reset_after_shape_creation();
}

QList<TlShape> Canvas::build_new_shapes_from_ai_inference() {
    //assert this->_current is not None
    if (this->create_mode_ == "ai_points_to_shape")
        return this->shapes_from_ai_points(
            this->current_.points_,
            this->current_.point_labels_
        );
    if (this->create_mode_ == "ai_box_to_shape")
        // point_labels: 2=box corner, 3=opposite box corner (SAM convention)
        return this->shapes_from_ai_points(
            this->current_.points_,
            {2, 3}
        );
    throw std::invalid_argument("unreachable: " + this->create_mode_.toStdString());
}

void Canvas::reset_after_shape_creation() {
    this->current_.clear();
    emit this->new_shape();
    this->update();
}

void Canvas::cancel_current_shape() {
    this->current_.clear();
    emit this->drawing_polygon(false);
    this->update();
}

// Required by QScrollArea: it queries these to compute the
// scrollable viewport whenever adjustSize() is called.
QSize Canvas::compute_canvas_size() const {
    if (this->pixmap_.isNull())
        return QWidget::minimumSizeHint();
    const auto scaled_w = static_cast<int>(this->pixmap_.width() * this->scale_);
    const auto scaled_h = static_cast<int>(this->pixmap_.height() * this->scale_);
    const auto viewport = this->scroll_viewport();
    if (viewport == nullptr)
        return {scaled_w, scaled_h};
    const auto slack_w = compute_overscroll_slack(scaled_w, viewport->width());
    const auto slack_h = compute_overscroll_slack(scaled_h, viewport->height());
    return {scaled_w + slack_w, scaled_h + slack_h};
}

QSize Canvas::sizeHint() const {
    return this->compute_canvas_size();
}

QSize Canvas::minimumSizeHint() const {
    return this->compute_canvas_size();
}

void Canvas::wheelEvent(QWheelEvent *event) {
    const auto mods = event->modifiers();
    const auto delta = event->angleDelta();
    if (mods == Qt::KeyboardModifier::ControlModifier) {
        // with Ctrl/Command key
        // zoom
        emit this->zoom_request(delta.y(), event->position());
    } else if (mods == Qt::KeyboardModifier::ShiftModifier && delta.x() == 0) {
        // Shift+wheel scrolls horizontally. macOS swaps the axis for us,
        // but Linux/Windows deliver the delta on y and expect the app to
        // remap it.
        emit this->scroll_request(delta.y(), Qt::Orientation::Horizontal);
    } else {
        // scroll
        emit this->scroll_request(delta.x(), Qt::Orientation::Horizontal);
        emit this->scroll_request(delta.y(), Qt::Orientation::Vertical);
    }
    event->accept();
}

void Canvas::move_by_keyboard(const QPointF &offset) {
    if (this->selected_shapes_.empty())
        return;
    this->drag_shapes(this->shapes_, this->prev_point_ + offset, this->selected_shapes_);
    this->update();
    this->is_moving_shape_ = true;
}

void Canvas::keyPressEvent(QKeyEvent *event) {
    const auto modifiers = event->modifiers();
    const auto key = event->key();
    if (this->mode_ == CanvasMode::CREATE) {
        if (key == Qt::Key::Key_Escape && current_) {
            this->cancel_current_shape();
        } else if (
            (key == Qt::Key::Key_Return || key == Qt::Key::Key_Space) && this->can_close_shape()
        ) {
            this->finalize();
        } else if (modifiers == Qt::KeyboardModifier::AltModifier) {
            this->snapping_ = false;
        }
    } else if (this->mode_ == CanvasMode::EDIT) {
        if (key == Qt::Key::Key_Up) {
            this->move_by_keyboard(QPointF(0.0, -MOVE_SPEED));
        } else if (key == Qt::Key::Key_Down) {
            this->move_by_keyboard(QPointF(0.0, MOVE_SPEED));
        } else if (key == Qt::Key::Key_Left) {
            this->move_by_keyboard(QPointF(-MOVE_SPEED, 0.0));
        } else if (key == Qt::Key::Key_Right) {
            this->move_by_keyboard(QPointF(MOVE_SPEED, 0.0));
        } else if (event->matches(QKeySequence::StandardKey::SelectAll)) {
            this->select_shapes(this->shapes_);
        }
    }
    this->update_status();
}

void Canvas::keyReleaseEvent(QKeyEvent *event) {
    const auto modifiers = event->modifiers();
    if (this->mode_ == CanvasMode::CREATE) {
        if (modifiers == Qt::NoModifier)
            this->snapping_ = true;
    } else if (this->mode_ == CanvasMode::EDIT)
        if (
            this->is_moving_shape_
            && !this->selected_shapes_.empty()
            && this->selected_shapes_[0] < this->shapes_.size()
        ) {
            const auto index = this->selected_shapes_[0];
            if (
                shape_backups_.back()[index].points_ != this->shapes_[index].points_
            ) {
                this->backup_shapes();
                emit this->shape_moved();
            }
            this->is_moving_shape_ = false;
        }
}

QList<TlShape> Canvas::set_last_label(const QString &text, const int32_t group_id, const QString &description, const QMap<QString, bool> &flags) {
    if (text.isEmpty())
        throw std::invalid_argument("text must not be empty");
    QList<TlShape> shapes;
    for (auto &shape : this->shapes_ | std::views::reverse) {
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
    this->shape_backups_.pop_back();
    this->backup_shapes();
    return shapes;
}

void Canvas::undo_last_line() {
    //assert(self.shapes)
    if (AI_CREATE_MODES.contains(this->create_mode_)) {
        // Remove all unlabeled shapes at the tail (added by AI in one shot)
        while (!this->shapes_.empty() && this->shapes_.back().label_.isEmpty())
            this->shapes_.pop_back();
        this->cancel_current_shape();
        return;
    }
    this->current_ = shape_to_draft(this->shapes_.back()).open(); this->shapes_.pop_back();
    if (POLYLINE_SHAPE_TYPES.contains(this->create_mode_)) {
        this->line_.points_ = {
            this->current_[-1], this->current_[0]
        };
    } else if (QKey{
        "rectangle",
        "line",
        "circle",
        "ai_box_to_shape"}.contains(this->create_mode_)
    ) {
        this->current_.points_ = {
            this->current_.points_[0], this->current_.points_[1] };
        this->current_.point_labels_ = {
            this->current_.point_labels_[0], this->current_.point_labels_[1]
        };
    } else if (this->create_mode_ == "point") {
        this->current_.clear();
    } else {
        assert(this->create_mode_ == "oriented_rectangle");
    }
    emit this->drawing_polygon(true);
}

void Canvas::undo_last_point() {
    auto current = this->current_;
    if (!current || current.closed_)
        return;
    if (this->create_mode_ == "oriented_rectangle" && current.points_.size() == 4) {
        this->unlock_oriented_rectangle_first_edge(current);
        this->update();
        return;
    }
    current = current.pop_point();
    this->current_ = current;
    if (!current.points_.empty()) {
        this->line_.points_[0] = current[-1];
        this->update();
    } else {
        this->cancel_current_shape();
    }
}

void Canvas::reset_interaction_state() {
    this->current_.clear();
    this->hovered_shape_ = None;
    this->hovered_vertex_ = None;
    this->hovered_edge_ = None;
    this->hovered_rotation_ = None;
    this->clear_highlight_state();
}

void Canvas::load_pixmap(const QPixmap &pixmap, const bool clear_shapes, const QString &filename) {
    //pixmap_arr = _utils.img_qt_to_arr(img_qt=pixmap.toImage())
    this->pixmap_ = pixmap;
    this->pixmap_hash_ = std::hash<QString>{}(filename);
    // A new image is a fresh inference context that should surface its own
    // first failure rather than staying muted by the prior image's latch.
    this->ai_inference_failed_ = false;
    if (clear_shapes)
        this->shapes_.clear();
    this->update();
}

void Canvas::load_shapes(const QList<TlShape> &shapes, const bool replace) {
    this->shapes_ = replace ? shapes : this->shapes_ + shapes;
    this->backup_shapes();
    this->reset_interaction_state();
    this->update();
}

void Canvas::set_shape_visible(const TlShape &shape, const bool value) {
    for (auto &s : this->shapes_) {
        if (s != shape) { continue; }
        if (s.visible_ == value) { return; }
        s.visible_ = value;
        break;
    }
    this->update();
}

void Canvas::apply_cursor(const CursorRole role) {
    if (role == this->cursor_)
        return;
    const auto shape = cursor_shape_for(role);
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
    if (QKey{"circle", "line"}.contains(shape_type))
        return points.size() != 2 || points[0] == points[1];
    if (shape_type == "oriented_rectangle")
        return points.size() != 4 || points[0] == points[1] || points[1] == points[2];
    return false;
}

QList<QPointF> Canvas::normalize_bbox_points(const QList<QPointF> &bbox_points) {
    if (bbox_points.size() != 2)
        throw std::invalid_argument(std::format("Expected 2 points for bbox, got {}", bbox_points.size()));

    const auto p1 = bbox_points[0], p2 = bbox_points[1];
    const auto xmin = std::min(p1.x(), p2.x());
    const auto ymin = std::min(p1.y(), p2.y());
    const auto xmax = std::max(p1.x(), p2.x());
    const auto ymax = std::max(p1.y(), p2.y());
    return {QPointF(xmin, ymin), QPointF(xmax, ymax)};
}

QPointF Canvas::snap_cursor_pos_for_square(const QPointF &pos, const QPointF &opposite_vertex) {
    QPointF pos_from_opposite = pos - opposite_vertex;
    float square_size = std::min(abs(pos_from_opposite.x()), abs(pos_from_opposite.y()));
    return opposite_vertex + QPointF(
        np::sign(pos_from_opposite.x()) * square_size,
        np::sign(pos_from_opposite.y()) * square_size
    );
}

int32_t Canvas::compute_overscroll_slack(const int32_t scaled, const int32_t viewport) {
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
    const QList<int32_t> &selected_shapes, const int32_t hovered_shape
) {
    if (selected_shapes.empty())
        return true;
    if (hovered_shape == None)
        return false;
    return !selected_shapes.contains(hovered_shape);
}

TlShape Canvas::pick_pending_moved_shape(
    const bool is_moving_shape, const int32_t hovered_index, const QList<TlShape> &shapes
) {
    if (!is_moving_shape)
        return {};
    if (hovered_index == None)
        return {};
    if (hovered_index >= shapes.size())
        return {};
    return shapes[hovered_index];
}

QPointF Canvas::opposite_corner_in_parallelogram(
    const QPointF &opposite_to, const QPointF &neighbor1, const QPointF &neighbor2
) {
    return neighbor1 + neighbor2 - opposite_to;
}

QPair<QPointF, QPointF> Canvas::project_oriented_rectangle_corners(
    const QPointF &anchor, const QPointF &edge_axis, const QPointF &moving
) {
    auto perp = utils::project_point_on_perpendicular_line(
        moving, edge_axis, anchor
    );
    auto para = opposite_corner_in_parallelogram(
        perp, anchor, moving
    );
    return {perp, para};
}

bool Canvas::is_out_of_image(const QPointF &point, const QSize &image_size) {
    return (
        point.x() < 0
        || point.y() < 0
        || point.x() > image_size.width()
        || point.y() > image_size.height()
    );
}

QList<QPointF> Canvas::reproject_oriented_rectangle_corners(
    const QList<QPointF> &corners,
    const int32_t vertex_index,
    const QPointF &pos,
    const QSize &image_size,
    const bool allow_out_of_bounds
) {
    //Given a 4-corner oriented rectangle and a dragged corner, return the new
    //corner positions: the dragged corner and its two neighbors move so the shape
    //stays a parallelogram, clipped to the image unless out-of-bounds points are
    //allowed; the opposite anchor is fixed.
    const QPointF anchor = corners[(vertex_index - 2 + 4) % 4];
    const QPointF edge_axis = corners[(vertex_index - 1 + 4) % 4];
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
    new_corners[(vertex_index + 1 + 4) % 4] = adjacent_perp;
    new_corners[(vertex_index - 1 + 4) % 4] = adjacent_para;
    return new_corners;
}

//
// User-assisted function.
//
Canvas::~Canvas() {
    ai_assist_session_.reset();
}

void Canvas::update_shape_info(const TlShape &shape) {
    for (auto &s : this->shapes_) {
        if (s == shape) {
            s.label_                = shape.label_;
            s.flags_                = shape.flags_;
            s.group_id_             = shape.group_id_;
            s.description_          = shape.description_;
            return;
        }
    }
}
