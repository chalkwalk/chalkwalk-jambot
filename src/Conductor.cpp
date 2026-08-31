#include "Conductor.h"

Conductor::Conductor(std::string username,
                     std::unique_ptr<BotClient::Client> c)
    : botName(std::move(username)), netClient(std::move(c)) {
  if (netClient)
    netClient->addListener(this);
}

Conductor::~Conductor() {
  part();
  if (netClient)
    netClient->removeListener(this);
}

void Conductor::setOwner(std::string ownerUsername) {
  std::lock_guard<std::mutex> sl(stateMutex);
  ownerName = std::move(ownerUsername);
}

std::string Conductor::owner() const {
  std::lock_guard<std::mutex> sl(stateMutex);
  return ownerName;
}

double Conductor::sampleRate() const {
  std::lock_guard<std::mutex> sl(stateMutex);
  return rate;
}

bool Conductor::join(const std::string &host, int port, double r) {
  if (!netClient)
    return false;

  netClient->setSampleRate(r);

  // No channel at all: the conductor sends nothing and is not a strip in
  // anybody's mixer.
  netClient->setChannels({});

  // Subscribed, because a conductor that cannot hear the room cannot lead it.
  netClient->setDefaultRecvEnabled(true);

  netClient->connect(host, port, botName, "");
  {
    std::lock_guard<std::mutex> sl(stateMutex);
    rate = r;
  }
  active = true;
  return true;
}

void Conductor::part() {
  if (!active.exchange(false))
    return;
  if (netClient)
    netClient->disconnect();
}
