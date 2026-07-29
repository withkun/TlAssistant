#include "ort_logging.h"

#include <map>
#include <regex>


//OrtLoggingFunction
//void* param: 用户自定义数据的指针, 可以用来传递上下文信息或配置选项给日志处理函数
//int level: 日志的级别
//const char* category: 日志的类别, 如"onnxruntime", 这有助于区分不同的日志来源或组件
//const char* logid: 与日志消息相关的唯一标识符或标签, 这可以用于跟踪或关联特定的日志事件
//const char* code_location: 产生日志消息的代码位置, 如文件名和行号, 这有助于快速定位问题代码
//const char* message: 实际的日志消息字符串, 包含了要记录的详细信息
void LoggingFunction(void *param, OrtLoggingLevel severity, const char *category, const char *logid, const char *code_location, const char *message) {
    const static std::map<OrtLoggingLevel, spdlog::level::level_enum> levels {
        {ORT_LOGGING_LEVEL_FATAL,   spdlog::level::critical},
        {ORT_LOGGING_LEVEL_ERROR,   spdlog::level::err},
        {ORT_LOGGING_LEVEL_WARNING, spdlog::level::warn},
        {ORT_LOGGING_LEVEL_INFO,    spdlog::level::info},
        {ORT_LOGGING_LEVEL_VERBOSE, spdlog::level::trace},
    };

    const auto it = levels.find(severity);
    const auto lv = it != levels.end() ? it->second :  spdlog::level::err;

    // 输出日志到 spdlog
    const auto logger = spdlog::default_logger_raw();

    // 日志乱码问题
    // https://blog.csdn.net/gitblog_01168/article/details/151431899
    thread_local std::string::size_type val_index = 0;
    thread_local std::string::size_type max_count = 128;
    thread_local std::vector<int32_t> line_num(max_count);
    thread_local std::vector<std::string> file_name(max_count);
    thread_local std::vector<std::string> func_name(max_count);

    // code_location格式:
    // session_state.cc:1316 onnxruntime::VerifyEachNodeIsAssignedToAnEp
    std::smatch smatch;
    const std::string location(code_location);
    const std::regex pattern("^(.+):(\\d+) (.+)$");
    if (std::regex_search(location, smatch, pattern)) {
        file_name[val_index] = smatch.str(1);
        line_num [val_index] = std::strtol(smatch.str(2).data(), nullptr, 10);
        func_name[val_index] = smatch.str(3);
        logger->log(spdlog::source_loc{file_name[val_index].data(), line_num[val_index], func_name[val_index].data()}, lv, message);
    } else {
        logger->log(spdlog::source_loc{code_location, __LINE__, nullptr}, lv, message);
    }

    ++val_index;
    if (val_index >= max_count) {
        val_index = 0;
    }
}