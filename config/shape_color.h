#ifndef __INC_SHAPE_COLOR_H
#define __INC_SHAPE_COLOR_H

#include <QVariant>


void validate_shape_color(const QVariant &config);
void migrate_shape_color(QMap<QString, QVariant> &config);

std::tuple<int32_t, int32_t, int32_t> resolve_shape_color(
    const QMap<QString, QVariant> &config, const QString &label, int32_t label_index
);

#endif //__INC_SHAPE_COLOR_H