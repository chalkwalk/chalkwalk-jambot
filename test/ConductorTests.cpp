#include "../src/Conductor.h"
#include "../src/TutorBot.h"
#include "FakeBandControl.h"
#include "FakeBotClient.h"
#include "JuceUnitShim.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>
#include <string>

// The conductor, driven without a room.
//
// Everything here is about the client lifecycle a band leader needs and a
// player does not: it arrives with no instrument, it can be got rid of, and it
// knows who it is playing with.

namespace {

using jambot::test::FakeBandControl;
using jambot::test::FakeClient;

struct Rig {
  FakeClient *client = nullptr;
  FakeBandControl control;
  std::unique_ptr<Conductor> conductor;

  Rig() {
    auto owned = std::make_unique<FakeClient>();
    client = owned.get();
    conductor = std::make_unique<Conductor>("Vell[bot]", std::move(owned));
    conductor->setControl(&control);
    conductor->join("127.0.0.1", 2049, 48000.0);
  }
};

// A conductor that speaks the moment it is connected, which is what the tutor
// does to greet. It exists to pin an ordering: `connect` can fire onConnected
// synchronously, so a conductor that is not yet "active" at that point silently
// loses the first line it ever says.
class GreetingConductor : public Conductor {
public:
  using Conductor::Conductor;

protected:
  void onConnected() override { say("hello"); }
};

class ConductorTests : public shim::UnitTest {
public:
  ConductorTests() : shim::UnitTest("Conductor", "bots") {}

  void runTest() override {
    beginTest("joins with no channel at all");
    {
      Rig rig;
      expect(rig.client->sawChannelCall, "it declared its channels");
      expect(rig.client->lastChannels.empty(),
             "and declared none -- a conductor is a name that talks, not a "
             "strip in anybody's mixer");
      expect(rig.client->connected);
      expect(rig.conductor->isActive());
    }

    beginTest("parting is idempotent");
    {
      Rig rig;
      rig.conductor->part();
      expect(!rig.conductor->isActive());
      rig.conductor->part();
      expect(!rig.conductor->isActive(),
             "a second part is a no-op, not a crash");
    }

    beginTest("says what it is told to say");
    {
      Rig rig;
      rig.conductor->say("The Understudies: Mirn (kit), Vell (bass).");
      expect(rig.client->said.size() == 1);
      expect(rig.client->said.front() ==
             "The Understudies: Mirn (kit), Vell (bass).");
    }

    beginTest("a parted conductor says nothing");
    {
      Rig rig;
      rig.conductor->part();
      rig.conductor->say("anybody there?");
      expect(rig.client->said.empty(),
             "speech after parting is a line nobody can answer");
    }

    beginTest("a line said from onConnected is not swallowed");
    {
      auto owned = std::make_unique<FakeClient>();
      auto *client = owned.get();
      GreetingConductor c("Vell[bot]", std::move(owned));
      c.join("127.0.0.1", 2049, 48000.0);
      expect(client->said.size() == 1,
             "the greeting was dropped -- `say` is guarded on being active, so "
             "becoming active after connect() loses anything said from the "
             "callback connect() fires");
    }

    beginTest("names the room once, after the arrival window");
    {
      Rig rig;
      rig.conductor->setBandName("The Understudies");
      rig.client->joins("Quado[kit-bot]");
      rig.client->joins("Vessa[bass-bot]");

      expect(rig.client->said.empty(),
             "it announced before the room had finished assembling");

      rig.client->fireDueTimers();

      expect(rig.client->said.size() == 2,
             "expected a roster and the line that says how to use it");
      const auto &line = rig.client->said.front();

      // The way IN first, then how to reach one of us, and the destructive one
      // last: a first-time player who types the first command they are shown
      // must not empty their own room.
      const auto &how = rig.client->said[1];
      const auto nameAt = how.find("say a name");
      const auto leaveAt = how.find("leave");
      expect(nameAt != std::string::npos && leaveAt != std::string::npos, how);
      expect(nameAt < leaveAt,
             "the eviction command is offered before the interesting one: " + how);
      expect(line.find("The Understudies") != std::string::npos, line);
      expect(line.find("quado") != std::string::npos, line);
      expect(line.find("vessa") != std::string::npos, line);
    }

    beginTest("it does not name the room twice");
    {
      Rig rig;
      rig.client->joins("Quado[kit-bot]");
      rig.client->fireDueTimers();
      const auto after = rig.client->said.size();
      expect(after == 2, "no roster to begin with");

      // The timer must be RE-ARMED to test this at all. `fireDueTimers` fires
      // only armed timers and ManualTimer stops itself before firing, so
      // calling it twice in a row exercises nothing -- which is how the first
      // version of this test passed with the guard removed.
      rig.conductor->join("127.0.0.1", 2049, 48000.0);
      rig.client->fireDueTimers();

      expect(rig.client->said.size() == after,
             "the conductor introduced the room twice");
    }

    beginTest("the roster is the band, not everybody present");
    {
      Rig rig;
      rig.conductor->setOwner("you");
      rig.client->joins("Quado[kit-bot]");
      rig.client->joins("you");
      rig.client->fireDueTimers();

      expect(rig.client->said.size() == 2);
      expect(rig.client->said.front().find("you") == std::string::npos,
             "the roster listed a human: " + rig.client->said.front());
    }

    beginTest("a conductor that also teaches still names the room");
    {
      // The trap this exists for: arming the arrival from onConnected would be
      // swallowed by TutorBot's own override, which does not chain -- and the
      // room that breaks is the DEFAULT one, because the tutor is on at the
      // command line while every fixture here turns it off. Green tests,
      // broken product.
      auto owned = std::make_unique<FakeClient>();
      auto *client = owned.get();
      TutorBot tutor("Tutor[bot]", std::move(owned));
      tutor.setBandName("The Understudies");
      tutor.join("127.0.0.1", 2049, 48000.0);
      client->joins("Quado[kit-bot]");

      const auto beforeRoster = client->said.size();
      client->fireDueTimers();

      bool named = false;
      for (auto i = beforeRoster; i < client->said.size(); ++i)
        if (client->said[i].find("quado") != std::string::npos)
          named = true;
      expect(named,
             "the tutor's onConnected override swallowed the conductor's "
             "arrival, so the default room would have had no roster");
    }

    beginTest("it waits for somebody to read the roster");
    {
      // The room is started by a host process, so the band connects seconds
      // before any human does. Announcing on a fixed timer means announcing to
      // an empty room -- the one line the band gets, said to nobody. So the
      // arrival re-arms for the FIRST human, and only the first: with anybody
      // else already there the band has been seen.
      Rig rig;
      rig.client->joins("Quado[kit-bot]");
      rig.client->fireDueTimers();
      const auto toNobody = rig.client->said.size();
      expect(toNobody == 2, "it should still have introduced the band");

      rig.client->joins("you");
      rig.client->fireDueTimers();
      expect(rig.client->said.size() == toNobody + 2,
             "a human arrived and the band was not introduced to them");

      // A second human is not a second roster.
      rig.client->joins("dave");
      rig.client->fireDueTimers();
      expect(rig.client->said.size() == toNobody + 2,
             "the band introduced itself again for a second human");
    }

    beginTest("asked for another player, it asks the room");
    {
      Rig rig;
      rig.client->joins("you");
      rig.client->say("you", "band, add a player");

      expect(rig.control.added == 1, "the conductor did not ask the room");
      expect(!rig.client->said.empty(), "it said nothing about it");
    }

    beginTest("it names the newcomer, so the band does not grow silently");
    {
      // A band that gains a player without a word reads as a process starting,
      // which is what the roster exists to prevent. The roster itself is not
      // re-posted -- everyone already met the others -- so this is the one
      // line that says who just arrived.
      Rig rig;
      rig.client->joins("you");
      // Whoever hosts the room adds the player, so by the time the call
      // returns the newcomer is in the room. The conductor reads the room
      // rather than being told a name.
      rig.control.onAdd = [&] { rig.client->joins("Vurn[horn-bot]"); };
      rig.client->say("you", "band, add a player");

      expect(!rig.client->said.empty());
      expect(rig.client->said.back().find("vurn") != std::string::npos,
             "the newcomer was not named: " + rig.client->said.back());
      expect(rig.client->said.back().find("horn") != std::string::npos,
             "it did not say what they play: " + rig.client->said.back());
    }

    beginTest("a refusal is a rule speaking, not silence");
    {
      Rig rig;
      rig.control.allowGrowth = false;
      rig.client->joins("you");
      const auto before = rig.client->said.size();
      rig.client->say("you", "band, add a player");

      expect(rig.client->said.size() == before + 1,
             "the room refused and nobody said so");
      const auto &line = rig.client->said.back();
      expect(line.find("as many") != std::string::npos,
             "the refusal does not say why: " + line);
    }

    beginTest("an unaddressed request is not one");
    {
      Rig rig;
      rig.client->joins("you");
      rig.client->say("you", "add a player");

      expect(rig.control.added == 0,
             "nobody is addressed by default, and a conductor is not an "
             "exception to that");
    }

    beginTest("it does not recruit on a bot's say-so");
    {
      // The band's own chat is the loudest thing in a practice room, and a
      // conductor that recruited on a bot's line would grow the band with
      // nobody asking.
      //
      // Enforced in `BotAddress::classify`, which returns Ignore for any bot
      // sender -- "a bot never triggers a bot", stated there as a property of
      // what can cause speech at all. A guard here as well was written and
      // then removed: it was a second home for one rule, and removing it did
      // not change this result, which is how it was found to be duplication
      // rather than defence.
      Rig rig;
      rig.client->joins("Quado[kit-bot]");
      rig.client->say("Quado[kit-bot]", "band, add a player");

      expect(rig.control.added == 0,
             "a bot asked for a player and the conductor obliged");
    }

    beginTest("a stop is one line about the band, not four");
    {
      Rig rig;
      rig.control.state = {BandPlayState::State::Playing,
                           BandPlayState::State::Playing,
                           BandPlayState::State::Silent};
      rig.control.interval = 7;
      rig.client->joins("you");
      const auto before = rig.client->said.size();
      rig.client->say("you", "band, stop");

      expect(rig.control.commands.size() == 1, "the band was not commanded");
      expect(rig.control.commands.front().act == BotChat::Act::StopPlaying);
      expect(rig.control.commands.front().atInterval == 8,
             "a stop takes effect from the NEXT interval, not this one");

      expect(rig.client->said.size() == before + 1,
             "a stop got more or fewer than one answer");
      expect(rig.client->said.back().find("wrapping") != std::string::npos,
             "somebody was playing and the band did not say it was ending: " +
                 rig.client->said.back());
    }

    beginTest("a half-stopped band is still wrapping up");
    {
      // The case the idle penalty existed for: with some bots playing and some
      // silent, "already stopped" would tell the room nothing was happening
      // while the rest ended the tune. The conductor states one fact, and the
      // fact is that the band is stopping.
      Rig rig;
      rig.control.state = {BandPlayState::State::Silent,
                           BandPlayState::State::Playing};
      rig.client->joins("you");
      rig.client->say("you", "band, stop");

      expect(rig.client->said.back().find("wrapping") != std::string::npos,
             rig.client->said.back());
    }

    beginTest("a band already ending is not told it is ending");
    {
      // Three states, not two. `audible()` covers Wrapping and Resolving as
      // well as Playing, so a band mid-ending would be told it is "wrapping it
      // up" -- a second ending announced over the first, and Resolving is one
      // interval from silence.
      Rig rig;
      rig.control.state = {BandPlayState::State::Wrapping,
                           BandPlayState::State::Resolving};
      rig.client->joins("you");
      rig.client->say("you", "band, stop");

      expect(rig.client->said.back().find("already bringing") !=
                 std::string::npos,
             rig.client->said.back());
      expect(rig.control.commands.empty(),
             "an ending already under way was started again");
    }

    beginTest("a band that is already silent says so");
    {
      Rig rig;
      rig.control.state = {BandPlayState::State::Silent,
                           BandPlayState::State::Silent};
      rig.client->joins("you");
      rig.client->say("you", "band, stop");

      expect(rig.client->said.back().find("already stopped") !=
                 std::string::npos,
             rig.client->said.back());
      expect(rig.control.commands.empty(),
             "a silent band was commanded to stop again");
    }

    beginTest("a start takes effect from the interval being played");
    {
      Rig rig;
      rig.control.state = {BandPlayState::State::Silent};
      rig.control.interval = 4;
      rig.client->joins("you");
      rig.client->say("you", "band, play");

      expect(rig.control.commands.size() == 1);
      expect(rig.control.commands.front().act == BotChat::Act::StartPlaying);
      expect(rig.control.commands.front().atInterval == 4,
             "a start waited for the next interval, which is a bar of silence "
             "for no musical reason");
    }

    beginTest("knows its own name and its owner");
    {
      Rig rig;
      expect(rig.conductor->name() == "Vell[bot]");
      expect(rig.conductor->owner().empty(), "nobody is the owner by default");
      rig.conductor->setOwner("you");
      expect(rig.conductor->owner() == "you");
    }

    // ---------------------------------------------------------------------
    // Voting
    // ---------------------------------------------------------------------
    //
    // The band's job is to leave the room's decision where it would have been
    // with no bots in it. That means topping a vote up once the humans have
    // cast what a room of just them would have needed -- and staying out
    // otherwise, because a bot that abstains is a bot voting against.

    // The server's line, as it broadcasts it: no username, no addressing.
    auto voteLine = [](int votes, int required, int value) {
      return "[voting system] leading candidate: " + std::to_string(votes) +
             "/" + std::to_string(required) + " votes for " +
             std::to_string(value) + " BPM [each vote expires in 120s]";
    };

    beginTest("it tops the vote up once the room has decided");
    {
      // Two humans and three bots at 50%: the room needs 3, the humans alone
      // would have needed 1, and one has voted. So the band owes two votes --
      // its own, and one more.
      Rig rig;
      rig.client->joins("alice");
      rig.client->joins("bob");
      rig.client->joins("Delvo[bass-bot]");
      rig.client->joins("Mirn[kit-bot]");

      rig.client->say("", voteLine(1, 3, 130));

      const auto said = rig.client->saidCopy();
      expect(std::find(said.begin(), said.end(), "!vote bpm 130") != said.end(),
             "the conductor never cast its own vote");
      expect(rig.control.votes.size() == 1u, "the band was not asked to vote");
      if (!rig.control.votes.empty()) {
        expect(rig.control.votes.front().isBpm);
        expectEquals(rig.control.votes.front().value, 130);
        expectEquals(rig.control.votes.front().count, 1,
                     "the band should cast the shortfall and no more");
      }
    }

    beginTest("one vote short means the conductor alone");
    {
      // Two humans and two bots: the room needs 2 of 4 and one human has
      // voted, which is what a room of just the two would have needed. One
      // vote outstanding, and it is the conductor's own -- no member is asked.
      //
      // The numbers have to be consistent with SOME threshold, which is the
      // trap this test fell into when it was written: "2/3 of 4 users" implies
      // a threshold of at least 63%, and at that threshold three humans need
      // all three votes, so the conductor was right to stay out.
      Rig rig;
      rig.client->joins("alice");
      rig.client->joins("bob");
      rig.client->joins("Delvo[bass-bot]");

      rig.client->say("", voteLine(1, 2, 140));

      const auto said = rig.client->saidCopy();
      expect(std::find(said.begin(), said.end(), "!vote bpm 140") != said.end());
      expect(rig.control.votes.empty(),
             "a shortfall of one is the conductor's own vote, nobody else's");
    }

    beginTest("it stays out while the room is still deciding");
    {
      // Three humans and two bots at 50%: the room needs 3, but a room of just
      // the three would have needed 2 and only one has voted.
      Rig rig;
      rig.client->joins("alice");
      rig.client->joins("bob");
      rig.client->joins("carol");
      rig.client->joins("Delvo[bass-bot]");

      rig.client->say("", voteLine(1, 3, 130));

      for (const auto &line : rig.client->saidCopy())
        expect(line.rfind("!vote", 0) != 0,
               "the band voted before the room had: " + line);
      expect(rig.control.votes.empty());
    }

    beginTest("a band alone never votes");
    {
      // No humans at all. A band changing its own tempo is the band voting for
      // itself, which is the whole of what it must never do.
      Rig rig;
      rig.client->joins("Delvo[bass-bot]");
      rig.client->joins("Mirn[kit-bot]");

      rig.client->say("", voteLine(1, 2, 130));

      for (const auto &line : rig.client->saidCopy())
        expect(line.rfind("!vote", 0) != 0, "a band voted with nobody there");
      expect(rig.control.votes.empty());
    }

    beginTest("it does not answer its own vote");
    {
      // Casting produces another line from the server. Without a latch the
      // conductor reads that one too and votes again, forever.
      Rig rig;
      rig.client->joins("alice");
      rig.client->joins("bob");
      rig.client->joins("Delvo[bass-bot]");
      rig.client->joins("Mirn[kit-bot]");

      rig.client->say("", voteLine(1, 3, 130));
      rig.client->say("", voteLine(2, 3, 130));
      rig.client->say("", voteLine(3, 3, 130));

      int casts = 0;
      for (const auto &line : rig.client->saidCopy())
        if (line == "!vote bpm 130")
          ++casts;
      expectEquals(casts, 1, "the conductor voted more than once");
      expectEquals((int)rig.control.votes.size(), 1);
    }

    beginTest("a new candidate is a new question");
    {
      Rig rig;
      rig.client->joins("alice");
      rig.client->joins("bob");
      rig.client->joins("Delvo[bass-bot]");
      rig.client->joins("Mirn[kit-bot]");

      rig.client->say("", voteLine(1, 3, 130));
      rig.client->say("", voteLine(1, 3, 145));

      const auto said = rig.client->saidCopy();
      expect(std::find(said.begin(), said.end(), "!vote bpm 130") != said.end());
      expect(std::find(said.begin(), said.end(), "!vote bpm 145") != said.end(),
             "the band stayed behind a candidate the room had left");
    }

    beginTest("a carried vote clears the poll, so the next one is answered");
    {
      Rig rig;
      rig.client->joins("alice");
      rig.client->joins("bob");
      rig.client->joins("Delvo[bass-bot]");
      rig.client->joins("Mirn[kit-bot]");

      rig.client->say("", voteLine(1, 3, 130));
      rig.client->say("", "[voting system] setting BPM to 130");
      rig.client->say("", voteLine(1, 3, 130));

      int casts = 0;
      for (const auto &line : rig.client->saidCopy())
        if (line == "!vote bpm 130")
          ++casts;
      expectEquals(casts, 2,
                   "the same tempo, proposed again, was treated as spent");
    }

    beginTest("a poll that expires is answered again when it comes back");
    {
      // Nothing announces an expiry: the server recomputes its tally only when
      // somebody votes, so a vote that runs out of time makes no traffic at
      // all. If the conductor does not notice, its latch refuses that value
      // for the rest of the session -- the band silently stops helping, and
      // the room has no way to tell.
      Rig rig;
      rig.client->joins("alice");
      rig.client->joins("bob");
      rig.client->joins("Delvo[bass-bot]");
      rig.client->joins("Mirn[kit-bot]");

      auto shortLived = [](int votes, int required, int value) {
        return "[voting system] leading candidate: " + std::to_string(votes) +
               "/" + std::to_string(required) + " votes for " +
               std::to_string(value) + " BPM [each vote expires in 1s]";
      };

      rig.client->say("", shortLived(1, 3, 130));
      std::this_thread::sleep_for(std::chrono::milliseconds(1200));
      rig.client->say("", shortLived(1, 3, 130));

      int casts = 0;
      for (const auto &line : rig.client->saidCopy())
        if (line == "!vote bpm 130")
          ++casts;
      expectEquals(casts, 2,
                   "the band would not back the same tempo a second time");
    }

    beginTest("a live poll is still not answered twice");
    {
      // The other side of the same clock: a long timeout must not expire, or
      // the latch stops holding and the conductor answers its own votes.
      Rig rig;
      rig.client->joins("alice");
      rig.client->joins("bob");
      rig.client->joins("Delvo[bass-bot]");
      rig.client->joins("Mirn[kit-bot]");

      rig.client->say("", voteLine(1, 3, 130));
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      rig.client->say("", voteLine(2, 3, 130));

      int casts = 0;
      for (const auto &line : rig.client->saidCopy())
        if (line == "!vote bpm 130")
          ++casts;
      expectEquals(casts, 1);
    }

    beginTest("a BPI vote is cast as a BPI vote");
    {
      Rig rig;
      rig.client->joins("alice");
      rig.client->joins("bob");
      rig.client->joins("Delvo[bass-bot]");

      rig.client->say("", "[voting system] leading candidate: 1/2 votes for "
                          "16 BPI [each vote expires in 120s]");

      const auto said = rig.client->saidCopy();
      expect(std::find(said.begin(), said.end(), "!vote bpi 16") != said.end(),
             "a BPI vote was cast as a BPM one, or not at all");
    }

    beginTest("the voting system is never treated as somebody talking");
    {
      // It arrives with an empty username, and the addressing scan must not
      // see it: "leading candidate" is a sentence, and a conductor answering
      // it in words would be answering the server.
      Rig rig;
      rig.client->joins("alice");
      rig.client->say("", "[voting system] Voting not enabled");

      for (const auto &line : rig.client->saidCopy())
        expect(line.rfind("!vote", 0) != 0);
      expect(rig.control.votes.empty());
    }
  }
};

TEST_CASE("conductor") {
  ConductorTests t;
  t.runTest();
}

} // namespace
