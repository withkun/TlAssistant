#include "canvas_interaction.h"
#include "shape_render.h"


HitTarget find_hover_target(
    const QList<TlShape> &shapes,
    const QPointF &point,
    const float scale,
    const float epsilon,
    const int32_t point_size,
    const int32_t priority_shape
) {
    const auto candidates = build_candidates(
        shapes,
        priority_shape
    );

    // Pass 1: vertex proximity : 顶点邻近度
    for (const auto &shape : candidates) {
        const auto idx = nearest_vertex_index(
            shapes[shape], point, scale, epsilon
        );
        if (idx != None)
            return HitTarget{.kind=HitKind::VERTEX, .shape=shape, .index=idx};
    }
    // Pass 2: rotation handle proximity : 旋转手柄邻近度
    for (const auto &shape : candidates) {
        const auto idx = nearest_rotation_point_index(
            shapes[shape], point, scale, epsilon
        );
        if (idx != None)
            return HitTarget{.kind=HitKind::ROTATION_HANDLE, .shape=shape, .index=idx};
    }
    // Pass 3: edge proximity (only shapes that support adding a point) : 边缘接近度(仅适用于支持添加点的形状)
    for (const auto &shape : candidates) {
        if (!shapes[shape].can_add_point())
            continue;
        const auto idx = nearest_edge_index(shapes[shape], point, scale, epsilon);
        if (idx != None)
            return HitTarget{.kind=HitKind::EDGE, .shape=shape, .index=idx};
    }
    // Pass 4: body hit : 主体命中
    for (const auto &shape : candidates) {
        const auto hit = is_hit_by_point(
            shapes[shape],
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

QList<int32_t> build_candidates(
    const QList<TlShape> &shapes,
    const int32_t priority_shape
) {
    QList<int32_t> candidates;
    if (priority_shape != None && shapes[priority_shape].visible_)
        candidates.append(priority_shape);
    for (auto &&[index, shape] : shapes | std::views::enumerate | std::views::reverse) {
        if (!shape.visible_)
            continue;
        if (index == priority_shape)
            continue;
        candidates.append(index);
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