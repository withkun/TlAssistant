#include "ai_assist_thread.h"
#include "tl_widgets/tl_canvas.h"


AiAssistThread::AiAssistThread(AiAssistSession *session) : QThread(), session_(session) {
    this->setObjectName("AiAssistThread");
    this->start();
}

AiAssistThread::~AiAssistThread() {
    stop_.store(true, std::memory_order_relaxed);
    cv_.notify_all();
    this->terminate();
    this->wait();
}

// 提交任务, 队列为空时接受开始处理, 队列不空时直接丢弃.
bool AiAssistThread::Submit(const QList<QPointF> &points, const QList<int32_t> &labels) {
    std::lock_guard<std::mutex> lock{mutex_};
    if (busy_.load(std::memory_order_acquire)) { // 处理中丢弃.
        return false;
    }

    pending_task_ = {points, labels};
    busy_.store(true, std::memory_order_release);
    cv_.notify_one();   // 唤醒处理线程.
    return true;
}

void AiAssistThread::run() {
    SPDLOG_INFO("AiAssistThread run enter");
    while (!stop_.load(std::memory_order_relaxed)) {
        QList<QPointF> points;
        QList<int32_t> labels;
        {
            std::unique_lock<std::mutex> lock{mutex_};
            cv_.wait(lock, [this] {
                return stop_.load(std::memory_order_relaxed) ||
                    (busy_.load(std::memory_order_acquire) && pending_task_.has_value() );
            });
            if (stop_.load(std::memory_order_relaxed)) {
                break;
            }
            if (!pending_task_.has_value()) {
                continue;
            }

            auto &[points_, labels_] = pending_task_.value();
            points_.swap(points);
            labels_.swap(labels);
            pending_task_.reset();
        }

        try {
            session_->propose_shapes(points, labels);
        } catch (std::exception &e) {
            SPDLOG_ERROR("AiAssistThread run Exception {}", e.what());
        } catch (...) {
            SPDLOG_ERROR("AiAssistThread run Exception Unknown");
        }

        // 任务完成标记空闲.
        busy_.store(false, std::memory_order_release);
        cv_.notify_all();
    }
    SPDLOG_INFO("AiAssistThread run leave");
}