#include "schema.h"
#include "ai_models.h"


const QList<QString> schema::Group = {
    "Appearance and language",
    "Files and saving",
    "Drawing and canvas",
    "Continue between images",
    "Label sources",
    "Label behavior",
    "AI assist",
};

const QList<QString> schema::Kind = {"bool", "enum", "str_list", "language"};

const QList<Setting> schema::SETTINGS {
    Setting{
        .key_path={"color_theme",},
        .group="Appearance and language",
        .label=(QT_TRANSLATE_NOOP("SettingsDialog", "Color theme")),
        .kind="enum",
        .choices={"system", "light", "dark"},
        .choice_labels={
            QT_TRANSLATE_NOOP("SettingsDialog", "System"),
            QT_TRANSLATE_NOOP("SettingsDialog", "Light"),
            QT_TRANSLATE_NOOP("SettingsDialog", "Dark"),
        },
    },
    Setting{
        .key_path={"language",},
        .group="Appearance and language",
        .label=(QT_TRANSLATE_NOOP("SettingsDialog", "Language")),
        .kind="language",
        .note=(
            QT_TRANSLATE_NOOP("SettingsDialog", "Takes effect after restart.")
        ),
    },
    Setting{
        .key_path={"auto_save",},
        .group="Files and saving",
        .label=(QT_TRANSLATE_NOOP("SettingsDialog", "Save automatically")),
        .kind="bool",
    },
    Setting{
        .key_path={"with_image_data",},
        .group="Files and saving",
        .label=(
            QT_TRANSLATE_NOOP("SettingsDialog", "Save image data in label file")
        ),
        .kind="bool",
        .note=(
            QT_TRANSLATE_NOOP(
                "SettingsDialog", "Embeds the image in the label JSON file."
            )
        ),
    },
    Setting{
        .key_path={"display_label_popup",},
        .group="Drawing and canvas",
        .label=(
            QT_TRANSLATE_NOOP("SettingsDialog", "Show label popup on new shape")
        ),
        .kind="bool",
    },
    Setting{
        .key_path={"keep_prev",},
        .group="Continue between images",
        .label=(
            QT_TRANSLATE_NOOP("SettingsDialog", "Keep previous annotation")
        ),
        .kind="bool",
    },
    Setting{
        .key_path={"keep_prev_scale",},
        .group="Continue between images",
        .label=(QT_TRANSLATE_NOOP("SettingsDialog", "Keep previous zoom")),
        .kind="bool",
    },
    Setting{
        .key_path={"keep_prev_brightness_contrast",},
        .group="Continue between images",
        .label=(
            QT_TRANSLATE_NOOP("SettingsDialog", "Keep previous brightness/contrast")
        ),
        .kind="bool",
    },
    Setting{
        .key_path={"canvas", "fill_drawing"},
        .group="Drawing and canvas",
        .label=(
            QT_TRANSLATE_NOOP("SettingsDialog", "Fill polygon while drawing")
        ),
        .kind="bool",
    },
    Setting{
        .key_path={"canvas", "allow_out_of_bounds_points"},
        .group="Drawing and canvas",
        .label=(
            QT_TRANSLATE_NOOP(
                "SettingsDialog", "Allow points outside the image boundary"
            )
        ),
        .kind="bool",
        .note=(
            QT_TRANSLATE_NOOP(
                "SettingsDialog",
                "Let shape points extend beyond the image, e.g. for partially "
                "visible objects."
            )
        ),
        .beta=true,
    },
    Setting{
        .key_path={"shape", "show_labels"},
        .group="Drawing and canvas",
        .label=(
            QT_TRANSLATE_NOOP("SettingsDialog", "Show shape labels on canvas")
        ),
        .kind="bool",
        .beta=true,
    },
    Setting{
        .key_path={"labels",},
        .group="Label sources",
        .label=(QT_TRANSLATE_NOOP("SettingsDialog", "Predefined labels")),
        .kind="str_list",
    },
    Setting{
        .key_path={"flags",},
        .group="Label sources",
        .label=(QT_TRANSLATE_NOOP("SettingsDialog", "Predefined image flags")),
        .kind="str_list",
    },
    Setting{
        .key_path={"validate_label",},
        .group="Label behavior",
        .label=(QT_TRANSLATE_NOOP("SettingsDialog", "Label validation")),
        .kind="enum",
        .choices={"", "exact"},
    },
    Setting{
        .key_path={"sort_labels",},
        .group="Label behavior",
        .label=(QT_TRANSLATE_NOOP("SettingsDialog", "Sort labels")),
        .kind="bool",
        .note=(
            QT_TRANSLATE_NOOP(
                "SettingsDialog",
                "Sort the label list alphabetically instead of keeping the "
                "provided order."
            )
        ),
    },
    Setting{
        .key_path={"show_label_text_field",},
        .group="Label behavior",
        .label=(QT_TRANSLATE_NOOP("SettingsDialog", "Show label text field")),
        .kind="bool",
    },
    Setting{
        .key_path={"label_completion",},
        .group="Label behavior",
        .label=(QT_TRANSLATE_NOOP("SettingsDialog", "Label completion")),
        .kind="enum",
        .choices={"startswith", "contains"},
        .choice_labels={
            QT_TRANSLATE_NOOP("SettingsDialog", "Starts with"),
            QT_TRANSLATE_NOOP("SettingsDialog", "Contains"),
        },
    },
    Setting{
        // Choices are the models' display names, matching the format
        // AiAssistedAnnotationWidget itself stores in ai.default (see
        // _ai_assisted_annotation_widget.py, where the dock combobox looks up
        // its initial selection by display name, not model id).
        .key_path={"ai", "default"},
        .group="AI assist",
        .label=(QT_TRANSLATE_NOOP("SettingsDialog", "Default model")),
        .kind="enum",
        .choices=ai_models::AI_ASSIST_MODEL_OPTIONS | std::views::transform([](const auto &opt){ return opt.display_name; }) | std::ranges::to<QStringList>(),
        // pyside6-lupdate needs literal markers here. A schema test keeps these
        // translation declarations aligned with the shared non-Qt model list.
        .choice_labels={
            QT_TRANSLATE_NOOP("SettingsDialog", "EfficientSam (speed)"),
            QT_TRANSLATE_NOOP("SettingsDialog", "EfficientSam (accuracy)"),
            QT_TRANSLATE_NOOP("SettingsDialog", "Sam (speed)"),
            QT_TRANSLATE_NOOP("SettingsDialog", "Sam (balanced)"),
            QT_TRANSLATE_NOOP("SettingsDialog", "Sam (accuracy)"),
            QT_TRANSLATE_NOOP("SettingsDialog", "Sam2 (speed)"),
            QT_TRANSLATE_NOOP("SettingsDialog", "Sam2 (balanced)"),
            QT_TRANSLATE_NOOP("SettingsDialog", "Sam2 (accuracy)"),
            QT_TRANSLATE_NOOP("SettingsDialog", "Sam3")
        },
    },
    Setting{
        .key_path={"ai", "suppress_existing_shape_matches"},
        .group="AI assist",
        .label=(
            QT_TRANSLATE_NOOP("SettingsDialog", "Suppress existing Shape matches")
        ),
        .kind="bool",
        .note=(
            QT_TRANSLATE_NOOP(
                "SettingsDialog",
                "When an AI Assist candidate matches an existing Shape, highlight "
                "that Shape instead of creating a new Shape."
            )
        )
    },

};