#ifndef __INC_NON_MAXIMUM_H
#define __INC_NON_MAXIMUM_H

#include "base/model.h"


std::tuple<std::vector<BoundingBoxF>, std::vector<float>, std::vector<int32_t>, std::vector<int32_t>>
non_maximum_suppression(
    std::vector<BoundingBoxF> boxes,    //: NDArray[np.floating],
    std::vector<float> scores,          //: NDArray[np.floating],
    float iou_threshold,
    float score_threshold,
    int max_num_detections
);

class NonMaximum : public Model {
public:
    explicit NonMaximum() {
        name_ = "nms";
        blobs_ = {
            {
                "textual", Blob(
                   "https://github.com/wkentaro/yolo-world-onnx/releases/download/v0.1.0/non_maximum_suppression.onnx",
                   "sha256:328310ba8fdd386c7ca63fc9df3963cc47b1268909647abd469e8ebdf7f3d20a"
                )
            },
        };
    }

    void non_maximum_suppression(
        std::vector<BoundingBox> boxes,     //: NDArray[np.floating],
        std::vector<float> scores,          //: NDArray[np.floating],
        float iou_threshold,
        float score_threshold,
        int max_num_detections
    );

};
#endif //__INC_NON_MAXIMUM_H