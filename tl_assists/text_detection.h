#ifndef __INC_TEXT_DETECTION_H
#define __INC_TEXT_DETECTION_H

#include "tl_modules/base/types.h"
#include "tl_modules/sam_session.h"
#include "tl_widgets/tl_shape.h"


std::tuple<std::vector<BoundingBoxF>, std::vector<float>, std::vector<int32_t>, std::vector<cv::Mat>>
get_bboxes_from_texts(
    SamSession *session, const cv::Mat &image, size_t image_id, const std::vector<std::string> &texts
);

std::tuple<std::vector<BoundingBoxF>, std::vector<float>, std::vector<int32_t>, std::vector<int32_t>>
nms_bboxes(
    std::vector<BoundingBoxF> boxes,
    std::vector<float> scores,
    std::vector<int32_t> labels,
    float iou_threshold,
    float score_threshold,
    int32_t max_num_detections
);

QList<TlShape> get_shapes_from_texts(
    SamSession *session, const cv::Mat &image, size_t image_id, const std::vector<std::string> &texts
);

#endif //__INC_TEXT_DETECTION_H