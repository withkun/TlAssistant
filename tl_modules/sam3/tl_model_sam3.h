#ifndef __INC_MODEL_SAM3_H
#define __INC_MODEL_SAM3_H

#include "base/model.h"

class Sam3 : public Model {
public:
    ImageEmbedding encode_image(const cv::Mat &image) override;
    GenerateResponse generate(GenerateRequest &request) override;

    explicit Sam3() {
        name_ = "sam3:latest";
        blobs_ = {
            {
                "image_encoder", Blob{
                   "https://hf-mirror.com/wkentaro/sam3-onnx-models-v0.3.0/resolve/main/sam3_image_encoder.onnx",
                   "sha256:c6f3769e9d42573806c34b663d6100b3827a4c0ecba6f45dbf39b8f3906be725",
                    {
                        "https://hf-mirror.com/wkentaro/sam3-onnx-models-v0.3.0/resolve/main/sam3_image_encoder.onnx.data",
                        "sha256:03bd50b0703e2b04e2193ca831b7f9d5ecf40bc5287cc59b1970f56ab800c995"
                    }
                }
            },
            {
                "language_encoder", Blob{
                    "https://hf-mirror.com/wkentaro/sam3-onnx-models-v0.3.0/resolve/main/sam3_language_encoder.onnx",
                    "sha256:b3b465935c9bf4cf5efd950589741f3da5eec9e9bfe459576989cbe565331b53",
                    {
                        "https://hf-mirror.com/wkentaro/sam3-onnx-models-v0.3.0/resolve/main/sam3_language_encoder.onnx.data",
                        "sha256:1b03dabc657c1f2887d1b8ce2a3537467d4c033ea1f8c8be141fbca55e4b95f7"
                    }
                }
            },
            {
                "decoder", Blob{
                    "https://hf-mirror.com/wkentaro/sam3-onnx-models-v0.3.0/resolve/main/sam3_decoder.onnx",
                    "sha256:bbc216dbf3de4742f692c2a0a0fd215ea8957956002a05f28150040a88b5dc1a"
                }
            },
        };
    }
};
#endif //__INC_MODEL_SAM3_H