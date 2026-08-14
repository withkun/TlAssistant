#ifndef __INC_SUPPRESSION_H
#define __INC_SUPPRESSION_H

#include "base/types.h"
#include "tl_shape.h"
#include "shape_builder.h"

class LocalMask {
public:
    cv::Mat         mask;
    int32_t         origin_x{0};
    int32_t         origin_y{0};
    int32_t         area{0};

    explicit operator bool() const {
        return !mask.empty();
    }
};


QList<Detection> suppress_detections_greedy(const QList<Detection> &detections, float iou_threshold);

QList<Detection> suppress_detections_overlapping_existing_shapes(const QList<Detection> &detections, const QList<TlShape> &existing_shapes);

bool is_redundant_pair(const LocalMask &new1, const LocalMask &peer, float iou_threshold);

LocalMask local_mask_from_detection(const Detection &detection);

LocalMask local_mask_from_shape(const TlShape &shape);

cv::Mat rasterize_shape(const TlShape &shape, int32_t xmin, int32_t ymin, int32_t width, int32_t height);

int32_t compute_mask_intersection_area(const LocalMask &a, const LocalMask &b);

#endif //__INC_SUPPRESSION_H