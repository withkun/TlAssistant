#ifndef __INC_GEOMETRY_H
#define __INC_GEOMETRY_H

#include "tl_shape.h"
#include "base/types.h"


class Circle {
public:
    float   cx{0.f};
    float   cy{0.f};
    float   radius{0.f};

    explicit operator bool() const {
        return !(cx == 0.f && cy == 0.f && radius == 0.f);
    }
};


BoundingBox shape_to_xyxy_bbox(const TlShape &shape);

Circle compute_circle_from_mask(const cv::Mat &mask);

std::vector<cv::Point> compute_oriented_rectangle_from_mask(const cv::Mat &mask);
std::vector<cv::Point> min_area_rect(const std::vector<cv::Point> &hull);
float get_contour_length(const std::vector<cv::Point> &contour);

std::vector<cv::Point> compute_polygon_from_mask(const cv::Mat &mask);

#endif //__INC_GEOMETRY_H