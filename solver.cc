#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <algorithm>

#define CHECK(statement)  if (!(statement)) printf("CHECK("#statement") failed")

namespace {

enum { SPADE, HEART, DIAMOND, CLUB, NUM_SUITS, NOTRUMP = NUM_SUITS };
enum { TWO, THREE, FOUR, FIVE, SIX, SEVEN, EIGHT, NINE, TEN,
       JACK, QUEEN, KING, ACE, NUM_RANKS };
enum { WEST, NORTH, EAST, SOUTH, NUM_SEATS };

const int SUIT_SIZE = 13;
const int TOTAL_TRICKS = 13;
const int TOTAL_CARDS = 52;

const char* SeatName(int seat) {
  static const char *seat_names[NUM_SEATS] = { "West", "North", "East", "South" };
  return seat_names[seat];
}
char SeatLetter(int seat) { return SeatName(seat)[0]; }

char suit_of[TOTAL_CARDS];
char rank_of[TOTAL_CARDS];
char card_of[NUM_SUITS][16];
char name_of[TOTAL_CARDS][4];

int SuitOf(int card) { return suit_of[card]; }
int RankOf(int card) { return rank_of[card]; }
int CardOf(int suit, int rank) { return card_of[suit][rank]; }
const char* NameOf(int card) { return name_of[card]; }

struct CardInitializer {
  CardInitializer() {
    static const char suit_names[] = "SHDC";
    static const char rank_names[] = "23456789TJQKA";
    for (int card = 0; card < TOTAL_CARDS; ++card) {
      suit_of[card] = card / SUIT_SIZE;
      rank_of[card] = SUIT_SIZE - 1 - card % SUIT_SIZE;
      name_of[card][0] = suit_names[SuitOf(card)];
      name_of[card][1] = rank_names[RankOf(card)];
      name_of[card][2] = '\0';
    }
    for (int suit = 0; suit < NUM_SUITS; ++suit)
      for (int rank = 0; rank < NUM_RANKS; ++rank)
        card_of[suit][rank] = suit * SUIT_SIZE + SUIT_SIZE - 1 - rank;
  }
} card_initializer;

int NextSeat(int seat, int count = 1) { return (seat + count) % NUM_SEATS; }
bool IsNS(int seat) { return seat % 2; }

bool WinOver(int c1, int c2, int trump) {
  return (SuitOf(c1) == SuitOf(c2) && RankOf(c1) > RankOf(c2)) ||
         (SuitOf(c1) != SuitOf(c2) && SuitOf(c1) == trump);
}

struct Options {
  int  alpha;
  int  beta;
  bool use_cache;
  bool use_test_driver;
  int  small_card;
  int  displaying_depth;
  bool discard_suit_bottom;

  Options()
    : alpha(0), beta(TOTAL_TRICKS), use_cache(false), use_test_driver(false),
      small_card(TWO), displaying_depth(4), discard_suit_bottom(false) {}
} options;

template <class T> int BitSize(T v) { return sizeof(v) * 8; }

}  // namespace

class Cards {
  public:
    Cards() : bits(0) {}
    Cards(uint64_t b) : bits(b) {}
    uint64_t Value() const { return bits; }

    int  Size() const { return __builtin_popcountll(bits); }
    bool Empty() const { return bits == 0; }
    bool Have(int card) const { return bits & Bit(card); }
    bool operator ==(const Cards& c) const { return bits == c.bits; }

    Cards Slice(int begin, int end) const { return bits & (Bit(end) - Bit(begin)); }
    Cards Suit(int suit) const { return Slice(suit * SUIT_SIZE, (suit + 1) * SUIT_SIZE); }
    Cards Bottom() const { return  Cards().Add(BitSize(bits) - 1 - __builtin_clzll(bits)); }

    Cards Add(int card) { bits |= Bit(card); return bits; }
    Cards Remove(int card) { bits &= ~Bit(card); return bits; }

    Cards Add(const Cards& c) { bits |= c.bits; return bits; }
    Cards Remove(const Cards& c) { bits &= ~c.bits; return bits; }

    void Show() const {
      for (int card : *this)
        printf("%s ", NameOf(card));
      printf("\n");
    }

    class Iterator {
      public:
        Iterator() : pos(TOTAL_CARDS) {}
        Iterator(uint64_t b) : bits(b | Bit(TOTAL_CARDS)), pos(__builtin_ctzll(bits)) {}
        void operator ++() { pos = __builtin_ctzll(bits &= ~Bit(pos)); }
        bool operator !=(const Iterator& it) const { return pos != *it; }
        int operator *() const { return pos; }

      private:
        uint64_t bits;
        int pos;
    };

    Iterator begin() const { return Iterator(bits); }
    Iterator end() const { return Iterator(); }

  private:
    static uint64_t Bit(int index) { return uint64_t(1) << index; }

    uint64_t bits;
};

struct Bound {
  int lower;
  int upper;
};

class Cache {
  public:
    Cache() : lookup_count(0), hit_count(0), update_count(0), collision_count(0) {
      for (int i = 0; i < size; ++i)
        entries_[i].hands[0].key = 0;

      srand(1);
      for (int i = 0; i < NUM_SEATS; ++i)
        hash_rand[i] = GenerateHashRandom();
    }

    ~Cache() {
      int loaded_count = 0;
      for (int i = 0; i < size; ++i)
        loaded_count += (entries_[i].hands[0].key != 0);

      puts("\n--- Cache Statistics ---");
      printf("lookups: %8d   hits:     %8d (%5.2f%%)\n",
             lookup_count, hit_count, hit_count * 100.0 / lookup_count);
      printf("updates: %8d   collisions: %6d (%5.2f%%)\n",
             update_count, collision_count, collision_count * 100.0 / update_count);
      printf("entries: %8d   loaded:   %8d (%5.2f%%)\n",
             size, loaded_count, loaded_count * 100.0 / size);
    }

    struct Entry;
    bool Lookup(const Cards hands[NUM_SEATS], int lead_seat, Bound* bound) const {
      ++lookup_count;

      Entry input;
      ComputeKeys(hands, &input);

      uint64_t hash = Hash(input);
      for (int d = 0; d < probe_distance; ++d) {
        const Entry& entry = entries_[(hash + d) & (size - 1)];
        if (SameKeys(entry, input)) {
          ++hit_count;
          bound->lower = entry.hands[lead_seat].lower;
          bound->upper = entry.hands[lead_seat].upper;
          return true;
        }
      }
      return false;
    }

    void Update(const Cards hands[NUM_SEATS], int lead_seat, const Bound& bound) {
      ++update_count;

      Entry input;
      ComputeKeys(hands, &input);
      input.hands[lead_seat].lower = bound.lower;
      input.hands[lead_seat].upper = bound.upper;

      // Linear probing benefits from hardware prefetch and thus is faster
      // than collision resolution with multiple hash functions.
      uint64_t hash = Hash(input);
      for (int d = 0; d < probe_distance; ++d) {
        Entry& entry = entries_[(hash + d) & (size - 1)];
        bool same_keys = SameKeys(entry, input);
        bool collided = entry.hands[0].key != 0 && !same_keys;
        if (!collided || d == probe_distance - 1) {
          collision_count += collided;
          if (!same_keys) {
            for (int i = 0; i < NUM_SEATS; ++i) {
              entry.hands[i].key = input.hands[i].key;
              entry.hands[i].lower = 0;
              entry.hands[i].upper = TOTAL_TRICKS;
            }
          }
          entry.hands[lead_seat].lower = bound.lower;
          entry.hands[lead_seat].upper = bound.upper;
          return;
        }
      }
    }

  private:
    void ComputeKeys(const Cards hands[NUM_SEATS], Entry* entry) const {
      for (int i = 0; i < NUM_SEATS; ++i)
        entry->hands[i].key = hands[i].Value();
    }

    bool SameKeys(const Entry& entry, const Entry& input) const {
      for (int i = 0; i < NUM_SEATS; ++i)
        if (entry.hands[i].key != input.hands[i].key)
          return false;
      return true;
    }

    __uint128_t GenerateHashRandom() {
      static const int BITS_PER_RAND = 32;
      __uint128_t r = 0;
      for (int i = 0; i < BitSize(r) / BITS_PER_RAND; ++i)
        r = (r << BITS_PER_RAND) + rand();
      return r | 1;
    }

    uint64_t Hash(const Entry& entry) const {
      __uint128_t sum = 0;
      for (int i = 0; i < NUM_SEATS; ++i)
        sum += entry.hands[i].key * hash_rand[i];
      return sum >> (BitSize(sum) - bits);
    }

    __uint128_t hash_rand[NUM_SEATS];

    static const int probe_distance = 16;
    static const int bits = 22;
    static const int size = 1 << bits;

    struct Hand {
      uint64_t key : TOTAL_CARDS;
      int64_t lower : 5;  // lowerbound when this hand is on lead
      int64_t upper : 5;  // upperbound when this hand is on lead
    };

    struct Entry {
      Hand hands[NUM_SEATS];
    } entries_[size];

    mutable int lookup_count;
    mutable int hit_count;
    mutable int update_count;
    mutable int collision_count;
};

Cache cache;

struct Trick {
  int lead_seat;
  int winning_seat;
  int cards[NUM_SEATS];

  int LeadSuit() const { return SuitOf(cards[lead_seat]); }
  int WinningCard() const { return cards[winning_seat]; }
  bool OnLead(int seat_to_play) const { return seat_to_play == lead_seat; }
  bool CompleteAfter(int seat) const { return NextSeat(seat) == lead_seat; }

  void Show() const {
    printf(" %c %s %s %s %s",
           SeatLetter(lead_seat), NameOf(cards[lead_seat]),
           NameOf(cards[NextSeat(lead_seat, 1)]),
           NameOf(cards[NextSeat(lead_seat, 2)]),
           NameOf(cards[NextSeat(lead_seat, 3)]));
  }
  void Show(int num_cards) const {
    if (num_cards == 4) {
      Show();
      return;
    }
    char output[64] = { ' ', SeatLetter(lead_seat), ' ' };
    int pos = 3;
    for (int seat = lead_seat; num_cards > 0; seat = NextSeat(seat), --num_cards) {
      strcpy(output + pos, NameOf(cards[seat]));
      output[pos + 2] = ' ';
      pos += 3;
    }
    if (pos == 3) pos = 0;
    output[pos] = '\0';
    printf("%s", output);
  }
};

class Node {
  public:
    Node(Cards h[NUM_SEATS], int t, int seat_to_play);
    int MinMax(int alpha, int beta, int seat_to_play, int depth);
    int MinMaxWithMemory(int alpha, int beta, int seat_to_play, int depth);

  private:
    void ShowTricks(int alpha, int beta, int seat_to_play, int depth, int ns_tricks) const;
    int CollectLastTrick(int seat_to_play);
    Cards CombineEquivalentCards(Cards cards) const;
    Cards GetPlayableCards(int seat_to_play) const;
    void GetPattern(const Cards hands[NUM_SEATS], Cards pattern_hands[NUM_SEATS]);

    struct State {
      int ns_tricks_won;
      int winning_seat;
      Cards all_cards;
      bool trick_completed;
    };

    State SaveState() const;
    int Play(int seat_to_play, int card_to_play, State *state);
    void Unplay(int seat_to_play, int card_to_play, const State& state);

    int    trump;
    int    ns_tricks_won;
    Cards  hands[NUM_SEATS];
    Cards  all_cards;
    Trick  tricks[TOTAL_TRICKS];
    Trick* current_trick;
};

Node::Node(Cards h[NUM_SEATS], int t, int seat_to_play)
  : trump(t), ns_tricks_won(0), current_trick(tricks) {
  for (int seat = 0; seat < NUM_SEATS; ++seat) {
    hands[seat] = h[seat];
    all_cards.Add(hands[seat]);
  }
  current_trick->lead_seat = seat_to_play;
}

void Node::ShowTricks(int alpha, int beta, int seat_to_play, int depth, int ns_tricks) const {
  ++depth;
  printf("%2d:", depth);
  for (const Trick* trick = tricks; trick <= current_trick; ++trick) {
    trick->Show(depth > 4 ? 4 : depth);
    depth -= 4;
  }
  printf(" -> %d (%d %d)\n", ns_tricks, alpha, beta);
}

Cards Node::CombineEquivalentCards(Cards cards) const {
  if (cards.Size() <= 1)
    return cards;

  // Two cards in one suit are equivalent when their relative ranks are next to
  // each other. Only need to keep one of them.
  Cards redundant_cards;
  int prev_card = *cards.begin();
  for (int cur_card : cards.Slice(prev_card + 1, TOTAL_CARDS)) {
    if (cards.Slice(prev_card, cur_card + 1) == all_cards.Slice(prev_card, cur_card + 1) ||
        (RankOf(prev_card) <= options.small_card && RankOf(cur_card) <= options.small_card)) {
      redundant_cards.Add(cur_card);
    } else {
      prev_card = cur_card;
    }
  }
  return cards.Remove(redundant_cards);
}

Cards Node::GetPlayableCards(int seat_to_play) const {
  const Cards& hand = hands[seat_to_play];
  if (!current_trick->OnLead(seat_to_play)) {
    Cards suit_cards = hand.Suit(current_trick->LeadSuit());
    if (!suit_cards.Empty())
      return CombineEquivalentCards(suit_cards);
  }
  Cards playable_cards;
  for (int suit = 0; suit < NUM_SUITS; ++suit) {
    Cards suit_cards = hand.Suit(suit);
    if (suit_cards.Empty()) continue;
    if (suit == trump || current_trick->OnLead(seat_to_play) ||
        !options.discard_suit_bottom) {
      playable_cards.Add(CombineEquivalentCards(suit_cards));
    } else {
      // Discard only the bottom card in a suit. It's very rare that discarding
      // a higher ranked card in the same suit is necessary. One example,
      // South to make 3NT:
      //                 AK83 AK A65432 K
      //  65 QJT876 KT9 AJ               JT92 54 Q 765432
      //                 Q74 932 J87 QT98
      playable_cards.Add(suit_cards.Bottom());
    }
  }
  return playable_cards;
}

void Node::GetPattern(const Cards hands[NUM_SEATS], Cards pattern_hands[NUM_SEATS]) {
  // Create the pattern using relative ranks. For example, when all the cards
  // remaining in a suit are J, 8, 7 and 2, they are treated as A, K, Q and J
  // respectively.
  Cards all_cards;
  int relative_ranks[NUM_SUITS];
  int card_holder[TOTAL_CARDS];
  for (int seat = 0; seat < NUM_SEATS; ++seat) {
    relative_ranks[seat] = ACE;
    Cards hand = hands[seat];
    all_cards.Add(hand);
    for (int card : hand)
      card_holder[card] = seat;
  }

  for (int suit = 0; suit < NUM_SUITS; ++suit) {
    Cards suit_cards = all_cards.Suit(suit);
    for (int card : suit_cards) {
      pattern_hands[card_holder[card]].Add(CardOf(suit, relative_ranks[suit]));
      --relative_ranks[suit];
    }
  }
}

int Node::CollectLastTrick(int seat_to_play) {
  current_trick->cards[seat_to_play] = *hands[seat_to_play].begin();
  current_trick->winning_seat = seat_to_play;
  for (int i = 1; i < NUM_SEATS; ++i) {
    seat_to_play = NextSeat(seat_to_play);
    int card_to_play = *hands[seat_to_play].begin();
    current_trick->cards[seat_to_play] = card_to_play;
    if (WinOver(card_to_play, current_trick->WinningCard(), trump)) {
      current_trick->winning_seat = seat_to_play;
    }
  }
  return ns_tricks_won += IsNS(current_trick->winning_seat);
}

Node::State Node::SaveState() const {
  State state;
  state.ns_tricks_won = ns_tricks_won;
  state.winning_seat = current_trick->winning_seat;
  state.all_cards = all_cards;
  return state;
}

int Node::Play(int seat_to_play, int card_to_play, State *state) {
  current_trick->cards[seat_to_play] = card_to_play;

  // who's winning?
  if (current_trick->OnLead(seat_to_play) ||
      WinOver(card_to_play, current_trick->WinningCard(), trump)) {
    current_trick->winning_seat = seat_to_play;
  }

  // who won?
  int next_seat_to_play;
  state->trick_completed = current_trick->CompleteAfter(seat_to_play);
  if (state->trick_completed) {
    (current_trick + 1)->lead_seat = current_trick->winning_seat;
    ns_tricks_won += IsNS(current_trick->winning_seat);
    // remove cards from global suits
    for (int seat = 0; seat < NUM_SEATS; ++seat)
      all_cards.Remove(current_trick->cards[seat]);
    ++current_trick;
    next_seat_to_play = current_trick->lead_seat;
  } else {
    next_seat_to_play = NextSeat(seat_to_play);
  }

  // remove played card from hand
  hands[seat_to_play].Remove(card_to_play);
  return next_seat_to_play;
}

void Node::Unplay(int seat_to_play, int card_to_play, const State& state) {
  // add played card back to hand
  hands[seat_to_play].Add(card_to_play);

  if (state.trick_completed) {
    --current_trick;
    // add cards back to global suits
    all_cards = state.all_cards;
    ns_tricks_won = state.ns_tricks_won;
  }

  // restore state
  current_trick->winning_seat = state.winning_seat;
}

int Node::MinMax(int alpha, int beta, int seat_to_play, int depth) {
  if (all_cards.Size() == 4) {
    int ns_tricks = CollectLastTrick(seat_to_play);
    if (depth < options.displaying_depth)
      ShowTricks(alpha, beta, seat_to_play, depth + 3, ns_tricks);
    return ns_tricks;
  }

  State state = SaveState();
  Cards playable_cards = GetPlayableCards(seat_to_play);
  // TODO: reorder playable cards
  int max_ns_tricks = 0;
  int min_ns_tricks = TOTAL_TRICKS;
  for (int card_to_play : playable_cards) {
    int next_seat_to_play = Play(seat_to_play, card_to_play, &state);
    int ns_tricks = MinMaxWithMemory(alpha, beta, next_seat_to_play, depth + 1);
    Unplay(seat_to_play, card_to_play, state);
    if (depth < options.displaying_depth)
      ShowTricks(alpha, beta, seat_to_play, depth, ns_tricks);

    if (IsNS(seat_to_play)) {
      max_ns_tricks = std::max(max_ns_tricks, ns_tricks);
      if (max_ns_tricks >= beta)
        break;
      alpha = std::max(alpha, ns_tricks);
    } else {
      min_ns_tricks = std::min(min_ns_tricks, ns_tricks);
      if (min_ns_tricks <= alpha)
        break;
      beta = std::min(beta, ns_tricks);
    }
  }
  return IsNS(seat_to_play) ? max_ns_tricks : min_ns_tricks;
}

int Node::MinMaxWithMemory(int alpha, int beta, int seat_to_play, int depth) {
  if (!options.use_cache || (!current_trick->OnLead(seat_to_play) && depth >= 4) ||
      all_cards.Size() == 4)
    return MinMax(alpha, beta, seat_to_play, depth);

  Cards pattern_hands[4];
  GetPattern(hands, pattern_hands);

  Bound bound;
  bound.lower = 0;
  bound.upper = TOTAL_TRICKS;

  if (cache.Lookup(pattern_hands, seat_to_play, &bound)) {
    bound.lower += ns_tricks_won;
    bound.upper += ns_tricks_won;
    if (bound.lower >= beta) {
      if (depth <= options.displaying_depth)
        printf("%2d: %c beta cut %d\n", depth, SeatLetter(seat_to_play), bound.lower);
      return bound.lower;
    }
    if (bound.upper <= alpha) {
      if (depth <= options.displaying_depth)
        printf("%2d: %c alpha cut %d\n", depth, SeatLetter(seat_to_play), bound.upper);
      return bound.upper;
    }
    alpha = std::max(alpha, bound.lower);
    beta = std::min(beta, bound.upper);
  }

  int ns_tricks = MinMax(alpha, beta, seat_to_play, depth);
  if (ns_tricks <= alpha)
    bound.upper = ns_tricks;
  else if (ns_tricks < beta)
    bound.upper = bound.lower = ns_tricks;
  else
    bound.lower = ns_tricks;
  bound.lower -= ns_tricks_won;
  bound.upper -= ns_tricks_won;
  cache.Update(pattern_hands, seat_to_play, bound);
  return ns_tricks;
}

namespace {

int CharToSuit(char c) {
  switch (toupper(c)) {
    case 'S': return SPADE;
    case 'H': return HEART;
    case 'D': return DIAMOND;
    case 'C': return CLUB;
    case 'N': return NOTRUMP;
    default:
              printf("Unknown suit: %c\n", c);
              exit(-1);
  }
}

int CharToRank(char c) {
  switch (toupper(c)) {
    case 'A': return ACE;
    case 'K': return KING;
    case 'Q': return QUEEN;
    case 'J': return JACK;
    case '1':
    case 'T': return TEN;
    case '9': return NINE;
    case '8': return EIGHT;
    case '7': return SEVEN;
    case '6': return SIX;
    case '5': return FIVE;
    case '4': return FOUR;
    case '3': return THREE;
    case '2': return TWO;
    default:
              printf("Unknown rank: %c\n", c);
              exit(-1);
  }
}

int CharToSeat(char c) {
  switch (toupper(c)) {
    case 'W': return WEST;
    case 'N': return NORTH;
    case 'E': return EAST;
    case 'S': return SOUTH;
    default:
              printf("Unknown seat: %c\n", c);
              exit(-1);
  }
}

Cards ParseHand(char *line) {
  static Cards all_cards;
  Cards hand;
  for (int suit = 0; suit < NUM_SUITS; ++suit) {
    while (line[0] && isspace(line[0])) ++line;
    while (line[0] && !isspace(line[0]) && line[0] != '-') {
      int rank = CharToRank(line[0]);
      int card = CardOf(suit, rank);
      if (all_cards.Have(card)) {
        printf("%s showed up twice.\n", NameOf(card));
        exit(-1);
      }
      all_cards.Add(card);
      hand.Add(card);

      if (rank == TEN && line[0] == '1') {
        if (line[1] != '0') {
          printf("Unknown rank: %c%c\n", line[0], line[1]);
          exit(-1);
        }
        ++line;
      }
      ++line;
    }
    if (line[0] == '-')
      ++line;
  }
  return hand;
}

int MemoryEnhancedTestDriver(Cards hands[], int trump, int seat_to_play,
                             int num_tricks) {
  int upperbound = num_tricks;
  int lowerbound = 0;
  int ns_tricks = num_tricks;
  while (lowerbound < upperbound) {
    Node node(hands, trump, seat_to_play);
    int beta = (ns_tricks == lowerbound ? ns_tricks + 1 : ns_tricks);
    ns_tricks = node.MinMaxWithMemory(beta - 1, beta, seat_to_play, 0);
    if (ns_tricks < beta)
      upperbound = ns_tricks;
    else
      lowerbound = ns_tricks;
    printf("lowerbound: %d\tupperbound: %d\n", lowerbound, upperbound);
  }
  return ns_tricks;
}

}  // namespace

int main(int argc, char* argv[]) {
  int c;
  while ((c = getopt(argc, argv, "a:b:cd:s:tD")) != -1) {
    switch (c) {
      case 'a': options.alpha = atoi(optarg); break;
      case 'b': options.beta = atoi(optarg); break;
      case 'c': options.use_cache = true; break;
      case 'd': options.displaying_depth = atoi(optarg); break;
      case 's': options.small_card = CharToRank(optarg[0]); break;
      case 't': options.use_test_driver = true; break;
      case 'D': options.discard_suit_bottom = true; break;
    }
  }

  // read hands
  char line[NUM_SEATS][80];
  CHECK(fgets(line[NORTH], sizeof(line[NORTH]), stdin));
  CHECK(fgets(line[WEST],  sizeof(line[WEST]),  stdin));
  CHECK(fgets(line[EAST],  sizeof(line[EAST]),  stdin));
  CHECK(fgets(line[SOUTH], sizeof(line[SOUTH]), stdin));

  Cards hands[NUM_SEATS];
  int num_tricks = 0;
  for (int seat = 0; seat < NUM_SEATS; ++seat) {
    hands[seat] = ParseHand(line[seat]);
    if (seat == 0)
      num_tricks = hands[seat].Size();
    else
      if (num_tricks != hands[seat].Size()) {
        printf("%s has %d cards, while %s has %d.\n",
               SeatName(seat), hands[seat].Size(), SeatName(0), num_tricks);
        exit(-1);
      }
  }

  CHECK(fgets(line[0], sizeof(line[0]), stdin));
  int trump = CharToSuit(line[0][0]);
  CHECK(fgets(line[0], sizeof(line[0]), stdin));
  int seat_to_play = CharToSeat(line[0][0]);

  struct timeval start;
  gettimeofday(&start, NULL);
  printf("Solving ...\n");
  int ns_tricks;
  int alpha = options.alpha;
  int beta = std::min(options.beta, num_tricks);
  if (options.use_test_driver) {
    ns_tricks = MemoryEnhancedTestDriver(hands, trump, seat_to_play, beta);
  } else {
    Node node(hands, trump, seat_to_play);
    ns_tricks = node.MinMaxWithMemory(alpha, beta, seat_to_play, 0);
  }
  printf("NS tricks: %d\tEW tricks: %d\n", ns_tricks, num_tricks - ns_tricks);
  struct timeval finish;
  gettimeofday(&finish, NULL);
  printf("time: %.3fs\n", (finish.tv_sec - start.tv_sec) +
         (double(finish.tv_usec) - start.tv_usec) * 1e-6);
  return 0;
}
