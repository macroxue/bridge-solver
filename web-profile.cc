// Drives the real _WEB code paths (solve/solve_plays) against a hard deal
// to produce PGO profile data (see the web.profdata makefile target).
// Has its own main() so it can run directly under Node.
#define _WEB
#define _TEST

#include "solver.cc"

int main() {
  // hard_deals/deal.8, the same deal used to train the native PGO build.
  std::string west("♠ KT8 ♥ KJ9875 ♦ Q8 ♣ 75");
  std::string north("♠ 42 ♥ AQT63 ♦ K52 ♣ T84");
  std::string east("♠ AJ65 ♥ 42 ♦ J963 ♣ KQJ");
  std::string south("♠ Q973 ♥ - ♦ AT74 ♣ A9632");

  auto dd_results = solve(west, north, east, south);
  printf("%s", dd_results.c_str());

  auto plays = solve_plays(west, north, east, south, 4, SPADE, WEST, "");
  printf("%s\n", plays.c_str());
  return 0;
}
