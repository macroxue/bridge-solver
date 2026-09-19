// The web demo's Emscripten-exported API: the C++ solver.cc engine plus
// this file's WASM/JS-facing glue (WebPlay, CollectHands, solve,
// solve_plays, and the EMSCRIPTEN_BINDINGS below). Kept separate from
// solver.cc for separation of concerns; this file, not solver.cc, is what
// the web build actually compiles.
#define _WEB
#include "../solver.cc"

class WebPlay {
 public:
  WebPlay(const Hands& hands, int trump, int lead_seat, int target_ns_tricks,
          std::vector<int> played_cards)
      : min_max(hands, trump, lead_seat),
        target_ns_tricks(target_ns_tricks),
        num_tricks(hands.num_tricks()),
        played_cards(played_cards) {}

  typedef std::map<int, int> CardTricks;

  CardTricks EvaluatePlays(int ns_tricks, bool ns_contract) {
    // Play all the cards from start.
    for (size_t p = 0; p <= played_cards.size(); ++p) {
      auto& play = min_max.play(p);
      if (play.TrickStarting()) {
        if (play.depth > 0) {
          play.ns_tricks_won = play.PreviousPlay().ns_tricks_won + play.PreviousPlay().NsWon();
          play.seat_to_play = play.PreviousPlay().WinningSeat();
        }
        play.trick->all_cards = play.hands.all_cards();
        play.ComputeShape();
        play.trick->ComputeRelativeHands(play.depth, play.hands);
      } else {
        play.ns_tricks_won = play.PreviousPlay().ns_tricks_won;
        play.seat_to_play = play.PreviousPlay().NextSeat();
      }
      // Leave the last trick for GetPlayableCards() below.
      if (p < played_cards.size() && p < TOTAL_CARDS - 4) play.PlayCard(played_cards[p]);
    }

    auto& play = min_max.play(played_cards.size());
    CardTricks card_tricks;
    if (played_cards.size() >= TOTAL_CARDS - 4) {
      // The last trick.
      auto [new_ns_tricks, _] = min_max.play(TOTAL_CARDS - 4).CollectLastTrick();
      int trick_diff =
          ns_contract ? new_ns_tricks - target_ns_tricks : target_ns_tricks - new_ns_tricks;
      card_tricks[play.GetPlayableCards().Top()] = trick_diff;
      return card_tricks;
    }
    for (int card : play.trick->FilterEquivalent(play.GetPlayableCards())) {
      auto search = [&play, card](int beta) {
        play.PlayCard(card);
        auto [ns_tricks, _] = play.NextPlay().SearchWithCache(beta);
        play.UnplayCard();
        return ns_tricks;
      };
      int new_ns_tricks = MemoryEnhancedTestDriver(search, num_tricks, ns_tricks);
      int trick_diff =
          ns_contract ? new_ns_tricks - target_ns_tricks : target_ns_tricks - new_ns_tricks;
      card_tricks[card] = trick_diff;
    }
    return card_tricks;
  }

 private:
  MinMax min_max;
  const int target_ns_tricks;
  const int num_tricks;
  const std::vector<int> played_cards;
};

Hands CollectHands(const char* west, const char* north, const char* east, const char* south) {
  Cards all_cards;
  Hands hands;
  hands[WEST] = ParseHand(west, all_cards);
  all_cards.Add(hands[WEST]);
  hands[NORTH] = ParseHand(north, all_cards);
  all_cards.Add(hands[NORTH]);
  hands[EAST] = ParseHand(east, all_cards);
  all_cards.Add(hands[EAST]);
  hands[SOUTH] = ParseHand(south, all_cards);
  all_cards.Add(hands[SOUTH]);
  return hands;
}

std::string solve(std::string west, std::string north, std::string east, std::string south) {
  auto hands = CollectHands(west.c_str(), north.c_str(), east.c_str(), south.c_str());

  // solve_plays() below leaves these populated for a different trump/deal;
  // clear them so Solve() doesn't get stale hits.
  common_bounds_cache.Reset();
  cutoff_cache.Reset();

  static char buffer[256];
  buffer[0] = '\0';
  auto start_time = Now();
  auto trump_start = [&](int trump) { sprintf(buffer + strlen(buffer), "%c", SuitName(trump)[0]); };
  auto seat_done = [&](int trump, int lead_seat, int ns_tricks) {
    sprintf(buffer + strlen(buffer), " %2d",
            IsNs(lead_seat) ? hands.num_tricks() - ns_tricks : ns_tricks);
  };
  auto trump_done = [start_time](int trump) {
    sprintf(buffer + strlen(buffer), " %5.2f s\n", Now() - start_time);
  };
  std::vector<int> trumps = {NOTRUMP, SPADE, HEART, DIAMOND, CLUB};
  std::vector<int> lead_seats = {WEST, EAST, NORTH, SOUTH};
  Solve(hands, trumps, lead_seats, trump_start, seat_done, trump_done);
  return buffer;
}

std::string solve_plays(std::string west, std::string north, std::string east, std::string south,
                        int level, int trump, int lead_seat, std::string played_cards) {
  auto hands = CollectHands(west.c_str(), north.c_str(), east.c_str(), south.c_str());
  bool ns_contract = !IsNs(lead_seat);
  int target_ns_tricks = ns_contract ? level + 6 : 7 - level;

  std::vector<int> cards;
  cards.reserve(played_cards.size() / 2);
  for (size_t i = 0; i < played_cards.size() / 2; ++i)
    cards.push_back(CardOf(CharToSuit(played_cards[i * 2]), CharToRank(played_cards[i * 2 + 1])));

  // Caches are reused for the same (hands, trump) across calls, like in the
  // standalone Solve(); reset only when the deal or trump actually changes.
  static Hands last_hands;
  static int last_trump = -1;
  if (!hands.Equals(last_hands) || trump != last_trump) {
    common_bounds_cache.Reset();
    cutoff_cache.Reset();
    last_hands = hands;
    last_trump = trump;
  }

  static char buffer[256];
  buffer[0] = '\0';
  auto web_play = WebPlay(hands, trump, lead_seat, target_ns_tricks, cards);
  auto card_tricks = web_play.EvaluatePlays(GuessTricks(hands, trump), ns_contract);
  for (const auto& card_trick : card_tricks) {
    sprintf(buffer + strlen(buffer), "%s:%+d ", NameOf(card_trick.first), card_trick.second);
  }
  return buffer;
}

#ifndef _TEST
#include <emscripten/bind.h>

using namespace emscripten;

EMSCRIPTEN_BINDINGS(my_module) {
  function("solve", &solve);
  function("solve_plays", &solve_plays);
}
#endif  // !_TEST
