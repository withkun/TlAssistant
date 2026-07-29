#include "ai_assist_annotation.h"

#include "info_button.h"
#include "spdlog/spdlog.h"
#include "common/format_qt.h"

#include <QLabel>
#include <QEvent>
#include <QToolTip>
#include <QVBoxLayout>
#include <QStandardItemModel>


namespace {
std::vector<std::pair<QString, QString>> available_models_{
    {"efficientsam:10m", "EfficientSam (speed)"},
    {"efficientsam:latest", "EfficientSam (accuracy)"},
    {"sam:100m", "Sam (speed)"},
    {"sam:300m", "Sam (balanced)"},
    {"sam:latest", "Sam (accuracy)"},
    {"sam2:small", "Sam2 (speed)"},
    {"sam2:latest", "Sam2 (balanced)"},
    {"sam2:large", "Sam2 (accuracy)"},
    {"sam3:latest", "Sam3"},
};
}

AiAssistAnnotation::AiAssistAnnotation(const QString &default_model,
                                       const std::function<void(const std::string &n)> &on_model_changed,
                                       const std::function<void(const std::string &n)> &on_output_format_changed,
                                       QWidget *parent)
    : QWidget(parent) {
    init_ui(default_model, on_model_changed, on_output_format_changed);
}

AiAssistAnnotation::~AiAssistAnnotation() {
}

//@property
QString AiAssistAnnotation::output_format() {
    return output_format_combo_->currentData().toString();
}

void AiAssistAnnotation::init_ui(
    const QString &default_model,
    const std::function<void(const std::string &n)> &on_model_changed,
    const std::function<void(const std::string &n)> &on_output_format_changed)
{
    auto *const layout = new QVBoxLayout();
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);
    this->setLayout(layout);

    auto *const header_layout = new QHBoxLayout();
    header_layout->addStretch();
    auto *const label = new QLabel(tr("AI-Assisted Annotation"));
    header_layout->addWidget(label);
    auto *const info_button = new InfoButton(
        tr("AI suggests annotation in 'AI-Points' and 'AI-Box' modes")
    );
    header_layout->addWidget(info_button);
    header_layout->addStretch();
    layout->addLayout(header_layout);

    body_ = new QWidget();
    this->installEventFilter(this);
    body_->installEventFilter(this);
    auto *const body_layout = new QVBoxLayout();
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->setSpacing(0);
    body_->setLayout(body_layout);

    model_combo_ = new QComboBox();
    for (auto &[model_id, model_display] : available_models_) {
        model_combo_->addItem(model_display, model_id);
    }
    body_layout->addWidget(model_combo_);

    output_format_combo_ = new QComboBox();
    output_format_combo_->addItem("Polygon", "polygon");
    output_format_combo_->addItem("Mask", "mask");
    body_layout->addWidget(output_format_combo_);

    layout->addWidget(body_);

    int32_t model_index;
    QList<QString> model_ui_names;
    std::ranges::for_each(available_models_, [&](auto &it) { model_ui_names.append(it.first); });
    if (model_ui_names.contains(default_model)) {
        model_index = model_ui_names.indexOf(default_model);
    } else {
        SPDLOG_WARN("Default AI model is not found: {}", default_model);
        model_index = 0;
    }

    QObject::connect(model_combo_, &QComboBox::currentIndexChanged, [this, on_model_changed](int index) {
        const QString model_name = model_combo_->itemData(index).toString();
        on_model_changed(model_name.toStdString());
    });
    model_combo_->setCurrentIndex(model_index);

    QObject::connect(output_format_combo_, &QComboBox::currentIndexChanged, [this, on_output_format_changed](int index) {
        const QString model_name = output_format_combo_->itemData(index).toString();
        on_output_format_changed(model_name.toStdString());
    });
    output_format_combo_->setCurrentIndex(0);

    setMaximumWidth(200);
}

void AiAssistAnnotation::set_disabled_models(const QList<QString> &disabled_models) {
    QStandardItemModel *model = static_cast<QStandardItemModel *>(model_combo_->model());
    for (int32_t i = 0; i < model_combo_->count(); ++i) {
        auto model_id = model_combo_->itemData(i);
        auto item = model->item(i);
        //assert item is not None
        if (disabled_models.contains(model_id)) {
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        } else
            item->setFlags(item->flags() | Qt::ItemIsEnabled);
    }
}

void AiAssistAnnotation::setEnabled(bool a0) {
    body_->setEnabled(a0);
    emit hover_highlight_requested(false);
}

bool AiAssistAnnotation::eventFilter(QObject *a0, QEvent *a1) {
    if (a0 == body_ && !body_->isEnabled()) {
        if (a1->type() == QEvent::Enter) {
            QToolTip::showText(
                QCursor::pos(),
                tr(
                    "Select 'AI-Points' or 'AI-Box' mode "
                    "to enable AI-Assisted Annotation"
                ),
                body_
            );
            emit hover_highlight_requested(true);
        } else if (a1->type() == QEvent::Leave) {
            emit hover_highlight_requested(false);
        }
    }
    return QWidget::eventFilter(a0, a1);
}