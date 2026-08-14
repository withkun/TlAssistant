#ifndef __INC_AI_ASSIST_SESSION_H
#define __INC_AI_ASSIST_SESSION_H

#include "tl_shape.h"
#include "sam_session.h"
#include "ai_assist_thread.h"
#include "shape_builder.h"


class Canvas;
class AiAssistSession {
public:
    explicit AiAssistSession(Canvas *canvas,
                             const std::string &model_name="sam2:latest",
                             const std::string &output_format="polygon");

    QList<TlShape> submit_propose_shapes(const QPixmap &image, size_t image_id,
                                         const QList<QPointF> &points, const QList<int32_t> &point_labels,
                                         const QList<TlShape> &existing_shapes);

    std::string                         model_name_;
    std::string                         output_format_;

private:
    friend class AiAssistThread;
    Canvas                             *canvas_{};
    std::unique_ptr<SamSession>         sam_session_;
    std::unique_ptr<AiAssistThread>     ai_assist_thread_;

    std::mutex                          mutex_;  // lock for AI shape.
    QString                             create_mode_;

    QList<QPointF>                      ai_assist_points_;
    QList<TlShape>                      ai_assist_shapes_;

    SamSession &get_session();
    void propose_shapes(const QList<QPointF> &points, const QList<int32_t> &point_labels);
};

QList<Detection> detections_from_annotations(const std::vector<Annotation> &annotations);

#endif //__INC_AI_ASSIST_SESSION_H