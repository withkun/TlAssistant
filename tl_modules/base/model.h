#ifndef __INC_MODEL_H
#define __INC_MODEL_H

#include "blob.h"
#include "types.h"
#include "utilities.h"


struct InferenceSession {
    std::shared_ptr<Ort::Session>           session_;

    std::vector<std::string>                input_names_;
    std::vector<Ort::Value>                 input_tensors_;

    std::vector<std::string>                output_names_;
    std::vector<Ort::Value>                 output_tensors_;

    void RunInference();
};

struct DetectResult {
    float       confidence; //结果置信度
    cv::Rect2f  bbox;       //目标矩形框
    cv::Mat     mask;       //矩形框内掩码
};
using DetectResults = std::vector<DetectResult>;

using Tokens = std::vector<std::string>;

class Model {
public:
    Model();
    virtual ~Model();

    virtual ImageEmbedding encode_image(const cv::Mat &image) = 0;
    virtual GenerateResponse generate(GenerateRequest &request) = 0;

    void init();
    void pull();

    std::string                             name_;
    std::map<std::string, Blob>             blobs_;
    std::map<std::string, InferenceSession> sessions_;
    Ort::MemoryInfo                         memoryInfo_{nullptr};

private:
    class Impl;
    std::unique_ptr<Impl>                   impl_;
};
#endif //__INC_MODEL_H