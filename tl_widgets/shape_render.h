#ifndef __INC_SHAPE_RENDER_H
#define __INC_SHAPE_RENDER_H

#include <QPainterPath>
#include "tl_shape.h"


//@dataclasses.dataclass(frozen=True)
class VertexHighlight {
public:
    int32_t     index;
    QString     mode;                   // Literal["move", "near"]

    //@property
    float size_factor() const {
        return QMap<QString, float>{
            {"move", 1.5f},
            {"near", 4.0f}}[mode];
    }

    //@property
    QString point_type() const {        //-> Literal["square", "round"]:
        if (mode == "move") {
            return "square";
        }
        if (mode == "near") {
            return "round";
        }
        //  typing.assert_never(self.mode);
        return mode;
    }
};

// @dataclasses.dataclass(frozen=True)
class Palette {
public:
    QColor line_;
    QColor fill_;
    QColor select_line_;
    QColor select_fill_;
    QColor vertex_fill_;
    QColor hvertex_fill_;

    //@classmethod
    static Palette from_rgb(const std::vector<int32_t> &rgb) {
        int32_t r = rgb[0], g = rgb[1], b = rgb[2];
        Palette p;
        p.line_           = QColor(r, g, b);
        p.fill_           = QColor(r, g, b, 128);
        p.select_line_    = QColor(255, 255, 255);
        p.select_fill_    = QColor(r, g, b, 155);
        p.vertex_fill_    = QColor(r, g, b);
        p.hvertex_fill_   = QColor(255, 255, 255);
        return p;
    }
};

//@dataclasses.dataclass(frozen=True)
class ShapeRenderContext {
public:
    float               scale_;
    Palette             palette_;
    int                 point_size_;
    QString             point_type_;    //: Literal["square", "round"]
    bool                selected_;
    bool                fill_;
    VertexHighlight     highlight_;
    VertexHighlight     rotation_highlight_;
    bool                show_label_{false};
};

QPainterPath build_image_path(TlShape shape);

#endif //__INC_SHAPE_RENDER_H