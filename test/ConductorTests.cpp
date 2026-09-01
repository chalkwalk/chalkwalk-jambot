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

      expect(rig.client->said.size() == 1, "expected exactly one roster line");
      const auto &line = rig.client->said.front();
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
      expect(after == 1, "no roster to begin with");

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

      expect(rig.client->said.size() == 1);
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
