#ifndef __INC_APP_CONFIG_H
#define __INC_APP_CONFIG_H

#include <string>
#include <memory>


class AppConfig {
public:
    static AppConfig &instance();

    void load() const;
    void save() const;

private:
    AppConfig();
    ~AppConfig() = default;

public:
    std::string         config_file_;                       //
    std::string         spd_log_level_;                     // 日志输出级别
    std::string         last_work_dir_;                     // 上次工作路径
    std::string         ai_assist_name_{"sam2:latest"};     // 分割模型名称
    std::string         ai_prompt_name_{"sam3:latest"};     // 分割模型名称
    float               iou_threshold_{0.5f};               //
    float               score_threshold_{0.1f};             // 置信度阈值
    int64_t             max_annotations_{1000};             //

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
#endif // __INC_APP_CONFIG_H