#ifndef __INC_EFFICIENT_SAM_H
#define __INC_EFFICIENT_SAM_H

#include "base/sam_base.h"


class EfficientSam : public SamBase {
public:
    ImageEmbedding encode_image(const cv::Mat &image) override {
        return compute_image_embedding_from_image(sessions_["encoder"], image);
    }
    cv::Mat generate_mask_from_image_embedding(ImageEmbedding &image_embedding, const Prompt &prompt) override {
        return generate_mask_from_image_embedding(sessions_["decoder"], image_embedding, prompt);
    }

private:
    ImageEmbedding compute_image_embedding_from_image(InferenceSession &session, const cv::Mat &image);
    cv::Mat generate_mask_from_image_embedding(InferenceSession &session, ImageEmbedding &image_embedding, const Prompt &prompt);
};

class EfficientSam10m : public EfficientSam {
public:
    EfficientSam10m() {
        name_ = "efficientsam:10m";
        blobs_ = {
            {
                "encoder", Blob(
                    "https://github.com/labelmeai/efficient-sam/releases/download/onnx-models-20231225/efficient_sam_vitt_encoder.onnx",
                    "sha256:7a73ee65aa2c37237c89b4b18e73082f757ffb173899609c5d97a2bbd4ebb02d"
                )
            },
            {
                "decoder", Blob(
                    "https://github.com/labelmeai/efficient-sam/releases/download/onnx-models-20231225/efficient_sam_vitt_decoder.onnx",
                    "sha256:e1afe46232c3bfa3470a6a81c7d3181836a94ea89528aff4e0f2d2c611989efd"
                )
            },
        };
    }
};

class EfficientSam30m : public EfficientSam {
public:
    EfficientSam30m() {
        name_ = "efficientsam:latest";
        blobs_ = {
            {
                "encoder", Blob(
                   "https://github.com/labelmeai/efficient-sam/releases/download/onnx-models-20231225/efficient_sam_vits_encoder.onnx",
                   "sha256:4cacbb23c6903b1acf87f1d77ed806b840800c5fcd4ac8f650cbffed474b8896"
                )
            },
            {
                "decoder", Blob(
                "https://github.com/labelmeai/efficient-sam/releases/download/onnx-models-20231225/efficient_sam_vits_decoder.onnx",
                "sha256:4727baf23dacfb51d4c16795b2ac382c403505556d0284e84c6ff3d4e8e36f22"
                )
            },
        };
    }
};
#endif //__INC_EFFICIENT_SAM_H