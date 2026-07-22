#include "sam_session.h"
#include "sam_apis.h"


SamSession::SamSession(std::string name, const int32_t cache_size) : model_name_(std::move(name)), cache_size_(cache_size) {
    memoryInfo_ = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
}

GenerateResponse SamSession::run(const cv::Mat &image, const size_t image_id,
                                 const std::vector<cv::Point2f> &point_coords, const std::vector<float> &point_labels,
                                 const std::vector<std::string> &texts) {
    Prompt prompt;
    if (!point_coords.empty() && !point_labels.empty()) {
        prompt = Prompt(point_coords, point_labels);
    } else if (!texts.empty()) {
        prompt = Prompt(texts, 0.5, 0.1, 1000);
    } else {
        throw std::runtime_error("Either points and point_labels, or texts must be provided.");
    }

    cv::Mat dest = image;
    if (image.type() == CV_8UC1) {
        cv::cvtColor(image, dest, cv::COLOR_GRAY2BGR);
    }
    auto image_embedding = get_or_compute_embedding(dest, image_id);
    GenerateRequest request{model_name_, dest, prompt, std::move(image_embedding)};
    return model_->generate(request);
}

ImageEmbedding SamSession::get_or_compute_embedding(const cv::Mat &image, const size_t image_id) {
    //for key, embedding in self._embedding_cache:
    //    if key == image_id:
    //        return embedding
    for (auto &[key, cache_embedding] : embedding_cache_) {
        if (key != image_id) {
            continue;
        }
        SPDLOG_INFO("Get computing embedding for cache_key={}", image_id);
        ImageEmbedding image_embedding{cache_embedding.image_h, cache_embedding.image_w};
        CloneOrtValue(cache_embedding.embedding, image_embedding.embedding);

        image_embedding.extra_features.resize(cache_embedding.extra_features.size());
        for (size_t i = 0; i < cache_embedding.extra_features.size(); ++i) {
            CloneOrtValue(cache_embedding.extra_features[i], image_embedding.extra_features[i]);
        }
        return image_embedding;
    }

    // 这里需要加载模型与图像编码耗时较长, 需要防止GUI界面假死.
    // 非GUI线程创建和操作QProgressDialog违反QT的GUI线程规则.
    // 所有GUI操作(包括QWidget及其子类的创建, 显示, 更新)必须在主线程执行.
    auto *const model = get_or_load_model();
    ImageEmbedding image_embedding = model->encode_image(image);

    SPDLOG_INFO("Set computing embedding for cache_key={}", image_id);
    //self._embedding_cache.append((image_id, embedding))
    while (embedding_cache_.size() >= cache_size_) {
        embedding_cache_.pop_front();
    }

    auto &[key, cache_embedding] = embedding_cache_.emplace_back();
    key = image_id;
    cache_embedding.image_h = image_embedding.image_h;
    cache_embedding.image_w = image_embedding.image_w;
    CloneOrtValue(image_embedding.embedding, cache_embedding.embedding);

    cache_embedding.extra_features.resize(image_embedding.extra_features.size());
    for (size_t i = 0; i < cache_embedding.extra_features.size(); ++i) {
        CloneOrtValue(image_embedding.extra_features[i], cache_embedding.extra_features[i]);
    }
    return image_embedding;
}

Model *SamSession::get_or_load_model() {
    if (model_ == nullptr) {
        // self._model = apis.get_model_type_by_name(self._model_name)  ()
        model_ = SamApis::instance().get_model_by_name(model_name_);
    }
    return model_;
}

void SamSession::CloneOrtValue(Ort::Value &tensor, Ort::Value &value)  const {
    if (tensor == nullptr || !tensor.IsTensor()) {
        return;
    }

    const auto typeShape = tensor.GetTypeInfo().GetTensorTypeAndShapeInfo();
    const auto b = tensor.GetTensorSizeInBytes();
    const auto s = typeShape.GetShape();
    const auto t = typeShape.GetElementType();
    value = Ort::Value::CreateTensor(memoryInfo_, tensor.GetTensorMutableRawData(), b, s.data(), s.size(), t);
    SPDLOG_INFO("copy image embedding: {}, element count: {}, size bytes: {}", typeShape.GetShape(), typeShape.GetElementCount(), b);
}
