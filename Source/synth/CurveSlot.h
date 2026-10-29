#pragma once
#include "core/Curve.h"
#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace kf
{
/** Lock-free (for readers) swappable immutable Curve snapshot.

    Writer side (message thread): set(). Reader side (audio thread): acquire() / release().
    A snapshot stays alive while any reader holds it (refcount) or while a reader is mid-acquire
    (hazard pointer). Retired snapshots are freed by the writer on later set()/collect() calls,
    so the audio thread never allocates, frees or locks. */
class CurveSlot
{
public:
    struct Snapshot
    {
        explicit Snapshot (Curve c) : curve (std::move (c)) {}
        Curve curve;
        mutable std::atomic<int> refs { 0 };
    };

    explicit CurveSlot (Curve initial = {})
    {
        owned.push_back (std::make_unique<Snapshot> (std::move (initial)));
        current.store (owned.back().get());
    }

    /** Message thread. Publishes a new immutable snapshot. */
    void set (Curve c)
    {
        std::lock_guard<std::mutex> g (writerLock);
        owned.push_back (std::make_unique<Snapshot> (std::move (c)));
        current.store (owned.back().get());
        sweepLocked();
    }

    /** Message thread. Frees retired snapshots nobody uses any more. */
    void collect()
    {
        std::lock_guard<std::mutex> g (writerLock);
        sweepLocked();
    }

    /** Message thread: copy of the current curve. */
    Curve get() const
    {
        std::lock_guard<std::mutex> g (writerLock);
        return current.load()->curve;
    }

    /** Audio thread. Never blocks. Must be paired with release(). */
    const Snapshot* acquire() noexcept
    {
        for (;;)
        {
            auto* p = current.load();
            hazard.store (p);
            if (current.load() == p)
            {
                p->refs.fetch_add (1);
                hazard.store (nullptr);
                return p;
            }
        }
    }

    static void release (const Snapshot* s) noexcept
    {
        if (s != nullptr) s->refs.fetch_sub (1);
    }

    size_t numSnapshotsAlive() const
    {
        std::lock_guard<std::mutex> g (writerLock);
        return owned.size();
    }

private:
    void sweepLocked()
    {
        auto* cur = current.load();
        auto* hz = hazard.load();
        std::erase_if (owned, [&] (const std::unique_ptr<Snapshot>& s)
                       { return s.get() != cur && s.get() != hz && s->refs.load() == 0; });
    }

    mutable std::mutex writerLock;
    std::vector<std::unique_ptr<Snapshot>> owned;
    std::atomic<const Snapshot*> current { nullptr };
    std::atomic<const Snapshot*> hazard { nullptr };
};
} // namespace kf
