#pragma once

#include <cstdint>
#include <string>

// Structure above the interval: which section of the tune an interval belongs
// to.
//
// Knows nothing about figures or voices. What a section IS lives here; what a
// section CHANGES is BotBand's business, and keeping the two apart is what lets
// the form be tested without rendering a note.
//
// Designed in Antiphon's docs/superpowers/specs/2026-09-04-the-form.md.
namespace Form {

// AABA, ABAC or AAAB, from the room seed -- so `shake` reaches the shape of the
// music and not only its notes.
std::string table(std::uint32_t seed);

// How many intervals a section lasts, from a DURATION rather than a count: the
// seed picks a target of roughly twenty to forty seconds and the metre converts
// it, so a section feels like a section whatever the room's tempo.
//
// Never one, and that bound is not taste. A listener hears the band an interval
// late, so a one-interval section is a whole section out of step in their ear,
// where at N it is 1/N. Never more than eight, because eight intervals of one
// letter at a slow metre stops being a section and becomes the tune.
//
// The seed's reach shrinks as the metre grows: at bpi 32 an interval is already
// about nineteen seconds, so every target in the range clamps to two and the
// seed changes nothing. Real at bpi 8 and 16, nominal above it.
int intervalsPerSection(std::uint32_t seed, int bpm, int bpi);

// Which letter `intervalIndex` belongs to, as an index where 0 is A.
//
// Measured from `origin`, the interval the form last restarted at -- a tune
// starting, or its key, chart, tempo or metre moving. An interval at or before
// the origin is the first interval of the first section: a reset that walked
// backwards would play a letter nobody had heard yet.
int letterAt(std::uint32_t seed, int bpm, int bpi, int origin,
             int intervalIndex);

// A letter, as a density: `basePulses` for A, and a different count inside
// [lo, hi] for anything else.
//
// A DIFFERENT density rather than a greater one -- the direction is the seed's,
// per letter. Two things follow and both are handled here rather than hoped:
// the range is bounded, so at an edge "different" can only go one way and a
// delta that would clamp back onto A's own count is reflected instead; and a
// form like ABAC needs B and C to differ from each other as well as from A, so
// the delta is a function of the LETTER rather than a coin flipped per section.
//
// Density and not rotation, which is forced rather than chosen: both foundation
// figures pin `rotation = 0` because the kick lands on the downbeat, and
// displacing it for a section would undo an argument the code already won.
int pulsesFor(int basePulses, int lo, int hi, int letter, std::uint32_t seed);

} // namespace Form
