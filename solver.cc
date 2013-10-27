#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <algorithm>

#define CHECK(statement)  if (!(statement)) printf("CHECK("#statement") failed")

namespace {

const char *seat_name[] = { "West", "North", "East", "South" };

enum { SPADE, HEART, DIAMOND, CLUB, NUM_SUITS, NOTRUMP = NUM_SUITS };
enum { TWO, THREE, FOUR, FIVE, SIX, SEVEN, EIGHT, NINE, TEN,
       JACK, QUEEN, KING, ACE, NUM_RANKS };
enum { WEST, NORTH, EAST, SOUTH, NUM_SEATS };

int SuitOf(int card) { return card / 13; }
int RankOf(int card) { return 12 - card % 13; }
int CardFromSuitRank(int suit, int rank) { return suit * 13 + 12 - rank; }
int NextSeatToPlay(int seat_to_play) { return (seat_to_play + 1) % NUM_SEATS; }
bool IsNS(int seat) { return seat % 2; }

bool WinOver(int c1, int c2, int trump) {
  return (SuitOf(c1) == SuitOf(c2) && RankOf(c1) > RankOf(c2)) ||
         (SuitOf(c1) != SuitOf(c2) && SuitOf(c1) == trump);
}

const char* CardName(int card) {
  static const char suit_name[] = "SHDC";
  static const char rank_name[] = "23456789TJQKA";
  static char name[3];
  name[0] = suit_name[SuitOf(card)];
  name[1] = rank_name[RankOf(card)];
  name[2] = '\0';
  return name;
}

struct Options {
  bool use_cache;
  bool use_test_driver;
  int  small_card;

  Options() : use_cache(false), use_test_driver(false), small_card(TWO) {}
} options;

}  // namespace

class Cards {
  public:
    Cards() : cards_(0) {}
    Cards(uint64_t cards) : cards_(cards) {}
    uint64_t Value() const { return cards_; }

    int  Size() const { return __builtin_popcountll(cards_); }
    bool Empty() const { return cards_ == 0; }
    bool Has(int card) { return cards_ & Bit(card); }
    bool operator ==(const Cards& c) const { return cards_ == c.cards_; }

    Cards Slice(int begin, int end) const { return cards_ & (Bit(end) - Bit(begin)); }
    Cards GetSuit(int suit) const { return Slice(suit * 13, suit * 13 + 13); }

    int Begin() const { return After(-1); }
    int After(int card) const { return __builtin_ctzll(Slice(card + 1, End()).Add(End()).cards_); }
    int End() const { return 52; }

    Cards Add(int card) { cards_ |= Bit(card); return cards_; }
    Cards Remove(int card) { cards_ &= ~Bit(card); return cards_; }

    Cards Add(const Cards& c) { cards_ |= c.cards_; return cards_; }
    Cards Remove(const Cards& c) { cards_ &= ~c.cards_; return cards_; }

    void Show() {
      for (int card = Begin(); card != End(); card = After(card))
        printf("%s ", CardName(card));
      printf("\n");
    }

  private:
    uint64_t Bit(int index) const { return uint64_t(1) << index; }

    uint64_t cards_;
};

struct Bound {
  int lower;
  int upper;
};

class Cache {
  public:
    Cache() : lookup_count(0), hit_count(0), update_count(0), collision_count(0) {
      for (int i = 0; i < size; ++i)
        entries_[i].key = 0;

      srand(1);
      for (int r = 0; r < hash_rounds; ++r) {
        hash_rand[r] = 0;
        for (int i = 0; i < 4; ++i)
          hash_rand[r] = (hash_rand[r] << 32) + rand();
        hash_rand[r] |= 1;
      }
    }

    ~Cache() {
      int unused_count = 0;
      for (int i = 0; i < size; ++i)
        unused_count += (entries_[i].key == 0);

      printf("lookups: %d  hits: %d (%.2f%%)\n",
             lookup_count, hit_count, hit_count * 100.0 / lookup_count);
      printf("updates: %d  collisions: %d (%.2f%%)\n",
             update_count, collision_count, collision_count * 100.0 / update_count);
      printf("entries: %d  unused: %d (%.2f%%)\n",
             size, unused_count, unused_count * 100.0 / size);
    }

    bool Lookup(Cards cards, int lead_seat, Bound* bound) const {
      ++lookup_count;
      uint64_t key = cards.Value() * 4 + lead_seat;
      for (int r = 0; r < hash_rounds; ++r) {
        uint64_t hash = Hash(key, r);
        const Entry& entry = entries_[hash];
        if (entry.key == key) {
          ++hit_count;
          bound->lower = entry.lower;
          bound->upper = entry.upper;
          return true;
        }
      }
      return false;
    }

    void Update(Cards cards, int lead_seat, const Bound& bound) {
      ++update_count;
      uint64_t key = cards.Value() * 4 + lead_seat;
      for (int r = 0; r < hash_rounds; ++r) {
        uint64_t hash = Hash(key, r);
        Entry& entry = entries_[hash];
        bool collision_free = (entry.key == 0 || entry.key == key);
        if (collision_free || r == hash_rounds - 1) {
          collision_count += !collision_free;
          entry.key = key;
          entry.lower = bound.lower;
          entry.upper = bound.upper;
          return;
        }
      }
    }

  private:
    uint64_t Hash(uint64_t value, int r) const {
      return (value * hash_rand[r]) >> (128 - bits);
    }
    static const int hash_rounds = 2;
    __uint128_t hash_rand[hash_rounds];

    static const int bits = 24;
    static const int size = 1 << bits;
    struct Entry {
      uint64_t key : 54;
      int64_t lower : 5;
      int64_t upper : 5;
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
  bool CompleteAfter(int seat) const { return NextSeatToPlay(seat) == lead_seat; }
};

class Node {
  public:
    Node(Cards h[NUM_SEATS], int t, int seat_to_play);
    int MinMax(int alpha, int beta, int seat_to_play, int depth);
    int MinMaxWithMemory(int alpha, int beta, int seat_to_play, int depth);

  private:
    int CollectLastTrick(int seat_to_play);
    Cards RemoveRedundantCards(Cards cards) const;
    Cards GetPlayableCards(int seat_to_play) const;

    struct State {
      int ns_tricks;
      int winning_seat;
      Cards all_cards;
      bool trick_completed;
    };

    State SaveState() const;
    int Play(int seat_to_play, int card_to_play, State *state);
    void Unplay(int seat_to_play, int card_to_play, const State& state);

    int    trump;
    int    ns_tricks;
    Cards  hands[NUM_SEATS];
    Cards  all_cards;
    Trick  tricks[13];
    Trick* current_trick;
};

Node::Node(Cards h[NUM_SEATS], int t, int seat_to_play)
  : trump(t), ns_tricks(0), current_trick(tricks) {
  for (int seat = 0; seat < NUM_SEATS; ++seat) {
    hands[seat] = h[seat];
    all_cards.Add(hands[seat]);
  }
  current_trick->lead_seat = seat_to_play;
}

Cards Node::RemoveRedundantCards(Cards cards) const {
  if (cards.Size() <= 1)
    return cards;

  Cards redundant_cards;
  int prev_card = cards.Begin();
  int cur_card = cards.After(prev_card);
  while (cur_card != cards.End()) {
    if (cards.Slice(prev_card, cur_card + 1) == all_cards.Slice(prev_card, cur_card + 1) ||
        (RankOf(prev_card) <= options.small_card && RankOf(cur_card) <= options.small_card)) {
      redundant_cards.Add(cur_card);
    } else {
      prev_card = cur_card;
    }
    cur_card = cards.After(cur_card);
  }
  return cards.Remove(redundant_cards);
}

Cards Node::GetPlayableCards(int seat_to_play) const {
  const Cards& hand = hands[seat_to_play];
  if (!current_trick->OnLead(seat_to_play)) {
    Cards suit = hand.GetSuit(current_trick->LeadSuit());
    if (!suit.Empty())
      return RemoveRedundantCards(suit);
  }
  Cards cards;
  for (int i = 0; i < NUM_SUITS; ++i)
    cards.Add(RemoveRedundantCards(hand.GetSuit(i)));
  return cards;
}

int Node::CollectLastTrick(int seat_to_play) {
  current_trick->cards[seat_to_play] = hands[seat_to_play].Begin();
  current_trick->winning_seat = seat_to_play;
  for (int i = 1; i < NUM_SEATS; i++) {
    seat_to_play = NextSeatToPlay(seat_to_play);
    int card_to_play = hands[seat_to_play].Begin();
    current_trick->cards[seat_to_play] = card_to_play;
    if (WinOver(card_to_play, current_trick->WinningCard(), trump)) {
      current_trick->winning_seat = seat_to_play;
    }
  }
  return ns_tricks + IsNS(current_trick->winning_seat);
}

Node::State Node::SaveState() const {
  State state;
  state.ns_tricks = ns_tricks;
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
    ns_tricks += IsNS(current_trick->winning_seat);
    // remove cards from global suits
    for (int i = 0; i < NUM_SEATS; ++i)
      all_cards.Remove(current_trick->cards[i]);
    ++current_trick;
    next_seat_to_play = current_trick->lead_seat;
  } else {
    next_seat_to_play = NextSeatToPlay(seat_to_play);
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
    ns_tricks = state.ns_tricks;
  }

  // restore state
  current_trick->winning_seat = state.winning_seat;
}

int Node::MinMax(int alpha, int beta, int seat_to_play, int depth) {
  if (hands[seat_to_play].Size() == 1 && current_trick->OnLead(seat_to_play))
    return CollectLastTrick(seat_to_play);

  State state = SaveState();
  Cards playable_cards = GetPlayableCards(seat_to_play);
  // TODO: reorder playable cards
  int max_tricks = 0;
  int min_tricks = 13;
  for (int card_to_play = playable_cards.Begin(); card_to_play != playable_cards.End();
       card_to_play = playable_cards.After(card_to_play)) {

    int next_seat_to_play = Play(seat_to_play, card_to_play, &state);
    int num_tricks = MinMaxWithMemory(alpha, beta, next_seat_to_play, depth + 1);
    Unplay(seat_to_play, card_to_play, state);
    if (depth < 4)
      printf("%*s => %d (%d, %d)\n",
             depth * 2 + 2, CardName(card_to_play), num_tricks, alpha, beta);

    if (IsNS(seat_to_play)) {
      max_tricks = std::max(max_tricks, num_tricks);
      if (max_tricks >= beta)
        break;
      alpha = std::max(alpha, num_tricks);
    } else {
      min_tricks = std::min(min_tricks, num_tricks);
      if (min_tricks <= alpha)
        break;
      beta = std::min(beta, num_tricks);
    }
  }
  return IsNS(seat_to_play) ? max_tricks : min_tricks;
}

int Node::MinMaxWithMemory(int alpha, int beta, int seat_to_play, int depth) {
  if (!options.use_cache || !current_trick->OnLead(seat_to_play))
    return MinMax(alpha, beta, seat_to_play, depth);

  Cards remaining_cards;
  for (int i = 0; i < NUM_SEATS; ++i)
    remaining_cards.Add(hands[i]);

  Bound bound;
  bound.lower = 0;
  bound.upper = 13;

  if (cache.Lookup(remaining_cards, seat_to_play, &bound)) {
    bound.lower += ns_tricks;
    bound.upper += ns_tricks;
    if (bound.lower >= beta)
      return bound.lower;
    if (bound.upper <= alpha)
      return bound.upper;
    alpha = std::max(alpha, bound.lower);
    beta = std::min(beta, bound.upper);
  }

  int g = MinMax(alpha, beta, seat_to_play, depth);
  if (g <= alpha)
    bound.upper = g;
  else if (g < beta)
    bound.upper = bound.lower = g;
  else
    bound.lower = g;
  bound.lower -= ns_tricks;
  bound.upper -= ns_tricks;
  cache.Update(remaining_cards, seat_to_play, bound);
  return g;
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
  for (int suit = 0; suit < NUM_SUITS; suit++) {
    while (line[0] && isspace(line[0])) line++;
    while (line[0] && !isspace(line[0]) && line[0] != '-') {
      int rank = CharToRank(line[0]);
      int card = CardFromSuitRank(suit, rank);
      if (all_cards.Has(card)) {
        printf("%s showed up twice.\n", CardName(card));
        exit(-1);
      }
      all_cards.Add(card);
      hand.Add(card);

      if (rank == TEN && line[0] == '1') {
        if (line[1] != '0') {
          printf("Unknown rank: %c%c\n", line[0], line[1]);
          exit(-1);
        }
        line++;
      }
      line++;
    }
    if (line[0] == '-')
      line++;
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
    printf("lowerbound: %d     upperbound: %d\n", lowerbound, upperbound);
  }
  return ns_tricks;
}

}  // namespace

int main(int argc, char* argv[]) {
  int c;
  while ((c = getopt(argc, argv, "cs:t")) != -1) {
    switch (c) {
      case 'c': options.use_cache = true; break;
      case 's': options.small_card = CharToRank(optarg[0]); break;
      case 't': options.use_test_driver = true; break;
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
  for (int seat = 0; seat < NUM_SEATS; seat ++) {
    hands[seat] = ParseHand(line[seat]);
    if (seat == 0)
      num_tricks = hands[seat].Size();
    else
      if (num_tricks != hands[seat].Size()) {
        printf("%s has %d cards, while %s has %d.\n",
               seat_name[seat], hands[seat].Size(),
               seat_name[0], num_tricks);
        exit(-1);
      }
  }

  CHECK(fgets(line[0], sizeof(line[0]), stdin));
  int trump = CharToSuit(line[0][0]);
  CHECK(fgets(line[0], sizeof(line[0]), stdin));
  int seat_to_play = CharToSeat(line[0][0]);

  printf("Solving ...\n");
  int ns_tricks;
  if (options.use_test_driver) {
    ns_tricks = MemoryEnhancedTestDriver(hands, trump, seat_to_play, num_tricks);
  } else {
    Node node(hands, trump, seat_to_play);
    ns_tricks = node.MinMaxWithMemory(0, num_tricks, seat_to_play, 0);
  }
  printf("NS tricks: %d      EW tricks: %d\n", ns_tricks, num_tricks - ns_tricks);
  return 0;
}
