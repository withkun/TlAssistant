#include "mainwindow.h"

#include <QApplication>
#include <QLoggingCategory>
#include <QTranslator>
#include <QLocale>
#include <QScreen>
#include <QMessageBox>
#include <QStyleFactory>
#include <QQuickStyle>

#include "spdlog/spdlog.h"
#include "spdlog/async.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/rotating_file_sink.h"

#include "opencv2/opencv.hpp"
#include "opencv2/core/utils/logger.hpp"
#include "config/app_config.h"
#include "config/tl_yaml_config.h"


const std::string args{
    "{app_config   | app_config.json    | application config file                   }"
    "{file_name    |                    | file name for open                        }"
    "{output_dir   |                    | result output directory                   }"
    "{console      | true               | show log console                          }"
};

// 初始化日志系统
static void slogInit(const bool console) {
    std::vector<spdlog::sink_ptr> sinks;
    try {
        // 循环日志rotating_sink
        const std::string logFile("tl_assistant.log");
        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logFile, 50*1024*1024, 100, false));
        // 控制台日志console_sink
        if (console) {
            sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        }
    } catch (const spdlog::spdlog_ex &ex) {
        const QString err = QString::fromStdString(ex.what());
        const QString msg = QObject::tr("Can not create log file: %1\nProgram exit...").arg(err);
        QMessageBox::critical(nullptr, qApp->applicationName(), msg, QMessageBox::StandardButton::Ok);
        std::exit(-1);
    }

    // 创建异步日志
    spdlog::init_thread_pool(64, 1);
    const auto logger = std::make_shared<spdlog::async_logger>("TLA", sinks.begin(), sinks.end(), spdlog::thread_pool());
    spdlog::register_logger(logger);
    spdlog::set_default_logger(logger);
    spdlog::flush_on(spdlog::level::info);
    spdlog::flush_every(std::chrono::milliseconds(10));
    spdlog::set_pattern("%^[%Y-%m-%dT%T.%f,%L,%t,%s:%#:%!]%$ %v");
    spdlog::set_level(spdlog::level::info);

    // 打印输出测试
    SPDLOG_INFO("\xE7\xA8\x8B\xE5\xBA\x8F\xE5\x90\xAF\xE5\x8A\xA8 ...");    // 程序启动UTF8编码, 控制台不应显示乱码
    SPDLOG_INFO("Program started ...");
}

void qMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    const static std::map<QtMsgType, spdlog::level::level_enum> levels {
        {QtFatalMsg,    spdlog::level::critical},
        {QtCriticalMsg, spdlog::level::critical},
        {QtWarningMsg,  spdlog::level::warn},
        {QtInfoMsg,     spdlog::level::info},
        {QtDebugMsg,    spdlog::level::trace},
    };

    const auto it = levels.find(type);
    const auto lv = it != levels.end() ? it->second :  spdlog::level::err;

    // 输出日志到 spdlog
    const auto logger = spdlog::get("TLA");

    const QByteArray &localMsg = msg.toLocal8Bit();
    logger->log(spdlog::source_loc{context.file, context.line, context.function}, lv, localMsg.constData());
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // 解析命令行参数
    cv::CommandLineParser parser(argc, argv, args);
    cv::utils::logging::setLogLevel(cv::utils::logging::LogLevel::LOG_LEVEL_INFO);
    AppConfig &appConfig = AppConfig::instance();
    appConfig.load();

    slogInit(parser.get<bool>("console"));
    qInstallMessageHandler(qMessageHandler);
    QLoggingCategory::defaultCategory()->setEnabled(QtInfoMsg, true);
    QLoggingCategory::defaultCategory()->setEnabled(QtDebugMsg, true);

    QLocale locale;
    QTranslator translator;
    qInfo() << translator.load(":/i18n/zh_CN.qm", ":/i18n/");
    QApplication::installTranslator(&translator);

    qInfo() << QStyleFactory::keys();
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QQuickStyle::setStyle("Fusion");

    QString config_file;
    YAML::Node config_overrides;
    if (!appConfig.config_file_.empty()) {
        try {
            config_file = QString::fromStdString(appConfig.config_file_);
        } catch (const YAML::BadFile &e) {
        }
    }

    QString file_name;
    const auto v_file_name = parser.get<std::string>("file_name");
    if (!v_file_name.empty()) {
        file_name = QString::fromStdString(v_file_name);
    }
    QString output_dir;
    const auto v_output_dir = parser.get<std::string>("output_dir");
    if (!v_output_dir.empty()) {
        output_dir = QString::fromStdString(v_output_dir);
    }

    MainWindow win(config_file, config_overrides, file_name, output_dir);
    win.show();

    return QApplication::exec();
}