#ifndef __INC_CANVAS_H
#define __INC_CANVAS_H

#include <QMenu>
#include <QWidget>
#include <QPainter>

#include "tl_shape.h"
#include "shape_render.h"
#include "canvas_interaction.h"
#include "tl_assists/ai_assist_session.h"

// 信号与槽和设计模式中的观察者模式很类似
// emit
// signals
// slot

// 在使用信号与槽机制时, 需要在QObject的子类中添加Q_OBJECT宏
// 这个宏会在编译过程中使用元对象系统自动生成必要的代码, 以支持信号与槽的运行时连接

//三种信号绑定方式:
// //1. 函数无参数的时候, 使用宏
// QObject::connect(qAction, SIGNAL(triggered()), this, SLOT(DealSlot()));
//
// //2. 函数无参数的时候, 使用函数指针
// QObject::connect(qAction, &QAction::triggered, this, &MainWindow::DealSlot);
//
// //3. 函数带参数的时候, 使用函数指针
// void (QAction::*fnSignal)(bool) = &QAction::triggered;
// void (MainWindow::*fnSlot)() = &MainWindow::DealSlot;
// QObject::connect(qAction, fnSignal, this, fnSlot);
//
// QObject::connect(qAction, SIGNAL(toggled(bool)), this, SLOT(setChecked(bool)));


enum class CanvasMode: int32_t {
    CREATE = 0,
    EDIT   = 1,
};

inline QString ModeName(const CanvasMode c) {
    const static std::map<CanvasMode, QString> ModeNames = {
        {CanvasMode::CREATE, "CREATE"},
        {CanvasMode::EDIT,   "EDIT"},
    };
    const auto it = ModeNames.find(c);
    return it != ModeNames.end() ? it->second : "Unknown";
}

//#define ENUM_TO_STRING(EnumType, ...)   \
//    constexpr const char *EnumType##ToString(EnumType value) {                  \
//        size_t index = static_cast<size_t>(value);                              \
//        static constexpr const char *strings[] = { __VA_ARGS__ };               \
//        return (index < std::size(strings)) ? strings[index] : "Unknown";       \
//    }
//ENUM_TO_STRING(CanvasMode, "CREATE", "EDIT");

class DraftShape {
public:
    QString             shape_type_{"polygon"};
    QList<QPointF>      points_;
    QList<int32_t>      point_labels_;
    bool                closed_{false};

    DraftShape close();
    DraftShape open();

    DraftShape add_point(const QPointF &point, int32_t label=1, bool autoclose=false);
    DraftShape pop_point();

public:
    void clear();

    bool empty() const {
        return this->points_.empty();
    }

    int32_t size() const{
        return static_cast<int32_t>(this->points_.size());
    }

    QPointF operator[](int32_t index) const {
        return (index >= 0) ? this->points_[index] : this->points_[this->points_.size() + index];
    }

    explicit operator bool() const {
        return !this->points_.empty();
    }
};

class Canvas : public QWidget {
    Q_OBJECT
public:
    explicit Canvas(float epsilon,
                    const QString &double_click="close",
                    int32_t num_backups=10,
                    const QMap<QString, bool> &crosshair={},
                    bool allow_out_of_bounds_points=false);
    ~Canvas() override;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

signals:
    void zoom_request(int32_t delta, QPointF pos);
    void scroll_request(int32_t delta, Qt::Orientation orientation);
    void pan_request(QPoint pos);
    void new_shape();
    void inference_produced_no_shapes();
    void inference_failed(const QString &message);
    void degenerate_shape_rejected();
    void selection_changed(const QList<int32_t> &selected_shapes);   // 选中状态变化
    void shape_moved();
    void drawing_polygon(bool drawing);
    void vertex_selected(bool value);
    void edge_selected(bool value);
    void mouse_moved(QPointF pos);
    void status_updated(const QString &str);
    void aiAssistSubmit();
    void aiAssistFinish();

private:
    friend class MainWindow;
    friend class AiAssistSession;
    using fColorResolver = std::function<std::vector<int32_t>(const QString &)>;
    QPixmap                             pixmap_;
    size_t                              pixmap_hash_;
    QString                             create_mode_;
    bool                                fill_drawing_;
    bool                                show_labels_;
    fColorResolver                      color_resolver_;
    int32_t                             point_size_;
    QString                             point_type_;
    Palette                             draft_palette_;
    QMap<QString, Palette>              palette_cache_;             // : dict[str, Palette]
    CanvasMode                          mode_;

    QPair<QPointF, QRectF>              drag_anchor_;               // : tuple[QPointF, QRectF]
    QPointF                             rotation_center_;           // : np.ndarray
    double                              rotation_initial_angle_;    // : float
    QList<QPointF>                      rotation_original_points_;  // : np.ndarray
    QPointF                             pan_anchor_;                // : QPointF | None

    VertexHighlight                     highlight_;
    VertexHighlight                     rotation_highlight_;

    float                               scale_{1.0};
    float                               epsilon_{10.0};
    QString                             double_click_;
    int32_t                             num_backups_{};
    bool                                allow_out_of_bounds_points_{};
    QMap<QString, bool>                 crosshair_;

    std::unique_ptr<AiAssistSession>    ai_assist_session_;
    bool                                ai_inference_failed_{};

    QList<TlShape>                      shapes_;
    QList<QList<TlShape>>               shape_backups_;    //多次复制记录.
    bool                                is_moving_shape_{};
    DraftShape                          current_;
    QList<int32_t>                      selected_shapes_;
    QList<TlShape>                      selected_shapes_copy_;
    DraftShape                          line_;

    QPointF                             prev_point_;
    QPointF                             prev_move_point_;
    int32_t                             hovered_shape_{None};
    int32_t                             hovered_vertex_{None};
    int32_t                             hovered_edge_{None};
    int32_t                             hovered_rotation_{None};
    int32_t                             last_hovered_shape_{None};
    int32_t                             last_hovered_vertex_{None};
    int32_t                             last_hovered_edge_{None};

    bool                                snapping_{};
    bool                                hovered_shape_is_selected_{};

    QPainter                            painter_;
    CursorRole                          cursor_;

    ContextMenuPair                     context_menus_;
    QPoint                              context_menu_origin_;

    void set_fill_drawing(bool value);
    void set_show_labels(bool value);
    void set_allow_out_of_bounds_points(bool value);
    void set_color_resolver(const fColorResolver &resolver);
    void set_point_size(int32_t point_size);
    Palette resolve_palette(const QString &label);
    void set_draft_palette(const Palette &palette);
    void highlight_vertex(int32_t index, const QString &mode);
    void highlight_rotation_point(int32_t index, const QString &mode);
    void clear_highlight_state();
    ShapeRenderContext render_context(const TlShape &shape, int32_t index, bool highlighted);
    ShapeRenderContext draft_render_context(bool selected, bool fill, const VertexHighlight &highlight, const VertexHighlight &rotation_highlight);
    bool is_drawing() const;
    QString create_mode() const;
    void create_mode(const QString &value);
    void reconcile_partial_shape_on_mode_switch(const QString &old_mode, const QString &new_mode);
    std::string get_ai_model_name();
    void set_ai_model_name(const std::string &model_name);
    void set_ai_output_format(const std::string &output_format);
    QList<TlShape> shapes_from_ai_points(const QList<QPointF> &points, const QList<int32_t> &point_labels);
    void report_inference_failure(const QString& error);
    void backup_shapes();
    bool can_restore_shape();
    void restore_last_shape();
    //def enterEvent(self, a0: QtCore.QEvent) -> None:
    //def leaveEvent(self, a0: QtCore.QEvent) -> None:
    //def focusOutEvent(self, a0: QtGui.QFocusEvent) -> None:
    void set_editing(bool value = true);
    bool set_highlight(int32_t hovered_shape, int32_t hovered_edge, int32_t hovered_vertex, int32_t hovered_rotation);
    bool is_vertex_selected() const;
    bool is_edge_selected() const;
    bool is_rotation_point_selected() const;
    void update_status(const QList<QString> &extra_messages={});
    QString get_create_mode_message();
    //def mouseMoveEvent(self, a0: QtGui.QMouseEvent) -> None:
    void dispatch_pointer_move(const QPointF &pos, QMouseEvent *event);
    void advance_pan(QMouseEvent *event);
    void track_drawing_cursor(QPointF pos, QMouseEvent *event);
    QPointF project_drawing_pos_into_image(const QPointF &pos);
    bool cursor_should_snap_to_polygon_origin(const QPointF &pos);
    void refresh_hover_state(const QPointF &pos);
    QPointF update_drawing_line(QPointF pos, bool is_shift_pressed);
    void continue_right_button_drag(const QPointF &pos);
    void continue_left_button_drag(const QPointF &pos, QMouseEvent *event);
    void drag_hovered_vertex(const QPointF &pos, bool is_shift_pressed);
    void drag_hovered_rotation_point(const QPointF &pos);
    void capture_rotation_anchors();
    void drag_selected_shapes(const QPointF &pos);
    void highlight_hover_shape(const QPointF &pos, QList<QString> &status_messages);
    void add_point_to_edge();
    bool remove_selected_point();
    //def mousePressEvent(self, a0: QtGui.QMouseEvent) -> None:
    void dispatch_pointer_press(const QPointF &pos, QMouseEvent *event);
    void press_left(const QPointF &pos, QMouseEvent *event);
    void press_left_while_drawing(const QPointF &pos, QMouseEvent *event, bool is_shift_pressed);
    void extend_current_shape(DraftShape current, QMouseEvent *event);
    void lock_oriented_rectangle_first_edge(const DraftShape &current);
    void unlock_oriented_rectangle_first_edge(const DraftShape &current);
    void start_new_shape(const QPointF &pos, QMouseEvent *event, bool is_shift_pressed);
    void press_left_while_editing(const QPointF &pos, QMouseEvent *event);
    bool maybe_modify_polygon_topology(Qt::KeyboardModifiers modifiers);
    void press_right(const QPointF &pos, QMouseEvent *event);
    void begin_pan(QMouseEvent *event);
    //def mouseReleaseEvent(self, a0: QtGui.QMouseEvent) -> None:
    void dispatch_pointer_release(QMouseEvent *event);
    void release_right(QMouseEvent *event);
    void release_left();
    void finish_pan();
    bool is_image_overflowing_viewport();
    QWidget *scroll_viewport() const;
    void commit_pending_shape_move();
    bool end_move(bool copy);
    void apply_copy_move();
    void apply_in_place_move();
    bool can_close_shape();
    //def mouseDoubleClickEvent(self, a0: QtGui.QMouseEvent) -> None:
    void select_shapes(const QList<TlShape> &shapes);
    void select_shape_point(const QPointF &point, bool multiple_selection_mode);
    int32_t find_shape_at_point(const QPointF &point) const;
    void record_drag_anchor(const QPointF &click);
    void bounded_move_vertex(TlShape &shape, int32_t vertex_index, QPointF pos, bool is_shift_pressed);
    void bounded_move_oriented_rectangle_vertex(TlShape &shape, int32_t vertex_index, const QPointF &pos);
    bool drag_shapes(QList<TlShape> &shapes, const QPointF &cursor, const QList<int32_t> &indexes={});
    bool deselect_shape();
    QList<TlShape> delete_selected();
    void delete_shape(const TlShape &shape);
    //def paintEvent(self, a0: QtGui.QPaintEvent) -> None:
    void render_canvas();
    void setup_world_transform(QPainter &painter);
    QList<std::function<void(QPainter &)>> render_layers();
    void draw_pixmap_layer(QPainter &painter);
    void draw_crosshair_layer(QPainter &painter);
    bool should_draw_crosshair(const QPointF &cursor);
    void draw_committed_shapes_layer(QPainter &painter);
    void draw_active_shape_layer(QPainter &painter);
    void draw_drag_copy_layer(QPainter &painter);
    void draw_preview_overlay_layer(QPainter &painter);
    void render_draft(QPainter &painter, const DraftShape &draft, bool highlighted);
    TlShape build_preview_shape();
    TlShape build_polygon_preview(const DraftShape &current);
    TlShape build_ai_points_preview(DraftShape current);
    QPointF transform_point_widget_to_image(const QPointF &point);
    QPointF compute_image_origin_offset();
    bool is_out_of_pixmap(const QPointF &p);
    bool should_constrain_to_pixmap(const QPointF &point);
    void finalize();
    QList<TlShape> build_new_shapes_from_ai_inference();
    void reset_after_shape_creation();
    void cancel_current_shape();
    QSize compute_canvas_size() const;
    //def sizeHint(self) -> QtCore.QSize:
    //def minimumSizeHint(self) -> QtCore.QSize:
    //def wheelEvent(self, a0: QtGui.QWheelEvent) -> None:
    void move_by_keyboard(const QPointF &offset);
    //def keyPressEvent(self, a0: QtGui.QKeyEvent) -> None:
    //def keyReleaseEvent(self, a0: QtGui.QKeyEvent) -> None:
    QList<TlShape> set_last_label(const QString &text, int32_t group_id, const QString &description, const QMap<QString, bool> &flags);
    void undo_last_line();
    void undo_last_point();
    void reset_interaction_state();
    void load_pixmap(const QPixmap &pixmap, bool clear_shapes=true, const QString &filename="");
    void load_shapes(const QList<TlShape> &shapes, bool replace=true);
    void set_shape_visible(const TlShape &shape, bool value);
    void apply_cursor(CursorRole role);
    void release_cursor();
    void reset_state();

    static bool is_degenerate_draft(const DraftShape &draft);
    static QList<QPointF> normalize_bbox_points(const QList<QPointF> &bbox_points);
    static QPointF snap_cursor_pos_for_square(const QPointF &pos, const QPointF &opposite_vertex);
    static int32_t compute_overscroll_slack(int32_t scaled, int32_t viewport);
    static QPointF compute_intersection_edges_image(const QPointF &p1, const QPointF &p2, const QSize &image_size);
    static bool should_reselect_on_right_press(const QList<int32_t> &selected_shapes, int32_t hovered_shape);
    static TlShape pick_pending_moved_shape(bool is_moving_shape, int32_t hovered_index, const QList<TlShape> &shapes);
    static QPointF opposite_corner_in_parallelogram(const QPointF &opposite_to, const QPointF &neighbor1, const QPointF &neighbor2);
    static QPair<QPointF, QPointF> project_oriented_rectangle_corners(const QPointF &anchor, const QPointF &edge_axis, const QPointF &moving);
    static bool is_out_of_image(const QPointF &point, const QSize &image_size);
    static QList<QPointF> reproject_oriented_rectangle_corners(const QList<QPointF> &corners, int32_t vertex_index, const QPointF &pos, const QSize &image_size, bool allow_out_of_bounds);


    void update_shape_info(const TlShape &shape);
};
#endif //__INC_CANVAS_H