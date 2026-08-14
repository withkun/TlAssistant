#include "ai_prompt_annotation.h"
#include "info_button.h"

#include <QLabel>
#include <QEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolTip>


namespace {
std::vector<std::pair<std::string, std::string>> available_models_{
    {"sam3:latest", "SAM3 (smart)"},
    {"yoloworld:latest", "YOLO-World (fast)"},
};

const char *default_model_name_ = "yoloworld:latest";
constexpr float default_score_threshold_ = 0.1;
constexpr float default_iou_threshold_ = 0.5;
}


AiPromptAnnotation::AiPromptAnnotation(const std::string &default_model,
                                       const std::function<void()> &on_submit,
                                       QWidget *parent) : QWidget(parent) {
    this->init_ui(default_model.empty() ? default_model_name_ : default_model, on_submit);
}

AiPromptAnnotation::~AiPromptAnnotation() = default;

void AiPromptAnnotation::init_ui(const std::string &default_model, const std::function<void()> &on_submit) {
    auto *const layout = new QVBoxLayout();
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);
    this->setLayout(layout);

    auto *const header_layout = new QHBoxLayout();
    header_layout->addStretch();
    auto *const label = new QLabel(tr("AI Text-to-Annotation"));
    header_layout->addWidget(label);
    auto *const info_button = new InfoButton(
        tr("AI creates annotations from the text prompt")
    );
    header_layout->addWidget(info_button);
    header_layout->addStretch();
    layout->addLayout(header_layout);

    body_ = new QWidget();
    body_->installEventFilter(this);
    auto *const body_layout = new QVBoxLayout();
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->setSpacing(0);
    body_->setLayout(body_layout);

    auto *const grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(2);

    text_input_ = new QLineEdit();
    text_input_->setPlaceholderText(tr("e.g., dog,cat,bird"));
    text_input_->setFixedHeight(24);
    grid->addWidget(text_input_, 0, 0);

    auto *const run_button = new QToolButton();
    run_button->setText(tr("Run"));
    run_button->setFixedHeight(24);
    run_button->setCursor(Qt::PointingHandCursor);
    QObject::connect(run_button, &QToolButton::clicked, [=] { on_submit(); });
    grid->addWidget(run_button, 0, 1);

    auto *const settings_layout = new QHBoxLayout();
    settings_layout->setContentsMargins(0, 0, 0, 0);
    settings_layout->setSpacing(4);

    model_combo_ = new QComboBox();
    for (auto &[model_id, model_display] : available_models_) {
        model_combo_->addItem(QString::fromStdString(model_display), QString::fromStdString(model_id));
    }
    int32_t model_index = 0;
    for (auto i = 0; i < available_models_.size(); ++i) {
        if (available_models_[i].first == default_model) {
            model_index = i;
        }
    }
    model_combo_->setCurrentIndex(model_index);
    settings_layout->addWidget(model_combo_, 1);

    auto *const score_label = new QLabel(tr("Score"));
    score_label->setStyleSheet("color: gray; font-size: 10px;");
    settings_layout->addWidget(score_label);

    score_spinbox_ = new QDoubleSpinBox();
    score_spinbox_->setStyleSheet("font-size: 10px;");
    score_spinbox_->setFixedWidth(50);
    score_spinbox_->setRange(0, 1);
    score_spinbox_->setSingleStep(0.05);
    score_spinbox_->setValue(default_score_threshold_);
    settings_layout->addWidget(score_spinbox_);

    auto *const iou_label = new QLabel(tr("IoU"));
    iou_label->setStyleSheet("color: gray; font-size: 10px;");
    settings_layout->addWidget(iou_label);

    iou_spinbox_ = new QDoubleSpinBox();
    iou_spinbox_->setStyleSheet("font-size: 10px;");
    iou_spinbox_->setFixedWidth(50);
    iou_spinbox_->setRange(0, 1);
    iou_spinbox_->setSingleStep(0.05);
    iou_spinbox_->setValue(default_iou_threshold_);
    settings_layout->addWidget(iou_spinbox_);

    grid->addLayout(settings_layout, 1, 0, 1, 2);

    body_layout->addLayout(grid);
    layout->addWidget(body_);

    setMaximumWidth(320);
}

std::vector<std::string> AiPromptAnnotation::get_texts_prompt() const {
    const auto items = text_input_->text().split(",");
    std::vector<std::string> prompt_texts;
    std::ranges::transform(items, std::back_inserter(prompt_texts), [](auto &s) { return s.toStdString(); });
    return prompt_texts;
}

std::string AiPromptAnnotation::get_model_name() const {
    return model_combo_->currentData().toString().toStdString();
}

float AiPromptAnnotation::get_score_threshold() {
    return score_spinbox_->value();
}

float AiPromptAnnotation::get_iou_threshold() {
    return iou_spinbox_->value();
}

void AiPromptAnnotation::setEnabled(bool a0) {
    body_->setEnabled(a0);
}

bool AiPromptAnnotation::eventFilter(QObject *a0, QEvent *a1) {
    if (a0 == body_ && !body_->isEnabled()) {
        if (a1->type() == QEvent::Enter) {
            QToolTip::showText(
                QCursor::pos(),
                tr(
                    "Select 'Polygon', 'Rectangle', or 'AI-Points' mode to enable"
                ),
                body_
            );
        }
    }
    return QWidget::eventFilter(a0, a1);
}