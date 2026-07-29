#ifndef __INC_BBOX_FROM_TEXT_H
#define __INC_BBOX_FROM_TEXT_H

#include "base/types.h"
#include "sam_session.h"
#include "tl_widgets/tl_shape.h"


namespace bbox_from_text {
void get_bboxes_from_texts(
    const GenerateResponse &response,
    std::vector<float> &scores,
    std::vector<int32_t> &labels,
    std::vector<cv::Rect> &boxes,
    std::vector<cv::Mat> &masks
);// -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray | None]:

//def nms_bboxes(
//    boxes: np.ndarray,
//    scores: np.ndarray,
//    labels: np.ndarray,
//    iou_threshold: float,
//    score_threshold: float,
//    max_num_detections: int,
//) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:

QList<TlShape> get_shapes_from_bboxes(
//    boxes: np.ndarray,
//    scores: np.ndarray,
//    labels: np.ndarray,
//    texts: list[str],
//    masks: NDArray[np.bool_] | None,
//    shape_type: Literal["rectangle", "polygon", "mask"],
);

QList<TlShape> get_shapes_from_texts(
    SamSession *sam_session,
    const cv::Mat &image, size_t image_id, const std::vector<std::string> &texts
);
} //namespace bbox_from_text
#endif //__INC_BBOX_FROM_TEXT_H