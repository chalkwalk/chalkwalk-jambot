#include "../src/InputCheck.h"
#include "JuceUnitShim.h"

#include <cmath>
#include <vector>

// Calibrating the tutor's one instrument.
//
// Every signal here has an answer known before it is measured, which is the
// standing rule: a detector used only by the code that defines it is a detector
// nobody has calibrated, and this one decides whether a person is told they are
// not playing. Being wrong in that direction is the worst thing the tutor can
// do, so most of what follows is signals that must read as `Playing`.

namespace {

constexpr double kSr = 48000.0;

// Not `M_PI`: that is a POSIX extension rather than standard C++, and MSVC
// does not define it without _USE_MATH_DEFINES -- so this file built on Linux
// and macOS and failed on Windows for as long as it has existed.
constexpr double kPi = 3.14159265358979323846;

// A whole interval at 100 bpm and bpi 16 -- the practice room's own shape, and
// long enough that a single short note really is a tiny fraction of it. That
// fraction is the trap this instrument was rebuilt to avoid.
constexpr int kInterval = (int)(9.6 * kSr);

std::vector<float> silence(int n = kInterval) {
  return std::vector<float>((std::size_t)n, 0.0f);
}

std::vector<float> sine(double hz, double amplitude, int n = kInterval) {
  std::vector<float> out((std::size_t)n);
  for (int i = 0; i < n; ++i)
    out[(std::size_t)i] =
        (float)(amplitude * std::sin(2.0 * kPi * hz * i / kSr));
  return out;
}

// A struck note: a sine under an exponential decay. `decay` is the time
// constant, so the note is audible for roughly 4.6 of them.
void strike(std::vector<float> &buf, int at, double hz, double amplitude,
            double decay) {
  for (int i = at; i < (int)buf.size(); ++i) {
    const double t = (i - at) / kSr;
    const double env = std::exp(-t / decay);
    if (env < 1e-5)
      break;
    buf[(std::size_t)i] +=
        (float)(amplitude * env * std::sin(2.0 * kPi * hz * t));
  }
}

class InputCheckTests : public shim::UnitTest {
public:
  InputCheckTests() : shim::UnitTest("InputCheck", "bots") {}

  void runTest() override {
    beginTest("nothing arrived");
    {
      const auto s = silence();
      expect(InputCheck::read(s.data(), nullptr, (int)s.size(), kSr) ==
             InputCheck::Reading::Silent);

      // Below the floor but not zero: still nothing anybody could hear.
      const auto hum = sine(60.0, 0.0005);
      expect(InputCheck::read(hum.data(), nullptr, (int)hum.size(), kSr) ==
                 InputCheck::Reading::Silent,
             "a signal 66 dB down was treated as a part");
    }

    beginTest("there, but faint");
    {
      // -50 dBFS rms, which is above the silence floor and below the faint one.
      const auto quiet = sine(220.0, 0.00316 * std::sqrt(2.0));
      const auto sig =
          InputCheck::measure(quiet.data(), nullptr, (int)quiet.size(), kSr);
      expectWithinAbsoluteError(20.0 * std::log10(sig.rms), -50.0, 0.5);
      expect(InputCheck::classify(sig) == InputCheck::Reading::Faint);
    }

    beginTest("clicks are told from playing by how long they last");
    {
      // One sample of full scale, which is what an underrun sounds like.
      auto click = silence();
      click[(std::size_t)(kInterval / 3)] = 1.0f;
      const auto sig =
          InputCheck::measure(click.data(), nullptr, (int)click.size(), kSr);
      expect(sig.soundingSeconds < InputCheck::kClickSeconds);
      expect(InputCheck::classify(sig) == InputCheck::Reading::Clicks);
    }

    beginTest("ONE short note in a whole interval is playing, not clicks");
    {
      // The case section 7 names by hand: one note per interval must never be
      // told it is not playing. It is tiny as a fraction -- 23 ms in 9.6 s, a
      // quarter of one percent -- which is why the click test is in seconds and
      // not in duty. Measured as a fraction this reads as a click.
      auto note = silence();
      strike(note, kInterval / 4, 200.0, 0.9, 0.005);
      const auto sig =
          InputCheck::measure(note.data(), nullptr, (int)note.size(), kSr);

      expect(sig.duty < 0.01, "the note was not as sparse as this test needs");
      expect(sig.crest > InputCheck::kClickCrest,
             "the note was not as spiky as this test needs");
      expect(sig.soundingSeconds > InputCheck::kClickSeconds,
             "a 5 ms strike did not outlast the click threshold");
      expect(InputCheck::classify(sig) == InputCheck::Reading::Playing,
             "one short note per interval was called clicks");
    }

    beginTest("a sparse kit is playing");
    {
      auto kit = silence();
      for (int beat = 0; beat < 4; ++beat)
        strike(kit, beat * kInterval / 4, 60.0, 0.9, 0.12);
      expect(InputCheck::read(kit.data(), nullptr, (int)kit.size(), kSr) ==
                 InputCheck::Reading::Playing,
             "four kicks in an interval were not heard as playing");
    }

    beginTest("a held drone is playing");
    {
      const auto drone = sine(110.0, 0.2);
      expect(InputCheck::read(drone.data(), nullptr, (int)drone.size(), kSr) ==
             InputCheck::Reading::Playing);
    }

    beginTest("clipping needs full scale AND to be there throughout");
    {
      auto hot = sine(220.0, 1.4);
      for (auto &v : hot)
        v = std::max(-1.0f, std::min(1.0f, v));
      const auto sig =
          InputCheck::measure(hot.data(), nullptr, (int)hot.size(), kSr);
      expect(sig.peak >= InputCheck::kClipPeak);
      expect(sig.duty > InputCheck::kClipDuty);
      expect(InputCheck::classify(sig) == InputCheck::Reading::Clipping);

      // The same peak, once. A full-scale click is a click, and the duty guard
      // on the clipping row is what keeps it from being called distortion --
      // which is why neither row depends on being tested first.
      auto single = silence();
      single[(std::size_t)(kInterval / 2)] = 1.0f;
      expect(InputCheck::read(single.data(), nullptr, (int)single.size(),
                              kSr) == InputCheck::Reading::Clicks,
             "one full-scale sample was reported as clipping");
    }

    beginTest("a hard-panned input is not silence");
    {
      const auto left = silence();
      const auto right = sine(220.0, 0.3);
      expect(InputCheck::read(left.data(), right.data(), (int)left.size(),
                              kSr) == InputCheck::Reading::Playing,
             "audio in the right channel alone read as nothing arriving");
    }

    beginTest("nothing is read off the end, and no rate divides by zero");
    {
      const auto s = sine(220.0, 0.3, 64);
      expect(InputCheck::read(nullptr, nullptr, 4800, kSr) ==
             InputCheck::Reading::Silent);
      expect(InputCheck::read(s.data(), nullptr, 0, kSr) ==
             InputCheck::Reading::Silent);
      expect(InputCheck::read(s.data(), nullptr, (int)s.size(), 0.0) ==
             InputCheck::Reading::Silent);

      // Shorter than one frame: measurable as a level, not as a duty cycle.
      const auto sig = InputCheck::measure(s.data(), nullptr, 8, kSr);
      expectWithinAbsoluteError(sig.duty, 0.0, 1e-9);
    }
  }
};

TEST_CASE("input check") {
  InputCheckTests t;
  t.runTest();
}

} // namespace
