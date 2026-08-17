#include "text_detection.h"
#include "polygon_from_mask.h"

#include <future>
#include <ranges>
#include <QApplication>


std::tuple<std::vector<BoundingBoxF>, std::vector<float>, std::vector<int32_t>, std::vector<cv::Mat>>
get_bboxes_from_texts(
    SamSession *session, const cv::Mat &image, const size_t image_id, const std::vector<std::string> &texts
) {
    SPDLOG_INFO(
        "Requesting with model={session.model_name!r}, "
        "image={(image.shape, image.dtype)}, texts={texts!r}"
    );
    const QList<std::string> q_texts{ texts.begin(), texts.end() };
    const GenerateResponse response = session->run(
        image,
        image_id, {}, {},
        texts
    );

    const size_t num_annotations = response.annotations.size();
    SPDLOG_INFO(
        "Response: num_annotations={num_annotations}, "
        "elapsed_time={time.time() - t_start:.3f} [s]"
    );

    std::vector<BoundingBoxF> boxes(num_annotations);    //: NDArray[np.float32] = np.empty((num_annotations, 4), dtype=np.float32)
    std::vector<float>        scores(num_annotations);   //: NDArray[np.float32] = np.empty((num_annotations,), dtype=np.float32)
    std::vector<int32_t>      labels(num_annotations);   //: NDArray[np.int32] = np.empty((num_annotations,), dtype=np.int32)
    for (const auto &&[i, annotation] : response.annotations | std::views::enumerate) {
        if (!annotation.bounding_box)
            throw std::invalid_argument("Bounding box is missing in the annotation.");
        if (!q_texts.contains(annotation.text))
            throw std::invalid_argument(
                "Unexpected text {annotation.text!r} found in the response."
            );
        boxes[i] = {
            .xmin = static_cast<float>(annotation.bounding_box.xmin),
            .ymin = static_cast<float>(annotation.bounding_box.ymin),
            .xmax = static_cast<float>(annotation.bounding_box.xmax),
            .ymax = static_cast<float>(annotation.bounding_box.ymax),
        };
        scores[i] = annotation.score;
        labels[i] = static_cast<int32_t>(q_texts.indexOf(annotation.text));
    }
    std::vector<cv::Mat> masks; //: list[NDArray[np.bool_]] | None = None
    if (!response.annotations.empty() && !response.annotations[0].mask.empty()) {
        masks = {};
        for (const auto &annotation : response.annotations) {
            if (annotation.mask.empty())
                throw std::invalid_argument("Mask is missing in the annotation.");
            masks.emplace_back(annotation.mask);
        }
    }
    return { boxes, scores, labels, masks };
}

std::tuple<std::vector<BoundingBoxF>, std::vector<float>, std::vector<int32_t>, std::vector<int32_t>>
nms_bboxes(
    std::vector<BoundingBoxF> boxes,
    std::vector<float> scores,
    std::vector<int32_t> labels,
    const float iou_threshold,
    const float score_threshold,
    const int32_t max_num_detections
) {
    std::vector<int32_t> indices;
    if (boxes.empty())
        return {boxes, scores, labels, indices};

    const int32_t num_classes = *std::ranges::max_element(labels) + 1;
    std::vector<float> scores_of_all_classes(
        (boxes.size() * num_classes)
    );
    for (const auto &&[i, zip] : std::views::zip(scores, labels) | std::views::enumerate) {
        auto &[score, label] = zip;
        scores_of_all_classes[i*num_classes + label] = score;
    }
    SPDLOG_INFO(
        "Running NMS: iou_threshold={}, score_threshold={}, max_num_detections={}",
        iou_threshold,
        score_threshold,
        max_num_detections
    );
    SPDLOG_INFO("Input: num_boxes={len(boxes)}");
    //boxes, scores, labels, indices = non_maximum_suppression(
    //    boxes=boxes,
    //    scores=scores_of_all_classes,
    //    iou_threshold=iou_threshold,
    //    score_threshold=score_threshold,
    //    max_num_detections=max_num_detections,
    //);
    SPDLOG_INFO("Output: num_boxes={len(boxes)}");
    return { boxes, scores, labels, indices };
}

QList<TlShape> get_shapes_from_texts(
    SamSession *session, const cv::Mat &image, const size_t image_id, const std::vector<std::string> &texts
) {
    // 这里需要加载模型与图像编码耗时较长, 需要防止GUI界面假死.
    //const GenerateResponse response = sam_session->run(image, image_id, {}, {}, texts);
    std::future<GenerateResponse> future = std::async(std::launch::async, [&]() {
                                               return session->run(image, image_id, {}, {}, texts);
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
            shape.points_ = {QPointF(annotation.bounding_box.xmin, annotation.bounding_box.ymin), QPointF(annotation.bounding_box.xmax, annotation.bounding_box.ymax)};
        } else {
            shape.shape_type_ = "polygon";
            const auto x1 = annotation.bounding_box.xmin;
            const auto y1 = annotation.bounding_box.ymin;
            auto points = measure::compute_polygon_from_mask(annotation.mask);
            std::ranges::transform(points, std::back_inserter(shape.points_), [x1, y1](auto &p) { return QPointF(x1+p.x, y1+p.y); });
        }
    }

    return shapes;
}
