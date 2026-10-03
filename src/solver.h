// The public API of the double-dummy solver in src/solver.cc, used by the
// command line in src/solver-cli.cc and the web app in web/web-bindings.cc.
// Everything else in src/solver.cc is internal.
#ifndef SOLVER_H
#define SOLVER_H

#include <functional>
#include <string>
#include <vector>

enum { SPADE, HEART, DIAMOND, CLUB, NUM_SUITS, NOTRUMP = NUM_SUITS };
enum { TWO, TEN = 8, JACK, QUEEN, KING, ACE, NUM_RANKS };
enum { WEST, NORTH, EAST, SOUTH, NUM_SEATS };

const int TOTAL_TRICKS = NUM_RANKS;

// Settings for every search.
struct Options {
  int guess_tricks = -1;             // N/S tricks to try first; -1 guesses from the hands.
  int displaying_depth = -1;         // _DEBUG builds trace the search to this depth.
  int stats_level = 0;               // Show search and cache statistics after each search.
  bool discard_suit_bottom = false;  // Discard only a suit's lowest card: faster, imprecise.
};
extern Options options;

const char* SeatName(int seat);
const char* SuitName(int suit);
const char* SuitSign(int suit);  // Colored when stdout is a terminal.
char RankName(int rank);
bool IsNs(int seat);

// These exit on an unknown character.
int CharToSuit(char c);
int CharToRank(char c);
int CharToSeat(char c);

// A card is an int; only these functions look inside it.
int CardOf(int suit, int rank);
int SuitOf(int card);
int RankOf(int card);
const char* NameOf(int card);  // Suit letter and rank, e.g. "SA".

// A deal or an ending, as each seat's cards from spades to clubs, with suits
// separated by spaces and '-' for a void, e.g. "AK2 QJT9 - 8765432". Other
// characters, such as suit symbols, are ignored.
struct Deal {
  std::string hands[NUM_SEATS];
};

// Checks a deal typed by a user and writes it in the form above. An 'x' is the
// lowest card of its suit not given out yet, going W, N, E, S, and hands left
// empty get the remaining cards at random. Exits on an invalid deal.
Deal ParseDeal(const Deal& deal);
Deal RandomDeal();
// Deals the cards of `seats`, e.g. "WE", among them again at random.
Deal ShuffleDeal(const Deal& deal, const char* seats);
// A full deal's unique code and back.
std::string EncodeDeal(const Deal& deal);
Deal DecodeDeal(const char* code);

int NumTricks(const Deal& deal);
void ShowCompact(const Deal& deal, int rotation = 0);
// Also shows each hand's high-card points.
void ShowDetailed(const Deal& deal, int rotation = 0);

// Solves a deal for each trump and then each lead seat, giving the N/S tricks
// to `seat_done`.
void Solve(const Deal& deal, const std::vector<int>& trumps, const std::vector<int>& lead_seats,
           const std::function<void(int trump)>& trump_start,
           const std::function<void(int trump, int lead_seat, int ns_tricks)>& seat_done,
           const std::function<void(int trump)>& trump_done);

// The position after `played_cards` from the start of a deal.
struct Position {
  Deal hands;  // The cards not played yet.
  int seat_to_play;
  int ns_tricks_won;  // In the tricks finished so far.
  // The cards seat_to_play may play, one of each group of equivalent cards.
  std::vector<int> playable_cards;
};
Position GetPosition(const Deal& deal, int trump, int lead_seat,
                     const std::vector<int>& played_cards);

// The N/S tricks at the end of the deal when both sides play their best after
// `played_cards`, which has at least one card. The search starts at
// `guess_ns_tricks`, or at a guess from the hands when it is -1. Searches by
// Solve() and SolvePlay() share their caches while the deal and trump stay the
// same.
int SolvePlay(const Deal& deal, int trump, int lead_seat, const std::vector<int>& played_cards,
              int guess_ns_tricks = -1);

#endif  // SOLVER_H
