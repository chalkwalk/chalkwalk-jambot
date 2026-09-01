#pragma once

#include <cstdint>
#include <string>
#include <vector>

// What the bots are called, and why they are called anything.
//
// A name here is an ADDRESS before it is a personality. `docs/BOT-CHAT.md`
// section 5 addresses a bot by scanning a message's tokens against the room's
// user list, and how safely that can be done depends entirely on how rare the
// name is: `delvo` can be matched anywhere in a sentence, so "what are the
// changes delvo" works, while an instrument word like `bass` can only be
// matched where a name would go, because "the bass is too loud" is an ordinary
// remark and must not summon anybody.
//
// So the criteria are mechanical rather than a matter of taste:
//
//   - not an ordinary English word, a personal name, or a brand, so it can be
//     matched anywhere in a sentence;
//   - typeable without thinking. NOT "one obvious pronunciation", which an
//     earlier draft asked for and which rejected `Ravo` and `Pemo` for having
//     two readings apiece. That conflated two things: a name is SAID when a
//     screen reader reads the roster, and it is TYPED when somebody addresses a
//     bot. Reading aloud needs a pronunciation, not an agreed one, and
//     addressing never needs one at all;
//   - a rime an English reader already owns. This matters more than syllable
//     count, which an earlier draft asked for instead: `Mirn` is one syllable
//     and reads instantly because `-irn` is fern, burn, turn, while `Nolm`,
//     `Selm`, `Velk` and `Cralt` are the same length and read as truncations,
//     their clusters having no familiar English pattern behind them;
//   - one token, no spaces. Every Ninjam client sends a private message as
//     `/msg <user> <text>` and splits on the first space, so the original
//     `Keys [bot]` could not be sent one at all -- it addressed a user called
//     `Keys`, who does not exist, and failed silently;
//   - distinct first letters, and at least two edits apart, so a near-miss on a
//     typo stays unambiguous. This is a constraint on the BAND, not on the
//     pool: two names that must not appear together are both fine to have
//     available, and `bandFor` keeps them apart. Getting that the wrong way
//     round is what made an earlier pool needlessly small.
//
// The four in use were chosen by searching thirty candidates and keeping the
// least occupied -- not by counting results, which no search engine reports,
// but by asking whether the word is already a person, a handle or a brand that
// somebody might turn up using. Eighteen were struck for being exactly that,
// including a Premier League goalkeeper, a techno producer on Drumcode (the
// worst possible collision for a music program), an AI chat app, and several
// ordinary given names. What is left is owned by a dog chew, some industrial
// screwdrivers, a Bhutanese stone-throwing sport and a gas-meter acronym.

namespace BotNames {

// The pool: eight names for a band of four.
//
// Bigger than the band for two reasons. A name colliding with somebody already
// in the room is skipped at join rather than degrading afterwards, and that
// needs somewhere to skip TO. And a different four each session is worth having
// on its own -- nobody is meant to memorise a fixed line-up, and the roster
// announcement introduces them every time anyway.
//
// `Vessa` and `Ravo` were briefly cut and are back. The case against Vessa was
// that it reads as a shortened Vanessa; the usual short form is Nessa, so it
// does not. The case against Ravo was two possible pronunciations, which turns
// out not to matter for a name you type.
//
// `Vurn` and `Pemo` are here despite conflicting with names already in the
// list -- Vurn shares Mirn's rime and Pemo shares Pundo's initial -- because
// those are constraints on which four play TOGETHER, which `bandFor` enforces,
// not on which eight exist.
//
// Ordered, and the order is part of the contract -- `bandFor` rotates through
// it, so the same seed brings the same players back.
inline const std::vector<std::string> &pool() {
  static const std::vector<std::string> names = {
      "Mirn", "Delvo", "Pundo", "Quado", "Vessa", "Ravo", "Vurn", "Pemo"};
  return names;
}

// The tutor is not one of them.
//
// It is a role rather than a bandmate, and a role is addressed by what it is:
// `tutor:` is what anybody would type without being told, and nobody says the
// word casually in a jam. Matched in the address position only, like `band`.
//
// The `[bot]` is what makes it legible as a bot without making it a BANDMATE --
// see the two predicates below. `handleOf` reads the part before the bracket,
// so it is still addressed as `tutor`.
inline const char *tutorName() { return "Tutor[bot]"; }

// Neither is the conductor, for the same reason and by the same rule: it is a
// role, `conductor:` is what anybody would type without being told, and nobody
// says the word casually in a jam.
inline const char *conductorName() { return "Conductor[bot]"; }

// The suffix that makes a bot legible as one, to a human reading the mixer and
// to other bots deciding whether to answer.
//
// It identifies nothing and is trivially spoofable, which is fine, because it
// decides only who talks. A human naming themselves this way is choosing to be
// ignored, which is not an attack (`docs/BOT-CHAT.md` section 5).
inline std::string usernameFor(const std::string &name,
                               const std::string &instrument) {
  return name + "[" + instrument + "-bot]";
}

// The short handle a bot answers to, lowercased: the part before the bracket.
inline std::string handleOf(const std::string &username) {
  const auto bracket = username.find('[');
  std::string handle = username.substr(0, bracket);
  for (auto &c : handle)
    c = (char)std::tolower((unsigned char)c);
  return handle;
}

// Two questions, two suffixes, and the difference between them is the whole
// reason both exist.
//
//   Delvo[bass-bot]   a BANDMATE: a player, with an instrument
//   Tutor[bot]        a ROLE: one of ours, and not in the band
//
// Is this one of the BAND -- a player with an instrument?
//
// `-bot]`, which only an instrument can put there. Asked where the question is
// about the band itself: who is in the roster, and who is ranked against whom.
// The roles are excluded, because a conductor is in the room and not in it.
inline bool looksLikeBandmate(const std::string &username) {
  return username.size() > 5 &&
         username.compare(username.size() - 5, 5, "-bot]") == 0;
}

// Is this one of OURS at all -- bandmate or role?
//
// `[bot]` or `-bot]`, which is the marker with the character that must precede
// it. Not a bare `bot]`: that would make a human called `robot]` one of ours,
// which a test here insists on. Asked where the alternative is A PERSON: how
// many humans are here, whether the room has emptied, whether an arrival is
// somebody worth introducing the band to.
//
// A SUFFIX rather than a list of role names, so a role invented later is
// recognised without anybody remembering to come back here. It cost a real bug
// to learn that this question exists: the roles used to be plain words, so every
// bot counted the tutor as a human -- the roster re-posted when it joined and
// the band never saw an empty room. That was live for as long as the tutor has
// been, and the tutor is on by default at the command line; it stayed hidden
// because the test fixture turns the tutor off.
inline bool looksLikeBot(const std::string &username) {
  return looksLikeBandmate(username) ||
         (username.size() > 5 &&
          username.compare(username.size() - 5, 5, "[bot]") == 0);
}

// Pick `count` names, skipping any that collide with somebody already in the
// room, deterministically from the seed.
//
// A collision is checked against the whole of each participant's name and
// against its handle, case-insensitively, because the risk is not that a human
// is called `Delvo[bass-bot]` -- it is that one is called `delvo`, which makes
// the short handle ambiguous and would cost the bot the ability to be addressed
// naturally. Skipping at join is cheaper than degrading afterwards.
std::vector<std::string> bandFor(int count, std::uint32_t seed,
                                 const std::vector<std::string> &taken);

} // namespace BotNames
