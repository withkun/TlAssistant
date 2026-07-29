#ifndef __INC_LABEL_DIALOG_H
#define __INC_LABEL_DIALOG_H

#include "utilities.h"

#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QTextEdit>

class LabelLineEdit : public QLineEdit {
    Q_OBJECT
public:
    void setListWidget(QListWidget *list_widget);

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
                const QMap<QString, bool> &flags={});

    QMap<QString, bool>                 fit_to_content_;
    QMap<QString, bool>                 flags_;
    bool                                sort_labels_;
    QDialogButtonBox                   *buttonBox_{nullptr};
    LabelLineEdit                      *edit_{nullptr};
    QTextEdit                          *editDescription_{nullptr};
    QLineEdit                          *edit_group_id_{nullptr};
    QVBoxLayout                        *flagsLayout_{nullptr};
    QListWidget                        *labelList_{nullptr};

    void validate();
    QString get_stripped_text();
    void labelSelected(QListWidgetItem *current, QListWidgetItem *previous);
    void labelDoubleClicked(QListWidgetItem *item);
    void updateFlags(QString label_new);
    void postProcess();
    void addLabelHistory(const QString &label);
    void deleteFlags();
    void resetFlags(QString label="");
    void setFlags(QMap<QString, bool> &flags);
    QMap<QString, bool> getFlags();
    int32_t getGroupId();
    std::tuple<QString, QMap<QString, bool>, int32_t, QString>
    popUp(QString text="", QMap<QString, bool> flags={}, int32_t group_id=None, QString description="", bool flags_disabled=false, bool move=true);
};
#endif //__INC_LABEL_DIALOG_H