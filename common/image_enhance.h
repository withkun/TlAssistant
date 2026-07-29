#ifndef __INC_IMAGE_ENHANCE_H
#define __INC_IMAGE_ENHANCE_H

#include <QImage>

class Enhance {
public:
    explicit Enhance(const QImage &image);

    QImage enhance(float factor) const;

protected:
    QImage image_;
    QImage degenerate_;
};

// 调整‌色彩饱和度:
// factor = 0.0: 转为黑白图像
// factor = 1.0: 保持原图
// factor > 1.0: 增加饱和度(颜色更鲜艳)
// img = Saturation(img).enhance(1.5)  # 增加50%饱和度
class Saturation : public Enhance {
public:
    explicit Saturation(const QImage &image);
};

// 调整‌对比度‌:
// factor = 0.0: 纯灰色
// factor = 1.0: 原图
// factor > 1.0: 增强对比(明暗更分明)
// img = Contrast(img).enhance(1.3)  # 增加30%对比度
class Contrast : public Enhance {
public:
    explicit Contrast(const QImage &image);
};

// 调整‌亮度:
// factor = 0.0: 完全黑色
// factor = 1.0: 原图
// factor > 1.0: 变亮
// img = Brightness(img).enhance(1.2)  # 增加20%亮度
class Brightness : public Enhance {
public:
    explicit Brightness(const QImage &image);
};

// 调整‌锐度‌:
// factor = 0.0: 模糊
// factor = 1.0: 原图
// factor > 1.0: 锐化(边缘更清晰)
// img = Sharpness(img).enhance(1.1)  # 轻微锐化
class Sharpness : public Enhance {
public:
    explicit Sharpness(const QImage &image);
};
#endif //__INC_IMAGE_ENHANCE_H