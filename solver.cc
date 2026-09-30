#include <sys/resource.h>
#include <sys/stat.h>
#include <termios.h>

#include "solver.h"

char suit_of[CARD_END];
char rank_of[CARD_END];
char card_of[NUM_SUITS][16];
char name_of[CARD_END][4];

struct CardInitializer {
  CardInitializer() {
    for (int suit = 0; suit < NUM_SUITS; ++suit) {
      for (int rank = 0; rank < NUM_RANKS; ++rank) {
        int card = suit * SUIT_SPAN + NUM_RANKS - 1 - rank;
        suit_of[card] = suit;
        rank_of[card] = rank;
        card_of[SuitOf(card)][RankOf(card)] = card;
        name_of[card][0] = SuitName(SuitOf(card))[0];
        name_of[card][1] = RankName(RankOf(card));
        name_of[card][2] = '\0';
      }
    }
  }
} card_initializer;

const char* SuitSign(int suit) {
  static const char* plain_suit_signs[] = {"♠", "♥", "♦", "♣", "NT"};
#ifdef _DEBUG
  return plain_suit_signs[suit];
#else
  static const char* color_suit_signs[] = {"♠", "\e[31m♥\e[0m", "\e[31m♦\e[0m", "♣", "NT"};
  static struct stat stdout_stat;
  static bool is_terminal = fstat(1, &stdout_stat) == 0 && S_ISCHR(stdout_stat.st_mode);
  return is_terminal ? color_suit_signs[suit] : plain_suit_signs[suit];
#endif
}

void Cards::Show() const {
  for (int suit = 0; suit < NUM_SUITS; ++suit) {
    ShowSuit(suit);
    putchar(' ');
  }
}

void Cards::ShowSuit(int suit) const {
  printf("%s ", SuitSign(suit));
  if (Suit(suit))
    for (int card : Suit(suit)) putchar(NameOf(card)[1]);
  else
    putchar('-');
}

void Hands::Show() const {
  for (int seat = 0; seat < NUM_SEATS; ++seat) {
    hands[seat].Show();
    if (seat < NUM_SEATS - 1) printf(", ");
  }
  puts("");
}

void Hands::ShowCode() const {
  uint64_t values[3];
  auto mask = DECK_MASK;
  for (int seat = 0; seat < NUM_SEATS - 1; ++seat) {
    values[seat] = PackBits(hands[seat].Value(), mask);
    mask &= ~hands[seat].Value();
  }
  printf("# %" PRIX64 ",%" PRIX64 ",%" PRIX64 "\n", values[0], values[1], values[2]);
}

void Hands::ShowCompact(int rotation) const {
  int seat = (NORTH + rotation) % NUM_SEATS;
  printf("%25s ", " ");
  hands[seat].Show();
  printf("\n");

  seat = (WEST + rotation) % NUM_SEATS;
  int num_cards = hands[seat].Size();
  printf("%*s ", 14 - num_cards, " ");
  hands[seat].Show();

  seat = (EAST + rotation) % NUM_SEATS;
  printf("%*s ", num_cards + 8, " ");
  hands[seat].Show();
  printf("\n");

  seat = (SOUTH + rotation) % NUM_SEATS;
  printf("%25s ", " ");
  hands[seat].Show();
  printf("\n");
}

void Hands::ShowDetailed(int rotation) const {
  int gap = 26;
  int seat = (NORTH + rotation) % NUM_SEATS;
  for (int suit = 0; suit < NUM_SUITS; ++suit) {
    ShowHandInfo(hands[seat], seat, suit, gap);
    hands[seat].ShowSuit(suit);
    printf("\n");
  }
  for (int suit = 0; suit < NUM_SUITS; ++suit) {
    gap = 13;
    seat = (WEST + rotation) % NUM_SEATS;
    ShowHandInfo(hands[seat], seat, suit, gap);
    hands[seat].ShowSuit(suit);

    gap = 26 - std::max(1, hands[seat].Suit(suit).Size());
    seat = (EAST + rotation) % NUM_SEATS;
    ShowHandInfo(hands[seat], seat, suit, gap);
    hands[seat].ShowSuit(suit);
    printf("\n");
  }
  gap = 26;
  seat = (SOUTH + rotation) % NUM_SEATS;
  for (int suit = 0; suit < NUM_SUITS; ++suit) {
    ShowHandInfo(hands[seat], seat, suit, gap);
    hands[seat].ShowSuit(suit);
    printf("\n");
  }
}

void Hands::ShowHandInfo(Cards hand, int seat, int suit, int gap) const {
  if (suit == SPADE)
    printf("%*s%c ", gap - 2, " ", SeatLetter(seat));
  else if (suit == CLUB)
    printf("%*s%2d ", gap - 3, " ", hand.Points());
  else
    printf("%*s", gap, " ");
}

Options options;
Hands empty_hands;
Stat stats[TOTAL_CARDS];
Cache<ShapeEntry> common_bounds_cache("Common Bounds Cache", 13);
Cache<CutoffEntry> cutoff_cache("Cut-off Cache", 16);

void Options::Read(int argc, char* argv[]) {
  int c;
  while ((c = getopt(argc, argv, "c:df:im:oprs:t:D:G:S:")) != -1) {
    switch (c) {
      // clang-format off
      case 'c': code = optarg; break;
      case 'd': discard_suit_bottom = true; break;
      case 'f': input_file = optarg; break;
      case 'i': ignore_trump_and_lead = true; break;
      case 'm': show_hands_mask = atoi(optarg); break;
      case 'o': deal_only = true; break;
      case 'p': play_interactively = true; break;
      case 'r': randomize = true; break;
      case 's': shuffle_seats = optarg; break;
      case 't': trump = CharToSuit(optarg[0]); break;
      case 'D': displaying_depth = atoi(optarg); break;
      case 'G': guess_tricks = atoi(optarg); break;
      case 'S': stats_level = atoi(optarg); break;
        // clang-format on
    }
  }
}

void Options::ShowUsage(const char* name) {
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

void ReadHands(Hands& hands, std::vector<int>& trumps, std::vector<int>& lead_seats) {
  auto* const input_file = fopen(options.input_file, "rt");
  if (!input_file) {
    fprintf(stderr, "Input file not found: '%s'.\n", options.input_file);
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

  int num_tricks = 0;
  Cards all_cards;
  std::vector<int> empty_seats;
  for (int seat = 0; seat < NUM_SEATS; ++seat) {
    hands[seat] = ParseHand(line[seat], all_cards);
    all_cards.Add(hands[seat]);
    if (num_tricks == 0 && hands[seat])
      num_tricks = hands[seat].Size();
    else if (hands[seat] && hands[seat].Size() != num_tricks) {
      fprintf(stderr, "%s has %d cards, while %s has %d.\n", SeatName(seat), hands[seat].Size(),
              SeatName(0), num_tricks);
      exit(-1);
    } else if (!hands[seat])
      empty_seats.push_back(seat);
  }
  if (!empty_seats.empty()) {
    if (num_tricks != TOTAL_TRICKS && empty_seats.size() != NUM_SEATS) {
      fprintf(stderr, "%d trick(s) already played.\n", TOTAL_TRICKS - num_tricks);
      exit(-1);
    }
    hands.Deal(all_cards.Complement(), empty_seats);
  }

  if (!options.ignore_trump_and_lead && fscanf(input_file, " %s ", line[0]) == 1)
    trumps = {CharToSuit(line[0][0])};

  if (!options.ignore_trump_and_lead && fscanf(input_file, " %s ", line[0]) == 1)
    lead_seats = {CharToSeat(line[0][0])};

  fclose(input_file);
}

class InteractivePlay {
 public:
  InteractivePlay(const Hands& hands, int trump, int lead_seat, int target_ns_tricks)
      : min_max(hands, trump, lead_seat),
        target_ns_tricks(target_ns_tricks),
        num_tricks(hands.num_tricks()),
        trump(trump) {
    ShowUsage();
    DetermineContract(lead_seat);

    int ns_tricks = target_ns_tricks;
    for (int p = 0; p < num_tricks * 4; ++p) {
      auto& play = min_max.play(p);
      if (play.TrickStarting() && !SetupTrick(play)) break;

      auto card_tricks = EvaluateCards(play, ns_tricks, ns_contract);
      int card_to_play;
      switch (SelectCard(card_tricks, play, &card_to_play)) {
        case PLAY:
          // Save the old NS tricks first.
          play_history.push_back({(int)card_tricks.size(), ns_tricks});
          ns_tricks = card_tricks[card_to_play];
          play.PlayCard(card_to_play);
          break;
        case UNDO:
          // Undo to the beginning of the previous trick.
          while (p > 0) {
            --p;
            min_max.play(p).UnplayCard();
            ns_tricks = play_history.back().ns_tricks;
            int num_choices = play_history.back().num_choices;
            play_history.pop_back();
            if (num_choices > 1 && p % 4 == 0) break;
          }
          --p;
          break;
        case ROTATE:
          rotation = (rotation + 3) % 4;
          if (!play.TrickStarting()) play.hands.ShowDetailed(rotation);
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

  void DetermineContract(int lead_seat) {
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

  bool SetupTrick(Play& play) const {
    // TODO: Clean up! Search() recomputes the same info.
    if (play.depth > 0) {
      play.ns_tricks_won = play.PreviousPlay().ns_tricks_won + play.PreviousPlay().NsWon();
      play.seat_to_play = play.PreviousPlay().WinningSeat();
    }

    play.trick->all_cards = play.hands.all_cards();
    play.ComputeShape();
    play.trick->ComputeRelativeHands(play.depth, play.hands);

    int trick_index = play.depth / 4;
    printf("------ %s: NS %d EW %d ------\n", contract, starting_ns_tricks + play.ns_tricks_won,
           starting_ew_tricks + trick_index - play.ns_tricks_won);
    play.hands.ShowDetailed(rotation);
    if (trick_index == num_tricks - 1) {
      int ns_tricks_won = play.CollectLastTrick().first;
      printf("====== %s: NS %d EW %d ======\n", contract, starting_ns_tricks + ns_tricks_won,
             starting_ew_tricks + trick_index + 1 - ns_tricks_won);
      return false;
    }
    return true;
  }

  typedef std::map<int, int> CardTricks;

  CardTricks EvaluateCards(Play& play, int ns_tricks, bool ns_contract) const {
    int last_suit = NOTRUMP;
    CardTricks card_tricks;
    printf("From");
    for (int card : play.trick->FilterEquivalent(play.GetPlayableCards())) {
      if (SuitOf(card) != last_suit) {
        last_suit = SuitOf(card);
        printf(" %s ", SuitSign(SuitOf(card)));
      }
      printf("%c?\b", NameOf(card)[1]);
      fflush(stdout);

      auto search = [&play, card](int beta) {
        play.PlayCard(card);
        auto [ns_tricks, _] = play.NextPlay().Search(beta);
        play.UnplayCard();
        return ns_tricks;
      };
      int new_ns_tricks = MemoryEnhancedTestDriver(search, num_tricks, ns_tricks);
      card_tricks[card] = new_ns_tricks;

      int trick_diff =
          ns_contract ? new_ns_tricks - target_ns_tricks : target_ns_tricks - new_ns_tricks;
      if (-1 <= trick_diff && trick_diff <= 1)
        printf("%c", "-=+"[trick_diff + 1]);
      else
        printf("(%+d)", trick_diff);
      fflush(stdout);
    }
    printf(" %s plays ", SeatName(play.seat_to_play));
    return card_tricks;
  }

  enum Action { PLAY, UNDO, ROTATE, NEXT };

  Action SelectCard(const CardTricks& card_tricks, const Play& play, int* card_to_play) const {
    // Auto-play when there is only one choice.
    if (card_tricks.size() == 1) {
      *card_to_play = card_tricks.begin()->first;
      printf("%s.\n", ColoredNameOf(*card_to_play));
      return PLAY;
    }

    // Choose the optimal play, using rank as the tie-breaker.
    *card_to_play = -1;
    if (IsNs(play.seat_to_play)) {
      int max_ns_tricks = -1;
      for (const auto& pair : card_tricks) {
        if (pair.second > max_ns_tricks ||
            (pair.second == max_ns_tricks && LowerRank(pair.first, *card_to_play))) {
          *card_to_play = pair.first;
          max_ns_tricks = pair.second;
        }
      }
    } else {
      int min_ns_tricks = TOTAL_TRICKS + 1;
      for (const auto& pair : card_tricks) {
        if (pair.second < min_ns_tricks ||
            (pair.second == min_ns_tricks && LowerRank(pair.first, *card_to_play))) {
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
          if (play.depth > 0) {
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

  char* ColoredNameOf(int card) const {
    static char name[32];
    snprintf(name, sizeof(name), "%s %c", SuitSign(SuitOf(card)), NameOf(card)[1]);
    return name;
  }

  char GetRawChar() const {
    char buf = 0;
    struct termios old = {0};
    if (tcgetattr(0, &old) < 0) perror("tcsetattr()");
    old.c_lflag &= ~ICANON;
    old.c_lflag &= ~ECHO;
    old.c_cc[VMIN] = 1;
    old.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSANOW, &old) < 0) perror("tcsetattr ICANON");
    if (read(0, &buf, 1) < 0) perror("read()");
    old.c_lflag |= ICANON;
    old.c_lflag |= ECHO;
    if (tcsetattr(0, TCSADRAIN, &old) < 0) perror("tcsetattr ~ICANON");
    return (buf);
  }

  MinMax min_max;
  const int target_ns_tricks;
  const int num_tricks;
  const int trump;

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

#ifndef _WEB
int main(int argc, char* argv[]) {
  options.Read(argc, argv);

  Hands hands;
  std::vector<int> trumps = {NOTRUMP, SPADE, HEART, DIAMOND, CLUB};
  std::vector<int> lead_seats = {WEST, EAST, NORTH, SOUTH};
  if (options.code)
    hands.Decode(options.code);
  else if (options.input_file)
    ReadHands(hands, trumps, lead_seats);
  else if (options.randomize)
    hands.Randomize();
  else
    options.ShowUsage(argv[0]);
  if (options.shuffle_seats) hands.Shuffle(options.shuffle_seats);

  if (options.show_hands_mask & 1) hands.ShowCode();
  if (options.show_hands_mask & 2) hands.ShowCompact();
  if (options.show_hands_mask & 4) hands.ShowDetailed();
  if (options.deal_only) return 0;

  if (options.trump != -1) {
    trumps.clear();
    trumps.push_back(options.trump);
  }
  if (options.play_interactively) {
    auto do_nothing = [](int trump) {};
    auto seat_done = [&hands](int trump, int lead_seat, int ns_tricks) {
      if (hands.num_tricks() < TOTAL_TRICKS ||
          (hands.num_tricks() == TOTAL_TRICKS && ns_tricks >= 7 && !IsNs(lead_seat)) ||
          (hands.num_tricks() == TOTAL_TRICKS && ns_tricks < 7 && IsNs(lead_seat))) {
        InteractivePlay(hands, trump, lead_seat, ns_tricks);
      } else {
        int declarer = (lead_seat + 3) % NUM_SEATS;
        printf("%s can't make a %s contract.\n", SeatName(declarer), SuitSign(trump));
      }
    };
    Solve(hands, trumps, lead_seats, do_nothing, seat_done, do_nothing);
  } else {
    auto start_time = Now();
    auto trump_start = [](int trump) { printf("%c", SuitName(trump)[0]); };
    auto seat_done = [&hands](int trump, int lead_seat, int ns_tricks) {
      printf(" %2d", IsNs(lead_seat) ? hands.num_tricks() - ns_tricks : ns_tricks);
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
    Solve(hands, trumps, lead_seats, trump_start, seat_done, trump_done);
  }
  return 0;
}
#endif  // _WEB
