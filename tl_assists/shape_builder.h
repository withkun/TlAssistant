#ifndef __INC_SHAPE_BUILDER_H
#define __INC_SHAPE_BUILDER_H

#include "base/types.h"
#include "tl_shape.h"
#include "geometry.h"


class Detection {
public:
    BoundingBox     bbox;
    cv::Mat         mask;
    std::string     label;
    std::string     description;

    explicit operator bool() const {
        return bbox || !mask.empty();
    }
};


TlShape shape_from_detection(const Detection &detection, const std::string &shape_type);

QList<QPointF> oriented_rectangle_for_detection(const Detection &detection);

Circle circle_for_detection(const Detection &detection);

QList<TlShape> shapes_from_detections(const QList<Detection> &detections, const std::string &shape_type);

#endif //__INC_SHAPE_BUILDER_H