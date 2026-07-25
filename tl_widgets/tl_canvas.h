#ifndef __INC_CANVAS_H
#define __INC_CANVAS_H

#include <QMenu>
#include <QWidget>
#include <QPainter>

#include "tl_shape.h"
#include "tl_modules/sam_session.h"
#include "tl_modules/ai_assist_thread.h"

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

struct ContextMenuPair {
    QMenu *without_selection;
    QMenu *with_selection;
};

inline QString ModeName(const CanvasMode c) {
    const static std::map<CanvasMode, QString> ModeNames = {
        {CanvasMode::CREATE, "CREATE"},
        {CanvasMode::EDIT,   "EDIT"},
    };
    const auto it = ModeNames.find(c);
    return (it != ModeNames.end()) ? it->second : "Unknown";
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
    void close() {
        closed_ = true;
    }
    void open() {
        closed_ = false;
    }

    void add_point(const QPointF &point, const int32_t label = 1, const bool autoclose = false) {
        if (autoclose && !points_.empty() && points_[0] == point) {
            closed_ = true;
            return;
        }

        points_.push_back(point);
        point_labels_.push_back(label);
    }

    void pop_point() {
        if (points_.empty()) {
            return;
        }

        points_.pop_back();
        point_labels_.pop_back();
    }

    bool                closed_{false};
    QString             shape_type_{"polygon"};
    QList<QPointF>      points_;
    QList<int32_t>      point_labels_;
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
    void zoomRequest(int32_t delta, QPointF pos);
    void scrollRequest(int32_t delta, Qt::Orientation orientation);
    void newShape();
    void selectionChanged(const QList<int32_t> &selected_shapes);   // 选中状态变化
    void shapeMoved();
    void drawingPolygon(bool drawing);
    void vertexSelected(bool value);
    void mouseMoved(QPointF pos);
    void statusUpdated(const QString &str);
    void aiAssistSubmit();
    void aiAssistFinish();

private:
    friend class MainWindow;
    QString                             create_mode_;
    bool                                fill_drawing_;
    bool                                show_labels_;

    float                               epsilon_{10.0};
    QString                             double_click_;
    int32_t                             num_backups_;
    QMap<QString, bool>                 crosshair_;

    CanvasMode                          mode_;

    std::string                         sam_session_model_name_{"sam2:latest"};
    std::unique_ptr<SamSession>         sam_session_{nullptr};
    std::string                         ai_output_format_;

    friend class AiAssistThread;
    std::mutex                          mutex_;  // lock for AI shape.
    std::unique_ptr<AiAssistThread>     ai_assist_thread_;
    QList<QPointF>                      ai_assist_points_;
    QList<TlShape>                      ai_assist_shapes_;

    QList<TlShape>                      shapes_;
    QList<QList<TlShape>>               shapesBackups_;    //多次复制记录.
    TlShape                             current_;
    QList<int32_t>                      selectedShapes_;
    QList<TlShape>                      selectedShapesCopy_;
    TlShape                             line_;

    QPointF                             prevPoint_;
    QPointF                             prevMovePoint_;
    QList<QPointF>                      offsets_;

    float                               scale_{1.0};
    QPixmap                             pixmap_;
    size_t                              pixmap_hash_;

    QMap<QString, bool>                 visible_;
    bool                                hideBackround_;
    bool                                hideBackround1_;
    int32_t                             hShape_;
    int32_t                             hVertex_;
    int32_t                             hEdge_;

    int32_t                             lasthShape_;
    int32_t                             lasthVertex_;
    int32_t                             lasthEdge_;

    bool                                movingShape_;
    bool                                snapping_;
    bool                                hShapeIsSelected_;

    QPainter                            painter_;
    QCursor                             cursor_;
    QPointF                             dragging_start_pos_;
    bool                                is_dragging_;
    bool                                is_dragging_enabled_;

    ContextMenuPair                     context_menus_;

    bool fillDrawing() const;
    void set_fill_drawing(bool value);
    bool is_drawing() const;
    QString createMode()  const;
    void createMode(const QString &value);
    std::string get_ai_model_name();
    void set_ai_model_name(const std::string &model_name);
    void set_ai_output_format(const std::string &output_format);
    SamSession &get_osam_session();
    QList<TlShape> shapes_from_points_ai(const QList<QPointF> &points, const QList<int32_t> &labels);
    QList<TlShape> shapes_from_bbox_ai(const QList<QPointF> &bbox_points);

    void storeShapes();
    bool can_restore_shape();
    void restore_last_shape();
    bool isVisible(const TlShape &shape);
    bool drawing();
    bool editing();
    void set_editing(bool value = true);
    bool set_highlight(int32_t hShape, int32_t hEdge, int32_t hVertex);
    bool selectedVertex();
    bool selectedEdge();
    void update_status(const std::list<QString> &extra_messages);
    QString get_create_mode_message();
    void highlight_hover_shape(QPointF pos, std::list<QString> &status_messages);
    void addPointToEdge();
    void removeSelectedPoint();
    void lock_oriented_rectangle_first_edge(TlShape &current);
    void unlock_oriented_rectangle_first_edge(TlShape &current);
    bool end_move(bool copy);
    void hideBackroundShapes(bool value);
    void setHiding(bool enable = true);
    bool can_close_shape();
    void selectShapes(const QList<TlShape> &shapes);
    void selectShapePoint(const QPointF &point, bool multiple_selection_mode);
    void calculateOffsets(const QPointF &point);
    void boundedMoveVertex(TlShape &shape, int32_t vertex_index, QPointF pos, bool is_shift_pressed);
    bool boundedMoveShapes(QList<TlShape> &shapes, const QList<int32_t> &indexes, QPointF pos);
    bool deSelectShape();
    QList<TlShape> deleteSelected();
    void deleteShape(const TlShape &shape);

    void render_canvas();
    void setup_world_transform(QPainter &painter);
    void render_layers();
    void draw_pixmap_layer(QPainter &painter);
    void draw_crosshair_layer(QPainter &painter);
    bool should_draw_crosshair(QPointF &cursor);
    void draw_committed_shapes_layer(QPainter &painter);
    void draw_active_shape_layer(QPainter &painter);
    void draw_drag_copy_layer(QPainter &painter);
    void draw_preview_overlay_layer(QPainter &painter);

    QPointF transformPos(QPointF point);
    void enableDragging(bool enabled);
    QPointF offsetToCenter();
    bool outOfPixmap(const QPointF &p);
    void finalize();
    bool closeEnough(const QPointF &p1, const QPointF &p2);
    void moveByKeyboard(QPointF offset);
    QList<TlShape> setLastLabel(const QString &text, int32_t group_id, const QString &description, const QMap<QString, bool> &flags);
    void undoLastLine();
    void undoLastPoint();
    void loadPixmap(const QPixmap &pixmap, const QString &filename, bool clear_shapes = true);
    void loadShapes(const QList<TlShape> &shapes, bool replace = true);
    void setShapeVisible(const TlShape &shape, bool value);
    void overrideCursor(const QCursor &cursor);
    void restoreCursor();
    void resetState();

    TlShape shape_from_annotation(const Annotation &annotation, const std::string &output_format);
    QList<TlShape> shapes_from_ai_response(GenerateResponse &response, const std::string &output_format);

    QPointF snap_cursor_pos_for_square(QPointF pos, QPointF opposite_vertex);

    QPointF compute_intersection_edges_image(const QPointF &p1, const QPointF &p2, const QSize &image_size);
    std::vector<std::tuple<qreal, int32_t, QPointF>> compute_intersection_edges(
        const QPointF &point1, const QPointF &point2, const std::vector<QPointF> &points
    );

    void update_shape_info(const TlShape &shape);

    void submit_shape_with_ai(const QList<QPointF> &points, const QList<int32_t> &labels);
    void update_shape_with_ai(const QList<QPointF> &points, const QList<int32_t> &labels);
};
#endif //__INC_CANVAS_H