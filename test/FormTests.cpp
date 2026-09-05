#include "../src/Form.h"
#include "JuceUnitShim.h"

#include <set>
#include <string>

// The form, without a note of audio. What a section IS is arithmetic over the
// interval index; what a section CHANGES is BotBandTests' business.

namespace {

class FormTests : public shim::UnitTest {
public:
  FormTests() : shim::UnitTest("Form", "music") {}

  void runTest() override {

    beginTest("a form is one of the three tables, and the seed picks");
    {
      std::set<std::string> seen;
      for (std::uint32_t seed = 1; seed <= 60; ++seed)
        seen.insert(Form::table(seed));
      expect(seen.size() > 1, "every seed gave the same form");
      for (const auto &t : seen)
        expect(t == "AABA" || t == "ABAC" || t == "AAAB", "unknown form " + t);
    }

    beginTest("every form starts on A");
    {
      // letterAt returns 0 for the first section by construction, so a table
      // that did not start on A would disagree with its own first interval.
      for (std::uint32_t seed = 1; seed <= 60; ++seed)
        expect(Form::table(seed).front() == 'A',
               "form " + Form::table(seed) + " does not open on A");
    }

    beginTest("a section is never one interval, and never more than eight");
    {
      // The lower bound is the interval delay: at one interval per section the
      // boundary a listener hears is a whole section out of step with the one
      // they are playing over. At N it is 1/N.
      for (std::uint32_t seed = 1; seed <= 40; ++seed)
        for (int bpm : {60, 100, 120, 180})
          for (int bpi : {2, 8, 16, 32, 64}) {
            const int n = Form::intervalsPerSection(seed, bpm, bpi);
            expect(n >= 2 && n <= 8,
                   "section of " + std::to_string(n) + " at bpm " +
                       std::to_string(bpm) + " bpi " + std::to_string(bpi));
          }
    }

    beginTest("nonsense metres do not produce a nonsense form");
    {
      expect(Form::intervalsPerSection(1u, 0, 16) >= 2);
      expect(Form::intervalsPerSection(1u, 100, 0) >= 2);
      expect(Form::intervalsPerSection(1u, -100, -16) >= 2);
    }

    beginTest("a section lasts about the same time whatever the metre");
    {
      // The point of deriving it from a duration. Outside the clamp the metre
      // wins, which is why this asserts a range rather than a constant.
      for (int bpi : {8, 16}) {
        const int n = Form::intervalsPerSection(12345u, 100, bpi);
        const double seconds = (double)n * (double)bpi * 60.0 / 100.0;
        expect(seconds >= 15.0 && seconds <= 60.0,
               "section of " + std::to_string(seconds) + "s at bpi " +
                   std::to_string(bpi));
      }
    }

    beginTest("the letter walks the table, from the origin");
    {
      const std::uint32_t seed = 7;
      const int n = Form::intervalsPerSection(seed, 100, 16);
      const auto t = Form::table(seed);
      for (int i = 1; i < 40; ++i) {
        const int letter = Form::letterAt(seed, 100, 16, 0, i);
        const int section = i / n;
        expectEquals(letter,
                     (int)(t[(std::size_t)(section % (int)t.size())] - 'A'),
                     "interval " + std::to_string(i));
      }
    }

    beginTest("the origin restarts the form, it does not shift it");
    {
      // An origin of K must make interval K the first interval of the first
      // section, or a reset lands the band mid-structure -- which is what the
      // reset exists to prevent.
      const std::uint32_t seed = 7;
      for (int origin : {0, 1, 5, 13})
        expectEquals(Form::letterAt(seed, 100, 16, origin, origin), 0,
                     "origin " + std::to_string(origin) +
                         " did not start on A");
    }

    beginTest("an interval before the origin is the first section");
    {
      // Reachable: the origin is set from the chat thread while the pump is
      // already rendering. Negative distances must not walk the table
      // backwards into a letter nobody has heard.
      const std::uint32_t seed = 7;
      for (int i = 0; i < 5; ++i)
        expectEquals(Form::letterAt(seed, 100, 16, 10, i), 0,
                     "interval " + std::to_string(i) + " before origin 10");
    }

    beginTest("the form comes round");
    {
      // A table of N letters over sections of n intervals repeats every N*n.
      const std::uint32_t seed = 3;
      const int n = Form::intervalsPerSection(seed, 100, 16);
      const int period = n * (int)Form::table(seed).size();
      for (int i = 0; i < 60; ++i)
        expectEquals(Form::letterAt(seed, 100, 16, 0, i + period),
                     Form::letterAt(seed, 100, 16, 0, i),
                     "interval " + std::to_string(i));
    }

    // --- a letter, as a density ---

    beginTest("letter A is the tune's own figure, untouched");
    {
      for (int base = 3; base <= 12; ++base)
        expectEquals(Form::pulsesFor(base, 3, 16, 0, 99u), base);
    }

    beginTest("another letter is a different density, either way");
    {
      // "Different", not "denser": the direction is the seed's, per letter.
      std::set<int> directions;
      for (std::uint32_t seed = 1; seed <= 80; ++seed) {
        const int p = Form::pulsesFor(8, 3, 16, 1, seed);
        expect(p != 8,
               "letter B kept A's density at seed " + std::to_string(seed));
        directions.insert(p > 8 ? 1 : -1);
      }
      expect(directions.size() == 2,
             "every seed moved the density the same way");
    }

    beginTest("letters differ from each other, not only from A");
    {
      // ABAC needs B and C distinguishable, or the form has three letters and
      // two sounds.
      int differ = 0, total = 0;
      for (std::uint32_t seed = 1; seed <= 80; ++seed) {
        ++total;
        if (Form::pulsesFor(8, 3, 16, 1, seed) !=
            Form::pulsesFor(8, 3, 16, 2, seed))
          ++differ;
      }
      expect(differ > total / 2,
             "B and C landed on the same density more often than not");
    }

    beginTest("a density is always inside the voice's range");
    {
      for (int base = 3; base <= 16; ++base)
        for (int letter = 1; letter <= 3; ++letter)
          for (std::uint32_t seed = 1; seed <= 40; ++seed) {
            const int p = Form::pulsesFor(base, 3, 16, letter, seed);
            expect(p >= 3 && p <= 16,
                   "density " + std::to_string(p) + " outside 3..16");
          }
    }

    beginTest("at the edge of the range it reflects rather than collapsing");
    {
      // A figure already at its sparsest can only get denser, and one at its
      // densest can only thin. Clamping would silently return A's own count
      // and the section would vanish with nothing to say so.
      for (std::uint32_t seed = 1; seed <= 40; ++seed) {
        expect(Form::pulsesFor(3, 3, 16, 1, seed) > 3, "sparse edge collapsed");
        expect(Form::pulsesFor(16, 3, 16, 1, seed) < 16, "dense edge collapsed");
      }
    }

    beginTest("a range too narrow to move in leaves the figure alone");
    {
      // Better one letter that sounds like another than a figure outside what
      // the voice can play.
      expectEquals(Form::pulsesFor(4, 4, 4, 1, 12345u), 4);
      expectEquals(Form::pulsesFor(4, 5, 3, 1, 12345u), 4);
    }
  }
};

TEST_CASE("form") {
  FormTests t;
  t.runTest();
}

} // namespace
