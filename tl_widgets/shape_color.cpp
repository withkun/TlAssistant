#include "shape_color.h"

#include <QColor>


std::vector<QColor> label_colormap() {
    std::vector<QColor> colormap(256);
    for (int32_t label_id = 0; label_id < 256; ++label_id) {
        // 循环 8 次, 每次处理 3 个位 (分别给 R, G, B)
        // i 从 7 降到 0, 对应 Python 中的 np.arange(8)[::-1]
        uint8_t r = 0, g = 0, b = 0;
        uint8_t i_val = static_cast<uint8_t>(label_id);
        for (int32_t i = 7; i >= 0; --i) {
            // 提取当前最低的 3 位
            uint8_t r_bit = (i_val >> 0) & 0x01;
            uint8_t g_bit = (i_val >> 1) & 0x01;
            uint8_t b_bit = (i_val >> 2) & 0x01;
            // 将这些位放到结果字节的第 i 位
            r |= (r_bit << i);
            g |= (g_bit << i);
            b |= (b_bit << i);
            // 右移 3 位, 处理下一组
            i_val >>= 3;
        }

        colormap[label_id] = {r, g, b};
    }
    return colormap;
}

const static std::vector<QColor> LABEL_COLORMAP = label_colormap();


std::tuple<int, int, int> resolve_shape_color(
    const QMap<QString, QVariant> &config, const QString &label, const int32_t label_index
) {
    int32_t r, g, b;
    const auto mode = config["mode"];
    if (mode == "auto") {
        const auto label_id = 1 + label_index + config["auto"].toMap()["shift"].toInt();
        LABEL_COLORMAP[label_id % LABEL_COLORMAP.size()].getRgb(&r, &g, &b);
        return {r, g, b};
    }
    if (mode == "uniform") {
        const auto v = config["uniform"].toMap()["color"].value<QList<int32_t>>();
        return {v[0], v[1], v[2]};
    }

    const auto colors = config["by_label"].toMap()["colors"].toMap();
    auto rgb = colors.value(label, {}).value<QList<int32_t>>();
    if (rgb.empty())
        rgb = config["by_label"].toMap()["fallback"].value<QList<int32_t>>();
    return {rgb[0], rgb[1], rgb[2]};
}