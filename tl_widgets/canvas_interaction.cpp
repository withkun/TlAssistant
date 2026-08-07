#include "canvas_interaction.h"
#include "shape_render.h"


HitTarget::operator bool() const {
    return this->kind != HitKind::INVALID && this->shape && this->index != -1;
}

HitTarget find_hover_target(
    const QList<TlShape> &shapes,
    const QPointF &point,
    float scale,
    float epsilon,
    int32_t point_size,
    const TlShape &priority_shape
) {
    const auto candidates = build_candidates(
        shapes,
        priority_shape
    );

    // Pass 1: vertex proximity
    for (const auto &shape : candidates) {
        const auto idx = nearest_vertex_index(
            shape, point, scale, epsilon
        );
        if (idx != None)
            return HitTarget{.kind=HitKind::VERTEX, .shape=shape, .index=idx};
    }
    // Pass 2: rotation handle proximity
    for (const auto &shape : candidates) {
        const auto idx = nearest_rotation_point_index(
            shape, point, scale, epsilon
        );
        if (idx != None)
            return HitTarget{.kind=HitKind::ROTATION_HANDLE, .shape=shape, .index=idx};
    }
    // Pass 3: edge proximity (only shapes that support adding a point)
    for (const auto &shape : candidates) {
        if (!shape.can_add_point())
            continue;
        const auto idx = nearest_edge_index(shape, point, scale, epsilon);
        if (idx != None)
            return HitTarget{.kind=HitKind::EDGE, .shape=shape, .index=idx};
    }
    // Pass 4: body hit
    for (const auto &shape : candidates) {
        const auto hit = is_hit_by_point(
            shape,
            point,
            scale,
            point_size,
            epsilon
        );
        if (hit)
            return HitTarget{.kind=HitKind::BODY, .shape=shape, .index=None};
    }
    return {};
}

QList<TlShape> build_candidates(
    const QList<TlShape> &shapes,
    const TlShape &priority_shape
) {
    QList<TlShape> candidates;
    if (priority_shape && priority_shape.visible_)
        candidates.append(priority_shape);
    for (auto &shape : shapes | std::views::reverse) {
        if (!shape.visible_)
            continue;
        if (shape == priority_shape)
            continue;
        candidates.append(shape);
    }
    return candidates;
}

bool is_within_pick_threshold(
    const QPointF &a,
    const QPointF &b,
    const float scale,
    const float epsilon
) {
    return bool(utils::distance(a - b) < epsilon / scale);
}

QMap<CursorRole, Qt::CursorShape> CURSOR_SHAPE_MAP_ {
    { CursorRole::DEFAULT,       Qt::CursorShape::ArrowCursor },
    { CursorRole::DRAW,          Qt::CursorShape::CrossCursor },
    { CursorRole::HANDLE,        Qt::CursorShape::PointingHandCursor },
    { CursorRole::GRAB,          Qt::CursorShape::OpenHandCursor },
    { CursorRole::MOVE,          Qt::CursorShape::ClosedHandCursor },
};

Qt::CursorShape cursor_shape_for(const CursorRole role) {
    return CURSOR_SHAPE_MAP_[role];
}