#pragma once

#include "../src/BandControl.h"

#include <functional>
#include <vector>

namespace jambot::test {

// A host a suite drives by hand: it records what it was asked and answers with
// whatever the test set.
class FakeBandControl final : public BandControl {
public:
  int interval = 0;
  std::vector<BandPlayState::State> state;
  bool allowGrowth = true;
  int added = 0;

  // What the host does when it adds a player. A real room puts somebody in the
  // room, which is how the conductor learns their name -- it reads the room
  // rather than being handed one.
  std::function<void()> onAdd;

  struct Commanded {
    BotChat::Act act;
    int atInterval;
  };
  std::vector<Commanded> commands;

  int currentInterval() const override { return interval; }
  std::vector<BandPlayState::State> phases() const override { return state; }

  void command(BotChat::Act act, int atInterval) override {
    commands.push_back({act, atInterval});
  }

  bool addPlayer() override {
    if (!allowGrowth)
      return false;
    ++added;
    if (onAdd)
      onAdd();
    return true;
  }
};

} // namespace jambot::test
