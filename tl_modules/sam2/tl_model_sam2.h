#ifndef __INC_MODEL_SAM2_H
#define __INC_MODEL_SAM2_H

#include "base/sam_base.h"

class Sam2: public SamBase {
public:
    ImageEmbedding encode_image(const cv::Mat &image) override {
        return compute_image_embedding_from_image(sessions_["encoder"], image);
    }

    cv::Mat generate_mask_from_image_embedding(ImageEmbedding &image_embedding, const Prompt &prompt) override {
        const int32_t input_size = get_input_size(sessions_["encoder"]);
        return generate_mask_from_image_embedding(sessions_["decoder"], image_embedding, prompt, input_size);
    }

private:
    ImageEmbedding compute_image_embedding_from_image(InferenceSession &session, const cv::Mat &image);
    cv::Mat generate_mask_from_image_embedding(InferenceSession &session, ImageEmbedding &image_embedding, const Prompt &prompt, int32_t input_size);
    int32_t get_input_size(const InferenceSession &session) const;
};

class Sam2Tiny: public Sam2 {
public:
    explicit Sam2Tiny() {
        name_ = "sam2:tiny";
        blobs_ = {
            {
                "encoder", Blob(
                    "https://github.com/wkentaro/osam/releases/download/sam2.1/sam2.1_tiny_preprocess.onnx",
                    "sha256:5557482c56565f6a6c8206874b1a11c392cef8a1766477bf035b919092f2b619"
                )
            },
            {
                "decoder", Blob(
                    "https://github.com/wkentaro/osam/releases/download/sam2.1/sam2.1_tiny.onnx",
                    "sha256:11a2c86fabbea9d0268213a9205c99a7f7e379caa0493bd13f5cca8ffaae6777"
                )
            },
        };
    }
};

class Sam2Small: public Sam2 {
public:
    explicit Sam2Small() {
        name_ = "sam2:small";
        blobs_ = {
            {
                "encoder", Blob(
                    "https://github.com/wkentaro/osam/releases/download/sam2.1/sam2.1_small_preprocess.onnx",
                    "sha256:06016c6dfb146ce10e4dadfdf49e88a05c8d1f97a6b7c57e150e60d2d46a72e7"
                )
            },
            {
                "decoder", Blob(
                "https://github.com/wkentaro/osam/releases/download/sam2.1/sam2.1_small.onnx",
                "sha256:153aaef5047a3b95285d90cbb39dad6c7b5821bfd944dbf56483f3f735936941"
                )
            },
        };
    }
};

class Sam2BasePlus: public Sam2 {
public:
    explicit Sam2BasePlus() {
        name_ = "sam2:latest";
        blobs_ = {
            {
                "encoder", Blob(
                   "https://github.com/wkentaro/osam/releases/download/sam2.1/sam2.1_base_plus_preprocess.onnx",
                   "sha256:ce95c44082b4532c25ae01e11da3c9337dab7b04341455c09ae599dc9ae5c438"
                )
            },
            {
                "decoder", Blob(
                "https://github.com/wkentaro/osam/releases/download/sam2.1/sam2.1_base_plus.onnx",
                "sha256:2ad091af889b20ad2035503b4355cd8924fcf0e29fa6536924c48dc220ecdc56"
                )
            },
        };
    }
};

class Sam2Large: public Sam2 {
public:
    explicit Sam2Large() {
        name_ = "sam2:large";
        blobs_ = {
            {
                "encoder", Blob(
                   "https://github.com/wkentaro/osam/releases/download/sam2.1/sam2.1_large_preprocess.onnx",
                   "sha256:ab676f957528918496990f242163fd6b41a7222ae255862e846d9ab35115c12e"
                )
            },
            {
                "decoder", Blob(
                    "https://github.com/wkentaro/osam/releases/download/sam2.1/sam2.1_large.onnx",
                    "sha256:a3ebc6b8e254bd4ca1346901b9472bc2fae9e827cfd67d67e162d0ae2b1ec9a0"
                )
            },
        };
    }
};
#endif //__INC_MODEL_SAM2_H