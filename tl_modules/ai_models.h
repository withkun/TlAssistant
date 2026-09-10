#ifndef __INC_AI_MODELS_H
#define __INC_AI_MODELS_H

#include <QList>
#include <QString>
#include <string>


class AiAssistModelOption {
public:
    QString         model_name;
    QString         display_name;
    bool            supports_point_prompts{false};

    bool isNull() const {
        return model_name.isEmpty() && display_name.isEmpty();
    }
    explicit operator bool() const {
        return !model_name.isEmpty() && !display_name.isEmpty();
    }
};

class ai_models {
public:
    static const QList<AiAssistModelOption> AI_ASSIST_MODEL_OPTIONS;

    static AiAssistModelOption find_ai_assist_model_option(const std::string &model_name);

    static bool supports_point_prompts(const std::string &model_name);
};
#endif //__INC_AI_MODELS_H