#ifndef __INC_CANVAS_INTERACTION_H
#define __INC_CANVAS_INTERACTION_H

#include <QMenu>
#include "tl_shape.h"


enum class HitKind : int32_t {
    INVALID,            // uninitialized
    VERTEX,             // vertex
    ROTATION_HANDLE,    // rotation_handle
    EDGE,               // edge
    BODY,               // body
};

struct HitTarget {
    HitKind kind{};
    int32_t shape{None};
    int32_t index{None};

    explicit operator bool() const {
        return !(this->kind == HitKind::INVALID && this->shape == None && this->index == None);
    }
};

HitTarget find_hover_target(const QList<TlShape> &shapes, const QPointF &point, float scale, float epsilon, int32_t point_size, int32_t priority_shape);

QList<int32_t> build_candidates(const QList<TlShape> &shapes, int32_t priority_shape);

bool is_within_pick_threshold(const QPointF &a, const QPointF &b, float scale, float epsilon);


enum class CursorRole : int32_t {
    DEFAULT,            // "default"
    DRAW,               // "draw"
    HANDLE,             // "handle"
    GRAB,               // "grab"
    MOVE,               // "move"
};

Qt::CursorShape cursor_shape_for(CursorRole role);


class ContextMenuPair {
public:
    QMenu *without_selection_{};
    QMenu *with_selection_{};

    QMenu *menu_for(const bool has_selection) const {
        if (has_selection)
            return with_selection_;
        return without_selection_;
    }
};
#endif //__INC_CANVAS_INTERACTION_H