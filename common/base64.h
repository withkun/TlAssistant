/*
    ******
    base64.hpp is a repackaging of the base64.cpp and base64.h files into a
    single header suitable for use as a header only library. This conversion was
    done by Peter Thorson (webmaster@zaphoyd.com) in 2012. All modifications to
    the code are redistributed under the same license as the original, which is
    listed below.
    ******

   base64.cpp and base64.h

   Copyright (C) 2004-2008 René Nyffenegger

   This source code is provided 'as-is', without any express or implied
   warranty. In no event will the author be held liable for any damages
   arising from the use of this software.

   Permission is granted to anyone to use this software for any purpose,
   including commercial applications, and to alter it and redistribute it
   freely, subject to the following restrictions:

   1. The origin of this source code must not be misrepresented; you must not
      claim that you wrote the original source code. If you use this source code
      in a product, an acknowledgment in the product documentation would be
      appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
      misrepresented as being the original source code.

   3. This notice may not be removed or altered from any source distribution.

   René Nyffenegger rene.nyffenegger@adp-gmbh.ch

*/
#ifndef __INC_BASE64_H
#define __INC_BASE64_H

#include <QString>
#include <QByteArray>
#include "opencv2/opencv.hpp"


namespace base64 {
/**
 * Encode a char buffer into a base64 string
 * @param input The input data
 * @param len The length of input in bytes
 * @return A base64 encoded string representing input
 */
std::string b64encode(const uint8_t *input, size_t len);

/**
 * Decode a base64 encoded string into a string of raw bytes
 * @param input The base64 encoded input data
 * @return A string representing the decoded raw bytes
 */
std::string b64decode(const std::string &input);

/**
 * Encode a string into a base64 string
 * @param input The input data
 * @return A base64 encoded string representing input
 */
inline std::string b64encode(const std::string &input) {
    return b64encode(
        reinterpret_cast<const uint8_t *>(input.data()),
        input.size()
    );
}

/**
 * Encode a QByteArray into a base64 string
 * @param input The input data
 * @return A base64 encoded string representing input
 */
inline std::string b64encode(const QByteArray &input) {
    return b64encode(reinterpret_cast<const uint8_t *>(input.data()), input.size());
}

/**
 * Encode a QString into a base64 string
 * @param input The input data
 * @return A base64 encoded string representing input
 */
inline std::string b64encode(const QString &input) {
    return b64encode(input.toLocal8Bit());
}


inline std::string mat_to_img_b64(const cv::Mat &image) {
    std::vector<uchar> im_data;
    cv::imencode(".png", image, im_data);
    std::string b64data = b64encode(im_data.data(), im_data.size());
    return b64data;
}

inline cv::Mat img_b64_to_mat(const std::string &b64data) {
    std::string im_data = b64decode(b64data);
    const std::vector<char> base64_img(im_data.begin(), im_data.end());
    cv::Mat image = cv::imdecode(base64_img, cv::IMREAD_GRAYSCALE);
    return image;
}
} // namespace base64
#endif // __INC_BASE64_H