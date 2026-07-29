#ifndef __INC_POLYGON_FROM_MASK_H
#define __INC_POLYGON_FROM_MASK_H

#include "opencv2/opencv.hpp"


namespace measure {
std::vector<cv::Point2d> approximate_polygon(const std::vector<cv::Point2d> &coords, double tolerance);

std::vector<cv::Point2d> compute_polygon_from_mask(const cv::Mat &mask);
}
#endif //__INC_POLYGON_FROM_MASK_H