// The command line of the double-dummy solver, built on the API in solver.h.
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "solver.h"

struct CliOptions {
  char* code = nullptr;
  char* input_file = nullptr;
  char* shuffle_seats = nullptr;
  int trump = -1;
  int show_hands_mask = 2;
  bool deal_only = false;
  bool randomize = false;
  bool ignore_trump_and_lead = false;
  bool play_interactively = false;

  // Also sets the search options.
  void Read(int argc, char* argv[]) {
    int c;
    while ((c = getopt(argc, argv, "c:df:im:oprs:t:D:G:S:")) != -1) {
      switch (c) {
        // clang-format off
        case 'c': code = optarg; break;
        case 'd': options.discard_suit_bottom = true; break;
        case 'f': input_file = optarg; break;
        case 'i': ignore_trump_and_lead = true; break;
        case 'm': show_hands_mask = atoi(optarg); break;
        case 'o': deal_only = true; break;
        case 'p': play_interactively = true; break;
        case 'r': randomize = true; break;
        case 's': shuffle_seats = optarg; break;
        case 't': trump = CharToSuit(optarg[0]); break;
        case 'D': options.displaying_depth = atoi(optarg); break;
        case 'G': options.guess_tricks = atoi(optarg); break;
        case 'S': options.stats_level = atoi(optarg); break;
          // clang-format on
      }
    }
  }

  void ShowUsage(const char* name) {
    printf("%s  A fast double-dummy solver for the card game of Bridge.\n", name);
    printf(
        "\t-r           Solve a random deal.\n"
        "\t-f <file>    Solve a deal in the input file. See files in *_deals/ for examples.\n"
        "\t-c <code>    Solve a deal defined by its unique code. See -m below.\n"
        "\t-p           Play interactively, possibly exploring all paths.\n"
        "\n"
        "\t-s <seats>   Shuffle hands in the specified seats, a combination of {W, N, E, S}.\n"
        "\t-m <mask>    Mask for showing a deal. The following values can be added.\n"
        "\t               1    Show the deal's unique code\n"
        "\t               2    Show the deal in compact format\n"
        "\t               4    Show the deal in expanded format\n"
        "\t-o           Show the deal without solving it.\n"
        "\t-i           Ignore the strain and the leading seat specified in the input file.\n"
        "\t-t <strain>  Solve for the specified strain, one of {N, S, H, D, C}.\n"
        "\t-d           Discard only the smallest card in a suit, imprecise but faster.\n");
    exit(0);
  }
} cli_options;

double Now() {
  timeval now;
  gettimeofday(&now, nullptr);
  return now.tv_sec + now.tv_usec * 1e-6;
}

Deal ReadDeal(std::vector<int>& trumps, std::vector<int>& lead_seats) {
  auto* const input_file = fopen(cli_options.input_file, "rt");
  if (!input_file) {
    fprintf(stderr, "Input file not found: '%s'.\n", cli_options.input_file);
    exit(-1);
  }
  // read hands
  char line[NUM_SEATS][120];
  assert(fgets(line[NORTH], sizeof(line[NORTH]), input_file));
  assert(fgets(line[WEST], sizeof(line[WEST]), input_file));
  char* gap = strstr(line[WEST], "    ");
  if (!gap) gap = strstr(line[WEST], "\t");
  if (gap != nullptr && gap != line[WEST]) {
    // East hand is on the same line as West.
    strcpy(line[EAST], gap);
    *gap = '\0';
  } else {
    assert(fgets(line[EAST], sizeof(line[EAST]), input_file));
  }
  assert(fgets(line[SOUTH], sizeof(line[SOUTH]), input_file));

  Deal deal;
  for (int seat = 0; seat < NUM_SEATS; ++seat) deal.hands[seat] = line[seat];
  deal = ParseDeal(deal);

  if (!cli_options.ignore_trump_and_lead && fscanf(input_file, " %s ", line[0]) == 1)
    trumps = {CharToSuit(line[0][0])};

  if (!cli_options.ignore_trump_and_lead && fscanf(input_file, " %s ", line[0]) == 1)
    lead_seats = {CharToSeat(line[0][0])};

  fclose(input_file);
  return deal;
}

class InteractivePlay {
 public:
  InteractivePlay(const Deal& deal, int trump, int lead_seat, int target_ns_tricks)
      : deal(deal),
        trump(trump),
        lead_seat(lead_seat),
        target_ns_tricks(target_ns_tricks),
        num_tricks(NumTricks(deal)) {
    ShowUsage();
    DetermineContract();

    int ns_tricks = target_ns_tricks;
    for (int p = 0; p < num_tricks * 4; ++p) {
      auto position = GetPosition(deal, trump, lead_seat, played_cards);
      if (p % 4 == 0 && !SetupTrick(position)) break;

      auto card_tricks = EvaluateCards(position, ns_tricks);
      int card_to_play;
      switch (SelectCard(card_tricks, position, &card_to_play)) {
        case PLAY:
          // Save the old NS tricks first.
          play_history.push_back({(int)card_tricks.size(), ns_tricks});
          ns_tricks = card_tricks[card_to_play];
          played_cards.push_back(card_to_play);
          break;
        case UNDO:
          // Undo to the beginning of the previous trick.
          while (p > 0) {
            --p;
            played_cards.pop_back();
            ns_tricks = play_history.back().ns_tricks;
            int num_choices = play_history.back().num_choices;
            play_history.pop_back();
            if (num_choices > 1 && p % 4 == 0) break;
          }
          --p;
          break;
        case ROTATE:
          rotation = (rotation + 3) % 4;
          if (p % 4 != 0) ShowDetailed(position.hands, rotation);
          --p;
          break;
        case NEXT:
          return;
      }
    }
  }

 private:
  void ShowUsage() const {
    static bool first_time = true;
    if (first_time) {
      first_time = false;
      puts(
          "******\n"
          "<Enter>/<Space> to accept the suggestion or input another card like 'CK'.\n"
          "If there is only one club or one king in the list, 'C' or 'K' works too.\n"
          "Use 'U' to undo, 'R' to rotate the board or 'N' to play the next hand.\n"
          "******");
    }
  }

  void DetermineContract() {
    if (target_ns_tricks >= (num_tricks + 1) / 2) {
      starting_ns_tricks = TOTAL_TRICKS - num_tricks;
      ns_contract = true;
      int level = (TOTAL_TRICKS - num_tricks) + target_ns_tricks - 6;
      auto declarer = starting_ns_tricks == 0 ? SeatName((lead_seat + 3) % 4) : "NS";
      snprintf(contract, sizeof(contract), "%d%s by %s", level, SuitSign(trump), declarer);
    } else {
      starting_ew_tricks = TOTAL_TRICKS - num_tricks;
      ns_contract = false;
      int level = TOTAL_TRICKS - target_ns_tricks - 6;
      auto declarer = starting_ew_tricks == 0 ? SeatName((lead_seat + 3) % 4) : "EW";
      snprintf(contract, sizeof(contract), "%d%s by %s", level, SuitSign(trump), declarer);
    }
  }

  bool SetupTrick(const Position& position) const {
    int trick_index = played_cards.size() / 4;
    printf("------ %s: NS %d EW %d ------\n", contract,
           starting_ns_tricks + position.ns_tricks_won,
           starting_ew_tricks + trick_index - position.ns_tricks_won);
    ShowDetailed(position.hands, rotation);
    if (trick_index == num_tricks - 1) {
      int ns_tricks_won = SolvePlay(deal, trump, lead_seat, With(position.playable_cards[0]));
      printf("====== %s: NS %d EW %d ======\n", contract, starting_ns_tricks + ns_tricks_won,
             starting_ew_tricks + trick_index + 1 - ns_tricks_won);
      return false;
    }
    return true;
  }

  // The cards played so far, then `card`.
  std::vector<int> With(int card) const {
    auto cards = played_cards;
    cards.push_back(card);
    return cards;
  }

  typedef std::map<int, int> CardTricks;

  CardTricks EvaluateCards(const Position& position, int ns_tricks) const {
    int last_suit = NOTRUMP;
    CardTricks card_tricks;
    printf("From");
    for (int card : position.playable_cards) {
      if (SuitOf(card) != last_suit) {
        last_suit = SuitOf(card);
        printf(" %s ", SuitSign(SuitOf(card)));
      }
      printf("%c?\b", RankName(RankOf(card)));
      fflush(stdout);

      int new_ns_tricks = SolvePlay(deal, trump, lead_seat, With(card), ns_tricks);
      card_tricks[card] = new_ns_tricks;

      int trick_diff =
          ns_contract ? new_ns_tricks - target_ns_tricks : target_ns_tricks - new_ns_tricks;
      if (-1 <= trick_diff && trick_diff <= 1)
        printf("%c", "-=+"[trick_diff + 1]);
      else
        printf("(%+d)", trick_diff);
      fflush(stdout);
    }
    printf(" %s plays ", SeatName(position.seat_to_play));
    return card_tricks;
  }

  // Whether card1 is in a later suit than card2, or lower in the same suit.
  static bool ComesAfter(int card1, int card2) {
    if (SuitOf(card1) != SuitOf(card2)) return SuitOf(card1) > SuitOf(card2);
    return RankOf(card1) < RankOf(card2);
  }

  enum Action { PLAY, UNDO, ROTATE, NEXT };

  Action SelectCard(const CardTricks& card_tricks, const Position& position,
                    int* card_to_play) const {
    // Auto-play when there is only one choice.
    if (card_tricks.size() == 1) {
      *card_to_play = card_tricks.begin()->first;
      printf("%s.\n", ColoredNameOf(*card_to_play));
      return PLAY;
    }

    // Choose the optimal play, using rank as the tie-breaker.
    *card_to_play = -1;
    if (IsNs(position.seat_to_play)) {
      int max_ns_tricks = -1;
      for (const auto& pair : card_tricks) {
        if (pair.second > max_ns_tricks ||
            (pair.second == max_ns_tricks && ComesAfter(pair.first, *card_to_play))) {
          *card_to_play = pair.first;
          max_ns_tricks = pair.second;
        }
      }
    } else {
      int min_ns_tricks = TOTAL_TRICKS + 1;
      for (const auto& pair : card_tricks) {
        if (pair.second < min_ns_tricks ||
            (pair.second == min_ns_tricks && ComesAfter(pair.first, *card_to_play))) {
          *card_to_play = pair.first;
          min_ns_tricks = pair.second;
        }
      }
    }
    printf("%s?", ColoredNameOf(*card_to_play));
    fflush(stdout);

    std::set<int> playable_cards;
    for (const auto& pair : card_tricks) playable_cards.insert(pair.first);

    int suit = SuitOf(*card_to_play);
    int rank = RankOf(*card_to_play);
    while (true) {
      switch (int c = toupper(GetRawChar())) {
        case '\n':
        case ' ':
          if (suit != -1 && rank != -1) {
            *card_to_play = CardOf(suit, rank);
            printf("\b.\n");
            return PLAY;
          }
          break;
        case 'R':
          printf("\n");
          return ROTATE;
        case 'U':
          if (!played_cards.empty()) {
            printf("\n");
            return UNDO;
          }
          break;
        case 'N':
          printf("\n");
          return NEXT;
        case 'S':
        case 'H':
        case 'D':
        case 'C': {
          std::set<int> matches;
          for (const auto& card : playable_cards) {
            if (strchr(NameOf(card), c)) matches.insert(card);
          }
          if (matches.empty()) break;
          suit = SuitOf(*matches.begin());
          if (matches.size() == 1) {
            rank = RankOf(*matches.begin());
          } else if (rank != -1) {
            if (playable_cards.find(CardOf(suit, rank)) == playable_cards.end()) rank = -1;
          }
          break;
        }
        default:
          std::set<int> matches;
          for (const auto& card : playable_cards) {
            if (strchr(NameOf(card), c)) matches.insert(card);
          }
          if (matches.empty()) break;
          rank = RankOf(*matches.begin());
          if (matches.size() == 1) {
            suit = SuitOf(*matches.begin());
          } else if (suit != -1) {
            if (playable_cards.find(CardOf(suit, rank)) == playable_cards.end()) suit = -1;
          }
          break;
      }
      if (rank == -1)
        printf("\b\b\b\b%s  ?", SuitSign(suit));
      else if (suit == -1)
        printf("\b\b\b\b  %c?", RankName(rank));
      else
        printf("\b\b\b\b%s?", ColoredNameOf(CardOf(suit, rank)));
      fflush(stdout);
    }
  }

  static char* ColoredNameOf(int card) {
    static char name[32];
    snprintf(name, sizeof(name), "%s %c", SuitSign(SuitOf(card)), RankName(RankOf(card)));
    return name;
  }

  static char GetRawChar() {
    char buf = 0;
    struct termios old = {0};
    if (tcgetattr(0, &old) < 0) perror("tcsetattr()");
    old.c_lflag &= ~ICANON;
    old.c_lflag &= ~ECHO;
    old.c_cc[VMIN] = 1;
    old.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSANOW, &old) < 0) perror("tcsetattr ICANON");
    ssize_t num_read = read(0, &buf, 1);
    old.c_lflag |= ICANON;
    old.c_lflag |= ECHO;
    if (tcsetattr(0, TCSADRAIN, &old) < 0) perror("tcsetattr ~ICANON");
    if (num_read < 0) {
      perror("read()");
      exit(-1);
    }
    // Piped input ran out: stop instead of reading nothing forever.
    if (num_read == 0) {
      printf("\n");
      exit(0);
    }
    return (buf);
  }

  const Deal deal;
  const int trump;
  const int lead_seat;
  const int target_ns_tricks;
  const int num_tricks;
  std::vector<int> played_cards;

  char contract[80];
  bool ns_contract = false;
  int starting_ns_tricks = 0;
  int starting_ew_tricks = 0;
  int rotation = 0;

  struct PlayRecord {
    int num_choices;
    int ns_tricks;
  };
  std::vector<PlayRecord> play_history;
};

int main(int argc, char* argv[]) {
  cli_options.Read(argc, argv);

  Deal deal;
  std::vector<int> trumps = {NOTRUMP, SPADE, HEART, DIAMOND, CLUB};
  std::vector<int> lead_seats = {WEST, EAST, NORTH, SOUTH};
  if (cli_options.code)
    deal = DecodeDeal(cli_options.code);
  else if (cli_options.input_file)
    deal = ReadDeal(trumps, lead_seats);
  else if (cli_options.randomize)
    deal = RandomDeal();
  else
    cli_options.ShowUsage(argv[0]);
  if (cli_options.shuffle_seats) deal = ShuffleDeal(deal, cli_options.shuffle_seats);

  if (cli_options.show_hands_mask & 1) printf("# %s\n", EncodeDeal(deal).c_str());
  if (cli_options.show_hands_mask & 2) ShowCompact(deal);
  if (cli_options.show_hands_mask & 4) ShowDetailed(deal);
  if (cli_options.deal_only) return 0;

  if (cli_options.trump != -1) {
    trumps.clear();
    trumps.push_back(cli_options.trump);
  }
  int num_tricks = NumTricks(deal);
  if (cli_options.play_interactively) {
    auto do_nothing = [](int trump) {};
    auto seat_done = [&deal, num_tricks](int trump, int lead_seat, int ns_tricks) {
      if (num_tricks < TOTAL_TRICKS ||
          (num_tricks == TOTAL_TRICKS && ns_tricks >= 7 && !IsNs(lead_seat)) ||
          (num_tricks == TOTAL_TRICKS && ns_tricks < 7 && IsNs(lead_seat))) {
        InteractivePlay(deal, trump, lead_seat, ns_tricks);
      } else {
        int declarer = (lead_seat + 3) % NUM_SEATS;
        printf("%s can't make a %s contract.\n", SeatName(declarer), SuitSign(trump));
      }
    };
    Solve(deal, trumps, lead_seats, do_nothing, seat_done, do_nothing);
  } else {
    auto start_time = Now();
    auto trump_start = [](int trump) { printf("%c", SuitName(trump)[0]); };
    auto seat_done = [num_tricks](int trump, int lead_seat, int ns_tricks) {
      printf(" %2d", IsNs(lead_seat) ? num_tricks - ns_tricks : ns_tricks);
    };
    auto trump_done = [start_time](int trump) {
      struct rusage usage;
      getrusage(RUSAGE_SELF, &usage);
      // ru_maxrss is KB on Linux, but bytes on macOS specifically -- other
      // BSDs (FreeBSD/OpenBSD/NetBSD) report KB like Linux.
#ifdef __APPLE__
      double peak_mb = usage.ru_maxrss / (1024.0 * 1024.0);
#else
      double peak_mb = usage.ru_maxrss / 1024.0;
#endif
      printf(" %5.2f s %5.1f M\n", Now() - start_time, peak_mb);
    };
    Solve(deal, trumps, lead_seats, trump_start, seat_done, trump_done);
  }
  return 0;
}
