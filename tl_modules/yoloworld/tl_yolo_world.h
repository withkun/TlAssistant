#ifndef __INC_YOLO_WORLD_H
#define __INC_YOLO_WORLD_H

#include "base/model.h"


class YoloWorld : public Model {
public:
    ImageEmbedding encode_image(const cv::Mat &image) override;
    GenerateResponse generate(GenerateRequest &request) override;

    int32_t TOKEN_W_ = 77;
    int32_t input_size_ = 640;
    explicit YoloWorld() {
        name_ = "yoloworld:latest";
        blobs_ = {
            {
                "textual", Blob(
                   "https://clip-as-service.s3.us-east-2.amazonaws.com/models/onnx/ViT-B-32/textual.onnx",
                   "sha256:55c85d8cbb096023781c1d13c557eb95d26034c111bd001b7360fdb7399eec68"
                )
            },
            {
                "yolo", Blob(
                    "https://github.com/wkentaro/yolo-world-onnx/releases/download/v0.1.0/yolo_world_v2_xl_vlpan_bn_2e-3_100e_4x8gpus_obj365v1_goldg_train_lvis_minival.onnx",
                    "sha256:92660c6456766439a2670cf19a8a258ccd3588118622a15959f39e253731c05d"
                )
            },
            {
                "nms", Blob(
                    "https://github.com/wkentaro/yolo-world-onnx/releases/download/v0.1.0/non_maximum_suppression.onnx",
                    "sha256:328310ba8fdd386c7ca63fc9df3963cc47b1268909647abd469e8ebdf7f3d20a"
                )
            },
        };
    }

private:
    ImageEmbedding encode_token(InferenceSession &session, const Tokens &texts);
    ImageEmbedding decode_image(InferenceSession &session, const cv::Mat &image, ImageEmbedding &token_embedding);
    GenerateResponse non_maximum_suppression(InferenceSession &session, ImageEmbedding &image_embedding, Prompt &prompt);

    cv::Mat transform_image(const cv::Mat &image, int32_t input_size, int32_t &offset_y, int32_t &offset_x);
    cv::Mat transform_boxes(const cv::Mat &boxes, int32_t input_size, int32_t image_h, int32_t image_w, int32_t offset_y, int32_t offset_x);
};
#endif //__INC_YOLO_WORLD_H