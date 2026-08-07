#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <memory>
#include <random>
#include <ranges>

#include <QApplication>
#include <QDesktopServices>
#include <QFormLayout>
#include <QDockWidget>
#include <QFileDialog>
#include <QListWidget>
#include <QPushButton>
#include <QMenuBar>
#include <QScrollBar>
#include <QStatusBar>
#include <QToolButton>
#include <QMouseEvent>
#include <QMessageBox>
#include <QImageReader>
#include <QDirIterator>
#include <QPixmapCache>
#include <QStyleHints>
#include <QMimeData>

#include "common/qt_utils.h"
#include "common/natsort.h"
#include "config/app_config.h"
#include "config/tl_yaml_config.h"
#include "tl_widgets/tl_tool_bar.h"
#include "tl_widgets/tl_file_dialog.h"
#include "tl_widgets/tl_brightness.h"
#include "tl_widgets/status_stats.h"
#include "tl_modules/sam_apis.h"
#include "tl_modules/bbox_from_text.h"
#include "tl_modules/polygon_from_mask.h"


const static std::vector<QColor> LABEL_COLORMAP = label_colormap();

const QList<QString> TextToAnnotationCreateMode { "polygon", "rectangle" };

const QList<QString> AI_CREATE_MODES {
    "ai_points_to_shape",
    "ai_box_to_shape",
};

const QList<QString> AI_MODELS_WITHOUT_POINT_SUPPORT { "sam3:latest", };


MainWindow::MainWindow(const QString &config_file,
                       const YAML::Node &config_overrides,
                       const QString &file_or_dir,
                       const QString &output_dir)
    : QMainWindow(), ui_(new Ui::MainWindow), self(*this), window_state_("tl_assistant", "tl_assistant") {
    ui_->setupUi(this);
    this->setWindowTitle(_appname_);

    const AppConfig &appConfig = AppConfig::instance();
    this->config_file_ = this->load_config(config_file, config_overrides);

    // set default shape colors
    TlShape::line_color = YAML_COLOR(config_["shape"]["line_color"]);
    TlShape::fill_color = YAML_COLOR(config_["shape"]["fill_color"]);
    TlShape::select_line_color  = YAML_COLOR(
        config_["shape"]["select_line_color"]
    );
    TlShape::select_fill_color  = YAML_COLOR(
        config_["shape"]["select_fill_color"]
    );
    TlShape::vertex_fill_color  = YAML_COLOR(
        config_["shape"]["vertex_fill_color"]
    );
    TlShape::hvertex_fill_color = YAML_COLOR(
        config_["shape"]["hvertex_fill_color"]
    );

    // Set point size from config file
    TlShape::point_size_ = config_["shape"]["point_size"].as<int32_t>();

    this->copied_shapes_ = {};

    this->shape_clipboard_ = new ShapeClipboard();

    this->label_dialog_ = this->make_label_dialog();

    this->prev_opened_dir_ = QString::fromStdString(appConfig.last_work_dir_);
    this->label_list_menu_origin_ = QPoint();
    this->docks_ = this->setup_dock_widgets();

    this->setAcceptDrops(true);
    this->canvas_widgets_ = this->setup_canvas();

    this->actions_ = this->setup_actions();
    QObject::connect(this->shape_clipboard_, &ShapeClipboard::availability_changed,
        [this](bool available){ this->actions_.paste_->setEnabled(available); }
    );
    this->menus_ = this->setup_menus();

    this->ai_assist_annotation_ = new AiAssistAnnotation(
        QString::fromStdString(appConfig.ai_assist_name_),
        [this](const std::string &name){ this->canvas_widgets_.canvas_->set_ai_model_name(name); },
        [this](const std::string &name){ this->canvas_widgets_.canvas_->set_ai_output_format(name); },
        this
    );
    this->ai_assist_annotation_->setEnabled(false);
    this->ai_buttons_highlighted_ = false;

    this->ai_text_to_annotation_ = new AiTextToAnnotation(
        appConfig.ai_prompt_name_, [this] { this->submit_ai_prompt(); }, this
    );
    this->ai_text_to_annotation_->setEnabled(false);

    this->setup_toolbars();

    this->status_bar_ = this->setup_status_bar();

    this->setup_app_state(file_or_dir, output_dir);

    QObject::connect(this->canvas_widgets_.zoom_widget_, &ZoomWidget::valueChanged, this, &MainWindow::paint_canvas);

    this->populate_mode_actions();

    // colorSchemeChanged fires while setColorScheme is still running, before
    // the new palette is applied, so connect queued: _retheme runs on the next
    // event loop pass, against the live palette.
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
        this, &MainWindow::retheme, Qt::ConnectionType::QueuedConnection
    );
}

void MainWindow::retheme() {
    // Two things do not follow Qt's palette swap: cached QIcon pixmaps (keyed
    // by the old tint color) and stylesheet'd widgets (QStyleSheetStyle pins
    // their palette at polish time, leaving a palette(...) toolbar on the old
    // scheme).
    QPixmapCache::clear();
    //app = QtWidgets.QApplication.instance()
    //if not isinstance(app, QtWidgets.QApplication):
    //    return
    for (const auto widget : QApplication::allWidgets()) {
        const auto sheet = widget->styleSheet();
        // Only stylesheets with palette(...) references go stale on a scheme
        // change; re-applying just those avoids re-polishing composite widgets
        // (combo boxes, spin boxes) whose re-polish can invalidate siblings.
        if (sheet.contains("palette("))
            widget->setStyleSheet(sheet);  // re-resolve palette refs; also repaints
        else
            widget->update();
    }
    // The AI-button highlight bakes palette colors into its stylesheet (no
    // palette() ref), so recompute it against the new palette.
    this->highlight_ai_buttons(this->ai_buttons_highlighted_);
}

Actions MainWindow::setup_actions() {
    const auto action = [this](const QString &text, auto slot, const QList<QString> &shortcut={}, const QString &file="", const QString &tip="", const bool enabled=true, const bool checkable=false, const bool checked=false) {
        auto *a = utils::newAction(text, shortcut, file, tip, enabled, checkable, checked);
        QObject::connect(a, &QAction::triggered, this, slot);
        return a;
    };
    const auto shortcuts = [this](const std::string &key) { return YAML_KEYS(this->config_["shortcuts"][key]); };

    auto *about = action(
        "&About " + _appname_,
        [this]() {
            QMessageBox::about(
                this,
                "About " + _appname_,
"<h3>" + _appname_ + "</h3>"
"<p>Image Polygonal Annotation with C++</p>"
"<p>Version: " + _version_ + "</p>"
"<p>Author: Kentaro Wada</p>"
"<p>"
"    <a href=\"https://labelme.io\">Homepage</a> | "
"    <a href=\"https://labelme.io/docs\">Documentation</a> | "
"    <a href=\"https://labelme.io/docs/troubleshoot\">Troubleshooting</a>"
"</p>"
"<p>"
"    <a href=\"https://github.com/wkentaro/labelme\">GitHub</a> | "
"    <a href=\"https://x.com/labelmeai\">Twitter/X</a>"
"</p>"
            );
        }
    );
    auto *save = action(
        tr("&Save\n"),
        &MainWindow::save_label_file,
        shortcuts("save"), ":/icons/floppy-disk.svg",
        tr("Save labels to file"),
        false
    );
    auto *save_as = action(
        tr("&Save As"),
        [this](){ save_label_file(true); },
        shortcuts("save_as"), ":/icons/floppy-disk.svg",
        tr("Save labels to a different file"),
        false
    );
    auto *save_auto = action(
        tr("Save &Automatically"),
        [this](auto x) { actions_.save_auto_->setChecked(x); },
        {}, ":/icons/save.svg",
        tr("Save automatically"),
        true, true
    );
    save_auto->setChecked(config_["auto_save"].as<bool>());
    auto *save_with_image_data = action(
        tr("Save With Image Data"),
        &MainWindow::set_save_image_with_data,
        {}, ":/icons/icon-256.png",
        tr("Save image data in label file"),
        true, true, this->config_["with_image_data"].as<bool>()
    );
    auto *change_output_dir = action(
        tr("&Change Output Dir"),
        &MainWindow::prompt_output_dir,
        shortcuts("save_to"), ":/icons/folders.svg",
        tr("Change where annotations are loaded/saved")
    );
    auto *open = action(
        tr("&Open\n"),
        &MainWindow::open_file_with_dialog,
        shortcuts("open"), ":/icons/folder-open.svg",
        tr("Open image or label file")
    );
    auto *open_dir = action(
        tr("Open Dir"),
        [this] { open_dir_with_dialog(); },
        shortcuts("open_dir"), ":/icons/folder-open.svg",
        tr("Open Dir")
    );
    auto *close = action(
        tr("&Close"),
        &MainWindow::close_file,
        shortcuts("close"), ":/icons/x-circle.svg",
        tr("Close current file")
    );
    auto *delete_file = action(
        tr("&Delete File"),
        &MainWindow::delete_file,
        shortcuts("delete_file"), ":/icons/file-x.svg",
        tr("Delete current label file"),
        false
    );
    auto *keep_prev_action = action(
        tr("Keep Previous Annotation"),
        [this]() {
            this->config_["keep_prev"] = !this->config_["keep_prev"].as<bool>();
        },
        shortcuts("toggle_keep_prev_mode"), ":/icons/icon-256.png",
        tr("Toggle \"keep previous annotation\" mode"),
        true, true, this->config_["keep_prev"].as<bool>()
    );
    auto *toggle_keep_prev_brightness_contrast = action(
        tr("Keep Previous Brightness/Contrast"),
        [this] {
            this->config_["keep_prev_brightness_contrast"] = !this->config_["keep_prev_brightness_contrast"].as<bool>();
        },
        {}, ":/icons/question.svg",
        "",
        true, true, this->config_["keep_prev_brightness_contrast"].as<bool>()
    );
    auto *delete_ = action(
        tr("Delete Shapes"),
        &MainWindow::delete_selected_shapes,
        shortcuts("delete_shape"), ":/icons/trash.svg",
        tr("Delete the selected shapes"),
        false
    );
    auto *edit = action(
        tr("&Edit Label"),
        &MainWindow::edit_label,
        shortcuts("edit_label"), ":/icons/note-pencil.svg",
        tr("Modify the label of the selected shape"),
        false
    );
    auto *duplicate = action(
        tr("Duplicate Shapes"),
        &MainWindow::duplicateSelectedShape,
        shortcuts("duplicate_shape"), ":/icons/copy.svg",
        tr("Create a duplicate of the selected shapes"),
        false
    );
    auto *copy = action(
        tr("Copy Shapes"),
        &MainWindow::copySelectedShape,
        shortcuts("copy_shape"), ":/icons/copy_clipboard.svg",
        tr("Copy selected shapes to clipboard"),
        false
    );
    auto *paste = action(
        tr("Paste Shapes"),
        [this] { this->insert_shapes(this->copied_shapes_); },
        shortcuts("paste_shape"), ":/icons/paste.svg",
        tr("Paste copied shapes"),
        false
    );
    auto *undo_last_point = action(
        tr("Undo last point"),
        [this] { canvas_widgets_.canvas_->undo_last_point(); },
        shortcuts("undo_last_point"), ":/icons/arrow-u-up-left.svg",
        tr("Undo last drawn point"),
        false
    );
    auto *undo = action(
        tr("Undo\n"),
        &MainWindow::undo_shape_edit,
        shortcuts("undo"), ":/icons/arrow-u-up-left.svg",
        tr("Undo last add and edit of shape"),
        false
    );
    auto *remove_point = action(
        tr("Remove Selected Point"),
        &MainWindow::remove_selected_point,
        shortcuts("remove_selected_point"), ":/icons/trash.svg",
        tr("Remove selected point from polygon"),
        false
    );
    auto *add_point_to_edge = action(
        tr("Add Point to Edge"),
        [this] { canvas_widgets_.canvas_->add_point_to_edge(); },
        {}, "",
        tr("Insert a new point at the hovered polygon edge"),
        false
    );
    auto *create_mode = action(
        tr("Polygon"),
        [this] { this->switch_canvas_mode(false, "polygon"); },
        shortcuts("create_polygon"), ":/icons/polygon.svg",
        tr("Start drawing polygons"),
        false
    );
    auto *edit_mode = action(
        tr("Edit Shapes"),
        [this] { this->switch_canvas_mode(true); },
        shortcuts("edit_shape"), ":/icons/note-pencil.svg",
        tr("Move and edit the selected shapes"),
        false
    );
    auto *create_rectangle_mode = action(
        tr("Rectangle"),
        [this] { this->switch_canvas_mode(false, "rectangle"); },
        shortcuts("create_rectangle"), ":/icons/rectangle.svg",
        tr("Start drawing rectangles"),
        false
    );
    auto *create_oriented_rectangle_mode = action(
        tr("Oriented Rectangle"),
        [this] { this->switch_canvas_mode(false, "oriented_rectangle"); },
        shortcuts("create_oriented_rectangle"),
        ":/icons/oriented_rectangle.svg",
        tr("Start drawing oriented rectangles"),
        false
    );
    auto *create_circle_mode = action(
        tr("Circle"),
        [this] { this->switch_canvas_mode(false, "circle"); },
        shortcuts("create_circle"), ":/icons/circle.svg",
        tr("Start drawing circles"),
        false
    );
    auto *create_line_mode = action(
        tr("Line"),
        [this] { this->switch_canvas_mode(false, "line"); },
        shortcuts("create_line"), ":/icons/line-segment.svg",
        tr("Start drawing lines"),
        false
    );
    auto *create_point_mode = action(
        tr("Point"),
        [this] { this->switch_canvas_mode(false, "point"); },
        shortcuts("create_point"), ":/icons/circles-four.svg",
        tr("Start drawing points"),
        false
    );
    auto *create_line_strip_mode = action(
        tr("LineStrip"),
        [this] { this->switch_canvas_mode(false, "linestrip"); },
        shortcuts("create_linestrip"), ":/icons/line-segments.svg",
        tr("Start drawing linestrip. Ctrl+LeftClick ends creation."),
        false
    );
    auto *create_ai_points_to_shape_mode = action(
        tr("AI-Points"),
        [this] { this->switch_canvas_mode(false, "ai_points_to_shape"); },
        shortcuts("create_ai_polygon"), ":/icons/ai-polygon.svg",
        tr("Click points to segment object. Ctrl+LeftClick ends creation."),
        false
    );
    auto *create_ai_box_to_shape_mode = action(
        tr("AI-Box"),
        [this] { this->switch_canvas_mode(false, "ai_box_to_shape"); },
        shortcuts("create_ai_mask"), ":/icons/ai-mask.svg",
        tr("Draw a bounding box to segment object."),
        false
    );
    auto *open_next_img = action(
        tr("&Next Image"),
        &MainWindow::open_next_image,
        shortcuts("open_next"), ":/icons/arrow-fat-right.svg",
        tr("Open next (hold Ctrl+Shift to copy labels)"),
        false
    );
    auto *open_prev_img = action(
        tr("&Prev Image"),
        &MainWindow::open_prev_image,
        shortcuts("open_prev"), ":/icons/arrow-fat-left.svg",
        tr("Open prev (hold Ctrl+Shift to copy labels)"),
        false
    );
    auto *keep_prev_zoom = action(
        tr("&Keep Previous Zoom"),
        [this](bool checked) {
            this->config_["keep_prev_scale"] = checked;
        },
        {}, {}, {},
        true, true, this->config_["keep_prev_scale"].as<bool>()
    );
    auto *fit_window = action(
        tr("&Fit Window"),
        &MainWindow::set_fit_window_mode,
        shortcuts("fit_window"), ":/icons/frame-corners.svg",
        tr("Zoom follows window size"),
        false, true
    );
    auto *fit_width = action(
        tr("Fit &Width"),
        &MainWindow::set_fit_width_mode,
        shortcuts("fit_width"), ":/icons/frame-arrows-horizontal.svg",
        tr("Zoom follows window width"),
        false, true
    );
    auto *brightness_contrast = action(
        tr("&Brightness Contrast"),
        [this] { this->open_brightness_contrast_dialog(); },
        {}, ":/icons/brightness-contrast.svg",
        tr("Adjust brightness and contrast"),
        false
    );
    auto *zoom_in = action(
        tr("Zoom &In"),
        [this] { add_zoom(1.1); },
        shortcuts("zoom_in"), ":/icons/magnifying-glass-minus.svg",
        tr("Increase zoom level"),
        false
    );
    auto *zoom_out = action(
        tr("&Zoom Out"),
        [this] { add_zoom(0.9); },
        shortcuts("zoom_out"), ":/icons/magnifying-glass-plus.svg",
        tr("Decrease zoom level"),
        false
    );
    auto *zoom_org = action(
        tr("&Original size"),
        &MainWindow::set_zoom_to_original,
        shortcuts("zoom_to_original"), ":/icons/image-square.svg",
        tr("Zoom to original size"),
        false
    );
    auto *reset_layout = action(
        tr("Reset Layout"),
        &MainWindow::reset_layout,
        {}, ":/icons/layout-duotone.svg"
    );
    auto *fill_drawing = action(
        tr("Fill Drawing Polygon"),
        [this] { canvas_widgets_.canvas_->set_fill_drawing(actions_.fill_drawing_->isChecked()); },
        {}, ":/icons/paint-bucket.svg",
        tr("Fill polygon while drawing"),
        true, true
    );
    if (config_["canvas"]["fill_drawing"].as<bool>()) {
        canvas_widgets_.canvas_->set_fill_drawing(true);
    }
    auto *hide_all = action(
        tr("&Hide\nShapes"),
        [this] { this->toggle_shape_visibility(false); },
        shortcuts("hide_all_shapes"), ":/icons/eye.svg",
        tr("Hide all shapes"),
        false
    );
    auto *show_all = action(
        tr("&Show\nShapes"),
        [this] { this->toggle_shape_visibility(true); },
        shortcuts("show_all_shapes"), ":/icons/eye.svg",
        tr("Show all shapes"),
        false
    );
    auto *toggle_all = action(
        tr("&Toggle\nShapes"),
        [this] { this->toggle_shape_visibility(None); },
        shortcuts("toggle_all_shapes"), ":/icons/eye.svg",
        tr("Toggle all shapes"),
        false
    );

    auto *zoom_widget_action = new QWidgetAction(this);
    auto *zoom_box_layout = new QVBoxLayout();
    auto *zoom_label = new QLabel(tr("Zoom"));
    zoom_label->setAlignment(Qt::AlignCenter);
    zoom_box_layout->addWidget(zoom_label);
    zoom_box_layout->addWidget(this->canvas_widgets_.zoom_widget_);
    zoom_widget_action->setDefaultWidget(new QWidget());
    zoom_widget_action->defaultWidget()->setLayout(zoom_box_layout);
    this->canvas_widgets_.zoom_widget_->setWhatsThis(
        QString(
            tr(
                "Zoom in or out of the image. Also accessible with "
                "%1 %2 and %3 from the canvas."
            )
        ).arg(
            utils::fmtShortcut(shortcuts("zoom_in")), utils::fmtShortcut(shortcuts("zoom_out")),
            tr("Ctrl+Wheel")
        )
    );
    this->canvas_widgets_.zoom_widget_->setEnabled(false);

    this->zoom_mode_ = ZoomMode::FIT_WINDOW;
    fit_window->setChecked(true);

    QObject::connect(this->canvas_widgets_.canvas_, &Canvas::vertex_selected, [this](bool value){ actions_.remove_point_->setEnabled(value); });
    QObject::connect(this->canvas_widgets_.canvas_, &Canvas::aiAssistSubmit, this, &MainWindow::slotTaskSubmit);
    QObject::connect(this->canvas_widgets_.canvas_, &Canvas::aiAssistFinish, this, &MainWindow::slotTaskFinish);

    QList<QPair<QString, QAction *>> draw = {
        {"polygon",            create_mode},
        {"rectangle",          create_rectangle_mode},
        {"oriented_rectangle", create_oriented_rectangle_mode},
        {"circle",             create_circle_mode},
        {"point",              create_point_mode},
        {"line",               create_line_mode},
        {"linestrip",          create_line_strip_mode},
        {"ai_points_to_shape", create_ai_points_to_shape_mode},
        {"ai_box_to_shape",    create_ai_box_to_shape_mode},
    };
    std::list<QAction *> zoom = {
        //self._canvas_widgets.zoom_widget,
        zoom_in,
        zoom_out,
        zoom_org,
        fit_window,
        fit_width
    };
    std::list<QAction *> on_load_active = {
        close,
        create_mode,
        create_rectangle_mode,
        create_oriented_rectangle_mode,
        create_circle_mode,
        create_line_mode,
        create_point_mode,
        create_line_strip_mode,
        create_ai_points_to_shape_mode,
        create_ai_box_to_shape_mode,
        brightness_contrast,
    };
    std::list<QAction *> on_shapes_present = {save_as, hide_all, show_all, toggle_all};
    std::list<QObject *> context_menu = {
        edit_mode,
        edit,
        duplicate,
        copy,
        paste,
        delete_,
        undo,
        undo_last_point,
        add_point_to_edge,
        remove_point,
    };
    std::for_each(draw.rbegin(), draw.rend(), [&](auto &p) { context_menu.push_front(p.second); });
    std::list<QAction *> edit_menu = {
        edit,
        duplicate,
        copy,
        paste,
        delete_,
        nullptr,
        undo,
        undo_last_point,
        nullptr,
        remove_point,
        nullptr,
        keep_prev_action,
    };
    return Actions{
        .about_ = about,
        .save_ = save,
        .save_as_ = save_as,
        .save_auto_ = save_auto,
        .save_with_image_data_ = save_with_image_data,
        .change_output_dir_ = change_output_dir,
        .open_ = open,
        .close_ = close,
        .delete_file_ = delete_file,
        .toggle_keep_prev_mode_ = keep_prev_action,
        .toggle_keep_prev_brightness_contrast_ = toggle_keep_prev_brightness_contrast,
        .delete_ = delete_,
        .edit_ = edit,
        .duplicate_ = duplicate,
        .copy_ = copy,
        .paste_ = paste,
        .undo_last_point_ = undo_last_point,
        .undo_ = undo,
        .add_point_to_edge_ = add_point_to_edge,
        .remove_point_ = remove_point,
        .create_mode_ = create_mode,
        .edit_mode_ = edit_mode,
        .create_rectangle_mode_ = create_rectangle_mode,
        .create_oriented_rectangle_mode_ = create_oriented_rectangle_mode,
        .create_circle_mode_ = create_circle_mode,
        .create_line_mode_ = create_line_mode,
        .create_point_mode_ = create_point_mode,
        .create_line_strip_mode_ = create_line_strip_mode,
        .create_ai_points_to_shape_mode_ = create_ai_points_to_shape_mode,
        .create_ai_box_to_shape_mode_ = create_ai_box_to_shape_mode,
        .open_next_img_ = open_next_img,
        .open_prev_img_ = open_prev_img,
        .keep_prev_zoom_ = keep_prev_zoom,
        .fit_window_ = fit_window,
        .fit_width_ = fit_width,
        .brightness_contrast_ = brightness_contrast,
        .zoom_in_ = zoom_in,
        .zoom_out_ = zoom_out,
        .zoom_org_ = zoom_org,
        .reset_layout_ = reset_layout,
        .fill_drawing_ = fill_drawing,
        .hide_all_ = hide_all,
        .show_all_ = show_all,
        .toggle_all_ = toggle_all,
        .open_dir_ = open_dir,
        .zoom_widget_action_ = zoom_widget_action,
        .draw_ = draw,
        .zoom_ = zoom,
        .on_load_active_ = on_load_active,
        .on_shapes_present_ = on_shapes_present,
        .context_menu_ = context_menu,
        .edit_menu_ = edit_menu,
    };
}

Menus MainWindow::setup_menus() {
    const auto action = [this](const QString &text, auto slot, const QList<QString> &shortcut={}, const QString &file="", const QString &tip="", const bool enabled=true, const bool checkable=false, const bool checked=false) {
        auto *a = utils::newAction(text, shortcut, file, tip, enabled, checkable, checked);
        QObject::connect(a, &QAction::triggered, this, slot);
        return a;
    };
    const auto shortcuts = [this](const std::string &key) { return YAML_KEYS(config_["shortcuts"][key]); };

    auto *quit = action(
        tr("&Quit"),
        &MainWindow::close,
        shortcuts("quit"), ":/icons/quit.png",
        tr("Quit application")
    );
    auto settings_editable = this->is_settings_editable();
    auto *open_config = action(
        tr("Settings…"),
        &MainWindow::open_settings,
        {"Ctrl+Shift+,"}, "",
        settings_editable ?
            tr("Edit settings") :
            tr("Settings are managed via --config for this session"),
        settings_editable
    );
    open_config->setMenuRole(QAction::PreferencesRole);
    auto *help = action(
        tr("&Tutorial"),
        &MainWindow::tutorial,
        {}, ":/icons/question.svg",
        tr("Show tutorial page")
    );

    auto *file_menu = this->menu(tr("&File"));
    auto *edit_menu = this->menu(tr("&Edit"));
    auto *view_menu = this->menu(tr("&View"));
    auto *help_menu = this->menu(tr("&Help"));
    auto *label_menu = new QMenu();
    utils::add_actions(label_menu, { actions_.edit_, actions_.delete_ });
    this->docks_.shape_list_->setContextMenuPolicy(
        Qt::ContextMenuPolicy::CustomContextMenu
    );
    QObject::connect(this->docks_.shape_list_, &ShapeListView::customContextMenuRequested, this,
        &MainWindow::show_label_list_menu
    );

    utils::add_actions(
        file_menu,
        {
            this->actions_.open_,
            this->actions_.open_next_img_,
            this->actions_.open_prev_img_,
            this->actions_.open_dir_,
            this->actions_.save_,
            this->actions_.save_as_,
            this->actions_.save_auto_,
            this->actions_.change_output_dir_,
            this->actions_.save_with_image_data_,
            this->actions_.close_,
            this->actions_.delete_file_,
            nullptr,
            open_config,
            nullptr,
            quit
        }
    );
    utils::add_actions(help_menu, {help, this->actions_.about_});
    utils::add_actions(
        view_menu,
        {
            this->docks_.flag_dock_->toggleViewAction(),
            this->docks_.label_dock_->toggleViewAction(),
            this->docks_.shape_dock_->toggleViewAction(),
            this->docks_.file_dock_->toggleViewAction(),
            nullptr,
            this->actions_.reset_layout_,
            nullptr,
            this->actions_.fill_drawing_,
            nullptr,
            this->actions_.hide_all_,
            this->actions_.show_all_,
            this->actions_.toggle_all_,
            nullptr,
            this->actions_.zoom_in_,
            this->actions_.zoom_out_,
            this->actions_.zoom_org_,
            this->actions_.keep_prev_zoom_,
            nullptr,
            this->actions_.fit_window_,
            this->actions_.fit_width_,
            nullptr,
            this->actions_.brightness_contrast_,
            this->actions_.toggle_keep_prev_brightness_contrast_,
        }
    );

    utils::add_actions(
        this->canvas_widgets_.canvas_->context_menus_.without_selection_,
        this->actions_.context_menu_
    );
    utils::add_actions(
        this->canvas_widgets_.canvas_->context_menus_.with_selection_,
        {
            action("&Copy here", [this] { this->copy_shape(); }),
            action("&Move here", [this] { this->move_shape(); }),
        }
    );

    return Menus{
        .file_ = file_menu,
        .edit_ = edit_menu,
        .view_ = view_menu,
        .help_ = help_menu,
        .label_list_ = label_menu
    };
}

void MainWindow::setup_toolbars() {
    auto *select_ai_model = new QWidgetAction(this);
    select_ai_model->setDefaultWidget(this->ai_assist_annotation_);

    auto *ai_prompt_action = new QWidgetAction(this);
    ai_prompt_action->setDefaultWidget(this->ai_text_to_annotation_);

    this->addToolBar(
        Qt::TopToolBarArea,
        new TlToolBar(
            "Tools",
            {
                this->actions_.open_,
                this->actions_.open_dir_,
                this->actions_.open_prev_img_,
                this->actions_.open_next_img_,
                this->actions_.save_,
                this->actions_.delete_file_,
                nullptr,
                this->actions_.edit_mode_,
                this->actions_.duplicate_,
                this->actions_.delete_,
                this->actions_.undo_,
                this->actions_.brightness_contrast_,
                nullptr,
                this->actions_.fit_window_,
                this->actions_.zoom_widget_action_,
                nullptr,
                select_ai_model,
                nullptr,
                ai_prompt_action
            },
            Qt::Orientation::Horizontal,
            Qt::ToolButtonStyle::ToolButtonTextUnderIcon,
            this->font()
        )
    );

    std::list<QAction *> actions;
    std::ranges::for_each(actions_.draw_, [&actions](auto &item) { if (!item.first.startsWith("ai_")) { actions.push_back(item.second); } });
    actions.push_back(nullptr);
    std::ranges::for_each(actions_.draw_, [&actions](auto &item) { if (item.first.startsWith("ai_")) { actions.push_back(item.second); } });
    this->addToolBar(
        Qt::ToolBarArea::LeftToolBarArea,
        new TlToolBar(
            "CreateShapeTools",
            actions,
            Qt::Orientation::Vertical,
            Qt::ToolButtonStyle::ToolButtonTextUnderIcon,
            this->font()
        )
    );
    QObject::connect(this->ai_assist_annotation_, &AiAssistAnnotation::hover_highlight_requested, this,
        &MainWindow::highlight_ai_buttons
    );
}

void MainWindow::setup_app_state(
    const QString &file_or_dir,
    const QString &output_dir
) {
    this->output_dir_ = output_dir;

    this->image_ = {};
    this->annotation_ = {};
    this->label_file_path_ = {};
    this->image_path_ = {};
    this->prev_image_path_ = {};
    this->zoom_values_ = {};
    this->brightness_contrast_values_ = {};
    this->scroll_values_ = {
        {Qt::Orientation::Horizontal, {}},
        {Qt::Orientation::Vertical, {}}
    };

    if (!this->config_["file_search"].IsNull()) {
        this->docks_.file_search_->setText(YAML_QSTR(this->config_["file_search"]));
    }

    this->default_state_ = this->saveState();
    //
    // XXX: Could be completely declarative.
    // Restore the window geometry and dock layout (separate from the user
    // Config; this Qt store holds only window state).
    //this->window_state_ = QSettings("tl_assistant", "tl_assistant");
    //
    // Bump this when dock/toolbar layout changes to reset window state
    // for users upgrading from an older version.
    int32_t SETTINGS_VERSION = 1;
    if (this->window_state_.value("settingsVersion", 0).toInt() != SETTINGS_VERSION) {
        this->reset_layout();
        this->window_state_.setValue("settingsVersion", SETTINGS_VERSION);
    }
    this->resize(
        this->window_state_.value("window/size", QSize(900, 500)).toSize()
    );
    this->move(
        this->window_state_.value("window/position", QPoint(0, 0)).toPoint()
    );
    this->restoreState(
        this->window_state_.value("window/state", QByteArray()).toByteArray()
    );
    // Recover window position when the saved screen is no longer connected.
    if (std::ranges::none_of(QApplication::screens(), [this](auto &s) {
            return s->availableGeometry().intersects(this->frameGeometry());
        }) &&
        QApplication::primaryScreen() != nullptr) {
        this->move(QApplication::primaryScreen()->availableGeometry().topLeft());
    }

    if (!file_or_dir.isEmpty()) {
        this->load_from_file_or_dir(file_or_dir);
    }
}

StatusBarWidgets MainWindow::setup_status_bar() {
    auto *message = new QLabel(tr("%1 started.").arg(_appname_));
    auto *stats = new StatusStats();
    this->statusBar()->addWidget(message, 1);
    this->statusBar()->addWidget(stats, 0);
    this->statusBar()->show();
    return StatusBarWidgets{.message_ = message, .stats_ = stats};
}

CanvasWidgets MainWindow::setup_canvas() {
    auto *zoom_widget = new ZoomWidget();

    auto *canvas = new Canvas(
        this->config_["epsilon"].as<float>(),
        YAML_QSTR(this->config_["canvas"]["double_click"]),
        this->config_["canvas"]["num_backups"].as<int32_t>(),
        YAML_QMAP(this->config_["canvas"]["crosshair"]),
        this->config_["canvas"][
            "allow_out_of_bounds_points"
        ].as<bool>()
    );
    canvas->set_point_size(this->config_["shape"]["point_size"].as<int32_t>());
    canvas->set_show_labels(this->config_["shape"]["show_labels"].as<bool>());
    canvas->set_draft_palette(
        Palette(
            YAML_COLOR(this->config_["shape"]["line_color"]),
            YAML_COLOR(this->config_["shape"]["fill_color"]),
            YAML_COLOR(this->config_["shape"]["select_line_color"]),
            YAML_COLOR(this->config_["shape"]["select_fill_color"]),
            YAML_COLOR(this->config_["shape"]["vertex_fill_color"]),
            YAML_COLOR(this->config_["shape"]["hvertex_fill_color"])
        )
    );
    canvas->set_color_resolver(
        [this](const auto &label) {
            return this->get_rgb_by_label(label, this->docks_.label_list_);
        }
    );
    QObject::connect(canvas, &Canvas::zoom_request, this, &MainWindow::zoom_requested);
    QObject::connect(canvas, &Canvas::mouse_moved, this, &MainWindow::update_status_stats);
    QObject::connect(canvas, &Canvas::status_updated, [this](const auto &text) {
        this->status_bar_.message_->setText(text); }
    );

    auto *scroll_area = new QScrollArea();
    scroll_area->setWidget(canvas);
    scroll_area->setWidgetResizable(true);
    QMap<Qt::Orientation, QScrollBar *> scroll_bars {
        { Qt::Vertical, scroll_area->verticalScrollBar() },
        { Qt::Horizontal, scroll_area->horizontalScrollBar() }
    };
    QObject::connect(canvas, &Canvas::scroll_request, this, &MainWindow::on_scroll_request);
    QObject::connect(canvas, &Canvas::pan_request, this, &MainWindow::on_pan_request);

    QObject::connect(canvas, &Canvas::new_shape, this, &MainWindow::on_new_shape);
    QObject::connect(canvas, &Canvas::inference_produced_no_shapes, this,
        &MainWindow::on_inference_produced_no_shapes
    );
    // The preview path emits this from inside paintEvent (an active
    // QPainter); a queued connection defers the status-bar update until
    // after the paint cycle so it never mutates UI mid-paint.
    QObject::connect(canvas, &Canvas::inference_failed, this,
        &MainWindow::on_inference_failed,
        Qt::QueuedConnection
    );
    QObject::connect(canvas, &Canvas::degenerate_shape_rejected, [this]() {
        this->show_status_message(
            tr("Shape had no area; nothing created."), 5000
        ); }
    );
    QObject::connect(canvas, &Canvas::shape_moved, this, &MainWindow::mark_dirty);
    QObject::connect(canvas, &Canvas::selection_changed, this, &MainWindow::on_shape_selection_changed);
    QObject::connect(canvas, &Canvas::drawing_polygon, this, &MainWindow::on_drawing_polygon_changed);

    this->setCentralWidget(scroll_area);

    return CanvasWidgets{
        .canvas_ = canvas,
        .zoom_widget_ = zoom_widget,
        .scroll_bars_ = scroll_bars
    };
}

DockWidgets MainWindow::setup_dock_widgets() {
    auto *flag_list = new QListWidget();
    auto *flag = new QDockWidget(tr("Flags"), this);
    flag->setObjectName("Flags");
    if (!this->config_["flags"].IsNull()) {
        this->load_flags(this->config_["flags"], flag_list);
    }
    flag->setWidget(flag_list);
    QObject::connect(flag_list, &QListWidget::itemChanged, this, &MainWindow::mark_dirty);

    auto *shape_list =  new ShapeListView();    // LabelListWidget()
    QObject::connect(shape_list, &ShapeListView::item_selection_changed, this, &MainWindow::label_selection_changed);
    QObject::connect(shape_list, &ShapeListView::item_double_clicked, this, &MainWindow::edit_label);
    QObject::connect(shape_list, &ShapeListView::item_changed, this, &MainWindow::on_label_item_changed);
    QObject::connect(shape_list, &ShapeListView::item_dropped, this, &MainWindow::on_label_order_changed);
    auto *shape = new QDockWidget(tr("Annotation List"), this);
    shape->setObjectName("Labels");
    shape->setWidget(shape_list);

    auto *label_list =  new LabelList();        // UniqueLabelQListWidget()
    label_list->setToolTip(
        tr("Select label to start annotating for it. Press 'Esc' to deselect.")
    );
    if (!config_["labels"].IsNull()) {
        for (auto &lbl : YAML_KEYS(config_["labels"]))
            label_list->add_label_item(
                lbl,
                get_rgb_by_label(lbl, label_list)
            );
    }
    auto *label = new QDockWidget(tr("Label List"), this);
    label->setObjectName("Label List");
    label->setWidget(label_list);

    auto *file_search = new QLineEdit();
    file_search->setPlaceholderText(tr("Search Filename"));
    QObject::connect(file_search, &QLineEdit::textChanged, this, &MainWindow::on_file_search_changed);
    auto *file_list = new QListWidget();
    QObject::connect(file_list, &QListWidget::itemSelectionChanged, this, &MainWindow::file_list_item_selection_changed);
    auto *file_list_layout = new QVBoxLayout();
    file_list_layout->setContentsMargins(0, 0, 0, 0);
    file_list_layout->setSpacing(0);
    file_list_layout->addWidget(file_search);
    file_list_layout->addWidget(file_list);
    auto *file = new QDockWidget(tr("File List"), this);
    file->setObjectName("Files");
    auto *file_list_container = new QWidget();
    file_list_container->setLayout(file_list_layout);
    file->setWidget(file_list_container);

    for (auto &[config_key, dock_widget] : std::map<std::string, QDockWidget *>{
        {"flag_dock", flag},
        {"label_dock", label},
        {"shape_dock", shape},
        {"file_dock", file}
    }) {
        auto features = QDockWidget::DockWidgetFeatures();
        if (config_[config_key]["closable"].as<bool>())
            features |= QDockWidget::DockWidgetFeature::DockWidgetClosable;
        if (config_[config_key]["floatable"].as<bool>())
            features |= QDockWidget::DockWidgetFeature::DockWidgetFloatable;
        if (config_[config_key]["movable"].as<bool>())
            features |= QDockWidget::DockWidgetFeature::DockWidgetMovable;
        dock_widget->setFeatures(features);
        if (config_[config_key]["show"].as<bool>() == false)
            dock_widget->setVisible(false);
        this->addDockWidget(Qt::DockWidgetArea::RightDockWidgetArea, dock_widget);
    }
    return DockWidgets{
        .flag_dock_ = flag,
        .flag_list_ = flag_list,
        .shape_dock_ = shape,
        .shape_list_ = shape_list,
        .label_dock_ = label,
        .label_list_ = label_list,
        .file_dock_ = file,
        .file_search_ = file_search,
        .file_list_ = file_list,
    };
}

QString MainWindow::load_config(
    QString config_file, const YAML::Node &config_overrides
) { // -> tuple[Path | None, dict]:
    try {
        config_ = TlConfig::load_config(
            config_file.toStdString(), config_overrides
        );
    } catch (const YAML::BadFile &e) {
        auto msg_box = QMessageBox(this);
        msg_box.setIcon(QMessageBox::Icon::Warning);
        msg_box.setWindowTitle(this->tr("Configuration Errors"));
        msg_box.setText(
            this->tr(
                "Errors were found while loading the configuration. "
                "Please review the errors below and reload your configuration or "
                "ignore the erroneous lines."
            )
        );
        msg_box.setInformativeText(e.what());
        msg_box.setStandardButtons(QMessageBox::StandardButton::Ignore);
        msg_box.setModal(false);
        msg_box.show();

        config_file.clear();
        //config_overrides = {}
        config_ = TlConfig::load_config(
            config_file.toStdString(), {}
        );
    }
    return config_file; //, config
}

QMenu *MainWindow::menu(
    const QString &title,
    const std::list<QObject *> &actions
) {
    auto *menu = this->menuBar()->addMenu(title);
    if (!actions.empty()) {
        utils::add_actions(menu, actions);
    }
    return menu;
}

// Support Functions

bool MainWindow::has_no_shapes() const {
    return this->docks_.shape_list_->empty();
}

void MainWindow::populate_mode_actions() {
    this->canvas_widgets_.canvas_->context_menus_.without_selection_->clear();
    utils::add_actions(
        this->canvas_widgets_.canvas_->context_menus_.without_selection_,
        this->actions_.context_menu_
    );
    this->menus_.edit_->clear();
    std::list<QObject *> actions;
    std::ranges::for_each(this->actions_.draw_, [&actions](auto &p){ actions.push_back(p.second); });
    actions.push_back(this->actions_.edit_mode_);
    std::ranges::for_each(this->actions_.edit_menu_, [&actions](auto &a) { actions.push_back(a); });

    utils::add_actions(this->menus_.edit_, actions);
}

QString MainWindow::get_window_title(const bool dirty) {
    const auto *file_list = this->docks_.file_list_;
    const auto file_index = file_list->currentItem() ? file_list->currentRow() : None;
    return format_window_title(
        this->image_path_,
        file_index,
        file_list->count(),
        this->image_,
        dirty
    );
}

void MainWindow::mark_dirty() {
    // Autosave does not clear the undo stack; keep the undo action available.
    this->actions_.undo_->setEnabled(this->canvas_widgets_.canvas_->can_restore_shape());

    if (this->actions_.save_auto_->isChecked()) {
        // assert self._image_path is not None
        this->save_labels(
            this->resolve_label_path(
                this->image_path_,
                this->output_dir_
            )
        );
        return;
    }
    this->is_changed_ = true;
    this->actions_.save_->setEnabled(true);
    this->setWindowTitle(this->get_window_title(true));
}

void MainWindow::mark_clean() {
    this->is_changed_ = false;
    this->actions_.save_->setEnabled(false);
    for (const auto &action : this->actions_.draw_ | std::views::values) {
        action->setEnabled(true);
    }
    this->setWindowTitle(get_window_title(false));

    if (this->has_label_file()) {
        this->actions_.delete_file_->setEnabled(true);
    } else {
        this->actions_.delete_file_->setEnabled(false);
    }
}

void MainWindow::update_action_states(bool value) {
    this->canvas_widgets_.zoom_widget_->setEnabled(value);
    for (const auto a : this->actions_.zoom_) {
        a->setEnabled(value);
    }
    for (const auto a : this->actions_.on_load_active_) {
        a->setEnabled(value);
    }
}

void MainWindow::show_status_message(const QString &message, int32_t delay) {
    this->statusBar()->showMessage(message, delay);
}

void MainWindow::submit_ai_prompt() {
    const auto create_mode = this->canvas_widgets_.canvas_->create_mode();
    const auto shape_type = resolve_text_annotation_shape_type(
        create_mode,
        this->ai_assist_annotation_->output_format()
    );
    if (shape_type.isEmpty()) {
        SPDLOG_WARN("Unsupported create_mode={}", create_mode);
        return;
    }

    const auto texts = this->ai_text_to_annotation_->get_texts_prompt();
    if (texts.empty()) {
        return;
    }

    const auto model_name = this->ai_text_to_annotation_->get_model_name();
    //model_type = osam.apis.get_model_type_by_name(model_name);
    //if not (_is_already_downloaded := model_type.get_size() is not None):
    //    if not download_ai_model(model_name=model_name, parent=self):
    //        return;
    if (this->text_osam_session_ == nullptr ||
        this->text_osam_session_->model_name() != model_name) {
        this->text_osam_session_ = std::make_unique<SamSession>(model_name);
    }

    //try:
    //    boxes, scores, labels, masks = _automation.get_bboxes_from_texts(
    //        session=self._text_osam_session,
    //        image=_utils.img_qt_to_arr(self._image)[:, :, :3],
    //        image_id=str(hash(self._image_path)),
    //        texts=texts,
    //    )
    //except Exception as e:
    //    logger.opt(exception=e).error("AI text inference failed")
    //    self._on_inference_failed(message=f"{type(e).__name__}: {e}")
    //    return
    //
    //if (
    //    masks is None
    //    and len(boxes) > 0
    //    and shape_type in _automation.MASK_REQUIRED_SHAPE_TYPES
    //):
    //    QtWidgets.QMessageBox.warning(
    //        self,
    //        self.tr("Mask Output Unavailable"),
    //        self.tr(
    //            "%s only detects bounding boxes and cannot create "
    //            "'%s' annotations.\n\n"
    //            "Switch the AI Text-to-Annotation model to 'SAM3 (smart)', "
    //            "or set the output format to 'Rectangle'."
    //        )
    //        % (self._ai_text.get_model_display_name(), shape_type),
    //    )
    //    return
    //
    //SCORE_FOR_EXISTING_SHAPE: Final[float] = 1.01
    //for shape in self._canvas_widgets.canvas.shapes:
    //    if shape.shape_type != shape_type or shape.label not in texts:
    //        continue
    //    shape_bbox = _automation.shape_to_xyxy_bbox(shape=shape)
    //    if shape_bbox is None:
    //        continue
    //    boxes = np.r_[boxes, [shape_bbox]]
    //    scores = np.r_[scores, [SCORE_FOR_EXISTING_SHAPE]]
    //    labels = np.r_[labels, [texts.index(shape.label)]]
    //
    //boxes, scores, labels, indices = _automation.nms_bboxes(
    //    boxes=boxes,
    //    scores=scores,
    //    labels=labels,
    //    iou_threshold=self._ai_text.get_iou_threshold(),
    //    score_threshold=self._ai_text.get_score_threshold(),
    //    max_num_detections=100,
    //)
    //
    //is_new = scores != SCORE_FOR_EXISTING_SHAPE
    //boxes = boxes[is_new]
    //scores = scores[is_new]
    //labels = labels[is_new]
    //indices = indices[is_new]
    //
    //if masks is None:
    //    masks = [None] * len(boxes)
    //else:
    //    masks = [masks[i] for i in indices]
    //del indices
    //
    //detections: list[_automation.Detection] = []
    //for i, (box, score, label, mask) in enumerate(zip(boxes, scores, labels, masks)):
    //    text: str = texts[label] + "_{:03d}".format(i)
    //    detections.append(
    //        _automation.Detection(
    //            bbox=(
    //                float(box[0]),
    //                float(box[1]),
    //                float(box[2]),
    //                float(box[3]),
    //            ),
    //            mask=mask,
    //            label=text,
    //            description=json.dumps(dict(score=score.item(), text=text)),
    //        )
    //    )
    //detections = _automation.suppress_detections_greedy(
    //    detections=detections,
    //    iou_threshold=self._ai_text.get_iou_threshold(),
    //)
    //shapes: list[Shape] = _automation.shapes_from_detections(
    //    detections=detections, shape_type=shape_type
    //)

    this->slotTaskSubmit();
    const auto image = utils::ImageToMat(image_);
    const auto image_id = std::hash<QString>{}(this->image_path_);
    QList<TlShape> shapes = bbox_from_text::get_shapes_from_texts(this->text_osam_session_.get(), image, image_id, texts);
    this->slotTaskFinish();

    this->canvas_widgets_.canvas_->backup_shapes();
    this->load_shapes(shapes, false);
    this->mark_dirty();
}

void MainWindow::reset_state() {
    this->docks_.shape_list_->clear();
    this->annotation_ = {};
    this->image_path_.clear();
    this->label_file_path_.reset();
    this->imageData_.clear();
    this->other_data_.clear();
    this->canvas_widgets_.canvas_->reset_state();
}

// Callbacks

void MainWindow::undo_shape_edit() {
    this->canvas_widgets_.canvas_->restore_last_shape();
    this->docks_.shape_list_->clear();
    this->load_shapes(this->canvas_widgets_.canvas_->shapes_);
    this->mark_dirty();
}

void MainWindow::tutorial() const {
    QString url("https://github.com/labelmeai/labelme/tree/main/examples/tutorial");
    QDesktopServices::openUrl(url);
}

void MainWindow::on_drawing_polygon_changed(const bool drawing) {
    //In the middle of drawing, toggling between modes should be disabled.
    this->actions_.edit_mode_->setEnabled(!drawing);
    this->actions_.undo_last_point_->setEnabled(drawing);
    this->actions_.undo_->setEnabled(!drawing);
    this->actions_.delete_->setEnabled(!drawing);
}

void MainWindow::switch_canvas_mode(
    const bool edit, const QString &create_mode
) {
    if (create_mode == "ai_points_to_shape") {
        const auto model_name = this->canvas_widgets_.canvas_->get_ai_model_name();
        if (AI_MODELS_WITHOUT_POINT_SUPPORT.contains(model_name)) {
            QMessageBox::warning(
                this,
                tr("AI-Points Unavailable"),
                tr(
                    "%1 does not support point prompts.\n"
                    "Please select a different model or use AI-Box mode."
                )
                .arg(model_name)
            );
            return;
        }
    }
    this->canvas_widgets_.canvas_->set_editing(edit);
    if (!create_mode.isEmpty()) {
        this->canvas_widgets_.canvas_->create_mode_ = create_mode;
    }
    if (edit) {
        for (const auto &draw_action : this->actions_.draw_ | std::views::values) {
            draw_action->setEnabled(true);
        }
    } else {
        for (auto &[draw_mode, draw_action] : this->actions_.draw_) {
            draw_action->setEnabled(create_mode != draw_mode);
        }
    }
    // Keep edit_mode disabled while a partial shape is alive so the user
    // can't abandon it mid-draw.
    this->actions_.edit_mode_->setEnabled(
        !edit && !canvas_widgets_.canvas_->is_drawing()
    );
    this->ai_text_to_annotation_->setEnabled(
        !edit
        && AI_CREATE_MODES.contains(create_mode)
    );
    this->ai_assist_annotation_->setEnabled(!edit && AI_CREATE_MODES.contains(create_mode));
    if (create_mode == "ai_points_to_shape") {
        this->ai_assist_annotation_->set_disabled_models(AI_MODELS_WITHOUT_POINT_SUPPORT);
    } else {
        this->ai_assist_annotation_->set_disabled_models({});
    }
}

void MainWindow::highlight_ai_buttons(bool highlight) {
    this->ai_buttons_highlighted_ = highlight;
    constexpr int32_t BG_ALPHA = 60;
    constexpr int32_t BORDER_ALPHA = 120;
    // alpha 0 (not highlighted) reads as transparent; HexArgb gives "#AARRGGBB",
    // which Qt stylesheets accept.
    auto bg = this->palette().color(QPalette::ColorRole::Highlight);
    bg.setAlpha(highlight ? BG_ALPHA : 0);
    auto border = QColor(bg);
    border.setAlpha(highlight ? BORDER_ALPHA : 0);
    const QString style {
        "QToolButton:!checked:!pressed {"
        " background-color: " + bg.name(QColor::NameFormat::HexArgb) + ";" +
        " border: 1px solid " + border.name(QColor::NameFormat::HexArgb) + ";" +
        "}"
    };
    for (auto &[mode, action] : this->actions_.draw_) {
        if (AI_CREATE_MODES.contains(mode))
            for (const auto &widget : action->associatedWidgets())
                if (qobject_cast<QToolButton *>(widget))
                    widget->setStyleSheet(style);
    }
}

void MainWindow::show_label_list_menu(const QPoint &point) {
    this->label_list_menu_origin_ = this->docks_.shape_list_->mapToGlobal(point);
    try {
        // PySide6 type QMenu.exec() argument too narrowly
        this->menus_.label_list_->exec(this->label_list_menu_origin_);
    } catch (...) {}
    this->label_list_menu_origin_ = QPoint();
}

bool MainWindow::validate_label(const QString &label) {
    const QString policy = YAML_QSTR(this->config_["validate_label"]);
    if (policy.isEmpty()) {
        return true;
    }
    QStringList existing_labels;
    const auto *label_list = this->docks_.label_list_;
    for (auto i = 0; i < label_list->count(); ++i) {
        existing_labels.append(label_list->item(i)->data(Qt::ItemDataRole::UserRole).toString());
    }
    return is_valid_label(
        label, existing_labels, policy
    );
}

void MainWindow::edit_label(bool value) {
    QList<TlShape> shapes;
    auto items = this->docks_.shape_list_->selected_items();
    if (items.empty()) {
        SPDLOG_WARN("No label is selected, so cannot edit label.");
        return;
    }

    std::ranges::for_each(items, [&](auto &it) { shapes.push_back(it->shape()); });
    const auto first_shape = shapes[0];

    bool edit_text = true;
    bool edit_flags = true;
    bool edit_group_id = true;
    bool edit_description = true;
    if (items.size() > 1) {
        edit_text = std::all_of(shapes.begin() + 1, shapes.end(), [&](const auto &s) { return s.label_ == first_shape.label_; });
        edit_flags = std::all_of(shapes.begin() + 1, shapes.end(), [&](const auto &s) { return s.flags_ == first_shape.flags_; });
        edit_group_id = std::all_of(shapes.begin() + 1, shapes.end(), [&](const auto &s) {
            return s.group_id_ == first_shape.group_id_;
        });
        edit_description = std::all_of(shapes.begin() + 1, shapes.end(), [&](const auto &s) {
            return s.description_ == first_shape.description_;
        });
    }
    if (!edit_text) {
        this->label_dialog_->edit_->setDisabled(true);
        this->label_dialog_->label_list_->setDisabled(true);
    }
    if (!edit_group_id)
        this->label_dialog_->edit_group_id_->setDisabled(true);
    if (!edit_description)
        this->label_dialog_->edit_description_->setDisabled(true);

    const QPoint canvas_menu_origin = this->canvas_widgets_.canvas_->context_menu_origin_;
    const QPoint menu_origin = (
        canvas_menu_origin != QPoint() ?
            canvas_menu_origin :
            this->label_list_menu_origin_
    );

    const auto [text, flags, group_id, description] = this->label_dialog_->popUp(
        edit_text ? first_shape.label_ : "",
        menu_origin,
        edit_flags ? first_shape.flags_ : QMap<QString, bool>{},
        edit_group_id ? first_shape.group_id_ : None,
        edit_description ? first_shape.description_ : "",
        !edit_flags
    );

    if (!edit_text) {
        this->label_dialog_->edit_->setDisabled(false);
        this->label_dialog_->label_list_->setDisabled(false);
    }
    if (!edit_group_id)
        this->label_dialog_->edit_group_id_->setDisabled(false);
    if (not edit_description)
        this->label_dialog_->edit_description_->setDisabled(false);

    if (text.isEmpty()) { // canceled
        //assert flags is None
        //assert group_id is None
        //assert description is None
        return;
    }

    if (!this->validate_label(text)) {
        this->show_error_message(
            tr("Invalid label"),
            tr("Invalid label '%1' with validation type '%2'").arg(
                text, YAML_QSTR(this->config_["validate_label"])
            )
        );
        return;
    }

    this->canvas_widgets_.canvas_->backup_shapes();
    for (const auto &item : items) {
        auto shape = item->shape();
        //assert shape is not None

        if (edit_text)
            shape.label_ = text;
        if (edit_flags)
            shape.flags_ = flags;
        if (edit_group_id)
            shape.group_id_ = group_id;
        if (edit_description)
            shape.description_ = description;

        // assert shape.label is not None
        item->setText(
            format_shape_label(
                shape,
                this->get_rgb_by_label(
                    shape.label_,
                    this->docks_.label_list_
                )
            )
        );
        this->update_shape_color(shape);    // 由于保存的是对象, 先更新轮廓信息.
        this->canvas_widgets_.canvas_->update_shape_info(shape);
        this->mark_dirty();
        if (this->docks_.label_list_->find_label_item(shape.label_) == nullptr) {
            this->docks_.label_list_->add_label_item(
                shape.label_,
                this->get_rgb_by_label(
                    shape.label_,
                    this->docks_.label_list_
                )
            );
        }
    }
}

void MainWindow::on_file_search_changed() {
    this->import_images_from_dir(
        this->prev_opened_dir_, this->docks_.file_search_->text()
    );
}

void MainWindow::file_list_item_selection_changed() {
    if (!this->can_continue()) {
        return;
    }
    const auto items = this->docks_.file_list_->selectedItems();
    if (items.empty()) {
        return;
    }
    this->load_file(items[0]->text());
}

// React to canvas signals.
void MainWindow::on_shape_selection_changed(const QList<int32_t> &selected_shapes) {
    QObject::disconnect(this->docks_.shape_list_, &ShapeListView::item_selection_changed, this,
        &MainWindow::label_selection_changed
    );
    for (auto &shape : this->canvas_widgets_.canvas_->selected_shapes_) {
        this->canvas_widgets_.canvas_->shapes_[shape].selected_ = false;
    }
    this->docks_.shape_list_->clearSelection();
    this->canvas_widgets_.canvas_->selected_shapes_ = selected_shapes;
    for (auto &idx : this->canvas_widgets_.canvas_->selected_shapes_) {
        this->canvas_widgets_.canvas_->shapes_[idx].selected_ = true;
        const auto item = this->docks_.shape_list_->find_item_by_shape(this->canvas_widgets_.canvas_->shapes_[idx]);
        this->docks_.shape_list_->select_item(item);
        this->docks_.shape_list_->scroll_to_item(item);
    }
    QObject::connect(this->docks_.shape_list_, &ShapeListView::item_selection_changed, this,
        &MainWindow::label_selection_changed
    );
    const auto n_selected = !selected_shapes.empty();
    this->actions_.delete_->setEnabled(n_selected);
    this->actions_.duplicate_->setEnabled(n_selected);
    this->actions_.copy_->setEnabled(n_selected);
    this->actions_.edit_->setEnabled(n_selected);
}

void MainWindow::add_label(TlShape &shape) {
    //assert shape.label is not None
    auto *const shape_list_item = new ShapeListItem("", shape);
    this->docks_.shape_list_->add_item(shape_list_item);
    if (this->docks_.label_list_->find_label_item(shape.label_) == nullptr) {
        this->docks_.label_list_->add_label_item(
            shape.label_,
            get_rgb_by_label(
                shape.label_,
                this->docks_.label_list_
            )
        );
    }
    this->label_dialog_->add_label_history(shape.label_);
    for (const auto &action : this->actions_.on_shapes_present_) {
        action->setEnabled(true);
    }

    update_shape_color(shape);    // 由于保存的是对象, 先更新轮廓信息.
    this->canvas_widgets_.canvas_->update_shape_info(shape);
    shape_list_item->setText(
        format_shape_label(
            shape,
            this->get_rgb_by_label(
                shape.label_,
                this->docks_.label_list_
            )
        )
    );
}

std::vector<int32_t> MainWindow::get_rgb_by_label(
    const QString &label,
    LabelList *unique_label_list
) {
    if (YAML_STR(this->config_["shape_color"]) == "auto") {
        const auto *item = unique_label_list->find_label_item(label);
        const int32_t item_index = (
            (item != nullptr) ?
            unique_label_list->indexFromItem(item).row() :
            unique_label_list->count()
        );
        const int32_t label_id = (
            1   // skip black color by default
            + item_index
            + this->config_["shift_auto_shape_color"].as<int32_t>()
        );
        return rgb_from_colormap_id(label_id);
    }
    if (YAML_STR(this->config_["shape_color"]) == "manual") {
        auto rgb = rgb_from_label_colors(
            label.toStdString(), self.config_["label_colors"].as<std::map<std::string, std::vector<int32_t>>>()
        );
        if (!rgb.empty())
            return rgb;
    }
    if (!this->config_["default_shape_color"].as<std::vector<int32_t>>().empty()) {
        return this->config_["default_shape_color"].as<std::vector<int32_t>>();
    }
    return {0, 255, 0};
}

void MainWindow::remove_labels(const QList<TlShape> &shapes) {
    QObject::disconnect(this->docks_.shape_list_, &ShapeListView::item_dropped, this, &MainWindow::on_label_order_changed);
    for (const auto &shape : shapes) {
        auto *item = this->docks_.shape_list_->find_item_by_shape(shape);
        this->docks_.shape_list_->removeItem(item);
    }
    QObject::connect(this->docks_.shape_list_, &ShapeListView::item_dropped, this, &MainWindow::on_label_order_changed);
}

void MainWindow::load_shapes(QList<TlShape> &shapes, bool replace) {
    QObject::disconnect(this->docks_.shape_list_, &ShapeListView::item_selection_changed, this,
        &MainWindow::label_selection_changed
    );
    //shape: Shape
    for (auto &shape : shapes) {
        add_label(shape);
    }
    this->docks_.shape_list_->clearSelection();
    QObject::connect(this->docks_.shape_list_, &ShapeListView::item_selection_changed, this,
        &MainWindow::label_selection_changed
    );
    this->canvas_widgets_.canvas_->load_shapes(shapes, replace);
}

void MainWindow::load_flags(
    const YAML::Node &flags,
    QListWidget *widget
) const {
    widget->clear();
    //key: str
    //flag: bool
    for (const auto &&[key, flag] : flags | std::views::transform([](const auto &i) { return std::make_pair(i.first.as<std::string>(), i.second.as<bool>()); })) {
        auto *item = new QListWidgetItem(QString::fromStdString(key));
        item->setFlags(item->flags() | Qt::ItemFlag::ItemIsUserCheckable);
        item->setCheckState(
            flag ? Qt::CheckState::Checked : Qt::CheckState::Unchecked
        );
        widget->addItem(item);
    }
}

bool MainWindow::save_labels(const QString &label_path) {
    QList<ShapeDict> shapes;
    std::ranges::for_each(this->docks_.shape_list_->items(), [this, &shapes](const auto &i) {
        shapes.append(shape_to_dict(i->shape()));
    });

    QMap<QString, bool> flags = this->read_flag_dock_states();
    try {
        const QFileInfo fileInfo(this->image_path_);
        const QString imagePath = fileInfo.fileName();
        const QByteArray imageData = this->config_["with_image_data"].as<bool>() ? this->imageData_ : QByteArray{};
        if (!fileInfo.path().isEmpty() && !QFile::exists(fileInfo.path())) {
            (void)QDir().mkdir(fileInfo.path());
        }
        auto lf = std::make_unique<LabelFile>();
        lf->save(
            label_path,
            this->canvas_widgets_.canvas_->shapes_,
            imagePath,
            imageData,
            this->image_.height(),
            this->image_.width(),
            this->other_data_,
            flags
        );
        this->label_file_path_ = std::move(lf);
        const auto items = this->docks_.file_list_->findItems(this->image_path_, Qt::MatchExactly);
        if (items.count() > 0) {
            if (items.count() != 1)
                throw std::runtime_error("There are duplicate files.");
            items[0]->setCheckState(Qt::CheckState::Checked);
        }
        return true;
    } catch (const LabelFileError &e) {
        show_error_message(
            tr("Error saving label data"), tr("<b>%1</b>").arg(e.what())
        );
    }
    return false;

    try {
        //assert self._image_path
        //assert self._annotation is not None
        const auto label_dir = QFileInfo(label_path).absoluteDir().absolutePath();
        std::filesystem::create_directories(label_dir.toStdString());
        auto annotation = AnnotationEx(
            label_path,
            this->annotation_.image_data_,
            shapes,
            flags,
            this->annotation_.other_data_
        );
        write_label_file(
            label_path,
            annotation,
            this->image_.height(),
            this->image_.width(),
            this->config_["with_image_data"].as<bool>()
        );
        //this->label_file_path_ = label_path;
        const auto items = this->docks_.file_list_->findItems(
            this->image_path_, Qt::MatchFlag::MatchExactly
        );
        if (items.count() > 0) {
            if (items.count() != 1)
                throw std::runtime_error("There are duplicate files.");
            items[0]->setCheckState(Qt::CheckState::Checked);
        }
        return true;
    } catch (const LabelFileError &e) {
        this->show_error_message(
            tr("Error saving label data"), tr("<b>%1</b>").arg(e.what())
        );
    }
    return false;
}

// 粘贴时需要为图形生成新的uuid.
void MainWindow::insert_shapes(const QList<TlShape> &shapes) {
    if (shapes.empty()) return;
    QList<TlShape> copied_shapes;
    std::ranges::for_each(shapes, [&copied_shapes](auto &s) { copied_shapes.push_back(s.clone()); });
    this->load_shapes(copied_shapes, false);
    this->canvas_widgets_.canvas_->select_shapes(copied_shapes);
    this->mark_dirty();
}

void MainWindow::label_selection_changed() {
    QList<TlShape> selected_shapes = {};
    for (const auto &item : this->docks_.shape_list_->selected_items()) {
        auto shape = item->shape();
        //assert shape is not None
        selected_shapes.append(shape);
    }
    if (!selected_shapes.empty()) {
        this->canvas_widgets_.canvas_->select_shapes(selected_shapes);
    } else {
        if (this->canvas_widgets_.canvas_->deselect_shape()) {
            this->canvas_widgets_.canvas_->update();
        }
    }
}

void MainWindow::on_label_item_changed(const ShapeListItem *item) {
    const auto shape = item->shape();
    this->canvas_widgets_.canvas_->set_shape_visible(shape, item->checkState() == Qt::Checked);

    //bool is_visible_new = item->checkState() == Qt::CheckState::Checked;
    //
    //bool selected_group = (
    //    this->docks_.shape_list_->selection_at_press()
    //    || this->docks_.shape_list_->selected_items()
    //);
    //items_to_toggle = (
    //    selected_group
    //    if item in selected_group and len(selected_group) > 1
    //    else [item]
    //);
    //items_to_change = [
    //    it
    //    for it in items_to_toggle
    //    if (sh := it.shape()) is not None and sh.visible != is_visible_new
    //];
    //if not items_to_change:
    //    return;
    //
    //new_check_state = (
    //    Qt.CheckState.Checked if is_visible_new else Qt.CheckState.Unchecked
    //);
    //with QtCore.QSignalBlocker(this->docks_.label_list_._model):
    //    for item_to_toggle in items_to_change:
    //        shape_to_toggle = item_to_toggle.shape()
    //        assert shape_to_toggle is not None
    //        item_to_toggle.setCheckState(new_check_state)
    //        this->canvas_widgets_.canvas_->set_shape_visible(
    //            shape=shape_to_toggle, value=is_visible_new
    //        );
    //
    //this->canvas_widgets_.canvas_->backup_shapes();
    //this->actions_.undo_->setEnabled(this->canvas_widgets_.canvas_->can_restore_shape);
}

void MainWindow::on_label_order_changed() {
    mark_dirty();
    // 不能且不需要重新加载, shape_list中保存的原始图形, 不包含锚点调整信息。
    //QList<TlShape> shapes;
    //QList<ShapeListItem *> items = shape_list_->items();
    //std::ranges::transform(items, std::back_inserter(shapes), [](auto &item){ return item->shape(); });
    //canvas_widgets_.canvas_->loadShapes(shapes);
}

// Callback functions:

void MainWindow::on_new_shape() {
    auto items = this->docks_.label_list_->selectedItems();
    QString text;
    if (!items.isEmpty()) {
        text = items[0]->data(Qt::UserRole).toString();
    }
    QMap<QString, bool> flags = {};
    int32_t group_id = None;
    QString description;
    if (this->config_["display_label_popup"].as<bool>() || text.isEmpty()) {
        QString previous_text = this->label_dialog_->edit_->text();
        std::tie(text, flags, group_id, description) = this->label_dialog_->popUp(text);
        if (text.isEmpty()) {
            this->label_dialog_->edit_->setText(previous_text);
        }
    }

    if (!text.isEmpty() && !validate_label(text)) {
        show_error_message(
            tr("Invalid label"),
            tr("Invalid label '%1' with validation type '%2'").arg(
                text, this->config_["validate_label"].as<bool>()
            )
        );
        text = "";
    }
    if (!text.isEmpty()) {
        this->docks_.label_list_->clearSelection();
        //assert isinstance(flags, dict)
        auto shapes = this->canvas_widgets_.canvas_->set_last_label(text, group_id, description, flags);    // 在Canvas上更新.
        for (auto shape : shapes) {
            shape.group_id_ = group_id;
            shape.description_ = description;
            add_label(shape);
        }
        this->actions_.edit_mode_->setEnabled(true);
        this->actions_.undo_last_point_->setEnabled(false);
        this->actions_.undo_->setEnabled(true);
        mark_dirty();
    } else {
        this->canvas_widgets_.canvas_->undo_last_line();
        this->canvas_widgets_.canvas_->shape_backups_.pop_back();
    }
}

void MainWindow::on_inference_produced_no_shapes() {
    this->show_status_message(
        tr("AI inference produced no new annotation."), 5000
    );
}

void MainWindow::on_inference_failed(const QString &message) {
    this->show_status_message(tr("AI inference failed: %s") % message, 10000);
}

void MainWindow::on_scroll_request(int32_t delta, Qt::Orientation orientation) {
    const auto units = -delta * 0.1;  // natural scroll
    const auto *bar = this->canvas_widgets_.scroll_bars_[orientation];
    const auto value = bar->value() + bar->singleStep() * units;
    this->set_scroll_value(orientation, value);
}

void MainWindow::on_pan_request(const QPoint &step) {
    // Pan moves the viewport opposite to the cursor delta so the image
    // tracks the grabbed point one-for-one in widget pixels.
    const auto *h_bar = this->canvas_widgets_.scroll_bars_[Qt::Horizontal];
    const auto *v_bar = this->canvas_widgets_.scroll_bars_[Qt::Vertical];
    this->set_scroll_value(Qt::Horizontal, h_bar->value() - step.x());
    this->set_scroll_value(Qt::Vertical, v_bar->value() - step.y());
}

void MainWindow::set_scroll_value(Qt::Orientation orientation, float value) {
    this->canvas_widgets_.scroll_bars_[orientation]->setValue(value);
    if (!this->image_path_.isEmpty())
        this->scroll_values_[orientation][this->image_path_] = value;
}

void MainWindow::set_zoom(int32_t value, QPointF pos) {
    if (this->image_path_.isEmpty()) {
        SPDLOG_WARN("image_path is None, cannot set zoom");
        return;
    }

    if (pos.isNull())
        pos = QPointF(
            this->canvas_widgets_.canvas_->visibleRegion().boundingRect().center()
        );
    int32_t canvas_width_old = this->canvas_widgets_.canvas_->width();

    this->sync_zoom_mode_actions();
    this->canvas_widgets_.zoom_widget_->setValue(value);  // triggers self._paint_canvas
    this->zoom_values_[image_path_] = {this->zoom_mode_, value};

    int32_t canvas_width_new = this->canvas_widgets_.canvas_->width();
    if (canvas_width_old == canvas_width_new) {
        return;
    }
    float canvas_scale_factor = 1.0*canvas_width_new / canvas_width_old;
    float x_shift = pos.x() * canvas_scale_factor - pos.x();
    float y_shift = pos.y() * canvas_scale_factor - pos.y();
    set_scroll_value(
        Qt::Horizontal,
        this->canvas_widgets_.scroll_bars_[Qt::Horizontal]->value() + x_shift
    );
    set_scroll_value(
        Qt::Vertical,
        this->canvas_widgets_.scroll_bars_[Qt::Vertical]->value() + y_shift
    );
}

void MainWindow::set_zoom_to_original() {
    this->zoom_mode_ = ZoomMode::MANUAL_ZOOM;
    set_zoom(100);
}

void MainWindow::add_zoom(const float increment, const QPointF &pos) {
    // Multiplicative stepping on a float widget; the QDoubleSpinBox rounds to
    // its decimal precision, so no integer ceil/floor clamping is needed.
    const int32_t zoom_value = this->canvas_widgets_.zoom_widget_->value() * increment;
    this->zoom_mode_ = ZoomMode::MANUAL_ZOOM;
    this->set_zoom(zoom_value, pos);
}

void MainWindow::zoom_requested(const int32_t delta, const QPointF &pos) {
    this->add_zoom(delta > 0 ? 1.1 : 0.9, pos);
}

void MainWindow::set_fit_window_mode(const bool value) {
    const auto target = value ? ZoomMode::FIT_WINDOW : ZoomMode::MANUAL_ZOOM;
    this->switch_zoom_mode(target);
}

void MainWindow::set_fit_width_mode(const bool value) {
    const auto target = value ? ZoomMode::FIT_WIDTH : ZoomMode::MANUAL_ZOOM;
    this->switch_zoom_mode(target);
}

void MainWindow::switch_zoom_mode(const ZoomMode mode) {
    this->zoom_mode_ = mode;
    this->adjust_scale();
}

void MainWindow::sync_zoom_mode_actions() {
    this->actions_.fit_window_->setChecked(this->zoom_mode_ == ZoomMode::FIT_WINDOW);
    this->actions_.fit_width_->setChecked(this->zoom_mode_ == ZoomMode::FIT_WIDTH);
}

// QPixmap::fromImage: 深拷贝, 原始QImage的数据会被复制到新的QPixmap中.
void MainWindow::on_brightness_contrast_changed(const QImage &image) {
    this->canvas_widgets_.canvas_->load_pixmap(
        QPixmap::fromImage(image), this->image_path_, false
    );
}

void MainWindow::open_brightness_contrast_dialog(
    bool value, const bool is_initial_load
) {
    if (this->image_path_.isEmpty()) {
        SPDLOG_WARN("image_path is None, cannot set brightness/contrast");
        return;
    }

    int32_t brightness = None;
    int32_t contrast = None;
    if (const auto it = this->brightness_contrast_values_.find(this->image_path_); it != this->brightness_contrast_values_.end()) {
        brightness = it->first; contrast = it->second;
    }

    if (is_initial_load) {
        if (this->config_["keep_prev_brightness_contrast"].as<bool>() && !this->prev_image_path_.isEmpty())
            if (const auto it = this->brightness_contrast_values_.find(prev_image_path_); it != this->brightness_contrast_values_.end()) {
                brightness = it->first, contrast = it->second;
            }
        if (brightness == None && contrast == None) {
            return;
        }
    }

    SPDLOG_DEBUG(
        "Opening brightness/contrast dialog with brightness={}, contrast={}",
        brightness,
        contrast,
    );
    auto dialog = BrightnessContrast(
        QImage::fromData(imageData_),
        [this](const QImage &image) { on_brightness_contrast_changed(image); },
        this
    );

    if (brightness != None)
        dialog.slider_brightness_->setValue(brightness);
    if (contrast != None)
        dialog.slider_contrast_->setValue(contrast);

    if (is_initial_load) {
        dialog.onNewValue(None);
    } else {
        dialog.exec();
        brightness = dialog.slider_brightness_->value();
        contrast = dialog.slider_contrast_->value();
    }

    this->brightness_contrast_values_[this->image_path_] = {brightness, contrast};
    SPDLOG_DEBUG(
        "Updated states for {}: brightness={}, contrast={}",
        this->image_path_,
        brightness,
        contrast);
}

void MainWindow::toggle_shape_visibility(int32_t value) {
    for (auto *item : this->docks_.shape_list_->items()) {
        auto target = (
            value == None ? item->checkState() == Qt::Unchecked : value
        );
        item->setCheckState(
            target ? Qt::Checked : Qt::Unchecked
        );
    }
}

AnnotationEx MainWindow::open_label_file_into_state(const QString &label_path) {
    AnnotationEx annotation;
    try {
        annotation = read_label_file(label_path);
    } catch (LabelFileError &e) {
        this->show_file_open_error(label_path, "label", "", e.what());
        return {};
    }
    //this->label_file_path_ = label_path;
    this->annotation_ = annotation;
    this->image_path_ = QFileInfo(label_path).path() + "/" + annotation.image_path_;
    return annotation;
}

bool MainWindow::open_image_into_state(const QString &image_path) {
    QByteArray image_data;
    try {
        image_data = read_image_file(image_path);
    } catch (OSError &e) {
        this->show_file_open_error(image_path, "image", e.what(), "");
        return false;
    }
    this->annotation_ = AnnotationEx(
        QFileInfo(image_path).fileName(),
        image_data,
        {},
        {},
        {}
    );
    this->image_path_ = image_path;
    this->label_file_path_.reset();
    return true;
}

void MainWindow::load_file(const QString &image_or_label_path) {
    // changing fileListWidget loads file
    if (this->image_list().contains(image_or_label_path) &&
        this->docks_.file_list_->currentRow() != this->image_list().indexOf(image_or_label_path)
    ) {
        this->docks_.file_list_->setCurrentRow(
            this->image_list().indexOf(image_or_label_path)
        );
        this->docks_.file_list_->repaint();
        return;
    }

    QList<TlShape> prev_shapes = (
        this->config_["keep_prev"].as<bool>() || QApplication::keyboardModifiers() == (Qt::ControlModifier | Qt::ShiftModifier)
        ? this->canvas_widgets_.canvas_->shapes_ : QList<TlShape>{}
    );
    this->prev_image_path_ = this->image_path_;
    this->reset_state();
    this->canvas_widgets_.canvas_->setEnabled(false);
    if (!QFile::exists(image_or_label_path)) {
        this->show_error_message(
            tr("Error opening file"),
            tr("No such file: <b>%1</b>").arg(image_or_label_path)
        );
        return;
    }
    // assumes same name, but json extension
    this->show_status_message(
        tr("Loading %1...").arg(QFileInfo(image_or_label_path).baseName())
    );

    const auto t0_load_file = std::chrono::system_clock::now();
    if (QString label_path; QFile::exists(
        label_path = resolve_label_path(image_or_label_path))
    ) {
        try {
            this->label_file_path_ = std::make_unique<LabelFile>(label_path);
        } catch (LabelFileError &e) {
            show_error_message(
                tr("Error opening file"),
                tr(
                    "<p><b>%1</b></p>"
                    "<p>Make sure <i>%2</i> is a valid label file.</p>"
                )
                .arg(e.what(), label_path)
            );
            show_status_message(tr("Error reading %1").arg(label_path));
            return;
        }
        this->imageData_ = label_file_path_->imageData_;
        this->image_path_ = QFileInfo(label_path).absolutePath() + "/" + label_file_path_->imagePath_;
        this->other_data_ = label_file_path_->otherData_;
    } else {
        auto image_path = image_or_label_path;
        try {
            this->imageData_ = LabelFile::load_image_file(image_path);
        } catch (OSError &e) {
            show_error_message(
                tr("Error opening file"),
                tr(
                    "<p><b>%1</b></p>"
                    "<p>Make sure <i>%2</i> is a valid image file.</p>"
                )
                .arg(e.what(), image_path)
            );
            show_status_message(tr("Error reading %1").arg(image_path));
            return;
        }
        if (!imageData_.isEmpty())
            this->image_path_ = image_path;
        this->label_file_path_.reset();
    }
    auto t0 = std::chrono::system_clock::now();
    const auto image = QImage::fromData(imageData_);
    SPDLOG_INFO("Created QImage in {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - t0).count());

    if (image.isNull()) {
        QStringList formats;
        for (auto &fmt : QImageReader::supportedImageFormats()) {
            formats.append(QString("*.%1").arg(fmt.toLower()));
        }
        show_error_message(
            tr("Error opening file"),
            tr(
                "<p>Make sure <i>%1</i> is a valid image file.<br/>"
                "Supported image formats: %2</p>"
            ).arg(image_or_label_path, formats.join(","))
        );
        show_status_message(tr("Error reading %1").arg(image_or_label_path));
        return;
    }
    this->image_ = image;
    t0 = std::chrono::system_clock::now();
    this->canvas_widgets_.canvas_->load_pixmap(QPixmap::fromImage(image), this->image_path_);
    SPDLOG_INFO("Loaded pixmap in {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - t0).count());
    YAML::Node flags; //flags = {k: False for k in config_["flags"] or []}
    if (this->label_file_path_) {
        load_shape_dicts(label_file_path_->shapes1_);
        //if (labelFile_->flags_ is not None) {
        //    flags.update(this->labelFile.flags);
        //}
    }
    this->load_flags(flags, this->docks_.flag_list_);
    if (config_["keep_prev"].as<bool>() && this->has_no_shapes()) {
        this->load_shapes(prev_shapes, false);
        this->mark_dirty();
    } else {
        this->mark_clean();
    }
    this->canvas_widgets_.canvas_->setEnabled(true);
    // set zoom values
    bool is_initial_load = !zoom_values_.empty();
    if (this->zoom_values_.contains(image_path_)) {
        this->zoom_mode_ = this->zoom_values_[this->image_path_].first;
        this->set_zoom(this->zoom_values_[this->image_path_].second);
    } else if (is_initial_load || !this->config_["keep_prev_scale"].as<bool>()) {
        this->zoom_mode_ = ZoomMode::FIT_WINDOW;
        this->adjust_scale();
    }
    // set scroll values
    for (auto &orientation : this->scroll_values_.keys()) {
        if (this->scroll_values_[orientation].contains(this->image_path_))
            this->set_scroll_value(
                orientation, this->scroll_values_[orientation][this->image_path_]
            );
    }
    this->open_brightness_contrast_dialog(false, true);
    this->paint_canvas();
    this->update_action_states(true);
    this->canvas_widgets_.canvas_->setFocus();
    this->show_status_message(
        tr("Loaded %1").arg(QFileInfo(image_or_label_path).baseName())
    );
    SPDLOG_INFO(
        "Loaded file: {} in {}ms",
        image_or_label_path,
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - t0_load_file).count()
    );
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    if (
        this->canvas_widgets_.canvas_ &&
        !this->image_.isNull() &&
        this->zoom_mode_ != ZoomMode::MANUAL_ZOOM) {
        this->adjust_scale();
    }
    QMainWindow::resizeEvent(event);
}

void MainWindow::paint_canvas() {
    if (this->image_.isNull()) {
        SPDLOG_WARN("image is null, cannot paint canvas");
        return;
    }
    canvas_widgets_.canvas_->scale_ = (
        0.01 * this->canvas_widgets_.zoom_widget_->value()
    );
    this->canvas_widgets_.canvas_->adjustSize();
    this->canvas_widgets_.canvas_->update();
}

void MainWindow::adjust_scale() {
    float scale = 1.0f;
    if (this->zoom_mode_ == ZoomMode::FIT_WINDOW)
        scale = this->fit_window_scale();
    else if (this->zoom_mode_ == ZoomMode::FIT_WIDTH)
        scale = this->fit_width_scale();
    else
        scale = 1.0;
    this->set_zoom(scale * 100);
}

float MainWindow::fit_window_scale() const {
    const float FIT_WINDOW_SCROLLBAR_MARGIN = 2.0f;
    const auto viewport = this->centralWidget();
    const auto &pixmap = this->canvas_widgets_.canvas_->pixmap_;
    const auto available_w = viewport->width() - FIT_WINDOW_SCROLLBAR_MARGIN;
    const auto available_h = viewport->height() - FIT_WINDOW_SCROLLBAR_MARGIN;
    const auto scale_by_width = available_w / (pixmap.width() * 1.f);
    const auto scale_by_height = available_h / (pixmap.height() * 1.f);
    return std::min(scale_by_width, scale_by_height);
}

float MainWindow::fit_width_scale() const {
    const float FIT_WIDTH_SCROLLBAR_MARGIN = 15.0f;
    const float available_w = this->centralWidget()->width() - FIT_WIDTH_SCROLLBAR_MARGIN;
    return available_w / this->canvas_widgets_.canvas_->pixmap_.width();
}

void MainWindow::set_save_image_with_data(bool enabled) {
    this->config_["with_image_data"] = enabled;
    this->actions_.save_with_image_data_->setChecked(enabled);
}

void MainWindow::reset_layout() {
    this->window_state_.remove("window/state");
    this->restoreState(this->default_state_);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (!can_continue()) {
        event->ignore();
    }
    this->window_state_.setValue("window/size", this->size());
    this->window_state_.setValue("window/position", this->pos());
    this->window_state_.setValue("window/state", this->saveState());
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    QStringList extensions;
    for (auto &fmt : QImageReader::supportedImageFormats()) {
        extensions.append(QString(".%1").arg(fmt.toLower()));
    }
    if (event->mimeData()->hasUrls()) {
        bool endsWidth = false;
        for (auto &i : event->mimeData()->urls()) {
            const auto url = i.toLocalFile().toLower();
            for (auto &ext : extensions) {
                endsWidth |= url.endsWith(ext);
            }
            if (endsWidth) {
                event->accept();
                break;
            }
        }
    } else {
        event->ignore();
    }
}

void MainWindow::dropEvent(QDropEvent *event) {
    if (!can_continue()) {
        event->ignore();
        return;
    }
    QStringList items;
    std::ranges::for_each(event->mimeData()->urls(), [&](auto &i){ items.append(i.toLocalFile()); });
    import_dropped_image_files(items);
}

// User Dialogs #

void MainWindow::open_prev_image(bool value) {
    int32_t row_prev = docks_.file_list_->currentRow() - 1;
    if (row_prev < 0) {
        SPDLOG_INFO("there is no prev image");
        return;
    }
    SPDLOG_INFO("setting current row to {}", row_prev);
    docks_.file_list_->setCurrentRow(row_prev);
    docks_.file_list_->repaint();
}

void MainWindow::open_next_image(bool value) {
    int32_t row_next = docks_.file_list_->currentRow() + 1;
    if (row_next >= docks_.file_list_->count()) {
        SPDLOG_INFO("there is no next image");
        return;
    }
    SPDLOG_INFO("setting current row to {}", row_next);
    docks_.file_list_->setCurrentRow(row_next);
    docks_.file_list_->repaint();
}

void MainWindow::open_file_with_dialog(bool value) {
    if (!can_continue()) {
        return;
    }
    QStringList formats;
    std::ranges::for_each(QImageReader::supportedImageFormats(), [&](const auto &fmt) {
        formats.append("*." + QString::fromUtf8(fmt));
    });
    const QString filters = tr("Image & Label files (%1)").arg(
        formats.join(" ") + " *" + LabelFile::suffix
    );
    const auto image_or_label_path = QFileDialog::getOpenFileName(
        this,
        tr("%1 - Choose Image or Label file").arg(_appname_),
        QString::fromStdString(AppConfig::instance().last_work_dir_),
        filters
    );
    if (!image_or_label_path.isEmpty()) {
        this->load_from_file_or_dir(image_or_label_path);
    }
}

void MainWindow::prompt_output_dir(bool _value) {
    QString default_output_dir;
    if (!this->output_dir_.isEmpty()) {
        default_output_dir = this->output_dir_;
    } else if (!this->image_path_.isEmpty()) {
        default_output_dir = QFileInfo(this->image_path_).path();
    } else  {
        default_output_dir = this->current_path();
    }

    const auto output_dir = QFileDialog::getExistingDirectory(
        this,
        tr("%1 - Save/Load Annotations in Directory").arg(_appname_),
        default_output_dir,
        QFileDialog::ShowDirsOnly
        | QFileDialog::DontResolveSymlinks
    );
    //output_dir = str(output_dir)

    if (output_dir.isEmpty()) {
        return;
    }
    this->output_dir_ = output_dir;

    this->statusBar()->showMessage(
        tr("%1 . Annotations will be saved/loaded in %2")
        .arg("Change Annotations Dir", this->output_dir_)
    );
    this->statusBar()->show();

    const auto current_image_path = this->image_path_;
    this->import_images_from_dir(this->prev_opened_dir_);

    if (this->image_list().contains(current_image_path)) {
        // retain currently selected file
        this->docks_.file_list_->setCurrentRow(
            this->image_list().indexOf(current_image_path)
        );
        this->docks_.file_list_->repaint();
    }
}

void MainWindow::save_label_file(bool save_as) {
    //assert not self.image.isNull(), "cannot save empty image"

    QString label_path;
    if (!save_as && (this->label_file_path_ != nullptr))
        label_path = this->label_file_path_->filename_;
    if (label_path.isEmpty())
        label_path = this->prompt_save_file_path();

    if (label_path.isEmpty()) {
        SPDLOG_WARN("label_path={} is empty, so cannot save", label_path);
        return;
    }
    if (this->save_labels(label_path)) {
        this->mark_clean();
    }
}

QString MainWindow::prompt_save_file_path() {
    //assert self._image_path is not None
    const QString caption = tr("%1 - Choose File").arg(_appname_);
    const QString filters = tr("Label files (*%1)").arg(LabelFile::suffix);
    auto dlg = QFileDialog(
        this,
        caption,
        !output_dir_.isEmpty() ? output_dir_ : QFileInfo(image_path_).path(),
        filters
    );
    dlg.setDefaultSuffix("json");
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setOption(QFileDialog::DontConfirmOverwrite, false);
    dlg.setOption(QFileDialog::DontUseNativeDialog, false);
    QString label_path = dlg.getSaveFileName(
        this,
        tr("Choose File"),
        resolve_label_path(image_path_),
        tr("Label files (*%1)").arg(LabelFile::suffix)
    );
    return label_path;
}

void MainWindow::close_file(bool value) {
    if (!can_continue()) {
        return;
    }
    this->reset_state();
    this->mark_clean();
    this->update_action_states(false);
    canvas_widgets_.canvas_->setEnabled(false);
    docks_.file_list_->setFocus();
    actions_.save_as_->setEnabled(false);
}

QString MainWindow::current_label_file_path() {
    //assert self.image_path_ is not None
    std::filesystem::path file_path(image_path_.toStdString());
    return QString::fromStdString(file_path.replace_extension("json").string());
}

bool MainWindow::confirm_deletion(const QString &message) {
    auto msg_box = QMessageBox(this);
    msg_box.setIcon(QMessageBox::Icon::Warning);
    msg_box.setWindowTitle(tr("Attention"));
    msg_box.setText(message);
    const auto *delete_button = msg_box.addButton(
        tr("Delete"), QMessageBox::ButtonRole::DestructiveRole
    );
    auto cancel_button = msg_box.addButton(
        tr("Cancel"), QMessageBox::ButtonRole::RejectRole
    );
    msg_box.setDefaultButton(cancel_button);
    msg_box.exec();
    return msg_box.clickedButton() == delete_button;
}

void MainWindow::delete_file() {
    const auto msg = tr(
        "You are about to permanently delete this label file, proceed anyway?"
    );
    const auto answer = QMessageBox::warning(
        this,
        tr("Attention"),
        msg,
        QMessageBox::Yes | QMessageBox::No
    );
    if (answer != QMessageBox::Yes) {
        return;
    }

    const auto annotation_path = current_label_file_path();
    if (!QFileInfo::exists(annotation_path)) {
        return;
    }
    QFile::remove(annotation_path);
    SPDLOG_INFO("Label file is removed: {}", annotation_path);

    auto *const item = docks_.file_list_->currentItem();
    if (item) {
        item->setCheckState(Qt::Unchecked);
    }

    // 修改: 删除标签文件后保持当前打开图像.
    //resetState();
    docks_.shape_list_->clear();
    this->label_file_path_.reset();
    this->other_data_.clear();
    canvas_widgets_.canvas_->reset_state();
    canvas_widgets_.canvas_->load_pixmap(QPixmap::fromImage(this->image_), this->image_path_);
}

//@property
bool MainWindow::is_settings_editable() {
    return !self.config_file_.isEmpty(); // && not self.config_overrides_;
}

LabelDialog *MainWindow::make_label_dialog() {
    return new LabelDialog(
        this,
        YAML_VSTR(config_["labels"]),
        config_["sort_labels"].as<bool>(),
        config_["show_label_text_field"].as<bool>(),
        YAML_QSTR(config_["label_completion"]),
        YAML_QMAP(config_["fit_to_content"]),
        YAML_QMAP(config_["label_flags"])
    );
}

bool MainWindow::on_setting_changed(const QString &key_path, QObject value) {
    //# The dialog only opens with an editable config file (see _open_settings),
    //# so there is always a file to persist to.
    //if self._config_file is None:
    //    return False
    //try:
    //    _config.set_override(
    //        config_file=self._config_file, key_path=key_path, value=value
    //    )
    //except (OSError, ValueError) as e:
    //    QtWidgets.QMessageBox.warning(self, self.tr("Configuration Error"), str(e))
    //    return False
    //
    //node: dict = self._config
    //for key in key_path[:-1]:
    //    node = node[key]
    //node[key_path[-1]] = value
    //self._apply_to_live_widgets(key_path=key_path)
    return true;
}

void MainWindow::apply_to_live_widgets(const QString &key_path) {
    //if key_path == ("color_theme",):
    //    # apply_color_theme -> setColorScheme emits colorSchemeChanged, which
    //    # drives _retheme; no explicit refresh needed here.
    //    _utils.apply_color_theme(theme=self._config["color_theme"])
    //elif key_path == ("shape", "show_labels"):
    //    canvas = self._canvas_widgets.canvas
    //    canvas.set_show_labels(self._config["shape"]["show_labels"])
    //    canvas.update()
    //elif key_path == ("canvas", "allow_out_of_bounds_points"):
    //    canvas = self._canvas_widgets.canvas
    //    canvas.set_allow_out_of_bounds_points(
    //        self._config["canvas"]["allow_out_of_bounds_points"]
    //    )
    //    canvas.update()
    //elif key_path[0] == "labels":
    //    # Update predefined labels in place so session history (labels learned
    //    # from loaded/created shapes via add_label_history) is preserved, while
    //    # a removed predefined label drops from suggestions unless it was used
    //    # this session.
    //    self._label_dialog.set_predefined_labels(self._config["labels"] or [])
    //    # The Label List dock is append-only (a shape's label stays after the
    //    # shape is deleted), so add new predefined labels and leave removed
    //    # ones until restart.
    //    for label in self._config["labels"] or []:
    //        if (
    //            self._docks.unique_label_list.find_label_item(label=label)
    //            is not None
    //        ):
    //            continue
    //        self._docks.unique_label_list.add_label_item(
    //            label=label,
    //            color=self._get_rgb_by_label(
    //                label=label,
    //                unique_label_list=self._docks.unique_label_list,
    //            ),
    //        )
    //elif key_path[0] == "flags":
    //    # The flag dock otherwise only repopulates on the next image load.
    //    # Refresh it now additively: add newly predefined flags (unchecked) and
    //    # keep every flag already in the dock with its checked state. Like the
    //    # label docks, a flag removed from the config lingers until the next
    //    # image load, so the edit never drops a flag the current image carries.
    //    current = self._read_flag_dock_states()
    //    flags = {key: False for key in self._config["flags"] or []}
    //    flags.update(current)
    //    self._load_flags(flags=flags, widget=self._docks.flag_list);
}

QMap<QString, bool> MainWindow::read_flag_dock_states() {
    QMap<QString, bool> flags;
    for (auto i = 0; i < this->docks_.flag_list_->count(); ++i) {
        const auto *item = this->docks_.flag_list_->item(i);
        //assert item is not None
        flags[item->text()] = item->checkState() == Qt::CheckState::Checked;
    }
    return flags;
}

void MainWindow::open_settings() {
    //if not self._is_settings_editable:
    //    return
    //# Keep a single dialog instance; it edits self._config by reference, so
    //# reopening it shows the current values without rebuilding.
    //if self._settings_dialog is None:
    //    self._settings_dialog = SettingsDialog(
    //        config=self._config,
    //        apply_setting=self._on_setting_changed,
    //        open_as_text=self._open_config_file,
    //        parent=self,
    //    )
    //self._settings_dialog.show()
    //self._settings_dialog.raise_()
    //self._settings_dialog.activateWindow()
}

void MainWindow::open_config_file() {
    //# Only reachable from the Settings dialog, which opens solely when the
    //# config is an editable file (see _is_settings_editable).
    //assert self._config_file is not None
    //config_file: Path = self._config_file
    //
    //# Hand off to the text editor: close the dialog first so flush-on-close
    //# persists current values, then drop it so a later Close cannot overwrite
    //# the hand-edits.
    //if self._settings_dialog is not None:
    //    self._settings_dialog.close()
    //    self._settings_dialog.deleteLater()
    //    self._settings_dialog = None
    //
    //system: str = platform.system()
    //if system == "Darwin":
    //    subprocess.Popen(["open", "-t", config_file])
    //elif system == "Windows":
    //    os.startfile(config_file)  # ty: ignore[unresolved-attribute]  # Windows-only
    //else:
    //    subprocess.Popen(["xdg-open", config_file])
}

bool MainWindow::has_label_file() {
    if (this->image_path_.isEmpty()) {
        return false;
    }

    auto label_file = this->current_label_file_path();
    return QFile::exists(label_file);
}

bool MainWindow::can_continue() {
    if (!this->is_changed_) {
        return true;
    }
    const QString prompt_text = QString(tr("Save annotations to \"{%1}\" before closing?")).arg(
        this->image_path_
    );
    auto user_choice = QMessageBox::question(
        this,
        tr("Save annotations?"),
        prompt_text,
        QMessageBox::StandardButton::Save
        | QMessageBox::StandardButton::Discard
        | QMessageBox::StandardButton::Cancel,
        QMessageBox::StandardButton::Save
    );
    if (user_choice ==  QMessageBox::StandardButton::Save) {
        this->save_label_file();
        return true;
    }
    return user_choice ==  QMessageBox::StandardButton::Discard;
}

void MainWindow::show_error_message(const QString &title, const QString &message) {
    QMessageBox::critical(
        this, title, QString("<p><b>%1</b></p>%2").arg(title, message)
    );
}

void MainWindow::show_file_open_error(
    const QString &path,
    const QString &file_kind,
    const QString &exc,
    const QString &extra
) {
    QString message;
    if (file_kind == "label") {
        message = tr(
            "The selected label file could not be opened: %1"
        ).arg(path);
    } else {
        message = tr(
            "The selected image file could not be opened: %1"
        ).arg(path);
    }
    if (!exc.isEmpty())
        message = message + "\n\n" + exc;
    if (!extra.isEmpty())
        message = message + "\n\n" + extra;
    QMessageBox::critical(this, tr("Error opening file"), message);
    this->show_status_message(tr("Failed to load: %1").arg(path));
}

QString MainWindow::current_path() {
    return this->image_path_.isEmpty() ? "." : QFileInfo(this->image_path_).path();
}

void MainWindow::remove_selected_point() {
    this->canvas_widgets_.canvas_->remove_selected_point();
    this->canvas_widgets_.canvas_->update();
    if (
        this->canvas_widgets_.canvas_->hovered_shape_ != None &&
        this->canvas_widgets_.canvas_->shapes_[this->canvas_widgets_.canvas_->hovered_shape_].points_.empty())
    {
        this->canvas_widgets_.canvas_->delete_shape(
            this->canvas_widgets_.canvas_->shapes_[this->canvas_widgets_.canvas_->hovered_shape_]
        );
        this->remove_labels({ this->canvas_widgets_.canvas_->shapes_[this->canvas_widgets_.canvas_->hovered_shape_] });
        if (this->has_no_shapes()) {
            for (const auto &action : this->actions_.on_shapes_present_) {
                action->setEnabled(false);
            }
        }
    }
    this->mark_dirty();
}

void MainWindow::delete_selected_shapes() {
    const auto yes = QMessageBox::Yes, no = QMessageBox::No;
    const auto msg = tr(
        "You are about to permanently delete %1 shapes, proceed anyway?"
    ).arg(this->canvas_widgets_.canvas_->selected_shapes_.length());
    if (yes == QMessageBox::warning(
        this, tr("Attention"), msg, yes | no, yes
    )) {
        this->remove_labels(canvas_widgets_.canvas_->delete_selected());
        this->mark_dirty();
        if (this->has_no_shapes()) {
            for (auto *action : this->actions_.on_shapes_present_) {
                action->setEnabled(false);
            }
        }
    }
}

void MainWindow::copy_shape() {
    this->canvas_widgets_.canvas_->end_move(true);
    for (auto &&shape : this->canvas_widgets_.canvas_->selected_shapes_ | std::views::transform([this](int32_t i) { return this->canvas_widgets_.canvas_->shapes_[i]; })) {
        this->add_label(shape);
    }
    this->docks_.label_list_->clearSelection();
    this->mark_dirty();
}

void MainWindow::move_shape() {
    this->canvas_widgets_.canvas_->end_move(false);
    this->mark_dirty();
}

void MainWindow::load_from_file_or_dir(const QString &file_or_dir) {
    if (file_or_dir.isEmpty())
        throw std::invalid_argument("file_or_dir cannot be empty");

    if (is_label_file_path(file_or_dir)) {
        this->docks_.file_list_->clear();
        this->docks_.file_dock_->setEnabled(false);
        this->docks_.file_dock_->setToolTip(
            tr("File list is disabled when a label file is opened")
        );
        this->load_file(file_or_dir);
    } else if (QFileInfo(file_or_dir).isDir()) {
        this->import_images_from_dir(
            file_or_dir, this->docks_.file_search_->text()
        );
        this->open_next_image();
    } else {
        this->import_images_from_dir(
            QFileInfo(file_or_dir).path(),
            this->docks_.file_search_->text()
        );
        this->load_file(file_or_dir);
    }
}

void MainWindow::open_dir_with_dialog(bool value) {
    if (!this->can_continue()) {
        return;
    }

    QString default_open_dir_path;
    if (!this->prev_opened_dir_.isEmpty() && QFile::exists(this->prev_opened_dir_)) {
        default_open_dir_path = this->prev_opened_dir_;
    } else {
        default_open_dir_path =
            this->image_path_.isEmpty() ? "." : QFileInfo(this->image_path_).path();
    }

    auto dir_path = QString(
        QFileDialog::getExistingDirectory(
            this,
            tr("%1 - Open Directory").arg(_appname_),
            default_open_dir_path,
            QFileDialog::Option::ShowDirsOnly
            | QFileDialog::Option::DontResolveSymlinks
        )
    );
    if (!dir_path.isEmpty())
        this->load_from_file_or_dir(dir_path);
}

//@property
QStringList MainWindow::image_list() const {
    QStringList lst;
    for (auto i = 0; i < this->docks_.file_list_->count(); ++i) {
        auto *const item = this->docks_.file_list_->item(i);
        //assert item
        lst.append(item->text());
    }
    return lst;
}

void MainWindow::import_dropped_image_files(const QStringList &image_files) {
    QStringList extensions;
    for (const auto fmt : QImageReader::supportedImageFormats() | std::views::transform([](auto &v){ return v.toLower(); })) {
        extensions.push_back(fmt);
    }
    const auto already_loaded = this->image_list();
    QStringList new_files;
    for (const auto file : image_files | std::views::transform([](auto &v){ return v.toLower(); })) {
        if (already_loaded.contains(file) && std::ranges::any_of(extensions, [&](auto &e) { return file.endsWith(e); }))
            new_files.push_back(file);
    }

    this->image_path_.clear();
    for (const auto &path : new_files) {
        this->docks_.file_list_->addItem(
            make_image_list_item(path, this->output_dir_)
        );
    }

    if (this->image_list().count() > 1) {
        this->actions_.open_next_img_->setEnabled(true);
        this->actions_.open_prev_img_->setEnabled(true);
    }

    this->open_next_image();
}

void MainWindow::import_images_from_dir(
    const QString &root_dir, const QString &pattern
) {
    this->actions_.open_next_img_->setEnabled(true);
    this->actions_.open_prev_img_->setEnabled(true);

    if (!this->can_continue() || root_dir.isEmpty()) {
        return;
    }
    this->docks_.file_dock_->setEnabled(true);
    this->docks_.file_dock_->setToolTip("");

    AppConfig::instance().last_work_dir_ = root_dir.toStdString();
    this->prev_opened_dir_ = root_dir;
    this->image_path_.clear();
    this->docks_.file_list_->clear();

    auto image_paths = scan_image_files(root_dir);
    QRegularExpression re(pattern);
    if (!pattern.isEmpty() && re.isValid()) {
        QStringList filtered;
        std::ranges::for_each(image_paths, [&filtered, re](auto &f) {
            if (const auto match = re.match(f); match.hasMatch()) { filtered.append(f); }
        } );
        image_paths = filtered;
    }
    for (const QString &image_path : image_paths) {
        auto *const item = new QListWidgetItem(image_path);
        //item->setIcon(QIcon(QPixmap(filename).scaled(128, 128)));
        //item->setSizeHint(QSize(128, 128));
        //item->setToolTip(filename);
        item->setFlags(Qt::ItemFlag::ItemIsEnabled | Qt::ItemFlag::ItemIsSelectable);
        if (QFile::exists(
            resolve_label_path(
                image_path, this->output_dir_
            )
        )) {
            item->setCheckState(Qt::CheckState::Checked);
        } else {
            item->setCheckState(Qt::CheckState::Unchecked);
        }
        this->docks_.file_list_->addItem(item);
    }
}

void MainWindow::update_status_stats(const QPointF &mouse_pos) {
    QStringList stats;
    stats.append(QString("mode=%1").arg(ModeName(canvas_widgets_.canvas_->mode_)));
    stats.append(QString("x=%1, y=%2").arg(mouse_pos.x(), 0, 'f', 1).arg(mouse_pos.y(), 0, 'f', 1));
    this->status_bar_.stats_->setText(stats.join(" | "));
}

QList<TlShape> MainWindow::shapes_from_dicts(
    const QList<ShapeDict> &shape_dicts,
    const QMap<QString, QList<QString>> &label_flags
) {
    QList<TlShape> shapes;
    for (const auto &shape_dict : shape_dicts) {
        TlShape shape(
            shape_dict.label,
            {},
            shape_dict.shape_type,
            {},
            shape_dict.group_id,
            shape_dict.description,
            shape_dict.mask,
            shape_dict.points,
            true
        );

        //default_flags: dict[str, bool] = {};
        //if label_flags:
        //    for pattern, keys in label_flags.items():
        //        if not isinstance(shape.label, str):
        //            logger.warning("shape.label is not str: {}", shape.label);
        //            continue;
        //        if re.match(pattern, shape.label):
        //            for key in keys:
        //                default_flags[key] = False;
        //shape.flags = default_flags;
        //shape.flags.update(shape_dict["flags"]);
        //shape.other_data = shape_dict["other_data"];
        //
        shapes.append(shape);
    }
    return shapes;
}

QString MainWindow::resolve_text_annotation_shape_type(
    const QString &create_mode, const QString &ai_output_format
) {
    if (AI_CREATE_MODES.contains(create_mode)) {
        return ai_output_format;
    }
    if (TextToAnnotationCreateMode.contains(create_mode)) {
        return create_mode;
    }
    return "";
}

std::vector<int32_t> MainWindow::rgb_from_colormap_id(int32_t label_id) {
    int32_t r, g, b;
    LABEL_COLORMAP[label_id % LABEL_COLORMAP.size()].getRgb(&r, &g, &b);
    return { r, g, b };
}

std::vector<int32_t> MainWindow::rgb_from_label_colors(
    const std::string &label, const std::map<std::string, std::vector<int32_t>> &label_colors
) {
    if (!label_colors.contains(label))
        return {};
    const auto &rgb = label_colors.at(label);
    if (rgb.size() != 3 || !std::ranges::all_of(rgb, [](auto c) { return 0 <= c && c <= 255; }))
        throw std::runtime_error("Color for label must be 0-255 RGB tuple, but got: ");
    return rgb;
}

bool MainWindow::is_valid_label(
    const QString &label, const QStringList &existing_labels, const QString &policy
) {
    if (policy.isEmpty()) {
        return true;
    }
    if (policy == "exact") {
        return existing_labels.contains(label);
    }
    return false;
}

QString MainWindow::format_window_title(
    const QString &image_path,
    int32_t file_index,
    int32_t file_count,
    const QImage &image,
    bool dirty
) {
    QString title = _appname_;
    if (!image_path.isEmpty()) {
        title = QString("%1 - %2").arg(title, image_path);
        if (file_count > 0 && file_index != None)
            title = QString("%1 [%2/%3]").arg(title).arg(file_index + 1).arg(file_count);
    }
    if (!image.isNull())
        title = QString("%1 | %2×%3").arg(title).arg(image.width()).arg(image.height());
    if (dirty)
        title = title + "*";
    return title;
}

QString MainWindow::resolve_label_path(const QString &image_or_label_path, const QString &output_dir) {
    if (is_label_file_path(image_or_label_path))
        return image_or_label_path;
    const QFileInfo image_path(image_or_label_path);
    return (output_dir.isEmpty() ? image_path.absolutePath() : output_dir)
        + "/" + image_path.baseName() + LabelFile::suffix;
}

QListWidgetItem *MainWindow::make_image_list_item(
    const QString &image_path, const QString &output_dir
) {
    auto *item = new QListWidgetItem(image_path);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    const QString label_path = resolve_label_path(
        image_path, output_dir
    );
    const bool has_label = QFile::exists(label_path);
    item->setCheckState(has_label ? Qt::Checked : Qt::Unchecked);
    return item;
}

ShapeDict MainWindow::shape_to_dict(const TlShape &shape) {
    //assert shape.label is not None
    return ShapeDict(
        shape.label_,
        shape.points_,
        shape.shape_type_,
        shape.flags_,
        shape.description_,
        shape.group_id_,
        shape.mask_,
        shape.other_data_
    );
}

QStringList MainWindow::scan_image_files(const QString &root_dir) {
    QStringList extensions;
    for (const auto fmt : QImageReader::supportedImageFormats() | std::views::transform([](auto &v){ return QString(v.toLower());})) {
        extensions.append(QString("*.%1").arg(fmt));
    }

    QStringList images;
    QDirIterator iterator(root_dir, extensions, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        const QFileInfo &fileInfo = iterator.fileInfo();
        if (fileInfo.isFile()) {
            images.append(fileInfo.absoluteFilePath());
        }
    }

    SPDLOG_DEBUG("found {} images in {}", images.size(), root_dir);
    return natsort::os_sorted(images);
    //try:
    //    return natsort.os_sorted(images)
    //except OSError:
    //    logger.warning(
    //        "natsort.os_sorted failed (known macOS strxfrm bug), "
    //        "falling back to locale-unaware natural sort"
    //    )
    return natsort::natsorted(images);
}


//
// User-assisted function.
//
MainWindow::~MainWindow() {
    delete ui_;
    SamApis::instance().unregister_all("");
    AppConfig::instance().save();
    spdlog::shutdown();
}

QListWidgetItem *MainWindow::current_item() {
    const auto items = docks_.label_list_->selectedItems();
    if (!items.empty()) {
        return items[0];
    }
    return nullptr;
}

void MainWindow::duplicateSelectedShape() {
    copySelectedShape();
    insert_shapes(this->copied_shapes_);
}

void MainWindow::copySelectedShape() {
    this->copied_shapes_.clear();
    std::ranges::for_each(this->canvas_widgets_.canvas_->selected_shapes_, [&](auto &s){
        this->copied_shapes_.push_back(this->canvas_widgets_.canvas_->shapes_[s]);}
    );
    this->actions_.paste_->setEnabled(!this->copied_shapes_.empty());
}

void MainWindow::enableKeepPrevScale(bool enabled) {
    config_["keep_prev_scale"] = enabled;
    actions_.keep_prev_zoom_->setChecked(enabled);
}

void MainWindow::load_shape_dicts(const QList<ShapeDict> &shape_dicts) {
    QList<TlShape> shapes;
    for (auto &shape_dict : shape_dicts) {
        TlShape shape;
        shape.label_ = shape_dict.label;
        shape.shape_type_ = shape_dict.shape_type;
        shape.group_id_ = shape_dict.group_id;
        shape.description_ = shape_dict.description;
        shape.mask_ = shape_dict.mask;

        for (auto &p : shape_dict.points) {
            shape.addPoint(QPointF(p.x(), p.y()));
        }
        shape.close();

        QMap<QString, bool> default_flags = {};
        //if this->config_["label_flags"]:
        //    for pattern, keys in this->config_["label_flags"].items():
        //        if not isinstance(shape.label, str):
        //            logger.warning("shape.label is not str: {}", shape.label)
        //            continue
        //        if re.match(pattern, shape.label):
        //            for key in keys:
        //                default_flags[key] = False
        shape.flags_ = default_flags;
        //shape.flags.update(shape_dict["flags"])
        shape.other_data_ = shape_dict.other_data;

        shapes.append(shape);
    }
    load_shapes(shapes);
}

void MainWindow::update_shape_color(TlShape &shape) {
    //assert shape.label is not None
    const auto v = get_rgb_by_label(
        shape.label_, this->docks_.label_list_
    );
    shape.line_color_ = QColor(v[0], v[1], v[2]);
    shape.vertex_fill_color_ = QColor(v[0], v[1], v[2]);
    shape.hvertex_fill_color_ = QColor(255, 255, 255);
    shape.fill_color_ = QColor(v[0], v[1], v[2], 128);
    shape.select_line_color_ = QColor(255, 255, 255);
    shape.select_fill_color_ = QColor(v[0], v[1], v[2], 155);
}

void MainWindow::slotTaskSubmit() {
    // 非GUI线程创建和操作QProgressDialog违反QT的GUI线程规则.
    // 所有GUI操作(包括QWidget及其子类的创建, 显示, 更新)必须在主线程执行.
    progress_dialog_ = new QProgressDialog("AI解码中, 请稍候...", "Cancel", 0, 0, this, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    progress_dialog_->setWindowModality(Qt::WindowModal);  // 确保用户无法操作其他窗口
    progress_dialog_->setCancelButton(nullptr);    // 隐藏取消按钮
    progress_dialog_->setMinimumDuration(100);     // 延迟100ms显示, 避免闪屏
    progress_dialog_->show();                      // 启动对话框展示
}

void MainWindow::slotTaskFinish() {
    if (progress_dialog_ != nullptr) {
        progress_dialog_->close();     // 关闭销毁
        delete progress_dialog_;
        progress_dialog_ = nullptr;
    }
}

void MainWindow::slotActionSetup() {
    train_widget_->show();
}

#include <QProcess>
void MainWindow::slotActionTrain() {
    sub_process_ = new QProcess();        //使用进程运行子进程窗口
    QObject::connect(sub_process_, &QProcess::readyReadStandardOutput, this, &MainWindow::slotReadyReadStandardOutput);
    QObject::connect(sub_process_, &QProcess::readyReadStandardError, this, &MainWindow::slotReadyReadStandardError);
    QObject::connect(sub_process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, qOverload<int , QProcess::ExitStatus>(&MainWindow::slotFinishedProcess));
    //connect(m_pProcessVM, &QProcess::readyRead, this, &CncOpWindows::slotReadProcessVM);

    QObject::connect(sub_process_, &QProcess::stateChanged, [=](QProcess::ProcessState state) {
        if (state == QProcess::ProcessState::NotRunning) {
        }
    });
    QObject::connect(sub_process_, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(slotProcessExited()));
    QObject::connect(qApp, SIGNAL(aboutToQuit()), sub_process_, SLOT(terminate()));
    QObject::connect(sub_process_, SIGNAL(error(QProcess::ProcessError)), this, SLOT(slotError(QProcess::ProcessError)));

    QObject::connect(sub_process_,&QProcess::started, [=]() {//启动完成
        std::cerr << "进程已启动" << std::endl;
    });
    QObject::connect(sub_process_,&QProcess::stateChanged, [=]() {//进程状态改变
        if (sub_process_->state()==QProcess::Running) {
            std::cerr << "正在运行" << std::endl;
        } else if(sub_process_->state()==QProcess::NotRunning) {
            std::cerr << "不在运行" << std::endl;
        } else {
            std::cerr << "正在启动" << std::endl;
        }
    });
    QObject::connect(sub_process_,&QProcess::errorOccurred, [=]() {
        std::cerr << sub_process_->errorString().toStdString(); //输出错误信息
    });

    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (auto &item : env.toStringList()) {
            std::cerr << "env<===" << item.toStdString() << std::endl;
        }
    }

    // 设置启动所需环境(效率比setEnvironment较高)
    QProcessEnvironment env;
    env.insert("PATH", "D:/PYTHON_HOME/Python312");
    env.insert("PYTHONPATH", "D:/PYTHON_HOME/yolo-packages");
    env.insert("YOLO_CONFIG_DIR", "datasets/data_lidian_cls");
    env.insert("HOMEPATH", "C:/Users/njtl007");
    //qputenv("PATH", "D:/PYTHON_HOME/Python312");
    //qputenv("PYTHONPATH", "D:/PYTHON_HOME/yolo-packages");
    sub_process_->setProcessEnvironment(env);

    sub_process_->setWorkingDirectory("D:/PYTHON_HOME/ultralytics-Yolo11");

    // 启动时设置的环境变量未生效, 需要设置目标程序绝对路径.
    // C:/WORK/TlAssistant/PYTHON_HOME/Ultralytics-Yolo11/train_cls.py
    QStringList arguments{"train_cls.py", "--config=datasets/data_lidian_cls/args.yaml"};
    //QStringList arguments{"--version"};
    sub_process_->start("D:/PYTHON_HOME/Python312/python.exe", arguments);

    sub_process_->waitForStarted(2000);
}

void MainWindow::slotActionInfer() {
}

void MainWindow::slotReadyReadStandardOutput() {
    std::cerr << "<==ReadStandardOut " << sub_process_->readAllStandardOutput().data();
}

void MainWindow::slotReadyReadStandardError() {
    std::cerr << "<==ReadStandardErr " << sub_process_->readAllStandardError().data();
}

void MainWindow::slotFinishedProcess(int32_t exitCode, QProcess::ExitStatus exitStatus) {
    std::cerr << "<==finishedProcess " << exitCode;
}

void MainWindow::slotProcessExited(int32_t exitCode, QProcess::ExitStatus exitStatus) {
    std::cerr << "<==processExited " << exitCode;
}

void MainWindow::slotError(QProcess::ProcessError) {

}