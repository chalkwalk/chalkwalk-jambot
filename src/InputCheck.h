#pragma once

// Does this look like an instrument somebody could hear?
//
// The one piece of listening in `docs/BOT-CHAT.md` section 7, and the whole of
// what it is allowed to ask. NOT "is that any good" -- it has no business
// having an opinion, and it does not know what you played. It answers the far
// narrower question of whether the audio that arrived is something the rest of
// the room could hear, so the tutor can say "that went out" rather than hope.
//
// Pure functions over a buffer, like `BotChat` is pure functions over a
// snapshot: samples in, a reading out. Nothing here knows there is a tutor.
namespace InputCheck {

// What arrived, in the terms the tutor can act on.
//
// `Playing` is the FALLBACK, not a positive finding, and that is the design
// rather than a shortcut. Section 7 requires that uncertainty says the neutral
// line -- a sparse part, a held drone, somebody warming up quietly must never
// be told they are not playing -- so anything that is not clearly one of the
// four problems is somebody playing. Testing for a confident fundamental or
// transients on a grid would compute a number and then arrive at the same
// answer the fallback already gives, which is why neither is computed.
enum class Reading {
  Silent,   // nothing arrived
  Faint,    // there, but others may struggle to hear it
  Clicks,   // spikes rather than a part; usually a buffer size
  Clipping, // at or above full scale, and it will distort for everyone
  Playing   // somebody playing, or not clearly anything else
};

// Every number the reading is made from, exposed so a test can assert the
// instrument rather than only its verdict -- the standing rule that a detector
// used only by the code that defines it is a detector nobody has calibrated.
struct Signals {
  double peak = 0.0;
  double rms = 0.0;
  double crest = 0.0;

  // Fraction of frames within 40 dB of the loudest frame: how much of the
  // interval had sound in it. What tells a sustained part from a sparse one,
  // and what stops a clipped note being called clipping on the strength of one
  // loud sample.
  double duty = 0.0;

  // The same measurement in SECONDS, and the one a click is judged by.
  //
  // Not a fraction, deliberately. A buffer underrun is a handful of samples --
  // an absolute duration, the same at any tempo -- whereas the fraction it
  // occupies depends on the interval it landed in, so the identical click would
  // be caught at bpi 8 and missed at bpi 32. Judging brevity in seconds also
  // draws the line where the difference actually is: a click is shorter than
  // any envelope an instrument can produce, which is what makes it a click.
  double soundingSeconds = 0.0;
};

// `right` may be null for a single channel. Both are read where present:
// measuring the left alone would call a hard-panned input silent, which is the
// one verdict that must never be wrong.
Signals measure(const float *left, const float *right, int numSamples,
                double sampleRate);

Reading classify(const Signals &s);

inline Reading read(const float *left, const float *right, int numSamples,
                    double sampleRate) {
  return classify(measure(left, right, numSamples, sampleRate));
}

// The thresholds, named so a test asserts against the same constant the
// classifier uses rather than a number retyped beside it.
//
// Section 7 gives these as approximate and they are kept approximate on
// purpose: the consequence of being wrong is a helpful line not said, or said
// when the player was only being quiet, and the second is worse. Every
// threshold below is therefore set so that a real part falls through to
// `Playing`.
inline constexpr double kSilentPeakDb = -60.0;
inline constexpr double kFaintRmsDb = -45.0;
inline constexpr double kClickCrest = 15.0;
inline constexpr double kClickSeconds = 0.005;
inline constexpr double kClipPeak = 0.999;
inline constexpr double kClipDuty = 0.5;

// The frame the duty cycle is measured over, and how far below the loudest
// frame still counts as sound.
//
// One millisecond because the shortest thing that must be told apart from a
// click is a short percussive note, and section 7 names one note per interval
// as a part that must never be called silence. At 10 ms the two were the same
// measurement.
inline constexpr double kFrameSeconds = 0.001;
inline constexpr double kDutyFloorDb = -40.0;

} // namespace InputCheck
