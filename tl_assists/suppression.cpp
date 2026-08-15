#include "format_cv.h"
#include "suppression.h"
#include "geometry.h"


QList<Detection> suppress_detections_greedy(
    const QList<Detection> &detections,
    const float iou_threshold
) {
    //Callers must pass detections in priority order (highest first); the first
    //detection in each redundant cluster is kept and later ones are dropped.
    //
    //Redundancy combines IoU with intersection-over-smaller (containment), so
    //nested masks (e.g. tree-cluster containing a single tree) deduplicate even
    //when their IoU is low.
    //"""
    if (detections.empty())
        return {};

    // Mixing bbox-only and mask detections would silently let bbox-only peers
    // (treated as fully-opaque rectangles) suppress overlapping mask detections
    // via containment. Require homogeneous input until a caller needs otherwise.
    const auto mask_presence = detections | std::views::filter([](const auto &d) { return d.bbox && !d.mask.empty(); }) | std::views::transform([](const auto &b) { return true; }) | std::ranges::to<std::vector<bool>>();
    if (mask_presence.size() > 1)
        throw std::runtime_error(
            "detections must be homogeneous: either all have masks or none do"
        );

    QList<Detection> kept;
    std::map<std::string, QList<LocalMask>> kept_masks_by_label;
    for (auto &detection : detections) {
        if (!detection.bbox) {
            kept.append(detection);
            continue;
        }
        auto new_local = local_mask_from_detection(detection);
        auto &peers = kept_masks_by_label[detection.label];
        if (std::ranges::any_of(peers, [&](const auto &peer) {
            return is_redundant_pair(
                new_local,
                peer,
                iou_threshold
                ); })
        )
            continue;

        kept.append(detection);
        peers.append(new_local);
    }
    return kept;
}

QList<Detection> suppress_detections_overlapping_existing_shapes(
    const QList<Detection> &detections,
    const QList<TlShape> &existing_shapes
) {
    constexpr float OVERLAP_IOU_THRESHOLD = 0.5;
    if (detections.empty())
        return {};
    const auto existing_masks = existing_shapes
        | std::views::transform([](const auto &s) { return local_mask_from_shape(s); })
        | std::views::filter([](const auto &local_mask) { return !local_mask.mask.empty(); })
        | std::ranges::to<std::vector<LocalMask>>();
    if (existing_masks.empty())
        return detections;

    QList<Detection> kept;
    for (auto &detection : detections) {
        if (!detection.bbox) {
            kept.append(detection);
            continue;
        }
        const auto new_local = local_mask_from_detection(detection);
        if (std::ranges::any_of(existing_masks, [&](const auto &existing) {
            return is_redundant_pair(
                new_local,
                existing,
                OVERLAP_IOU_THRESHOLD
                ); })
        )
            continue;
        kept.append(detection);
    }
    return kept;
}

bool is_redundant_pair(
    const LocalMask &new1,
    const LocalMask &peer,
    const float iou_threshold
) {
    // Containment (intersection-over-smaller) catches nested masks whose IoU
    // is too low for the IoU check (e.g. tree-cluster swallowing a single tree).
    constexpr float CONTAINMENT_THRESHOLD = 0.85f;

    const auto intersection = compute_mask_intersection_area(new1, peer);
    if (intersection == 0)
        return false;
    const auto iou = 1.0*intersection / (new1.area + peer.area - intersection);
    if (iou >= iou_threshold)
        return true;
    const auto containment = 1.0*intersection / std::min(new1.area, peer.area);
    return containment >= CONTAINMENT_THRESHOLD;
}

LocalMask local_mask_from_detection(const Detection &detection) {
    const auto &[xmin, ymin, xmax, ymax] = detection.bbox;
    if (detection.mask.empty()) {
        const auto h = ymax - ymin + 1, w = xmax - xmin + 1;
        const auto mask = cv::Mat::ones(h, w, CV_8UC1);
        return LocalMask{.mask=mask, .origin_xy={xmin, ymin}, .area=h * w};
    }
    // Mask geometry below assumes mask covers exactly the bbox-derived extent
    // (matching the OSAM Annotation contract). Reject inconsistent shapes loudly
    // so a future non-OSAM caller doesn't silently produce wrong IoU values.
    const auto expected_shape = cv::Size(ymax - ymin + 1, xmax - xmin + 1);
    if (detection.mask.size() != expected_shape)
        throw std::invalid_argument(
            std::format("mask shape {} does not match "
                        "bbox-derived extent {}", detection.mask.shape(), expected_shape)
        );
    return LocalMask{
        .mask=detection.mask,
        .origin_xy={xmin, ymin},
        .area=cv::countNonZero(detection.mask),
    };
}

LocalMask local_mask_from_shape(const TlShape &shape) {
    // Skip non-bbox shapes (point/line/linestrip) so callers can pass
    // canvas.shapes unfiltered.
    if (!QKey{
        "rectangle",
        "polygon",
        "circle",
        "oriented_rectangle",
        "mask"}.contains(shape.shape_type_)
    )
        return {};
    const auto bbox = shape_to_xyxy_bbox(shape);
    if (!bbox)
        return {};
    const auto &[xmin, ymin, xmax, ymax] = bbox;
    const int32_t width = xmax - xmin + 1;
    const int32_t height = ymax - ymin + 1;
    const auto mask = rasterize_shape(
        shape, xmin, ymin, width, height
    );
    return LocalMask{
        .mask=mask,
        .origin_xy={xmin, ymin},
        .area=cv::countNonZero(mask),
    };
}

cv::Mat rasterize_shape(
    const TlShape &shape, int32_t xmin, int32_t ymin, int32_t width, int32_t height
) {
    if (shape.shape_type_ == "mask") {
        if (shape.mask_.empty())
            return cv::Mat::ones(height, width, CV_8UC1);
        // Same bbox-extent contract as detection masks.
        if (shape.mask_.rows != height || shape.mask_.cols != width)
            throw std::invalid_argument(
                std::format("mask shape {} does not match "
                            "bbox-derived extent ({}, {})", shape.mask_.shape(), height, width)
            );
        return shape.mask_ > 0.f;
    }
    if (shape.shape_type_ == "rectangle")
        return cv::Mat::ones(height, width, CV_8UC1) > 0;
    if (shape.shape_type_ == "circle") {
        const auto center = shape.points_[0], edge = shape.points_[1];
        const auto cx_local = center.x() - xmin;
        const auto cy_local = center.y() - ymin;
        const auto radius = (int32_t)utils::distance(edge - center);
        cv::Mat image = cv::Mat::zeros(height, width, CV_8UC1);
        cv::circle(image, cv::Point2d(cx_local, cy_local), radius, cv::Scalar(255), 1, cv::FILLED);
        return image > 0;
    }
    if (QKey{"polygon", "oriented_rectangle"}.contains(shape.shape_type_)) {
        cv::Mat image = cv::Mat::zeros(height, width, CV_8UC1);
        const auto points_local = shape.points_ | std::views::transform([&xmin, &ymin](auto &p) { return cv::Point(p.x()-xmin, p.y()-ymin); }) | std::ranges::to<std::vector<cv::Point>>();
        cv::fillPoly(image, {points_local}, cv::Scalar(1));
        return image > 0;
    }
    throw std::invalid_argument("Unsupported shape_type: " + shape.shape_type_.toStdString());
}

int32_t compute_mask_intersection_area(const LocalMask &a, const LocalMask &b) {
    // bbox endpoints are inclusive pixel coords (mask width = xmax - xmin + 1),
    // so xmin + w is the exclusive x-upper-bound used for clipping.
    const auto &[a_xmin, a_ymin] = a.origin_xy;
    const auto &[b_xmin, b_ymin] = b.origin_xy;
    const auto a_h = a.mask.rows, a_w = a.mask.cols;
    const auto b_h = b.mask.rows, b_w = b.mask.cols;

    const auto inter_xmin = std::max(a_xmin, b_xmin);
    const auto inter_ymin = std::max(a_ymin, b_ymin);
    const auto inter_xmax = std::min(a_xmin + a_w, b_xmin + b_w);
    const auto inter_ymax = std::min(a_ymin + a_h, b_ymin + b_h);
    if (inter_xmax <= inter_xmin || inter_ymax <= inter_ymin)
        return 0;

    const auto sub_a = a.mask(cv::Rect(inter_xmin - a_xmin, inter_ymin - a_ymin,
        inter_xmax - a_xmin - (inter_xmin - a_xmin),
        inter_ymax - a_ymin - (inter_ymin - a_ymin)
    ));
    const auto sub_b = b.mask(cv::Rect(inter_xmin - b_xmin, inter_ymin - b_ymin,
        inter_xmax - b_xmin - (inter_xmin - b_xmin),
        inter_ymax - b_ymin - (inter_ymin - b_ymin)
    ));
    return cv::countNonZero(sub_a & sub_b);
}