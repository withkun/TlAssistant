#include "non_maximum.h"


std::tuple<std::vector<BoundingBoxF>, std::vector<float>, std::vector<int32_t>, std::vector<int32_t>>
non_maximum_suppression(
    std::vector<BoundingBoxF> boxes,    //: NDArray[np.floating],
    std::vector<float> scores,          //: NDArray[np.floating],
    const float iou_threshold,
    const float score_threshold,
    const int max_num_detections
) {
    std::vector<int32_t> labels(scores.size(), 0);
    std::vector<int32_t> indices(scores.size(), 1);

    return {boxes, scores, labels, indices};
}

void NonMaximum::non_maximum_suppression(
    std::vector<BoundingBox> boxes,     //: NDArray[np.floating],
    std::vector<float> scores,          //: NDArray[np.floating],
    const float iou_threshold,
    const float score_threshold,
    const int max_num_detections
) { //-> tuple[NDArray[np.floating], NDArray[np.floating], NDArray[np.int64], NDArray[np.int64]]:
//    global _non_maximum_suppression_inference_session
//    if _non_maximum_suppression_inference_session is None:
//        blob = types.Blob(
//            url="https://github.com/wkentaro/yolo-world-onnx/releases/download/v0.1.0/non_maximum_suppression.onnx",  # noqa
//            hash="sha256:328310ba8fdd386c7ca63fc9df3963cc47b1268909647abd469e8ebdf7f3d20a",
//        )
//        blob.pull()
//        _non_maximum_suppression_inference_session = onnxruntime.InferenceSession(
//            blob.path, providers=["CPUExecutionProvider"]
//        )
//    inference_session = _non_maximum_suppression_inference_session
//
//    outputs = inference_session.run(
//        output_names=["selected_indices"],
//        input_feed={
//            "boxes": boxes[None, :, :],
//            "scores": scores[None, :, :].transpose(0, 2, 1),
//            "max_output_boxes_per_class": np.array(
//                [max_num_detections], dtype=np.int64
//            ),
//            "iou_threshold": np.array([iou_threshold], dtype=np.float32),
//            "score_threshold": np.array([score_threshold], dtype=np.float32),
//        },
//    )
//    selected_indices = cast(NDArray[np.int64], outputs[0])
//    labels = selected_indices[:, 1]
//    box_indices = selected_indices[:, 2]
//    boxes = boxes[box_indices]
//    scores = scores[box_indices, labels]
//    indices = box_indices
//
//    if len(boxes) > max_num_detections:
//        keep_indices = np.argsort(scores)[-max_num_detections:]
//        boxes = boxes[keep_indices]
//        scores = scores[keep_indices]
//        labels = labels[keep_indices]
//        indices = indices[keep_indices]
//
//    return boxes, scores, labels, indices
}