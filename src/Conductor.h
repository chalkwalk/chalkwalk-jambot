#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

// The interval grid, driven.
//
// Bots generate rather than react: nothing arrives to trigger the next
// interval, so something has to count. This is that something -- one thread,
// waking on a deadline, calling back once per interval with its index.
//
// FREE-RUNNING, and deliberately not synchronised to any player's grid.
// Ninjam's absolute interval phase is free: every client plays a received
// interval starting at its OWN downbeat, so phase offsets between clients
// cancel out per listener (`PRINCIPLES` 9). Chasing somebody's phase here would
// add a dependency for no audible difference.
//
// JUCE-free, so the same loop drives a band inside a plugin and a band on a
// command line. `std::condition_variable` rather than a sleep because stopping
// has to be prompt: an interval is seconds long and a process that waits one
// out before exiting reads as hung.
//
// SLICED, so the work does not all land on the boundary. A band rendered every
// voice back to back at the top of the interval, which is one long contiguous
// burst of compute -- the shape most likely to starve an audio callback on a
// busy machine. The interval is divided into `slices` instead and the callback
// is invoked once per slice at its own offset, so a caller with one voice per
// slice spreads the same total work across the interval.
//
// The slack is real rather than borrowed: Ninjam transmits interval N while
// N-1 is playing, so a voice rendered three quarters of the way through the
// interval still reaches the server in time to be heard. What this does NOT do
// is make the work cheaper -- one thread doing the same total in smaller
// pieces -- and that distinction is the whole point of it.

namespace jambot {

class Conductor {
public:
  // `render` is called once per interval, on the conductor's thread, with a
  // monotonically increasing index. It may take a while -- encoding four
  // voices is not free -- and the loop accounts for that below.
  using RenderInterval = std::function<void(int intervalIndex)>;

  // The sliced form: called `slices` times per interval, at even offsets
  // through it, with the slice index alongside the interval's. Slice 0 is on
  // the boundary, so a one-slice conductor behaves exactly as this class did
  // before slicing existed.
  using RenderSlice = std::function<void(int intervalIndex, int slice)>;

  Conductor() = default;
  ~Conductor() { stop(); }

  Conductor(const Conductor &) = delete;
  Conductor &operator=(const Conductor &) = delete;

  void start(double intervalSeconds, RenderInterval render) {
    if (!render)
      return;
    start(intervalSeconds, 1,
          [render = std::move(render)](int intervalIndex, int) {
            render(intervalIndex);
          });
  }

  void start(double intervalSeconds, int slices, RenderSlice render) {
    stop();
    if (intervalSeconds <= 0.0 || slices <= 0 || !render)
      return;

    running = true;
    thread = std::thread([this, intervalSeconds, slices,
                          render = std::move(render)] {
      using clock = std::chrono::steady_clock;
      const auto period = std::chrono::duration_cast<clock::duration>(
          std::chrono::duration<double>(intervalSeconds));

      auto nextDue = clock::now();
      int intervalIndex = 0;

      while (running.load()) {
        for (int slice = 0; slice < slices; ++slice) {
          // Offsets are computed from the interval's start rather than
          // accumulated, so a slow slice cannot push the ones after it and the
          // grid stays anchored to the boundary.
          const auto sliceDue = nextDue + (period * slice) / slices;
          {
            std::unique_lock<std::mutex> lock(wakeMutex);
            // Predicated to close the lost-wakeup window, not to speed up the
            // common case: a `stop()` whose notify lands BEFORE this thread
            // reaches the wait would otherwise be missed, and the band would
            // play on for up to a whole interval after being told to stop. The
            // predicate is checked before waiting, so the notification cannot
            // be overtaken.
            if (wake.wait_until(lock, sliceDue,
                                [this] { return !running.load(); }))
              return;
          }
          if (!running.load())
            return;

          render(intervalIndex, slice);
        }

        ++intervalIndex;
        nextDue += period;

        // If the band overran -- a breakpoint, a stalled machine -- skip
        // forward rather than sprinting to catch up, which would burst several
        // intervals onto the wire at once.
        const auto after = clock::now();
        if (nextDue < after)
          nextDue = after + period;
      }
    });
  }

  void stop() {
    {
      std::lock_guard<std::mutex> lock(wakeMutex);
      running = false;
    }
    wake.notify_all();
    if (thread.joinable())
      thread.join();
  }

  bool isRunning() const { return running.load(); }

private:
  std::atomic<bool> running{false};
  std::thread thread;
  std::mutex wakeMutex;
  std::condition_variable wake;
};

} // namespace jambot
