#ifndef __INC_YAML_CONFIG_H
#define __INC_YAML_CONFIG_H

#include <QMap>
#include <QString>
#include <QVariant>
#include <QColor>


const auto VAR_COLOR = [](const QVariant &var) {
    const auto v = var.value<QList<QVariant>>() | std::views::transform([](const auto &a) { return a.toInt(); }) | std::ranges::to<QList<int>>();
    return v.size() > 3 ? QColor(v[0], v[1], v[2], v[3]) : QColor(v[0], v[1], v[2]);
};


QString get_user_config_file(bool create_if_missing=true);

class TlConfig {
public:
    static QMap<QString, QVariant> load_config(const QString &config_file, const QMap<QString, QVariant> &cfg_overrides);
};
#endif //__INC_YAML_CONFIG_H