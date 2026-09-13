#include "solver.cc"

#include <atomic>
#include <cassert>
#include <chrono>
#include <mutex>
#include <set>
#include <thread>

static Hands LoadDeal01() {
  Hands hands;
  std::vector<int> trumps, lead_seats;
  options.input_file = const_cast<char*>("deals/fixed/deal.01");
  ReadHands(hands, trumps, lead_seats);
  options.input_file = nullptr;
  return hands;
}

void TestParallelStrainsCorrectness() {
  Hands hands = LoadDeal01();
  std::vector<int> trumps = {NOTRUMP, SPADE, HEART, DIAMOND, CLUB};
  std::vector<int> lead_seats = {WEST, EAST, NORTH, SOUTH};
  // Expected declarer tricks: South, North, West, East (see deals/fixed/RESULTS).
  const int expected[5][4] = {
      {9, 9, 3, 3},   // N
      {11, 11, 2, 2},  // S
      {8, 8, 4, 4},    // H
      {6, 6, 6, 6},    // D
      {7, 7, 3, 3},    // C
  };

  int trump_index = -1;
  int seat_index = 0;
  auto trump_start = [&](int trump) {
    ++trump_index;
    seat_index = 0;
    assert(trump == trumps[trump_index]);
  };
  auto seat_done = [&](int trump, int lead_seat, int ns_tricks) {
    assert(trump == trumps[trump_index]);
    assert(lead_seat == lead_seats[seat_index]);
    int reported = IsNs(lead_seat) ? hands.num_tricks() - ns_tricks : ns_tricks;
    assert(reported == expected[trump_index][seat_index]);
    ++seat_index;
  };
  auto trump_done = [&](int trump) {
    assert(trump == trumps[trump_index]);
    assert(seat_index == 4);
  };

  Solve(hands, trumps, lead_seats, trump_start, seat_done, trump_done);
  assert(trump_index == 4);
}

void TestStrainsSolvedOnParallelThreads() {
  Hands hands = LoadDeal01();
  std::vector<int> trumps = {NOTRUMP, SPADE, HEART, DIAMOND, CLUB};
  std::vector<int> lead_seats = {WEST, EAST, NORTH, SOUTH};

  std::atomic<int> live{0};
  std::atomic<int> max_live{0};
  std::mutex ids_mu;
  std::set<std::thread::id> ids;

  on_trump_work_begin = [&](int) {
    int v = ++live;
    int prev = max_live.load();
    while (prev < v && !max_live.compare_exchange_weak(prev, v)) {
    }
    {
      std::lock_guard<std::mutex> lock(ids_mu);
      ids.insert(std::this_thread::get_id());
    }
    // Give other strain threads time to enter before this one exits.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    --live;
  };

  auto ignore = [](int) {};
  auto ignore_seat = [](int, int, int) {};
  Solve(hands, trumps, lead_seats, ignore, ignore_seat, ignore);
  on_trump_work_begin = nullptr;

  assert(max_live.load() > 1);
  assert(ids.size() > 1);
}

int main() {
  TestParallelStrainsCorrectness();
  TestStrainsSolvedOnParallelThreads();
  puts("parallel_strains_test: OK");
  return 0;
}
