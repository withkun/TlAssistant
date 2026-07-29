#ifndef __INC_MAINWINDOW_H
#define __INC_MAINWINDOW_H

#include <QMainWindow>
#include <QWidgetAction>
#include <QGraphicsScene>
#include <QProgressDialog>
#include <QScrollArea>
#include <QSettings>
#include <QProcess>

#include "tl_widgets/tl_canvas.h"
#include "tl_widgets/tl_tool_bar.h"
#include "tl_widgets/tl_shape_list.h"
#include "tl_widgets/tl_label_list.h"
#include "tl_widgets/tl_label_file.h"
#include "tl_widgets/tl_label_dialog.h"
#include "tl_widgets/zoom_widget.h"
#include "tl_widgets/tl_train_widget.h"
#include "tl_widgets/status_stats.h"
#include "tl_widgets/shape_clipboard.h"

#include "tl_modules/ai_assist_annotation.h"
#include "tl_modules/ai_text_to_annotation.h"
#include "yaml-cpp/yaml.h"


QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE


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
    std::list<QPair<QString, QAction *>>    draw_;                                      // list[tuple[str, QtGui.QAction]]
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

    QScrollArea                                    *scroll_area_{};
    QMap<Qt::Orientation, QMap<QString, int32_t>>   scroll_values_;

    ZoomMode                                        zoom_mode_{ZoomMode::FIT_WINDOW};
    CanvasWidgets                                   canvas_widgets_;
    StatusBarWidgets                                status_bar_;
    DockWidgets                                     docks_;
    Actions                                         actions_;
    Menus                                           menus_;

    QMap<QString, std::pair<ZoomMode, float>>       zoom_values_;
    QMap<QString, std::pair<int32_t, int32_t>>      brightness_contrast_values_;
    QMap<ZoomMode, std::function<float()>>          scalers_;

    QImage                                          image_;
    QString                                         image_path_;
    QString                                         prev_image_path_;
    QByteArray                                      imageData_;
    QByteArray                                      other_data_;
    std::unique_ptr<LabelFile>                      label_file_;

    std::string                                     sam_model_name_{"efficientsam:latest"};
    std::unique_ptr<SamSession>                     text_osam_session_{};

    AiAssistAnnotation                             *ai_assist_annotation_widget_{};
    AiTextToAnnotation                             *ai_text_to_annotation_widget_{};
    QWidgetAction                                  *select_ai_model_{};
    QWidgetAction                                  *ai_prompt_action_{};
    QProgressDialog                                *progress_dialog_{};
    bool                                            ai_buttons_highlighted_{false};

    //QAction                                        *actSetup_{};
    //QAction                                        *actTrain_{};
    //QAction                                        *actInfer_{};
    TlTrainWidget                                  *train_widget_{};
    QProcess                                       *sub_process_;

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
    QListWidgetItem *current_item();
    void undo_shape_edit();
    void tutorial();
    void about();
    void on_drawing_polygon_changed(bool drawing=true);
    void switch_canvas_mode(bool edit, const QString &create_mode="");
    void highlight_ai_buttons(bool highlight);
    void show_label_list_menu(const QPoint &point);
    bool validate_label(const QString &label);
    void edit_label(bool value=false);
    void on_file_search_changed();
    void file_list_item_selection_changed();
    void shapeSelectionChanged(const QList<int32_t> &selected_shapes);
    void addLabel(TlShape &shape);
    void update_shape_color(TlShape &shape);
    std::vector<int32_t> get_rgb_by_label(const QString &label, LabelList *label_list);
    void remLabels(const QList<TlShape> &shapes);
    void load_shapes(QList<TlShape> &shapes, bool replace=true);
    void load_shape_dicts(const QList<ShapeDict> &shapes);
    void load_flags(const YAML::Node &flags, QListWidget *widget) const;
    bool saveLabels(const QString &filename);
    void duplicateSelectedShape();
    void pasteSelectedShape();
    void copySelectedShape();
    void label_selection_changed();
    void labelItemChanged(const ShapeListItem *item);
    void labelOrderChanged();
    void newShape();
    void scrollRequest(int32_t delta, Qt::Orientation orientation);
    void setScroll(Qt::Orientation orientation, float value);
    void set_zoom(int32_t value, QPointF pos=QPointF());
    void set_zoom_to_original();
    void add_zoom(float increment=1.1, QPointF pos=QPointF());
    void zoom_requested(int32_t delta, QPointF pos);
    void setFitWindow(bool value=true);
    void setFitWidth(bool value=true);
    void enableKeepPrevScale(bool enabled);
    void onNewBrightnessContrast(const QImage &image);
    void brightnessContrast(bool value=false, bool is_initial_load=false);
    void toggleShapes(int32_t value);
    QString get_label_path(QString image_or_label_path);
    void load_file(QString image_or_label_path);
    void paint_canvas();
    void adjust_scale();
    float scaleFitWindow() const;
    float scaleFitWidth() const;
    void enableSaveImageWithData(bool enabled);
    void reset_layout();
    void open_prev_image(bool value=false);
    void open_next_image(bool value=false);
    void open_file_with_dialog(bool value=false);
    void changeOutputDirDialog(bool value=false);
    void save_label_file(bool save_as=false);
    QString saveFileDialog();
    void closeFile(bool value=false);
    QString getLabelFile();
    void deleteFile();
    LabelDialog *make_label_dialog();
    void open_config_file();
    bool hasLabels();
    bool has_label_file();
    bool can_continue();
    void errorMessage(const QString &title, const QString &message);
    QString currentPath();
    void toggleKeepPrevMode();
    void removeSelectedPoint();
    void deleteSelectedShape();
    void copy_shape();
    void move_shape();
    void load_from_file_or_dir(const QString &file_or_dir);
    void open_dir_with_dialog(bool value=false);
    QStringList imageList();
    void importDroppedImageFiles(const QStringList &imageFiles);
    void import_images_from_dir(const QString &root_dir, const QString &pattern="");
    void update_status_stats(const QPointF &mouse_pos);
    //QList<TlShape> shapes_from_dicts(shape_dicts: list[ShapeDict], label_flags: dict[str, list[str]] | None,);
    QString resolve_text_annotation_shape_type(const QString &create_mode, const QString &ai_output_format);
    //def _rgb_from_colormap_id(*, label_id: int)
    //void rgb_from_label_colors(label: str, label_colors: dict[str, list[int]] | None);
    bool is_valid_label(const QString &label, const QStringList &existing_labels, const QString &policy);
    QString format_window_title(const QString &image_path, int32_t file_index, int32_t file_count, const QImage &image, bool dirty);
    //QString resolve_label_path(image_or_label_path: str, output_dir: Path | None);
    //QListWidgetItem *make_image_list_item(image_path: str, output_dir: Path | None);
    //ShapeDict shape_to_dict(shape: Shape);
    QStringList scan_image_files(const QString &root_dir) const;

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