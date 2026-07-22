#include "model.h"
#include "ort_logging.h"
#include <ranges>


void InferenceSession::RunInference() {
    std::vector<const char *> input_names;
    std::vector<const char *> output_names;
    for (size_t i = 0; i < input_names_.size(); ++i) {
        input_names.emplace_back(input_names_[i].data());
        const auto &shapeInfo = input_tensors_[i].GetTensorTypeAndShapeInfo();
        SPDLOG_INFO("===> input: {}, type: {}, shape: {}, elements: {}", input_names_[i], shapeInfo.GetElementType(), shapeInfo.GetShape(), shapeInfo.GetElementCount() );
    }
    for (size_t i = 0; i < output_names_.size(); ++i) {
        output_names.emplace_back(output_names_[i].data());
        output_tensors_[i] = nullptr;
    }

    Ort::RunOptions runOptions_;
    runOptions_.SetRunLogSeverityLevel(ORT_LOGGING_LEVEL_WARNING);
    runOptions_.SetRunLogVerbosityLevel(ORT_LOGGING_LEVEL_WARNING);

    try {
        const auto enter_ms = std::chrono::system_clock::now();
        session_->Run(runOptions_, input_names.data(), input_tensors_.data(), input_names_.size(), output_names.data(), output_tensors_.data(), output_names_.size());
        for (size_t i = 0; i < output_tensors_.size(); ++i) {
            const auto tensorInfo = output_tensors_[i].GetTensorTypeAndShapeInfo();
            SPDLOG_INFO("===> output: {}, type: {}, shape: {}, elements: {}", output_names_[i], tensorInfo.GetElementType(), tensorInfo.GetShape(), tensorInfo.GetElementCount());
        }

        const auto leave_ms = std::chrono::system_clock::now();
        int64_t usage_ms =  std::chrono::duration_cast<std::chrono::microseconds>(leave_ms - enter_ms).count();
        SPDLOG_INFO("Run success, usage: {}μs", usage_ms);
    } catch (const Ort::Exception &e) {
        SPDLOG_INFO("Got Exception: {}", e.what());
    } catch (const std::exception &e) {
        SPDLOG_INFO("Got Exception: {}", e.what());
    } catch (const char *e) {
        SPDLOG_INFO("Got Exception: {}", e);
    } catch (...) {
        SPDLOG_INFO("Got Exception...");
    }
}

class Model::Impl {
public:
    Impl();

    InferenceSession load_session(const Blob &blob);

private:
    Ort::Env                                    env_;    //不能用临时变量, 初始化Session为变量引用.
    Ort::SessionOptions                         sessionOptions_;
    Ort::MemoryInfo                             memoryInfo_{nullptr};
};

Model::Impl::Impl() : env_{ORT_LOGGING_LEVEL_WARNING, "SAM", LoggingFunction, nullptr} {
    sessionOptions_.SetLogSeverityLevel(ORT_LOGGING_LEVEL_WARNING);
    sessionOptions_.SetIntraOpNumThreads(4);
    // 设置图优化级别为最高级别,这里使用最大优化
    sessionOptions_.SetGraphOptimizationLevel(ORT_ENABLE_BASIC);
    //sessionOptions_.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
    sessionOptions_.SetGraphOptimizationLevel(ORT_ENABLE_EXTENDED);
    //const OrtStatus &status = *OrtSessionOptionsAppendExecutionProvider_CUDA(sessionOptions_, 0);
    //SPDLOG_INFO("Append provider: {}", status.GetErrorMessage());

    memoryInfo_ = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
}

InferenceSession Model::Impl::load_session(const Blob &blob) {
    InferenceSession session;
    try {
        const std::string model_file = blob.path();
        const auto wModelFile = std::wstring(model_file.begin(), model_file.end());
        session.session_ = std::make_shared<Ort::Session>(env_, wModelFile.data(), sessionOptions_);
        // 注意: 此接口输入的是模型数据, 不是模型路径, 如下使用会将文件路径字符串当作ONNX模型的二进制内容解析产生错误.
        //session.session_ = std::make_shared<Ort::Session>(env_, model_file.data(), model_file.size(), sessionOptions_);
    } catch (const Ort::Exception &exp) {
        SPDLOG_INFO("load model fail: {}, error: {}", blob.path(), exp.what());
        std::cerr << "load model fail: " << blob.path() << ", error: " << exp.what() << std::endl;
        throw std::runtime_error("load model fail: " + blob.path() + ", error: " + exp.what());
    }

    const Ort::AllocatorWithDefaultOptions allocator;
    for (size_t i = 0; i < session.session_->GetInputCount(); ++i) {
        const auto tensorName = session.session_->GetInputNameAllocated(i, allocator);
        session.input_names_.emplace_back(tensorName.get());
        const auto typeInfo = session.session_->GetInputTypeInfo(i);
        const auto shapeInfo = typeInfo.GetTensorTypeAndShapeInfo();
        session.input_tensors_.emplace_back(nullptr);
        SPDLOG_INFO("Input Name: {}, Dimensions: {}, Type: {}",  session.input_names_.back(), shapeInfo.GetShape(), shapeInfo.GetElementType());
    }

    for (size_t i = 0; i < session.session_->GetOutputCount(); ++i) {
        const auto tensorName = session.session_->GetOutputNameAllocated(i, allocator);
        session.output_names_.emplace_back(tensorName.get());
        const auto typeInfo = session.session_->GetOutputTypeInfo(i);
        const auto shapeInfo = typeInfo.GetTensorTypeAndShapeInfo();
        session.output_tensors_.emplace_back(nullptr);
        SPDLOG_INFO("Output Name: {}, Dimensions: {}, Type: {}", session.output_names_.back(), shapeInfo.GetShape(), shapeInfo.GetElementType());
    }

    return session;
}

Model::Model() : impl_(new Impl) {
    memoryInfo_ = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
    SPDLOG_INFO("===> create model: {}", static_cast<void *>(this));
}

Model::~Model() {
    SPDLOG_INFO("===> destroy model: {}", static_cast<void *>(this));
}

void Model::init() {
    this->pull();
    for (const auto &[key, blob] : blobs_) {
        const auto enter_ms = std::chrono::system_clock::now();
        this->sessions_[key] = impl_->load_session(blob);
        const auto leave_ms = std::chrono::system_clock::now();
        int64_t usage_ms =  std::chrono::duration_cast<std::chrono::microseconds>(leave_ms - enter_ms).count();
        SPDLOG_INFO("Load {} success, usage: {}μs", key, usage_ms);
    }
}

void Model::pull() {
    // 下载模型文件, 当前保存在本地, 暂时不需要下载过程.
    for (auto &blob : blobs_ | std::views::values) {
        blob.pull();
    }
}