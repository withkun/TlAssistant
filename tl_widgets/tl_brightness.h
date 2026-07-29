#ifndef __INC_BRIGHTNESS_CONTRAST_H
#define __INC_BRIGHTNESS_CONTRAST_H

#include <QDialog>
#include <QSlider>


class BrightnessContrast : public QDialog {
public:
    explicit BrightnessContrast(const QImage &img, const std::function<void(const QImage &img)> &callback, QWidget *parent = nullptr);
    ~BrightnessContrast() override = default;

    void onNewValue(int32_t value);

    QSlider                                *slider_contrast_{nullptr};
    QSlider                                *slider_brightness_{nullptr};
    QSlider                                *slider_saturation_{nullptr};
    QSlider                                *slider_sharpness_{nullptr};

private:
    const double                            base_value_{50.0};
    std::function<void(const QImage &img)>  callback_;
    QImage                                  img_;
    QImage                                  alpha_;
};
#endif //__INC_BRIGHTNESS_CONTRAST_H