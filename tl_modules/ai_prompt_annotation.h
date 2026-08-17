#ifndef __INC_AI_TEXT_TO_ANNOTATION_H
#define __INC_AI_TEXT_TO_ANNOTATION_H

#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>


class AiPromptAnnotation : public QWidget {
    Q_OBJECT
public:
    explicit AiPromptAnnotation(const std::string &default_model, const std::function<void()> &on_submit, QWidget *parent = nullptr);
    ~AiPromptAnnotation() override;

    void setEnabled(bool a0);
    bool eventFilter(QObject *watched, QEvent *event) override;

    float get_score_threshold();
    float get_iou_threshold();

    std::string get_model_name() const;
    std::string get_text_prompt() const;
    std::string get_model_display_name();

private:
    void init_ui(const std::string &default_model, const std::function<void()> &on_submit);

    QWidget                *body_{};
    QLineEdit              *text_input_{};
    QComboBox              *model_combo_{};
    QDoubleSpinBox         *score_spinbox_{};
    QDoubleSpinBox         *iou_spinbox_{};
};
#endif //__INC_AI_TEXT_TO_ANNOTATION_H