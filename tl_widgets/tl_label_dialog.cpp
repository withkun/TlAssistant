#include "tl_label_dialog.h"

#include "spdlog/spdlog.h"
#include "common/format_qt.h"
#include "common/qt_utils.h"
#include "tl_widgets/label_flags.h"

#include <QKeyEvent>
#include <QCompleter>
#include <QCheckBox>
#include <QPushButton>
#include <QApplication>
#include <QRegularExpression>
#include <QTimer>


const char *PLACEHOLDER_TEXT = "Enter object label";
const char *GROUP_ID_PLACEHOLDER = "Group ID";
const char *DESCRIPTION_PLACEHOLDER = "Description";

const int32_t LABEL_LIST_HEIGHT = 150;
const int32_t FLAGS_SCROLL_MAX_HEIGHT = 150;


void LabelLineEdit::set_list_widget(QListWidget *list_widget) {
    this->list_widget_ = list_widget;
}

void LabelLineEdit::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
        this->clearFocus();
        list_widget_->setFocus();
        auto *fwdEvent = new QKeyEvent(event->type(), event->key(), event->modifiers());
        QApplication::postEvent(list_widget_, fwdEvent);
    } else {
        QLineEdit::keyPressEvent(event);
    }
}

LabelDialog::LabelDialog(QWidget *parent,
                         const QStringList &labels,
                         const bool sort_labels,
                         const bool show_text_field,
                         const QString &completion,
                         const QMap<QString, bool> &fit_to_content,
                         const QMap<QString, QList<QString>> &flags,
                         const QList<QString> &label_history) : QDialog(parent) {
    this->sort_labels_ = sort_labels;
    this->flags_spec_ = compile_label_flags(flags);
    this->label_history_ = label_history;
    this->flags_disabled_ = false;
    // The flags currently on show, keyed by flag name, so a flag named by
    // two matching label_flags patterns gets exactly one checkbox.
    this->flag_checkboxes_ = {}; // : dict[str, QtWidgets.QCheckBox] = {}
    // Checked state per flag key, remembered for the lifetime of one popup.
    // The checkboxes themselves cannot hold it: editing the label rebuilds
    // them, and an intermediate keystroke that matches no pattern destroys
    // them entirely.
    this->flag_states_ = {}; // : dict[str, bool] = {}

    if (fit_to_content.isEmpty()) {
        this->fit_to_content_ = { {"row", false}, {"column", true} };
    } else {
        this->fit_to_content_ = fit_to_content;
    }

    // Build widgets
    this->edit_ = new LabelLineEdit();
    this->edit_->setPlaceholderText(PLACEHOLDER_TEXT);
    this->edit_->setValidator(utils::labelValidator());

    this->edit_group_id_ = new QLineEdit();
    this->edit_group_id_->setPlaceholderText(GROUP_ID_PLACEHOLDER);
    this->edit_group_id_->setValidator(
        new QRegularExpressionValidator(QRegularExpression("\\d*"))
    );

    this->edit_description_ = new QTextEdit();
    this->edit_description_->setPlaceholderText(DESCRIPTION_PLACEHOLDER);
    this->edit_description_->setFixedHeight(50);

    this->label_list_ = new QListWidget();
    this->label_list_->setFixedHeight(LABEL_LIST_HEIGHT);

    // Configure label list
    if (sort_labels) {
        this->label_list_->setDragDropMode(
            QAbstractItemView::DragDropMode::NoDragDrop
        );
    } else {
        this->label_list_->setDragDropMode(
            QAbstractItemView::DragDropMode::InternalMove
        );
    }

    if (fit_to_content["row"]) {
        this->label_list_->setHorizontalScrollBarPolicy(
            Qt::ScrollBarPolicy::ScrollBarAlwaysOff
        );
    }
    if (fit_to_content["column"]) {
        this->label_list_->setVerticalScrollBarPolicy(
            Qt::ScrollBarPolicy::ScrollBarAlwaysOff
        );
    }

    // Set up completer bound to label_list's model
    auto *completer = this->make_completer(completion);  // 自动补全
    //completer->setModel(this->label_list_->model());
    this->edit_->setCompleter(completer);
    this->edit_->set_list_widget(this->label_list_);

    // Button box
    auto *button_box = new QDialogButtonBox(
        QDialogButtonBox::StandardButton::Ok
        | QDialogButtonBox::StandardButton::Cancel
    );
    QObject::connect(button_box, &QDialogButtonBox::accepted, [this]() {
        this->on_ok_clicked();
    });
    QObject::connect(button_box, &QDialogButtonBox::rejected, this, &LabelDialog::reject);

    // Build layout
    auto main_layout = new QVBoxLayout();
    this->setLayout(main_layout);

    if (show_text_field) {
        auto top_row = new QHBoxLayout();
        top_row->addWidget(this->edit_, 4);
        top_row->addWidget(this->edit_group_id_, 1);
        main_layout->addLayout(top_row);
    } else {
        this->edit_->setParent(nullptr);
    }

    main_layout->addWidget(button_box);
    main_layout->addWidget(this->label_list_);

    this->flags_container_ = new QWidget();
    this->flags_layout_ = new QVBoxLayout();
    this->flags_layout_->setContentsMargins(0, 0, 0, 0);
    this->flags_layout_->setSpacing(0);
    this->flags_container_->setLayout(this->flags_layout_);

    this->flags_scroll_ = new QScrollArea();
    this->flags_scroll_->setWidgetResizable(true);
    this->flags_scroll_->setFrameShape(QFrame::Shape::NoFrame);
    this->flags_scroll_->setHorizontalScrollBarPolicy(
        Qt::ScrollBarPolicy::ScrollBarAlwaysOff
    );
    this->flags_scroll_->setWidget(this->flags_container_);
    main_layout->addWidget(this->flags_scroll_);

    main_layout->addWidget(this->edit_description_);

    // Connect signals
    QObject::connect(this->edit_, &LabelLineEdit::editingFinished, this, &LabelDialog::strip_edit_text);
    QObject::connect(this->edit_, &LabelLineEdit::textChanged, this, &LabelDialog::update_flags);
    QObject::connect(this->label_list_, &QListWidget::currentItemChanged, this, &LabelDialog::on_label_selected);
    QObject::connect(this->label_list_, &QListWidget::itemDoubleClicked, this, &LabelDialog::on_item_double_clicked);

    // Populate initial labels
    for (const auto &label : (labels  + this->label_history_))
        this->label_list_->addItem(label);
    if (sort_labels)
        this->label_list_->sortItems();
}

//@property
QList<QString> LabelDialog::label_history() const {
    return this->label_history_;
}

QCompleter *LabelDialog::make_completer(const QString &completion) {
    if (completion == "startswith") {
        auto *completer = new QCompleter(this->label_list_->model());
        completer->setCompletionMode(
            QCompleter::CompletionMode::InlineCompletion
        );
        return completer;
    } else if (completion == "contains") {
        auto *completer = new QCompleter(this->label_list_->model());
        completer->setCompletionMode(
            QCompleter::CompletionMode::PopupCompletion
        );
        completer->setFilterMode(Qt::MatchFlag::MatchContains);
        return completer;
    } else {
        throw std::invalid_argument("Unknown completion mode: {completion!r}");
    }
}

void LabelDialog::strip_edit_text() {
    this->edit_->setText(this->edit_->text().trimmed());
}

void LabelDialog::on_label_selected(
    QListWidgetItem *current,
    QListWidgetItem *previous
) {
    if (current == nullptr)
        return;
    this->edit_->setText(current->text());
}

void LabelDialog::on_item_double_clicked(QListWidgetItem *item) {
    this->label_list_->setCurrentItem(item);
    this->on_ok_clicked();
}

void LabelDialog::on_ok_clicked() {
    if (!this->edit_->isEnabled() || !this->edit_->text().trimmed().isEmpty())
        this->accept();
}

void LabelDialog::clear_flag_checkboxes() {
    this->flag_checkboxes_.clear();
    while (this->flags_layout_->count()) {
        auto item = this->flags_layout_->takeAt(0);
        if (item == nullptr)
            continue;
        auto widget = item->widget();
        if (widget != nullptr) {
            widget->setParent(nullptr);
            widget->deleteLater();
        }
    }
}

void LabelDialog::update_flags(const QString &text) {
    //this->flag_states_.update(this->collect_flags());
    QMap<QString, bool> flags;
    //for (auto [pattern, flag_keys] : this->flags_spec_.items()) {
    //    if not pattern.match(text):
    //        continue
    //    for key in flag_keys:
    //        flags[key] = self._flag_states.get(key, False)
    //}
    this->set_flag_checkboxes(flags);
}

void LabelDialog::add_label_history(const QString &label) {
    if (!this->label_history_.contains(label))
        this->label_history_.append(label);

    if (label_list_->findItems(label, Qt::MatchFlag::MatchExactly).isEmpty()) {
        label_list_->addItem(label);
        if (sort_labels_)
            label_list_->sortItems();
    }
}

void LabelDialog::set_predefined_labels(const QList<QString> &labels) {
    const auto history_extras = this->label_history_ | std::views::filter([labels](const auto &h) { return !labels.contains(h);  } ) | std::ranges::to<QList<QString>>();
    const auto all_labels = labels + history_extras;

    this->label_list_->clear();
    for (const auto &label : all_labels)
        this->label_list_->addItem(label);

    if (this->sort_labels_)
        this->label_list_->sortItems();
}

std::tuple<QString, QMap<QString, bool>, int32_t, QString> LabelDialog::
popup(
    QString text,
    bool move,
    QPoint position,
    QMap<QString, bool> flags,
    int32_t group_id,
    QString description,
    bool flags_disabled
) {
    // Drop the previous popup's checkboxes and their remembered states so a
    // fresh popup starts unchecked. This has to precede setText() below,
    // whose textChanged signal would otherwise re-seed the states from the
    // previous popup's checkboxes; the flags block below rebuilds them.
    this->flag_states_.clear();
    this->clear_flag_checkboxes();
    this->flags_disabled_ = flags_disabled;

    if (!text.isEmpty())
        this->edit_->setText(text);
    this->edit_->selectAll();

    this->edit_description_->setPlainText(description);

    if (group_id == None)
        this->edit_group_id_->setText("");
    else
        this->edit_group_id_->setText(QString::number(group_id));

    if (!flags.isEmpty())
        this->set_flag_checkboxes(flags);
    else
        this->update_flags(this->edit_->text());

    const auto matches = this->label_list_->findItems(
        this->edit_->text(), Qt::MatchFlag::MatchFixedString
    );
    if (!matches.empty())
        this->label_list_->setCurrentItem(matches[0]);

    this->fit_label_list_to_content();
    this->edit_->setFocus(Qt::FocusReason::PopupFocusReason);

    if (move) {
        auto target = !position.isNull() ? position : QCursor::pos();
        this->move_within_screen(target);
        // frameGeometry() lacks the window-manager decoration size until the
        // dialog is mapped, so re-clamp once exec() has shown it. Clamp only
        // (no re-anchor to target): a full re-move visibly jerks the already
        // visible dialog, while the clamp is a no-op unless it overflows.
        QTimer::singleShot(0, [this, target]() { this->clamp_within_screen(target); });
    }

    const auto result = this->exec();

    if (result == QDialog::DialogCode::Accepted) {
        const auto label = this->edit_->text();
        const auto returned_flags = this->collect_flags();
        const auto gid_text = this->edit_group_id_->text();
        const auto returned_group_id = !gid_text.isEmpty() ? gid_text.toInt() : None;
        const auto returned_description = this->edit_description_->toPlainText();
        return {label, returned_flags, returned_group_id, returned_description};
    }

    return {{}, {}, None, {}};
}

void LabelDialog::set_flag_checkboxes(QMap<QString, bool> &flags) {
    //self._clear_flag_checkboxes()
    //for key, checked in flags.items():
    //    checkbox = QtWidgets.QCheckBox(key)
    //    checkbox.setChecked(checked)
    //    checkbox.setEnabled(not self._flags_disabled)
    //    self._flag_checkboxes[key] = checkbox
    //    self._flags_layout.addWidget(checkbox)
    //    # A widget added to a visible layout stays hidden until the event
    //    # loop activates the layout, and the layout counts hidden widgets as
    //    # empty, so the container hint below would be momentarily 0 and
    //    # would pin the scroll area shut for the rest of the popup.
    //    checkbox.show()
    //
    //content_height = self._flags_container.sizeHint().height()
    //self._flags_scroll.setFixedHeight(min(content_height, _FLAGS_SCROLL_MAX_HEIGHT))
}

QMap<QString, bool> LabelDialog::collect_flags() {
    //return {key: cb.isChecked() for key, cb in self._flag_checkboxes.items()}
    return {};
}

void LabelDialog::fit_label_list_to_content() {
    //if self._fit_to_content["row"]:
    //    self.label_list.setMinimumHeight(
    //        self.label_list.sizeHintForRow(0) * self.label_list.count() + 2
    //    )
    //if self._fit_to_content["column"]:
    //    self.label_list.setMinimumWidth(self.label_list.sizeHintForColumn(0) + 2)
}

void LabelDialog::move_within_screen(const QPoint &target) {
    //self.adjustSize()
    //# setGeometry() anchors the client area, unlike move() which anchors the
    //# window frame: the content corner lands at target, not the title bar's.
    //self.setGeometry(QtCore.QRect(target, self.size()))
    //self._clamp_within_screen(target)
}

void LabelDialog::clamp_within_screen(const QPoint &target) {
    //screen = (
    //    QtGui.QGuiApplication.screenAt(target)
    //    or QtGui.QGuiApplication.primaryScreen()
    //)
    //if screen is None:
    //    return
    //available = screen.availableGeometry()
    //
    //// Nudge by the actual frame overflow (frameGeometry() includes the
    //// window-manager decoration) so the title bar and borders stay on screen,
    //// not just the content rect.
    //frame = self.frameGeometry()
    //dx = min(0, available.right() - frame.right())
    //dx = max(dx, available.left() - frame.left())
    //dy = min(0, available.bottom() - frame.bottom())
    //dy = max(dy, available.top() - frame.top())
    //if dx or dy:
    //    self.move(self.x() + dx, self.y() + dy)
}