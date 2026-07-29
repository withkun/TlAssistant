#include "app_config.h"
#include "nlohmann/json.hpp"

#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <fstream>


class AppConfig::Impl {
public:
    explicit Impl(AppConfig *thiz) : thiz_(thiz) {
    }

    void apply(const nlohmann::json &appConfigJson);
    nlohmann::json load(bool defaultOnly);
    void save(const nlohmann::ordered_json &appConfigJson);
    nlohmann::ordered_json dump(bool defaultOnly);

    AppConfig                  *thiz_;
    nlohmann::json              initConfig_;                    //
};

nlohmann::json AppConfig::Impl::load(bool defaultOnly) {
    const auto app_config = QFileInfo(QCoreApplication::applicationDirPath() + QDir::separator() + "app_config.json");
    if (!app_config.exists()) {
        return dump(true);
    }

    const QString json_file = QDir::toNativeSeparators(app_config.absoluteFilePath());
    std::ifstream ifs(json_file.toLocal8Bit());
    if (!ifs.is_open()) {
        return dump(true);
    }

    nlohmann::json json;
    try {
        ifs >> json;
    } catch (nlohmann::json::exception ex) {
        json = dump(true);
    }
    ifs.close();
    return json;
}

void AppConfig::Impl::save(const nlohmann::ordered_json &appConfigJson) {
    const auto json_file = QCoreApplication::applicationDirPath() + QDir::separator() + "app_config.json";
    std::ofstream ofs(json_file.toLocal8Bit());
    if (!ofs.is_open()) {
        return;
    }

    ofs.width(4);
    ofs << appConfigJson;
    ofs.close();
}

void AppConfig::Impl::apply(const nlohmann::json &appConfigJson) {
    if (appConfigJson.contains("config_file")) {
        thiz_->config_file_ = appConfigJson.at("config_file");
    }
    if (appConfigJson.contains("spd_log_level")) {
        thiz_->spd_log_level_ = appConfigJson.at("spd_log_level");
    }
    if (appConfigJson.contains("last_work_dir")) {
        thiz_->last_work_dir_ = appConfigJson.at("last_work_dir");
    }

    if (appConfigJson.contains("ai_assist_name")) {
        thiz_->ai_assist_name_ = appConfigJson.at("ai_assist_name");
    }
    if (appConfigJson.contains("ai_prompt_name")) {
        thiz_->ai_prompt_name_ = appConfigJson.at("ai_prompt_name");
    }

    if (appConfigJson.contains("iou_threshold")) {
        thiz_->iou_threshold_ = appConfigJson.at("iou_threshold");
    }
    if (appConfigJson.contains("score_threshold")) {
        thiz_->score_threshold_ = appConfigJson.at("score_threshold");
    }
    if (appConfigJson.contains("max_annotations")) {
        thiz_->max_annotations_ = appConfigJson.at("max_annotations");
    }
}

nlohmann::ordered_json AppConfig::Impl::dump(bool defaultOnly) {
    nlohmann::ordered_json appConfigJson;
    appConfigJson["config_file"]        = thiz_->config_file_;
    appConfigJson["spd_log_level"]      = thiz_->spd_log_level_;
    appConfigJson["last_work_dir"]      = thiz_->last_work_dir_;
    appConfigJson["ai_assist_name"]     = thiz_->ai_assist_name_;
    appConfigJson["ai_prompt_name"]     = thiz_->ai_prompt_name_;
    appConfigJson["iou_threshold"]      = thiz_->iou_threshold_;
    appConfigJson["score_threshold"]    = thiz_->score_threshold_;
    appConfigJson["max_annotations"]    = thiz_->max_annotations_;

    return appConfigJson;
}

AppConfig &AppConfig::instance() {
    static AppConfig appConfig;
    return appConfig;
}

AppConfig::AppConfig() : impl_(new Impl(this)) {
}

void AppConfig::load() const {
    const auto appConfigJson = impl_->load(true);
    impl_->apply(appConfigJson);
}

void AppConfig::save() const {
    const auto appConfigJson = impl_->dump(true);
    impl_->save(appConfigJson);
}