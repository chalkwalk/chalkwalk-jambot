#pragma once

#include <chalkwalk/music/Harmony.h>
#include <chalkwalk/music/Notation.h>
#include <chalkwalk/ninjam/RoomConventions.h>

#include <string>

// What the bots know about music, and about how a room talks about it.
//
// Both come from shared libraries now. `Harmony` and the key itself are music
// theory (`chalkwalk-music`); the `[key: ...]` envelope and what a `!vote` will
// take are NINJAM room conventions (`chalkwalk-ninjam`). Neither belongs to the
// bots, and neither belongs to Antiphon: the two are siblings, so the shared
// things live beneath both.
//
// The aliases are what let that move happen without touching several hundred
// call sites. They go when this library takes a namespace of its own; that is
// a separate, mechanical change, kept apart from the extraction so each can be
// reviewed on its own terms.

namespace Harmony = chalkwalk::music::Harmony;

// The key itself, under the name the bots already use for it.
namespace MusicalKey {
using namespace chalkwalk::music::Notation;
} // namespace MusicalKey

// The tag a key travels in, which is a room convention rather than theory.
//
// `KeyTag` and not `MusicalKey`, and the separation is worth more than the
// convenience of one name: theory is what a key IS, and this is how a room
// happens to say it. They come from different libraries and change for
// different reasons.
//
// It is also what lets Antiphon hold its own copy under `MusicalKey`, which it
// must -- a plugin reading a key out of chat should not need the band. That is
// not duplication, because it is COMPOSITION rather than knowledge: the
// envelope is single-sourced in `chalkwalk::ninjam::conventions` and the
// spelling in `chalkwalk::music::Notation`. What is here is four lines joining
// them, and a library owning the join would have to depend on both -- exactly
// the dependency chalkwalk-ninjam is built to avoid by taking text and
// returning text, so that a protocol library never needs a music library.
namespace KeyTag {

using Key = chalkwalk::music::Notation::Key;

inline std::string tagPrefix() {
  return chalkwalk::ninjam::conventions::keyTagPrefix();
}

// `[key: D minor]` anywhere in a line.
inline Key parseTagged(const std::string &text) {
  return chalkwalk::music::Notation::parseName(
      chalkwalk::ninjam::conventions::extractKeyTag(text));
}

// The line to send. Only this form sets the key, which is why no bot may say
// it -- see BotAnswer.h.
inline std::string buildTagged(const Key &key) {
  if (!key.valid)
    return {};
  return chalkwalk::ninjam::conventions::buildKeyTag(
      chalkwalk::music::Notation::displayName(key));
}

// A key from a chat line: the tag anywhere, or a line-leading `/key`.
inline Key parseAnnouncement(const std::string &line) {
  return chalkwalk::music::Notation::parseName(
      chalkwalk::ninjam::conventions::extractKeyAnnouncement(line));
}

// What a bot should tell somebody to type. Deliberately NOT the tag, because
// saying the tag sets the key.
inline std::string announcementAdvice(const Key &key) {
  return chalkwalk::ninjam::conventions::keyAdviceLine(
      chalkwalk::music::Notation::displayName(key));
}
} // namespace KeyTag

// The room's conventions, under a short name. NOT `ChatFormat`: Antiphon has a
// header of that name for rendering chat lines, and an alias here would
// collide with it the moment a host includes both.
namespace RoomTalk = chalkwalk::ninjam::conventions;
