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
#ifdef _DEBUG
#define DEBUG(statement)  statement
#else
#define DEBUG(statement)
#endif

namespace {

enum { SPADE, HEART, DIAMOND, CLUB, NUM_SUITS, NOTRUMP = NUM_SUITS };
enum { TWO, THREE, FOUR, FIVE, SIX, SEVEN, EIGHT, NINE, TEN,
       JACK, QUEEN, KING, ACE, NUM_RANKS };
enum { WEST, NORTH, EAST, SOUTH, NUM_SEATS };

const int TOTAL_TRICKS = NUM_RANKS;
const int TOTAL_CARDS = NUM_RANKS * NUM_SUITS;

const char* SeatName(int seat) {
  static const char *seat_names[NUM_SEATS] = { "West", "North", "East", "South" };
  return seat_names[seat];
}
char SeatLetter(int seat) { return SeatName(seat)[0]; }

char suit_of[TOTAL_CARDS];
char rank_of[TOTAL_CARDS];
char card_of[NUM_SUITS][16];
uint64_t mask_of[NUM_SUITS];
char name_of[TOTAL_CARDS][4];

int SuitOf(int card) { return suit_of[card]; }
int RankOf(int card) { return rank_of[card]; }
int CardOf(int suit, int rank) { return card_of[suit][rank]; }
uint64_t MaskOf(int suit) { return mask_of[suit]; }
const char* NameOf(int card) { return name_of[card]; }

struct CardInitializer {
  CardInitializer(bool rank_first) {
    static const char suit_names[] = "SHDC";
    static const char rank_names[] = "23456789TJQKA";
    memset(mask_of, 0, sizeof(mask_of));
    for (int card = 0; card < TOTAL_CARDS; ++card) {
      if (rank_first) {
        suit_of[card] = card & (NUM_SUITS - 1);
        rank_of[card] = NUM_RANKS - 1 - card / NUM_SUITS;
      } else {
        suit_of[card] = card / NUM_RANKS;
        rank_of[card] = NUM_RANKS - 1 - card % NUM_RANKS;
      }
      card_of[SuitOf(card)][RankOf(card)] = card;
      mask_of[SuitOf(card)] |= uint64_t(1) << card;
      name_of[card][0] = suit_names[SuitOf(card)];
      name_of[card][1] = rank_names[RankOf(card)];
      name_of[card][2] = '\0';
    }
  }
};

int NextSeat(int seat, int count = 1) { return (seat + count) % NUM_SEATS; }
bool IsNS(int seat) { return seat % 2; }

bool WinOver(int c1, int c2, int trump) {
  return SuitOf(c1) == SuitOf(c2) ? RankOf(c1) > RankOf(c2) : SuitOf(c1) == trump;
}

struct Options {
  int  alpha;
  int  beta;
  bool use_cache;
  bool use_test_driver;
  int  small_card;
  int  displaying_depth;
  bool discard_suit_bottom;
  bool rank_first;

  Options()
    : alpha(0), beta(TOTAL_TRICKS), use_cache(false), use_test_driver(false),
      small_card(TWO), displaying_depth(4), discard_suit_bottom(false),
      rank_first(false) {}
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
    Cards Suit(int suit) const { return bits & MaskOf(suit); }
    int Top() const { return __builtin_ctzll(bits); }
    int Bottom() const { return  BitSize(bits) - 1 - __builtin_clzll(bits); }

    Cards Union(const Cards& c) const { return bits | c.bits; }
    Cards Intersect(const Cards& c) const { return bits & c.bits; }
    Cards Different(const Cards& c) const { return bits & ~c.bits; }

    Cards Add(int card) { bits |= Bit(card); return bits; }
    Cards Remove(int card) { bits &= ~Bit(card); return bits; }

    Cards Add(const Cards& c) { bits |= c.bits; return bits; }
    Cards Remove(const Cards& c) { bits &= ~c.bits; return bits; }

    void Show() const {
      for (int card : *this)
        printf("%s ", NameOf(card));
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

template <class Entry, int input_size, int bits>
class Cache {
  public:
    Cache(const char* name)
      : cache_name(name), lookup_count(0), hit_count(0), update_count(0), collision_count(0) {
      for (int i = 0; i < size; ++i)
        entries_[i].hash = 0;

      srand(1);
      for (int i = 0; i < input_size; ++i)
        hash_rand[i] = GenerateHashRandom();
    }

    ~Cache() {
      int loaded_count = 0;
      for (int i = 0; i < size; ++i)
        loaded_count += (entries_[i].hash != 0);

      printf("--- %s Statistics ---\n", cache_name);
      printf("lookups: %8d   hits:     %8d (%5.2f%%)\n",
             lookup_count, hit_count, hit_count * 100.0 / lookup_count);
      printf("updates: %8d   collisions: %6d (%5.2f%%)\n",
             update_count, collision_count, collision_count * 100.0 / update_count);
      printf("entries: %8d   loaded:   %8d (%5.2f%%)\n",
             size, loaded_count, loaded_count * 100.0 / size);
    }

    const Entry* Lookup(Cards cards[input_size]) const {
      ++lookup_count;

      uint64_t hash = Hash(cards);
      if (hash == 0) return NULL;
      uint64_t index = hash >> (BitSize(hash) - bits);

      for (int d = 0; d < probe_distance; ++d) {
        const Entry& entry = entries_[(index + d) & (size - 1)];
        if (entry.hash == hash) {
          ++hit_count;
          return &entry;
        }
      }
      return NULL;
    }

    Entry* Update(Cards cards[input_size]) {
      ++update_count;

      uint64_t hash = Hash(cards);
      if (hash == 0) return NULL;
      uint64_t index = hash >> (BitSize(hash) - bits);

      // Linear probing benefits from hardware prefetch and thus is faster
      // than collision resolution with multiple hash functions.
      for (int d = 0; d < probe_distance; ++d) {
        Entry& entry = entries_[(index + d) & (size - 1)];
        bool collided = entry.hash != 0 && entry.hash != hash;
        if (!collided || d == probe_distance - 1) {
          collision_count += collided;
          if (entry.hash != hash)
            entry.Reset(hash);
          return &entry;
        }
      }
      return NULL;
    }

  private:
    uint64_t Hash(Cards cards[input_size]) const {
      uint64_t sum = 0;
      for (int i = 0; i < input_size; ++i)
        sum += cards[i].Value() * hash_rand[i];
      return sum;
    }

    uint64_t GenerateHashRandom() {
      static const int BITS_PER_RAND = 32;
      uint64_t r = 0;
      for (int i = 0; i < BitSize(r) / BITS_PER_RAND; ++i)
        r = (r << BITS_PER_RAND) + rand();
      return r | 1;
    }

    static const int probe_distance = 16;
    static const int size = 1 << bits;

    const char* cache_name;
    uint64_t hash_rand[input_size];
    Entry entries_[size];

    mutable int lookup_count;
    mutable int hit_count;
    mutable int update_count;
    mutable int collision_count;
};

#pragma pack(push, 4)
struct BoundsEntry {
  // Save only the hash instead of full hands to save memory.
  // The chance of getting a hash collision is practically zero.
  uint64_t hash;
  // Bounds depending on the lead seat.
  struct {
    uint8_t lower : 4;
    uint8_t upper : 4;
  } bounds[NUM_SEATS];

  void Reset(uint64_t hash_in) {
    hash = hash_in;
    for (int i = 0; i < NUM_SEATS; ++i) {
      bounds[i].lower = 0;
      bounds[i].upper = TOTAL_TRICKS;
    }
  }
};
#pragma pack(pop)

Cache<BoundsEntry, 4, 22> bounds_cache("Bounds Cache");

struct CutoffEntry {
  uint64_t hash;
  // Cut-off card depending on the leading seat and the previous card.
  char card[TOTAL_CARDS + 1][NUM_SEATS][NUM_SEATS];

  void Reset(uint64_t hash_in) {
    hash = hash_in;
    memset(card, TOTAL_CARDS, sizeof(card));
  }
};

Cache<CutoffEntry, 1, 14> cutoff_cache("Cut-off Cache");

struct Trick {
  int lead_seat;
  int winning_seat;
  int cards[NUM_SEATS];
  char equivalence[TOTAL_CARDS];

  int LeadSuit() const { return SuitOf(cards[lead_seat]); }
  int WinningCard() const { return cards[winning_seat]; }
  bool OnLead(int seat_to_play) const { return seat_to_play == lead_seat; }
  bool CompleteAfter(int seat) const { return NextSeat(seat) == lead_seat; }

  void IdentifyEquivalentCards(Cards cards, Cards all_cards) {
    // Two cards in one suit are equivalent when their relative ranks are next to
    // each other.
    int prev_card = *cards.begin();
    equivalence[prev_card] = prev_card;
    for (int cur_card : cards.Slice(prev_card + 1, TOTAL_CARDS)) {
      if (cards.Slice(prev_card, cur_card + 1) == all_cards.Slice(prev_card, cur_card + 1) ||
          std::max(RankOf(prev_card), RankOf(cur_card)) <= options.small_card) {
        // This one is equivalent to the previous one.
      } else {
        prev_card = cur_card;
      }
      equivalence[cur_card] = prev_card;
    }
  }

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

class MinMax {
  public:
    MinMax(Cards h[NUM_SEATS], int t, int seat_to_play);
    int Search(int alpha, int beta, int seat_to_play, int depth);
    int SearchWithCache(int alpha, int beta, int seat_to_play, int depth);

  private:
    void ShowTricks(int alpha, int beta, int seat_to_play, int depth, int ns_tricks) const;
    int CollectLastTrick(int seat_to_play);
    Cards GetPlayableCards(int seat_to_play, const char equivalence[TOTAL_CARDS]) const;
    void GetPattern(const Cards hands[NUM_SEATS], Cards pattern_hands[NUM_SEATS]);
    int FastTricks(int seat_to_play) const;

    struct State {
      int ns_tricks_won;
      int winning_seat;
      Cards all_cards;
    };

    State SaveState() const;
    bool TrickCompletedAt(int depth) const { return depth % 4 == 3; }
    int Play(int seat_to_play, int card_to_play, int depth);
    void Unplay(int seat_to_play, int card_to_play, int depth, const State& state);
    Cards LookupCutoffCards(int seat_to_play);
    void SaveCutoffCard(int seat_to_play, int cutoff_card);
    bool Cutoff(int alpha, int beta, int seat_to_play, int depth, int card_to_play,
                State* state, int* bounded_ns_tricks);

    int    trump;
    int    ns_tricks_won;
    Cards  hands[NUM_SEATS];
    Cards  all_cards;
    Trick  tricks[TOTAL_TRICKS];
    Trick* current_trick;
};

MinMax::MinMax(Cards h[NUM_SEATS], int t, int seat_to_play)
  : trump(t), ns_tricks_won(0), current_trick(tricks) {
  for (int seat = 0; seat < NUM_SEATS; ++seat) {
    hands[seat] = h[seat];
    all_cards.Add(hands[seat]);
  }
  current_trick->lead_seat = seat_to_play;
}

void MinMax::ShowTricks(int alpha, int beta, int seat_to_play, int depth, int ns_tricks) const {
  ++depth;
  printf("%2d:", depth);
  for (const Trick* trick = tricks; trick <= current_trick; ++trick) {
    trick->Show(depth > 4 ? 4 : depth);
    depth -= 4;
  }
  printf(" -> %d (%d %d)\n", ns_tricks, alpha, beta);
}

Cards MinMax::GetPlayableCards(int seat_to_play, const char equivalence[TOTAL_CARDS]) const {
  const Cards& hand = hands[seat_to_play];
  if (current_trick->OnLead(seat_to_play))
    return hand;

  int lead_suit = current_trick->LeadSuit();
  Cards suit_cards = hand.Suit(lead_suit);
  if (!suit_cards.Empty())
    return suit_cards;

  if (!options.discard_suit_bottom)
    return hand;

  Cards playable_cards;
  for (int suit = 0; suit < NUM_SUITS; ++suit) {
    Cards suit_cards = hand.Suit(suit);
    if (suit_cards.Empty()) continue;
    if (suit == trump) {
      playable_cards.Add(suit_cards);
    } else {
      // Discard only the bottom card in a suit. It's very rare that discarding
      // a higher ranked card in the same suit is necessary. One example,
      // South to make 3NT:
      //                 AK83 AK A65432 K
      //  65 QJT876 KT9 AJ               JT92 54 Q 765432
      //                 Q74 932 J87 QT98
      playable_cards.Add(equivalence[suit_cards.Bottom()]);
    }
  }
  return playable_cards;
}

void MinMax::GetPattern(const Cards hands[NUM_SEATS], Cards pattern_hands[NUM_SEATS]) {
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

  for (int card : all_cards) {
    int suit = SuitOf(card);
    pattern_hands[card_holder[card]].Add(CardOf(suit, relative_ranks[suit]));
    --relative_ranks[suit];
  }
}

int MinMax::CollectLastTrick(int seat_to_play) {
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

MinMax::State MinMax::SaveState() const {
  State state;
  state.ns_tricks_won = ns_tricks_won;
  state.winning_seat = current_trick->winning_seat;
  state.all_cards = all_cards;
  return state;
}

int MinMax::Play(int seat_to_play, int card_to_play, int depth) {
  current_trick->cards[seat_to_play] = card_to_play;

  // who's winning?
  if (current_trick->OnLead(seat_to_play) ||
      WinOver(card_to_play, current_trick->WinningCard(), trump)) {
    current_trick->winning_seat = seat_to_play;
  }

  // who won?
  int next_seat_to_play;
  if (TrickCompletedAt(depth)) {
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

void MinMax::Unplay(int seat_to_play, int card_to_play, int depth, const State& state) {
  // add played card back to hand
  hands[seat_to_play].Add(card_to_play);

  if (TrickCompletedAt(depth)) {
    --current_trick;
    // add cards back to global suits
    all_cards = state.all_cards;
    ns_tricks_won = state.ns_tricks_won;
  }

  // restore state
  current_trick->winning_seat = state.winning_seat;
}

inline
Cards MinMax::LookupCutoffCards(int seat_to_play) {
  Cards cutoff_cards;
  if (current_trick->OnLead(seat_to_play)) {
    for (int suit = 0; suit < NUM_SUITS; ++suit) {
      Cards suit_cards = all_cards.Suit(suit);
      if (suit_cards.Empty()) continue;
      const auto* cutoff_entry = cutoff_cache.Lookup(&suit_cards);
      if (cutoff_entry) {
        int cutoff_card = cutoff_entry->card[TOTAL_CARDS][current_trick->lead_seat][seat_to_play];
        if (cutoff_card != TOTAL_CARDS)
           cutoff_cards.Add(cutoff_card);
      }
    }
  } else {
    Cards suit_cards = all_cards.Suit(current_trick->LeadSuit());
    int prev_card = current_trick->cards[NextSeat(seat_to_play, 3)];
    const auto* cutoff_entry = cutoff_cache.Lookup(&suit_cards);
    if (cutoff_entry) {
      int cutoff_card = cutoff_entry->card[prev_card][current_trick->lead_seat][seat_to_play];
      if (cutoff_card != TOTAL_CARDS)
        cutoff_cards.Add(cutoff_card);
    }
  }
  return cutoff_cards;
}

inline
void MinMax::SaveCutoffCard(int seat_to_play, int cutoff_card) {
  Cards suit_cards = all_cards.Suit(current_trick->LeadSuit());
  if (current_trick->OnLead(seat_to_play)) {
    auto* cutoff_entry = cutoff_cache.Update(&suit_cards);
    cutoff_entry->card[TOTAL_CARDS][current_trick->lead_seat][seat_to_play] = cutoff_card;
  } else {
    int prev_card = current_trick->cards[NextSeat(seat_to_play, 3)];
    auto* cutoff_entry = cutoff_cache.Update(&suit_cards);
    cutoff_entry->card[prev_card][current_trick->lead_seat][seat_to_play] = cutoff_card;
  }
}

inline
bool MinMax::Cutoff(int alpha, int beta, int seat_to_play, int depth, int card_to_play,
                  State* state, int* bounded_ns_tricks) {
  int next_seat_to_play = Play(seat_to_play, card_to_play, depth);
  int ns_tricks = SearchWithCache(alpha, beta, next_seat_to_play, depth + 1);
  Unplay(seat_to_play, card_to_play, depth, *state);
  if (depth < options.displaying_depth)
    ShowTricks(alpha, beta, seat_to_play, depth, ns_tricks);

  if (IsNS(seat_to_play)) {
    *bounded_ns_tricks = std::max(*bounded_ns_tricks, ns_tricks);
    if (*bounded_ns_tricks >= beta)
      return true;  // beta cut-off
  } else {
    *bounded_ns_tricks = std::min(*bounded_ns_tricks, ns_tricks);
    if (*bounded_ns_tricks <= alpha)
      return true;  // alpha cut-off
  }
  return false;  // no cut-off
}

int MinMax::Search(int alpha, int beta, int seat_to_play, int depth) {
  if (all_cards.Size() == 4) {
    int ns_tricks = CollectLastTrick(seat_to_play);
    if (depth < options.displaying_depth)
      ShowTricks(alpha, beta, seat_to_play, depth + 3, ns_tricks);
    return ns_tricks;
  }

  if (current_trick->OnLead(seat_to_play))
    for (int seat = 0; seat < NUM_SEATS; ++seat)
      for (int suit = 0; suit < NUM_SUITS; ++suit)
        current_trick->IdentifyEquivalentCards(hands[seat].Suit(suit),
                                               all_cards.Suit(suit));

  State state = SaveState();
  Cards playable_cards = GetPlayableCards(seat_to_play, current_trick->equivalence);
  Cards cutoff_cards = LookupCutoffCards(seat_to_play);
  Cards sets_of_playables[2] = {
    playable_cards.Intersect(cutoff_cards),
    playable_cards.Different(cutoff_cards)
  };
  int bounded_ns_tricks = IsNS(seat_to_play) ? 0 : TOTAL_TRICKS;
  for (Cards playables : sets_of_playables)
    for (int card : playables) {
      if (current_trick->equivalence[card] != card) continue;
      if (Cutoff(alpha, beta, seat_to_play, depth, card, &state, &bounded_ns_tricks)) {
        SaveCutoffCard(seat_to_play, card);
        return bounded_ns_tricks;
      }
    }
  return bounded_ns_tricks;
}

int MinMax::FastTricks(int seat_to_play) const {
  Cards both_hands = hands[seat_to_play].Union(hands[NextSeat(seat_to_play, 2)]);
  int fast_tricks = 0;
  if (trump == NOTRUMP || all_cards.Suit(trump) == both_hands.Suit(trump)) {
    for (int suit = 0; suit < NUM_SUITS; ++suit) {
      Cards my_suit_cards = hands[seat_to_play].Suit(suit);
      for (int card : all_cards.Suit(suit))
        if (my_suit_cards.Have(card))
          ++fast_tricks;
        else
          break;
    }
  }
  return fast_tricks;
}

int MinMax::SearchWithCache(int alpha, int beta, int seat_to_play, int depth) {
  if (current_trick->OnLead(seat_to_play)) {
    int fast_tricks = FastTricks(seat_to_play);
    if (IsNS(seat_to_play) && ns_tricks_won + fast_tricks >= beta)
      return ns_tricks_won + fast_tricks;
    int remaining_tricks = hands[0].Size();
    if (!IsNS(seat_to_play) && ns_tricks_won + (remaining_tricks - fast_tricks) <= alpha)
      return ns_tricks_won + (remaining_tricks - fast_tricks);
  }

  if (!options.use_cache || (!current_trick->OnLead(seat_to_play) && depth >= 4) ||
      all_cards.Size() == 4)
    return Search(alpha, beta, seat_to_play, depth);

  Cards pattern_hands[4];
  GetPattern(hands, pattern_hands);

  struct Bounds {
    int lower;
    int upper;
  } bounds = { 0, TOTAL_TRICKS };

  const auto* bounds_entry = bounds_cache.Lookup(pattern_hands);
  if (bounds_entry) {
    bounds.lower = bounds_entry->bounds[seat_to_play].lower + ns_tricks_won;
    bounds.upper = bounds_entry->bounds[seat_to_play].upper + ns_tricks_won;
    if (bounds.upper > TOTAL_TRICKS) bounds.upper = TOTAL_TRICKS;
    if (bounds.lower >= beta) {
      if (depth <= options.displaying_depth)
        printf("%2d: %c beta cut %d\n", depth, SeatLetter(seat_to_play), bounds.lower);
      return bounds.lower;
    }
    if (bounds.upper <= alpha) {
      if (depth <= options.displaying_depth)
        printf("%2d: %c alpha cut %d\n", depth, SeatLetter(seat_to_play), bounds.upper);
      return bounds.upper;
    }
    alpha = std::max(alpha, bounds.lower);
    beta = std::min(beta, bounds.upper);
  }

  int ns_tricks = Search(alpha, beta, seat_to_play, depth);
  if (ns_tricks <= alpha)
    bounds.upper = ns_tricks;
  else if (ns_tricks < beta)
    bounds.upper = bounds.lower = ns_tricks;
  else
    bounds.lower = ns_tricks;
  bounds.lower -= ns_tricks_won;
  bounds.upper -= ns_tricks_won;
  if (bounds.lower < 0) bounds.lower = 0;
  auto* new_bounds_entry = bounds_cache.Update(pattern_hands);
  new_bounds_entry->bounds[seat_to_play].lower = bounds.lower;
  new_bounds_entry->bounds[seat_to_play].upper = bounds.upper;
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
    MinMax min_max(hands, trump, seat_to_play);
    int beta = (ns_tricks == lowerbound ? ns_tricks + 1 : ns_tricks);
    ns_tricks = min_max.SearchWithCache(beta - 1, beta, seat_to_play, 0);
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
  while ((c = getopt(argc, argv, "a:b:cd:rs:tD")) != -1) {
    switch (c) {
      case 'a': options.alpha = atoi(optarg); break;
      case 'b': options.beta = atoi(optarg); break;
      case 'c': options.use_cache = true; break;
      case 'd': options.displaying_depth = atoi(optarg); break;
      case 'r': options.rank_first = true; break;
      case 's': options.small_card = CharToRank(optarg[0]); break;
      case 't': options.use_test_driver = true; break;
      case 'D': options.discard_suit_bottom = true; break;
    }
  }

  CardInitializer card_initializer(options.rank_first);

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
    MinMax min_max(hands, trump, seat_to_play);
    ns_tricks = min_max.SearchWithCache(alpha, beta, seat_to_play, 0);
  }
  printf("NS tricks: %d\tEW tricks: %d\n", ns_tricks, num_tricks - ns_tricks);
  struct timeval finish;
  gettimeofday(&finish, NULL);
  printf("time: %.3fs\n", (finish.tv_sec - start.tv_sec) +
         (double(finish.tv_usec) - start.tv_usec) * 1e-6);
  return 0;
}
