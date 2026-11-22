#include <pxs/core/feed/change_feed.h>

#include "check.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

using pxs::core::ChangeFeed;

namespace {

struct Recorder : ChangeFeed::Observer {
    std::vector<std::string> events;
    void parameterWritten(std::uint64_t holder, std::string_view path) override
    {
        events.push_back("p " + std::to_string(holder) + " " + std::string(path));
    }
    void layerChanged(std::uint64_t layer) override { events.push_back("l " + std::to_string(layer)); }
    void addressChanged(std::string_view address) override { events.push_back("a " + std::string(address)); }
};

void nobodyListening()
{
    ChangeFeed feed;
    CHECK(!feed.listening());
    feed.parameter(1, "/x"); // nothing happens, nothing crashes
    feed.layer(2);
    feed.address("/y");
}

void theListenerHearsInOrder()
{
    ChangeFeed feed;
    Recorder r;
    feed.observe(&r);
    CHECK(feed.listening());
    feed.parameter(0, "/composition/opacity");
    feed.layer(7);
    feed.address("/layer/7");
    feed.ignore(&r);
    CHECK(!feed.listening());
    feed.parameter(1, "/after"); // not heard
    CHECK_EQ(r.events.size(), std::size_t(3));
    CHECK_EQ(r.events[0], std::string("p 0 /composition/opacity"));
    CHECK_EQ(r.events[1], std::string("l 7"));
    CHECK_EQ(r.events[2], std::string("a /layer/7"));
}

void ignoringAnotherListenerChangesNothing()
{
    ChangeFeed feed;
    Recorder a, b;
    feed.observe(&a);
    feed.ignore(&b); // b never listened: a still does
    CHECK(feed.listening());
    feed.layer(1);
    CHECK_EQ(a.events.size(), std::size_t(1));
    feed.ignore(&a);
}

void oneListenerAtATime()
{
    ChangeFeed feed;
    Recorder a, b;
    feed.observe(&a);
    feed.observe(&b);
    feed.layer(1);
    CHECK(a.events.empty());
    CHECK_EQ(b.events.size(), std::size_t(1));
    feed.ignore(&b);
}

// The one guarantee that matters: once `ignore` returns, no callback is running or will run, so the listener can be
// destroyed. Writers hammer the feed from several threads while the listener is created, installed, removed and destroyed
// again and again; run under ThreadSanitizer and AddressSanitizer this fails if a callback ever reaches a dead listener.
void ignoreWaitsForCallbacksInFlight()
{
    ChangeFeed feed;
    std::atomic<bool> stop{false};
    std::vector<std::thread> writers;
    for (int t = 0; t < 4; ++t)
        writers.emplace_back([&] {
            std::uint64_t n = 0;
            while (!stop.load(std::memory_order_relaxed)) {
                feed.parameter(n++, "/a/b/c");
                feed.layer(n);
                feed.address("/x");
            }
        });
    struct Counting : ChangeFeed::Observer {
        std::atomic<long> heard{0};
        char padding[64] = {};
        void parameterWritten(std::uint64_t, std::string_view) override { heard.fetch_add(1, std::memory_order_relaxed); }
        void layerChanged(std::uint64_t) override { heard.fetch_add(1, std::memory_order_relaxed); }
        void addressChanged(std::string_view) override { heard.fetch_add(1, std::memory_order_relaxed); }
    };
    long total = 0;
    for (int round = 0; round < 300; ++round) {
        auto *listener = new Counting;
        feed.observe(listener);
        std::this_thread::yield();
        feed.ignore(listener);
        total += listener->heard.load();
        delete listener; // a callback still running here would write into freed memory
    }
    stop = true;
    for (auto &w : writers) w.join();
    CHECK(total >= 0);
    CHECK(!feed.listening());
}

} // namespace

int main()
{
    nobodyListening();
    theListenerHearsInOrder();
    ignoringAnotherListenerChangesNothing();
    oneListenerAtATime();
    ignoreWaitsForCallbacksInFlight();
    return pxs_test::finish("change_feed");
}
