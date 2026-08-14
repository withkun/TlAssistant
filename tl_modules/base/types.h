#ifndef __INC_TYPES_H
#define __INC_TYPES_H

#include "opencv2/opencv.hpp"
#include "onnxruntime_cxx_api.h"


class Prompt {
public:
    Prompt() = default;
    explicit Prompt(const std::vector<cv::Point2f> &points, const std::vector<float> &point_labels);
    explicit Prompt(const std::vector<std::string> &texts, float iou_threshold=0.5, float score_threshold=0.1, int32_t max_annotations=1000);

    bool empty() const;

public:
    std::vector<cv::Point2f>        point_coords_;
    //point_labels: ignore=-1, background=0, foreground=1, box_lt=2, box_rb=3
    std::vector<float>              point_labels_;

    std::vector<std::string>        texts_;
    float                           iou_threshold_{0.5};
    float                           score_threshold_{0.1};
    int64_t                         max_annotations_{100};
};

struct BoundingBox {
    int32_t                         x1{0};         // tl_x
    int32_t                         y1{0};         // tl_y
    int32_t                         x2{0};         // br_x
    int32_t                         y2{0};         // br_y

    explicit operator bool() const {
        return !(x1 == 0 && y1 == 0 && x2 == 0 && y2 == 0);
    }
};

struct Annotation {
    float                           score;  // Optional[float] = pydantic.Field(default=None)
    BoundingBox                     bbox;   // Optional[BoundingBox] = pydantic.Field(default=None)
    std::string                     text;   // Optional[str] = pydantic.Field(default=None)
    cv::Mat                         mask;   // Optional[np.ndarray] = pydantic.Field(default=None)
};
using Annotations = std::vector<Annotation>;

class ImageEmbedding {
public:
    int32_t                         image_h;
    int32_t                         image_w;
    Ort::Value                      embedding;
    std::vector<Ort::Value>         extra_features;
    //extra_features: list[npt.NDArray[np.float32]] = pydantic.Field(default_factory=list)
};

class GenerateRequest {
public:
    std::string                     model;                  //: str
    cv::Mat                         image;                  //: Optional[np.ndarray] = pydantic.Field(default=None)
    Prompt                          prompt;                 //: Optional[Prompt] = pydantic.Field(default=None)
    ImageEmbedding                  image_embedding;        //: Optional[ImageEmbedding] = pydantic.Field(default=None)
    std::vector<Annotation>         annotations;            //: Optional[list[Annotation]] = pydantic.Field(default=None)
};

class GenerateResponse {
public:
    std::string                     model;                  //: str
    ImageEmbedding                  image_embedding;        //: Optional[ImageEmbedding] = pydantic.Field(default=None)
    std::vector<Annotation>         annotations;            //: list[Annotation]
};
#endif //__INC_TYPES_H