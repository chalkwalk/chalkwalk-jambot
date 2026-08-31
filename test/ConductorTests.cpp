#include "../src/Conductor.h"
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
