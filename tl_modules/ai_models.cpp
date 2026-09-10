#include "ai_models.h"


const QList<AiAssistModelOption> ai_models::AI_ASSIST_MODEL_OPTIONS {
    AiAssistModelOption{
        .model_name="efficientsam:10m",
        .display_name="EfficientSam (speed)",
        .supports_point_prompts=true,
    },
    AiAssistModelOption{
        .model_name="efficientsam:latest",
        .display_name="EfficientSam (accuracy)",
        .supports_point_prompts=true,
    },
    AiAssistModelOption{
        .model_name="sam:100m",
        .display_name="Sam (speed)",
        .supports_point_prompts=true,
    },
    AiAssistModelOption{
        .model_name="sam:300m",
        .display_name="Sam (balanced)",
        .supports_point_prompts=true,
    },
    AiAssistModelOption{
        .model_name="sam:latest",
        .display_name="Sam (accuracy)",
        .supports_point_prompts=true,
    },
    AiAssistModelOption{
        .model_name="sam2:small",
        .display_name="Sam2 (speed)",
        .supports_point_prompts=true,
    },
    AiAssistModelOption{
        .model_name="sam2:latest",
        .display_name="Sam2 (balanced)",
        .supports_point_prompts=true,
    },
    AiAssistModelOption{
        .model_name="sam2:large",
        .display_name="Sam2 (accuracy)",
        .supports_point_prompts=true,
    },
    AiAssistModelOption{
        .model_name="sam3:latest",
        .display_name="Sam3",
        .supports_point_prompts=false,
    },

};

AiAssistModelOption ai_models::find_ai_assist_model_option(const std::string &model_name) {
    for (const auto &option : AI_ASSIST_MODEL_OPTIONS)
        if (option.model_name == model_name)
            return option;
    return {};
}

bool ai_models::supports_point_prompts(const std::string &model_name) {
    const auto option = find_ai_assist_model_option(model_name);
    return option.isNull() || option.supports_point_prompts;
}