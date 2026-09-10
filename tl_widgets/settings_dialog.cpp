#include "settings_dialog.h"

#include <QScrollBar>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QMessageBox>


void PlainTextEdit::mark_committed() {
    this->committed_text_ = this->toPlainText();
}

void PlainTextEdit::commit() {
    // Emit only on a real change so re-focusing or closing the dialog does
    // not rewrite the config file with an identical value.
    if (this->toPlainText() == this->committed_text_)
        return;
    this->mark_committed();
    emit this->editing_finished();
}

void PlainTextEdit::focusOutEvent(QFocusEvent *e) {
    QPlainTextEdit::focusOutEvent(e);
    this->commit();
}

SettingsPage::SettingsPage(
    const QList<std::tuple<QString, QIcon, QWidget *>> &groups
) {
    //super().__init__()

    auto *navigation = new QListWidget();
    navigation->setAccessibleName(tr("Settings sections"));
    navigation->setHorizontalScrollBarPolicy(
        Qt::ScrollBarPolicy::ScrollBarAlwaysOff
    );
    navigation->setTextElideMode(Qt::TextElideMode::ElideRight);
    constexpr float NAVIGATION_FONT_SIZE_INCREMENT = 1.0;
    constexpr QSize NAVIGATION_ICON_SIZE = QSize(18, 18);
    constexpr int32_t NAVIGATION_TEXT_INSET = 8;
    constexpr int32_t NAVIGATION_VERTICAL_PADDING = 12;
    QFont navigation_font = navigation->font();
    navigation_font.setPointSizeF(
        navigation_font.pointSizeF() + NAVIGATION_FONT_SIZE_INCREMENT
    );
    navigation->setFont(navigation_font);
    navigation->setIconSize(NAVIGATION_ICON_SIZE);
    navigation->setStyleSheet(
        QString("QListWidget::item {{ padding-left: {%1}px; }}").arg(NAVIGATION_TEXT_INSET)
    );
    for (const auto &[title, icon, _group_box] : groups) {
        const auto item = new QListWidgetItem(icon, title);
        item->setToolTip(title);
        navigation->addItem(item);
        auto item_size = navigation->sizeHintForIndex(navigation->indexFromItem(item));
        item_size.setHeight(
            navigation->fontMetrics().height() + NAVIGATION_VERTICAL_PADDING
        );
        item->setSizeHint(item_size);
    }
    constexpr int32_t MINIMUM_NAVIGATION_WIDTH  = 160;
    constexpr int32_t MAXIMUM_NAVIGATION_WIDTH  = 240;
    constexpr int32_t NAVIGATION_PADDING  = 8;
    const auto navigation_width = std::max(
        MINIMUM_NAVIGATION_WIDTH,
        std::min(
            MAXIMUM_NAVIGATION_WIDTH,
            navigation->sizeHintForColumn(0) + NAVIGATION_PADDING
        )
    );
    navigation->setFixedWidth(navigation_width);

    const auto content = new QWidget();
    const auto content_layout = new QVBoxLayout(content);
    for (const auto &[_title, _icon, group_box] : groups)
        content_layout->addWidget(group_box);
    content_layout->addStretch(1);

    const auto scroll_area = new QScrollArea();
    scroll_area->setFrameShape(QFrame::Shape::NoFrame);
    scroll_area->setWidgetResizable(true);
    scroll_area->setWidget(content);

    const auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(navigation);
    layout->addWidget(scroll_area, 1);

    this->navigation_ = navigation;;
    this->scroll_area_ = scroll_area;
    this->content_ = content;
    this->groups_ = groups | std::views::transform([](const auto &g) { return std::get<2>(g); }) | std::ranges::to<QList<QWidget *>>();
    this->scrolling_to_group_ = false;

    QObject::connect(navigation, &QListWidget::currentRowChanged, this, &SettingsPage::scroll_to_group);
    QObject::connect(navigation, &QListWidget::itemClicked, [this, navigation](auto *item) {
        this->scroll_to_group(navigation->row(item));
    }
    );
    QObject::connect(scroll_area->verticalScrollBar(), &QScrollBar::valueChanged, this,
        &SettingsPage::sync_navigation_to_scroll
    );
    navigation->setCurrentRow(0);
}

//@property
int32_t SettingsPage::required_width() {
    // The width below which the settings scroll sideways. The page size hint
    // does not report it: QScrollArea caps its own hint at 36 character
    // widths and ignores how far its widget refuses to shrink, so a font
    // wider than the one the layout was tuned for would size the page too
    // narrow. Swap the scroll area's hint for what the content cannot give
    // up, and keep the rest of the page hint as measured.
    return (
        this->sizeHint().width()
        - this->scroll_area_->sizeHint().width()
        + this->content_->minimumSizeHint().width()
    );
}

void SettingsPage::scroll_to_group(int32_t index) {
    if (index < 0 || index >= this->groups_.size())
        return;
    const auto group = this->groups_[index];
    const auto group_top = group->mapTo(this->content_, QPoint()).y();
    // Blocking the scroll bar would also cut the scroll area's own
    // valueChanged connection, moving the handle while the content stays put,
    // so gate the navigation sync instead of silencing the scroll bar.
    this->scrolling_to_group_ = true;
    this->scroll_area_->verticalScrollBar()->setValue(group_top);
    this->scrolling_to_group_ = false;
    QSignalBlocker blocker(this->navigation_);
    this->navigation_->setCurrentRow(index);
}

void SettingsPage::sync_navigation_to_scroll(const int32_t value) {
    if (this->scrolling_to_group_)
        return;
    const auto viewport = this->scroll_area_->viewport();
    // Move the reading point toward the viewport center as the user leaves
    // the top, so short groups near the bottom can become active too.
    const int32_t reading_position = value + std::min(value, viewport->height() / 2);
    int32_t active = 0;
    for (const auto &[index, group] :  this->groups_ | std::views::enumerate){
        auto group_top = group->mapTo(this->content_, QPoint()).y();
        if (group_top > reading_position)
            break;
        active = index;
    }
    const auto scroll_bar = this->scroll_area_->verticalScrollBar();
    if (scroll_bar->maximum() > 0 && value == scroll_bar->maximum())
        active = this->groups_.size() - 1;
    QSignalBlocker blocker(this->navigation_);
    this->navigation_->setCurrentRow(active);
}

SettingsDialog::SettingsDialog(
    const QMap<QString, QVariant> &config,
    const std::function<bool(QList<QString>, QVariant)> &apply_setting,
    const std::function<void(bool)> &open_as_text,
    QWidget *parent
) : QDialog(parent) {
    //super().__init__(parent)
    this->setWindowTitle(tr("Settings"));

    this->config_ = config;
    this->apply_setting_ = apply_setting;
    this->editors_ = {};

    QMap<QString, QString> GROUP_ICONS = {
        {"Appearance and language", ":/icons/palette.svg"},
        {"Files and saving", ":/icons/floppy-disk-duotone.svg"},
        {"Drawing and canvas", ":/icons/polygon.svg"},
        {"Continue between images", ":/icons/images.svg"},
        {"Label sources", ":/icons/tag.svg"},
        {"Label behavior", ":/icons/sliders-horizontal.svg"},
        {"AI assist", ":/icons/sparkle.svg"},
    };
    QList<std::tuple<QString, QIcon, QWidget *>> groups;
    for (const auto &group : schema::Group) {
        const auto settings = schema::SETTINGS | std::views::filter([group](const auto &setting) {
            return setting.group == group;
        }) | std::ranges::to<QList<Setting>>();
        if (settings.empty())
            continue;
        groups.append(
            {
                group,
                QIcon(GROUP_ICONS[group]),
                this->build_group(group, settings)
            }
        );
    }
    const auto page = new SettingsPage(groups);
    this->page_ = page;

    const auto open_button = new QPushButton(tr("Open config file as text…"));
    open_button->setToolTip(
        tr("Edits made in the text file apply after restart")
    );
    QObject::connect(open_button, &QPushButton::clicked, open_as_text);
    const auto close_button = new QPushButton(tr("Close"));
    close_button->setDefault(true);
    QObject::connect(close_button, &QPushButton::clicked, this, &SettingsDialog::accept);

    const auto button_layout = new QHBoxLayout();
    button_layout->addWidget(open_button);
    button_layout->addStretch(1);
    button_layout->addWidget(close_button);

    const auto layout = new QVBoxLayout();
    layout->addWidget(page, 1);
    layout->addLayout(button_layout);
    this->setLayout(layout);
    const QSize DEFAULT_DIALOG_SIZE = QSize(760, 590);
    const auto scroll_bar_width = this->style()->pixelMetric(
        QStyle::PixelMetric::PM_ScrollBarExtent
    );
    const auto page_width = std::max(page->sizeHint().width(), page->required_width());
    const auto dialog_chrome_width = this->sizeHint().width() - page->sizeHint().width();
    const auto preferred_dialog_size = QSize(
        std::max(
            DEFAULT_DIALOG_SIZE.width(),
            page_width + dialog_chrome_width + scroll_bar_width
        ),
        DEFAULT_DIALOG_SIZE.height()
    );
    const auto initial_dialog_size = preferred_dialog_size.boundedTo(
        this->screen()->availableGeometry().size()
    );
    this->setMinimumWidth(initial_dialog_size.width());
    this->resize(initial_dialog_size);

    this->sync_validate_label_gate();
}

void SettingsDialog::accept() {
    // Flush text editors whose edits commit on focus-out: clicking Close
    // does not always move focus first, so apply pending input explicitly.
    // commit() is a no-op when the text is unchanged.
    for (const auto &editor : this->editors_.values())
        if (qobject_cast<PlainTextEdit *>(editor))
            qobject_cast<PlainTextEdit *>(editor)->commit();
    QDialog::accept();
}

void SettingsDialog::reject() {
    // Immediate-apply dialog: Escape and the window-close button discard
    // nothing, so treat them like Close and flush pending edits.
    this->accept();
}

void SettingsDialog::set_value(const QList<QString> &key_path, const QVariant &value) {
    auto editor = this->editors_[key_path];
    QSignalBlocker blocker(editor);
    this->set_editor_value(editor, value);
}

void SettingsDialog::set_choice_enabled(
    const QList<QString> &key_path,
    const QVariant &value,
    const bool enabled,
    const QString &disabled_reason
) {
    auto editor = qobject_cast<QComboBox *>(this->editors_[key_path]);
    //assert isinstance(editor, QtWidgets.QComboBox)
    auto index = editor->findData(value);
    //assert index >= 0
    auto model = qobject_cast<QStandardItemModel *>(editor->model());
    //assert isinstance(model, QtGui.QStandardItemModel)
    auto item = model->item(index);
    //assert item is not None
    item->setEnabled(enabled);
    item->setToolTip(enabled ? "" : disabled_reason);
}

QVariant SettingsDialog::read_value(const QList<QString> &key_path) {
    QVariant node = this->config_;
    for (const auto &key : key_path) {
        if (!node.canConvert<QMap<QString, QVariant>>())
            throw std::invalid_argument("config path {key_path} is not a mapping at {key!r}");
        node = node.toMap()[key];
    }
    return node;
}

QWidget *SettingsDialog::build_group(
    const QString &title, const QList<Setting> &settings
) {
    auto group_box = new QGroupBox(title);
    group_box->setFlat(true);
    auto layout = new QVBoxLayout(group_box);
    for (const auto &setting : settings) {
        auto editor = this->create_editor(setting);
        editor->setAccessibleName(setting.label);
        this->editors_[{setting.key_path}] = editor;

        auto label_cell = this->build_label_cell(setting);
        auto row = new QWidget;
        if (setting.kind == "str_list") {
            auto row_layout = new QVBoxLayout(row);
            row_layout->addWidget(label_cell);
            row_layout->addWidget(editor);
        } else {
            auto row_layout = new QHBoxLayout(row);
            row_layout->addWidget(label_cell, 1);
            // Top-align the control so it pairs with the label's first line
            // rather than centering against the label+note block.
            row_layout->addWidget(editor, Qt::AlignmentFlag::AlignTop);
        }
        layout->addWidget(row);
    }
    return group_box;
}

QWidget *SettingsDialog::build_label_cell(const Setting &setting) {
    auto label = new QLabel(setting.label);
    label->setWordWrap(true);
    QWidget *title = label;
    if (setting.beta) {
        // Keep the label on one line so the badge hugs it instead of floating
        // past a wrap; the dialog auto-widens to fit the row.
        label->setWordWrap(false);
        title = new QWidget;
        auto title_layout = new QHBoxLayout(title);
        title_layout->setContentsMargins(0, 0, 0, 0);
        title_layout->setSpacing(6);
        title_layout->addWidget(label);
        title_layout->addWidget(
            build_beta_badge(tr("BETA")),
            Qt::AlignmentFlag::AlignVCenter
        );
        title_layout->addStretch(1);
    }
    if (setting.note.isEmpty())
        return title;
    auto cell = new QWidget;
    auto cell_layout = new QVBoxLayout(cell);
    cell_layout->setContentsMargins(0, 0, 0, 0);
    cell_layout->setSpacing(2);
    cell_layout->addWidget(title);
    auto note = new QLabel(setting.note);
    note->setWordWrap(true);
    // Secondary text color, kept enabled: a disabled label would be announced
    // as a disabled control and carry the platform's washed-out gray. The
    // foreground role (not an explicit palette) tracks live theme changes.
    note->setForegroundRole(QPalette::ColorRole::PlaceholderText);
    cell_layout->addWidget(note);
    return cell;
}

QWidget *SettingsDialog::create_editor(const Setting &setting) {
    const auto value = this->read_value(setting.key_path);
    if (setting.kind == "bool") {
        const auto check = new QCheckBox;
        this->set_editor_value(check, value);
        QObject::connect(check, &QCheckBox::toggled, [this, setting](bool checked) -> bool {
            return this->apply({setting.key_path}, checked);
        });
        return check;
    }
    if (setting.kind == "enum") {
        //assert setting.choices is not None
        QList<QPair<QString, QVariant>> enum_items;
        for (const auto &[index, choice] : setting.choices | std::views::enumerate) {
            QString label;
            if (setting.choice_labels.isEmpty())
                label = setting.choice_labels[index];
            else if (choice.isEmpty())
                label = "(none)";
            else
                label = choice;
            enum_items.append({label, choice});
        }
        return this->create_combo(
            setting, value, enum_items, 140
        );
    }
    if (setting.kind == "language") {
        //auto languages = sorted(
        //    (
        //        (QtCore.QLocale(code).nativeLanguageName() or code, code)
        //        for code in _locale.available_translation_locales()
        //    ),
        //    key=lambda name_and_code: name_and_code[0].casefold(),
        //)
        //items = [
        //    (self.tr("System default"), None),
        //    ("English", _locale.SOURCE_LOCALE),
        //    *languages,
        //]
        return this->create_combo(
            setting, value, {}, 160
        );
    }
    if (setting.kind == "str_list") {
        auto edit = new PlainTextEdit;
        edit->setPlaceholderText(tr("one item per line"));
        edit->setMinimumHeight(64);
        edit->setMaximumHeight(96);
        this->set_editor_value(edit, value);
        if (setting.key_path == QList<QString>{"labels"})
            QObject::connect(edit, &PlainTextEdit::editing_finished, [this, edit](){ this->on_labels_edited(edit); });
        else
            QObject::connect(edit, &PlainTextEdit::editing_finished, [this, edit, setting]() {
                this->apply(setting.key_path, parse_str_list(edit));
            });
        return edit;
    }
    throw std::runtime_error("typing.assert_never(setting.kind)");
}

QWidget *SettingsDialog::create_combo(
    const Setting &setting,
    const QVariant &value,
    const QList<QPair<QString, QVariant>> &items,
    const int32_t min_width
) {
    auto combo = new QComboBox;
    combo->setMinimumWidth(min_width);
    for (const auto &[label, data] : items)
        combo->addItem(label, data);
    this->set_editor_value(combo, value);
    QObject::connect(combo, &QComboBox::currentIndexChanged, [this, setting, combo]() {
        this->apply(setting.key_path, combo->currentData());
    });
    return combo;
}

void SettingsDialog::set_editor_value(QWidget *editor, const QVariant &value) {
    if (auto *e = qobject_cast<QCheckBox *>(editor))
        e->setChecked(value.toBool());
    else if (auto *e = qobject_cast<QComboBox *>(editor))
        e->setCurrentIndex(std::max(e->findData(value), 0));
    else if (auto *e = qobject_cast<PlainTextEdit *>(editor)) {
        auto items = value.toStringList();
        e->setPlainText(items.join("\n"));
        e->mark_committed();
    }
}

bool SettingsDialog::apply(const QList<QString> &key_path, const QVariant &value) {
    auto editor = qobject_cast<QComboBox *>(this->editors_[key_path]);
    if (qobject_cast<QComboBox *>(editor)) {
        auto model = qobject_cast<QStandardItemModel *>(editor->model());
        //assert isinstance(model, QtGui.QStandardItemModel)
        auto item = model->item(editor->findData(value));
        //assert item is not None
        if (!item->isEnabled()) {
            this->revert_editor(key_path);
            return false;
        }
    }
    if (this->apply_setting_({key_path}, value))
        return true;
    // The write failed and the in-memory config was left unchanged, so reset
    // the editor to the last-saved value rather than show a phantom edit that
    // never persisted.
    this->revert_editor(key_path);
    return false;
}

void SettingsDialog::revert_editor(const QList<QString> &key_path) {
    this->set_value(key_path, this->read_value(key_path));
}

void SettingsDialog::on_labels_edited(PlainTextEdit *edit) {
    auto labels = parse_str_list(edit);
    auto validate_combo = qobject_cast<QComboBox *>(this->editors_[{"validate_label"}]);
    if (
        !labels.empty()
        && qobject_cast<QComboBox *>(validate_combo)
        && validate_combo->currentData() == "exact"
    ) {
        QMessageBox::warning(
            this,
            tr("Configuration Error"),
            tr(
                "Predefined labels cannot be empty while Label validation is set "
                "to exact. Disable exact validation first."
            )
        );
        this->revert_editor({"labels"});
        return;
    }
    this->apply({"labels"}, labels);
    this->sync_validate_label_gate();
}

void SettingsDialog::sync_validate_label_gate() {
    auto *labels_editor = qobject_cast<PlainTextEdit *>(this->editors_[{"labels"}]);
    auto *validate_combo = qobject_cast<QComboBox *>(this->editors_[{"validate_label"}]);
    if (!qobject_cast<PlainTextEdit *>(labels_editor) || !qobject_cast<QComboBox *>(
        validate_combo
    ))
        return;
    const auto exact_index = validate_combo->findData("exact");
    const auto model = qobject_cast<QStandardItemModel *>(validate_combo->model());
    if (exact_index < 0 || !qobject_cast<QStandardItemModel *>(model))
        return;

    const bool allowed = parse_str_list(labels_editor).isEmpty();
    model->item(exact_index)->setEnabled(allowed);
    if (!allowed && validate_combo->currentData() == "exact")
        validate_combo->setCurrentIndex(validate_combo->findData(""));
}

QLabel *SettingsDialog::build_beta_badge(const QString &text) {
    const auto badge = new QLabel(text);
    badge->setSizePolicy(
        QSizePolicy::Policy::Fixed, QSizePolicy::Policy::Fixed
    );
    // palette() refs (not literal hex) so the app's theme switch re-resolves them
    // via _retheme; a muted outline reads as a status tag, not an accent-colored
    // control, and text-on-window keeps the body-text contrast in both themes.
    badge->setStyleSheet(
        "QLabel {"
        "  color: palette(text);"
        "  border: 1px solid palette(mid);"
        "  border-radius: 7px;"
        "  padding: 0px 6px;"
        "  font-size: 10px;"
        "  font-weight: 600;"
        "}"
    );
    return badge;
}

QList<QString> SettingsDialog::parse_str_list(const PlainTextEdit *edit) {
    QList<QString> items;
    for (const auto &line : edit->toPlainText().split('\n')) {
        const auto item = line.trimmed();
        if (!item.isEmpty() && !items.contains(item))
            items.append(item);
    }
    return items;
}