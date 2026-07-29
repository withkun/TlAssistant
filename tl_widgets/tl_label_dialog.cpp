#include "tl_label_dialog.h"
#include "spdlog/spdlog.h"
#include "common/format_qt.h"
#include "common/utilities.h"

#include <QKeyEvent>
#include <QCompleter>
#include <QCheckBox>
#include <QPushButton>
#include <QApplication>
#include <QRegularExpression>


void LabelLineEdit::setListWidget(QListWidget *list_widget) {
    list_widget_ = list_widget;
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
                         const QMap<QString, bool> &flags) : QDialog(parent) {
    if (fit_to_content.isEmpty()) {
        fit_to_content_ = { {"row", false}, {"column", true} };
    } else {
        fit_to_content_ = fit_to_content;
    }

    edit_ = new LabelLineEdit;
    edit_->setPlaceholderText("Enter object label");
    edit_->setValidator(utils::labelValidator());
    QObject::connect(edit_, &LabelLineEdit::editingFinished, this, &LabelDialog::postProcess);
    if (!flags.isEmpty()) {
        QObject::connect(edit_, &LabelLineEdit::textChanged, this, &LabelDialog::updateFlags);
    }
    edit_group_id_ = new QLineEdit;
    edit_group_id_->setPlaceholderText("Group ID");
    edit_group_id_->setValidator(
        new QRegularExpressionValidator(QRegularExpression("\\d*"))
    );
    auto *layout = new QVBoxLayout;
    if (show_text_field) {
        auto layout_edit = new QHBoxLayout;
        layout_edit->addWidget(edit_, 6);
        layout_edit->addWidget(edit_group_id_, 2);
        layout->addLayout(layout_edit);
    }
    // buttons
    buttonBox_ = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        Qt::Horizontal,
        this
    );
    QObject::connect(buttonBox_, &QDialogButtonBox::accepted, this, &LabelDialog::validate);
    QObject::connect(buttonBox_, &QDialogButtonBox::rejected, this, &LabelDialog::reject);
    layout->addWidget(buttonBox_);
    // label_list
    labelList_ = new QListWidget;
    if (fit_to_content_["row"]) {
        labelList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }
    if (fit_to_content_["column"]) {
        labelList_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }
    sort_labels_ = sort_labels;
    if (!labels.isEmpty()) {
        labelList_->addItems(labels);
    }
    if (sort_labels) {
        labelList_->sortItems();
    } else {
        labelList_->setDragDropMode(QAbstractItemView::InternalMove);
    }
    QObject::connect(labelList_, &QListWidget::currentItemChanged, this, &LabelDialog::labelSelected);
    QObject::connect(labelList_, &QListWidget::itemDoubleClicked, this, &LabelDialog::labelDoubleClicked);
    labelList_->setFixedHeight(150);
    edit_->setListWidget(labelList_);
    layout->addWidget(labelList_);
    //label_flags
    //if (flags.isEmpty())
    //    flags = {};
    flags_ = flags;
    flagsLayout_ = new QVBoxLayout;
    resetFlags();
    layout->addItem(flagsLayout_);
    QObject::connect(edit_, &LabelLineEdit::textChanged, this, &LabelDialog::updateFlags);
    // text edit
    editDescription_ = new QTextEdit;
    editDescription_->setPlaceholderText("Label description");
    editDescription_->setFixedHeight(50);
    layout->addWidget(editDescription_);
    setLayout(layout);
    // completion
    auto *completer = new QCompleter;  // 自动补全
    if (completion == "startswith") {
        completer->setCompletionMode(QCompleter::InlineCompletion);
        //Default settings.
        //completer->setFilterMode(Qt::MatchStartsWith);
    } else if (completion == "contains") {
        completer->setCompletionMode(QCompleter::PopupCompletion);
        completer->setFilterMode(Qt::MatchContains);
    } else {
        throw std::invalid_argument("Unsupported completion: " + completion.toStdString());
    }
    completer->setModel(labelList_->model());
    edit_->setCompleter(completer);
}

void LabelDialog::addLabelHistory(const QString &label) {
    if (!labelList_->findItems(label, Qt::MatchExactly).isEmpty()) {
        return;
    }
    labelList_->addItem(label);
    if (sort_labels_) {
        labelList_->sortItems();
    }
}

void LabelDialog::labelSelected(QListWidgetItem *item, QListWidgetItem *previous) {
    edit_->setText(item->text());
}

void LabelDialog::validate() {
    if (!edit_->isEnabled()) {
        accept();
        return;
    }
    if (!get_stripped_text().isEmpty()) {
        accept();
    }
}

QString LabelDialog::get_stripped_text() {
    auto text = edit_->text();
    //if (hasattr(text, "strip")) {
    //    text = text.strip();
    //} else {
        return text.trimmed();
    //}

    return text;
}

void LabelDialog::labelDoubleClicked(QListWidgetItem *item) {
    validate();
}

void LabelDialog::postProcess() {
    edit_->setText(get_stripped_text());
}

void LabelDialog::updateFlags(QString label_new) {
    // keep state of shared flags
    auto flags_old = getFlags();

    QMap<QString, bool> flags_new = {};
    for (auto &item : flags_) {
        ////pattern, keys
        //QRegularExpression re(item.first);
        //if (re.match(item->first, label_new)) {
        //    for (auto key : item.second) {
        //        flags_new[key] = flags_old.get(key, False);
        //    }
        //}
    }
    setFlags(flags_new);
}

void LabelDialog::deleteFlags() {
    //for (i in reversed(range(self.flagsLayout.count()))) {
    //    item = self.flagsLayout.itemAt(i).widget();
    //    self.flagsLayout.removeWidget(item);
    //    item.setParent(None);
    //}
}

void LabelDialog::resetFlags(QString label) {
    QMap<QString, bool> flags = {};
    //for (pattern, keys in self._flags.items()) {
    //    if (re.match(pattern, label)) {
    //        for (key in keys) {
    //            flags[key] = false;
    //        }
    //    }
    //}
    setFlags(flags);
}

void LabelDialog::setFlags(QMap<QString, bool> &flags) {
    deleteFlags();
    for (auto key : flags.keys()) {
        auto item = new QCheckBox(key, this);
        item->setChecked(flags[key]);
        flagsLayout_->addWidget(item);
        item->show();
    }
}

QMap<QString, bool> LabelDialog::getFlags() {
    QMap<QString, bool> flags = {};
    for (auto i = 0; i < flagsLayout_->count(); ++i) {
        auto *item = qobject_cast<QCheckBox *>(flagsLayout_->itemAt(i)->widget());
        flags[item->text()] = item->isChecked();
    }
    return flags;
}

int32_t LabelDialog::getGroupId() {
    const auto group_id = edit_group_id_->text();
    if (!group_id.isEmpty()) {
        return group_id.toInt();
    }
    return None;
}

std::tuple<QString, QMap<QString, bool>, int32_t, QString> LabelDialog::
popUp(
    QString text,
    QMap<QString, bool> flags,
    int32_t group_id,
    QString description,
    bool flags_disabled,
    bool move) {
    if (fit_to_content_["row"]) {
        labelList_->setMinimumHeight(
            labelList_->sizeHintForRow(0) * labelList_->count() + 2
        );
    }
    if (fit_to_content_["column"]) {
        labelList_->setMinimumWidth(labelList_->sizeHintForColumn(0) + 2);
    }
    // if text is None, the previous label in self.edit is kept
    if (text.isEmpty()) {
        text = edit_->text();
    }
    // description is always initialized by empty text c.f., self.edit.text
    if (description.isEmpty()) {
        description = "";
    }
    editDescription_->setPlainText(description);
    if (!flags.isEmpty()) {
        setFlags(flags);
    } else {
        resetFlags(text);
    }
    if (flags_disabled) {
        for (auto i = 0; i < flagsLayout_->count(); ++i) {
            flagsLayout_->itemAt(i)->widget()->setDisabled(true);
        }
    }
    edit_->setText(text);
    edit_->setSelection(0, text.length());
    if (group_id == None) {
        edit_group_id_->clear();
    } else {
        edit_group_id_->setText(QString::number(group_id));
    }
    auto items = labelList_->findItems(text, Qt::MatchFixedString);
    if (!items.isEmpty()) {
        if (items.length() != 1) {
            SPDLOG_WARN("Label list has duplicate: " + text);
        }
        labelList_->setCurrentItem(items[0]);
        auto row = labelList_->row(items[0]);
        edit_->completer()->setCurrentRow(row);
    }
    edit_->setFocus(Qt::PopupFocusReason);
    if (move) {
        this->move(QCursor::pos());
    }
    if (this->exec()) {
        return std::make_tuple(
            edit_->text(),
            getFlags(),
            getGroupId(),
            editDescription_->toPlainText()
        );
    } else {
        return std::make_tuple(QString(""), QMap<QString, bool>{}, None, QString(""));
    }
}