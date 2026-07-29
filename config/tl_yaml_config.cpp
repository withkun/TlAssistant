#include "tl_yaml_config.h"
#include "spdlog/spdlog.h"
#include "common/format_qt.h"
#include "nlohmann/detail/input/parser.hpp"
#include "spdlog/fmt/bundled/base.h"

#include <QFile>
#include <QTextStream>
#include <QStandardPaths>


// default_config_file = os.path.join(os.path.expanduser("~"), ".labelmerc")
// 'c:/Users/njtl007/.labelmerc'

YAML::Node get_config() {
    const auto HomeLocation = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    YAML::Node config = YAML::LoadFile("C:/Users/njtl007/.labelmerc");
    if (config["labels"] && config["validate_label"]) {
        return YAML::Node();
    }

    return config;
}

void update_dict(YAML::Node target_dict, const YAML::Node &new_dict, const std::function<void(std::string k, std::string v)> validate_item) {
    //for (auto it = new_dict.begin(); it != new_dict.end(); ++it) {
    for (const auto it : new_dict) {
        const auto key = it.first.as<std::string>();
        const auto value = it.second;
        //if (validate_item)
        //    validate_item(key, value);
        if (!target_dict[key].IsDefined())
            throw std::invalid_argument("Unexpected key in config: {key}");
        if (target_dict[key].IsMap() && value.IsMap())
            update_dict(target_dict[key], value, validate_item);
        else
            target_dict[key] = value;
    }
}

const auto validate_config_item = [](auto key, auto value) {
    //if (key == "validate_label" and value not in [None, "exact"])
    //    throw std::invalid_argument("Unexpected value for config key 'validate_label': {value}");
    //if (key == "shape_color" and value not in [None, "auto", "manual"])
    //    throw std::invalid_argument("Unexpected value for config key 'shape_color': {value}");
    //if (key == "labels" and value is not None and len(value) != len(set(value)))
    //    throw std::invalid_argument("Duplicates are detected for config key 'labels': {value}");
};

void migrate_config_from_file(YAML::Node &config_from_yaml) {
    bool keep_prev_brightness = YAML_POP<bool>(config_from_yaml, "keep_prev_brightness", false);
    bool keep_prev_contrast = YAML_POP<bool>(config_from_yaml, "keep_prev_contrast", false);
    if (keep_prev_brightness || keep_prev_contrast) {
        SPDLOG_INFO(
            "Migrating old config: keep_prev_brightness={} or keep_prev_contrast={} "
            "-> keep_prev_brightness_contrast=True",
            keep_prev_brightness,
            keep_prev_contrast
        );
        config_from_yaml["keep_prev_brightness_contrast"] = true;
    }

    if (config_from_yaml["store_data"].IsDefined()) {
        SPDLOG_INFO("Migrating old config: store_data -> with_image_data");
        config_from_yaml["with_image_data"] = config_from_yaml["store_data"];
        config_from_yaml.remove("store_data");
    }

    if (config_from_yaml["shortcuts"]["add_point_to_edge"].IsDefined()) {
        SPDLOG_INFO("Migrating old config: removing shortcuts.add_point_to_edge");
        config_from_yaml["shortcuts"].remove("add_point_to_edge");
    }

    //if (model_name := config_from_yaml.get("ai", {}).get("default")) and (
    //    m := re.match(r"^SegmentAnything \((.*)\)$", model_name)
    //):
    //    model_name_new: str = f"Sam ({m.group(1)})"
    //    logger.info(
    //        "Migrating old config: ai.default={!r} -> ai.default={!r}",
    //        model_name,
    //        model_name_new,
    //    )
    //    config_from_yaml["ai"]["default"] = model_name_new

    // Migrate polygon shortcut keys to shape
    std::map<std::string, std::string> POLYGON_TO_SHAPE_RENAMES = {
        {"edit_polygon", "edit_shape"},
        {"delete_polygon", "delete_shape"},
        {"duplicate_polygon", "duplicate_shape"},
        {"copy_polygon", "copy_shape"},
        {"paste_polygon", "paste_shape"},
        {"show_all_polygons", "show_all_shapes"},
        {"hide_all_polygons", "hide_all_shapes"},
        {"toggle_all_polygons", "toggle_all_shapes"},
    };
    //shortcuts = config_from_yaml["shortcuts"];
    for (auto [old_key, new_key] : POLYGON_TO_SHAPE_RENAMES) {
        if (config_from_yaml["shortcuts"][old_key].IsDefined() && !config_from_yaml["shortcuts"][new_key].IsDefined()) {
            SPDLOG_INFO(
                "Migrating old config: shortcuts.{} -> shortcuts.{}",
                old_key,
                new_key
            );
            config_from_yaml["shortcuts"][new_key] = config_from_yaml["shortcuts"][old_key];
            config_from_yaml["shortcuts"].remove(old_key);
        }
    }
}


std::string get_user_config_file(bool create_if_missing=true) {
    //user_config_file: str = osp.join(osp.expanduser("~"), ".labelmerc")
    //if not osp.exists(user_config_file) and create_if_missing:
    //    try:
    //        with open(user_config_file, "w") as f:
    //            f.write(
    //                "# Labelme config file.\n"
    //                "# Only add settings you want to override.\n"
    //                "# For all available options and defaults, see:\n"
    //                "#   https://github.com/wkentaro/labelme/blob/main/labelme/config/default_config.yaml\n"
    //                "#\n"
    //                "# Example:\n"
    //                "# with_image_data: true\n"
    //                "# auto_save: false\n"
    //                "# labels: [cat, dog]\n"
    //            )
    //    except Exception:
    //        logger.warning("Failed to save config: {!r}", user_config_file)
    return "c:/Users/njtl007/.labelmerc"; //user_config_file;
}

YAML::Node TlConfig::load_config(const std::string &config_file, const YAML::Node &config_overrides) {
    YAML::Node config;
    try {
        QString content;
        QFile file(":/config/default_config.yaml");
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            content = in.readAll();
        }
        config = YAML::Load(content.toStdString());
    } catch (const YAML::Exception &e) {
        SPDLOG_ERROR("Load default config fault: {}", e.what());
    }

    if (!config_file.empty()) {
        YAML::Node config_from_yaml;
        try {
            config_from_yaml = YAML::LoadFile(config_file);
        } catch (const YAML::Exception &e) {
            SPDLOG_ERROR("Load user config fault: {}", e.what());
        }

        migrate_config_from_file(config_from_yaml);
        update_dict(config, config_from_yaml, validate_config_item);
    }

    update_dict(config, config_overrides, validate_config_item);

    if (config["labels"].IsDefined() && !config["validate_label"].IsDefined()) {
        throw std::invalid_argument("labels must be specified when validate_label is enabled");
    }

    return config;
}