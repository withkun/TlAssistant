#ifndef __INC_AI_ASSIST_ANNOTATION_H
#define __INC_AI_ASSIST_ANNOTATION_H

#include <QWidget>
#include <QComboBox>


class AiAssistAnnotation : public QWidget {
    Q_OBJECT
public:
    explicit AiAssistAnnotation(const QString &default_model,
                                const std::function<void(const std::string &n)> &on_model_changed,
                                const std::function<void(const std::string &n)> &on_output_format_changed,
                                QWidget *parent = nullptr);
    ~AiAssistAnnotation() override;

    QString output_format();

    void set_disabled_models(const QList<QString> &disabled_models);
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
};
#endif //__INC_AI_ASSIST_ANNOTATION_H