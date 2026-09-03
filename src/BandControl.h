#pragma once

#include "BandPlayState.h"
#include "BotChat.h"

#include <vector>

// What the conductor needs from whoever hosts the room, and nothing else.
//
// One class rather than four callbacks because they are one relationship: a
// host that provides three of them should fail to COMPILE, where four loose
// `std::function`s fail at runtime, in a room, on a Tuesday.
//
// The conductor never learns what a room IS. The cap, the arranging order, the
// naming and the clock all stay the host's, and the answers come back as facts
// rather than as capabilities -- which is what stops this becoming a second
// place any of them can be decided.
class BandControl {
public:
  virtual ~BandControl() = default;

  // The interval being rendered now. Commands are named relative to this, so it
  // is the one number the conductor cannot work out for itself.
  virtual int currentInterval() const = 0;

  // What every player is doing.
  //
  // Asked rather than remembered. The conductor could track what it last
  // commanded, and would be wrong the first time somebody told one bot to stop
  // on its own -- so the fact it states about the band comes from the band.
  virtual std::vector<BandPlayState::State> phases() const = 0;

  // Play, or bring it to an end, taking effect FROM `atInterval`.
  //
  // A stop lands at that interval's head, because an ending is musical and the
  // head is where a band can begin one together. A start lands as soon as there
  // is time to render inside it: waiting for the next head would be an interval
  // of silence between asking and hearing, with nothing musical in the gap.
  //
  // An interval already gone is REFUSED rather than applied late. Late is worse
  // than never here: it would put one player an interval out of step with the
  // rest, which is the split naming an interval exists to prevent.
  virtual void command(BotChat::Act act, int atInterval) = 0;

  // Have up to `count` members each cast `!vote bpm <value>` (or bpi). Returns
  // how many actually did.
  //
  // THE ONE COMMAND WITH NO INTERVAL, and it is a different shape rather than
  // an exception: play and stop name the interval they take effect from
  // because they change what the pump renders, where a vote acts on the server
  // and is outside the audio timeline entirely. Smuggling it through
  // `command` with a meaningless interval would make that argument disappear.
  //
  // The conductor has already decided the room wants this and has cast its own
  // vote; `count` is the remainder, so a host casts as many as it can and says
  // how many that was. Defaulted to none: a band that cannot vote should
  // report nothing rather than look wired up.
  virtual int castVotes(bool isBpm, int value, int count) {
    (void)isBpm;
    (void)value;
    (void)count;
    return 0;
  }

  // One more player. False means the room said no -- the cap belongs to the
  // host and the conductor never learns what it is.
  //
  // Defaulted, because a room that does not grow is still a room: implementing
  // this is a decision, and NOT implementing it should say so rather than
  // looking like nobody wired it up.
  virtual bool addPlayer() { return false; }
};
