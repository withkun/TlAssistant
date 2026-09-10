#include "label_flags.h"
#include "spdlog/spdlog.h"
#include "format_qt.h"


QMap<QString, QList<QString>> compile_label_flags(
    const QMap<QString, QList<QString>> &label_flags
) { //-> dict[re.Pattern[str], list[str]]:
    // The patterns arrive unvalidated from ~/.labelmerc or --label-flags, so
    // neither a typo like `person-(` nor a non-str key (an unquoted `2024`
    // parses as an int) must take the app down. The str check is what keeps a
    // bytes pattern out: it compiles happily, then raises at match time.
    QMap<QString, QList<QString>> compiled; //: dict[re.Pattern[str], list[str]] = {}
    for (const auto &[pattern, keys] : label_flags.asKeyValueRange()) {
        if (pattern.isEmpty()) {
            SPDLOG_WARN("Non-str label_flags pattern: {}", pattern);
            continue;
        }
        try {
            QRegularExpression re(pattern);
            compiled[pattern] = keys;   //[re] = keys;
        } catch (std::exception &e) {
            SPDLOG_WARN("Invalid label_flags pattern {}: {}", pattern, e.what());
        }
    }
    return compiled;
}