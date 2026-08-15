#include "ai_assist_session.h"
#include "tl_modules/polygon_from_mask.h"
#include "base/types.h"
#include "tl_canvas.h"
#include "suppression.h"
#include <ranges>


AiAssistSession::AiAssistSession(Canvas *canvas, const std::string &model_name, const std::string &output_format)
    : model_name_(model_name), output_format_(output_format), canvas_(canvas) {
    this->ai_assist_thread_ = std::make_unique<AiAssistThread>(this);
}

// AI辅助需要加载模型与图像编码耗时较长, 需要防止GUI界面假死, 这里进行异步处理拆分.
QList<TlShape> AiAssistSession::submit_propose_shapes(const QPixmap &image,
                                                      const size_t image_id,
                                                      const QList<QPointF> &points,
                                                      const QList<int32_t> &point_labels,
                                                      const QList<TlShape> &existing_shapes) {
    if (this->ai_assist_points_ != points) {
        if (this->ai_assist_thread_->Submit(points, point_labels)) {
            this->ai_assist_points_ = points;
        }
    }

    QList<TlShape> new_shapes;
    {
        std::lock_guard<std::mutex> lock{this->mutex_};
        new_shapes = this->ai_assist_shapes_;
    }
    return new_shapes;
}

SamSession &AiAssistSession::get_session() {
    if (this->sam_session_ == nullptr || this->sam_session_->model_name() != this->model_name_)
        this->sam_session_ = std::make_unique<SamSession>(this->model_name_);
    return *this->sam_session_;
}

void AiAssistSession::propose_shapes(
    const QList<QPointF> &points,
    const QList<int32_t> &point_labels
) {
    std::vector<cv::Point2f> coords_points;
    std::ranges::for_each(points, [&](const auto &v) { coords_points.push_back(cv::Point2f(v.x(), v.y())); });
    std::vector<float> coords_labels;
    std::ranges::for_each(point_labels, [&](const auto &v) { coords_labels.push_back(v); });

    emit this->canvas_->aiAssistSubmit();
    const GenerateResponse response = this->get_session().run(
        utils::PixmapToMat(this->canvas_->pixmap_),
        this->canvas_->pixmap_hash_,
        coords_points,
        coords_labels
    );
    // iou_threshold is hardcoded because the AI Assist flow has no
    // user-facing IoU control (unlike the AI Text Prompt flow); 0.5 matches
    // the AI Text Prompt widget default.
    auto detections = suppress_detections_greedy(
        detections_from_annotations(response.annotations),
        0.5
    );
    detections = suppress_detections_overlapping_existing_shapes(
        detections,
        this->canvas_->shapes_
    );
    QList<TlShape> new_shapes = shapes_from_detections(
        detections,
        this->output_format_
    );
    {
        std::lock_guard<std::mutex> lock{this->mutex_};
        this->ai_assist_shapes_.swap(new_shapes);
    }
    emit canvas_->aiAssistFinish();
}

QList<Detection> detections_from_annotations(
    const std::vector<Annotation> &annotations
) {
    if (annotations.empty()) {
        SPDLOG_WARN("No annotations returned");
        return {};
    }

    // 根据score从大到小排序.
    std::vector<Annotation> sorted_annotations = annotations;
    std::ranges::sort(sorted_annotations, [](const auto &a, const auto &b) {
        return a.score > b.score;
    });

    QList<Detection> detections;
    for (auto &annotation : sorted_annotations) {
        BoundingBox bbox;
        if (annotation.bbox) {
            auto bb = annotation.bbox;
            bbox = {bb.x1, bb.y1, bb.x2, bb.y2};
        }
        detections.append(Detection(bbox, annotation.mask));
    }
    return detections;
}