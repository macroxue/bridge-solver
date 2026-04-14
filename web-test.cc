#define _WEB
#define _TEST

#include "solver.cc"
#include <cassert>
#include <regex>

void Test1() {
  std::string west("♠ 7 ♥ QJ7542 ♦ JT974 ♣ A");
  std::string north("♠ T9852 ♥ KT83 ♦ 862 ♣ 6");
  std::string east("♠ AKJ4 ♥ 9 ♦ AK5 ♣ Q7542");
  std::string south("♠ Q63 ♥ A6 ♦ Q3 ♣ KJT983");

  // Double-dummy results.
  auto dd_results = solve(west, north, east, south);
  printf("%s", dd_results.c_str());
  std::regex dd_results_re("N  4  4  9  9 .*\n"
                           "S  5  5  7  7 .*\n"
                           "H  3  3 10 10 .*\n"
                           "D  2  2 11 11 .*\n"
                           "C  5  5  7  7 .*\n");
  std::smatch m;
  assert(std::regex_match(dd_results, m, dd_results_re));

  // Opening plays.
  auto plays = solve_plays(west, north, east, south, 3, NOTRUMP, SOUTH, "");
  printf("South: %s\n", plays.c_str());
  assert(plays == "SQ:+1 S6:+1 S3:+1 HA:+0 H6:+1 DQ:+0 D3:+0 CK:+1 CJ:+0 C3:+0 ");

  // South played CJ.
  plays = solve_plays(west, north, east, south, 3, NOTRUMP, SOUTH, "CJ");
  printf("West: %s\n", plays.c_str());
  assert(plays == "CA:+0 ");

  // West played CA.
  plays = solve_plays(west, north, east, south, 3, NOTRUMP, SOUTH, "CJCA");
  printf("North: %s\n", plays.c_str());
  assert(plays == "C6:+0 ");

  // North played C6.
  plays = solve_plays(west, north, east, south, 3, NOTRUMP, SOUTH, "CJCAC6");
  printf("East: %s\n", plays.c_str());
  assert(plays == "CQ:+0 C7:+0 C5:+0 C2:+0 ");

  // East played C2.
  plays = solve_plays(west, north, east, south, 3, NOTRUMP, SOUTH, "CJCAC6C2");
  printf("West: %s\n", plays.c_str());
  assert(plays == "S7:+0 HQ:+0 H7:+0 H5:+0 H2:+0 DJ:+0 D7:+0 D4:+0 ");

  std::string played("CJCAC6C2"   // 1
                    "D4D2DAD3"   // 2
                    "DKDQD7D6"   // 3
                    "D5C3DJD8"   // 4
                    "DTS2C7CT"   // 5
                    "D9S5C5C9"   // 6
                    "S7STSAS3"   // 7
                    "SKS6H7S9"   // 8
                    "H9HAH5H3"   // 9
                    "H6HQHKS4"   // 10
                    "S8SJSQH4"   // 11
                    "CKH2HTC4"); // 12
  plays = solve_plays(west, north, east, south, 3, NOTRUMP, SOUTH, played);
  printf("South: %s\n", plays.c_str());
  assert(plays == "C8:+0 ");

  plays = solve_plays(west, north, east, south, 3, NOTRUMP, SOUTH, played + "C8");
  printf("West: %s\n", plays.c_str());
  assert(plays == "HJ:+0 ");

  plays = solve_plays(west, north, east, south, 3, NOTRUMP, SOUTH, played + "C8HJ");
  printf("North: %s\n", plays.c_str());
  assert(plays == "H8:+0 ");

  plays = solve_plays(west, north, east, south, 3, NOTRUMP, SOUTH, played + "C8HJH8");
  printf("East: %s\n", plays.c_str());
  assert(plays == "CQ:+0 ");
}

void Test2() {
  std::string west("♠ 43 ♥ 82 ♦ AQT874 ♣ AQ6");
  std::string north("♠ AQ95 ♥ K754 ♦ J ♣ JT54");
  std::string east("♠ KJ2 ♥ JT ♦ 9652 ♣ K732");
  std::string south("♠ T876 ♥ AQ963 ♦ K3 ♣ 98");

  // Double-dummy results.
  auto dd_results = solve(west, north, east, south);
  printf("%s", dd_results.c_str());
  std::regex dd_results_re("N  4  4  7  7 .*\n"
                           "S  8  8  5  5 .*\n"
                           "H  8  8  5  5 .*\n"
                           "D  3  3 10 10 .*\n"
                           "C  4  4  9  9 .*\n");
  std::smatch m;
  assert(std::regex_match(dd_results, m, dd_results_re));

  // Opening plays.
  auto plays = solve_plays(west, north, east, south, 3, HEART, WEST, "");
  printf("West: %s\n", plays.c_str());
  assert(plays == "S4:-1 H8:-1 H2:-1 DA:-1 DQ:+0 DT:+0 D8:+0 D4:+0 CA:-1 CQ:-1 C6:-1 ");

  // Last trick.
  std::string played("S4SAS2S8"   // 1
                    "DJD2DKDA"   // 2
                    "S3S5SJS7"   // 3
                    "SKS6D4S9"   // 4
                    "HJH6H8HK"   // 5
                    "H7HTHAH2"   // 6
                    "STD8SQC3"   // 7
                    "H5D6HQD7"   // 8
                    "D3DQH4D9"   // 9
                    "C5CKC9C6"   // 10
                    "C7C8CACJ"   // 11
                    "CQCTC2H9"); // 12
  plays = solve_plays(west, north, east, south, 3, HEART, WEST, played);
  printf("South: %s\n", plays.c_str());
  assert(plays == "H3:-1 ");

  plays = solve_plays(west, north, east, south, 3, HEART, WEST, played + "H3");
  printf("West: %s\n", plays.c_str());
  assert(plays == "DT:-1 ");

  plays = solve_plays(west, north, east, south, 3, HEART, WEST, played + "H3DT");
  printf("North: %s\n", plays.c_str());
  assert(plays == "C4:-1 ");

  plays = solve_plays(west, north, east, south, 3, HEART, WEST, played + "H3DTC4");
  printf("East: %s\n", plays.c_str());
  assert(plays == "D5:-1 ");
}

void TestDifferentContracts() {
  std::string west("K943 74 T632 Q85 ");
  std::string north("J5 KQJ532 5 KJT2 ");
  std::string east("AT872 A96 QJ87 9 ");
  std::string south("Q6 T8 AK94 A7643 ");

  // Opening plays for 4S by East.
  auto plays = solve_plays(west, north, east, south, 4, SPADE, SOUTH, "");
  printf("South: %s\n", plays.c_str());
  assert(plays == "SQ:-1 S6:-1 HT:-1 H8:-1 DA:-3 D9:-1 D4:-1 CA:-2 C7:-2 C4:-2 ");

  // Opening plays for 4H by North.
  plays = solve_plays(west, north, east, south, 4, HEART, EAST, "");
  printf("East: %s\n", plays.c_str());
  assert(plays == "SA:+0 ST:+0 S8:+0 S2:+0 HA:+0 H9:+1 H6:+1 DQ:+1 D8:+1 C9:+0 ");
}

int main(int argc, char *argv[]) {
  Test1();
  Test2();
  TestDifferentContracts();
  return 0;
}
