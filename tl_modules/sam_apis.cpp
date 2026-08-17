#include "sam_apis.h"


SamApis &SamApis::instance() {
    static SamApis sam_apis;
    return sam_apis;
}

void SamApis::register_model(const std::string &name, const FactoryFunc &factory) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (registry_.contains(name)) {
        throw std::runtime_error("Model already registered: " + name);
    }
    registry_.emplace(name, std::move(factory));
}

Model *SamApis::get_model_by_name(const std::string &name) {
    {
        // 并发读取缓存
        std::lock_guard<std::mutex> lock(mutex_);
        if (const auto it = instances_.find(name); it != instances_.end()) {
            return it->second.get();
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    {
        // 二次检查缓存
        if (const auto it = instances_.find(name); it != instances_.end()) {
            return it->second.get();
        }
    }

    // 检查注册表
    const auto it = registry_.find(name);
    if (it == registry_.end()) {
        SPDLOG_ERROR("Model not registered: {}", name);
        std::cerr << "Model not registered: " << name << std::endl;
        throw std::runtime_error("Model not registered: " + name);
    }

    // 创建并初始化实例
    auto instance = it->second();
    if (name != instance->name_) {
        throw std::runtime_error(std::format("Different register name[{}] and model name[{}]", name, instance->name_));
    }
    instance->init();
    instances_[name] = std::move(instance);
    return instances_[name].get();
}

void SamApis::unregister_all(const std::string &name) {
    std::lock_guard<std::mutex> lock(mutex_);
    registry_.clear();
    instances_.clear();
}