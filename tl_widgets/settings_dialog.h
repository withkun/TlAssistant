#ifndef __INC_SETTINGS_DIALOG_H
#define __INC_SETTINGS_DIALOG_H

#include <QDialog>
#include <QGroupBox>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QStandardItemModel>

#include "schema.h"


class PlainTextEdit: public QPlainTextEdit {
    Q_OBJECT
public:
    explicit PlainTextEdit(QWidget *parent=nullptr) : QPlainTextEdit(parent) {}

    void mark_committed();
    void commit();

protected:
    void focusOutEvent(QFocusEvent *e) override;

signals:
    void editing_finished();

private:
    QString                             committed_text_;
};

class SettingsPage: public QWidget {
public:
    explicit SettingsPage(const QList<std::tuple<QString, QIcon, QWidget *>> &groups);

    int32_t required_width();
    void scroll_to_group(int32_t index);
    void sync_navigation_to_scroll(int32_t value);

private:
    QListWidget                        *navigation_{nullptr};
    QScrollArea                        *scroll_area_{nullptr};
    QWidget                            *content_{nullptr};
    QList<QWidget *>                    groups_;
    bool                                scrolling_to_group_{false};

};

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(
        const QMap<QString, QVariant> &config,
        const std::function<bool(QList<QString>, QVariant)> &apply_setting,
        const std::function<void(bool)> &open_as_text,
        QWidget *parent=nullptr
    );
    ~SettingsDialog() override = default;

    void accept() override;
    void reject() override;

    void set_value(const QList<QString> &key_path, const QVariant &value);
    void set_choice_enabled(const QList<QString> &key_path, const QVariant &value, bool enabled, const QString &disabled_reason);
    QVariant read_value(const QList<QString> &key_path);
    QWidget *build_group(const QString &title, const QList<Setting> &settings);
    QWidget *build_label_cell(const Setting &setting);
    QWidget *create_editor(const Setting &setting);
    QWidget *create_combo(const Setting &setting, const QVariant &value, const QList<QPair<QString, QVariant>> &items, int32_t min_width);
    void set_editor_value(QWidget *editor, const QVariant &value);
    bool apply(const QList<QString> &key_path, const QVariant &value);
    void revert_editor(const QList<QString> &key_path);
    void on_labels_edited(PlainTextEdit *edit);
    void sync_validate_label_gate();

    static QLabel *build_beta_badge(const QString &text);
    static QList<QString> parse_str_list(const PlainTextEdit *edit);

private:
    QMap<QString, QVariant>                         config_;
    std::function<bool(QList<QString>, QVariant)>   apply_setting_;
    QMap<QList<QString>, QWidget *>                 editors_;

    SettingsPage                                   *page_{nullptr};
};
#endif //__INC_SETTINGS_DIALOG_H