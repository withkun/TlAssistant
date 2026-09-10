#include "tl_yaml_config.h"
#include "tl_yaml_loader.h"
#include "shape_color.h"
#include "spdlog/spdlog.h"
#include "common/format_qt.h"

#include <QFile>
#include <QVariant>
#include <QFileInfo>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QRegularExpression>


void update_dict(
    QMap<QString, QVariant> &target_dict,
    const QMap<QString, QVariant> &new_dict,
    const std::function<void(QString k, QVariant v)> &validate_item
) {
    for (const auto [key, value] : new_dict.asKeyValueRange()) {
        if (validate_item)
            validate_item(key, value);
        if (!target_dict.contains(key))
            throw std::invalid_argument("Unexpected key in config: {key}");
        if (!target_dict[key].canConvert<QMap<QString, QVariant>>()) {
            target_dict[key] = value;
            continue;
        }

        // target_dict[key] is a section, so the override must be a mapping.
        if (value.isNull())
            // An empty section (e.g. a bare `shortcuts:`) keeps its defaults
            // instead of wiping the whole section.
            continue;
        if (!value.canConvert<QMap<QString, QVariant>>())
            // A non-mapping override (e.g. `shortcuts: oops`) would wipe the
            // section with a scalar and crash the app downstream; surface it as
            // a config error instead.
            throw std::invalid_argument(
                "Config section {key!r} must be a mapping, "
                "but got {type(value).__name__}: {value!r}"
            );
        auto target = target_dict[key].toMap();
        update_dict(
            target,
            value.toMap(),
            validate_item
        );
        target_dict[key] = target;
    }
}

void validate_config_item(const QString &key, const QVariant &value) {
    if (key == "validate_label" && (!value.isNull() && value != "exact"))
        throw std::invalid_argument("Unexpected value for config key 'validate_label': {value}");
    if (key == "labels" && value != "") {
        if (!value.canConvert<QList<QString>>())
            throw std::invalid_argument(
                "Config key 'labels' must be a list, "
                "but got {type(value).__name__}: {value!r}"
            );
        auto list = value.value<QList<QString>>();
        if (list.removeDuplicates() != 0)
            throw std::invalid_argument(
                "Duplicates are detected for config key 'labels': {value}"
            );
    }
}

void migrate_config_from_file(QMap<QString, QVariant> &config_from_yaml) {
    migrate_shape_color(config_from_yaml);
    bool keep_prev_brightness = config_from_yaml.value("keep_prev_brightness", false).toBool(); config_from_yaml.remove("keep_prev_brightness");
    bool keep_prev_contrast = config_from_yaml.value("keep_prev_contrast", false).toBool(); config_from_yaml.remove("keep_prev_contrast");
    if (keep_prev_brightness || keep_prev_contrast) {
        SPDLOG_INFO(
            "Migrating old config: keep_prev_brightness={} or keep_prev_contrast={} "
            "-> keep_prev_brightness_contrast=True",
            keep_prev_brightness,
            keep_prev_contrast
        );
        config_from_yaml["keep_prev_brightness_contrast"] = true;
    }
    if (config_from_yaml.contains("store_data")) {
        SPDLOG_INFO("Migrating old config: store_data -> with_image_data");
        config_from_yaml["with_image_data"] = config_from_yaml["store_data"]; config_from_yaml.remove("store_data");
    }
    if (config_from_yaml.contains("logger_level")) {
        SPDLOG_INFO("Migrating old config: removing logger_level");
        config_from_yaml.remove("logger_level");
    }
    // A malformed section (e.g. `shortcuts: oops`) is left untouched here so the
    // merge in _update_dict reports it as a config error instead of crashing.
    auto shortcuts = config_from_yaml.value("shortcuts", {}).toMap();
    //if not isinstance(shortcuts, dict):
    //    shortcuts = {}
    if (shortcuts.remove("add_point_to_edge"))
        SPDLOG_INFO("Migrating old config: removing shortcuts.add_point_to_edge");

    const auto ai = config_from_yaml.value("ai", {}).toMap();
    const auto model_name = ai.value("default", "").toString();
    if (
        const auto m = QRegularExpression("^SegmentAnything \\((.*)\\)$").match(model_name);
        m.hasMatch()
    ) {
        auto model_name_new = QString("Sam (%1)").arg(m.captured(1));
        SPDLOG_INFO(
            "Migrating old config: ai.default={} -> ai.default={}",
            model_name,
            model_name_new
        );
        ai["default"] = model_name_new;
    }
    // Migrate polygon shortcut keys to shape
    QMap<QString, QString> POLYGON_TO_SHAPE_RENAMES = {
        { "edit_polygon", "edit_shape" },
        { "delete_polygon", "delete_shape" },
        { "duplicate_polygon", "duplicate_shape" },
        { "copy_polygon", "copy_shape" },
        { "paste_polygon", "paste_shape" },
        { "show_all_polygons", "show_all_shapes" },
        { "hide_all_polygons", "hide_all_shapes" },
        { "toggle_all_polygons", "toggle_all_shapes" },
    };
    for (auto [old_key, new_key] : POLYGON_TO_SHAPE_RENAMES.asKeyValueRange()) {
        if (!shortcuts.contains(old_key))
            continue;
        auto old_value = shortcuts.value(old_key); shortcuts.remove(old_key);
        if (shortcuts.contains(new_key)) {
            SPDLOG_INFO(
                "Migrating old config: dropping shortcuts.{}={} superseded by "
                "shortcuts.{}={}",
                old_key,
                old_value.toString(),
                new_key,
                shortcuts[new_key].toString()
            );
            continue;
        }
        SPDLOG_INFO(
            "Migrating old config: shortcuts.{} -> shortcuts.{}",
            old_key,
            new_key
        );
        shortcuts[new_key] = old_value;
    }
    // A malformed canvas/crosshair section is left untouched so the merge in
    // _update_dict reports it as a config error instead of crashing.
    auto canvas = config_from_yaml.value("canvas", {}).toMap();
    auto crosshair = canvas.value("crosshair", {}).toMap();
    //if not isinstance(crosshair, dict):
    //    crosshair = {}
    auto ai_polygon = crosshair.value("ai_polygon", {}).toString();
    auto ai_mask = crosshair.value("ai_mask", {}).toString();
    if (!ai_polygon.isEmpty() || !ai_mask.isEmpty()) {
        SPDLOG_INFO(
            "Migrating old config: canvas.crosshair.ai_polygon={} or "
            "canvas.crosshair.ai_mask={} -> canvas.crosshair.ai_points_to_shape",
            ai_polygon,
            ai_mask
        );
        if (!crosshair.contains("ai_points_to_shape"))
            crosshair["ai_points_to_shape"] = !ai_polygon.isEmpty() || !ai_mask.isEmpty();
    }

    // 需要把更新后的数据设置回去.
    canvas["crosshair"] = crosshair;
    config_from_yaml["canvas"] = canvas;
    config_from_yaml["shortcuts"] = shortcuts;
    config_from_yaml["ai"] = ai;
}

QString get_user_config_file(bool create_if_missing) {
    QString user_config_path = QCoreApplication::applicationDirPath() + "/.labelmerc";
    if (!QFileInfo::exists(user_config_path))
        return user_config_path;

    user_config_path = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/.labelmerc";
    if (!QFileInfo::exists(user_config_path) && create_if_missing)
        try {
            if (QFile file(user_config_path); file.open(QIODevice::WriteOnly | QIODevice::Text))
                file.close();
        } catch (std::exception &e) {
            SPDLOG_WARN("Failed to save config: {}", user_config_path);
        }
    return user_config_path;
}

QMap<QString, QVariant> TlConfig::load_config(const QString &config_file, const QMap<QString, QVariant> &cfg_overrides) {
    QMap<QString, QVariant> config;
    if (QFile file(":/config/default_config.yaml"); file.open(QIODevice::ReadOnly | QIODevice::Text))
        config = YamlLoader::safe_load(YAML::Load(QTextStream(&file).readAll().toStdString()));

    if (QFileInfo::exists(config_file)) {
        const auto f = YAML::LoadFile(config_file.toStdString());
        auto config_from_yaml = YamlLoader::safe_load(f);

        migrate_config_from_file(config_from_yaml);
        if (config_from_yaml.contains("shape_color"))
            validate_shape_color(config_from_yaml["shape_color"]);
        update_dict(config, config_from_yaml, validate_config_item);
    }

    auto config_overrides = cfg_overrides;
    migrate_shape_color(config_overrides);
    if (config_overrides.contains("shape_color"))
        validate_shape_color(config_overrides["shape_color"]);
    update_dict(config, config_overrides, validate_config_item);

    if (!config.contains("labels") && config["validate_label"].isValid())
        throw std::invalid_argument("labels must be specified when validate_label is enabled");
    validate_shape_color(config["shape_color"]);

    return config;
}