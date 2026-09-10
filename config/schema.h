#ifndef __INC_SCHEMA_H
#define __INC_SCHEMA_H

#include <QList>
#include <QString>
#include <QVariant>


class Setting {
public:
    QList<QString>  key_path;
    QString         group;
    QString         label;
    QString         kind;
    // For "enum": the allowed values. A None entry is a real choice meaning
    // "unset/disabled"; it round-trips to YAML null and the dialog renders it
    // as an explicit "(none)" option, never as the string "None".
    QList<QString>  choices;    //: tuple[object, ...] | None = None
    // Display labels paralleling choices; falls back to str(choice) when None.
    QList<QString>  choice_labels;  //: tuple[str, ...] | None = None
    // Optional muted caption rendered beneath the control.
    QString         note;       //: str | None = None
    // Marks a feature shipped for early use: renders a "BETA" badge beside the
    // label so users expect rough edges and report issues. Drop when it stabilizes.
    bool            beta{false};
};

class schema {
public:
    static const QList<QString> Group;

    static const QList<QString> Kind;

    static const QList<Setting> SETTINGS;
};
#endif //__INC_SCHEMA_H