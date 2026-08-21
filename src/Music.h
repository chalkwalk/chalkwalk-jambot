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

// The key itself, under the name the bots already use for it, plus the tag it
// travels in.
//
// While this directory lived inside Antiphon the tag could not be here: that
// project's `MusicalKey.h` opens the same namespace, and two headers defining
// one function is a collision rather than a boundary. Separate builds, so the
// composition comes across.
//
// It is COMPOSITION, not knowledge, and that is why having it on both sides is
// not duplication. The envelope -- the brackets, the line-leading slash, what
// a `!vote` will take -- is single-sourced in `chalkwalk::ninjam::conventions`;
// the spelling is single-sourced in `chalkwalk::music::Notation`. What is here
// is four lines joining them, and a library that owned the join would have to
// depend on both, which is exactly the dependency chalkwalk-ninjam is built to
// avoid: it takes text and returns text so that a protocol library never needs
// a music library.
namespace MusicalKey {
using namespace chalkwalk::music::Notation;

inline std::string tagPrefix() {
  return chalkwalk::ninjam::conventions::keyTagPrefix();
}

// `[key: D minor]` anywhere in a line.
inline Key parseTagged(const std::string &text) {
  return parseName(chalkwalk::ninjam::conventions::extractKeyTag(text));
}

// The line to send. Only this form sets the key, which is why no bot may say
// it -- see BotAnswer.h.
inline std::string buildTagged(const Key &key) {
  if (!key.valid)
    return {};
  return chalkwalk::ninjam::conventions::buildKeyTag(displayName(key));
}

// A key from a chat line: the tag anywhere, or a line-leading `/key`.
inline Key parseAnnouncement(const std::string &line) {
  return parseName(
      chalkwalk::ninjam::conventions::extractKeyAnnouncement(line));
}

// What a bot should tell somebody to type. Deliberately NOT the tag, because
// saying the tag sets the key.
inline std::string announcementAdvice(const Key &key) {
  return chalkwalk::ninjam::conventions::keyAdviceLine(displayName(key));
}
} // namespace MusicalKey

namespace ChatFormat = chalkwalk::ninjam::conventions;
