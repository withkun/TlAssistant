#include "tl_brightness.h"
#include "common/image_enhance.h"

#include <QLabel>
#include <QHBoxLayout>


BrightnessContrast::BrightnessContrast(const QImage &img, const std::function<void(const QImage &img)> &callback, QWidget *parent) : QDialog(parent) {
    this->setModal(false);  // 非模态: 不阻塞主窗口
    this->setWindowTitle("Brightness/Contrast");
    this->setWindowFlags(Qt::Dialog | Qt::MSWindowsFixedSizeDialogHint);

    const auto fNewSlider = [=](auto title, auto v_layout) {
        auto *const layout = new QHBoxLayout();
        auto *const title_label = new QLabel(tr(title));
        title_label->setFixedWidth(75);
        layout->addWidget(title_label);

        auto *const slider = new QSlider(Qt::Horizontal);
        slider->setFixedWidth(150);
        slider->setRange(0, 3 * base_value_);
        slider->setValue(base_value_);
        layout->addWidget(slider);

        // filedWidth: 点位总长度, 如果filedWidth大于实际值, 使用fillChar补充.
        // precision: 精度, 当fmt为f时代表小数点后面的位数, 当fmt为g时代表总共的位数.
        auto *const value_label = new QLabel(QString("%1").arg(slider->value() / base_value_, 0, 'f', 2));
        value_label->setAlignment(Qt::AlignRight);
        layout->addWidget(value_label);

        QObject::connect(slider, &QSlider::valueChanged, this, &BrightnessContrast::onNewValue);
        QObject::connect(slider, &QSlider::valueChanged, [=]() {
            value_label->setText(QString("%1").arg(slider->value() / base_value_, 0, 'f', 2)); }
        );
        v_layout->addLayout(layout);
        return slider;
    };

    auto *const layout = new QVBoxLayout();
    this->slider_contrast_   = fNewSlider("Contrast:",   layout);
    this->slider_brightness_ = fNewSlider("Brightness:", layout);
    this->slider_saturation_ = fNewSlider("Saturation:", layout);
    this->slider_sharpness_  = fNewSlider("Sharpness:",  layout);
    this->setLayout(layout);

    this->alpha_ = QImage();
    if (img.hasAlphaChannel()) {
        this->alpha_ = img.convertToFormat(QImage::Format_Alpha8);
    }
    if (img.format() != QImage::Format_RGB32 && img.format() != QImage::Format_ARGB32 && img.format() != QImage::Format_RGB888) {
        this->img_ = img.convertToFormat(QImage::Format_RGB888);
    } else {
        this->img_ = img;
    }

    callback_ = callback;
}

void BrightnessContrast::onNewValue(int32_t value) {
    const double contrast   = slider_contrast_->value() / base_value_;
    const double brightness = slider_brightness_->value() / base_value_;
    const double saturation = slider_saturation_->value() / base_value_;
    const double sharpness  = slider_sharpness_->value() / base_value_;

    QImage img = this->img_.copy(); // 深拷贝.
    if (brightness != 1.0f)
        img = Brightness(img).enhance(brightness);
    if (contrast != 1.0f)
        img = Contrast(img).enhance(contrast);
    if (saturation != 1.0f)
        img = Saturation(img).enhance(saturation);
    if (sharpness != 1.0f)
        img = Sharpness(img).enhance(sharpness);

    callback_(img);
}