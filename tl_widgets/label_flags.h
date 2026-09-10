#ifndef __INC_LABEL_FLAGS_H
#define __INC_LABEL_FLAGS_H

#include <QMap>
#include <QList>
#include <QString>
#include <QRegularExpression>


QMap<QString, QList<QString>> compile_label_flags(
    const QMap<QString, QList<QString>> &label_flags
);

#endif //__INC_LABEL_FLAGS_H