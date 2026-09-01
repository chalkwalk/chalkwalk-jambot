#include "Conductor.h"

#include "BotNames.h"

#include <algorithm>
#include <vector>

Conductor::Conductor(std::string username,
                     std::unique_ptr<BotClient::Client> c)
    : botName(std::move(username)), netClient(std::move(c)) {
  if (netClient) {
    netClient->addListener(this);
    arrivalTimer = netClient->createTimer([this] { onArrivalDue(); });
  }
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

  {
    std::lock_guard<std::mutex> sl(stateMutex);
    rate = r;
  }

  // Active BEFORE connecting, because connect() can fire onConnected
  // synchronously and a subclass may speak from it -- the tutor greets there.
  // `say` is guarded on this flag, so setting it afterwards silently drops the
  // first line the conductor ever says.
  active = true;
  netClient->connect(host, port, botName, "");

  // Armed HERE rather than from onConnected, because a subclass overrides that
  // -- TutorBot greets from it -- and an override does not chain by default. A
  // conductor whose arrival never fired would post no roster at all, and the
  // room that breaks is the DEFAULT one, since `withTutor` is on at the command
  // line while every test fixture turns it off.
  if (arrivalTimer)
    arrivalTimer->start(kArrivalDelayMs);
  return true;
}

void Conductor::setBandName(std::string name) {
  std::lock_guard<std::mutex> sl(stateMutex);
  bandName = std::move(name);
}

void Conductor::onArrivalDue() {
  // Once per room. There is no "unless somebody else already did it" here --
  // that question only existed because there were peers who might have.
  if (!isActive() || arrivalDone.exchange(true))
    return;

  std::vector<std::string> entries;
  for (const auto &m : netClient->members()) {
    if (!BotNames::looksLikeBandmate(m.username))
      continue;
    const auto handle = BotNames::handleOf(m.username);
    const auto open = m.username.find('[');
    std::string instrument;
    if (open != std::string::npos) {
      const auto rest = m.username.substr(open + 1);
      const auto end = rest.find("-bot]");
      instrument = end == std::string::npos ? rest : rest.substr(0, end);
    }
    entries.push_back(instrument.empty() ? handle
                                         : handle + " (" + instrument + ")");
  }

  if (entries.empty())
    return;

  std::sort(entries.begin(), entries.end());

  std::string roster;
  {
    std::lock_guard<std::mutex> sl(stateMutex);
    if (!bandName.empty())
      roster = bandName + " -- ";
  }
  for (std::size_t i = 0; i < entries.size(); ++i)
    roster += (i ? std::string(", ") : std::string()) + entries[i];
  roster += ".";

  say(roster);
}

void Conductor::say(const std::string &text) {
  if (!active.load() || !netClient)
    return;
  netClient->sendChat(text);
}

void Conductor::part() {
  if (!active.exchange(false))
    return;
  if (arrivalTimer)
    arrivalTimer->stop();
  if (netClient)
    netClient->disconnect();
}
