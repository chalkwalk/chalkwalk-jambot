#include "Conductor.h"

#include "BotLanguage.h"
#include "BotNames.h"

#include <chrono>
#include <set>

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

// Introduce the band to the first person who turns up.
//
// The room is started by a host process, so the band connects seconds before
// any human does -- which means a fixed timer introduces the band to an empty
// room, and the one line it gets is said to nobody. Re-arming for the first
// human runs the same announcement when somebody can read it.
//
// Only for the first: with anybody else already present the band has been
// seen, and a roster per arrival is the chattiness this design exists to avoid.
void Conductor::onRoomMembershipChange(const std::string &username,
                                       bool joined) {
  if (!joined || BotNames::looksLikeBot(username))
    return;

  int otherHumans = 0;
  for (const auto &m : netClient->members())
    if (m.username != username && m.username != botName &&
        !BotNames::looksLikeBot(m.username))
      ++otherHumans;

  if (otherHumans != 0)
    return;

  arrivalDone = false;
  if (arrivalTimer)
    arrivalTimer->start(kArrivalDelayMs);
}

// A player as the room should hear them named: "vurn (horn)".
//
// One home, because the roster and the line that names a newcomer must agree --
// somebody introduced as "vurn (horn)" and later listed as "Vurn[horn-bot]"
// reads as two people.
std::string Conductor::describe(const std::string &username) {
  const auto handle = BotNames::handleOf(username);
  const auto open = username.find('[');
  if (open == std::string::npos)
    return handle;
  const auto rest = username.substr(open + 1);
  const auto end = rest.find("-bot]");
  const auto instrument = end == std::string::npos ? rest : rest.substr(0, end);
  return instrument.empty() ? handle : handle + " (" + instrument + ")";
}

void Conductor::setControl(BandControl *c) {
  std::lock_guard<std::mutex> sl(stateMutex);
  control = c;
}

void Conductor::onChatMessage(const std::string &type,
                              const std::string &username,
                              const std::string &text) {
  if (!isActive() || type != "MSG")
    return;

  // Only a person may ask. The band's own chat is the loudest thing in a
  // practice room, and a conductor that recruited on a bot's line would grow
  // the band with nobody asking for it.
  BotAddress::Room room;
  auto add = [&room](const std::string &name) {
    BotAddress::Participant p;
    p.username = name;
    p.handle = BotNames::handleOf(name);
    p.isBot = BotNames::looksLikeBot(name);
    room.participants.push_back(p);
  };

  // Ourselves first, so the scan can find us even in an empty room: a client
  // never appears in its own `members()`.
  add(botName);
  for (const auto &m : netClient->members())
    if (m.username != botName)
      add(m.username);
  room.resolveHandles();

  BotAddress::Incoming in;
  in.sender = username;
  in.text = text;
  in.isPrivate = false;
  using namespace std::chrono;
  in.at = duration<double>(steady_clock::now().time_since_epoch()).count();

  // `Collective` is the one that matters: "band, add a player" is addressed to
  // all of us, and growing the band is one answer about the room. `Named` and
  // `Continuation` count too, so `conductor: add a player` and a follow-up
  // inside the attention window both work.
  const auto who = BotAddress::classify(room, botName, in, attention);
  if (who != BotAddress::Address::Collective &&
      who != BotAddress::Address::Named &&
      who != BotAddress::Address::Continuation)
    return;

  const auto rest = BotAddress::withoutAddress(room, botName, text);
  const auto intent = BotLanguage::read(rest).intent;

  if (intent == BotLanguage::Intent::StopPlaying ||
      intent == BotLanguage::Intent::StartPlaying) {
    BandControl *band = nullptr;
    {
      std::lock_guard<std::mutex> sl(stateMutex);
      band = control;
    }
    if (band == nullptr)
      return;
    commandBand(*band, intent);
    return;
  }

  if (intent != BotLanguage::Intent::AddPlayer)
    return;

  // Who was here BEFORE asking, so that whoever is here afterwards and was not
  // before is the newcomer. The alternative is trusting the host to hand back a
  // name, which makes the arranging order two people's business.
  std::set<std::string> seen;
  for (const auto &m : netClient->members())
    seen.insert(m.username);

  BandControl *host = nullptr;
  {
    std::lock_guard<std::mutex> sl(stateMutex);
    host = control;
  }
  if (host == nullptr)
    return;

  if (host->addPlayer()) {
    // Named, not merely acknowledged. A band that gains a player without a word
    // reads as a process starting, which is what the roster exists to prevent.
    // The roster is not re-posted -- everybody has met the others already -- so
    // this is the one line that says who just arrived.
    //
    // Read from the room rather than returned by the callback: the host has
    // added the player by now, and asking the room is what keeps the arranging
    // order and the naming its business rather than ours.
    std::string newest;
    for (const auto &m : netClient->members())
      if (BotNames::looksLikeBandmate(m.username) && !seen.count(m.username))
        newest = describe(m.username);

    say(newest.empty() ? "bringing somebody else in."
                       : "bringing " + newest + " in.");
  } else
    say("that is as many of us as this room takes.");
}

// One fact about the band, said once.
//
// The wording is the band's own, kept verbatim: a player reads the same line
// whichever bot says it, and these were written for the room rather than for
// the code that used to hold them.
void Conductor::commandBand(BandControl &band, BotLanguage::Intent intent) {
  const auto now = band.phases();

  const auto anyIn = [&now](BandPlayState::State want) {
    return std::any_of(now.begin(), now.end(),
                       [want](BandPlayState::State s) { return s == want; });
  };

  if (intent == BotLanguage::Intent::StopPlaying) {
    // Three states, not two. `audible()` covers the two ending states as well
    // as Playing, so asking "is anybody audible" would tell a band already
    // resolving that it is wrapping up -- a second ending announced over the
    // first, one interval before silence.
    if (anyIn(BandPlayState::State::Playing)) {
      // Said BEFORE commanding, and the order matters. Commanding takes the
      // host's lock, which the render thread holds for as long as a render --
      // seconds, with four bots -- so speaking afterwards puts the line behind
      // it, and a room hears the band end before it is told the band is ending.
      //
      // Safe in this direction because neither of these commands can fail: the
      // line states an intention the host has no way to refuse.
      say("we're wrapping it up -- ending on the downbeat after this one.");

      // From the NEXT interval: an ending is musical, and the head of an
      // interval is where a band can begin one together.
      band.command(BotChat::Act::StopPlaying, band.currentInterval() + 1);
      return;
    }
    if (anyIn(BandPlayState::State::Wrapping) ||
        anyIn(BandPlayState::State::Resolving)) {
      say("already bringing it to an end.");
      return;
    }
    say("already stopped. say \"band play\" when you want us back in.");
    return;
  }

  if (anyIn(BandPlayState::State::Playing)) {
    say("already playing.");
    return;
  }

  // Said first, for the reason above.
  say("we're coming in on the next interval.");

  // From the interval being played. There is nothing musical in the gap before
  // the next one, so waiting for it is an interval of silence for nothing.
  band.command(BotChat::Act::StartPlaying, band.currentInterval());
}

void Conductor::onArrivalDue() {
  // Once per room. There is no "unless somebody else already did it" here --
  // that question only existed because there were peers who might have.
  if (!isActive() || arrivalDone.exchange(true))
    return;

  std::vector<std::string> entries;
  for (const auto &m : netClient->members())
    if (BotNames::looksLikeBandmate(m.username))
      entries.push_back(describe(m.username));

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

  // How to use the band, said once, beside the line that says who they are.
  //
  // The only answer to "how would anybody know they can talk to these things",
  // so it travels with the roster rather than waiting to be asked.
  //
  // The interesting thing first and the destructive one last, stated plainly
  // enough that nobody types it idly: leading with `leave` would invite a
  // curious player to empty their own room with the first command they were
  // shown.
  say("say \"band play\" to start us and \"band stop\" to end the tune. say a "
      "name to talk to one of us. say \"leave\" and we all go home.");
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
