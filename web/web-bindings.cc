// The web app's Emscripten-exported API (solve, shuffle_and_solve,
// solve_plays and the EMSCRIPTEN_BINDINGS below), built on ../solver.h.
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include <string>
#include <vector>

#include "../solver.h"

double Now() {
  timeval now;
  gettimeofday(&now, nullptr);
  return now.tv_sec + now.tv_usec * 1e-6;
}

Deal CollectDeal(const std::string& west, const std::string& north, const std::string& east,
                 const std::string& south) {
  Deal deal;
  deal.hands[WEST] = west;
  deal.hands[NORTH] = north;
  deal.hands[EAST] = east;
  deal.hands[SOUTH] = south;
  return ParseDeal(deal);
}

// Runs Solve() over all 5 strains/4 lead seats and formats the per-seat
// trick counts into solve()/shuffle_and_solve()'s shared "<strain> <S> <N>
// <W> <E> <time> s" line format.
std::string SolveToString(const Deal& deal) {
  static char buffer[256];
  buffer[0] = '\0';
  auto append = [&](const char* fmt, auto... args) {
    size_t len = strlen(buffer);
    snprintf(buffer + len, sizeof(buffer) - len, fmt, args...);
  };
  auto start_time = Now();
  auto trump_start = [&](int trump) { append("%c", SuitName(trump)[0]); };
  int num_tricks = NumTricks(deal);
  auto seat_done = [&](int trump, int lead_seat, int ns_tricks) {
    append(" %2d", IsNs(lead_seat) ? num_tricks - ns_tricks : ns_tricks);
  };
  auto trump_done = [start_time, &append](int trump) {
    append(" %5.2f s\n", Now() - start_time);
  };
  std::vector<int> trumps = {NOTRUMP, SPADE, HEART, DIAMOND, CLUB};
  std::vector<int> lead_seats = {WEST, EAST, NORTH, SOUTH};
  Solve(deal, trumps, lead_seats, trump_start, seat_done, trump_done);
  return buffer;
}

std::string solve(std::string west, std::string north, std::string east, std::string south) {
  return SolveToString(CollectDeal(west, north, east, south));
}

// Like solve(), but first redeals the given seats' pooled cards among
// themselves (matching the CLI's `-s <seats>` flag) and, when
// discard_suit_bottom is set, solves with that speedup -- the web app's
// Shuffle feature passes true, mirroring shuffle.py's own `-s ... -d`
// usage. Runs in its own worker/wasm instance, separate from
// solve()/solve_plays()'s exact-precision caches.
std::string shuffle_and_solve(std::string west, std::string north, std::string east,
                              std::string south, std::string shuffle_seats,
                              bool discard_suit_bottom) {
  auto deal = ShuffleDeal(CollectDeal(west, north, east, south), shuffle_seats.c_str());
  options.discard_suit_bottom = discard_suit_bottom;
  return SolveToString(deal);
}

std::string solve_plays(std::string west, std::string north, std::string east, std::string south,
                        int target_tricks, int trump, int lead_seat, std::string played_cards) {
  auto deal = CollectDeal(west, north, east, south);
  int num_tricks = NumTricks(deal);
  bool ns_contract = !IsNs(lead_seat);
  // Declarer's target, e.g. 9 for 3NT, leaves the rest to N/S when E/W
  // declare.
  int target_ns_tricks = ns_contract ? target_tricks : num_tricks - target_tricks;

  std::vector<int> cards;
  cards.reserve(played_cards.size() / 2);
  for (size_t i = 0; i < played_cards.size() / 2; ++i)
    cards.push_back(CardOf(CharToSuit(played_cards[i * 2]), CharToRank(played_cards[i * 2 + 1])));

  static char buffer[256];
  buffer[0] = '\0';
  for (int card : GetPosition(deal, trump, lead_seat, cards).playable_cards) {
    cards.push_back(card);
    int new_ns_tricks = SolvePlay(deal, trump, lead_seat, cards);
    cards.pop_back();
    int trick_diff =
        ns_contract ? new_ns_tricks - target_ns_tricks : target_ns_tricks - new_ns_tricks;
    size_t len = strlen(buffer);
    snprintf(buffer + len, sizeof(buffer) - len, "%s:%+d ", NameOf(card), trick_diff);
  }
  return buffer;
}

#ifndef _TEST
#include <emscripten/bind.h>

using namespace emscripten;

EMSCRIPTEN_BINDINGS(my_module) {
  function("shuffle_and_solve", &shuffle_and_solve);
  function("solve", &solve);
  function("solve_plays", &solve_plays);
}
#endif  // !_TEST
