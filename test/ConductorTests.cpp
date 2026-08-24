#include "../src/Conductor.h"
#include "JuceUnitShim.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <vector>

// The interval grid, and specifically its SHAPE in time.
//
// What is under test is that slices land where they are supposed to, because
// that is the whole reason slicing exists: a band that rendered every voice on
// the boundary put a second of contiguous compute there, and spreading it is
// the difference between one long burst and several short ones.
//
// Timing tests are ordinarily a bad idea, so the bounds here are loose on
// purpose -- what is asserted is that slice 3 of 4 happens around three
// quarters of the way through the interval, not that it happens at 750.0 ms.

namespace {

struct Call {
  int intervalIndex;
  int slice;
  double msSinceStart;
};

class ConductorTests : public shim::UnitTest {
public:
  ConductorTests() : shim::UnitTest("Conductor", "bots") {}

  void runTest() override {
    beginTest("an unsliced conductor fires once per interval, on the boundary");
    {
      const auto calls = record(0.2, 1, 5);
      expect(calls.size() >= 3, "the conductor barely ran");

      for (std::size_t i = 0; i < calls.size(); ++i) {
        expectEquals(calls[i].slice, 0);
        expectEquals(calls[i].intervalIndex, (int)i);
      }
    }

    beginTest("slices land at even offsets through the interval");
    {
      // 200 ms interval, four slices: 0, 50, 100, 150 ms.
      const auto calls = record(0.2, 4, 5);
      expect(calls.size() >= 8, "too few slices to judge");

      for (const auto &c : calls) {
        const double intervalStart = c.intervalIndex * 200.0;
        const double offset = c.msSinceStart - intervalStart;
        const double expected = c.slice * 50.0;

        // Generous: a scheduler that is 25 ms late is still a scheduler
        // putting this slice nowhere near the boundary, which is the claim.
        expect(std::abs(offset - expected) < 25.0,
               "interval " + juce::String(c.intervalIndex) + " slice " +
                   juce::String(c.slice) + " landed at " +
                   juce::String(offset, 1) + " ms, expected near " +
                   juce::String(expected, 1));
      }
    }

    beginTest("every slice fires once per interval, in order");
    {
      const auto calls = record(0.2, 4, 5);

      int expectedInterval = 0, expectedSlice = 0;
      for (const auto &c : calls) {
        expectEquals(c.intervalIndex, expectedInterval);
        expectEquals(c.slice, expectedSlice);
        if (++expectedSlice == 4) {
          expectedSlice = 0;
          ++expectedInterval;
        }
      }
    }

    beginTest("a nonsensical slice count is refused rather than guessed at");
    {
      jambot::Conductor c;
      c.start(0.2, 0, [](int, int) {});
      expect(!c.isRunning(), "zero slices started a conductor");
      c.start(0.2, -1, [](int, int) {});
      expect(!c.isRunning(), "a negative slice count started a conductor");
    }

    beginTest("stopping does not wait out the interval");
    {
      // The reason this uses a condition variable rather than a sleep: an
      // interval is seconds long in the real thing, and a process that waits
      // one out before exiting reads as hung.
      jambot::Conductor c;
      c.start(5.0, 4, [](int, int) {});

      const auto before = std::chrono::steady_clock::now();
      c.stop();
      const double tookMs =
          std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - before)
              .count();

      expect(!c.isRunning());
      expect(tookMs < 500.0, "stop() took " + juce::String(tookMs, 1) +
                                 " ms, so it waited for a slice");
    }
  }

private:
  // Runs a conductor for `intervals` intervals and returns what it called,
  // with the time of each call relative to the first.
  std::vector<Call> record(double intervalSeconds, int slices, int intervals) {
    std::mutex lock;
    std::vector<Call> calls;
    std::chrono::steady_clock::time_point start;
    bool started = false;

    jambot::Conductor c;
    c.start(intervalSeconds, slices, [&](int intervalIndex, int slice) {
      const auto now = std::chrono::steady_clock::now();
      std::lock_guard<std::mutex> sl(lock);
      if (!started) {
        start = now;
        started = true;
      }
      calls.push_back({intervalIndex, slice,
                       std::chrono::duration<double, std::milli>(now - start)
                           .count()});
    });

    std::this_thread::sleep_for(std::chrono::duration<double>(
        intervalSeconds * intervals + intervalSeconds * 0.5));
    c.stop();

    std::lock_guard<std::mutex> sl(lock);
    return calls;
  }
};

TEST_CASE("conductor") {
  ConductorTests t;
  t.runTest();
}

} // namespace
