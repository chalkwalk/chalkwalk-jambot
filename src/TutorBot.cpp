#include "TutorBot.h"

#include "BotNames.h"

#include <chalkwalk/music/Text.h>
#include <chalkwalk/ninjam/RoomConventions.h>

namespace {

// The six lines.
//
// Plain, lowercase, and each one says a thing that just happened rather than a
// thing that might. `docs/BOT-CHAT.md` section 8 is the voice; the constraint
// that matters here is that none of them is an instruction to go and do
// something -- every one is triggered BY the thing it describes, so it reads as
// somebody noticing rather than a wizard with a next button.
constexpr const char *kGreeting =
    "welcome -- this is a practice room, and the band plays while you do. "
    "say `part` to any of us to send them home.";

constexpr const char *kFirstPlayed =
    "that interval just went out, and nobody has heard it yet -- everything "
    "you play reaches everyone else one interval later.";

constexpr const char *kSecondPlayed =
    "and that is why the band sounds a bar behind you. they are answering the "
    "interval you played last, not this one. that is the form rather than a "
    "fault.";

constexpr const char *kKeySet =
    "the band took that key. chords work the same way -- write a chart like "
    "| Am | F | and they will follow it.";

constexpr const char *kShaken =
    "the parts changed and the tune did not. shake rerolls what everyone "
    "plays, never what you agreed to play.";

constexpr const char *kSignOff =
    "that is the whole of it -- i'll get out of the way. the band will keep "
    "playing.";

bool isShake(const std::string &text) {
  const auto t =
      chalkwalk::music::text::lower(chalkwalk::music::text::trim(text));
  return t == "shake" || t == "new" || t == "again";
}

} // namespace

TutorBot::TutorBot(std::string name,
                   std::unique_ptr<BotClient::Client> botClient)
    : username(std::move(name)), client(std::move(botClient)) {
  if (client)
    client->addListener(this);
}

TutorBot::~TutorBot() {
  if (client) {
    client->removeListener(this);
    client->disconnect();
  }
}

void TutorBot::setOwner(std::string ownerUsername) {
  std::lock_guard<std::mutex> sl(stateMutex);
  owner = std::move(ownerUsername);
}

bool TutorBot::join(const std::string &host, int port, double sampleRate) {
  if (!client)
    return false;

  client->setSampleRate(sampleRate);

  // No channel at all. The tutor sends nothing and is not a strip in anybody's
  // mixer; it is a name in the room that talks.
  client->setChannels({});

  // Subscribed by default, because the thread is gated on hearing the owner
  // play. It measures nothing and keeps nothing -- see `onIntervalReceived`.
  client->setDefaultRecvEnabled(true);

  client->connect(host, port, username, "");
  active = true;
  return true;
}

void TutorBot::part() {
  if (!active.exchange(false))
    return;
  if (client)
    client->disconnect();
}

TutorBot::Step TutorBot::step() const {
  std::lock_guard<std::mutex> sl(stateMutex);
  return current;
}

void TutorBot::advance(const std::string &line) {
  Step next;
  {
    std::lock_guard<std::mutex> sl(stateMutex);
    switch (current) {
    case Step::Greeting:
      next = Step::FirstPlayed;
      break;
    case Step::FirstPlayed:
      next = Step::SecondPlayed;
      break;
    case Step::SecondPlayed:
      next = Step::KeySet;
      break;
    case Step::KeySet:
      next = Step::Shaken;
      break;
    case Step::Shaken:
      next = Step::Done;
      break;
    case Step::Done:
      return;
    }
    current = next;
  }

  if (client)
    client->sendChat(line);

  // The last line and the leaving are one act. A tutor that said it would get
  // out of the way and then stayed would be worse than one that never said it.
  if (next == Step::Done) {
    if (client)
      client->sendChat(kSignOff);
    part();
  }
}

void TutorBot::onConnected() {
  if (step() == Step::Greeting)
    advance(kGreeting);
}

void TutorBot::onDisconnected(const std::string &) { active = false; }

void TutorBot::onChatMessage(const std::string &type, const std::string &sender,
                             const std::string &text) {
  if (!active.load())
    return;

  // Bots do not teach bots, and the tutor does not react to its own line.
  if (BotNames::looksLikeBot(sender) || sender == username)
    return;
  (void)type;

  const Step at = step();

  if (at == Step::KeySet &&
      !chalkwalk::ninjam::conventions::extractKeyAnnouncement(text).empty()) {
    advance(kKeySet);
    return;
  }

  if (at == Step::Shaken && isShake(text))
    advance(kShaken);
}

void TutorBot::onIntervalReceived(const std::string &from, int, const float *,
                                  const float *, int numSamples) {
  if (!active.load() || numSamples <= 0)
    return;

  {
    std::lock_guard<std::mutex> sl(stateMutex);
    if (!owner.empty() && from != owner)
      return; // Somebody else playing must not carry the newcomer forward.
  }

  // NOTHING IS MEASURED HERE YET. The samples are deliberately ignored: this
  // step is meant to check that what arrived looks like an instrument somebody
  // could hear, and the diagnostic table for that is section 7's and is not
  // built. Until it is, the tutor says the encouraging line -- which is what
  // that section requires it to do whenever the reading is unclear anyway, so
  // the neutral path is the one it will keep.
  const Step at = step();
  if (at == Step::FirstPlayed)
    advance(kFirstPlayed);
  else if (at == Step::SecondPlayed)
    advance(kSecondPlayed);
}
