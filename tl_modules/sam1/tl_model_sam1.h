#ifndef __INC_MODEL_SAM1_H
#define __INC_MODEL_SAM1_H

#include "base/sam_base.h"

class Sam1 : public SamBase {
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

class Sam100m : public Sam1 {
public:
    explicit Sam100m() {
        name_ = "sam:100m";
        blobs_ = {
            {
                "encoder", Blob(
                   "https://github.com/wkentaro/labelme/releases/download/sam-20230416/sam_vit_b_01ec64.quantized.encoder.onnx",
                   "sha256:3346b9cc551c9902fbf3b203935e933592b22e042365f58321c17fc12641fd6a"
                )
            },
            {
                "decoder", Blob(
                    "https://github.com/wkentaro/labelme/releases/download/sam-20230416/sam_vit_b_01ec64.quantized.decoder.onnx",
                    "sha256:edbcf1a0afaa55621fb0abe6b3db1516818b609ea9368f309746a3afc68f7613"
                )
            },
        };
    }
};

class Sam300m : public Sam1 {
public:
    explicit Sam300m() {
        name_ = "sam:300m";
        blobs_ = {
            {
                "encoder", Blob(
                   "https://github.com/wkentaro/labelme/releases/download/sam-20230416/sam_vit_l_0b3195.quantized.encoder.onnx",
                   "sha256:f7158a4a1fe7f670ef47ea2f7f852685425c1ed6caa40f5df86cbe2b0502034f"
                )
            },
            {
                "decoder", Blob(
                    "https://github.com/wkentaro/labelme/releases/download/sam-20230416/sam_vit_l_0b3195.quantized.decoder.onnx",
                    "sha256:552ebb23bf52c5e5b971ac710d1eb8dccfd88b36cc6aff881d1536d1662e6d7b"
                )
            },
        };
    }
};

class Sam600m : public Sam1 {
public:
    explicit Sam600m() {
        name_ = "sam:latest";
        blobs_ = {
            {
                "encoder", Blob(
                   "https://github.com/wkentaro/labelme/releases/download/sam-20230416/sam_vit_h_4b8939.quantized.encoder.onnx",
                   "sha256:a5c745fd4279efc5e5436b412200e983dafc2249ce172af6cc0002a71bb5f485"
                )
            },
            {
                    "decoder", Blob(
                    "https://github.com/wkentaro/labelme/releases/download/sam-20230416/sam_vit_h_4b8939.quantized.decoder.onnx",
                    "sha256:020b385a45ffe51097e1acd10cd791075a86171153505f789a793bc382eef210"
                )
            },
        };
    }
};
#endif //__INC_MODEL_SAM1_H