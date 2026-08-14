#ifndef __INC_AI_ASSIST_THREAD_H
#define __INC_AI_ASSIST_THREAD_H

#include <QThread>


class AiAssistSession;
class AiAssistThread : public QThread {
    Q_OBJECT
    struct Task {
        QList<QPointF> points_;
        QList<int32_t> labels_;
    };
public:
    explicit AiAssistThread(AiAssistSession *session);
    ~AiAssistThread() override;

    // 提交任务, 队列为空时接受开始处理, 队列不空时直接丢弃.
    bool Submit(const QList<QPointF> &points, const QList<int32_t> &labels);

protected:
    void run() override;

private:
    AiAssistSession            *session_{nullptr};

    std::atomic<bool>           stop_{false};       // 退出标识
    std::atomic<bool>           busy_{false};       // 执行标识
    std::optional<Task>         pending_task_;      // 待处理任务
    std::condition_variable     cv_;
    std::mutex                  mutex_;
};
#endif //__INC_AI_ASSIST_THREAD_H