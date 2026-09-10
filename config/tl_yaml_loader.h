#ifndef __INC_YAML_LOADER_H
#define __INC_YAML_LOADER_H

#include <QVariant>
#include "yaml-cpp/yaml.h"


class YamlLoader {
public:
    static QMap<QString, QVariant> safe_load(const YAML::Node &node);

    static QVariant YamlMapToMap(const YAML::Node &node);
    static QVariant YamlMapToList(const YAML::Node &node);
    static QVariant YamlNodeToVariant(const YAML::Node &node);
};

#endif //__INC_YAML_LOADER_H