#ifndef __INC_AI_ASSIST_ANNOTATION_H
#define __INC_AI_ASSIST_ANNOTATION_H

#include <QComboBox>


class AiAssistAnnotation : public QWidget {
    Q_OBJECT
public:
    explicit AiAssistAnnotation(const QString &default_model,
                                const std::function<void(const std::string &n)> &on_model_changed,
                                const std::function<void(const std::string &n)> &on_output_format_changed,
                                QWidget *parent = nullptr);
    ~AiAssistAnnotation() override = default;

    QString current_model_id() const;
    bool is_point_prompt_mode() const;
    QString output_format() const;

    void set_current_model(const QString &model_display);
    void set_point_prompt_mode(bool enabled);

    void setEnabled(bool a0);
    bool eventFilter(QObject *watched, QEvent *event) override;

    void init_ui(const QString &default_model,
                 const std::function<void(const std::string &n)> &on_model_changed,
                 const std::function<void(const std::string &n)> &on_output_format_changed);

signals:
    void hover_highlight_requested(bool);

private:
    QWidget                *body_{nullptr};
    QComboBox              *model_combo_{nullptr};
    QComboBox              *output_format_combo_{nullptr};
    bool                    is_point_prompt_mode_{false};
};
#endif //__INC_AI_ASSIST_ANNOTATION_H