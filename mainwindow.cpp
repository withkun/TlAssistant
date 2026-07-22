#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <random>
#include <ranges>

#include <QFileDialog>
#include <QListWidget>
#include <QLabel>
#include <QLineEdit>
#include <QFormLayout>
#include <QDockWidget>
#include <QAction>
#include <QMenuBar>
#include <QScrollBar>
#include <QStatusBar>
#include <QToolButton>
#include <QMouseEvent>
#include <QWidgetAction>
#include <QMessageBox>
#include <QMimeData>
#include <QImageReader>
#include <QDirIterator>
#include <QTimer>
#include <QApplication>

#include "config/app_config.h"
#include "config/tl_yaml_config.h"
#include "tl_widgets/tl_utils.h"
#include "tl_widgets/tl_tool_bar.h"
#include "tl_widgets/tl_file_dialog.h"
#include "tl_widgets/tl_brightness.h"
#include "tl_widgets/status_stats.h"
#include "tl_modules/sam_apis.h"
#include "tl_modules/bbox_from_text.h"
#include "tl_modules/polygon_from_mask.h"


std::vector<QColor> label_colormap() {
    std::vector<QColor> colormap(256);
    for (int i = 0; i < 256; ++i) {
        // 提取标签i的8个二进制位
        const uint8_t b0 = (i >> 0) & 1;
        const uint8_t b1 = (i >> 1) & 1;
        const uint8_t b2 = (i >> 2) & 1;
        const uint8_t b3 = (i >> 3) & 1;
        const uint8_t b4 = (i >> 4) & 1;
        const uint8_t b5 = (i >> 5) & 1;
        const uint8_t b6 = (i >> 6) & 1;
        const uint8_t b7 = (i >> 7) & 1;
        // 合成RGB通道色彩值.
        const uint8_t r = (b0 << 7) | (b3 << 6) | (b6 << 5);
        const uint8_t g = (b1 << 7) | (b4 << 6) | (b7 << 5);
        const uint8_t b = (b2 << 7) | (b5 << 6);
        colormap[i] = QColor(r, g, b);
    }
    return colormap;
}
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
    : QMainWindow(), ui_(new Ui::MainWindow), settings_("tl_assistant", "tl_assistant") {
    ui_->setupUi(this);
    this->setWindowTitle(tr("tl assistant"));

    AppConfig &appConfig = AppConfig::instance();
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

    this->label_dialog_ = new LabelDialog(
        this,
        YAML_VSTR(config_["labels"]),
        config_["sort_labels"].as<bool>(),
        config_["show_label_text_field"].as<bool>(),
        YAML_QSTR(config_["label_completion"]),
        YAML_QMAP(config_["fit_to_content"]),
        YAML_QMAP(config_["label_flags"])
    );

    this->prev_opened_dir_ = QString::fromStdString(appConfig.last_work_dir_);
    this->setup_dock_widgets();

    this->setAcceptDrops(true);
    this->setup_canvas();

    this->setup_actions();
    this->scalers_ = {
        { ZoomMode::FIT_WINDOW, [this] { return scaleFitWindow(); } },
        { ZoomMode::FIT_WIDTH, [this] { return scaleFitWidth(); } },
        { ZoomMode::MANUAL_ZOOM, [this] { return 1.; } }
    };
    this->setup_menus();

    ai_assist_annotation_widget_ = new AiAssistAnnotation(
        QString::fromStdString(appConfig.ai_assist_name_),
        [this](const std::string &name){ this->canvas_->set_ai_model_name(name); },
        [this](const std::string &name){ this->canvas_->set_ai_output_format(name); },
        this
    );
    ai_assist_annotation_widget_->setEnabled(false);

    ai_text_to_annotation_widget_ = new AiTextToAnnotation(
        appConfig.ai_prompt_name_, [this] { this->submit_ai_prompt(); }, this
    );
    ai_text_to_annotation_widget_->setEnabled(false);

    this->setup_toolbars();

    this->setup_status_bar();

    this->setup_app_state(file_or_dir, output_dir);

    QObject::connect(zoom_widget_, &ZoomWidget::valueChanged, this, &MainWindow::paint_canvas);

    this->populateModeActions();
}

void MainWindow::retheme() {
    // Two things do not follow Qt's palette swap: cached QIcon pixmaps (keyed
    // by the old tint color) and stylesheet'd widgets (QStyleSheetStyle pins
    // their palette at polish time, leaving a palette(...) toolbar on the old
    // scheme).
    //QtGui::QPixmapCache::clear();
    //auto app = QtWidgets::QApplication::instance();
    //if not isinstance(app, QtWidgets.QApplication):
    //    return
    //for (auto &widget : app.allWidgets()) {
    //    //sheet = widget.styleSheet();
    //    //# Only stylesheets with palette(...) references go stale on a scheme
    //    //# change; re-applying just those avoids re-polishing composite widgets
    //    //# (combo boxes, spin boxes) whose re-polish can invalidate siblings.
    //    //if sheet and "palette(" in sheet:
    //    //    widget.setStyleSheet(sheet)  # re-resolve palette refs; also repaints
    //    //else:
    //    //    widget.update();
    //}
    // The AI-button highlight bakes palette colors into its stylesheet (no
    // palette() ref), so recompute it against the new palette.
    //highlight_ai_buttons(ai_buttons_highlighted_);
}

void MainWindow::setup_actions() {
    const auto action = [this](const QString &text, auto slot, const QList<QString> &shortcut={}, const QString &file="", const QString &tip="", bool checkable=false, bool enabled=true, bool checked=false) {
        auto *a = utils::newAction(text, shortcut, file, tip, checkable, enabled, checked);
        QObject::connect(a, &QAction::triggered, this, slot);
        return a;
    };
    const auto shortcuts = [this](const std::string &key) { return YAML_KEYS(config_["shortcuts"][key]); };

    about_ = action(
        tr("&About"),
        &MainWindow::about, {}, ":/icons/question.svg",
        tr("Show about page"), false, true, false
    );
    save_ = action(
        tr("&Save\n"),
        &MainWindow::save_label_file, shortcuts("save"), ":/icons/floppy-disk.svg",
        tr("Save labels to file"), false, false, false
    );
    save_as_ = action(
        tr("&Save As"),
        [this](){ save_label_file(true); }, shortcuts("save_as"), ":/icons/floppy-disk.svg",
        tr("Save labels to a different file"), false, false, false
    );
    save_auto_ = action(
        tr("Save &Automatically"),
        [this](auto x) { save_auto_->setChecked(x); }, {}, ":/icons/save1.svg",
        tr("Save automatically"), true, true, false
    );
    save_auto_->setChecked(config_["auto_save"].as<bool>());
    save_with_image_data_ = action(
        tr("Save With Image Data"),
        &MainWindow::enableSaveImageWithData, {}, ":/icons/icon-256.png",
        tr("Save image data in label file"), true, true, false
    );
    change_output_dir_ = action(
        tr("&Change Output Dir"),
        &MainWindow::changeOutputDirDialog, shortcuts("save_to"), ":/icons/folders.svg",
        tr("Change where annotations are loaded/saved"), false, true, false
    );
    open_ = action(
        tr("&Open\n"),
        &MainWindow::open_file_with_dialog, shortcuts("open"), ":/icons/folder-open.svg",
        tr("Open image or label file"), false, true, false
    );
    open_dir_ = action(
        tr("Open Dir"),
        [this] { open_dir_with_dialog(); }, shortcuts("open_dir"), ":/icons/folder-open.svg",
        tr("Open Dir"), false, true, false
    );
    close_ = action(
        tr("&Close"),
        &MainWindow::closeFile, shortcuts("close"), ":/icons/x-circle.svg",
        tr("Close current file"), false, true, false
    );
    delete_file_ = action(
        tr("&Delete File"),
        &MainWindow::deleteFile, shortcuts("delete_file"), ":/icons/file-x.svg",
        tr("Delete current label file"), false, false, false
    );
    toggle_keep_prev_mode_ = action(
        tr("Keep Previous Annotation"),
        &MainWindow::toggleKeepPrevMode, shortcuts("toggle_keep_prev_mode"), ":/icons/icon-256.png",
        tr("Toggle \"keep previous annotation\" mode"), true, true, false
    );
    toggle_keep_prev_mode_->setChecked(config_["keep_prev"].as<bool>());
    toggle_keep_prev_brightness_contrast_ = action(
        tr("Keep Previous Brightness/Contrast"),
        [this] { config_["keep_prev_brightness_contrast"] = !config_["keep_prev_brightness_contrast"].as<bool>(); }, {}, ":/icons/question.svg",
        "", true, true, config_["keep_prev_brightness_contrast"].as<bool>()
    );
    delete_ = action(
        tr("Delete Shapes"),
        &MainWindow::deleteSelectedShape, shortcuts("delete_shape"), ":/icons/trash.svg",
        tr("Delete the selected shapes"), false, false, false
    );
    edit_ = action(
        tr("&Edit Label"),
        &MainWindow::edit_label, shortcuts("edit_label"), ":/icons/note-pencil.svg",
        tr("Modify the label of the selected shape"), false, false, false
    );
    duplicate_ = action(
        tr("Duplicate Shapes"),
        &MainWindow::duplicateSelectedShape, shortcuts("duplicate_shape"), ":/icons/copy.svg",
        tr("Create a duplicate of the selected shapes"), false, false, false
    );
    copy_ = action(
        tr("Copy Shapes"),
        &MainWindow::copySelectedShape, shortcuts("copy_shape"), ":/icons/copy_clipboard.svg",
        tr("Copy selected shapes to clipboard"), false, false, false
    );
    paste_ = action(
        tr("Paste Shapes"),
        &MainWindow::pasteSelectedShape, shortcuts("paste_shape"), ":/icons/paste.svg",
        tr("Paste copied shapes"), false, false, false
    );
    undo_last_point_ = action(
        tr("Undo last point"),
        [this] { canvas_->undoLastPoint(); }, shortcuts("undo_last_point"), ":/icons/arrow-u-up-left.svg",
        tr("Undo last drawn point"), false, false, false
    );
    undo_ = action(
        tr("Undo\n"),
        &MainWindow::undoShapeEdit, shortcuts("undo"), ":/icons/arrow-u-up-left.svg",
        tr("Undo last add and edit of shape"), false, false, false
    );
    remove_point_ = action(
        tr("Remove Selected Point"),
        &MainWindow::removeSelectedPoint, shortcuts("remove_selected_point"), ":/icons/trash.svg",
        tr("Remove selected point from polygon"), false, false, false
    );
    add_point_to_edge_ = action(
        tr("Add Point to Edge"),
        [this] { canvas_->addPointToEdge(); }, {}, "",
        tr("Insert a new point at the hovered polygon edge"), false, false, false
    );
    create_mode_ = action(
        tr("Polygon"),
        [this] { this->switch_canvas_mode(false, "polygon"); }, shortcuts("create_polygon"), ":/icons/polygon.svg",
        tr("Start drawing polygons"), false, false, false
    );
    edit_mode_ = action(
        tr("Edit Shapes"),
        [this] { this->switch_canvas_mode(true); }, shortcuts("edit_shape"), ":/icons/note-pencil.svg",
        tr("Move and edit the selected shapes"), false, false, false
    );
    create_rectangle_mode_ = action(
        tr("Rectangle"),
        [this] { this->switch_canvas_mode(false, "rectangle"); }, shortcuts("create_rectangle"), ":/icons/rectangle.svg",
        tr("Start drawing rectangles"), false, false, false
    );
    create_oriented_rectangle_mode_ = action(
        tr("Oriented Rectangle"),
        [this] { this->switch_canvas_mode(false, "oriented_rectangle"); }, shortcuts("create_oriented_rectangle"),
        ":/icons/oriented_rectangle.svg",
        tr("Start drawing oriented rectangles"), false, false, false
    );
    create_circle_mode_ = action(
        tr("Circle"),
        [this] { this->switch_canvas_mode(false, "circle"); }, shortcuts("create_circle"), ":/icons/circle.svg",
        tr("Start drawing circles"), false, false, false
    );
    create_line_mode_ = action(
        tr("Line"),
        [this] { this->switch_canvas_mode(false, "line"); }, shortcuts("create_line"), ":/icons/line-segment.svg",
        tr("Start drawing lines"), false, false, false
    );
    create_point_mode_ = action(
        tr("Point"),
        [this] { this->switch_canvas_mode(false, "point"); }, shortcuts("create_point"), ":/icons/circles-four.svg",
        tr("Start drawing points"), false, false, false
    );
    create_line_strip_mode_ = action(
        tr("LineStrip"),
        [this] { this->switch_canvas_mode(false, "linestrip"); }, shortcuts("create_linestrip"), ":/icons/line-segments.svg",
        tr("Start drawing linestrip. Ctrl+LeftClick ends creation."), false, false, false
    );
    create_ai_points_to_shape_mode_ = action(
        tr("AI-Points"),
        [this] { this->switch_canvas_mode(false, "ai_points_to_shape"); }, shortcuts("create_ai_polygon"), ":/icons/ai-polygon.svg",
        tr("Click points to segment object. Ctrl+LeftClick ends creation."), false, false, false
    );
    create_ai_box_to_shape_mode_ = action(
        tr("AI-Box"),
        [this] { this->switch_canvas_mode(false, "ai_box_to_shape"); }, shortcuts("create_ai_mask"), ":/icons/ai-mask.svg",
        tr("Draw a bounding box to segment object."), false, false, false
    );
    open_next_img_ = action(
        tr("&Next Image"),
        &MainWindow::open_next_image, shortcuts("open_next"), ":/icons/arrow-fat-right.svg",
        tr("Open next (hold Ctrl+Shift to copy labels)"), false, false, false
    );
    open_prev_img_ = action(
        tr("&Prev Image"),
        &MainWindow::open_prev_image, shortcuts("open_prev"), ":/icons/arrow-fat-left.svg",
        tr("Open prev (hold Ctrl+Shift to copy labels)"), false, false, false
    );
    keep_prev_scale_ = action(
        tr("&Keep Previous Scale"),
        &MainWindow::enableKeepPrevScale, {}, ":/icons/icon-256.png",
        tr("Keep previous zoom scale"), true, true, false
    );
    fit_window_ = action(
        tr("&Fit Window"),
        &MainWindow::setFitWindow, shortcuts("fit_window"), ":/icons/frame-corners.svg",
        tr("Zoom follows window size"), true, false, false
    );
    fit_width_ = action(
        tr("Fit &Width"),
        &MainWindow::setFitWidth, shortcuts("fit_width"), ":/icons/frame-arrows-horizontal.svg",
        tr("Zoom follows window width"), true, false, false
    );
    brightness_contrast_ = action(
        tr("&Brightness Contrast"),
        [this] { brightnessContrast(); }, {}, ":/icons/brightness-contrast.svg",
        tr("Adjust brightness and contrast"), false, false, false
    );
    zoom_in_ = action(
        tr("Zoom &In"),
        [this] { add_zoom(1.1); }, shortcuts("zoom_in"), ":/icons/magnifying-glass-minus.svg",
        tr("Increase zoom level"), false, false, false
    );
    zoom_out_ = action(
        tr("&Zoom Out"),
        [this] { add_zoom(0.9); }, shortcuts("zoom_out"), ":/icons/magnifying-glass-plus.svg",
        tr("Decrease zoom level"), false, false, false
    );
    zoom_org_ = action(
        tr("&Original size"),
        &MainWindow::set_zoom_to_original, shortcuts("zoom_to_original"), ":/icons/image-square.svg",
        tr("Zoom to original size"), false, false, false
    );
    reset_layout_ = action(
        tr("Reset Layout"),
        &MainWindow::reset_layout, {}, ":/icons/layout-duotone.svg",
        "", false, false, false
    );
    fill_drawing_ = action(
        tr("Fill Drawing Polygon"),
        [this] { canvas_->setFillDrawing(fill_drawing_->isChecked()); }, {}, ":/icons/paint-bucket.svg",
        tr("Fill polygon while drawing"), true, true, false
    );
    if (config_["canvas"]["fill_drawing"].as<bool>()) {
        fill_drawing_->trigger();
    }
    hide_all_ = action(
        tr("&Hide\nShapes"),
        [this] { toggleShapes(false); }, shortcuts("hide_all_shapes"), ":/icons/eye.svg",
        tr("Hide all shapes"), false, false, false
    );
    show_all_ = action(
        tr("&Show\nShapes"),
        [this] { toggleShapes(true); }, shortcuts("show_all_shapes"), ":/icons/eye.svg",
        tr("Show all shapes"), false, false, false
    );
    toggle_all_ = action(
        tr("&Toggle\nShapes"),
        [this] { toggleShapes(None); }, shortcuts("toggle_all_shapes"), ":/icons/eye.svg",
        tr("Toggle all shapes"), false, false, false
    );

    zoom_action_ = new QWidgetAction(this);
    const auto zoom_box_layout = new QVBoxLayout();
    const auto zoom_label = new QLabel(tr("Zoom"));
    zoom_label->setAlignment(Qt::AlignCenter);
    zoom_box_layout->addWidget(zoom_label);
    zoom_box_layout->addWidget(zoom_widget_);
    zoom_action_->setDefaultWidget(new QWidget());
    zoom_action_->defaultWidget()->setLayout(zoom_box_layout);
    zoom_widget_->setWhatsThis(
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
    zoom_widget_->setEnabled(false);

    this->zoom_mode_ = ZoomMode::FIT_WINDOW;
    fit_window_->setChecked(true);

    QObject::connect(canvas_, &Canvas::vertexSelected, [this](bool value){ remove_point_->setEnabled(value); });
    QObject::connect(canvas_, &Canvas::aiAssistSubmit, this, &MainWindow::slotTaskSubmit);
    QObject::connect(canvas_, &Canvas::aiAssistFinish, this, &MainWindow::slotTaskFinish);

    actions_draw_ = {
        {"polygon",            create_mode_},
        {"rectangle",          create_rectangle_mode_},
        {"oriented_rectangle", create_oriented_rectangle_mode_},
        {"circle",             create_circle_mode_},
        {"point",              create_point_mode_},
        {"line",               create_line_mode_},
        {"linestrip",          create_line_strip_mode_},
        {"ai_points_to_shape", create_ai_points_to_shape_mode_},
        {"ai_box_to_shape",    create_ai_box_to_shape_mode_},
    };

    actions_zoom_ = {
        //zoom_widget_,
        zoom_in_,
        zoom_out_,
        zoom_org_,
        fit_window_,
        fit_width_
    };
    actions_on_load_active_ = {
        close_,
        create_mode_,
        create_rectangle_mode_,
        create_oriented_rectangle_mode_,
        create_circle_mode_,
        create_line_mode_,
        create_point_mode_,
        create_line_strip_mode_,
        create_ai_points_to_shape_mode_,
        create_ai_box_to_shape_mode_,
        brightness_contrast_,
    };
    actions_on_shapes_present_ = {save_as_, hide_all_, show_all_, toggle_all_};
    std::list<QAction *> context_menu = {
        edit_mode_,
        edit_,
        duplicate_,
        copy_,
        paste_,
        delete_,
        undo_,
        undo_last_point_,
        add_point_to_edge_,
        remove_point_,
    };
    std::ranges::for_each(actions_draw_, [this](auto &p) { actions_context_menu_.push_back(p.second); });
    std::ranges::for_each(context_menu, [this](auto &a) { actions_context_menu_.push_back(a); });
    actions_edit_menu_ = {
        edit_,
        duplicate_,
        copy_,
        paste_,
        delete_,
        nullptr,
        undo_,
        undo_last_point_,
        nullptr,
        remove_point_,
        nullptr,
        toggle_keep_prev_mode_,
    };
    //return _Actions(
    //    about=about,
    //    save=save,
    //    save_as=save_as,
    //    save_auto=save_auto,
    //    save_with_image_data=save_with_image_data,
    //    change_output_dir=change_output_dir,
    //    open=open_,
    //    close=close,
    //    delete_file=delete_file,
    //    toggle_keep_prev_mode=toggle_keep_prev_mode,
    //    toggle_keep_prev_brightness_contrast=toggle_keep_prev_brightness_contrast,
    //    delete=delete,
    //    edit=edit,
    //    duplicate=duplicate,
    //    copy=copy,
    //    paste=paste,
    //    undo_last_point=undo_last_point,
    //    undo=undo,
    //    remove_point=remove_point,
    //    add_point_to_edge=add_point_to_edge,
    //    create_mode=create_mode,
    //    edit_mode=edit_mode,
    //    create_rectangle_mode=create_rectangle_mode,
    //    create_oriented_rectangle_mode=create_oriented_rectangle_mode,
    //    create_circle_mode=create_circle_mode,
    //    create_line_mode=create_line_mode,
    //    create_point_mode=create_point_mode,
    //    create_line_strip_mode=create_line_strip_mode,
    //    create_ai_polygon_mode=create_ai_polygon_mode,
    //    create_ai_mask_mode=create_ai_mask_mode,
    //    open_next_img=open_next_img,
    //    open_prev_img=open_prev_img,
    //    keep_prev_scale=keep_prev_scale,
    //    fit_window=fit_window,
    //    fit_width=fit_width,
    //    brightness_contrast=brightness_contrast,
    //    zoom_in=zoom_in,
    //    zoom_out=zoom_out,
    //    zoom_org=zoom_org,
    //    reset_layout=reset_layout,
    //    fill_drawing=fill_drawing,
    //    hide_all=hide_all,
    //    show_all=show_all,
    //    toggle_all=toggle_all,
    //    open_dir=open_dir,
    //    zoom_widget_action=zoom_widget_action,
    //    draw=draw,
    //    zoom=zoom,
    //    on_load_active=on_load_active,
    //    on_shapes_present=on_shapes_present,
    //    context_menu=context_menu,
    //    edit_menu=edit_menu,
    //)
}

void MainWindow::setup_menus() {
    const auto action = [this](const QString &text, auto slot, const QList<QString> &shortcut={}, const QString &file="", const QString &tip="", bool checkable=false, bool enabled=true, bool checked=false) {
        auto *a = utils::newAction(text, shortcut, file, tip, checkable, enabled, checked);
        QObject::connect(a, &QAction::triggered, this, slot);
        return a;
    };
    const auto shortcuts = [this](const std::string &key) { return YAML_KEYS(config_["shortcuts"][key]); };

    quit_ = action(
        tr("&Quit"),
        &MainWindow::close, shortcuts("quit"), ":/icons/quit.png",
        tr("Quit application"), false, true, false
    );
    open_config_ = action(
        tr("Preferences…"),
        &MainWindow::open_config_file, {"Ctrl+Shift+,"}, ":/icons/icon-256.png",
        tr("Open config file in text editor"), false, true, false
    );
    open_config_->setMenuRole(QAction::PreferencesRole);
    help_ = action(
        tr("&Tutorial"),
        &MainWindow::tutorial, {}, ":/icons/question.svg",
        tr("Show tutorial page"), false, true, false
    );

    file_menu_ = menu(tr("&File"));
    edit_menu_ = menu(tr("&Edit"));
    view_menu_ = menu(tr("&View"));
    help_menu_ = menu(tr("&Help"));

    label_menu_ = new QMenu();
    utils::addActions(label_menu_, {edit_, delete_});
    shape_list_->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(shape_list_, &ShapeListView::customContextMenuRequested, this, &MainWindow::popLabelListMenu);

    utils::addActions(
        file_menu_,
        {
            open_,
            open_next_img_,
            open_prev_img_,
            open_dir_,
            save_,
            save_as_,
            save_auto_,
            change_output_dir_,
            save_with_image_data_,
            close_,
            delete_file_,
            nullptr,
            open_config_,
            nullptr,
            quit_
        }
    );
    utils::addActions(help_menu_, {help_, about_});
    utils::addActions(
        view_menu_,
        {
            flags_dock_->toggleViewAction(),
            label_dock_->toggleViewAction(),
            shape_dock_->toggleViewAction(),
            files_dock_->toggleViewAction(),
            nullptr,
            reset_layout_,
            nullptr,
            fill_drawing_,
            nullptr,
            hide_all_,
            show_all_,
            toggle_all_,
            nullptr,
            zoom_in_,
            zoom_out_,
            zoom_org_,
            keep_prev_scale_,
            nullptr,
            fit_window_,
            fit_width_,
            nullptr,
            brightness_contrast_,
            toggle_keep_prev_brightness_contrast_,
        }
    );

    utils::addActions(
        this->canvas_->menus_[0],
        this->actions_context_menu_
    );
    utils::addActions(
        canvas_->menus_[1],
        {
            action("&Copy here", [this] { this->copyShape(); }),
            action("&Move here", [this] { this->moveShape(); }),
        }
    );

    //return _Menus(
    //    file=file_menu,
    //    edit=edit_menu,
    //    view=view_menu,
    //    help=help_menu,
    //    label_list=label_menu,
    //)
}

void MainWindow::setup_toolbars() {
    select_ai_model_ = new QWidgetAction(this);
    select_ai_model_->setDefaultWidget(ai_assist_annotation_widget_);

    ai_prompt_action_ = new QWidgetAction(this);
    ai_prompt_action_->setDefaultWidget(ai_text_to_annotation_widget_);

    this->addToolBar(
        Qt::TopToolBarArea,
        new TlToolBar(
            "Tools",
            {
                open_,
                open_dir_,
                open_prev_img_,
                open_next_img_,
                save_,
                delete_file_,
                nullptr,
                edit_mode_,
                duplicate_,
                delete_,
                undo_,
                brightness_contrast_,
                nullptr,
                fit_window_,
                zoom_action_,
                nullptr,
                select_ai_model_,
                nullptr,
                ai_prompt_action_
            },
            Qt::Horizontal,
            Qt::ToolButtonTextUnderIcon,
            this->font()
        )
    );

    std::list<QAction *> draw_actions;
    std::ranges::for_each(actions_draw_, [&draw_actions](auto &item) { if (!item.first.startsWith("ai_")) { draw_actions.push_back(item.second); } });
    draw_actions.push_back(nullptr);
    std::ranges::for_each(actions_draw_, [&draw_actions](auto &item) { if (item.first.startsWith("ai_")) { draw_actions.push_back(item.second); } });
    this->addToolBar(
        Qt::LeftToolBarArea,
        new TlToolBar(
            "CreateShapeTools",
            draw_actions,
            Qt::Vertical,
            Qt::ToolButtonTextUnderIcon,
            this->font()
        )
    );

    QObject::connect(ai_assist_annotation_widget_, &AiAssistAnnotation::hover_highlight_requested, this, &MainWindow::highlight_ai_buttons);
}

void MainWindow::setup_app_state(const QString &file_or_dir, const QString &output_dir) {
    this->output_dir_ = output_dir;

    this->image_;
    this->label_file_;
    this->image_path_;
    this->prev_image_path_;
    this->other_data_ = nullptr;
    this->zoom_values_ = {};
    this->brightness_contrast_values_ = {};
    this->scroll_values_ = {
        {Qt::Horizontal, {}},
        {Qt::Vertical, {}}
    }; // key=filename, value=scroll_value

    if (!config_["file_search"].IsNull()) {
        files_search_->setText(YAML_QSTR(config_["file_search"]));
    }

    default_state_ = saveState();
    //
    // XXX: Could be completely declarative.
    // Restore application settings.
    //settings_ = QSettings("tl_assistant", "tl_assistant");
    //
    // Bump this when dock/toolbar layout changes to reset window state
    // for users upgrading from an older version.
    int32_t SETTINGS_VERSION = 1;
    if (settings_.value("settingsVersion", 0).toInt() != SETTINGS_VERSION) {
        reset_layout();
        settings_.setValue("settingsVersion", SETTINGS_VERSION);
    }
    this->resize(this->settings_.value("window/size", QSize(900, 500)).toSize());
    this->move(this->settings_.value("window/position", QPoint(0, 0)).toPoint());
    this->restoreState(this->settings_.value("window/state", QByteArray()).toByteArray());
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

void MainWindow::setup_status_bar() {
    this->message_ = new QLabel(tr("%1 started.").arg(tr("tl assistant")));
    this->stats_ = new StatusStats();
    this->statusBar()->addWidget(message_, 1);
    this->statusBar()->addWidget(stats_, 0);
    this->statusBar()->show();
    //return _StatusBarWidgets(message=message, stats=stats)
}

void MainWindow::setup_canvas() {
    zoom_widget_ = new ZoomWidget();

    canvas_ = new Canvas(
        config_["epsilon"].as<float>(),
        YAML_QSTR(config_["canvas"]["double_click"]),
        config_["canvas"]["num_backups"].as<int32_t>(),
        YAML_QMAP(config_["canvas"]["crosshair"])
    );
    QObject::connect(canvas_, &Canvas::zoomRequest, this, &MainWindow::zoom_requested);
    QObject::connect(canvas_, &Canvas::mouseMoved, this, &MainWindow::update_status_stats);
    QObject::connect(canvas_, &Canvas::statusUpdated, [this](const auto &text) {
        this->message_->setText(text); }
    );

    scroll_area_ = new QScrollArea();
    scroll_area_->setWidget(canvas_);
    scroll_area_->setWidgetResizable(true);
    scroll_bars_ = {
        { Qt::Vertical, this->scroll_area_->verticalScrollBar() },
        { Qt::Horizontal, this->scroll_area_->horizontalScrollBar() }
    };
    QObject::connect(canvas_, &Canvas::scrollRequest, this, &MainWindow::scrollRequest);

    QObject::connect(canvas_, &Canvas::newShape, this, &MainWindow::newShape);
    QObject::connect(canvas_, &Canvas::shapeMoved, this, &MainWindow::setDirty);
    QObject::connect(canvas_, &Canvas::selectionChanged, this, &MainWindow::shapeSelectionChanged);
    QObject::connect(canvas_, &Canvas::drawingPolygon, this, &MainWindow::toggleDrawingSensitive);

    this->setCentralWidget(scroll_area_);

    //return _CanvasWidgets(
    //    canvas=canvas,
    //    zoom_widget=zoom_widget,
    //    scroll_bars=scroll_bars,
    //)
}

void MainWindow::setup_dock_widgets() {
    this->flags_list_ = new QListWidget();
    this->flags_dock_ = new QDockWidget(tr("Flags"), this);
    this->flags_dock_->setObjectName("Flags");
    if (!this->config_["flags"].IsNull()) {
        this->load_flags(this->config_["flags"], this->flags_list_);
    }
    this->flags_dock_->setWidget(this->flags_list_);
    QObject::connect(flags_list_, &QListWidget::itemChanged, this, &MainWindow::setDirty);

    this->shape_list_ =  new ShapeListView();   // LabelListWidget()
    QObject::connect(shape_list_, &ShapeListView::itemSelectionChanged, this, &MainWindow::label_selection_changed);
    QObject::connect(shape_list_, &ShapeListView::itemDoubleClicked, this, &MainWindow::edit_label);
    QObject::connect(shape_list_, &ShapeListView::itemChanged, this, &MainWindow::labelItemChanged);
    QObject::connect(shape_list_, &ShapeListView::itemDropped, this, &MainWindow::labelOrderChanged);
    this->shape_dock_ = new QDockWidget(tr("Annotation List"), this);
    this->shape_dock_->setObjectName("Labels");
    this->shape_dock_->setWidget(this->shape_list_);

    this->label_list_ =  new LabelList();    // UniqueLabelQListWidget()
    this->label_list_->setToolTip(
        tr("Select label to start annotating for it. Press 'Esc' to deselect.")
    );
    if (!config_["labels"].IsNull()) {
        for (auto &lbl : YAML_KEYS(config_["labels"]))
            this->label_list_->add_label_item(
                lbl,
                get_rgb_by_label(lbl, label_list_)
            );
    }
    this->label_dock_ = new QDockWidget(tr("Label List"), this);
    this->label_dock_->setObjectName("Label List");
    this->label_dock_->setWidget(this->label_list_);

    files_search_ = new QLineEdit();
    files_search_->setPlaceholderText(tr("Search Filename"));
    QObject::connect(files_search_, &QLineEdit::textChanged, this, &MainWindow::fileSearchChanged);
    docks_files_list_ = new QListWidget();
    QObject::connect(docks_files_list_, &QListWidget::itemSelectionChanged, this, &MainWindow::file_list_item_selection_changed);
    auto *file_list_layout = new QVBoxLayout();
    file_list_layout->setContentsMargins(0, 0, 0, 0);
    file_list_layout->setSpacing(0);
    file_list_layout->addWidget(files_search_);
    file_list_layout->addWidget(docks_files_list_);
    this->files_dock_ = new QDockWidget(tr("File List"), this);
    this->files_dock_->setObjectName("Files");
    auto *file_list_container = new QWidget();
    file_list_container->setLayout(file_list_layout);
    this->files_dock_->setWidget(file_list_container);

    for (auto &[config_key, dock_widget] : std::map<std::string, QDockWidget *>{
        {"flag_dock", flags_dock_},
        {"label_dock", label_dock_},
        {"shape_dock", shape_dock_},
        {"file_dock", files_dock_}
    }) {
        auto features = QDockWidget::DockWidgetFeatures();
        if (config_[config_key]["closable"].as<bool>())
            features = features | QDockWidget::DockWidgetClosable;
        if (config_[config_key]["floatable"].as<bool>())
            features = features | QDockWidget::DockWidgetFloatable;
        if (config_[config_key]["movable"].as<bool>())
            features = features | QDockWidget::DockWidgetMovable;
        dock_widget->setFeatures(features);
        if (config_[config_key]["show"].as<bool>() == false)
            dock_widget->setVisible(false);
        this->addDockWidget(Qt::RightDockWidgetArea, dock_widget);
    }
    //return _DockWidgets(
    //    flag_dock=flag,
    //    flag_list=flag_list,
    //    shape_dock=shape,
    //    label_list=label_list,
    //    label_dock=label,
    //    unique_label_list=unique_label_list,
    //    file_dock=file,
    //    file_search=file_search,
    //    file_list=file_list,
    //)
}

QString MainWindow::load_config(
    QString config_file, const YAML::Node &config_overrides
) { // -> tuple[Path | None, dict]:
    try {
        config_ = TlConfig::load_config(
            config_file.toStdString(), config_overrides
        );
    } catch (const YAML::BadFile &e) {
        QMessageBox msg_box(this);
        msg_box.setIcon(QMessageBox::Warning);
        msg_box.setWindowTitle(this->tr("Configuration Errors"));
        msg_box.setText(
            this->tr(
                "Errors were found while loading the configuration. "
                "Please review the errors below and reload your configuration or "
                "ignore the erroneous lines."
            )
        );
        msg_box.setInformativeText(e.what());
        msg_box.setStandardButtons(QMessageBox::Ignore);
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

QMenu *MainWindow::menu(const QString &title, const std::list<QObject *> &actions) {
    auto *menu = this->menuBar()->addMenu(title);
    if (!actions.empty()) {
        utils::addActions(menu, actions);
    }
    return menu;
}
// Support Functions

bool MainWindow::noShapes() {
    return shape_list_->empty();
}

void MainWindow::populateModeActions() {
    canvas_->menus_[0]->clear();
    utils::addActions(
        this->canvas_->menus_[0], actions_context_menu_
    );
    this->edit_menu_->clear();
    std::list<QObject *> actions;
    std::ranges::transform(actions_draw_, std::back_inserter(actions), [](auto &it){ return it.second; });
    actions.push_back(edit_mode_);
    std::ranges::transform(actions_edit_menu_, std::back_inserter(actions), [](auto &it){ return it; });

    utils::addActions(this->edit_menu_, actions);
}

QString MainWindow::get_window_title(bool dirty) {
    QString window_title = tr("tl assistant");
    if (!image_path_.isEmpty()) {
        window_title = QString("%1 - %2").arg(window_title, image_path_);
        if (docks_files_list_->count() && docks_files_list_->currentItem())
            window_title = QString(
                "%1 "   // window_title
                "[%2"   // {self.fileListWidget.currentRow() + 1}
                "/%3]"  // {self.fileListWidget.count()}
            ).arg(window_title).arg(docks_files_list_->currentRow() + 1).arg(docks_files_list_->count());
    }
    if (!this->image_.isNull()) {
        window_title = QString("%1 | %2x%3").arg(window_title).arg(image_.width()).arg(image_.height());
    }
    if (dirty) {
        window_title = window_title + "*";
    }
    return window_title;
}

void MainWindow::setDirty() {
    // Even if we autosave the file, we keep the ability to undo
    this->undo_->setEnabled(this->canvas_->isShapeRestorable());

    if (config_["auto_save"].as<bool>() || save_auto_->isChecked()) {
        std::filesystem::path file_path(image_path_.toStdString());
        auto label_file = QString::fromStdString(file_path.replace_extension("json").string());
        if (!output_dir_.isEmpty()) {
            label_file = output_dir_ + "/" + QFileInfo(label_file).baseName();
        }
        this->saveLabels(label_file);
        return;
    }
    this->is_changed_ = true;
    this->save_->setEnabled(true);
    this->setWindowTitle(get_window_title(true));
}

void MainWindow::setClean() {
    this->is_changed_ = false;
    this->save_->setEnabled(false);
    for (const auto &action : this->actions_draw_ | std::views::values) {
        action->setEnabled(true);
    }
    this->setWindowTitle(get_window_title(false));

    if (this->hasLabelFile()) {
        this->delete_file_->setEnabled(true);
    } else {
        this->delete_file_->setEnabled(false);
    }
}

void MainWindow::toggleActions(bool value) {
    // Enable/Disable widgets which depend on an opened image.
    zoom_widget_->setEnabled(value);
    for (auto &z : actions_zoom_) {
        z->setEnabled(value);
    }
    for (auto &action : actions_on_load_active_) {
        action->setEnabled(value);
    }
}

//void MainWindow::queueEvent(function) {
//    QtCore.QTimer.singleShot(0, function)
//}

void MainWindow::show_status_message(const QString &message, int32_t delay) {
    this->statusBar()->showMessage(message, delay);
}

void MainWindow::submit_ai_prompt() {
    auto create_mode = canvas_->createMode();
    QString shape_type;  //: Literal["rectangle", "polygon", "mask"]
    if (AI_CREATE_MODES.contains(create_mode)) {
        shape_type = ai_assist_annotation_widget_->output_format();
    } else if (TextToAnnotationCreateMode.contains(create_mode)) {
        shape_type = create_mode;
    } else {
        SPDLOG_WARN("Unsupported createMode={}", create_mode);
        return;
    }

    const auto texts = ai_text_to_annotation_widget_->get_texts_prompt();
    if (texts.empty()) {
        return;
    }

    const auto model_name = ai_text_to_annotation_widget_->get_model_name();
    //model_type = osam.apis.get_model_type_by_name(model_name);
    //if not (_is_already_downloaded := model_type.get_size() is not None):
    //    if not download_ai_model(model_name=model_name, parent=self):
    //        return;
    if (text_osam_session_ == nullptr ||
        text_osam_session_->model_name() != model_name) {
        text_osam_session_ = std::make_unique<SamSession>(model_name);
    }

    //boxes, scores, labels, masks = bbox_from_text.get_bboxes_from_texts(
    //    session=this->_text_osam_session,
    //    image=utils.img_qt_to_arr(this->image)[:, :, :3],
    //    image_id=str(hash(this->imagePath)),
    //    texts=texts,
    //);

    //SCORE_FOR_EXISTING_SHAPE: float = 1.01;
    //for shape in this->canvas.shapes:
    //    if shape.shape_type != shape_type or shape.label not in texts:
    //        continue;
    //    points: NDArray[np.float64] = np.array(
    //        [[p.x(), p.y()] for p in shape.points]
    //    );
    //    xmin, ymin = points.min(axis=0);
    //    xmax, ymax = points.max(axis=0);
    //    box = np.array([xmin, ymin, xmax, ymax], dtype=np.float32);
    //    boxes = np.r_[boxes, [box]];
    //    scores = np.r_[scores, [SCORE_FOR_EXISTING_SHAPE]];
    //    labels = np.r_[labels, [texts.index(shape.label)]];
    //
    //boxes, scores, labels, indices = bbox_from_text.nms_bboxes(
    //    boxes=boxes,
    //    scores=scores,
    //    labels=labels,
    //    iou_threshold=this->_ai_text_to_annotation_widget.get_iou_threshold(),
    //    score_threshold=this->_ai_text_to_annotation_widget.get_score_threshold(),
    //    max_num_detections=100,
    //);
    //
    //is_new = scores != SCORE_FOR_EXISTING_SHAPE;
    //boxes = boxes[is_new];
    //scores = scores[is_new];
    //labels = labels[is_new];
    //indices = indices[is_new];
    //
    //if masks is not None:
    //    masks = masks[indices]
    //del indices;
    //
    //shapes: list[Shape] = bbox_from_text.get_shapes_from_bboxes(
    //    boxes=boxes,
    //    scores=scores,
    //    labels=labels,
    //    texts=texts,
    //    masks=masks,
    //    shape_type=shape_type,
    //);

    this->slotTaskSubmit();
    const auto image = utils::ImageToMat(image_);
    const auto image_id = std::hash<QString>{}(image_path_);
    QList<TlShape> shapes = bbox_from_text::get_shapes_from_texts(text_osam_session_.get(), image, image_id, texts);
    this->slotTaskFinish();

    this->canvas_->storeShapes();
    this->load_shapes(shapes, false);
    this->setDirty();
}

void MainWindow::resetState() {
    this->shape_list_->clear();
    this->image_path_.clear();
    this->imageData_.clear();
    this->label_file_.reset();
    this->other_data_.clear();
    this->canvas_->resetState();
}

QListWidgetItem *MainWindow::currentItem() {
    const auto items = this->label_list_->selectedItems();
    if (!items.empty()) {
        return items[0];
    }
    return nullptr;
}

// Callbacks

void MainWindow::undoShapeEdit() {
    this->canvas_->restoreShape();
    this->shape_list_->clear();
    this->load_shapes(this->canvas_->shapes_);
    this->undo_->setEnabled(this->canvas_->isShapeRestorable());
}

void MainWindow::tutorial() {
    //url = "https://github.com/labelmeai/labelme/tree/main/examples/tutorial"  # NOQA
    //webbrowser.open(url)
}

void MainWindow::about() {
}

void MainWindow::toggleDrawingSensitive(bool drawing) {
    //Toggle drawing sensitive.
    //
    //In the middle of drawing, toggling between modes should be disabled.
    //
    this->edit_mode_->setEnabled(!drawing);
    this->undo_last_point_->setEnabled(drawing);
    this->undo_->setEnabled(!drawing);
    this->delete_->setEnabled(!drawing);
}

void MainWindow::switch_canvas_mode(bool edit, const QString &createMode) {
    if (createMode == "ai_points_to_shape") {
        const auto model_name = canvas_->get_ai_model_name();
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
    this->canvas_->setEditing(edit);
    if (!createMode.isEmpty()) {
        this->canvas_->createMode_ = createMode;
    }
    if (edit) {
        for (const auto &draw_action : this->actions_draw_ | std::views::values) {
            draw_action->setEnabled(true);
        }
    } else {
        for (auto &[draw_mode, draw_action] : this->actions_draw_) {
            draw_action->setEnabled(createMode != draw_mode);
        }
    }
    this->edit_mode_->setEnabled(!edit);
    this->ai_text_to_annotation_widget_->setEnabled(
        !edit
        && AI_CREATE_MODES.contains(createMode)
    );
    this->ai_assist_annotation_widget_->setEnabled(!edit && AI_CREATE_MODES.contains(createMode));
    if (createMode == "ai_points_to_shape") {
        this->ai_assist_annotation_widget_->set_disabled_models(AI_MODELS_WITHOUT_POINT_SUPPORT);
    } else {
        this->ai_assist_annotation_widget_->set_disabled_models({});
    }
}

void MainWindow::highlight_ai_buttons(bool highlight) {
    const QString HIGHLIGHT_COLOR("#FFFFCC");
    const QString BORDER_COLOR("#E6E6A0");
    const QString bg = highlight ? HIGHLIGHT_COLOR : "transparent";
    const QString border = highlight ? BORDER_COLOR : "transparent";
    const QString style = QString(
        "QToolButton:!checked:!pressed {"
        " background-color: %1; border: 1px solid {border};"
        " }"
    ).arg(bg);
    for (auto &[mode, action] : actions_draw_) {
        if (AI_CREATE_MODES.contains(mode))
            for (const auto &widget : action->associatedWidgets())
                if (qobject_cast<QToolButton *>(widget))
                    widget->setStyleSheet(style);
    }
}

void MainWindow::popLabelListMenu(const QPoint &point) {
    // PyQt5 stubs type QMenu.exec() argument too narrowly
    this->label_menu_->exec(this->shape_list_->mapToGlobal(point));
}

bool MainWindow::validateLabel(const QString &label) {
    // no validation
    if (YAML_STR(config_["validate_label"]).empty()) {
        return true;
    }

    for (auto i = 0; i < label_list_->count(); ++i) {
        // PyQt5 stubs: item() typed as Optional and .data() unrecognized
        auto label_i = label_list_->item(i)->data(Qt::UserRole);
        if (YAML_STR(config_["validate_label"]) == "exact") {
            if (label_i == label) {
                return true;
            }
        }
    }
    return false;
}

void MainWindow::edit_label(bool value) {
    auto items = shape_list_->selectedItems();
    if (items.empty()) {
        SPDLOG_WARN("No label is selected, so cannot edit label.");
        return;
    }

    const auto shape = items[0]->shape();

    bool edit_text = true;
    bool edit_flags = true;
    bool edit_group_id = true;
    bool edit_description = true;
    if (items.size() > 1) {
        edit_text = std::all_of(
            items.begin() + 1, items.end(), [&](const auto &item) { return item->shape().label_ == shape.label_ ; });
        edit_flags = std::all_of(
            items.begin() + 1, items.end(), [&](const auto &item) { return item->shape().flags_ == shape.flags_ ; });
        edit_group_id = std::all_of(
            items.begin() + 1, items.end(), [&](const auto &item) {
            return item->shape().group_id_ == shape.group_id_ ;
        });
        edit_description = std::all_of(items.begin() + 1, items.end(), [&](const auto &item) {
            return item->shape().description_ == shape.description_ ;
        });
    }
    if (!edit_text) {
        this->label_dialog_->edit_->setDisabled(true);
        this->label_dialog_->labelList_->setDisabled(true);
    }
    if (!edit_group_id)
        this->label_dialog_->edit_group_id_->setDisabled(true);
    if (!edit_description)
        this->label_dialog_->editDescription_->setDisabled(true);

    const auto [text, flags, group_id, description] = this->label_dialog_->popUp(
        edit_text ? shape.label_ : "",
        edit_flags ? shape.flags_ : QMap<QString, bool>{},
        edit_group_id ? shape.group_id_ : None,
        edit_description ? shape.description_ : "",
        !edit_flags
    );

    if (!edit_text) {
        this->label_dialog_->edit_->setDisabled(false);
        this->label_dialog_->labelList_->setDisabled(false);
    }
    if (!edit_group_id)
        this->label_dialog_->edit_group_id_->setDisabled(false);
    if (not edit_description)
        this->label_dialog_->editDescription_->setDisabled(false);

    if (text.isEmpty()) { // canceled
        //assert flags is None
        //assert group_id is None
        //assert description is None
        return;
    }

    if (!this->validateLabel(text)) {
        this->errorMessage(
            this->tr("Invalid label"),
            this->tr("Invalid label '%1' with validation type '%2'").arg(
                text, YAML_QSTR(config_["validate_label"])
            )
        );
        return;
    }

    this->canvas_->storeShapes();
    for (const auto &item : items) {
        auto shape = item->shape();

        if (edit_text)
            shape.label_ = text;
        if (edit_flags)
            shape.flags_ = flags;
        if (edit_group_id)
            shape.group_id_ = group_id;
        if (edit_description)
            shape.description_ = description;

        this->update_shape_color(shape);
        // assert shape.label is not None
        if (shape.group_id_ == None) {
            int32_t r, g, b;
            shape.fill_color.getRgb(&r, &g, &b);
            item->setText(
                QString("%1 <font color=\"#%2%3%4\">●</font>").arg(text.toHtmlEscaped())
                    .arg(r, 2, 16, '0').arg(g, 2, 16, '0').arg(b, 2, 16, '0')
            );
        } else {
            item->setText(QString("%1 (%2)").arg(shape.label_).arg(shape.group_id_));
        }
        item->setShape(shape);  // 由于保存的是对象, 需要更新到回去.
        canvas_->update_shape_info(shape);
        this->setDirty();
        if (this->label_list_->find_label_item(shape.label_) == nullptr)
            this->label_list_->add_label_item(
                shape.label_,
                get_rgb_by_label(shape.label_, label_list_)
            );
    }
}

void MainWindow::fileSearchChanged() {
    import_images_from_dir(
        prev_opened_dir_, files_search_->text()
    );
}

void MainWindow::file_list_item_selection_changed() {
    if (!can_continue()) {
        return;
    }
    const auto items = docks_files_list_->selectedItems();
    if (items.empty()) {
        return;
    }
    load_file(items[0]->text());
}

// React to canvas signals.
void MainWindow::shapeSelectionChanged(const QList<int32_t> &selected_shapes) {
    QObject::disconnect(shape_list_, &ShapeListView::itemSelectionChanged, this,
        &MainWindow::label_selection_changed
    );
    for (auto &shape : canvas_->selectedShapes_) {
        canvas_->shapes_[shape].selected_ = false;
    }
    shape_list_->clearSelection();
    canvas_->selectedShapes_ = selected_shapes;
    for (auto &idx : canvas_->selectedShapes_) {
        canvas_->shapes_[idx].selected_ = true;
        const auto item = shape_list_->findItemByShape(canvas_->shapes_[idx]);
        shape_list_->selectItem(item);
        shape_list_->scrollToItem(item);
    }
    QObject::connect(shape_list_, &ShapeListView::itemSelectionChanged, this,
        &MainWindow::label_selection_changed
    );
    const auto n_selected = selected_shapes.size() > 0;
    delete_->setEnabled(n_selected);
    duplicate_->setEnabled(n_selected);
    copy_->setEnabled(n_selected);
    edit_->setEnabled(n_selected);
}

void MainWindow::addLabel(TlShape &shape) {
    QString text;
    if (shape.group_id_ == None) {
        text = shape.label_;
    } else {
        text = QString("%1 (%2)").arg(shape.label_).arg(shape.group_id_);
    }
    auto *const shape_list_item = new ShapeListItem(text);
    shape_list_->addItem(shape_list_item);
    if (label_list_->find_label_item(shape.label_) == nullptr) {
        this->label_list_->add_label_item(
            shape.label_,
            get_rgb_by_label(shape.label_, label_list_)
        );
    }
    label_dialog_->addLabelHistory(shape.label_);
    for (const auto &action : actions_on_shapes_present_) {
        action->setEnabled(true);
    }

    int32_t r, g, b;
    update_shape_color(shape);
    shape.fill_color_.getRgb(&r, &g, &b);
    shape_list_item->setText(
        QString("%1 <font color=\"#%2%3%4\">●</font>").arg(text.toHtmlEscaped())
            .arg(r, 2, 16, '0').arg(g, 2, 16, '0').arg(b, 2, 16, '0')
    );
    shape_list_item->setShape(shape);   // 更新完颜色后再设置.
}

void MainWindow::update_shape_color(TlShape &shape) {
    //assert shape.label is not None
    const auto v = get_rgb_by_label(
        shape.label_, label_list_
    );
    shape.line_color_ = QColor(v[0], v[1], v[2]);
    shape.vertex_fill_color_ = QColor(v[0], v[1], v[2]);
    shape.hvertex_fill_color_ = QColor(255, 255, 255);
    shape.fill_color_ = QColor(v[0], v[1], v[2], 128);
    shape.select_line_color_ = QColor(255, 255, 255);
    shape.select_fill_color_ = QColor(v[0], v[1], v[2], 155);
}

std::vector<int32_t> MainWindow::get_rgb_by_label(const QString &label, LabelList *label_list) {
    const auto label_colors = YAML_QSTR(config_["label_colors"]);
    if (YAML_STR(config_["shape_color"]) == "auto") {
        const auto *item = label_list->find_label_item(label);
        const int32_t item_index = (
            (item != nullptr) ?
            label_list->indexFromItem(item).row() :
            label_list->count()
        );
        const int32_t label_id = (
            1   // skip black color by default
            + item_index
            + this->config_["shift_auto_shape_color"].as<int32_t>()
        );
        int32_t r, g, b;
        LABEL_COLORMAP[label_id % LABEL_COLORMAP.size()].getRgb(&r, &g, &b);
        return { r, g, b };
    } else if (
        YAML_STR(config_["shape_color"]) == "manual"
        && !label_colors.isNull()
        && label_colors.contains(label)
    ) {
        if (this->config_["label_colors"][label.toStdString()].as<std::vector<int32_t>>().size() != 3) {
            throw std::runtime_error(
                "Color for label must be 0-255 RGB tuple, but got: "
            );
        }
        return this->config_["label_colors"][label.toStdString()].as<std::vector<int32_t>>();
    } else if (!this->config_["default_shape_color"].as<std::vector<int32_t>>().empty()) {
        return this->config_["default_shape_color"].as<std::vector<int32_t>>();
    }

    return {0, 255, 0};
}

void MainWindow::remLabels(const QList<TlShape> &shapes) {
    QObject::disconnect(shape_list_, &ShapeListView::itemDropped, this, &MainWindow::labelOrderChanged);
    for (const auto &shape : shapes) {
        auto *item = shape_list_->findItemByShape(shape);
        shape_list_->removeItem(item);
    }
    QObject::connect(shape_list_, &ShapeListView::itemDropped, this, &MainWindow::labelOrderChanged);
}

void MainWindow::load_shapes(QList<TlShape> &shapes, bool replace) {
    QObject::disconnect(shape_list_, &ShapeListView::itemSelectionChanged, this, &MainWindow::label_selection_changed);
    for (auto &shape : shapes) {
        addLabel(shape);
    }
    shape_list_->clearSelection();
    QObject::connect(shape_list_, &ShapeListView::itemSelectionChanged, this, &MainWindow::label_selection_changed);
    canvas_->loadShapes(shapes, replace);
}

void MainWindow::load_shape_dicts(const QList<ShapeDict> &shape_dicts) {
    QList<TlShape> shapes;
    for (auto &shape_dict : shape_dicts) {
        TlShape shape;
        shape.label_=shape_dict.label;
        shape.shape_type_=shape_dict.shape_type;
        shape.group_id_=shape_dict.group_id;
        shape.description_=shape_dict.description;
        shape.mask_=shape_dict.mask;

        for (auto &p : shape_dict.points) {
            shape.addPoint(QPointF(p.x(), p.y()));
        }
        shape.close();

        QMap<QString, bool> default_flags = {};
        //if self._config["label_flags"]:
        //    for pattern, keys in self._config["label_flags"].items():
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

void MainWindow::load_flags(const YAML::Node &flags, QListWidget *const widget) const {
    widget->clear();
    for (const auto &it : flags) {
        const auto key = it.first.as<std::string>();
        const auto flag = it.second.as<bool>();
        auto *item = new QListWidgetItem(QString::fromStdString(key));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(flag ? Qt::Checked : Qt::Unchecked);
        widget->addItem(item);
    }
}

bool MainWindow::saveLabels(const QString &filename) {
    auto lf = std::unique_ptr<LabelFile>(new LabelFile());

    const auto format_shape = [](const TlShape &s) {
        auto data = s.other_data_;
        QMap<QString, bool> flags;
        ShapeDict shape_dict;
        //data.update(
        //    dict(
                //shape_dict.other_data=s.other_data_;
                shape_dict.label=s.label_; //.encode("utf-8") if PY2 else s.label,
                shape_dict.points=s.points_; // [(p.x(), p.y()) for p in s.points],
                shape_dict.group_id=s.group_id_;
                shape_dict.description=s.description_;
                shape_dict.shape_type=s.shape_type_;
                shape_dict.flags=s.flags_;
                shape_dict.mask=s.mask_;
                //if s.mask is None
                //else utils.img_arr_to_b64(s.mask.astype(np.uint8)),
        //    );
        //);
        return shape_dict;
    };

    //shapes = [format_shape(item.shape()) for item in this->labelList];
    QMap<QString, bool> flags = {};
    for (auto i = 0; i < this->flags_list_->count(); ++i) {
        const auto *item = this->flags_list_->item(i);
        const auto key = item->text();
        const auto flag = item->checkState() == Qt::Checked;
        flags[key] = flag;
    }
    try {
        QFileInfo fileInfo(image_path_);
        QString imagePath = fileInfo.fileName();
        QByteArray imageData = config_["with_image_data"].as<bool>() ? imageData_ : QByteArray{};
        if (!fileInfo.path().isEmpty() && !QFile::exists(fileInfo.path())) {
            (void)QDir().mkdir(fileInfo.path());
        }
        lf->save(
            filename,
            canvas_->shapes_,
            imagePath,
            imageData,
            image_.height(),
            image_.width(),
            other_data_,
            flags
        );
        label_file_ = std::move(lf);
        auto items = docks_files_list_->findItems(image_path_, Qt::MatchExactly);
        if (items.count() > 0) {
            if (items.count() != 1)
                throw std::runtime_error("There are duplicate files.");
            items[0]->setCheckState(Qt::Checked);
        }
        return true;
    } catch (const LabelFileError &e) {
        errorMessage(
            tr("Error saving label data"), tr("<b>%1</b>").arg(e.what())
        );
    }
    return false;
}

void MainWindow::duplicateSelectedShape() {
    copySelectedShape();
    pasteSelectedShape();
}

void MainWindow::pasteSelectedShape() {
    // 粘贴时需要为图形生成新的uuid.
    QList<TlShape> copied_shapes;
    std::ranges::for_each(copied_shapes_, [&copied_shapes](auto &s){ copied_shapes.push_back(s.clone()); });
    this->load_shapes(copied_shapes, false);
    this->canvas_->selectShapes(copied_shapes);
    setDirty();
}

void MainWindow::copySelectedShape() {
    copied_shapes_.clear();
    std::ranges::for_each(this->canvas_->selectedShapes_, [&](auto &s){
        copied_shapes_.push_back(canvas_->shapes_[s]);}
    );
    paste_->setEnabled(copied_shapes_.size() > 0);
}

void MainWindow::label_selection_changed() {
    QList<TlShape> selected_shapes = {};
    for (const auto &item : shape_list_->selectedItems()) {
        selected_shapes.append(item->shape());
    }
    if (!selected_shapes.empty()) {
        canvas_->selectShapes(selected_shapes);
    } else {
        if (canvas_->deSelectShape()) {
            canvas_->update();
        }
    }
}

void MainWindow::labelItemChanged(const ShapeListItem *item) {
    const auto shape = item->shape();
    canvas_->setShapeVisible(shape, item->checkState() == Qt::Checked);
}

void MainWindow::labelOrderChanged() {
    setDirty();
    // 不能且不需要重新加载, shape_list中保存的原始图形, 不包含锚点调整信息。
    //QList<TlShape> shapes;
    //QList<ShapeListItem *> items = shape_list_->items();
    //std::ranges::transform(items, std::back_inserter(shapes), [](auto &item){ return item->shape(); });
    //this->canvas_->loadShapes(shapes);
}

// Callback functions:

void MainWindow::newShape() {
    //Pop-up and give focus to the label editor.
    //
    //position MUST be in global coordinates.
    //
    auto items = label_list_->selectedItems();
    QString text;
    if (!items.isEmpty()) {
        text = items[0]->data(Qt::UserRole).toString();
    }
    QMap<QString, bool> flags = {};
    int32_t group_id = None;
    QString description;
    if (config_["display_label_popup"].as<bool>() || text.isEmpty()) {
        QString previous_text = label_dialog_->edit_->text();
        std::tie(text, flags, group_id, description) = label_dialog_->popUp(text);
        if (text.isEmpty()) {
            label_dialog_->edit_->setText(previous_text);
        }
    }

    if (!text.isEmpty() && !validateLabel(text)) {
        errorMessage(
            tr("Invalid label"),
            tr("Invalid label '%1' with validation type '%2'").arg(
                text, config_["validate_label"].as<bool>()
            )
        );
        text = "";
    }
    if (!text.isEmpty()) {
        label_list_->clearSelection();
        //assert isinstance(flags, dict)
        auto shapes = canvas_->setLastLabel(text, group_id, description, flags);    // 在Canvas上更新.
        for (auto shape : shapes) {
            shape.group_id_ = group_id;
            shape.description_ = description;
            addLabel(shape);
        }
        edit_mode_->setEnabled(true);
        undo_last_point_->setEnabled(false);
        undo_->setEnabled(true);
        setDirty();
    } else {
        canvas_->undoLastLine();
        canvas_->shapesBackups_.pop_back();
    }
}

void MainWindow::scrollRequest(int32_t delta, Qt::Orientation orientation) {
    const auto units = -delta * 0.1;  // natural scroll
    const auto *bar = scroll_bars_[orientation];
    const auto value = bar->value() + bar->singleStep() * units;
    setScroll(orientation, value);
}

void MainWindow::setScroll(Qt::Orientation orientation, float value) {
    scroll_bars_[orientation]->setValue(value);
    scroll_values_[orientation][image_path_] = value;
}

void MainWindow::set_zoom(int32_t value, QPointF pos) {
    if (image_path_.isEmpty()) {
        SPDLOG_WARN("image_path is None, cannot set zoom");
        return;
    }

    if (pos.isNull())
        pos = QPointF(this->canvas_->visibleRegion().boundingRect().center());
    int32_t canvas_width_old = this->canvas_->width();

    this->fit_width_->setChecked(zoom_mode_ == ZoomMode::FIT_WIDTH);
    this->fit_window_->setChecked(zoom_mode_ == ZoomMode::FIT_WINDOW);
    this->canvas_->enableDragging(
        value > scalers_[ZoomMode::FIT_WINDOW]() * 100
    );
    this->zoom_widget_->setValue(value);  // triggers self._paint_canvas
    this->zoom_values_[image_path_] = {this->zoom_mode_, value};

    int32_t canvas_width_new = this->canvas_->width();
    if (canvas_width_old == canvas_width_new) {
        return;
    }
    float canvas_scale_factor = 1.0*canvas_width_new / canvas_width_old;
    float x_shift = pos.x() * canvas_scale_factor - pos.x();
    float y_shift = pos.y() * canvas_scale_factor - pos.y();
    setScroll(
        Qt::Horizontal,
        this->scroll_bars_[Qt::Horizontal]->value() + x_shift
    );
    setScroll(
        Qt::Vertical,
        this->scroll_bars_[Qt::Vertical]->value() + y_shift
    );
}

void MainWindow::set_zoom_to_original() {
    zoom_mode_ = ZoomMode::MANUAL_ZOOM;
    set_zoom(100);
}

void MainWindow::add_zoom(float increment, QPointF pos) {
    int32_t zoom_value;
    if (increment > 1) {
        zoom_value = std::ceil(zoom_widget_->value() * increment);
    } else {
        zoom_value = std::floor(zoom_widget_->value() * increment);
    }
    zoom_mode_ = ZoomMode::MANUAL_ZOOM;
    set_zoom(zoom_value, pos);
}

void MainWindow::zoom_requested(int32_t delta, QPointF pos) {
    add_zoom(delta > 0 ? 1.1 : 0.9, pos);
}

void MainWindow::setFitWindow(bool value) {
    if (value) {
        fit_width_->setChecked(false);
    }
    zoom_mode_ = value ? ZoomMode::FIT_WINDOW : ZoomMode::MANUAL_ZOOM;
    adjust_scale();
}

void MainWindow::setFitWidth(bool value) {
    if (value) {
        fit_window_->setChecked(false);
    }
    zoom_mode_ = value ? ZoomMode::FIT_WIDTH : ZoomMode::MANUAL_ZOOM;
    adjust_scale();
}

void MainWindow::enableKeepPrevScale(bool enabled) {
    config_["keep_prev_scale"] = enabled;
    keep_prev_scale_->setChecked(enabled);
}

void MainWindow::onNewBrightnessContrast(const QImage &image) {
    // QPixmap::fromImage: 深拷贝, 原始QImage的数据会被复制到新的QPixmap中.
    canvas_->loadPixmap(QPixmap::fromImage(image), image_path_, false);
}

void MainWindow::brightnessContrast(bool value, bool is_initial_load) {
    if (image_path_.isEmpty()) {
        SPDLOG_WARN("image_path is None, cannot set brightness/contrast");
        return;
    }

    int32_t brightness = None;
    int32_t contrast = None;
    if (const auto it = brightness_contrast_values_.find(this->image_path_); it != brightness_contrast_values_.end()) {
        brightness = it->first; contrast = it->second;
    }

    if (is_initial_load) {
        if (config_["keep_prev_brightness_contrast"].as<bool>() && !prev_image_path_.isEmpty())
            if (const auto it = brightness_contrast_values_.find(prev_image_path_); it != brightness_contrast_values_.end()) {
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
        [this](const QImage &image) { onNewBrightnessContrast(image); },
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
        image_path_,
        brightness,
        contrast);
}

void MainWindow::toggleShapes(int32_t value) {
    for (auto *item : shape_list_->items()) {
        auto target = (value == None) ? item->checkState() == Qt::Unchecked : value;
        item->setCheckState(target ? Qt::Checked : Qt::Unchecked); // emit itemChanged
    }
}

QString MainWindow::get_label_path(QString image_or_label_path) {
    if (LabelFile::is_label_file(image_or_label_path))
        return image_or_label_path;
    const QFileInfo file_info(image_or_label_path);
    return (output_dir_.isEmpty() ? file_info.absolutePath() : output_dir_)
        + "/" + file_info.baseName() + LabelFile::suffix;
}

void MainWindow::load_file(QString image_or_label_path) {
    // changing fileListWidget loads file
    if (imageList().contains(image_or_label_path) &&
        docks_files_list_->currentRow() != imageList().indexOf(image_or_label_path)
    ) {
        this->docks_files_list_->setCurrentRow(
            imageList().indexOf(image_or_label_path)
        );
        this->docks_files_list_->repaint();
        return;
    }

    //prev_shapes: list[Shape] = (
    //    self._canvas_widgets.canvas.shapes
    //    if self._config["keep_prev"]
    //    or QtWidgets.QApplication.keyboardModifiers()
    //    == (Qt.ControlModifier | Qt.ShiftModifier)
    //    else []
    //)
    QList<TlShape> prev_shapes = (
        config_["keep_prev"].as<bool>() || QApplication::keyboardModifiers() == (Qt::ControlModifier | Qt::ShiftModifier)
        ? canvas_->shapes_ : QList<TlShape>()
    );
    this->prev_image_path_ = this->image_path_;
    this->resetState();
    this->canvas_->setEnabled(false);
    if (!QFile::exists(image_or_label_path)) {
        this->errorMessage(
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
        label_path = get_label_path(image_or_label_path))
    ) {
        try {
            this->label_file_ = std::unique_ptr<LabelFile>(new LabelFile(label_path));
        } catch (LabelFileError &e) {
            errorMessage(
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
        this->imageData_ = label_file_->imageData_;
        this->image_path_ = QFileInfo(label_path).absolutePath() + "/" + label_file_->imagePath_;
        this->other_data_ = label_file_->otherData_;
    } else {
        auto image_path = image_or_label_path;
        try {
            this->imageData_ = LabelFile::load_image_file(image_path);
        } catch (OSError &e) {
            errorMessage(
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
        this->label_file_.reset();
    }
    auto t0 = std::chrono::system_clock::now();
    const auto image = QImage::fromData(imageData_);
    SPDLOG_INFO("Created QImage in {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - t0).count());

    if (image.isNull()) {
        QStringList formats;
        for (auto &fmt : QImageReader::supportedImageFormats()) {
            formats.append(QString("*.%1").arg(fmt.toLower()));
        }
        errorMessage(
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
    canvas_->loadPixmap(QPixmap::fromImage(image), this->image_path_);
    SPDLOG_INFO("Loaded pixmap in {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - t0).count());
    YAML::Node flags; //flags = {k: False for k in config_["flags"] or []}
    if (this->label_file_) {
        load_shape_dicts(label_file_->shapes1_);
        //if (labelFile_->flags_ is not None) {
        //    flags.update(this->labelFile.flags);
        //}
    }
    this->load_flags(flags, flags_list_);
    if (config_["keep_prev"].as<bool>() && this->noShapes()) {
        this->load_shapes(prev_shapes, false);
        this->setDirty();
    } else {
        this->setClean();
    }
    canvas_->setEnabled(true);
    // set zoom values
    bool is_initial_load = !zoom_values_.empty();
    if (zoom_values_.contains(image_path_)) {
        zoom_mode_ = zoom_values_[image_path_].first;
        set_zoom(zoom_values_[image_path_].second);
    } else if (is_initial_load || !config_["keep_prev_scale"].as<bool>()) {
        zoom_mode_ = ZoomMode::FIT_WINDOW;
        adjust_scale();
    }
    // set scroll values
    for (auto &orientation : scroll_values_.keys()) {
        if (scroll_values_[orientation].contains(image_path_)) {
            setScroll(
                orientation, scroll_values_[orientation][image_path_]);
        }
    }
    this->brightnessContrast(false, true);
    this->paint_canvas();
    this->toggleActions(true);
    this->canvas_->setFocus();
    show_status_message(
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
        canvas_ &&
        !image_.isNull() &&
        zoom_mode_ != ZoomMode::MANUAL_ZOOM) {
        adjust_scale();
    }
    QMainWindow::resizeEvent(event);
}

void MainWindow::paint_canvas() {
    if (image_.isNull()) {
        SPDLOG_WARN("image is null, cannot paint canvas");
        return;
    }
    canvas_->scale_ = 0.01 * zoom_widget_->value();
    canvas_->adjustSize();
    canvas_->update();
}

void MainWindow::adjust_scale() {
    set_zoom(scalers_[zoom_mode_]() * 100);
}

float MainWindow::scaleFitWindow() const {
    const float EPSILON_TO_HIDE_SCROLLBAR = 2.0;
    const auto viewport_w = this->centralWidget()->width() - EPSILON_TO_HIDE_SCROLLBAR;
    const auto viewport_h = this->centralWidget()->height() - EPSILON_TO_HIDE_SCROLLBAR;

    const auto pixmap_w = this->canvas_->pixmap_.width() * 1.;
    const auto pixmap_h = this->canvas_->pixmap_.height() * 1.;

    const auto scale_by_width = viewport_w / pixmap_w;
    const auto scale_by_height = viewport_h / pixmap_h;
    return std::min(scale_by_width, scale_by_height);
}

float MainWindow::scaleFitWidth() const {
    float EPSILON_TO_HIDE_SCROLLBAR = 15.0;
    auto w = this->centralWidget()->width() - EPSILON_TO_HIDE_SCROLLBAR;
    return w / this->canvas_->pixmap_.width();
}

void MainWindow::enableSaveImageWithData(bool enabled) {
    config_["with_image_data"] = enabled;
    save_with_image_data_->setChecked(enabled);
}

void MainWindow::reset_layout() {
    settings_.remove("window/state");
    restoreState(default_state_);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (!can_continue()) {
        event->ignore();
    }
    settings_.setValue("window/size", this->size());
    settings_.setValue("window/position", this->pos());
    settings_.setValue("window/state", this->saveState());
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
    importDroppedImageFiles(items);
}

// User Dialogs #

void MainWindow::open_prev_image(bool value) {
    int32_t row_prev = docks_files_list_->currentRow() - 1;
    if (row_prev < 0) {
        SPDLOG_INFO("there is no prev image");
        return;
    }
    SPDLOG_INFO("setting current row to {}", row_prev);
    docks_files_list_->setCurrentRow(row_prev);
    docks_files_list_->repaint();
}

void MainWindow::open_next_image(bool value) {
    int32_t row_next = docks_files_list_->currentRow() + 1;
    if (row_next >= docks_files_list_->count()) {
        SPDLOG_INFO("there is no next image");
        return;
    }
    SPDLOG_INFO("setting current row to {}", row_next);
    docks_files_list_->setCurrentRow(row_next);
    docks_files_list_->repaint();
}

void MainWindow::open_file_with_dialog(bool value) {
    if (!can_continue()) {
        return;
    }
    QString path = QString::fromStdString(AppConfig::instance().last_work_dir_);
    QString filters(tr("Images(*.png *.bmp *.jpg *.jpeg)"));
    //formats = [
    //    f"*.{fmt.data().decode()}"
    //    for fmt in QtGui.QImageReader.supportedImageFormats()
    //]
    //filters = self.tr("Image & Label files (%s)") % " ".join(
    //    formats + [f"*{LabelFile.suffix}"]
    //)
    FileDialogPreview fileDialog(this);
    fileDialog.setFileMode(FileDialogPreview::ExistingFile);
    fileDialog.setNameFilter(filters);
    fileDialog.setWindowTitle(
        tr("%1 - Choose Image or Label file").arg(tr("tl assistant"))
    );
    fileDialog.setWindowFilePath(path);
    fileDialog.setViewMode(FileDialogPreview::Detail);
    if (fileDialog.exec()) {
        const auto image_or_label_path = fileDialog.selectedFiles()[0];
        if (!image_or_label_path.isEmpty()) {
            this->load_from_file_or_dir(image_or_label_path);
        }
    }
}

void MainWindow::changeOutputDirDialog(bool _value) {
    auto default_output_dir = this->output_dir_;
    if (default_output_dir.isEmpty() && !this->image_path_.isEmpty()) {
        default_output_dir = QFileInfo(this->image_path_).path();
    }
    if (default_output_dir.isEmpty()) {
        default_output_dir = this->currentPath();
    }

    auto output_dir = QFileDialog::getExistingDirectory(
        this,
        tr("%1 - Save/Load Annotations in Directory").arg(tr("tl assistant")),
        default_output_dir,
        QFileDialog::ShowDirsOnly |
        QFileDialog::DontResolveSymlinks
    );
    //output_dir = str(output_dir)

    if (output_dir.isEmpty()) {
        return;
    }
    output_dir_ = output_dir;

    statusBar()->showMessage(
        tr("%1 . Annotations will be saved/loaded in %2")
        .arg("Change Annotations Dir", output_dir_)
    );
    statusBar()->show();

    const auto current_image_path = image_path_;
    import_images_from_dir(prev_opened_dir_);

    const auto imagelist = imageList();
    if (imagelist.contains(current_image_path)) {
        // retain currently selected file
        docks_files_list_->setCurrentRow(
            imagelist.indexOf(current_image_path)
        );
        docks_files_list_->repaint();
    }
}

void MainWindow::save_label_file(bool save_as) {
    //assert not self.image.isNull(), "cannot save empty image"

    QString label_path;
    if (!save_as && (label_file_ != nullptr))
        label_path = label_file_->filename_;
    if (label_path.isEmpty())
        label_path = saveFileDialog();

    if (label_path.isEmpty()) {
        SPDLOG_WARN("label_path={} is empty, so cannot save", label_path);
        return;
    }
    if (saveLabels(label_path)) {
        setClean();
    }
}

QString MainWindow::saveFileDialog() {
    //assert self._image_path is not None
    const QString caption = tr("%1 - Choose File").arg(tr("tl assistant"));
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
        get_label_path(image_path_),
        tr("Label files (*%1)").arg(LabelFile::suffix)
    );
    return label_path;
}

void MainWindow::closeFile(bool value) {
    if (!can_continue()) {
        return;
    }
    this->resetState();
    this->setClean();
    this->toggleActions(false);
    this->canvas_->setEnabled(false);
    this->docks_files_list_->setFocus();
    this->save_as_->setEnabled(false);
}

QString MainWindow::getLabelFile() {
    //assert self.image_path_ is not None
    std::filesystem::path file_path(image_path_.toStdString());
    return QString::fromStdString(file_path.replace_extension("json").string());
}

void MainWindow::deleteFile() {
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

    const auto annotation_path = getLabelFile();
    if (!QFileInfo::exists(annotation_path)) {
        return;
    }
    QFile::remove(annotation_path);
    SPDLOG_INFO("Label file is removed: {}", annotation_path);

    auto *const item = docks_files_list_->currentItem();
    if (item) {
        item->setCheckState(Qt::Unchecked);
    }

    // 修改: 删除标签文件后保持当前打开图像.
    //resetState();
    this->shape_list_->clear();
    this->label_file_.reset();
    this->other_data_.clear();
    this->canvas_->resetState();
    this->canvas_->loadPixmap(QPixmap::fromImage(this->image_), this->image_path_);
}

void MainWindow::open_config_file() {
//    if self._config_file is None:
//        QtWidgets.QMessageBox.information(
//            self,
//            self.tr("No Config File"),
//            self.tr(
//                "Configuration was provided as a YAML expression via "
//                "command line.\n\n"
//                "To use the preferences editor, start Labelme with a config file:\n"
//                "  tl_assistant --config ~/.labelmerc"
//            ),
//        )
//        return
//    config_file: Path = self._config_file
//
//    system: str = platform.system()
//    if system == "Darwin":
//        subprocess.Popen(["open", "-t", config_file])
//    elif system == "Windows":
//        os.startfile(config_file)  # type: ignore[attr-defined]
//    else:
//        subprocess.Popen(["xdg-open", config_file])
}

// Message Dialogs. #
bool MainWindow::hasLabels() {
    if (noShapes()) {
        this->errorMessage(
            "No objects labeled",
            "You must label at least one object to save the file."
        );
        return false;
    }
    return true;
}

bool MainWindow::hasLabelFile() {
    if (image_path_.isEmpty()) {
        return false;
    }

    auto label_file = getLabelFile();
    return QFile::exists(label_file);
}

bool MainWindow::can_continue() {
    if (!is_changed_) {
        return true;
    }
    const QString prompt_text = QString(tr("Save annotations to \"{%1}\" before closing?")).arg(
        image_path_
    );
    auto user_choice = QMessageBox::question(
        this,
        tr("Save annotations?"),
        prompt_text,
        QMessageBox::Save
        | QMessageBox::Discard
        | QMessageBox::Cancel,
        QMessageBox::Save
    );
    if (user_choice ==  QMessageBox::Save) {
        save_label_file();
        return true;
    }
    return user_choice ==  QMessageBox::Discard;
}

void MainWindow::errorMessage(const QString &title, const QString &message) {
    QMessageBox::critical(
        this, title, QString("<p><b>%1</b></p>%2").arg(title, message)
    );
}

QString MainWindow::currentPath() {
    return image_path_.isEmpty() ? "." : QFileInfo(image_path_).path();
}

void MainWindow::toggleKeepPrevMode() {
    config_["keep_prev"] = !config_["keep_prev"].as<bool>();
}

void MainWindow::removeSelectedPoint() {
    canvas_->removeSelectedPoint();
    canvas_->update();
    if (
        canvas_->hShape_ != None &&
        canvas_->shapes_[canvas_->hShape_].points_.empty())
    {
        canvas_->deleteShape(canvas_->shapes_[canvas_->hShape_]);
        remLabels({ canvas_->shapes_[canvas_->hShape_] });
        if (noShapes()) {
            for (const auto &action : actions_on_shapes_present_) {
                action->setEnabled(false);
            }
        }
    }
    setDirty();
}

void MainWindow::deleteSelectedShape() {
    const auto yes = QMessageBox::Yes, no = QMessageBox::No;
    const auto msg = tr(
        "You are about to permanently delete %1 shapes, proceed anyway?"
    ).arg(canvas_->selectedShapes_.length());
    if (yes == QMessageBox::warning(
        this, tr("Attention"), msg, yes | no, yes
    )) {
        remLabels(canvas_->deleteSelected());
        setDirty();
        if (noShapes()) {
            for (auto *action : actions_on_shapes_present_) {
                action->setEnabled(false);
            }
        }
    }
}

void MainWindow::copyShape() {
    canvas_->endMove(true);
    for (auto &&shape : canvas_->selectedShapes_ | std::views::transform([this](int32_t i) { return canvas_->shapes_[i]; })) {
        addLabel(shape);
    }
    label_list_->clearSelection();
    setDirty();
}

void MainWindow::moveShape() {
    canvas_->endMove(false);
    setDirty();
}

void MainWindow::load_from_file_or_dir(const QString &file_or_dir) {
    if (file_or_dir.isEmpty())
        throw std::invalid_argument("file_or_dir cannot be empty");

    if (LabelFile::is_label_file(file_or_dir)) {
        docks_files_list_->clear();
        files_dock_->setEnabled(false);
        files_dock_->setToolTip(
            tr("File list is disabled when a label file is opened")
        );
        load_file(file_or_dir);
    } else if (QFileInfo(file_or_dir).isDir()) {
        import_images_from_dir(
            file_or_dir, files_search_->text()
        );
        open_next_image();
    } else {
        import_images_from_dir(
            QFileInfo(file_or_dir).path(),
            files_search_->text()
        );
        load_file(file_or_dir);
    }
}

void MainWindow::open_dir_with_dialog(bool value) {
    if (!can_continue()) {
        return;
    }

    QString defaultOpenDirPath;
    if (!prev_opened_dir_.isEmpty() && QFile::exists(prev_opened_dir_)) {
        defaultOpenDirPath = prev_opened_dir_;
    } else {
        defaultOpenDirPath =
            image_path_.isEmpty() ? "." : QFileInfo(image_path_).path();
    }

    auto dir_path = QString(
        QFileDialog::getExistingDirectory(
            this,
            tr("%1 - Open Directory").arg(tr("tl assistant")),
            defaultOpenDirPath,
            QFileDialog::ShowDirsOnly |
            QFileDialog::DontResolveSymlinks
        )
    );
    if (!dir_path.isEmpty())
        load_from_file_or_dir(dir_path);
}

//@property
QStringList MainWindow::imageList() {
    QStringList lst;
    for (auto i = 0; i < docks_files_list_->count(); ++i) {
        auto *const item = docks_files_list_->item(i);
        //assert item
        lst.append(item->text());
    }
    return lst;
}

void MainWindow::importDroppedImageFiles(const QStringList &imageFiles) {
    QStringList extensions;
    for (const auto fmt : QImageReader::supportedImageFormats() | std::views::transform([](auto &v){ return v.toLower(); })) {
        extensions.push_back(fmt);
    }

    image_path_.clear();
    for (const auto file : imageFiles | std::views::transform([](auto &v){ return v.toLower(); })) {
        if (imageList().contains(file) || std::ranges::none_of(extensions, [&](auto &e) { return file.endsWith(e); }))
            continue;
        auto *item = new QListWidgetItem(file);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        if (QFile::exists(get_label_path(file))) {
            item->setCheckState(Qt::Checked);
        } else {
            item->setCheckState(Qt::Unchecked);
        }
        docks_files_list_->addItem(item);
    }

    if (imageList().count() > 1) {
        open_next_img_->setEnabled(true);
        open_prev_img_->setEnabled(true);
    }

    open_next_image();
}

void MainWindow::import_images_from_dir(
    const QString &root_dir, const QString &pattern)
{
    open_next_img_->setEnabled(true);
    open_prev_img_->setEnabled(true);

    if (!can_continue() || root_dir.isEmpty()) {
        return;
    }
    files_dock_->setEnabled(true);
    files_dock_->setToolTip("");

    AppConfig::instance().last_work_dir_ = root_dir.toStdString();
    prev_opened_dir_ = root_dir;
    image_path_.clear();
    docks_files_list_->clear();

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
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        if (QFile::exists(
            get_label_path(image_path)
        )) {
            item->setCheckState(Qt::Checked);
        } else {
            item->setCheckState(Qt::Unchecked);
        }
        docks_files_list_->addItem(item);
    }
}

void MainWindow::update_status_stats(const QPointF &mouse_pos) {
    QStringList stats;
    stats.append(QString("mode=%1").arg(ModeName(canvas_->mode_)));
    stats.append(QString("x=%1, y=%2").arg(mouse_pos.x(), 0, 'f', 1).arg(mouse_pos.y(), 0, 'f', 1));
    stats_->setText(stats.join(" | "));
}

QStringList MainWindow::scan_image_files(const QString &root_dir) const {
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

    // return natsort.os_sorted(images)
    return utils::natsorted(images);
}

//
// User-assisted function.

MainWindow::~MainWindow() {
    delete ui_;
    delete canvas_;
    SamApis::instance().unregister_all("");
    AppConfig::instance().save();
    spdlog::shutdown();
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

    QObject::connect(sub_process_, &QProcess::stateChanged, this, [=](QProcess::ProcessState state) {
        if (state == QProcess::ProcessState::NotRunning) {
        }
    });
    QObject::connect(sub_process_, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(slotProcessExited()));
    QObject::connect(qApp, SIGNAL(aboutToQuit()), sub_process_, SLOT(terminate()));
    QObject::connect(sub_process_, SIGNAL(error(QProcess::ProcessError)), this, SLOT(slotError(QProcess::ProcessError)));

    QObject::connect(sub_process_,&QProcess::started,[=]() {//启动完成
        std::cerr << "进程已启动" << std::endl;
    });
    QObject::connect(sub_process_,&QProcess::stateChanged,[=]() {//进程状态改变
        if (sub_process_->state()==QProcess::Running) {
            std::cerr << "正在运行" << std::endl;
        } else if(sub_process_->state()==QProcess::NotRunning) {
            std::cerr << "不在运行" << std::endl;
        } else {
            std::cerr << "正在启动" << std::endl;
        }
    });
    QObject::connect(sub_process_,&QProcess::errorOccurred,[=]() {
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