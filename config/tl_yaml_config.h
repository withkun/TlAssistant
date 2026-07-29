#ifndef __INC_CONFIG_H
#define __INC_CONFIG_H

#include <QMap>
#include <QString>
#include "yaml-cpp/yaml.h"


const auto YAML_STR = [](const auto &node) {
    return node.IsNull() ? std::string() : node.as<std::string>();
};

const auto YAML_QSTR = [](const auto &node) {
    return QString::fromStdString(YAML_STR(node));
};

const auto YAML_VSTR = [](const auto &node) {
    QList<QString> slist;
    if (node.IsScalar()) {
        slist.append(YAML_QSTR(node));
    } else if (node.IsSequence()) {
        for (const auto &n : node) {
            slist.append(YAML_QSTR(n));
        }
    }
    return slist;
};

const auto YAML_KEYS = [](const auto &node) {
    QList<QString> slist;
    if (node.IsScalar()) {
        slist.append(YAML_QSTR(node));
    } else if (node.IsSequence()) {
        for (const auto &n : node) {
            slist.append(YAML_QSTR(n));
        }
    }
    return slist;
};

const auto YAML_QMAP = [](const auto &node) {
    QMap<QString, bool> value;
    if (node.IsMap()) {
        for (const auto &n : node) {
            const auto key = YAML_QSTR(n.first);
            const auto val = n.second.as<bool>();
            value[key] = val;
        }
    }
    return value;
};

const auto YAML_VCT = [](const auto &node) {
    return node.as<std::vector<int32_t>>();
};

const auto YAML_COLOR = [](const auto &node) {
    const auto v = YAML_VCT(node);
    return v.size() > 3 ? QColor(v[0], v[1], v[2], v[3]) : QColor(v[0], v[1], v[2]);
};

template <typename T>
const auto YAML_POP = [](YAML::Node &node, const std::string &key, T value) {
    if (node.IsDefined() && node[key].IsDefined()) {
        value = node[key].as<T>();
    }
    node.remove(key);
    return value;
};

class TlConfig {
public:
    static YAML::Node load_config(const std::string &config_file, const YAML::Node &config_overrides);
};
#endif // __INC_CONFIG_H