#ifndef __INC_MAINWINDOW_H
#define __INC_MAINWINDOW_H

#include <QMainWindow>
#include <QWidgetAction>
#include <QGraphicsScene>
#include <QProgressDialog>
#include <QSettings>
#include <QProcess>

#include "tl_widgets/tl_canvas.h"
#include "tl_widgets/tl_shape_list.h"
#include "tl_widgets/tl_label_list.h"
#include "tl_widgets/tl_label_file.h"
#include "tl_widgets/tl_label_dialog.h"
#include "tl_widgets/zoom_widget.h"
#include "tl_widgets/tl_train_widget.h"
#include "tl_widgets/status_stats.h"
#include "tl_widgets/shape_clipboard.h"
#include "tl_modules/ai_assist_annotation.h"
#include "tl_modules/ai_prompt_annotation.h"
#include "yaml-cpp/yaml.h"


QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

#define _appname_                           tr("tl assistant")
#define _version_                           "1.0.0.0"

enum class ZoomMode : int32_t {
    FIT_WIDTH,
    FIT_WINDOW,
    MANUAL_ZOOM
};

struct StatusBarWidgets {
    QLabel                                 *message_{};                                 // QtWidgets.QLabel
    StatusStats                            *stats_{};                                   // StatusStats
};

struct CanvasWidgets {
    Canvas                                 *canvas_{};                                  // Canvas
    ZoomWidget                             *zoom_widget_{};                             // ZoomWidget
    QMap<Qt::Orientation, QScrollBar *>     scroll_bars_;                               // dict[Qt.Orientation, QtWidgets.QScrollBar]
};

struct DockWidgets {
    QDockWidget                            *flag_dock_{};                               // QtWidgets.QDockWidget
    QListWidget                            *flag_list_{};                               // QtWidgets.QListWidget
    QDockWidget                            *shape_dock_{};                              // QtWidgets.QDockWidget
    ShapeListView                          *shape_list_{};                              // LabelListWidget
    QDockWidget                            *label_dock_{};                              // QtWidgets.QDockWidget
    LabelList                              *label_list_{};                              // UniqueLabelQListWidget
    QDockWidget                            *file_dock_{};                               // QtWidgets.QDockWidget
    QLineEdit                              *file_search_{};                             // QtWidgets.QLineEdit
    QListWidget                            *file_list_{};                               // QtWidgets.QListWidget
};

struct Actions {
    QAction                                *about_{};                                   // QtGui.QAction
    QAction                                *save_{};                                    // QtGui.QAction
    QAction                                *save_as_{};                                 // QtGui.QAction
    QAction                                *save_auto_{};                               // QtGui.QAction
    QAction                                *save_with_image_data_{};                    // QtGui.QAction
    QAction                                *change_output_dir_{};                       // QtGui.QAction
    QAction                                *open_{};                                    // QtGui.QAction
    QAction                                *close_{};                                   // QtGui.QAction
    QAction                                *delete_file_{};                             // QtGui.QAction
    QAction                                *toggle_keep_prev_mode_{};                   // QtGui.QAction
    QAction                                *toggle_keep_prev_brightness_contrast_{};    // QtGui.QAction
    QAction                                *delete_{};                                  // QtGui.QAction
    QAction                                *edit_{};                                    // QtGui.QAction
    QAction                                *duplicate_{};                               // QtGui.QAction
    QAction                                *copy_{};                                    // QtGui.QAction
    QAction                                *paste_{};                                   // QtGui.QAction
    QAction                                *undo_last_point_{};                         // QtGui.QAction
    QAction                                *undo_{};                                    // QtGui.QAction
    QAction                                *add_point_to_edge_{};                       // QtGui.QAction
    QAction                                *remove_point_{};                            // QtGui.QAction
    QAction                                *create_mode_{};                             // QtGui.QAction
    QAction                                *edit_mode_{};                               // QtGui.QAction
    QAction                                *create_rectangle_mode_{};                   // QtGui.QAction
    QAction                                *create_oriented_rectangle_mode_{};          // QtGui.QAction
    QAction                                *create_circle_mode_{};                      // QtGui.QAction
    QAction                                *create_line_mode_{};                        // QtGui.QAction
    QAction                                *create_point_mode_{};                       // QtGui.QAction
    QAction                                *create_line_strip_mode_{};                  // QtGui.QAction
    QAction                                *create_ai_points_to_shape_mode_{};          // QtGui.QAction
    QAction                                *create_ai_box_to_shape_mode_{};             // QtGui.QAction
    QAction                                *open_next_img_{};                           // QtGui.QAction
    QAction                                *open_prev_img_{};                           // QtGui.QAction
    QAction                                *keep_prev_zoom_{};                          // QtGui.QAction
    QAction                                *fit_window_{};                              // QtGui.QAction
    QAction                                *fit_width_{};                               // QtGui.QAction
    QAction                                *brightness_contrast_{};                     // QtGui.QAction
    QAction                                *zoom_in_{};                                 // QtGui.QAction
    QAction                                *zoom_out_{};                                // QtGui.QAction
    QAction                                *zoom_org_{};                                // QtGui.QAction
    QAction                                *reset_layout_{};                            // QtGui.QAction
    QAction                                *fill_drawing_{};                            // QtGui.QAction
    QAction                                *hide_all_{};                                // QtGui.QAction
    QAction                                *show_all_{};                                // QtGui.QAction
    QAction                                *toggle_all_{};                              // QtGui.QAction
    QAction                                *open_dir_{};                                // QtGui.QAction
    QWidgetAction                          *zoom_widget_action_{};                      // QtWidgets.QWidgetAction
    QList<QPair<QString, QAction *>>        draw_;                                      // list[tuple[str, QtGui.QAction]]
    std::list<QAction *>                    zoom_;                                      // tuple[ZoomWidget | QtGui.QAction, ...]
    std::list<QAction *>                    on_load_active_;                            // tuple[QtGui.QAction, ...]
    std::list<QAction *>                    on_shapes_present_;                         // tuple[QtGui.QAction, ...]
    std::list<QObject *>                    context_menu_;                              // tuple[QtGui.QAction, ...]
    std::list<QAction *>                    edit_menu_;                                 // tuple[QtGui.QAction | None, ...]
};

struct Menus {
    QMenu                                  *file_{};                                    // QtWidgets.QMenu
    QMenu                                  *edit_{};                                    // QtWidgets.QMenu
    QMenu                                  *view_{};                                    // QtWidgets.QMenu
    QMenu                                  *help_{};                                    // QtWidgets.QMenu
    QMenu                                  *label_list_{};                              // QtWidgets.QMenu
};


class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(const QString &config_file,
               const YAML::Node &config_overrides,
               const QString &file_or_dir,
               const QString &output_dir);
    ~MainWindow() override;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    Ui::MainWindow                                 *ui_{};

    QString                                         config_file_;
    YAML::Node                                      config_;
    QSettings                                       window_state_;
    QByteArray                                      default_state_;
    QString                                         output_dir_;
    QString                                         output_file_;
    QString                                         prev_opened_dir_;
    QPoint                                          label_list_menu_origin_;

    bool                                            is_changed_{false};
    QList<TlShape>                                  copied_shapes_;
    ShapeClipboard                                 *shape_clipboard_{};
    LabelDialog                                    *label_dialog_{};

    QMap<Qt::Orientation, QMap<QString, int32_t>>   scroll_values_;

    ZoomMode                                        zoom_mode_{ZoomMode::FIT_WINDOW};
    CanvasWidgets                                   canvas_widgets_;
    StatusBarWidgets                                status_bar_;
    DockWidgets                                     docks_;
    Actions                                         actions_;
    Menus                                           menus_;

    QMap<QString, std::pair<ZoomMode, float>>       zoom_values_;
    QMap<QString, std::pair<int32_t, int32_t>>      brightness_contrast_values_;

    QImage                                          image_;
    AnnotationEx                                    annotation_;
    QString                                         image_path_;
    QString                                         prev_image_path_;
    QByteArray                                      imageData_;
    QByteArray                                      other_data_;
    QString                                         label_file_path_;

    std::string                                     sam_model_name_{"efficientsam:latest"};
    std::unique_ptr<SamSession>                     text_osam_session_{};

    AiAssistAnnotation                             *ai_assist_annotation_{};
    AiPromptAnnotation                             *ai_prompt_annotation_{};
    QProgressDialog                                *progress_dialog_{};
    bool                                            ai_buttons_highlighted_{false};

    //QAction                                        *actSetup_{};
    //QAction                                        *actTrain_{};
    //QAction                                        *actInfer_{};
    TlTrainWidget                                  *train_widget_{};
    QProcess                                       *sub_process_{};

    void retheme();
    Actions setup_actions();
    Menus setup_menus();
    void setup_toolbars();
    void setup_app_state(const QString &file_or_dir, const QString &output_dir);
    StatusBarWidgets setup_status_bar();
    CanvasWidgets setup_canvas();
    DockWidgets setup_dock_widgets();
    QString load_config(QString config_file, const YAML::Node &config_overrides);
    QMenu *menu(const QString &title, const std::list<QObject *> &actions={});
    bool has_no_shapes() const;
    void populate_mode_actions();
    QString get_window_title(bool dirty);
    void mark_dirty();
    void mark_clean();
    void update_action_states(bool value=true);
    void show_status_message(const QString &message, int32_t delay=5000);
    void submit_ai_prompt();
    void reset_state();
    void undo_shape_edit();
    void tutorial() const;
    void on_drawing_polygon_changed(bool drawing=true);
    void switch_canvas_mode(bool edit, const QString &create_mode="");
    void highlight_ai_buttons(bool highlight);
    void show_label_list_menu(const QPoint &point);
    bool validate_label(const QString &label);
    void edit_label(bool value=false);
    void on_file_search_changed();
    void file_list_item_selection_changed();
    void on_shape_selection_changed(const QList<int32_t> &selected_shapes);
    void add_label(const TlShape &shape);
    std::vector<int32_t> get_rgb_by_label(const QString &label, LabelList *unique_label_list);
    void remove_labels(const QList<TlShape> &shapes);
    void load_shapes(const QList<TlShape> &shapes, bool replace=true);
    void load_flags(const YAML::Node &flags, QListWidget *widget) const;
    bool save_labels(const QString &label_path);
    void insert_shapes(const QList<TlShape> &shapes);
    void label_selection_changed();
    void on_label_item_changed(ShapeListItem *item);
    void on_label_order_changed();
    void on_new_shape();
    void on_inference_produced_no_shapes();
    void on_inference_failed(const QString &message);
    void on_scroll_request(int32_t delta, Qt::Orientation orientation);
    void on_pan_request(const QPoint &step);
    void set_scroll_value(Qt::Orientation orientation, float value);
    void set_zoom(int32_t value, QPointF pos=QPointF());
    void set_zoom_to_original();
    void add_zoom(float increment=1.1, const QPointF &pos=QPointF());
    void zoom_requested(int32_t delta, const QPointF &pos);
    void set_fit_window_mode(bool value=true);
    void set_fit_width_mode(bool value=true);
    void switch_zoom_mode(ZoomMode mode);
    void sync_zoom_mode_actions();
    void on_brightness_contrast_changed(const QImage &image);
    void open_brightness_contrast_dialog(bool value=false, bool is_initial_load=false);
    void toggle_shape_visibility(int32_t value);
    AnnotationEx open_label_file_into_state(const QString &label_path);
    bool open_image_into_state(const QString &image_path);
    void load_file(const QString &image_or_label_path);
    //def resizeEvent(self, a0: QtGui.QResizeEvent) -> None:
    void paint_canvas();
    void adjust_scale();
    float fit_window_scale() const;
    float fit_width_scale() const;
    void set_save_image_with_data(bool enabled);
    void reset_layout();
    //def closeEvent(self, a0: QtGui.QCloseEvent) -> None:
    //def dragEnterEvent(self, a0: QtGui.QDragEnterEvent) -> None:
    //def dropEvent(self, a0: QtGui.QDropEvent) -> None:
    void open_prev_image(bool value=false);
    void open_next_image(bool value=false);
    void open_file_with_dialog(bool value=false);
    void prompt_output_dir(bool value=false);
    void save_label_file(bool save_as=false);
    QString prompt_save_file_path();
    void close_file(bool value=false);
    QString current_label_file_path();
    bool confirm_deletion(const QString &message);
    void delete_file();
    bool is_settings_editable();
    LabelDialog *make_label_dialog();
    bool on_setting_changed(const QString &key_path, QObject value);
    void apply_to_live_widgets(const QString &key_path);
    QMap<QString, bool> read_flag_dock_states();
    void open_settings();
    void open_config_file();
    bool has_label_file();
    bool can_continue();
    void show_error_message(const QString &title, const QString &message);
    void show_file_open_error(const QString &path, const QString &file_kind, const QString &exc, const QString &extra);
    QString current_path();
    void remove_selected_point();
    void delete_selected_shapes();
    void copy_shape();
    void move_shape();
    void load_from_file_or_dir(const QString &file_or_dir);
    void open_dir_with_dialog(bool value=false);
    QStringList image_list() const;
    void import_dropped_image_files(const QStringList &image_files);
    void import_images_from_dir(const QString &root_dir, const QString &pattern="");
    void update_status_stats(const QPointF &mouse_pos);

    static QList<TlShape> shapes_from_dicts(const QList<ShapeDict> &shape_dicts, const QMap<QString, QList<QString>> &label_flags);
    static QString resolve_text_annotation_shape_type(const QString &create_mode, const QString &ai_output_format);
    static std::vector<int32_t> rgb_from_colormap_id(int32_t label_id);
    static std::vector<int32_t> rgb_from_label_colors(const std::string &label, const std::map<std::string, std::vector<int32_t>> &label_colors);
    static bool is_valid_label(const QString &label, const QStringList &existing_labels, const QString &policy);
    static QString format_window_title(const QString &image_path, int32_t file_index, int32_t file_count, const QImage &image, bool dirty);
    static QString resolve_label_path(const QString &image_or_label_path, const QString &output_dir="");
    static QListWidgetItem *make_image_list_item(const QString &image_path, const QString &output_dir);
    static ShapeDict shape_to_dict(const TlShape &shape);
    static QStringList scan_image_files(const QString &root_dir);

    QListWidgetItem *current_item() const;
    TlShape canvas_shape(const TlShape &shape) const;


private slots:
    void slotTaskSubmit();
    void slotTaskFinish();
    // slot for DeepLearning
    void slotActionSetup();
    void slotActionTrain();
    void slotActionInfer();
    void slotReadyReadStandardOutput();
    void slotReadyReadStandardError();
    void slotFinishedProcess(int32_t exitCode, QProcess::ExitStatus exitStatus);
    void slotProcessExited(int32_t exitCode, QProcess::ExitStatus exitStatus);
    void slotError(QProcess::ProcessError);
};
#endif //__INC_MAINWINDOW_H