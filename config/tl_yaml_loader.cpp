#include "tl_yaml_loader.h"
#include "spdlog/spdlog.h"


QVariant YamlLoader::YamlMapToMap(const YAML::Node &node) {
    QMap<QString, QVariant> result;
    for (const auto &kv : node) {
        QString key = QString::fromStdString(kv.first.as<std::string>());
        result[key] = YamlNodeToVariant(kv.second);
    }
    return result;
}

QVariant YamlLoader::YamlMapToList(const YAML::Node &node) {
    QList<QVariant> result;
    for (const auto &item : node) {
        result.append(YamlNodeToVariant(item));
    }
    return result;
}

QVariant YamlLoader::YamlNodeToVariant(const YAML::Node &node) {
    if (node.IsScalar()) {
        const auto scalar = node.as<std::string>();
        if (scalar == "true" || scalar == "True" || scalar == "TRUE") {
            return true;
        }
        if (scalar == "false" || scalar == "False" || scalar == "FALSE") {
            return false;
        }

        try {
            int v = node.as<int>();
            SPDLOG_INFO("===> {}", v);
            return v;
        } catch (...) {}

        try {
            return node.as<double>();
        } catch (...) {}

        return QString::fromStdString(scalar);
    }
    if (node.IsMap()) {
        return QVariant::fromValue(YamlMapToMap(node));
    }
    if (node.IsSequence()) {
        return QVariant::fromValue(YamlMapToList(node));
    }
    return {};
}

QMap<QString, QVariant> YamlLoader::safe_load(const YAML::Node &node) {
    QMap<QString, QVariant> result;
    for (const auto &kv : node) {
        const auto key = QString::fromStdString(kv.first.as<std::string>());
        result[key] = YamlLoader::YamlNodeToVariant(kv.second);
    }
    return result;
}