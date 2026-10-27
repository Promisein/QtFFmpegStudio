#include "deep_copy_packey_queue.h"
#include "pipeline.h"
#include <chrono>
#include <future>
#include <iostream>
using namespace std::chrono_literals;

struct Output { MuxPacketQueue::Result result; bool video; int64_t dts; };
static Output take(MuxPacketQueue& q) {
    PacketPtr p(av_packet_alloc()); bool video = false;
    auto result = q.pop(*p, video, AVRational{1,25}, AVRational{1,48000});
    return {result, video, p->dts};
}
static bool add(MuxPacketQueue& q, bool video, int64_t dts) {
    PacketPtr p(av_packet_alloc()); p->pts = p->dts = dts;
    return q.push(video, *p);
}
int main() {
    {
        MuxPacketQueue q;
        auto future = std::async(std::launch::async, [&]{ return take(q); });
        if (!add(q, true, 25)) return 1;
        if (future.wait_for(30ms) != std::future_status::timeout) return 2;
        if (!add(q, false, 24000)) return 3;
        if (future.wait_for(2s) != std::future_status::ready) { q.stop(); return 4; }
        Output a = future.get();
        if (a.result != MuxPacketQueue::Result::Packet || a.video || a.dts != 24000) return 5;
        q.finish(false);
        Output b = take(q);
        if (b.result != MuxPacketQueue::Result::Packet || !b.video || b.dts != 25) return 6;
        q.finish(true);
        if (take(q).result != MuxPacketQueue::Result::End) return 7;
    }
    {
        MuxPacketQueue q; q.finish(true);
        auto future = std::async(std::launch::async, [&]{ return take(q); });
        if (!add(q, false, -1024)) return 8;
        if (future.wait_for(2s) != std::future_status::ready) { q.stop(); return 9; }
        Output a = future.get();
        if (a.result != MuxPacketQueue::Result::Packet || a.video || a.dts != -1024) return 10;
        q.finish(false);
        if (take(q).result != MuxPacketQueue::Result::End) return 11;
    }
    for (int i = 0; i < 1000; ++i) {
        MuxPacketQueue q;
        auto future = std::async(std::launch::async, [&]{ return take(q); });
        q.stop();
        if (future.wait_for(2s) != std::future_status::ready) return 12;
        if (future.get().result != MuxPacketQueue::Result::Stopped) return 13;
    }
    for (int i = 0; i < 500; ++i) {
        Pipeline pipeline;
        auto future = std::async(std::launch::async, [&] {
            AVCodecParameters* video = nullptr;
            AVCodecParameters* audio = nullptr;
            return pipeline.wait_for_encoder_parameters(video, audio);
        });
        pipeline.report_error("test failure");
        if (future.wait_for(2s) != std::future_status::ready) return 14;
        if (future.get() || !pipeline.has_error()) return 15;
    }
    for (int i = 0; i < 500; ++i) {
        PacketQueue<AVPacket> queue;
        auto future = std::async(std::launch::async, [&] { AVPacket packet{}; return queue.pop(packet); });
        queue.stop();
        if (future.wait_for(2s) != std::future_status::ready) return 16;
        if (future.get()) return 17;
    }
    std::cout << "mux queue PASS\n";
}
