#pragma once
// Who is told that something an OSC client can read has changed (the OSC feedback). Nothing polls: what writes a parameter,
// a control or a layer's state says so, the one who listens (if any) hears it, and reads only what was named.
//
// Header-only, no dependency beyond the C++17 standard library. One listener at a time.
//
// Threads: the notifications (`parameter`, `layer`, `address`) may come from any thread, often with the writer's lock held:
// the listener must only note what happened and return, and must not call `observe` or `ignore` from inside a callback
// (`ignore` waits for the callbacks in flight, so it would wait for itself).
#include <atomic>
#include <cstdint>
#include <string_view>
#include <thread>

namespace pxs::core {

class ChangeFeed
{
public:
    class Observer
    {
    public:
        virtual ~Observer() = default;
        // A parameter was written: its holder (a layer's id; 0: the composition, an interaction, the show, a fixture) and
        // its address in the holder
        virtual void parameterWritten(std::uint64_t holder, std::string_view path) = 0;
        // Everything of a layer's own (its flags, its source, its transport) may have changed
        virtual void layerChanged(std::uint64_t layer) = 0;
        // What is at this OSC address, and below it, may have changed
        virtual void addressChanged(std::string_view address) = 0;
    };

    // Starts listening (the one that listened before is replaced: call `ignore` with it first if its callbacks must be over)
    void observe(Observer *o) noexcept { m_observer.store(o, std::memory_order_release); }
    // Stops `o` listening, if it is the one that does, and returns once the callbacks already on their way are over, so that
    // `o` can then be destroyed.
    void ignore(Observer *o) noexcept
    {
        Observer *expected = o;
        m_observer.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
        while (m_calls.load(std::memory_order_acquire) > 0) std::this_thread::yield();
    }
    bool listening() const noexcept { return m_observer.load(std::memory_order_acquire) != nullptr; }

    void parameter(std::uint64_t holder, std::string_view path) const
    {
        Call call(*this);
        if (Observer *o = m_observer.load(std::memory_order_acquire)) o->parameterWritten(holder, path);
    }
    void layer(std::uint64_t id) const
    {
        Call call(*this);
        if (Observer *o = m_observer.load(std::memory_order_acquire)) o->layerChanged(id);
    }
    void address(std::string_view address) const
    {
        Call call(*this);
        if (Observer *o = m_observer.load(std::memory_order_acquire)) o->addressChanged(address);
    }

private:
    struct Call { // a notification on its way: the listener is not destroyed under it
        explicit Call(const ChangeFeed &f) : feed(f) { feed.m_calls.fetch_add(1, std::memory_order_acq_rel); }
        ~Call() { feed.m_calls.fetch_sub(1, std::memory_order_acq_rel); }
        Call(const Call &) = delete;
        Call &operator=(const Call &) = delete;
        const ChangeFeed &feed;
    };
    std::atomic<Observer *> m_observer{nullptr};
    mutable std::atomic<int> m_calls{0};
};

} // namespace pxs::core
