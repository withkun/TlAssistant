#ifndef __INC_MAINWINDOW_H
#define __INC_MAINWINDOW_H

#include <QMainWindow>
#include <QWidgetAction>
#include <QGraphicsScene>
#include <QProgressDialog>
#include <QScrollArea>
#include <QSettings>
#include <QProcess>
#include <QLabel>

#include "tl_widgets/tl_canvas.h"
#include "tl_widgets/tl_tool_bar.h"
#include "tl_widgets/tl_shape_list.h"
#include "tl_widgets/tl_label_list.h"
#include "tl_widgets/tl_label_file.h"
#include "tl_widgets/tl_label_dialog.h"
#include "tl_widgets/zoom_widget.h"
#include "tl_widgets/tl_train_widget.h"

#include "tl_modules/ai_assist_annotation.h"
#include "tl_modules/ai_text_to_annotation.h"
#include "yaml-cpp/yaml.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT
    enum class ZoomMode : int32_t {
        FIT_WIDTH,
        FIT_WINDOW,
        MANUAL_ZOOM
    };
    using Menus = struct {
        QAction *file_;
        QAction *edit_;
    };

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
    Ui::MainWindow                                 *ui_{nullptr};
    QLabel                                         *message_{nullptr};
    QLabel                                         *stats_{nullptr};

    YAML::Node                                      config_;
    QSettings                                       settings_;
    QByteArray                                      default_state_;
    QString                                         config_file_;
    QString                                         output_dir_;
    QString                                         output_file_;
    QString                                         prev_opened_dir_;

    bool                                            is_changed_{false};
    QList<TlShape>                                  copied_shapes_;
    LabelDialog                                    *label_dialog_{nullptr};

    Canvas                                         *canvas_{nullptr};
    QScrollArea                                    *scroll_area_{nullptr};
    QMap<Qt::Orientation, QScrollBar *>             scroll_bars_;
    QMap<Qt::Orientation, QMap<QString, int32_t>>   scroll_values_;

    ZoomMode                                        zoom_mode_{ZoomMode::FIT_WINDOW};
    QMap<QString, std::pair<ZoomMode, float>>       zoom_values_;
    QMap<QString, std::pair<int32_t, int32_t>>      brightness_contrast_values_;
    QMap<ZoomMode, std::function<float()>>          scalers_;

    QDockWidget                                    *flags_dock_{nullptr};
    QListWidget                                    *flags_list_{nullptr};       // 标志列表

    QDockWidget                                    *label_dock_{nullptr};
    LabelList                                      *label_list_{nullptr};       // 标签列表

    QDockWidget                                    *shape_dock_{nullptr};
    ShapeListView                                  *shape_list_{nullptr};       // 轮廓列表

    QDockWidget                                    *files_dock_{nullptr};
    QListWidget                                    *docks_files_list_{nullptr}; // 文件列表
    QLineEdit                                      *files_search_{nullptr};

    QWidgetAction                                  *zoom_action_{nullptr};
    ZoomWidget                                     *zoom_widget_{nullptr};

    QImage                                          image_;
    QString                                         image_path_;
    QString                                         prev_image_path_;
    QByteArray                                      imageData_;
    QByteArray                                      other_data_;
    std::unique_ptr<LabelFile>                      label_file_;

    std::string                                     sam_model_name_{"efficientsam:latest"};
    std::unique_ptr<SamSession>                     text_osam_session_{nullptr};

    AiAssistAnnotation                             *ai_assist_annotation_widget_{nullptr};
    AiTextToAnnotation                             *ai_text_to_annotation_widget_{nullptr};
    QWidgetAction                                  *select_ai_model_{nullptr};
    QWidgetAction                                  *ai_prompt_action_{nullptr};
    QProgressDialog                                *progress_dialog_{nullptr};

    std::list<std::pair<QString, QAction *>>        actions_draw_;
    std::list<QAction *>                            actions_zoom_;
    std::list<QAction *>                            actions_on_load_active_;
    std::list<QAction *>                            actions_on_shapes_present_;
    std::list<QObject *>                            actions_context_menu_;
    std::list<QAction *>                            actions_edit_menu_;

    QMenu                                          *file_menu_{nullptr};
    QMenu                                          *edit_menu_{nullptr};
    QMenu                                          *view_menu_{nullptr};
    QMenu                                          *help_menu_{nullptr};
    QMenu                                          *label_menu_{nullptr};

    //QAction                                        *actSetup_{nullptr};
    //QAction                                        *actTrain_{nullptr};
    //QAction                                        *actInfer_{nullptr};
    TlTrainWidget                                  *train_widget_{nullptr};
    QProcess                                       *sub_process_;

    void retheme();
    void setup_actions();
    void setup_menus();
    void setup_toolbars();
    void setup_app_state(const QString &file_or_dir, const QString &output_dir);
    void setup_status_bar();
    void setup_canvas();
    void setup_dock_widgets();

    QString load_config(QString config_file, const YAML::Node &config_overrides);
    QMenu *menu(const QString &title, const std::list<QObject *> &actions={});
    bool noShapes();
    void populateModeActions();
    QString get_window_title(bool dirty);
    void setDirty();
    void setClean();
    void toggleActions(bool value=true);
    //void queueEvent(function);
    void show_status_message(const QString &message, int32_t delay=5000);
    void submit_ai_prompt();
    void resetState();
    QListWidgetItem *currentItem();
    void undoShapeEdit();
    void tutorial();
    void about();
    void toggleDrawingSensitive(bool drawing=true);
    void switch_canvas_mode(bool edit, const QString &createMode="");
    void highlight_ai_buttons(bool highlight);
    void popLabelListMenu(const QPoint &point);
    bool validateLabel(const QString &label);
    void edit_label(bool value=false);
    void fileSearchChanged();
    void file_list_item_selection_changed();
    void shapeSelectionChanged(const QList<int32_t> &selected_shapes);
    void addLabel(TlShape &shape);
    void update_shape_color(TlShape &shape);
    std::vector<int32_t> get_rgb_by_label(const QString &label, LabelList *label_list);
    void remLabels(const QList<TlShape> &shapes);
    void load_shapes(QList<TlShape> &shapes, bool replace=true);
    void load_shape_dicts(const QList<ShapeDict> &shapes);
    void load_flags(const YAML::Node &flags, QListWidget *const widget) const;
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
    void open_config_file();
    bool hasLabels();
    bool hasLabelFile();
    bool can_continue();
    void errorMessage(const QString &title, const QString &message);
    QString currentPath();
    void toggleKeepPrevMode();
    void removeSelectedPoint();
    void deleteSelectedShape();
    void copyShape();
    void moveShape();
    void load_from_file_or_dir(const QString &file_or_dir);
    void open_dir_with_dialog(bool value=false);
    QStringList imageList();
    void importDroppedImageFiles(const QStringList &imageFiles);
    void import_images_from_dir(const QString &root_dir, const QString &pattern="");
    void update_status_stats(const QPointF &mouse_pos);
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

private:
    QAction                *quit_{nullptr};
    QAction                *open_{nullptr};
    QAction                *open_config_{nullptr};
    QAction                *open_dir_{nullptr};
    QAction                *open_next_img_{nullptr};
    QAction                *open_prev_img_{nullptr};
    QAction                *save_{nullptr};
    QAction                *save_as_{nullptr};
    QAction                *delete_file_{nullptr};
    QAction                *change_output_dir_{nullptr};
    QAction                *save_auto_{nullptr};
    QAction                *save_with_image_data_{nullptr};
    QAction                *close_{nullptr};
    QAction                *toggle_keep_prev_mode_{nullptr};
    QAction                *toggle_keep_prev_brightness_contrast_{nullptr};
    QAction                *reset_layout_{nullptr};

    QAction                *create_mode_{nullptr};
    QAction                *create_rectangle_mode_{nullptr};
    QAction                *create_oriented_rectangle_mode_{nullptr};
    QAction                *create_circle_mode_{nullptr};
    QAction                *create_line_mode_{nullptr};
    QAction                *create_point_mode_{nullptr};
    QAction                *create_line_strip_mode_{nullptr};
    QAction                *create_ai_points_to_shape_mode_{nullptr};
    QAction                *create_ai_box_to_shape_mode_{nullptr};

    QAction                *edit_mode_{nullptr};
    QAction                *delete_{nullptr};
    QAction                *duplicate_{nullptr};
    QAction                *copy_{nullptr};
    QAction                *paste_{nullptr};
    QAction                *undo_last_point_{nullptr};
    QAction                *remove_point_{nullptr};
    QAction                *add_point_to_edge_{nullptr};
    QAction                *undo_{nullptr};

    QAction                *hide_all_{nullptr};
    QAction                *show_all_{nullptr};
    QAction                *toggle_all_{nullptr};
    QAction                *help_{nullptr};
    QAction                *about_{nullptr};

    QAction                *zoom_in_{nullptr};
    QAction                *zoom_out_{nullptr};
    QAction                *zoom_org_{nullptr};
    QAction                *keep_prev_scale_{nullptr};
    QAction                *fit_window_{nullptr};
    QAction                *fit_width_{nullptr};
    QAction                *brightness_contrast_{nullptr};

    QAction                *edit_{nullptr};
    QAction                *fill_drawing_{nullptr};
};
#endif //__INC_MAINWINDOW_H