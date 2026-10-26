//
// Created by Jianing on 2025/12/22.
//

#ifndef FFMPEGPROJECT_PACKET_QUEUE_H
#define FFMPEGPROJECT_PACKET_QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

// 前置声明
extern "C" {
#include <libavcodec/packet.h> // AVPacket的完整定义在这个头文件里
}

// 线程安全Packet队列（声明+实现一体，模板类特性）
template <typename T>
class PacketQueue {
public:
    std::queue<T> queue;
    std::mutex mtx;
    std::condition_variable cond;
    std::atomic<bool> stopped_{false};

    // 推送Packet（加锁）
    void push(const T& pkt) {
        std::lock_guard<std::mutex> lock(mtx);
        if (stopped_.load(std::memory_order_acquire)) return;
        queue.push(pkt);
        cond.notify_one();
    }

    // 取出Packet（阻塞/非阻塞）
    bool pop(T& pkt, bool block = true) {
        std::unique_lock<std::mutex> lock(mtx);
        if (block) {
            cond.wait(lock, [this]() {
                return !queue.empty() || stopped_.load(std::memory_order_acquire);
            });
        } else if (queue.empty()) {
            return false;
        }
        if (queue.empty()) return false;
        pkt = queue.front();
        queue.pop();
        return true;
    }

    // 级联终止：唤醒所有等待线程，后续push/pop均返回false
    void stop() {
        stopped_.store(true, std::memory_order_release);
        cond.notify_all();
    }

    // 判断队列是否为空
    bool is_empty() {
        std::lock_guard<std::mutex> lock(mtx);
        return queue.empty();
    }
};
#endif //FFMPEGPROJECT_PACKET_QUEUE_H
