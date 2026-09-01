#include "../src/Conductor.h"
#include "../src/TutorBot.h"
#include "FakeBotClient.h"
#include "JuceUnitShim.h"

#include <memory>
#include <string>

// The conductor, driven without a room.
//
// Everything here is about the client lifecycle a band leader needs and a
// player does not: it arrives with no instrument, it can be got rid of, and it
// knows who it is playing with.

namespace {

using jambot::test::FakeClient;

struct Rig {
  FakeClient *client = nullptr;
  std::unique_ptr<Conductor> conductor;

  Rig() {
    auto owned = std::make_unique<FakeClient>();
    client = owned.get();
    conductor = std::make_unique<Conductor>("Vell[bot]", std::move(owned));
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
      int asked = 0;
      rig.conductor->setRecruit([&] { ++asked; return true; });
      rig.client->joins("you");
      rig.client->say("you", "band, add a player");

      expect(asked == 1, "the conductor did not ask the room");
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
      rig.conductor->setRecruit([&] {
        // Whoever hosts the room adds the player, so by the time the callback
        // returns the newcomer is in the room. The conductor reads the room
        // rather than being told a name.
        rig.client->joins("Vurn[horn-bot]");
        return true;
      });
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
      rig.conductor->setRecruit([&] { return false; });
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
      int asked = 0;
      rig.conductor->setRecruit([&] { ++asked; return true; });
      rig.client->joins("you");
      rig.client->say("you", "add a player");

      expect(asked == 0,
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
      int asked = 0;
      rig.conductor->setRecruit([&] { ++asked; return true; });
      rig.client->joins("Quado[kit-bot]");
      rig.client->say("Quado[kit-bot]", "band, add a player");

      expect(asked == 0, "a bot asked for a player and the conductor obliged");
    }

    beginTest("knows its own name and its owner");
    {
      Rig rig;
      expect(rig.conductor->name() == "Vell[bot]");
      expect(rig.conductor->owner().empty(), "nobody is the owner by default");
      rig.conductor->setOwner("you");
      expect(rig.conductor->owner() == "you");
    }
  }
};

TEST_CASE("conductor") {
  ConductorTests t;
  t.runTest();
}

} // namespace
