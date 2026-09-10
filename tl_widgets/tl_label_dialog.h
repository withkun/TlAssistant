#ifndef __INC_LABEL_DIALOG_H
#define __INC_LABEL_DIALOG_H

#include "qt_utils.h"

#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QCheckBox>
#include <QScrollArea>


class LabelLineEdit : public QLineEdit {
    Q_OBJECT
public:
    void set_list_widget(QListWidget *list_widget);

protected:
    void keyPressEvent(QKeyEvent *) override;

public:
    QListWidget         *list_widget_;
};

class LabelDialog : public QDialog {
    Q_OBJECT
public:
    LabelDialog(QWidget *parent,
                const QStringList &labels={},
                bool sort_labels=true,
                bool show_text_field=true,
                const QString &completion="startswith",
                const QMap<QString, bool> &fit_to_content={},
                const QMap<QString, QList<QString>> &flags={},
                const QList<QString> &label_history={});

    QMap<QString, bool>                 fit_to_content_;
    QMap<QString, QList<QString>>       flags_;
    bool                                sort_labels_;
    QMap<QString, QList<QString>>       flags_spec_;
    QList<QString>                      label_history_;
    bool                                flags_disabled_{false};
    QMap<QString, QCheckBox *>          flag_checkboxes_;
    QMap<QString, bool>                 flag_states_;

    LabelLineEdit                      *edit_{nullptr};
    QTextEdit                          *edit_description_{nullptr};
    QLineEdit                          *edit_group_id_{nullptr};
    QListWidget                        *label_list_{nullptr};

    QWidget                            *flags_container_{};
    QVBoxLayout                        *flags_layout_{};
    QScrollArea                        *flags_scroll_{};

    void update_flags(const QString &text);
    void add_label_history(const QString &label);
    QList<QString> label_history() const;
    QCompleter *make_completer(const QString &completion);
    void strip_edit_text();
    void on_label_selected(QListWidgetItem *current, QListWidgetItem *previous);
    void on_item_double_clicked(QListWidgetItem *item);
    void on_ok_clicked();
    void clear_flag_checkboxes();
    void set_flag_checkboxes(QMap<QString, bool> &flags);
    QMap<QString, bool> collect_flags();
    void fit_label_list_to_content();
    void move_within_screen(const QPoint &target);
    void clamp_within_screen(const QPoint &target);

    void set_predefined_labels(const QList<QString> &labels);
    std::tuple<QString, QMap<QString, bool>, int32_t, QString>
    popup(QString text, bool move=true, QPoint position=QPoint(), QMap<QString, bool> flags={}, int32_t group_id=None, QString description="", bool flags_disabled=false);
};
#endif //__INC_LABEL_DIALOG_H