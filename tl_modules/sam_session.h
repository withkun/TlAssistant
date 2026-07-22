#ifndef __INC_SAM_SESSION_H
#define __INC_SAM_SESSION_H

#include "base/model.h"

using EmbeddingCache = std::deque<std::pair<size_t, ImageEmbedding>>;

class SamSession {
public:
    explicit SamSession(std::string name, int32_t cache_size = 3);
    ~SamSession() = default;

    GenerateResponse run(const cv::Mat &image, size_t image_id,
                         const std::vector<cv::Point2f> &point_coords, const std::vector<float> &point_labels,
                         const std::vector<std::string> &texts = {});

    [[nodiscard]] std::string model_name() const { return model_name_; }

protected:
    ImageEmbedding get_or_compute_embedding(const cv::Mat &image, size_t image_id);
    Model *get_or_load_model();

    std::string                 model_name_;
    Model                      *model_{nullptr};
    int32_t                     cache_size_;

private:
    Ort::MemoryInfo             memoryInfo_{nullptr};
    EmbeddingCache              embedding_cache_;
    void CloneOrtValue(Ort::Value &tensor, Ort::Value &value) const;
};
#endif //__INC_SAM_SESSION_H