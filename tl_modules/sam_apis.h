#ifndef __INC_SAM_APIS_H
#define __INC_SAM_APIS_H

#include <string>
#include <memory>
#include <mutex>

#include "base/model.h"


class SamApis {
public:
    using FactoryFunc = std::function<std::shared_ptr<Model>()>;
    static SamApis &instance();

    void register_model(const std::string &name, const FactoryFunc &factory);
    Model *get_model_by_name(const std::string &name);

    void unregister_all(const std::string &name);

private:
    SamApis() = default; // 禁止外部实例化
    mutable std::mutex                              mutex_;
    std::map<std::string, FactoryFunc>              registry_;
    std::map<std::string, std::shared_ptr<Model>>   instances_;
};

// 工具自动注册宏
#define AUTO_REGISTER_MODEL(ModelClass, RegName)       \
namespace {                                                             \
struct AutoRegister##ModelClass {                                       \
    AutoRegister##ModelClass() {                                        \
        SamApis::instance().register_model(                             \
            RegName, [] { return std::make_shared<ModelClass>(); }      \
        );                                                              \
    }                                                                   \
};                                                                      \
AutoRegister##ModelClass auto_register_##ModelClass;                    \
}
#endif //__INC_SAM_APIS_H