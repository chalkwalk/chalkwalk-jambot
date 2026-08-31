#include "../src/TutorBot.h"
#include "FakeBotClient.h"
#include "JuceUnitShim.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// The tutor's thread, driven directly.
//
// Through a room this is six lines separated by several seconds of audio and a
// key change; here it is six calls. That is the whole reason the tutor takes a
// `BotClient::Client` rather than owning a socket.

namespace {

// Only what a tutor touches. It never transmits, never uses a timer and never
// whispers, so this is much smaller than the fake the players need.
using jambot::test::FakeClient;

struct Rig {
  FakeClient *client = nullptr;
  std::unique_ptr<TutorBot> tutor;

  Rig() {
    auto owned = std::make_unique<FakeClient>();
    client = owned.get();
    tutor = std::make_unique<TutorBot>("Tutor", std::move(owned));
    tutor->setOwner("you");
    tutor->join("127.0.0.1", 1234, 48000.0);
  }
};

class TutorBotTests : public shim::UnitTest {
public:
  TutorBotTests() : shim::UnitTest("TutorBot", "bots") {}

  void runTest() override {
    beginTest("it greets on arrival and takes no channel");
    {
      Rig rig;
      expectEquals((int)rig.client->said.size(), 1);
      expect(rig.client->sawChannelCall,
             "the tutor never declared its channels");
      expect(rig.client->lastChannels.empty(),
             "the tutor claimed a channel; it has no audio to put in one");
      expect(rig.tutor->step() == TutorBot::Step::FirstPlayed);
    }

    beginTest("the thread runs in order and the teaching ends at the end");
    {
      Rig rig;
      rig.client->plays("you");
      expect(rig.tutor->step() == TutorBot::Step::SecondPlayed);

      rig.client->plays("you");
      expect(rig.tutor->step() == TutorBot::Step::KeySet);

      rig.client->say("you", "[key: D minor]");
      expect(rig.tutor->step() == TutorBot::Step::Shaken);

      rig.client->say("you", "shake");
      expect(rig.tutor->step() == TutorBot::Step::Done);

      // Six lines: the five steps plus the sign-off.
      expectEquals((int)rig.client->said.size(), 6);
      expect(rig.tutor->isActive(),
             "teaching ended but the conductor left. A conductor cannot part "
             "-- the band always has one, so section 7's 'six lines and it "
             "parts' is now 'its teaching ends'.");
      expect(rig.client->connected,
             "the conductor disconnected -- only the teaching was finite");
    }

    beginTest("somebody else playing does not advance the newcomer's thread");
    {
      // A room where another player is already going must not carry the person
      // being taught past the lines about their own first interval.
      Rig rig;
      rig.client->plays("dave");
      rig.client->plays("dave");
      expect(rig.tutor->step() == TutorBot::Step::FirstPlayed,
             "another player's audio advanced the thread");

      rig.client->plays("you");
      expect(rig.tutor->step() == TutorBot::Step::SecondPlayed);
    }

    beginTest("steps are gated in order, not fired by whatever happens first");
    {
      // A key set before a note is played waits. The thread is a sequence, and
      // line 4 only makes sense after line 3 has explained the delay.
      Rig rig;
      rig.client->say("you", "[key: D minor]");
      rig.client->say("you", "shake");
      expect(rig.tutor->step() == TutorBot::Step::FirstPlayed,
             "the thread skipped ahead");
      expectEquals((int)rig.client->said.size(), 1);
    }

    beginTest("it does not teach bots, or answer itself");
    {
      Rig rig;
      rig.client->plays("you");
      rig.client->plays("you");
      const int before = (int)rig.client->said.size();

      rig.client->say("Delvo[bass-bot]", "[key: D minor]");
      expectEquals((int)rig.client->said.size(), before,
                   "a bot's key announcement advanced the thread");

      rig.client->say("Tutor", "[key: D minor]");
      expectEquals((int)rig.client->said.size(), before,
                   "the tutor answered its own line");
    }

    beginTest("silence does not advance the thread, and says so once");
    {
      // Nothing arrived, so "that interval just went out" would be false. The
      // step has not happened and the tutor waits.
      Rig rig;
      rig.client->playsNothing("you");
      expect(rig.tutor->step() == TutorBot::Step::FirstPlayed,
             "an interval of silence was treated as playing");
      expectEquals((int)rig.client->said.size(), 1,
                   "one quiet interval was remarked on; that is a person "
                   "thinking, not a fault");

      rig.client->playsNothing("you");
      expectEquals((int)rig.client->said.size(), 1);

      // Three agreeing intervals is the point at which it is worth saying.
      rig.client->playsNothing("you");
      expectEquals((int)rig.client->said.size(), 2);
      expect(rig.client->said.back().find("input armed") != std::string::npos,
             "the wrong diagnostic was given for silence");

      // At most once, ever, however long it goes on.
      rig.client->playsNothing("you");
      rig.client->playsNothing("you");
      expectEquals((int)rig.client->said.size(), 2,
                   "the tutor nagged about the same reading twice");

      // And it still lets the thread through the moment audio arrives.
      rig.client->plays("you");
      expect(rig.tutor->step() == TutorBot::Step::SecondPlayed);
    }

    beginTest("a run has to be consecutive to count");
    {
      // Two silent intervals with a played one between them is somebody
      // pausing, not a broken input.
      Rig rig;
      rig.client->playsNothing("you");
      rig.client->playsNothing("you");
      rig.client->plays("you");
      rig.client->playsNothing("you");
      rig.client->playsNothing("you");

      for (const auto &line : rig.client->said)
        expect(line.find("input armed") == std::string::npos,
               "a broken run of silence was reported as a broken input");
    }

    beginTest("quiet audio went out, so it carries the thread AND is mentioned");
    {
      // The distinction the whole gating turns on: this reached the room, so
      // the lesson about the interval delay is true of it and the step has
      // happened. It still earns a line of its own once the reading holds.
      Rig rig;
      rig.client->playsQuietly("you");
      expect(rig.tutor->step() == TutorBot::Step::SecondPlayed,
             "audible-but-quiet audio was treated as nothing arriving");

      rig.client->playsQuietly("you");
      expect(rig.tutor->step() == TutorBot::Step::KeySet);

      // Three agreeing intervals, which is more than the thread needs -- so
      // this row can only ever fire if the check outlives step 2. It is the
      // reason it does.
      rig.client->playsQuietly("you");
      bool mentioned = false;
      for (const auto &line : rig.client->said)
        if (line.find("struggle to hear") != std::string::npos)
          mentioned = true;
      expect(mentioned, "a consistently quiet input was never mentioned");
    }

    beginTest("a diagnostic never displaces the lesson");
    {
      // The thread is what the tutor is for. A quiet player gets the same five
      // lines in the same order as anybody else, and the remark about level is
      // an addition to them rather than a substitution for one.
      Rig rig;
      for (int i = 0; i < 3; ++i)
        rig.client->playsQuietly("you");
      rig.client->say("you", "[key: D minor]");
      rig.client->say("you", "shake");

      expect(rig.tutor->step() == TutorBot::Step::Done);
      expectEquals((int)rig.client->said.size(), 7,
                   "the six lines plus one remark did not both arrive");
    }

    beginTest("a hundred events produce at most ten lines");
    {
      // The test that keeps the tutor from becoming annoying.
      //
      // Section 7 argues the tutor may speak more than a player because it is
      // FINITE BY CONSTRUCTION, and this is what holds the construction to it:
      // five lessons, a sign-off, and four diagnostics that fire once each.
      // Ten is the ceiling however long the session runs and whatever happens
      // in it, so a hundred events of every kind must not find an eleventh.
      Rig rig;
      for (int i = 0; i < 100; ++i) {
        switch (i % 10) {
        case 0:
          rig.client->plays("you");
          break;
        case 1:
          rig.client->playsNothing("you");
          break;
        case 2:
          rig.client->playsQuietly("you");
          break;
        case 3:
          rig.client->playsClipped("you");
          break;
        case 4:
          rig.client->playsClicks("you");
          break;
        case 5:
          rig.client->plays("dave");
          break;
        case 6:
          rig.client->say("you", "[key: D minor]");
          break;
        case 7:
          rig.client->say("you", "shake");
          break;
        case 8:
          rig.client->say("dave", "anyone about?");
          break;
        default:
          rig.client->say("Delvo[bass-bot]", "shake");
          break;
        }
      }

      // Counts EVERY line through the client, which is right while the
      // conductor says nothing on its own initiative. When plural speech
      // moves here -- the roster, the key acknowledgement, common answers --
      // this must count TEACHING lines only, or it will fail for the wrong
      // reason: a conductor legitimately speaks for the band forever, and only
      // the teaching is finite by construction.
      expect((int)rig.client->said.size() <= 10,
             "the tutor said more than its whole vocabulary");
      expect(rig.tutor->step() == TutorBot::Step::Done,
             "a hundred events did not finish a six-line thread");
      expect(rig.tutor->isActive(), "the conductor left after teaching");
    }

    beginTest("an empty interval is not a played one");
    {
      Rig rig;
      rig.client->plays("you", 0);
      expect(rig.tutor->step() == TutorBot::Step::FirstPlayed);
    }
  }
};

TEST_CASE("tutor bot") {
  TutorBotTests t;
  t.runTest();
}

} // namespace
