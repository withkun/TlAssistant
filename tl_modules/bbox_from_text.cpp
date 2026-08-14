#include "bbox_from_text.h"
#include "polygon_from_mask.h"

#include <future>
#include <ranges>
#include <QApplication>


namespace bbox_from_text {
void get_bboxes_from_texts(
    const GenerateResponse &response,
    std::vector<float> &scores,
    std::vector<int32_t> &labels,
    std::vector<cv::Rect> &boxes,
    std::vector<cv::Mat> &masks
) { // -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray | None]:
    //logger.debug(
    //    f"Requesting with model={session.model_name!r}, "
    //    f"image={(image.shape, image.dtype)}, texts={texts!r}"
    //)
    //t_start: float = time.time()
    //if len(image.shape) == 2 or image.shape[2] == 1:
    //    import cv2
    //    image = cv2.cvtColor(image, cv2.COLOR_GRAY2RGB)
    //response: osam.types.GenerateResponse = session.run(
    //    image=image,
    //    image_id=image_id,
    //    texts=texts,
    //)

    //num_annotations: int = len(response.annotations)
    //logger.debug(
    //    f"Response: num_annotations={num_annotations}, "
    //    f"elapsed_time={time.time() - t_start:.3f} [s]"
    //)
    //
    //boxes: NDArray[np.float32] = np.empty((num_annotations, 4), dtype=np.float32)
    //scores: NDArray[np.float32] = np.empty((num_annotations,), dtype=np.float32)
    //labels: NDArray[np.int32] = np.empty((num_annotations,), dtype=np.int32)
    //for i, annotation in enumerate(response.annotations):
    //    if annotation.bounding_box is None:
    //        raise ValueError("Bounding box is missing in the annotation.")
    //    if annotation.text not in texts:
    //        raise ValueError(
    //            f"Unexpected text {annotation.text!r} found in the response."
    //        )
    //    boxes[i] = [
    //        annotation.bounding_box.xmin,
    //        annotation.bounding_box.ymin,
    //        annotation.bounding_box.xmax,
    //        annotation.bounding_box.ymax,
    //    ]
    //    scores[i] = annotation.score
    //    labels[i] = texts.index(annotation.text)
    //
    //masks: NDArray[np.bool_] | None = None
    //if response.annotations and response.annotations[0].mask is not None:
    //    masks = np.array(
    //        [annotation.mask for annotation in response.annotations], dtype=np.bool_
    //    )
    //
    //return boxes, scores, labels, masks
}

//def nms_bboxes(
//    boxes: np.ndarray,
//    scores: np.ndarray,
//    labels: np.ndarray,
//    iou_threshold: float,
//    score_threshold: float,
//    max_num_detections: int,
//) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
//    if len(boxes) == 0:
//        return boxes, scores, labels, np.empty((0,), dtype=np.int32)
//
//    num_classes: int = max(labels) + 1
//    scores_of_all_classes: NDArray[np.float32] = np.zeros(
//        (len(boxes), num_classes), dtype=np.float32
//    )
//    for i, (score, label) in enumerate(zip(scores, labels)):
//        scores_of_all_classes[i, label] = score
//    logger.debug(
//        "Running NMS: iou_threshold={}, score_threshold={}, max_num_detections={}",
//        iou_threshold,
//        score_threshold,
//        max_num_detections,
//    )
//    logger.debug(f"Input: num_boxes={len(boxes)}")
//    boxes, scores, labels, indices = osam.apis.non_maximum_suppression(
//        boxes=boxes,
//        scores=scores_of_all_classes,
//        iou_threshold=iou_threshold,
//        score_threshold=score_threshold,
//        max_num_detections=max_num_detections,
//    )
//    logger.debug(f"Output: num_boxes={len(boxes)}")
//    return boxes, scores, labels, indices
//}

//def get_shapes_from_bboxes(
//    boxes: np.ndarray,
//    scores: np.ndarray,
//    labels: np.ndarray,
//    texts: list[str],
//    masks: NDArray[np.bool_] | None,
//    shape_type: Literal["rectangle", "polygon", "mask"],
//) -> list[Shape]:
//    shapes: list[Shape] = []
//    for i, (box, score, label) in enumerate(zip(boxes, scores, labels)):
//        text: str = texts[label]
//        xmin, ymin, xmax, ymax = box
//
//        points: list[list[float]]
//        mask: NDArray[np.bool_] | None = None
//        if shape_type == "rectangle":
//            points = [[xmin, ymin], [xmax, ymax]]
//        elif shape_type == "polygon":
//            if masks is None:
//                points = [
//                    [xmin, ymin],
//                    [xmax, ymin],
//                    [xmax, ymax],
//                    [xmin, ymax],
//                    [xmin, ymin],
//                ]
//            else:
//                points = compute_polygon_from_mask(mask=masks[i]).tolist()
//        elif shape_type == "mask":
//            xmin = int(xmin)
//            ymin = int(ymin)
//            xmax = int(xmax)
//            ymax = int(ymax)
//            points = [[xmin, ymin], [xmax, ymax]]
//            if masks is None:
//                mask = np.zeros((ymax - ymin, xmax - xmin), dtype=bool)
//            else:
//                mask = masks[i][ymin : ymax + 1, xmin : xmax + 1]
//        else:
//            raise ValueError(f"Unsupported shape_type: {shape_type!r}")
//
//        shape = Shape(
//            label=text,
//            shape_type=shape_type,
//            mask=mask,
//            description=json.dumps(dict(score=score.item(), text=text)),
//        )
//        for point in points:
//            shape.addPoint(QtCore.QPointF(point[0], point[1]))
//        shapes.append(shape)
//    return shapes
//}

QList<TlShape> get_shapes_from_texts(
    SamSession *sam_session,
    const cv::Mat &image, const size_t image_id, const std::vector<std::string> &texts
) {
    // 这里需要加载模型与图像编码耗时较长, 需要防止GUI界面假死.
    //const GenerateResponse response = sam_session->run(image, image_id, {}, {}, texts);
    std::future<GenerateResponse> future = std::async(std::launch::async, [&]() {
                                               return sam_session->run(image, image_id, {}, {}, texts);
                                           });
    while (future.wait_for(std::chrono::milliseconds(20)) != std::future_status::ready) {
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    const GenerateResponse response = future.get();

    QList<TlShape> shapes;
    for (const auto &&[index, annotation] : response.annotations | std::views::enumerate) {
        auto &shape = shapes.emplace_back();
        QString label = QString::fromStdString(annotation.text.empty() ? texts.front() : annotation.text);
        shape.label_ = QString("%1_%2").arg(label).arg(100+index, 3, 10, '0');
        shape.closed_ = true;
        if (annotation.mask.empty()) {
            shape.shape_type_ = "rectangle";
            shape.points_ = {QPointF(annotation.bbox.x1, annotation.bbox.y1), QPointF(annotation.bbox.x2, annotation.bbox.y2)};
        } else {
            shape.shape_type_ = "polygon";
            const auto x1 = annotation.bbox.x1;
            const auto y1 = annotation.bbox.y1;
            auto points = measure::compute_polygon_from_mask(annotation.mask);
            std::ranges::transform(points, std::back_inserter(shape.points_), [x1, y1](auto &p) { return QPointF(x1+p.x, y1+p.y); });
        }
    }

    return shapes;
}
} //namespace bbox_from_text