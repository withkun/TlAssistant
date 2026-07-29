#include "image_enhance.h"


Enhance::Enhance(const QImage &image) : image_(image) {
}

QImage Enhance::enhance(float factor) const {
    if (image_.isNull()) { return image_; }

    // Step 3: 统一格式为RGBA8888以确保混合一致性
    QImage original = image_.convertToFormat(QImage::Format_RGBA8888);
    QImage degenerate = degenerate_.convertToFormat(QImage::Format_RGBA8888);
    if (factor == 0.0) {
        return degenerate;
    }
    if (factor == 1.0) {
        return original;
    }

    // Step 4: 手动线性插值：result = factor * original + (1 - factor) * degenerate
    for (int32_t y = 0; y < original.height(); ++y) {
        for (int32_t x = 0; x < original.width(); ++x) {
            // 对每个通道进行线性插值
            const QRgb c1 = original.pixel(x, y);
            const QRgb c2 = degenerate.pixel(x, y);
            const int32_t r = std::clamp(static_cast<int32_t>(factor * qRed(c1)   + (1.0f - factor) * qRed(c2)),   0, 255);
            const int32_t g = std::clamp(static_cast<int32_t>(factor * qGreen(c1) + (1.0f - factor) * qGreen(c2)), 0, 255);
            const int32_t b = std::clamp(static_cast<int32_t>(factor * qBlue(c1)  + (1.0f - factor) * qBlue(c2)),  0, 255);
            const int32_t a = std::clamp(static_cast<int32_t>(factor * qAlpha(c1) + (1.0f - factor) * qAlpha(c2)), 0, 255);

            original.setPixel(x, y, qRgba(r, g, b, a));
        }
    }

    return original;
}

Saturation::Saturation(const QImage &image) : Enhance(image.convertToFormat(QImage::Format_RGBA8888)) {
    if (image_.isNull()) { return; }

    if (image_.hasAlphaChannel()) {
        // 转换为灰度(L)或灰度+Alpha(LA)
        const QImage gray = image_.convertToFormat(QImage::Format_Grayscale8);
        // 提取原始 Alpha 通道
        const QImage alpha = image_.convertToFormat(QImage::Format_Alpha8);
        // 创建 LA 格式图像
        degenerate_ = QImage(image_.size(), QImage::Format_RGBA8888);
        for (int32_t y = 0; y < image_.height(); ++y) {
            for (int32_t x = 0; x < image_.width(); ++x) {
                const quint8 grayVal = gray.pixel(x, y);
                const quint8 alphaVal = alpha.pixel(x, y);
                degenerate_.setPixel(x, y, qRgba(grayVal, grayVal, grayVal, alphaVal));
            }
        }
    } else {
        degenerate_ = image_.convertToFormat(QImage::Format_Grayscale8);
    }
}

Contrast::Contrast(const QImage &image) : Enhance(image) {
    if (image_.isNull()) { return; }

    // Step 1: 转为灰度图(等价于 image.convert("L"))
    QImage gray = image_.convertToFormat(QImage::Format_Grayscale8);

    // Step 2: 计算全局均值(等价于 ImageStat.Stat(image).mean)
    int32_t sum = 0;
    int32_t pixelCount = gray.width() * gray.height();
    for (int32_t y = 0; y < gray.height(); ++y) {
        const uchar *scanLine = gray.scanLine(y);
        for (int32_t x = 0; x < gray.width(); ++x) {
            sum += scanLine[x];
        }
    }
    const int32_t mean = static_cast<int32_t>(1.0 * sum / pixelCount + 0.5); // 四舍五入

    // Step 3: 创建退化图像(均值填充的灰度图)
    degenerate_ = QImage(gray.size(), QImage::Format_Grayscale8);
    std::fill_n(degenerate_.bits(), degenerate_.sizeInBytes(), static_cast<uchar>(mean));

    // Step 4: 若原图含Alpha, 将退化图升级为相同格式并注入原始Alpha
    if (image_.hasAlphaChannel()) {
        QImage alphaChannel = image_.convertToFormat(QImage::Format_Alpha8);
        degenerate_ = degenerate_.convertToFormat(QImage::Format_RGBA8888);
        for (int32_t y = 0; y < degenerate_.height(); ++y) {
            uchar *dstRow = degenerate_.scanLine(y);
            const uchar *alphaRow = alphaChannel.scanLine(y);
            for (int32_t x = 0; x < degenerate_.width(); ++x) {
                dstRow[x * 4 + 3] = alphaRow[x]; // 设置Alpha通道
            }
        }
    }
}

Brightness::Brightness(const QImage &image) : Enhance(image) {
    if (image_.isNull()) { return; }

    // Step 1: 创建退化图像(全零填充, 等价于 Image.new(image.mode, image.size, 0))
    if (image_.hasAlphaChannel()) {
        // 若含Alpha通道, 退化图初始化为全零的RGBA格式
        degenerate_ = QImage(image_.size(), QImage::Format_RGBA8888);
        std::fill(degenerate_.bits(), degenerate_.bits() + degenerate_.sizeInBytes(), 0);

        // Step 2: 精确复制原始Alpha通道(等价于 putalpha(image.getchannel("A")))
        QImage alphaChannel = image_.convertToFormat(QImage::Format_Alpha8);
        for (int32_t y = 0; y < degenerate_.height(); ++y) {
            uchar *dstRow = degenerate_.scanLine(y);
            const uchar *alphaRow = alphaChannel.scanLine(y);
            for (int32_t x = 0; x < degenerate_.width(); ++x) {
                dstRow[x * 4 + 3] = alphaRow[x]; // Alpha通道位于第4字节
            }
        }
    } else {
        // 无Alpha时, 退化图保持为Grayscale8(全零)
        degenerate_ = QImage(image_.size(), QImage::Format_Grayscale8);
        std::fill_n(degenerate_.bits(), degenerate_.sizeInBytes(), 0);
    }
}

Sharpness::Sharpness(const QImage &image) : Enhance(image.convertToFormat(QImage::Format_RGBA8888)) {
    if (image_.isNull()) { return; }

    // 3x3 平滑核(近似 PIL 的 SMOOTH 滤波器)
    QVector<float> kernel = {
        1.0f / 16.0f, 2.0f / 16.0f, 1.0f / 16.0f,
        2.0f / 16.0f, 4.0f / 16.0f, 2.0f / 16.0f,
        1.0f / 16.0f, 2.0f / 16.0f, 1.0f / 16.0f
    };

    const int32_t kernelSize = 3;
    const int32_t kernelHalf = kernelSize / 2;
    degenerate_ = image_.copy();
    for (int32_t y = kernelHalf; y < degenerate_.height() - kernelHalf; ++y) {
        for (int32_t x = kernelHalf; x < degenerate_.width() - kernelHalf; ++x) {
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
            float weightSum = 0.0f;

            for (int32_t ky = -kernelHalf; ky <= kernelHalf; ++ky) {
                for (int32_t kx = -kernelHalf; kx <= kernelHalf; ++kx) {
                    const int32_t px = x + kx;
                    const int32_t py = y + ky;
                    const QRgb pixel = degenerate_.pixel(px, py);
                    const float weight = kernel[(ky + kernelHalf) * kernelSize + (kx + kernelHalf)];
                    r += qRed(pixel)   * weight;
                    g += qGreen(pixel) * weight;
                    b += qBlue(pixel)  * weight;
                    a += qAlpha(pixel) * weight;
                    weightSum += weight;
                }
            }

            if (weightSum > 0.0f) {
                r /= weightSum;
                g /= weightSum;
                b /= weightSum;
                a /= weightSum;
            }

            degenerate_.setPixel(x, y, qRgba(static_cast<int32_t>(r), static_cast<int32_t>(g),
                                             static_cast<int32_t>(b), static_cast<int32_t>(a)));
        }
    }
}