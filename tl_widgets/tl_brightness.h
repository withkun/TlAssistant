#ifndef __INC_BRIGHTNESS_CONTRAST_H
#define __INC_BRIGHTNESS_CONTRAST_H

#include <QDialog>
#include <QSlider>
#include <QGridLayout>


class BrightnessContrast : public QDialog {
public:
    explicit BrightnessContrast(const QImage &img, const std::function<void(const QImage &img)> &callback, QWidget *parent = nullptr);
    ~BrightnessContrast() override = default;

    QSlider    *add_slider_row(QGridLayout *grid, int32_t row, const QString &title);
    QString     format_factor(int32_t value);
    void        apply();

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