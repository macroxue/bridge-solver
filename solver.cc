#include <assert.h>
#include <ctype.h>
#if defined(__BMI2__) || defined(__SSE4_1__)
#include <immintrin.h>
#elif defined(__ARM_NEON)
#include <arm_neon.h>
#endif
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <random>
#include <vector>

#include "solver.h"

// clang-format off
#ifdef _DEBUG
#define CHECK(statement) assert(statement)
#define VERBOSE(statement) if (depth <= options.displaying_depth) statement
#define STATS(statement) statement
#else
#define CHECK(statement)
#define VERBOSE(statement)
#define STATS(statement)
#endif
// clang-format on

const int TOTAL_CARDS = NUM_RANKS * NUM_SUITS;

const char* SeatName(int seat) {
  static const char* seat_names[] = {"West", "North", "East", "South"};
  return seat_names[seat];
}
char SeatLetter(int seat) { return SeatName(seat)[0]; }
bool IsNs(int seat) { return seat & 1; }

const char* SuitName(int suit) {
  static const char* suit_names[] = {"Spade", "Heart", "Diamond", "Club", "NoTrump"};
  return suit_names[suit];
}

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

char RankName(int rank) {
  static const char rank_names[] = "23456789TJQKA";
  return rank_names[rank];
}

int CharToSuit(char c) {
  for (int suit = SPADE; suit <= NOTRUMP; ++suit)
    if (toupper(c) == SuitName(suit)[0]) return suit;
  fprintf(stderr, "Unknown suit: %c\n", c);
  exit(-1);
}

int CharToRank(char c) {
  if (c == '1') return TEN;
  for (int rank = TWO; rank <= ACE; ++rank)
    if (toupper(c) == RankName(rank)) return rank;
  fprintf(stderr, "Unknown rank: %c\n", c);
  exit(-1);
}

int CharToSeat(char c) {
  for (int seat = WEST; seat <= SOUTH; ++seat)
    if (toupper(c) == SeatLetter(seat)) return seat;
  fprintf(stderr, "Unknown seat: %c\n", c);
  exit(-1);
}

// Number of bits in a suit, in the range of [13, 16]. Pick 16 to avoid divisions.
const int SUIT_SPAN = 16;
// The bit right after the highest valid card bit, also indicating an invalid card.
const int CARD_END = (NUM_SUITS - 1) * SUIT_SPAN + NUM_RANKS;

char suit_of[CARD_END];
char rank_of[CARD_END];
char card_of[NUM_SUITS][16];
char name_of[CARD_END][4];

int SuitOf(int card) { return SUIT_SPAN == 16 ? int(unsigned(card) / SUIT_SPAN) : suit_of[card]; }
int RankOf(int card) { return rank_of[card]; }
int CardOf(int suit, int rank) { return card_of[suit][rank]; }
uint64_t MaskOf(int suit) { return 0x1fffULL << (suit * SUIT_SPAN); }
const char* NameOf(int card) { return name_of[card]; }

const auto DECK_MASK = MaskOf(SPADE) + MaskOf(HEART) + MaskOf(DIAMOND) + MaskOf(CLUB);

bool LowerRank(int card1, int card2) { return card1 > card2; }
bool HigherRank(int card1, int card2) { return card1 < card2; }

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

Options options;

// The trump bounds at a trick start are tried only this close to the tricks needed.
const int MAX_RANK_TRICK_GAP = 2;
const int MAX_DRAW_TRUMP_GAP = 3;

template <class T>
int BitSize(T v) {
  return sizeof(v) * 8;
}

template <int STAGES = 6>
uint64_t PackBits(uint64_t source, uint64_t mask) {
#ifdef __BMI2__
  return _pext_u64(source, mask);
#elif defined(__EMSCRIPTEN__) && !defined(__wasm_simd128__)
  // Bit-at-a-time loop: measured faster than the branchless version below on
  // weak/narrow cores (older phones/tablets without wasm SIMD) -- its cost
  // scales with popcount(mask) (avg ~5.4 for suit-local masks), cheaper
  // there than paying the branchless version's larger fixed op count when
  // branches are cheap and there's no spare execution width to hide it.
  if (source == 0) return 0;
  uint64_t packed = 0;
  for (uint64_t bit = 1; mask; bit <<= 1, mask &= mask - 1)
    if (source & mask & -mask) packed |= bit;
  return packed;
#else
  // Branchless bit compress (Hacker's Delight 7-4). 4 stages covers a
  // suit-local mask (SUIT_SPAN <= 16), 6 the full 64-bit deck.
  source &= mask;
  uint64_t mk = ~mask << 1;
  for (int i = 0; i < STAGES; ++i) {
    uint64_t mp = mk ^ (mk << 1);
    mp ^= mp << 2;
    mp ^= mp << 4;
    mp ^= mp << 8;
    if constexpr (STAGES > 4) {
      mp ^= mp << 16;
      mp ^= mp << 32;
    }
    uint64_t mv = mp & mask;
    mask = (mask ^ mv) | (mv >> (1 << i));
    uint64_t t = source & mv;
    source = (source ^ t) | (t >> (1 << i));
    mk &= ~mp;
  }
  return source;
#endif
}

template <int STAGES = 6>
uint64_t UnpackBits(uint64_t source, uint64_t mask) {
#ifdef __BMI2__
  return _pdep_u64(source, mask);
#elif defined(__EMSCRIPTEN__) && !defined(__wasm_simd128__)
  // Bit-at-a-time loop -- see PackBits's comment for why this beats the
  // branchless version on weak/narrow cores.
  if (source == 0) return 0;
  uint64_t unpacked = 0;
  for (uint64_t bit = 1; source; bit <<= 1, mask &= mask - 1)
    if (source & bit) {
      unpacked |= mask & -mask;
      source &= ~bit;
    }
  return unpacked;
#else
  uint64_t mv[STAGES], mask0 = mask, mk = ~mask << 1;
  for (int i = 0; i < STAGES; ++i) {
    uint64_t mp = mk ^ (mk << 1);
    mp ^= mp << 2;
    mp ^= mp << 4;
    mp ^= mp << 8;
    if constexpr (STAGES > 4) {
      mp ^= mp << 16;
      mp ^= mp << 32;
    }
    mv[i] = mp & mask;
    mask = (mask ^ mv[i]) | (mv[i] >> (1 << i));
    mk &= ~mp;
  }
  for (int i = STAGES - 1; i >= 0; --i) {  // undo in reverse order
    uint64_t t = source << (1 << i);
    source = (source & ~mv[i]) | (t & mv[i]);
  }
  return source & mask0;
#endif
}

class Cards {
 public:
  Cards() : bits(0) {}
  Cards(uint64_t b) : bits(b) {}
  uint64_t Value() const { return bits; }
  uint64_t PackedValue() const {
    if (SUIT_SPAN == NUM_RANKS) return bits;
#ifdef __BMI2__
    return PackBits(bits, DECK_MASK);
#else
    return Suit(0).Value() + (Suit(1).Value() >> (SUIT_SPAN - NUM_RANKS)) +
           (Suit(2).Value() >> (SUIT_SPAN * 2 - NUM_RANKS * 2)) +
           (Suit(3).Value() >> (SUIT_SPAN * 3 - NUM_RANKS * 3));
#endif
  }

  int Size() const { return __builtin_popcountll(bits); }
  bool operator==(const Cards& c) const { return bits == c.bits; }
  bool operator!=(const Cards& c) const { return bits != c.bits; }
  operator bool() const { return bits != 0; }

  Cards Slice(int begin, int end) const { return bits & (Bit(end) - Bit(begin)); }
  Cards Suit(int suit) const { return bits & MaskOf(suit); }
  int Top() const { return __builtin_ctzll(bits); }
  int Bottom() const { return BitSize(bits) - 1 - __builtin_clzll(bits); }

  Cards Union(const Cards& c) const { return bits | c.bits; }
  Cards Intersect(const Cards& c) const { return bits & c.bits; }
  Cards Different(const Cards& c) const { return bits & ~c.bits; }
  Cards Complement() const { return DECK_MASK ^ bits; }
  bool Include(int card) const { return bits & Bit(card); }
  bool Include(const Cards& c) const { return Intersect(c) == c; }
  bool StrictlyInclude(const Cards& c) const { return Include(c) && bits != c.bits; }

  Cards Add(int card) { return bits |= Bit(card); }
  Cards Remove(int card) { return bits &= ~Bit(card); }

  Cards Add(const Cards& c) { return bits |= c.bits; }
  Cards Remove(const Cards& c) { return bits &= ~c.bits; }
  Cards ClearSuit(int suit) { return bits &= ~MaskOf(suit); }

  uint64_t ShapeParity() const {
    uint64_t parity = 0;
    for (int suit = 0; suit < NUM_SUITS; ++suit) parity += (Suit(suit).Size() % 2) << suit;
    return parity;
  }

  int Points() const {
    int points = 0;
    for (int card : *this)
      if (RankOf(card) > TEN) points += RankOf(card) - TEN;
    return points;
  }

  void Show() const {
    for (int suit = 0; suit < NUM_SUITS; ++suit) {
      ShowSuit(suit);
      putchar(' ');
    }
  }

  void ShowSuit(int suit) const {
    printf("%s ", SuitSign(suit));
    if (Suit(suit))
      for (int card : Suit(suit)) putchar(NameOf(card)[1]);
    else
      putchar('-');
  }

  class Iterator {
   public:
    Iterator(uint64_t bits) : bits(bits) {}
    void operator++() { bits &= bits - 1; }
    bool operator!=(const Iterator& it) const { return bits != it.bits; }
    int operator*() const { return __builtin_ctzll(bits); }

   private:
    uint64_t bits;
  };

  Iterator begin() const { return Iterator(bits); }
  Iterator end() const { return Iterator(0); }

 private:
  static uint64_t Bit(int index) { return uint64_t(1) << index; }

  uint64_t bits;
};

class Hands {
 public:
  void Randomize() {
    for (int seat = 0; seat < NUM_SEATS; ++seat)
      for (int i = 0; i < NUM_RANKS; ++i) hands[seat].Add(CardOf(seat, i));
    Shuffle("NEWS");
  }

  void Shuffle(const char* shuffle_seats) {
    std::vector<int> seats;
    for (auto* s = shuffle_seats; *s; ++s) seats.push_back(CharToSeat(*s));
    Cards cards;
    for (int seat : seats) {
      cards.Add(hands[seat]);
      hands[seat] = Cards();
    }
    Deal(cards, seats);
  }

  void Deal(Cards cards, const std::vector<int>& seats) {
    // Seed with multiple random_device draws as each one is only 32-bit.
    static std::mt19937 random = [] {
      std::random_device rd;
      std::seed_seq seed{rd(), rd(), rd(), rd()};
      return std::mt19937(seed);
    }();
    std::vector<int> deck;
    for (int card : cards) deck.push_back(card);
    std::shuffle(deck.begin(), deck.end(), random);
    int tricks = deck.size() / seats.size();
    for (int seat : seats) {
      for (int i = 0; i < tricks; ++i) {
        hands[seat].Add(deck.back());
        deck.pop_back();
      }
    }
  }

  void Decode(const char* code) {
    uint64_t values[3];
    int num_values =
        sscanf(code, "%" SCNx64 ",%" SCNx64 ",%" SCNx64, values, values + 1, values + 2);
    assert(num_values == 3);
    auto mask = DECK_MASK;
    for (int seat = 0; seat < NUM_SEATS - 1; ++seat) {
      hands[seat] = UnpackBits(values[seat], mask);
      mask &= ~hands[seat].Value();
    }
    hands[NUM_SEATS - 1] = UnpackBits((1ULL << TOTAL_TRICKS) - 1, mask);
    assert(hands[0].Size() == hands[1].Size() && hands[1].Size() == hands[2].Size() &&
           hands[2].Size() == hands[3].Size());
  }

  Cards all_cards() const {
    return hands[WEST].Union(hands[NORTH]).Union(hands[EAST]).Union(hands[SOUTH]);
  }
  const Cards& operator[](int seat) const { return hands[seat]; }
  Cards& operator[](int seat) { return hands[seat]; }

  bool Equals(const Hands& other) const {
#ifdef __AVX2__
    __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(hands));
    __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(other.hands));
    __m256i diff = _mm256_xor_si256(a, b);
    return _mm256_testz_si256(diff, diff);
#elif defined(__SSE4_1__)
    __m128i a0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&hands[WEST]));
    __m128i b0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&other.hands[WEST]));
    __m128i a1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&hands[EAST]));
    __m128i b1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&other.hands[EAST]));
    __m128i diff = _mm_or_si128(_mm_xor_si128(a0, b0), _mm_xor_si128(a1, b1));
    return _mm_testz_si128(diff, diff);
#elif defined(__ARM_NEON)
    uint64x2_t a0 = vld1q_u64(reinterpret_cast<const uint64_t*>(&hands[WEST]));
    uint64x2_t b0 = vld1q_u64(reinterpret_cast<const uint64_t*>(&other.hands[WEST]));
    uint64x2_t a1 = vld1q_u64(reinterpret_cast<const uint64_t*>(&hands[EAST]));
    uint64x2_t b1 = vld1q_u64(reinterpret_cast<const uint64_t*>(&other.hands[EAST]));
    uint64x2_t diff = vorrq_u64(veorq_u64(a0, b0), veorq_u64(a1, b1));
    return (vgetq_lane_u64(diff, 0) | vgetq_lane_u64(diff, 1)) == 0;
#else
    return (hands[WEST] == other.hands[WEST]) & (hands[NORTH] == other.hands[NORTH]) &
           (hands[EAST] == other.hands[EAST]) & (hands[SOUTH] == other.hands[SOUTH]);
#endif
  }

  int num_tricks() const { return hands[WEST].Size(); }

  int num_voids() const {
    int voids = 0;
    for (int seat = 0; seat < NUM_SEATS; ++seat)
      for (int suit = 0; suit < NUM_SUITS; ++suit)
        if (hands[seat].Suit(suit).Size() == 0) ++voids;
    return voids;
  }

  void Show() const {
    for (int seat = 0; seat < NUM_SEATS; ++seat) {
      hands[seat].Show();
      if (seat < NUM_SEATS - 1) printf(", ");
    }
    puts("");
  }

  std::string Encode() const {
    uint64_t values[3];
    auto mask = DECK_MASK;
    for (int seat = 0; seat < NUM_SEATS - 1; ++seat) {
      values[seat] = PackBits(hands[seat].Value(), mask);
      mask &= ~hands[seat].Value();
    }
    char code[64];
    snprintf(code, sizeof(code), "%" PRIX64 ",%" PRIX64 ",%" PRIX64, values[0], values[1],
             values[2]);
    return code;
  }

  void ShowCompact(int rotation = 0) const {
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

  void ShowDetailed(int rotation = 0) const {
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

 private:
  void ShowHandInfo(Cards hand, int seat, int suit, int gap) const {
    if (suit == SPADE)
      printf("%*s%c ", gap - 2, " ", SeatLetter(seat));
    else if (suit == CLUB)
      printf("%*s%2d ", gap - 3, " ", hand.Points());
    else
      printf("%*s", gap, " ");
  }

  Cards hands[NUM_SEATS];
};

Hands empty_hands;

class Shape {
 public:
  Shape() : value(0) {}
  Shape(uint64_t value) : value(value) {}
  Shape(const Hands& hands) : value(0) {
    for (int seat = 0; seat < NUM_SEATS; ++seat)
      for (int suit = 0; suit < NUM_SUITS; ++suit)
        value += uint64_t(hands[seat].Suit(suit).Size()) << Offset(seat, suit);
  }

  void PlayCards(int seat, int c1, int c2, int c3, int c4) {
    value -= Unit(seat, c1) + Unit(seat + 1, c2) + Unit(seat + 2, c3) + Unit(seat + 3, c4);
  }

  int SuitLength(int seat, int suit) const { return (value >> Offset(seat, suit)) & 0xf; }

  uint64_t Value() const { return value; }

  bool operator==(const Shape& s) const { return value == s.value; }

 private:
  static int Offset(int seat, int suit) { return 60 - (seat * NUM_SUITS + suit) * 4; }
  // One card of the suit of `card` in the hand of `seat`, which may be 4 or more.
  static uint64_t Unit(int seat, int card) {
    return 1ULL << Offset(seat & (NUM_SEATS - 1), SuitOf(card));
  }

  uint64_t value;
};

// The hands of a pattern or of a position to match patterns against, as the
// seat of each card in play, in two bit planes. Cards are numbered densely
// from the top card of spades down to the bottom card of clubs (PackBits's
// order): `cards` is the cards present, and bit i of `low`/`high` is the
// low/high bit of the seat holding card i (W=00, N=01, E=10, S=11). A pattern
// holds its rank winners; a position holds every card.
class RelativeHands {
 public:
  RelativeHands() : low(0), high(0), cards(0) {}

  // A position's whole hands with `all_cards` in play.
  RelativeHands(const Hands& hands, Cards all_cards)
      : low(PackBits(hands[NORTH].Union(hands[SOUTH]).Value(), all_cards.Value())),
        high(PackBits(hands[EAST].Union(hands[SOUTH]).Value(), all_cards.Value())),
        cards((uint64_t(1) << all_cards.Size()) - 1) {}  // PackBits of all_cards over itself

  // These hands reduced to the cards in `kept_cards`, numbered as above.
  RelativeHands Keep(uint64_t kept_cards) const {
    RelativeHands kept;
    kept.low = low & kept_cards;
    kept.high = high & kept_cards;
    kept.cards = kept_cards;
    return kept;
  }

  int Size() const { return __builtin_popcountll(cards); }

  // Whether every card in `other` is in the same seat's hand here (and no
  // card of `other` is missing).
  bool Include(const RelativeHands& other) const {
    return ((((low ^ other.low) | (high ^ other.high)) & other.cards) | (~cards & other.cards)) == 0;
  }
  bool Equals(const RelativeHands& other) const {
    return low == other.low && high == other.high && cards == other.cards;
  }

  // Whether every card here is in the same seat's hand in a position, which has every card
  // and comes as its two bit planes.
  bool Matches(uint64_t position_low, uint64_t position_high) const {
    return (((low ^ position_low) | (high ^ position_high)) & cards) == 0;
  }
  uint64_t Low() const { return low; }
  uint64_t High() const { return high; }

  // The cards here expanded to their real positions in all_cards.
  Cards GetRankWinners(Cards all_cards) const {
    return Cards(UnpackBits(cards, all_cards.Value()));
  }

  // These hands in shape's grid of seats and suits.
  void Show(Shape shape) const {
    for (int seat = 0; seat < NUM_SEATS; ++seat) {
      for (int suit = 0; suit < NUM_SUITS; ++suit) {
        auto suit_length = shape.SuitLength(seat, suit);
        if (suit_length == 0)
          putchar('-');
        else {
          // Card numbers of the suit start after those of the suits before it.
          int first = 0;
          for (int s = 0; s < suit; ++s)
            for (int t = 0; t < NUM_SEATS; ++t) first += shape.SuitLength(t, s);
          int total = 0;
          for (int t = 0; t < NUM_SEATS; ++t) total += shape.SuitLength(t, suit);
          int winners = 0;
          for (int i = 0; i < total; ++i)
            if ((cards >> (first + i) & 1) && SeatOf(first + i) == seat) {
              printf("%c", RankName(ACE - i));
              ++winners;
            }
          for (int i = winners; i < suit_length; ++i) putchar('x');
        }
        putchar(' ');
      }
      if (seat < NUM_SEATS - 1) printf(", ");
    }
  }

 private:
  // The seat holding a card, by number.
  int SeatOf(int number) const { return (low >> number & 1) + (high >> number & 1) * 2; }

  uint64_t low, high, cards;
};

template <class Entry>
class Cache {
 public:
  // The hash is stored in Entry and can vary in size.
  using HashT = decltype(Entry::hash);

  Cache(const char* name, int bits)
      : cache_name(name), bits(bits), size(1 << bits), entries(new Entry[size]) {
    Reset();
  }

  void Reset() {
    load_count = lookups = lookup_probes = hits = updates = update_probes = overwrites = 0;
    for (int i = 0; i < size; ++i) entries[i].Reset(0);
  }

  void ShowStatistics() const {
    printf("--- %s Statistics ---\n", cache_name);
    printf("lookups: %8d   probes: %8d (%.2f/lookup)         hits: %8d (%5.2f%%)\n", lookups,
           lookup_probes, lookup_probes * 1.0 / lookups, hits, hits * 100.0 / lookups);
    printf("updates: %8d   probes: %8d (%.2f/update)   overwrites: %8d (%5.2f%%)\n", updates,
           update_probes, update_probes * 1.0 / updates, overwrites, overwrites * 100.0 / updates);
    printf("entries: %8d   loaded: %8d (%5.2f%%)\n", size, load_count, load_count * 100.0 / size);

    int recursive_load = 0;
    for (int i = 0; i < size; ++i)
      if (entries[i].hash != 0) {
        recursive_load += entries[i].Size();
        STATS(if (options.stats_level > 1) entries[i].Show());
      }
    if (recursive_load > load_count) printf("recursive load: %8d\n", recursive_load);
  }

  // Callers compute the hash once and pass it to both Lookup() and, on a miss,
  // Update() so a single key is never hashed twice.
  HashT Hash(uint64_t value) const {
    // When HashT is narrower than 64 bits, keep the high-entropy top bits.
    return HashT((value * 0x9b8b4567327b23c7ULL) >> (64 - BitSize(HashT(0))));
  }

  const Entry* Lookup(HashT hash) const {
    STATS(++lookups);
    uint64_t index = hash >> (BitSize(hash) - bits);

    // Linear probing benefits from hardware prefetch.
    for (int d = 0;; ++d) {
      const Entry& entry = entries[(index + d) & (size - 1)];
      if (entry.hash == hash) {
        STATS(++hits);
        return &entry;
      }
      if (entry.hash == 0) break;
      STATS(++lookup_probes);
    }
    return nullptr;
  }

  Entry* Update(HashT hash) {
    if (load_count >= size * 3 / 4) Resize();

    STATS(++updates);
    uint64_t index = hash >> (BitSize(hash) - bits);

    for (int d = 0;; ++d) {
      Entry& entry = entries[(index + d) & (size - 1)];
      if (entry.hash == hash) {
        STATS(++overwrites);
        return &entry;
      }
      if (entry.hash == 0) {
        ++load_count;
        entry.Reset(hash);
        return &entry;
      }
      STATS(++update_probes);
    }
  }

 private:
  void Resize() {
    auto old_entries = std::move(entries);
    int old_size = size;

    // Double cache size.
    size = 1 << ++bits;
    entries.reset(new Entry[size]);
    CHECK(entries.get());
    for (int i = 0; i < size; ++i) entries[i].hash = 0;

    // Move entries in the old cache to the new cache.
    load_count = 0;
    for (int i = 0; i < old_size; ++i) {
      auto hash = old_entries[i].hash;
      if (hash == 0) continue;
      uint64_t index = hash >> (BitSize(hash) - bits);
      for (int d = 0;; ++d) {
        Entry& entry = entries[(index + d) & (size - 1)];
        if (entry.hash == 0) {
          old_entries[i].MoveTo(entry);
          ++load_count;
          break;
        }
      }
    }
  }

  const char* cache_name;
  int bits;
  int size;
  std::unique_ptr<Entry[]> entries;

  mutable int load_count;
  mutable int lookups, lookup_probes, hits;
  mutable int updates, update_probes, overwrites;
};

#pragma pack(push, 4)
struct Bounds {
  char lower;
  char upper;

  bool Empty() const { return upper < lower; }
  Bounds Intersect(Bounds b) const { return {std::max(lower, b.lower), std::min(upper, b.upper)}; }
  bool Include(Bounds b) const { return Intersect(b) == b; }
  bool operator==(Bounds b) const { return b.lower == lower && b.upper == upper; }
  bool operator!=(Bounds b) const { return !(*this == b); }
  bool Cutoff(int beta) const { return lower >= beta || upper < beta; }
};

// Free-list pool for Vector<T>'s backing storage: one LIFO list per
// power-of-two capacity, refilled in SLAB_SIZE slabs and never freed.
template <class T>
class VectorPool {
 public:
  static char* Allocate(int size_class) {
    STATS(++alloc_calls[size_class]);
    char*& head = free_lists[size_class];
    if (!head) {
      STATS(++miss_calls[size_class]);
      Refill(size_class);
    }
    char* block = head;
    head = *reinterpret_cast<char**>(block);
    return block;
  }

  static void Deallocate(char* block, int size_class) {
    char*& head = free_lists[size_class];
    *reinterpret_cast<char**>(block) = head;
    head = block;
  }

  static void ShowStatistics() {
    printf("--- VectorPool<T> Statistics (block = %zu bytes) ---\n", sizeof(T));
    uint64_t total_allocs = 0;
    for (int i = 0; i < 16; ++i) total_allocs += alloc_calls[i];
    for (int i = 0; i < 16; ++i) {
      if (!alloc_calls[i]) continue;
      printf("class %2d (cap %6zu): allocs %10" PRIu64 " (%5.2f%%)   misses %8" PRIu64
             " (%6.2f%% of allocs)\n",
             i, size_t{1} << i, alloc_calls[i], alloc_calls[i] * 100.0 / total_allocs,
             miss_calls[i], miss_calls[i] * 100.0 / alloc_calls[i]);
    }
  }

 private:
  static constexpr size_t SLAB_SIZE = 8192;

  // One allocation for a full slab, amortizing the malloc call across all of them.
  static void Refill(int size_class) {
    size_t block_bytes = (1ULL << size_class) * sizeof(T);
    size_t num_blocks = SLAB_SIZE / block_bytes;
    if (num_blocks == 0) num_blocks = 1;
    char* slab = new char[num_blocks * block_bytes];
    char*& head = free_lists[size_class];
    for (size_t i = 0; i < num_blocks; ++i) {
      char* block = slab + i * block_bytes;
      *reinterpret_cast<char**>(block) = head;
      head = block;
    }
  }

  static inline uint64_t alloc_calls[16] = {};
  static inline uint64_t miss_calls[16] = {};
  static inline char* free_lists[16] = {};
};

template <class T>
class Vector {
 public:
  ~Vector() { clear(); }

  void clear() {
    for (size_t i = 0; i < count; ++i) (*this)[i].~T();
    // Guard on capacity, not items: __builtin_ctz(0) is UB, and a null-items
    // guard could get optimized away since items==nullptr iff capacity==0.
    if (capacity) VectorPool<T>::Deallocate(items, __builtin_ctz(capacity));
    count = capacity = 0;
    items = nullptr;
  }

  void resize(size_t new_size) {
    if (capacity < new_size) {
      int size_class = new_size <= 1 ? 0 : 32 - __builtin_clz((unsigned)new_size - 1);
      char* new_items = VectorPool<T>::Allocate(size_class);
      memcpy(new_items, items, count * sizeof(T));
      if (capacity) VectorPool<T>::Deallocate(items, __builtin_ctz(capacity));
      items = new_items;
      capacity = 1 << size_class;
    }
    memset(items + count * sizeof(T), 0, (new_size - count) * sizeof(T));
    count = new_size;
  }

  size_t size() const { return count; }

  T& operator[](size_t i) { return reinterpret_cast<T*>(items)[i]; }
  const T& operator[](size_t i) const { return reinterpret_cast<T*>(items)[i]; }

  T& back() { return (*this)[count - 1]; }
  const T& back() const { return (*this)[count - 1]; }

  void pop_back() {
    back().~T();
    --count;
  }

  void swap(Vector<T>& v) {
    std::swap(count, v.count);
    std::swap(capacity, v.capacity);
    std::swap(items, v.items);
  }

 private:
  uint16_t count = 0;
  uint16_t capacity = 0;
  char* items = nullptr;
};

struct Pattern {
  RelativeHands hands;
  Bounds bounds;
  uint16_t order;
  Vector<Pattern> patterns;
#ifdef _DEBUG
  mutable uint16_t hits, cuts;
#endif

  Pattern(const RelativeHands& hands = RelativeHands(), Bounds bounds = Bounds())
      : hands(hands), bounds(bounds), order(hands.Size()) {
    STATS(hits = cuts = 0);
  }

  // Takes over p, leaving it with no subpatterns. Any subpatterns this pattern
  // owns are overwritten, not freed.
  void MoveFrom(Pattern& p) {
    memcpy((void*)this, (void*)&p, sizeof(Pattern));
    new (&p.patterns) Vector<Pattern>();
  }

  void swap(Pattern& p) {
    std::swap(hands, p.hands);
    std::swap(bounds, p.bounds);
    std::swap(order, p.order);
    patterns.swap(p.patterns);
    STATS(std::swap(hits, p.hits));
    STATS(std::swap(cuts, p.cuts));
  }

  // Lookup() below with the scan of `patterns` itself inlined into the caller.
  __attribute__((always_inline)) static const Pattern* Lookup(const Vector<Pattern>& patterns,
                                                              const RelativeHands& hands,
                                                              int beta) {
    const uint64_t low = hands.Low(), high = hands.High();
    for (size_t i = 0, size = patterns.size(); i < size; ++i) {
      auto& pattern = patterns[i];
      if (!pattern.hands.Matches(low, high)) continue;
      STATS(++pattern.hits);
      if (pattern.bounds.Cutoff(beta)) {
        STATS(++pattern.cuts);
        return &pattern;
      }
      if (pattern.patterns.size() == 0) continue;
      if (auto* best = Lookup(pattern.patterns, low, high, beta)) return best;
    }
    return nullptr;
  }

  // The first pattern, going down into the subpatterns of each pattern before the next one,
  // that has the position's cards and bounds deciding `beta`. The position comes as its two
  // bit planes so that they stay in registers.
  __attribute__((noinline)) static const Pattern* Lookup(const Vector<Pattern>& patterns,
                                                         uint64_t low, uint64_t high, int beta) {
    for (size_t i = 0, size = patterns.size(); i < size; ++i) {
      auto& pattern = patterns[i];
      if (!pattern.hands.Matches(low, high)) continue;
      STATS(++pattern.hits);
      if (pattern.bounds.Cutoff(beta)) {
        STATS(++pattern.cuts);
        return &pattern;
      }
      if (auto* best = Lookup(pattern.patterns, low, high, beta)) return best;
    }
    return nullptr;
  }

  // Patterns are sorted by order, most generic first.
  static void Update(Vector<Pattern>& patterns, Pattern& new_pattern) {
    size_t n = patterns.size(), pos = 0;
    // Local copies: the compiler can't prove new_pattern is disjoint from
    // patterns, so it would reload these on every comparison.
    const RelativeHands new_hands = new_pattern.hands;
    const uint16_t new_order = new_pattern.order;
    const Bounds new_bounds = new_pattern.bounds;
    // Go under the most generic pattern including the new one.
    for (; pos < n && patterns[pos].order <= new_order; ++pos) {
      auto& pattern = patterns[pos];
      if (!new_hands.Include(pattern.hands)) continue;
      if (new_hands.Equals(pattern.hands))
        pattern.bounds = pattern.bounds.Intersect(new_bounds);
      else
        Update(pattern.patterns, new_pattern);
      return;
    }
    // Adopt more specific patterns, dropping those with no tighter bounds but
    // keeping their subpatterns. The rest are kept in patterns[pos, kept).
    bool sorted = true;
    size_t kept = pos;
    for (size_t i = pos; i < n; ++i) {
      auto& pattern = patterns[i];
      if (!pattern.hands.Include(new_hands)) {
        if (kept != i) patterns[kept].MoveFrom(pattern);
        ++kept;
      } else if (pattern.bounds.Include(new_bounds)) {
        sorted &= pattern.patterns.size() == 0;
        Append(new_pattern.patterns, pattern.patterns);
        pattern.patterns.clear();
      } else {
        Append(new_pattern.patterns, pattern);
      }
    }
    if (!sorted) Sort(new_pattern.patterns);
    // Shift the kept patterns one slot back and put the new pattern at pos.
    // Slot kept owns nothing, so memmove is safe like the relocation in Vector::resize().
    if (kept == n) patterns.resize(n + 1);
    memmove((void*)&patterns[pos + 1], (void*)&patterns[pos], (kept - pos) * sizeof(Pattern));
    patterns[pos].MoveFrom(new_pattern);
    while (patterns.size() > kept + 1) patterns.pop_back();
  }

  static void Sort(Vector<Pattern>& patterns) {
    for (size_t i = 1; i < patterns.size(); ++i)
      for (size_t j = i; j > 0 && patterns[j - 1].order > patterns[j].order; --j)
        patterns[j].swap(patterns[j - 1]);
  }

  static void Append(Vector<Pattern>& patterns, Pattern& new_pattern) {
    patterns.resize(patterns.size() + 1);
    patterns.back().MoveFrom(new_pattern);
  }

  static void Append(Vector<Pattern>& patterns, Vector<Pattern>& new_patterns) {
    auto new_size = new_patterns.size();
    if (new_size == 0) return;
    auto size = patterns.size();
    patterns.resize(size + new_size);
    for (size_t i = 0; i < new_size; ++i) patterns[size + i].MoveFrom(new_patterns[i]);
  }

  Cards GetRankWinners(Cards all_cards) const { return hands.GetRankWinners(all_cards); }

  int Size() const {
    int sum = 1;
    for (size_t i = 0; i < patterns.size(); ++i) sum += patterns[i].Size();
    return sum;
  }

  void Show(Shape shape, int level = 1, Bounds parent_bounds = {0, TOTAL_TRICKS}) const {
    if (level > 0) {
      printf("%*d: (%d %d) ", level * 2, level, bounds.lower, bounds.upper);
      hands.Show(shape);
      printf(" %2d", order);
      STATS(printf(" hits %d cuts %d", hits, cuts));
      puts(level > 1 && bounds.Include(parent_bounds) ? " dup" : "");
    }
    for (size_t i = 0; i < patterns.size(); ++i) patterns[i].Show(shape, level + 1, bounds);
  }
};

struct ShapeEntry {
  uint64_t hash;
  Vector<Pattern> patterns[NUM_SEATS];
#ifdef _DEBUG
  Shape shape;

  void Show() const {
    for (int s = 0; s < NUM_SEATS; ++s) {
      if (patterns[s].size() == 0) continue;
      printf("hash %016lx shape %016lx seat %c size %ld total size %d\n", hash, shape.Value(),
             SeatLetter(s), patterns[s].size(), Size(s));
      for (size_t i = 0; i < patterns[s].size(); ++i) patterns[s][i].Show(shape);
    }
  }
#endif

  int Size(int s) const {
    int total = 0;
    for (size_t i = 0; i < patterns[s].size(); ++i) total += patterns[s][i].Size();
    return total;
  }

  int Size() const {
    int total = 0;
    for (int s = 0; s < NUM_SEATS; ++s) total += Size(s);
    return total;
  }

  void Reset(uint64_t hash_in) {
    hash = hash_in;
    for (int s = 0; s < NUM_SEATS; ++s) patterns[s].clear();
#ifdef _DEBUG
    shape = Shape();
#endif
  }

  void MoveTo(ShapeEntry& to) {
    to.hash = hash;
    for (int s = 0; s < NUM_SEATS; ++s) to.patterns[s].swap(patterns[s]);
#ifdef _DEBUG
    to.shape = shape;
#endif
  }
};

struct CutoffEntry {
  // Using 32-bit instead of 64-bit hash is safe because cut-off cards are
  // move-ordering hints and collisions impact performance, not correctness.
  uint32_t hash;
  // Cut-off card depending on the seat to play.
  char card[NUM_SEATS];

  int Size() const { return 1; }

  void Show() const {}

  void Reset(uint64_t hash_in) {
    hash = hash_in;
    memset(card, CARD_END, sizeof(card));
  }

  void MoveTo(CutoffEntry& to) { memcpy(&to, this, sizeof(*this)); }
};
#pragma pack(pop)

Cache<ShapeEntry> common_bounds_cache("Common Bounds Cache", 13);
Cache<CutoffEntry> cutoff_cache("Cut-off Cache", 16);

struct Trick {
  Shape shape;
  Cards all_cards;
  RelativeHands relative_hands;
  int lead_suit;

  // Each card's seat as dense card numbers (see RelativeHands).
  void ComputeRelativeHands(const Hands& hands) { relative_hands = RelativeHands(hands, all_cards); }

  // If `card` is equivalent to one of the tried cards, the rank positions from the higher
  // of the two down to just above the lower one (never empty); else no cards.
  Cards EquivalentRun(int card, Cards tried_suit_cards, Cards hand) const {
    if (!tried_suit_cards) return Cards();
    if (auto above = tried_suit_cards.Slice(0, card))
      if (all_cards.Slice(above.Bottom(), card) == hand.Slice(above.Bottom(), card))
        return Cards(~0ULL).Slice(above.Bottom(), card);
    if (auto below = tried_suit_cards.Slice(card + 1, CARD_END))
      if (all_cards.Slice(card, below.Top()) == hand.Slice(card, below.Top()))
        return Cards(~0ULL).Slice(card, below.Top());
    return Cards();
  }

  Cards FilterEquivalent(Cards playable_cards) const {
    Cards filtered_cards;
    for (int suit = 0; suit < NUM_SUITS; ++suit) {
      auto suit_cards = playable_cards.Suit(suit);
      if (!suit_cards) continue;
      int prev_card = suit_cards.Top();
      filtered_cards.Add(prev_card);
      suit_cards.Remove(prev_card);
      for (int card : suit_cards) {
        if (RelativeRank(prev_card, suit) != RelativeRank(card, suit) + 1) filtered_cards.Add(card);
        prev_card = card;
      }
    }
    return filtered_cards;
  }

  // The cards of a pattern for a position won with `rank_winners`: in each suit, those from
  // the top down to the lowest rank winner. `runs` are the equivalence runs the search of
  // this trick relied on: where a side that had to fail with every card skipped a card as
  // equivalent to a tried one, the rank positions from the higher of the two down to just
  // above the lower one. A pattern whose lowest rank winner fell inside such a run would
  // match positions where the two cards are not equivalent, so it is extended to the low end
  // of the run. (Skips by a side that needed only one card to work, and skips of cards below
  // every rank winner, need nothing: any low card does.)
  Cards ComputePatternCards(const Hands& hands, Cards rank_winners, Cards runs) const {
    Cards pattern_cards;
    for (int suit = 0; suit < NUM_SUITS; ++suit) {
      if (!rank_winners.Suit(suit)) continue;
      auto all_suit_cards = all_cards.Suit(suit);
      // A run ends just above a card in play, so this stops at one.
      int bottom_card = rank_winners.Suit(suit).Bottom();
      while (runs.Include(bottom_card)) ++bottom_card;
      // The number of cards kept, counting down from the top of the suit.
      int kept = all_suit_cards.Slice(0, bottom_card).Size() + 1;
      for (int seat = 0; seat < NUM_SEATS; ++seat) {
        if (!hands[seat].Include(bottom_card)) continue;
        // The seat's cards of the suit as relative cards, the top card of the suit at bit 0.
        uint64_t suit_cards = PackBits(hands[seat].Value(), all_suit_cards.Value());
        // If this seat holds every card from the bottom rank winner to the suit bottom, the
        // cards above them already determine the suit: stop above them. (This also keeps the
        // suit bottom, which can't win by rank, out of the pattern.)
        int lowest = kept + __builtin_ctzll(~(suit_cards >> kept));
        if (lowest == all_suit_cards.Size())
          kept = lowest - __builtin_clzll(~(suit_cards << (64 - lowest)));
        break;
      }
      pattern_cards.Add(Cards(UnpackBits((1ULL << kept) - 1, all_suit_cards.Value())));
    }
    return pattern_cards;
  }

 private:
  int RelativeRank(int card, int suit) const {
    return ACE - all_cards.Suit(suit).Slice(0, card).Size();
  }
};

struct Stat {
  int num_visits = 0;
  int num_branches = 0;
  int num_cutoff_collisions = 0;
  int num_cuts = 0;
  int num_cached_cuts = 0;
  int num_new_cuts = 0;
  int num_missed_cuts = 0;

  void Show(int depth) const {
    if (num_visits)
      printf("%2d: %7d * %.2f  Cutoff collisions %d  cuts %7d  cached %7d  new %5d  missed %5d\n",
             depth, num_visits, double(num_branches) / num_visits, num_cutoff_collisions, num_cuts,
             num_cached_cuts, num_new_cuts, num_missed_cuts);
  }
} stats[TOTAL_CARDS];

class Play {
 public:
  Play() {}
  Play(Play* plays, Trick* trick, Hands& hands, int trump, int depth, int seat_to_play)
      : plays(plays),
        trick(trick),
        hands(hands),
        trump(trump),
        depth(depth),
        seat_to_play(seat_to_play) {}

  typedef std::pair<int, Cards> Result;  // NS tricks and rank winners

  Result Search(int beta) {
    switch (depth & 3) {
      case 0: return SearchWithCache(beta);
      case 1: return SearchFollow<1>(beta);
      case 2: return SearchFollow<2>(beta);
      default: return SearchFollow<3>(beta);
    }
  }

  // The code of a play after the lead is specialized by POS, its position in the trick
  // (1 second .. 3 last); POS -1, the default of the helpers below, reads it from depth.
  template <int POS>
  Result SearchFollow(int beta) {
    ns_tricks_won = PreviousPlay().ns_tricks_won;
    seat_to_play = PreviousPlay().NextSeat();
    return EvaluatePlayableCards<POS>(beta);
  }

  // Searches the play after this one, which is at POS.
  template <int POS>
  Result SearchNext(int beta) {
    if constexpr (POS == 3)
      return NextPlay().SearchWithCache(beta);
    else
      return NextPlay().template SearchFollow<POS + 1>(beta);
  }

  // Split out so non-trick-start calls skip this large frame's prologue. Only
  // matters natively; wasm (Binaryen inlines it back) has cheap prologues.
#if defined(__GNUC__) && !defined(__clang__)
  // Pins the pattern-scan loop within 64-byte lines; a shift by unrelated
  // code once cost ~10% on deals/freak/deal.2.
  __attribute__((noinline, optimize("align-loops=32")))
#else
  __attribute__((noinline))
#endif
  Result SearchWithCache(int beta) {
    if (depth > 0) {
      ns_tricks_won = PreviousPlay().ns_tricks_won + PreviousPlay().NsWon();
      seat_to_play = PreviousPlay().WinningSeat();
    }

    if (ns_tricks_won >= beta) return {ns_tricks_won, {}};
    const int remaining_tricks = hands.num_tricks();
    if (ns_tricks_won + remaining_tricks < beta) return {ns_tricks_won + remaining_tricks, {}};

    if (remaining_tricks == 1) return CollectLastTrick();

    trick->all_cards = hands.all_cards();
    ComputeShape();
    trick->ComputeRelativeHands(hands);

    const auto shape_hash = common_bounds_cache.Hash(trick->shape.Value());
    auto* shape_entry = common_bounds_cache.Lookup(shape_hash);
    if (shape_entry) {
      auto pattern = Pattern::Lookup(shape_entry->patterns[seat_to_play], trick->relative_hands,
                                     beta - ns_tricks_won);
      if (pattern) {
        auto rank_winners = pattern->GetRankWinners(trick->all_cards);
        VERBOSE(ShowPattern("match", *pattern, trick->shape));
        int lower = pattern->bounds.lower + ns_tricks_won;
        if (lower >= beta) {
          VERBOSE(printf("%2d: beta cut %d\n", depth, lower));
          return {lower, rank_winners};
        }
        int upper = pattern->bounds.upper + ns_tricks_won;
        VERBOSE(printf("%2d: alpha cut %d\n", depth, upper));
        return {upper, rank_winners};
      }
    }

    equivalence_runs = Cards();
    auto [ns_tricks, rank_winners] = SearchWithSureTricks(beta);
    auto bounds = ns_tricks < beta
                      ? Bounds{0, char(ns_tricks - ns_tricks_won)}
                      : Bounds{char(ns_tricks - ns_tricks_won), char(remaining_tricks)};

    auto pattern_cards = trick->ComputePatternCards(hands, rank_winners, equivalence_runs);
    Pattern new_pattern(trick->relative_hands.Keep(
                            PackBits(pattern_cards.Value(), trick->all_cards.Value())),
                        bounds);
    VERBOSE(ShowPattern("update", new_pattern, trick->shape));
    auto* new_shape_entry = common_bounds_cache.Update(shape_hash);
#ifdef _DEBUG
    new_shape_entry->shape = trick->shape;
#endif
    Pattern::Update(new_shape_entry->patterns[seat_to_play], new_pattern);
    return {ns_tricks, pattern_cards};
  }

 private:
  Result SearchWithSureTricks(int beta) {
    bool with_trumps = trump != NOTRUMP && trick->all_cards.Suit(trump);

    int side_tricks = 0;
    Cards side_rank_winners;
    auto [lead_tricks, lead_rank_winners] = FastTricks(15, &side_tricks, &side_rank_winners);
    const bool from_fast_tricks = lead_tricks > 0;
    if (lead_tricks == 0 && with_trumps)
      std::tie(lead_tricks, lead_rank_winners) = SlowTrumpTricks(
          MySuit(trump), PdSuit(trump), LhoSuit(trump), RhoSuit(trump), /*leading=*/true);
    if (with_trumps) {
      int length_tricks =
          TrumpLengthTricks(MySuit(trump), PdSuit(trump), LhoSuit(trump), RhoSuit(trump));
      if (length_tricks > 0 && length_tricks >= lead_tricks) {
        lead_tricks = length_tricks;
        lead_rank_winners = {};
      }
    }
    auto ns_tricks = SureTrickCutoff(beta, lead_tricks, NsToPlay(), /*leading=*/true);
    if (ns_tricks >= 0 && from_fast_tricks && lead_rank_winners) {
      // The cut may need fewer tricks than there are: keep only the rank winners of the
      // tricks it needs, for a pattern that matches more positions.
      int need =
          NsToPlay() ? beta - ns_tricks_won : ns_tricks_won + hands.num_tricks() - beta + 1;
      if (MinimizeFastTricks(need, with_trumps, lead_tricks, lead_rank_winners))
        ns_tricks = SureTrickCutoff(beta, lead_tricks, NsToPlay(), /*leading=*/true);
    }
    if (ns_tricks >= 0) return {ns_tricks, lead_rank_winners};

    // Tricks each side still needs to decide this search.
    const int remaining_tricks = hands.num_tricks();
    const int lead_need =
        NsToPlay() ? beta - ns_tricks_won : remaining_tricks - (beta - ns_tricks_won) + 1;
    const int other_need = remaining_tricks - lead_need + 1;
    if (with_trumps) {
      const int lead_gap = lead_need - lead_tricks;
      // Side-suit fast tricks, then the trump tricks one hand has by rank. That hand
      // keeps its trumps meanwhile if it has enough other cards to play.
      if (lead_gap <= MAX_RANK_TRICK_GAP && lead_need > side_tricks) {
        Cards opp_trumps = LhoSuit(trump).Union(RhoSuit(trump));
        for (int seat : {seat_to_play, Partner()}) {
          Cards hand_trumps = hands[seat].Suit(trump);
          if (hands[seat].Size() - hand_trumps.Size() < side_tricks) continue;
          auto [trump_tricks, trump_rank_winners] =
              TrumpRankTricks(hand_trumps, opp_trumps, lead_need - side_tricks);
          if (trump_tricks > 0) {
            const int cut =
                SureTrickCutoff(beta, side_tricks + trump_tricks, NsToPlay(), /*leading=*/true);
            CHECK(cut >= 0);
            return {cut, side_rank_winners.Union(trump_rank_winners)};
          }
        }
      }
      if (lead_gap <= MAX_DRAW_TRUMP_GAP) {
        auto [draw_tricks, draw_rank_winners] = DrawTrumpsThenCash();
        if (draw_tricks >= lead_need) {
          const int cut = SureTrickCutoff(beta, draw_tricks, NsToPlay(), /*leading=*/true);
          CHECK(cut >= 0);
          return {cut, draw_rank_winners};
        }
      }
    }

    auto [other_tricks, other_rank_winners] =
        with_trumps ? TopTrumpTricks(LhoSuit(trump), RhoSuit(trump)) : OtherSlowNoTrumpTricks();
    if (!with_trumps && other_tricks == 0 && other_need == 1)
      std::tie(other_tricks, other_rank_winners) = CannotTakeAllNoTrump(seat_to_play);
    if (other_tricks == 0 && with_trumps)
      std::tie(other_tricks, other_rank_winners) = SlowTrumpTricks(
          LhoSuit(trump), RhoSuit(trump), PdSuit(trump), MySuit(trump), /*leading=*/false);
    if (with_trumps) {
      int length_tricks =
          TrumpLengthTricks(LhoSuit(trump), RhoSuit(trump), PdSuit(trump), MySuit(trump));
      if (length_tricks > 0 && length_tricks >= other_tricks) {
        other_tricks = length_tricks;
        other_rank_winners = {};
      }
      if (other_need - other_tricks == 1) {
        Cards our_trumps = MySuit(trump).Union(PdSuit(trump));
        for (int seat : {LeftHandOpp(), RightHandOpp()}) {
          auto [trump_tricks, trump_rank_winners] =
              TrumpRankTricks(hands[seat].Suit(trump), our_trumps, other_need);
          if (trump_tricks > 0) {
            other_tricks = trump_tricks;
            other_rank_winners = trump_rank_winners;
            break;
          }
        }
      }
    }
    ns_tricks = SureTrickCutoff(beta, other_tricks, !NsToPlay(), /*leading=*/false);
    if (ns_tricks >= 0) return {ns_tricks, other_rank_winners};

    return EvaluatePlayableCards<0>(beta);
  }

  int SureTrickCutoff(int beta, int sure_tricks, bool for_ns, bool leading) const {
    if (for_ns) {
      auto lower = ns_tricks_won + sure_tricks;
      if (lower >= beta) {
        VERBOSE(printf("%2d: beta %s cut %d+%d\n", depth, leading ? "lead" : "other", ns_tricks_won,
                       sure_tricks));
        return lower;
      }
    } else {
      auto upper = ns_tricks_won + (hands.num_tricks() - sure_tricks);
      if (upper < beta) {
        VERBOSE(printf("%2d: alpha %s cut %d+%d\n", depth, leading ? "lead" : "other",
                       ns_tricks_won, hands.num_tricks() - sure_tricks));
        return upper;
      }
    }
    return -1;
  }

  // Plays out the trick with each follower's first-ordered card and moves leads whose next
  // trick start cuts for the leader to the front.
  void PromoteProbedLeads(int start, int beta) {
    int hits[TOTAL_TRICKS], misses[TOTAL_TRICKS], num_hits = 0, num_misses = 0;
    for (int i = start; i < ordered_cards.Size(); ++i) {
      int card = ordered_cards.Card(i);
      PlayCard(card);
      int value = NextPlay().ProbeFollow(beta);
      UnplayCard();
      bool hit = value >= 0 && (NsToPlay() ? value >= beta : value < beta);
      if (hit)
        hits[num_hits++] = card;
      else
        misses[num_misses++] = card;
    }
    if (num_hits == 0) return;
    ordered_cards.Truncate(start);
    for (int i = 0; i < num_hits; ++i) ordered_cards.AddCard(hits[i]);
    for (int i = 0; i < num_misses; ++i) ordered_cards.AddCard(misses[i]);
  }

  // Returns NS tricks if the greedy line reaches a trick start with a cutting pattern, else -1.
  int ProbeFollow(int beta) {
    if (!TrickStarting()) {
      ns_tricks_won = PreviousPlay().ns_tricks_won;
      seat_to_play = PreviousPlay().NextSeat();
      auto playable_cards = GetPlayableCards();
      int card = LookupCutoffCard(cutoff_cache.Hash(BuildCutoffIndex(beta)));
      if (!playable_cards.Include(card)) {
        ordered_cards.Reset();
        OrderCards(playable_cards, beta);
        card = ordered_cards.Card(0);
      }
      PlayCard(card);
      int value = NextPlay().ProbeFollow(beta);
      UnplayCard();
      return value;
    }
    // TODO: Share this trick-start setup with Search() and SetupTrick().
    ns_tricks_won = PreviousPlay().ns_tricks_won + PreviousPlay().NsWon();
    seat_to_play = PreviousPlay().WinningSeat();
    const int remaining_tricks = hands.num_tricks();
    if (ns_tricks_won >= beta) return ns_tricks_won;
    if (ns_tricks_won + remaining_tricks < beta) return ns_tricks_won + remaining_tricks;
    trick->all_cards = hands.all_cards();
    ComputeShape();
    trick->ComputeRelativeHands(hands);
    if (auto* shape_entry =
            common_bounds_cache.Lookup(common_bounds_cache.Hash(trick->shape.Value())))
      if (auto pattern = Pattern::Lookup(shape_entry->patterns[seat_to_play], trick->relative_hands,
                                         beta - ns_tricks_won)) {
        int lower = pattern->bounds.lower + ns_tricks_won;
        return lower >= beta ? lower : pattern->bounds.upper + ns_tricks_won;
      }
    return -1;
  }

  // Second seat (from DDS QuickTricksSecondHand): our side surely wins this trick, by a ruff
  // the third hand cannot overruff or by a card above the lead and all of the third hand's
  // cards in the suit. In notrump the winner then cashes its top cards. Returns true with a
  // bound in `result` if that is enough for a cut.
  //
  // The rank winners stand on their own: the claim holds wherever the cards from the top of
  // each suit down to them are in the same hands, so it relies on no equivalence run.
  bool SecondSeatSureTricks(int beta, Result& result) const {
    const int lead = PreviousPlay().card_played, suit = SuitOf(lead);
    const Cards my_hand = hands[seat_to_play], pd_hand = hands[Partner()];
    const Cards third_hand = hands[LeftHandOpp()];
    const Cards my_suit = my_hand.Suit(suit), pd_suit = pd_hand.Suit(suit);
    const Cards third_suit = third_hand.Suit(suit);
    const bool side_suit = trump != NOTRUMP && suit != trump;
    if (side_suit && !third_suit && third_hand.Suit(trump)) return false;  // third hand ruffs
    Cards rank_winners;
    const Cards ours = my_suit.Union(pd_suit);
    if (side_suit && ((!my_suit && my_hand.Suit(trump)) || (!pd_suit && pd_hand.Suit(trump)))) {
      // We ruff; the third hand follows suit or has no trump. No rank matters.
    } else {
      if (!ours) return false;
      const int top = ours.Top();
      if (!HigherRank(top, lead) || (third_suit && !HigherRank(top, third_suit.Top())))
        return false;
      rank_winners.Add(top);
    }
    const int remaining = my_hand.Size();
    const int needed =
        NsToPlay() ? beta - ns_tricks_won : remaining - (beta - ns_tricks_won) + 1;
    int sure_tricks = 1;
    if (sure_tricks < needed) {
      if (trump != NOTRUMP) return false;
      // Notrump: the hand that wins this trick leads to the next one and cashes.
      const Cards winner_hand =
          !pd_suit || (my_suit && HigherRank(my_suit.Top(), pd_suit.Top())) ? my_hand : pd_hand;
      const Cards all_cards = trick->all_cards;  // cards at the start of this trick
      const Cards all_suit = all_cards.Suit(suit);
      const int top = all_suit.Top();
      if (winner_hand.Include(top)) {
        const Cards rest = all_suit.Different(Cards().Add(top));
        if (rest && winner_hand.Include(rest.Top())) {
          ++sure_tricks;
          rank_winners.Add(rest.Top());
        }
      }
      for (int s = 0; s < NUM_SUITS && sure_tricks < needed; ++s) {
        if (s == suit) continue;
        const Cards hand_suit = winner_hand.Suit(s);
        if (!hand_suit) continue;
        // Only the lead has been played, so the other suits are as at the trick start.
        const Cards all_s = all_cards.Suit(s);
        if (all_s == hand_suit) {
          sure_tricks += hand_suit.Size();
        } else if (hand_suit.Include(all_s.Top())) {
          ++sure_tricks;
          rank_winners.Add(all_s.Top());
        }
      }
      if (sure_tricks < needed) return false;
    }
    result = {NsToPlay() ? ns_tricks_won + sure_tricks : ns_tricks_won + remaining - sure_tricks,
              rank_winners};
    return true;
  }

  template <int POS>
  Result EvaluatePlayableCards(int beta) {
    STATS(++stats[depth].num_visits);
    if (At<POS>(1)) {
      Result result;
      if (SecondSeatSureTricks(beta, result)) {
        equivalence_runs = Cards();  // no card was skipped as equivalent to another
        return result;
      }
    }
    auto playable_cards = GetPlayableCards<POS>();
    VERBOSE(printf("%2d: all %lx playable %lx\n", depth, hands.all_cards().Value(),
                   playable_cards.Value()));
    // Plays a card, searches the rest and takes the card back.
    auto search_card = [this, beta](int card) -> Result {
      STATS(++stats[depth].num_branches);
      PlayCard<POS>(card);
      VERBOSE(ShowTricks(beta, 0, true));
      Result result = SearchNext<POS>(beta);
      if (At<POS>(3)) result.second.Add(GetTrickRankWinner());
      VERBOSE(ShowTricks(beta, result.first, false));
      UnplayCard();
      return result;
    };
    // One choice (a single card, or equivalent cards in one suit): skip the cutoff cache.
    int top = playable_cards.Top(), bottom = playable_cards.Bottom();
    if (SuitOf(top) == SuitOf(bottom) &&
        trick->all_cards.Slice(top, bottom + 1) == playable_cards) {
      Result result = search_card(top);
      equivalence_runs = NextRuns<POS>();
      // A failure relies on the other cards of the run failing like the one played.
      if (top != bottom && (NsToPlay() ? result.first < beta : result.first >= beta))
        equivalence_runs.Add(Cards(~0ULL).Slice(top, bottom));
      return result;
    }

    const auto cutoff_hash = cutoff_cache.Hash(BuildCutoffIndex<POS>(beta));
    const int cutoff_card = LookupCutoffCard<POS>(cutoff_hash);
    int card;
    if (playable_cards.Include(cutoff_card)) {
      VERBOSE(printf("%2d: use cutoff %s\n", depth, NameOf(cutoff_card)));
      card = cutoff_card;
      playable_cards.Remove(cutoff_card);
    } else {
      STATS(stats[depth].num_cutoff_collisions += (cutoff_card != CARD_END));
      ordered_cards.Reset();
      OrderCards<POS>(playable_cards, beta);
      playable_cards = Cards();
      card = ordered_cards.Card(0);
    }

    // The first card: every rank is still relevant and no card was tried. It is tried outside
    // the loop below as it cuts off most of the time, often straight from the cut-off cache.
    const bool ns_to_play = NsToPlay();
    auto is_cutoff = [ns_to_play, beta](int tricks) {
      return ns_to_play ? tricks >= beta : tricks < beta;
    };
    auto [ns_tricks, rank_winners] = search_card(card);
    if (is_cutoff(ns_tricks)) {
      STATS(++stats[depth].num_cuts);
      if (card != cutoff_card) {
        SaveCutoffCard<POS>(cutoff_hash, card);
        STATS(stats[depth].num_new_cuts += (cutoff_card == CARD_END));
        STATS(stats[depth].num_missed_cuts += (cutoff_card != CARD_END));
      } else {
        STATS(++stats[depth].num_cached_cuts);
      }
      VERBOSE(printf("%2d: search cut @1 %s\n", depth, NameOf(card)));
      // One card that works is enough: only its own play relies on equivalence runs.
      equivalence_runs = NextRuns<POS>();
      return {ns_tricks, rank_winners};
    }
    // The equivalence runs a failure of every card relies on.
    Cards runs = NextRuns<POS>();

    int min_relevant_ranks[NUM_SUITS] = {TWO, TWO, TWO, TWO};
    // If a card's rank is irrelevant, a relevant rank must be higher.
    auto update_relevant_ranks = [&min_relevant_ranks](int card, Cards branch_rank_winners) {
      int suit = SuitOf(card);
      auto suit_rank_winners = branch_rank_winners.Suit(suit);
      if (!suit_rank_winners)
        min_relevant_ranks[suit] = NUM_RANKS;
      else if (LowerRank(card, suit_rank_winners.Bottom()))
        min_relevant_ranks[suit] =
            std::max(min_relevant_ranks[suit], RankOf(suit_rank_winners.Bottom()));
    };
    update_relevant_ranks(card, rank_winners);
    Cards tried_cards;
    tried_cards.Add(card);
    if (playable_cards) {  // the cut-off card didn't cut: order the other cards after it
      ordered_cards.Reset();
      ordered_cards.AddCard(card);
      OrderCards<POS>(playable_cards, beta);
    }
    for (int i = 1; i < ordered_cards.Size(); ++i) {
      card = ordered_cards.Card(i);
      int suit = SuitOf(card), rank = RankOf(card);
      // Try a card if its rank is still relevant and it isn't equivalent to a tried card.
      Cards equivalent_run;
      if (rank >= min_relevant_ranks[suit]) {
        equivalent_run = trick->EquivalentRun(card, tried_cards.Suit(suit), hands[seat_to_play]);
        runs.Add(equivalent_run);
      }
      if (rank >= min_relevant_ranks[suit] && !equivalent_run) {
        auto [branch_ns_tricks, branch_rank_winners] = search_card(card);
        ns_tricks = ns_to_play ? std::max(ns_tricks, branch_ns_tricks)
                               : std::min(ns_tricks, branch_ns_tricks);
        if (is_cutoff(ns_tricks)) {
          STATS(++stats[depth].num_cuts);
          if (card != cutoff_card) {
            SaveCutoffCard<POS>(cutoff_hash, card);
            STATS(stats[depth].num_new_cuts += (cutoff_card == CARD_END));
            STATS(stats[depth].num_missed_cuts += (cutoff_card != CARD_END));
          } else {
            STATS(++stats[depth].num_cached_cuts);
          }
          VERBOSE(printf("%2d: search cut @%d %s\n", depth, i + 1, NameOf(card)));
          // One card that works is enough: only its own play relies on equivalence runs.
          equivalence_runs = NextRuns<POS>();
          return {ns_tricks, branch_rank_winners};
        }
        runs.Add(NextRuns<POS>());
        rank_winners.Add(branch_rank_winners);
        update_relevant_ranks(card, branch_rank_winners);
      }
      tried_cards.Add(card);
    }
    equivalence_runs = runs;
    return {ns_tricks, rank_winners};
  }

  template <bool SUIT_CONTRACT>
  void Lead(Cards playable_cards) {
    Cards good_leads, high_leads, high_leads_low, leads, bad_leads, trump_leads, ruff_leads,
        void_leads;
    auto pd_hand = hands[Partner()];
    auto lho_hand = hands[LeftHandOpp()], rho_hand = hands[RightHandOpp()];
    for (int suit = 0; suit < NUM_SUITS; ++suit) {
      auto my_suit = playable_cards.Suit(suit);
      if (!my_suit) continue;
      if (SUIT_CONTRACT) {
        // Trump suit - oddly bad.
        if (suit == trump) {
          trump_leads.Add(my_suit.Top());
          trump_leads.Add(my_suit.Bottom());
          continue;
        }
        // Opponents can ruff - very bad.
        if ((lho_hand.Suit(trump) && !lho_hand.Suit(suit)) ||
            (rho_hand.Suit(trump) && !rho_hand.Suit(suit)))
          continue;
      }
      auto pd_suit = pd_hand.Suit(suit), our_suits = my_suit.Union(pd_suit);
      auto lho_suit = lho_hand.Suit(suit);
      auto all_suit_cards = trick->all_cards.Suit(suit);
      int a = all_suit_cards.Top();
      int k = all_suit_cards.Remove(a).Top();
      int q = all_suit_cards.Remove(k).Top();
      int j = all_suit_cards.Remove(q).Top();
      int t = all_suit_cards.Remove(j).Top();
      // Finesse LHO - good.
      if (pd_suit.Size() >= 2 && lho_suit.Size() >= 2) {
        if ((pd_suit.Include(k) && lho_suit.Include(a)) ||
            (pd_suit.Include(a) && lho_suit.Include(k) &&
             (pd_suit.Include(q) || our_suits.Include(Cards().Add(q).Add(j)))) ||
            (pd_suit.Include(k) && lho_suit.Include(q) &&
             (pd_suit.Include(j) || our_suits.Include(Cards().Add(j).Add(t))))) {
          good_leads.Add(my_suit.Top());
          good_leads.Add(my_suit.Bottom());
          continue;
        }
      }
      auto rho_suit = rho_hand.Suit(suit);
      // Give free finesse to RHO - bad.
      if (my_suit.Size() >= 2 && rho_suit.Size() >= 2) {
        if ((my_suit.Include(a) && rho_suit.Include(k)) ||
            (my_suit.Include(k) && rho_suit.Include(a) && !our_suits.Include(q))) {
          if (SUIT_CONTRACT) {
            bad_leads.Add(my_suit.Top());
            continue;
          }
        }
      }
      Cards akq = Cards().Add(a).Add(k).Add(q);
      // Lead from strong suit - ok. Low first toward partner's A or K.
      if (lho_suit && rho_suit && our_suits.Intersect(akq).Size() >= 2) {
        auto& suit_leads = pd_suit.Include(a) || pd_suit.Include(k) ? high_leads_low : high_leads;
        suit_leads.Add(my_suit.Top());
        suit_leads.Add(my_suit.Bottom());
        continue;
      }
      // Give partner a ruff - good.
      if (SUIT_CONTRACT && !pd_suit && lho_suit && rho_suit && pd_hand.Suit(trump) &&
          pd_hand.Suit(trump).Size() <= playable_cards.Suit(trump).Size() &&
          my_suit.Bottom() != a) {
        ruff_leads.Add(my_suit.Bottom());
        continue;
      }
      // With voids in other players' hands.
      if (!pd_suit || !lho_suit || !rho_suit) {
        void_leads.Add(my_suit.Top());
        void_leads.Add(my_suit.Bottom());
        continue;
      }
      // Nothing special.
      leads.Add(my_suit.Top());
      leads.Add(my_suit.Bottom());
    }
    if (SUIT_CONTRACT) {
      ordered_cards.AddCards(ruff_leads);
      playable_cards.Remove(ruff_leads);
    }
    ordered_cards.AddCards(good_leads);
    playable_cards.Remove(good_leads);
    ordered_cards.AddReversedCards(high_leads_low);
    playable_cards.Remove(high_leads_low);
    ordered_cards.AddCards(high_leads);
    playable_cards.Remove(high_leads);
    ordered_cards.AddCards(leads);
    playable_cards.Remove(leads);
    ordered_cards.AddCards(void_leads);
    playable_cards.Remove(void_leads);
    if (SUIT_CONTRACT) {
      ordered_cards.AddCards(bad_leads);
      playable_cards.Remove(bad_leads);
      ordered_cards.AddCards(trump_leads);
      playable_cards.Remove(trump_leads);
    }
    ordered_cards.AddCards(playable_cards);
  }

  template <int POS = -1>
  void OrderCards(Cards playable_cards, int beta) {
    CHECK(playable_cards);
    if (playable_cards.Size() == 1) {
      ordered_cards.AddCard(playable_cards.Top());
      return;
    }
    if (At<POS>(0)) {  // lead
      const int start = ordered_cards.Size();
      trump == NOTRUMP ? Lead<false>(playable_cards) : Lead<true>(playable_cards);
      if (hands.num_tricks() >= 12) PromoteProbedLeads(start, beta);
      return;
    }
    int winning_seat = PreviousPlay().WinningSeat();
    int winning_card = PreviousPlay().WinningCard();
    Cards pd_suit = hands[Partner()].Suit(LeadSuit());
    Cards lho_suit = hands[LeftHandOpp()].Suit(LeadSuit());
    if (playable_cards.Suit(LeadSuit())) {  // follow
      if (!WinOver(playable_cards.Top(), winning_card))
        return ordered_cards.AddReversedCards(playable_cards);
      if (winning_seat == Partner() &&
          (At<POS>(3) || !lho_suit || HigherRank(winning_card, lho_suit.Top()) ||
           lho_suit.Slice(0, winning_card) == lho_suit.Slice(0, playable_cards.Top())))
        // Partner can win or force LHO's winner.
        return ordered_cards.AddReversedCards(playable_cards);
      if (At<POS>(1) && pd_suit && HigherRank(pd_suit.Top(), winning_card)) {
        if (lho_suit && HigherRank(lho_suit.Top(), pd_suit.Union(playable_cards).Top()) &&
            lho_suit.Slice(0, pd_suit.Top()) == lho_suit.Slice(0, playable_cards.Top()))
          // Play low as LHO may play a winner to prevent partner from winning.
          return ordered_cards.AddReversedCards(playable_cards);
        if (!lho_suit || HigherRank(pd_suit.Top(), lho_suit.Top()))
          // Play low as partner can win later.
          return ordered_cards.AddReversedCards(playable_cards);
      }
      auto higher_cards = playable_cards.Slice(0, winning_card);
      if (At<POS>(3) || !lho_suit || HigherRank(higher_cards.Bottom(), lho_suit.Top()))
        ordered_cards.AddReversedCards(higher_cards);
      else
        ordered_cards.AddCards(higher_cards);
      return ordered_cards.AddReversedCards(playable_cards.Different(higher_cards));
    }
    if (trump != NOTRUMP && playable_cards.Suit(trump)) {  // ruff
      if (winning_seat == Partner() &&
          (At<POS>(3) || (lho_suit && WinOver(winning_card, lho_suit.Top())))) {
        // Partner can win.
      } else {
        Cards my_trumps = playable_cards.Suit(trump);
        if (SuitOf(winning_card) == trump) {
          if (winning_seat != Partner() && WinOver(my_trumps.Top(), winning_card)) {
            auto higher_trumps = my_trumps.Slice(my_trumps.Top(), winning_card);
            ordered_cards.AddReversedCards(higher_trumps);
            playable_cards.Remove(higher_trumps);
          }
        } else if (At<POS>(3) || lho_suit || !hands[LeftHandOpp()].Suit(trump)) {
          // The lowest trump is guaranteed to win.
          ordered_cards.AddCard(my_trumps.Bottom());
          playable_cards.Remove(my_trumps.Bottom());
        } else {
          ordered_cards.AddReversedCards(my_trumps);
          playable_cards.Remove(my_trumps);
        }
      }
    }
    // discard
    int num_discards = 0;
    struct {
      int card;
      int weight;
    } discards[NUM_SUITS];
    for (int suit = 0; suit < NUM_SUITS; ++suit) {
      if (suit == trump) continue;
      if (auto my_suit = playable_cards.Suit(suit))
        discards[num_discards++] = {my_suit.Bottom(), my_suit.Size()};
    }
    if (num_discards >= 2) {
      // Sort discards with up to 3 comparisons.
      auto compare_swap = [&discards](int a, int b) {
        if (discards[a].weight < discards[b].weight) std::swap(discards[a], discards[b]);
      };
      compare_swap(0, 1);
      if (num_discards >= 3) {
        compare_swap(1, 2);
        compare_swap(0, 1);
      }
    }
    for (int i = 0; i < num_discards; ++i) {
      ordered_cards.AddCard(discards[i].card);
      playable_cards.Remove(discards[i].card);
    }
    ordered_cards.AddCards(playable_cards);
  }

  class OrderedCards {
   public:
    void Reset() { num_ordered_cards = 0; }

    void AddCard(int card) {
      CHECK(num_ordered_cards < TOTAL_TRICKS);
      ordered_cards[num_ordered_cards++] = card;
    }

    void AddCards(Cards cards) {
      for (int card : cards) AddCard(card);
    }

    void AddReversedCards(Cards cards) {
      while (cards) {
        AddCard(cards.Bottom());
        cards.Remove(cards.Bottom());
      }
    }

    int Size() const { return num_ordered_cards; }
    int Card(int i) const { return ordered_cards[i]; }
    void Truncate(int n) { num_ordered_cards = n; }

   private:
    short num_ordered_cards = 0;
    char ordered_cards[TOTAL_TRICKS];
  };

  void ComputeShape() const {
    if (depth == 0) {
      trick->shape = Shape(hands);
    } else {
      trick->shape = (trick - 1)->shape;
      trick->shape.PlayCards(plays[depth - 4].seat_to_play, plays[depth - 4].card_played,
                             plays[depth - 3].card_played, plays[depth - 2].card_played,
                             plays[depth - 1].card_played);
      CHECK(trick->shape == Shape(hands));
    }
  }

  template <int POS = -1>
  Cards GetPlayableCards() const {
    const Cards& hand = hands[seat_to_play];
    if (At<POS>(0)) return hand;

    Cards suit_cards = hand.Suit(LeadSuit());
    if (suit_cards) return suit_cards;

    if (!options.discard_suit_bottom) return hand;

    Cards playable_cards;
    for (int suit = 0; suit < NUM_SUITS; ++suit) {
      Cards suit_cards = hand.Suit(suit);
      if (!suit_cards) continue;
      if (suit == trump) {
        playable_cards.Add(suit_cards);
      } else {
        // Discard only the bottom card in a suit. It's very rare that discarding
        // a higher ranked card in the same suit is necessary.
        playable_cards.Add(suit_cards.Bottom());
      }
    }
    return playable_cards;
  }

  template <int POS = -1>
  void PlayCard(int card_to_play) {
    // remove played card from hand
    card_played = card_to_play;
    hands[seat_to_play].Remove(card_played);

    // who's winning?
    if (At<POS>(0)) trick->lead_suit = SuitOf(card_played);
    if (At<POS>(0) || WinOver(card_played, PreviousPlay().WinningCard())) {
      winning_play = depth;
    } else {
      winning_play = PreviousPlay().winning_play;
    }
  }

  void UnplayCard() {
    // add played card back to hand
    hands[seat_to_play].Add(card_played);
  }

  template <int POS = -1>
  uint64_t BuildCutoffIndex(int beta) const {
    if (At<POS>(0)) {
      // 52 bits for my hand + 4 bits for partner's shape parity + 4 bits for NS tricks needed
      // in suit contracts (NT loses from splitting entries across MTD passes).
      uint64_t needed = trump == NOTRUMP ? 0 : beta - ns_tricks_won;
      return hands[seat_to_play].PackedValue() + (hands[Partner()].ShapeParity() << TOTAL_CARDS) +
             (needed << (TOTAL_CARDS + 4));
    } else if (auto my_suit = hands[seat_to_play].Suit(LeadSuit())) {
      // 13/52 bits for all cards in suit + 6 bits for winner in the trick + 2 bits for lead seat.
      auto winner = PreviousPlay().WinningCard();
      return trick->all_cards.Suit(LeadSuit()).PackedValue() + (uint64_t(winner) << TOTAL_CARDS) +
             (uint64_t((this - Pos<POS>())->seat_to_play) << (TOTAL_CARDS + 6));
    } else {
      // 52 bits for my hand + 6 bits for winner in the trick + 4 bits for NS tricks needed
      // in suit contracts.
      auto winner = trump == NOTRUMP ? PreviousPlay().WinningSeat() : PreviousPlay().WinningCard();
      uint64_t needed = trump == NOTRUMP ? 0 : beta - ns_tricks_won;
      return hands[seat_to_play].PackedValue() + (uint64_t(winner) << TOTAL_CARDS) +
             (needed << (TOTAL_CARDS + 6));
    }
  }

  template <int POS = -1>
  int LookupCutoffCard(decltype(cutoff_cache)::HashT hash) const {
    const auto* entry = cutoff_cache.Lookup(hash);
    return entry ? entry->card[Pos<POS>()] : CARD_END;
  }

  template <int POS = -1>
  void SaveCutoffCard(decltype(cutoff_cache)::HashT hash, int cutoff_card) const {
    auto* entry = cutoff_cache.Update(hash);
    entry->card[Pos<POS>()] = cutoff_card;
  }

  Result TopTrumpTricks(Cards my_trumps, Cards pd_trumps) const {
    auto all_trumps = trick->all_cards.Suit(trump);
    if (my_trumps == all_trumps) return {my_trumps.Size(), {}};
    if (pd_trumps == all_trumps) return {pd_trumps.Size(), {}};

    auto both_trumps = my_trumps.Union(pd_trumps);
    auto max_trump_tricks = std::max(my_trumps.Size(), pd_trumps.Size());
    int sure_tricks = 0;
    Cards rank_winners;
    for (int card : all_trumps)
      if (both_trumps.Include(card) && sure_tricks < max_trump_tricks) {
        ++sure_tricks;
        rank_winners.Add(card);
      } else
        break;
    return {sure_tricks, rank_winners};
  }

  Result SlowTrumpTricks(Cards my_trumps, Cards pd_trumps, Cards lho_trumps, Cards rho_trumps,
                         bool leading) const {
    auto all_trumps = trick->all_cards.Suit(trump);
    if (all_trumps.Size() >= 3) {
      auto a = Cards().Add(all_trumps.Top());
      auto k = Cards().Add(all_trumps.Different(a).Top());
      // Kx behind A
      if ((pd_trumps.StrictlyInclude(k) && lho_trumps.Include(a)) ||
          (my_trumps.StrictlyInclude(k) && rho_trumps.Include(a) &&
           (!leading || hands.num_tricks() >= 3)))
        return {1, a.Union(k)};
      // Kx against stiff A
      if ((pd_trumps.StrictlyInclude(k) || my_trumps.StrictlyInclude(k)) &&
          (rho_trumps == a || lho_trumps == a) && (!leading || hands.num_tricks() >= 3))
        return {1, a.Union(k)};
      // KQJ against A
      auto q = Cards().Add(all_trumps.Different(a.Union(k)).Top());
      if (all_trumps.Size() >= 4) {
        auto j = Cards().Add(all_trumps.Different(a.Union(k).Union(q)).Top());
        if (lho_trumps.Union(rho_trumps).Include(a) &&
            my_trumps.Union(pd_trumps).Include(k.Union(q).Union(j)) &&
            (my_trumps.Size() >= 3 || pd_trumps.Size() >= 3))
          return {2, a.Union(k).Union(q).Union(j)};
      }
      // KQ against A
      if (lho_trumps.Union(rho_trumps).Include(a) &&
          my_trumps.Union(pd_trumps).Include(k.Union(q)) &&
          (my_trumps.Size() >= 2 || pd_trumps.Size() >= 2))
        return {1, a.Union(k).Union(q)};
      // Qxx behind AK
      if (all_trumps.Size() >= 5)
        if ((pd_trumps.Include(q) && pd_trumps.Size() >= 3 && lho_trumps.Include(a.Union(k))) ||
            (my_trumps.Include(q) && my_trumps.Size() >= 3 && rho_trumps.Include(a.Union(k)) &&
             (!leading || hands.num_tricks() >= 4)))
          return {1, a.Union(k).Union(q)};
      // Qxx against AK tight
      if (((pd_trumps.Include(q) && pd_trumps.Size() >= 3) ||
           (my_trumps.Include(q) && my_trumps.Size() >= 3)) &&
          (lho_trumps == a.Union(k) || rho_trumps == a.Union(k) ||
           (lho_trumps.Size() == 1 && rho_trumps.Size() == 1)) &&
          (!leading || hands.num_tricks() >= 4))
        return {1, a.Union(k).Union(q)};
    }
    return {0, {}};
  }

  // Each of our trumps wins a trick unless an opponent's trump is in it.
  // Trump lengths are in the shape, so this needs no rank winners.
  static int TrumpLengthTricks(Cards my_trumps, Cards pd_trumps, Cards lho_trumps,
                               Cards rho_trumps) {
    return std::max(my_trumps.Size(), pd_trumps.Size()) - lho_trumps.Size() - rho_trumps.Size();
  }

  Result OtherSlowNoTrumpTricks() const {
    auto my_hand = hands[seat_to_play];
    auto our_hands = my_hand.Union(hands[Partner()]);
    Cards rank_winners;
    for (int suit = 0; suit < NUM_SUITS; ++suit) {
      if (!my_hand.Suit(suit)) continue;
      auto top = trick->all_cards.Suit(suit).Top();
      if (our_hands.Include(top)) return {0, {}};
      rank_winners.Add(top);
    }
    if (hands[LeftHandOpp()].Include(rank_winners) || hands[RightHandOpp()].Include(rank_winners)) {
      return {rank_winners.Size(), rank_winners};
    } else
      return {1, rank_winners};
  }

  // No trumps (or none left), the side of a_seat on lead: it can't take all
  // the remaining tricks, so the other side gets one. Every trick is won in
  // the suit led, and a side wins a suit at most as often as the longer of its
  // two holdings.
  // 1. The other side keeps the top cards of the suits it tops, so the side
  //    wins tricks only in the suits it tops itself: too short, and it fails.
  // 2. With stoppers: in a suit the side tops, the other side's highest card
  //    stops it if its holder plays after the hand with all the higher cards
  //    (no finesse) and has a lower card to play on each of them; the side
  //    then wins that suit only as often as it has higher cards. A hand of the
  //    other side keeps a card for each top card or stopper it is in charge
  //    of, and has spare cards to play on the side's winners as long as
  //    winners + cards in charge <= tricks.
  // The rank winners are the top cards of the other side's suits and, for a
  // stopped suit, its cards down to the stopper.
  Result CannotTakeAllNoTrump(int a_seat) const {
    const int b_seat[2] = {(a_seat + 1) & (NUM_SEATS - 1), (a_seat + 3) & (NUM_SEATS - 1)};
    Cards a1 = hands[a_seat], a2 = hands[(a_seat + 2) & (NUM_SEATS - 1)];
    Cards a_cards = a1.Union(a2);
    int lengths = 0, winners = 0, in_charge[2] = {0, 0};
    Cards top_rank_winners, stopper_rank_winners;
    for (int suit = 0; suit < NUM_SUITS; ++suit) {
      auto all_suit = trick->all_cards.Suit(suit);
      if (!all_suit) continue;
      int top = all_suit.Top();
      if (!a_cards.Include(top)) {
        ++in_charge[hands[b_seat[0]].Include(top) ? 0 : 1];
        top_rank_winners.Add(top);
        continue;
      }
      Cards a_suit = a_cards.Suit(suit);
      int max_length = std::max(a1.Suit(suit).Size(), a2.Suit(suit).Size());
      lengths += max_length;
      if (Cards b_suit = all_suit.Different(a_suit)) {
        int stopper = b_suit.Top();
        int holder = hands[b_seat[0]].Include(stopper) ? 0 : 1;
        // The hand playing just before the stopper's holder.
        Cards before = hands[(b_seat[holder] + 3) & (NUM_SEATS - 1)];
        Cards higher = a_suit.Slice(0, stopper);
        if (before.Include(higher) && hands[b_seat[holder]].Suit(suit).Size() > higher.Size()) {
          winners += higher.Size();
          ++in_charge[holder];
          stopper_rank_winners.Add(all_suit.Slice(0, stopper + 1));
          continue;
        }
      }
      winners += max_length;
    }
    if (lengths < hands.num_tricks()) return {1, top_rank_winners};
    int most = std::max(in_charge[0], in_charge[1]);
    if (most > 0 && winners + most <= hands.num_tricks())
      return {1, top_rank_winners.Union(stopper_rank_winners)};
    return {0, {}};
  }

  // The fast tricks of a notrump position given by four hands, me on lead.
  Result FastTricksNoTrump(Cards my_hand, Cards pd_hand, Cards lho_hand, Cards rho_hand) const {
    Cards all_cards = my_hand.Union(pd_hand).Union(lho_hand).Union(rho_hand);
    Cards rank_winners, pd_rank_winners;
    int my_tricks = 0, pd_tricks = 0;
    bool my_entry = false, pd_entry = false;
    for (int suit = 0; suit < NUM_SUITS; ++suit) {
      auto my_suit = my_hand.Suit(suit);
      auto pd_suit = pd_hand.Suit(suit);
      int opp_max = std::max(lho_hand.Suit(suit).Size(), rho_hand.Suit(suit).Size());
      int my_max_rank_winners = std::max(pd_suit.Size(), opp_max);
      int pd_max_rank_winners = std::max(my_suit.Size(), opp_max);
      int my_winners = 0, pd_winners = 0;
      for (int card : all_cards.Suit(suit))
        if (my_suit.Include(card)) {
          ++my_winners;
          if (my_winners <= my_max_rank_winners) rank_winners.Add(card);
        } else if (pd_suit.Include(card)) {
          ++pd_winners;
          if (pd_winners <= pd_max_rank_winners) pd_rank_winners.Add(card);
        } else
          break;
      my_tricks +=
          SuitFastTricks(my_suit, my_winners, my_max_rank_winners, my_entry, pd_suit, pd_winners);
      pd_tricks +=
          SuitFastTricks(pd_suit, pd_winners, pd_max_rank_winners, pd_entry, my_suit, my_winners);
    }
    int fast_tricks = my_tricks;
    if (pd_entry) {
      fast_tricks = std::max(my_tricks, pd_tricks);
      rank_winners.Add(pd_rank_winners);
    }
    return {std::min(fast_tricks, my_hand.Size()), rank_winners};
  }

  // Draws the opponents' trumps in `rounds` rounds with the top trumps `run`,
  // all ours, whichever of us holds them, then counts the notrump fast tricks
  // of the hand left on lead. Each round the hand on lead leads its lowest
  // trump of the run, or its lowest trump to partner's lowest of the run.
  Result DrawTrumpsWithRun(Cards run, int rounds) const {
    Cards hand[2] = {hands[seat_to_play], hands[Partner()]};
    int discards[2] = {0, 0};
    int leader = 0;
    for (int r = 0; r < rounds; ++r) {
      int follower = 1 - leader;
      Cards lead_trumps = hand[leader].Suit(trump), follow_trumps = hand[follower].Suit(trump);
      if (!lead_trumps) return {0, {}};
      if (!follow_trumps) ++discards[follower];
      if (Cards lead_run = lead_trumps.Intersect(run)) {
        int lead_card = lead_run.Bottom();
        hand[leader].Remove(lead_card);
        if (follow_trumps) {
          int follow_card = follow_trumps.Bottom();
          hand[follower].Remove(follow_card);
          // The follower has only higher trumps: it wins the trick.
          if (HigherRank(follow_card, lead_card)) leader = follower;
        }
      } else {
        Cards follow_run = follow_trumps.Intersect(run);
        if (!follow_run) return {0, {}};
        hand[leader].Remove(lead_trumps.Bottom());
        hand[follower].Remove(follow_run.Bottom());
        leader = follower;
      }
    }
    // A hand out of trumps discarded the lowest cards of its longest side suits.
    for (int h = 0; h < 2; ++h)
      for (; discards[h] > 0; --discards[h]) {
        int longest = -1, length = 0;
        for (int suit = 0; suit < NUM_SUITS; ++suit)
          if (suit != trump && hand[h].Suit(suit).Size() > length) {
            length = hand[h].Suit(suit).Size();
            longest = suit;
          }
        if (longest < 0) return {0, {}};
        hand[h].Remove(hand[h].Suit(longest).Bottom());
      }
    Cards lho_hand = hands[LeftHandOpp()], rho_hand = hands[RightHandOpp()];
    lho_hand.ClearSuit(trump);
    rho_hand.ClearSuit(trump);
    auto [tricks, rank_winners] =
        FastTricksNoTrump(hand[leader], hand[1 - leader], lho_hand, rho_hand);
    return {std::min(rounds + tricks, hands.num_tricks()), rank_winners.Union(run)};
  }

  // We hold the top trumps needed to draw all the opponents' trumps: draw them,
  // then the rest is a notrump position in which our other trumps are winners.
  Result DrawTrumpsThenCash() const {
    int rounds = std::max(LhoSuit(trump).Size(), RhoSuit(trump).Size());
    if (rounds == 0) return {0, {}};
    Cards ours = MySuit(trump).Union(PdSuit(trump));
    Cards all_trumps = trick->all_cards.Suit(trump);
    Cards run = KeepTop(all_trumps, rounds);
    if (!ours.Include(run)) return {0, {}};
    auto result = DrawTrumpsWithRun(run, rounds);
    if (result.first > 0) return result;
    // Two of the top trumps may fall together: retry with up to twice as many.
    Cards long_run;
    int count = 0;
    for (int card : all_trumps) {
      if (!ours.Include(card) || count == 2 * rounds) break;
      long_run.Add(card);
      ++count;
    }
    if (long_run == run) return {0, {}};
    return DrawTrumpsWithRun(long_run, rounds);
  }

  // The most trumps of `trumps` that higher trumps of `opp_trumps` can beat,
  // each used once.
  static int TrumpKills(Cards trumps, Cards opp_trumps) {
    int kills = 0;
    while (trumps) {
      int low = trumps.Bottom();
      Cards higher = opp_trumps.Slice(0, low);
      if (!higher) break;
      ++kills;
      trumps.Remove(low);
      opp_trumps = higher;
      opp_trumps.Remove(higher.Bottom());
    }
    return kills;
  }

  // Sure trump tricks of one hand by rank, whoever leads. Each trump the hand
  // plays wins the trick for its side unless an opponent plays a higher trump
  // on it, and an opponent's trump does that once. Pins as few top trumps as
  // give `wanted` tricks: below the pinned cards any opposing trump may beat
  // any of the hand's. With no card pinned this is TrumpLengthTricks().
  Result TrumpRankTricks(Cards trumps, Cards opp_trumps, int wanted) const {
    int n = trumps.Size();
    if (wanted <= 0 || n < wanted) return {0, {}};
    if (n - TrumpKills(trumps, opp_trumps) < wanted) return {0, {}};
    Cards all_trumps = trick->all_cards.Suit(trump);
    Cards rest = trumps;
    int num_opp = opp_trumps.Size();
    for (int i = 1; i <= n; ++i) {
      int card = rest.Top();
      rest.Remove(card);
      Cards pinned = all_trumps.Slice(0, card + 1);
      Cards pinned_opp = opp_trumps.Intersect(pinned);
      int unpinned = n - i, unpinned_opp = num_opp - pinned_opp.Size();
      // Unpinned opposing trumps beat unpinned trumps only.
      int kills = std::min(unpinned, unpinned_opp);
      // Pinned opposing trumps beat the other unpinned trumps, then lower pinned ones.
      for (int k = unpinned - kills; k > 0 && pinned_opp; --k) {
        pinned_opp.Remove(pinned_opp.Bottom());
        ++kills;
      }
      kills += TrumpKills(trumps.Intersect(pinned), pinned_opp);
      if (n - kills >= wanted) return {n - kills, pinned};
    }
    return {0, {}};
  }

  // The n highest-ranked cards of a suit, i.e. its n lowest set bits.
  static Cards KeepTop(Cards suit, int n) {
    if (n <= 0) return Cards();
    if (suit.Size() <= n) return suit;
#ifdef __BMI2__
    return Cards(_pdep_u64((1ULL << n) - 1, suit.Value()));
#else
    uint64_t bits = suit.Value(), kept = 0;
    for (int i = 0; i < n; ++i) {
      uint64_t low = bits & -bits;
      kept |= low;
      bits ^= low;
    }
    return Cards(kept);
#endif
  }

  // `suit_mask` leaves out the side suits whose bit is clear. Also reports the side-suit
  // part alone, before the cap to the hand's size.
  Result FastTricks(unsigned suit_mask = 15, int* side_tricks = nullptr,
                    Cards* side_rank_winners = nullptr) const {
    Cards my_hand = hands[seat_to_play], pd_hand = hands[Partner()];
    Cards lho_hand = hands[LeftHandOpp()], rho_hand = hands[RightHandOpp()];
    Cards pd_rank_winners;
    auto [trump_tricks, rank_winners] =
        trump == NOTRUMP ? Result{0, {}} : TopTrumpTricks(my_hand.Suit(trump), pd_hand.Suit(trump));
    int fast_tricks = 0, my_tricks = 0, pd_tricks = 0;
    bool my_entry = false, pd_entry = false;
    const bool lho_has_trump = trump != NOTRUMP && lho_hand.Suit(trump);
    const bool rho_has_trump = trump != NOTRUMP && rho_hand.Suit(trump);
    for (int suit = 0; suit < NUM_SUITS; ++suit) {
      if (suit == trump || !((suit_mask >> suit) & 1)) continue;
      auto my_suit = my_hand.Suit(suit);
      auto pd_suit = pd_hand.Suit(suit);
      auto lho_suit = lho_hand.Suit(suit);
      auto rho_suit = rho_hand.Suit(suit);
      int opp_max = std::max(lho_suit.Size(), rho_suit.Size());
      int my_max_rank_winners = std::max(pd_suit.Size(), opp_max);
      int pd_max_rank_winners = std::max(my_suit.Size(), opp_max);

      auto max_suit_winners = TOTAL_TRICKS;
      if (trump != NOTRUMP) {
        if (lho_has_trump) max_suit_winners = lho_suit.Size();
        if (rho_has_trump) max_suit_winners = std::min(max_suit_winners, rho_suit.Size());
        my_suit = KeepTop(my_suit, max_suit_winners);
        pd_suit = KeepTop(pd_suit, max_suit_winners);
      }

      int my_winners = 0, pd_winners = 0;
      for (int card : trick->all_cards.Suit(suit))
        if (my_suit.Include(card)) {
          ++my_winners;
          if (my_winners <= my_max_rank_winners) rank_winners.Add(card);
        } else if (pd_suit.Include(card)) {
          ++pd_winners;
          if (pd_winners <= pd_max_rank_winners) pd_rank_winners.Add(card);
        } else
          break;
      my_tricks +=
          SuitFastTricks(my_suit, my_winners, my_max_rank_winners, my_entry, pd_suit, pd_winners);
      pd_tricks +=
          SuitFastTricks(pd_suit, pd_winners, pd_max_rank_winners, pd_entry, my_suit, my_winners);
    }
    if (pd_entry) {
      fast_tricks = std::max(my_tricks, pd_tricks);
      rank_winners.Add(pd_rank_winners);
    } else
      fast_tricks = my_tricks;
    if (side_tricks) {
      *side_tricks = fast_tricks;
      *side_rank_winners =
          trump == NOTRUMP ? rank_winners : rank_winners.Different(Cards(MaskOf(trump)));
    }

    if (trump != NOTRUMP) {
      // The long trump hand keeps its trumps while the fast tricks are cashed if it has
      // enough other cards to play, so its length tricks come afterwards.
      auto my_trumps = my_hand.Suit(trump), pd_trumps = pd_hand.Suit(trump);
      int length_tricks =
          TrumpLengthTricks(my_trumps, pd_trumps, lho_hand.Suit(trump), rho_hand.Suit(trump));
      int max_trumps = std::max(my_trumps.Size(), pd_trumps.Size());
      bool my_keeps =
          my_trumps.Size() == max_trumps && my_hand.Size() - my_trumps.Size() >= fast_tricks;
      bool pd_keeps =
          pd_trumps.Size() == max_trumps && pd_hand.Size() - pd_trumps.Size() >= fast_tricks;
      if (length_tricks > 0 && length_tricks >= trump_tricks && (my_keeps || pd_keeps)) {
        trump_tricks = length_tricks;
        rank_winners = rank_winners.Different(trick->all_cards.Suit(trump));
      }
    }
    return {std::min(trump_tricks + fast_tricks, my_hand.Size()), rank_winners};
  }

  int SuitFastTricks(Cards my_suit, int my_winners, int max_rank_winners, bool& my_entry,
                     Cards pd_suit, int pd_winners) const {
    // Entry from partner if my top winner can cover partner's bottom card.
    if (pd_suit && my_winners > 0 && HigherRank(my_suit.Top(), pd_suit.Bottom())) my_entry = true;
    if (pd_winners == 0) {
      // My winners outlast everyone else's cards, so my small cards win too.
      if (my_winners >= max_rank_winners) return my_suit.Size();
      // Partner has no winners.
      return my_winners;
    }
    // Cash all my winners, then partner's.
    if (my_winners == 0) return my_suit ? pd_winners : 0;
    // Suit blocked by partner.
    if (LowerRank(my_suit.Top(), pd_suit.Bottom())) return pd_winners;
    // Suit blocked by me.
    if (HigherRank(my_suit.Bottom(), pd_suit.Top())) return my_winners;
    // If partner has no small cards, treat one winner as a small card.
    if (pd_winners == pd_suit.Size()) --pd_winners;
    return std::min(my_suit.Size(), my_winners + pd_winners);
  }

  Cards GetTrickRankWinner() const {
    CHECK(TrickEnding());
    int winning_card = WinningCard();
    auto played_cards = trick->all_cards.Different(hands.all_cards());
    return played_cards.Suit(SuitOf(winning_card)).Size() > 1 ? Cards().Add(winning_card) : Cards();
  }

  // Given fast tricks that cut needing only `need` of them, looks for the same cut with
  // fewer rank winners (so its pattern matches more positions); returns whether it changed
  // the tricks and rank winners.
  bool MinimizeFastTricks(int need, bool with_trumps, int& lead_tricks,
                          Cards& lead_rank_winners) const {
    // Trump length tricks need no rank winners.
    if (with_trumps) {
      int length_tricks =
          TrumpLengthTricks(MySuit(trump), PdSuit(trump), LhoSuit(trump), RhoSuit(trump));
      if (length_tricks >= need) {
        lead_tricks = length_tricks;
        lead_rank_winners = {};
        return true;
      }
    }
    // The top cards of one suit.
    if (SingleSuitFastTricks(need, lead_rank_winners)) {
      lead_tricks = need;
      return true;
    }
    if (lead_tricks == need) return false;
    // Drop the suits the cut can do without, the one with the lowest rank winner first.
    unsigned suit_mask = 15, tried_mask = 0;
    bool reduced = false;
    for (;;) {
      int drop_suit = -1, lowest_rank = NUM_RANKS;
      for (int suit = 0; suit < NUM_SUITS; ++suit) {
        if (suit == trump || ((tried_mask >> suit) & 1) || !lead_rank_winners.Suit(suit)) continue;
        int rank = RankOf(lead_rank_winners.Suit(suit).Bottom());
        if (rank < lowest_rank) lowest_rank = rank, drop_suit = suit;
      }
      if (drop_suit < 0) break;
      tried_mask |= 1u << drop_suit;
      auto [tricks, rank_winners] = FastTricks(suit_mask & ~(1u << drop_suit));
      if (tricks < need) continue;
      suit_mask &= ~(1u << drop_suit);
      lead_tricks = tricks;
      lead_rank_winners = rank_winners;
      reduced = true;
      if (tricks == need) break;
    }
    return reduced;
  }

  // Whether the leader can cash `need` tricks from the top of one suit with fewer rank
  // winners than `rank_winners`; if so, sets them.
  bool SingleSuitFastTricks(int need, Cards& rank_winners) const {
    Cards my_hand = hands[seat_to_play], pd_hand = hands[Partner()];
    Cards lho_hand = hands[LeftHandOpp()], rho_hand = hands[RightHandOpp()];
    const bool lho_has_trump = trump != NOTRUMP && lho_hand.Suit(trump);
    const bool rho_has_trump = trump != NOTRUMP && rho_hand.Suit(trump);
    Cards best_winners;
    int best_score = -1;
    for (int suit = 0; suit < NUM_SUITS; ++suit) {
      Cards my_suit = my_hand.Suit(suit);
      if (my_suit.Size() < need) continue;
      if (suit != trump) {
        // An opponent with trumps must follow suit to every one of the tricks.
        int max_suit_winners = TOTAL_TRICKS;
        if (lho_has_trump) max_suit_winners = lho_hand.Suit(suit).Size();
        if (rho_has_trump)
          max_suit_winners = std::min(max_suit_winners, rho_hand.Suit(suit).Size());
        if (max_suit_winners < need) continue;
      }
      // Once the others have no card left in the suit, ranks no longer matter.
      int max_rank_winners =
          std::max(pd_hand.Suit(suit).Size(),
                   std::max(lho_hand.Suit(suit).Size(), rho_hand.Suit(suit).Size()));
      int winners = 0, lowest_winner = -1;
      Cards suit_winners;
      for (int card : trick->all_cards.Suit(suit)) {
        if (winners == need || !my_suit.Include(card)) break;
        ++winners;
        if (winners <= max_rank_winners) {
          suit_winners.Add(card);
          lowest_winner = card;
        }
      }
      if (winners < need) continue;
      // Fewest rank winners, then the highest card.
      int score = (TOTAL_TRICKS - suit_winners.Size()) * 16 +
                  (lowest_winner < 0 ? 15 : RankOf(lowest_winner));
      if (score > best_score) {
        best_score = score;
        best_winners = suit_winners;
      }
    }
    if (best_score < 0 || best_winners.Size() >= rank_winners.Size()) return false;
    rank_winners = best_winners;
    return true;
  }

  Result CollectLastTrick() const {
    int winning_card = hands[seat_to_play].Top();
    int winning_seat = seat_to_play;
    for (int s = 1; s < NUM_SEATS; ++s) {
      int seat = (seat_to_play + s) % NUM_SEATS;
      int card_to_play = hands[seat].Top();
      if (WinOver(card_to_play, winning_card)) {
        winning_card = card_to_play;
        winning_seat = seat;
      }
    }
    Cards rank_winners;
    if (hands.all_cards().Suit(SuitOf(winning_card)).Size() > 1) rank_winners.Add(winning_card);
    return {ns_tricks_won + IsNs(winning_seat), rank_winners};
  }

  void ShowTricks(int beta, int ns_tricks, bool starting) const {
    printf("%2d: %c", depth, SuitName(trump)[0]);
    for (int i = 0; i <= depth; ++i) {
      if ((i & 3) == 0) printf(" %c", SeatLetter(plays[i].seat_to_play));
      printf(" %s", NameOf(plays[i].card_played));
    }
    if (starting)
      printf(" (%d)\n", beta);
    else
      printf(" (%d) -> %d\n", beta, ns_tricks);
  }

  void ShowPattern(const char* action, const Pattern& pattern, Shape shape) const {
    printf("%2d: %s ", depth, action);
    pattern.Show(shape);
    printf(" / ");
    hands.Show();
  }

  // Whether this play is at position p of its trick.
  template <int POS>
  bool At(int p) const {
    return POS >= 0 ? POS == p : (depth & 3) == p;
  }
  template <int POS>
  int Pos() const {
    return POS >= 0 ? POS : depth & 3;
  }
  bool TrickStarting() const { return (depth & 3) == 0; }
  bool TrickEnding() const { return (depth & 3) == 3; }
  bool NsToPlay() const { return IsNs(seat_to_play); }
  bool NsWon() const { return IsNs(WinningSeat()); }
  bool WinOver(int c1, int c2) const {
    return SuitOf(c1) == SuitOf(c2) ? HigherRank(c1, c2) : SuitOf(c1) == trump;
  }
  int WinningCard() const { return plays[winning_play].card_played; }
  int WinningSeat() const { return plays[winning_play].seat_to_play; }
  int LeadSuit() const { return trick->lead_suit; }
  int NextSeat(int count = 1) const { return (seat_to_play + count) & (NUM_SEATS - 1); }
  int LeftHandOpp() const { return NextSeat(1); }
  int Partner() const { return NextSeat(2); }
  int RightHandOpp() const { return NextSeat(3); }
  Cards MySuit(int suit) const { return hands[seat_to_play].Suit(suit); }
  Cards PdSuit(int suit) const { return hands[Partner()].Suit(suit); }
  Cards LhoSuit(int suit) const { return hands[LeftHandOpp()].Suit(suit); }
  Cards RhoSuit(int suit) const { return hands[RightHandOpp()].Suit(suit); }
  const Play& PreviousPlay() const { return *(this - 1); }
  Play& NextPlay() { return *(this + 1); }

  // The equivalence runs that the result of the play after this one, at POS, relies on
  // within this trick: none if that play starts the next trick.
  template <int POS>
  Cards NextRuns() const {
    return At<POS>(3) ? Cards() : (this + 1)->equivalence_runs;
  }

  // Fixed info.
  Play* const plays = nullptr;
  Trick* const trick = nullptr;
  Hands& hands = empty_hands;
  const int trump = NOTRUMP;
  const int depth = 0;

  // Per play info.
  int ns_tricks_won = 0;
  int seat_to_play;
  int card_played;
  int winning_play;
  OrderedCards ordered_cards;
  // The equivalence runs this play's last result relies on within its trick; see
  // ComputePatternCards().
  Cards equivalence_runs;

  friend class PlaySequence;
};

class MinMax {
 public:
  MinMax(const Hands& hands_in, int trump, int seat_to_play) : hands(hands_in) {
    for (int i = 0; i < TOTAL_CARDS; ++i)
      new (&plays[i]) Play(plays, tricks + i / 4, hands, trump, i, seat_to_play);
    if (options.stats_level) memset((void*)stats, 0, sizeof(stats));
  }

  ~MinMax() {
    if (options.stats_level) {
      puts("");
      for (int i = 0; i < TOTAL_CARDS; ++i) stats[i].Show(i);
    }
  }

  int Search(int beta) { return plays[0].Search(beta).first; }

  Play& play(int i) { return plays[i]; }

 private:
  Hands hands;
  Play plays[TOTAL_CARDS];
  Trick tricks[TOTAL_TRICKS];
};

Cards ParseHand(const char* input_line, Cards all_cards) {
  // Filter out invalid characters.
  char filtered_line[120], *line = filtered_line;
  int pos = 0;
  for (const char* c = input_line; *c; ++c)
    if (strchr("AaKkQqJjTt1098765432Xx- ", *c)) filtered_line[pos++] = *c;
  filtered_line[pos] = '\0';

  Cards hand;
  for (int suit = 0; suit < NUM_SUITS; ++suit) {
    while (line[0] && isspace(line[0])) ++line;
    while (line[0] && !isspace(line[0]) && line[0] != '-') {
      int rank;
      if (tolower(line[0]) == 'x') {  // wildcard
        rank = RankOf(all_cards.Complement().Suit(suit).Bottom());
      } else {
        rank = CharToRank(line[0]);
      }
      int card = CardOf(suit, rank);
      if (all_cards.Include(card)) {
        fprintf(stderr, "%s showed up twice.\n", NameOf(card));
        exit(-1);
      }
      all_cards.Add(card);
      hand.Add(card);

      if (rank == TEN && line[0] == '1') {
        if (line[1] != '0') {
          fprintf(stderr, "Unknown rank: %c%c\n", line[0], line[1]);
          exit(-1);
        }
        ++line;
      }
      ++line;
    }
    if (line[0] == '-') ++line;
  }
  return hand;
}

int MemoryEnhancedTestDriver(const std::function<int(int)>& search, int num_tricks,
                             int guess_tricks) {
  int upperbound = num_tricks;
  int lowerbound = 0;
  int ns_tricks = guess_tricks;
  if (options.displaying_depth > 0)
    printf("Lowerbound: %d\tUpperbound: %d\n", lowerbound, upperbound);
  while (lowerbound < upperbound) {
    int beta = (ns_tricks == lowerbound ? ns_tricks + 1 : ns_tricks);
    ns_tricks = search(beta);
    if (ns_tricks < beta)
      upperbound = ns_tricks;
    else
      lowerbound = ns_tricks;
    if (options.displaying_depth > 0)
      printf("Lowerbound: %d\tUpperbound: %d\n", lowerbound, upperbound);
  }
  return ns_tricks;
}

int GuessTricks(const Hands& hands, int trump) {
  if (options.guess_tricks >= 0) return std::min(options.guess_tricks, hands.num_tricks());

  int ns_points = hands[NORTH].Points() + hands[SOUTH].Points();
  int ew_points = hands[EAST].Points() + hands[WEST].Points();
  if (trump == NOTRUMP) {
    if (ns_points * 2 < ew_points) return 0;
    if (ns_points < ew_points) return hands.num_tricks() / 2 + 1;
  } else {
    int n_trumps = hands[NORTH].Suit(trump).Size(), s_trumps = hands[SOUTH].Suit(trump).Size();
    int e_trumps = hands[EAST].Suit(trump).Size(), w_trumps = hands[WEST].Suit(trump).Size();
    if (ns_points < ew_points && (std::max(n_trumps, s_trumps) < std::max(e_trumps, w_trumps) ||
                                  (std::max(n_trumps, s_trumps) == std::max(e_trumps, w_trumps) &&
                                   n_trumps + s_trumps < e_trumps + w_trumps)))
      return 0;
  }
  return hands.num_tricks();
}

// The API's text form of a hand.
std::string HandText(Cards hand) {
  std::string text;
  for (int suit = 0; suit < NUM_SUITS; ++suit) {
    if (suit > 0) text += ' ';
    if (!hand.Suit(suit)) text += '-';
    for (int card : hand.Suit(suit)) text += RankName(RankOf(card));
  }
  return text;
}

Deal ToDeal(const Hands& hands) {
  Deal deal;
  for (int seat = 0; seat < NUM_SEATS; ++seat) deal.hands[seat] = HandText(hands[seat]);
  return deal;
}

Hands ToHands(const Deal& deal) {
  Hands hands;
  Cards all_cards;
  for (int seat = 0; seat < NUM_SEATS; ++seat) {
    hands[seat] = ParseHand(deal.hands[seat].c_str(), all_cards);
    all_cards.Add(hands[seat]);
  }
  return hands;
}

Deal ParseDeal(const Deal& deal) {
  Hands hands = ToHands(deal);
  int num_tricks = 0;
  std::vector<int> empty_seats;
  for (int seat = 0; seat < NUM_SEATS; ++seat) {
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
    hands.Deal(hands.all_cards().Complement(), empty_seats);
  }
  return ToDeal(hands);
}

Deal RandomDeal() {
  Hands hands;
  hands.Randomize();
  return ToDeal(hands);
}

Deal ShuffleDeal(const Deal& deal, const char* seats) {
  Hands hands = ToHands(deal);
  hands.Shuffle(seats);
  return ToDeal(hands);
}

std::string EncodeDeal(const Deal& deal) { return ToHands(deal).Encode(); }

Deal DecodeDeal(const char* code) {
  Hands hands;
  hands.Decode(code);
  return ToDeal(hands);
}

int NumTricks(const Deal& deal) { return ToHands(deal).num_tricks(); }
void ShowCompact(const Deal& deal, int rotation) { ToHands(deal).ShowCompact(rotation); }
void ShowDetailed(const Deal& deal, int rotation) { ToHands(deal).ShowDetailed(rotation); }

// The deal, trump and discard setting the caches hold entries for; no trump
// (-1) when they hold none.
Hands cached_hands;
int cached_trump = -1;
bool cached_discard_suit_bottom = false;

void ClearCaches() {
  common_bounds_cache.Reset();
  cutoff_cache.Reset();
  cached_trump = -1;
}

// Keeps the caches while searches stay on the same deal, trump and discard
// setting, and clears them otherwise. Empty caches aren't cleared again: the
// PGO training run must reset them as often as before, or Vector::clear()'s
// profile shifts and SearchWithCache() gets ~1% slower.
void UseCaches(const Hands& hands, int trump) {
  if (cached_trump != -1 &&
      !(hands.Equals(cached_hands) && trump == cached_trump &&
        options.discard_suit_bottom == cached_discard_suit_bottom))
    ClearCaches();
  cached_hands = hands;
  cached_trump = trump;
  cached_discard_suit_bottom = options.discard_suit_bottom;
}

void Solve(const Deal& deal, const std::vector<int>& trumps, const std::vector<int>& lead_seats,
           const std::function<void(int trump)>& trump_start,
           const std::function<void(int trump, int lead_seat, int ns_tricks)>& seat_done,
           const std::function<void(int trump)>& trump_done) {
  Hands hands = ToHands(deal);
  int num_tricks = hands[WEST].Size();
  for (int trump : trumps) {
    UseCaches(hands, trump);
    trump_start(trump);
    int guess_tricks = GuessTricks(hands, trump);
    for (int lead_seat : lead_seats) {
      MinMax min_max(hands, trump, lead_seat);
      auto search = [&min_max](int beta) { return min_max.Search(beta); };
      int ns_tricks = MemoryEnhancedTestDriver(search, num_tricks, guess_tricks);
      guess_tricks = std::min(ns_tricks + 1, TOTAL_TRICKS);
      if (options.stats_level) {
        common_bounds_cache.ShowStatistics();
        cutoff_cache.ShowStatistics();
        VectorPool<Pattern>::ShowStatistics();
      }
      seat_done(trump, lead_seat, ns_tricks);
      if (hands.num_voids() >= 4) cutoff_cache.Reset();
    }
    ClearCaches();
    trump_done(trump);
  }
}

// Plays cards from the start of a deal, leaving the last trick to
// CollectLastTrick(), and sets up the play after them.
class PlaySequence {
 public:
  PlaySequence(const Hands& hands, int trump, int lead_seat, const std::vector<int>& played_cards)
      : min_max(hands, trump, lead_seat),
        num_tricks(hands.num_tricks()),
        last_trick(4 * (num_tricks - 1)),
        played_cards(played_cards) {
    for (size_t p = 0; p <= played_cards.size(); ++p) {
      auto& play = min_max.play(p);
      if (play.TrickStarting()) {
        if (play.depth > 0) {
          play.ns_tricks_won = play.PreviousPlay().ns_tricks_won + play.PreviousPlay().NsWon();
          play.seat_to_play = play.PreviousPlay().WinningSeat();
        }
        play.trick->all_cards = play.hands.all_cards();
        play.ComputeShape();
        play.trick->ComputeRelativeHands(play.hands);
      } else {
        play.ns_tricks_won = play.PreviousPlay().ns_tricks_won;
        play.seat_to_play = play.PreviousPlay().NextSeat();
      }
      if (p < played_cards.size() && p < last_trick) play.PlayCard(played_cards[p]);
    }
  }

  Position GetPosition() {
    auto& play = min_max.play(played_cards.size());
    Hands hands = play.hands;
    Position position;
    position.seat_to_play = play.seat_to_play;
    position.ns_tricks_won = play.ns_tricks_won;
    if (played_cards.size() >= last_trick) {
      for (size_t p = last_trick; p < played_cards.size(); ++p)
        hands[min_max.play(p).seat_to_play].Remove(played_cards[p]);
      position.playable_cards.push_back(hands[play.seat_to_play].Top());
    } else {
      for (int card : play.trick->FilterEquivalent(play.GetPlayableCards()))
        position.playable_cards.push_back(card);
    }
    position.hands = ToDeal(hands);
    return position;
  }

  int SolvePlay(int card, int guess_ns_tricks) {
    if (played_cards.size() >= last_trick) return min_max.play(last_trick).CollectLastTrick().first;
    auto& play = min_max.play(played_cards.size());
    auto search = [&play, card](int beta) {
      play.PlayCard(card);
      auto [ns_tricks, _] = play.NextPlay().Search(beta);
      play.UnplayCard();
      return ns_tricks;
    };
    return MemoryEnhancedTestDriver(search, num_tricks, guess_ns_tricks);
  }

 private:
  MinMax min_max;
  const int num_tricks;
  const size_t last_trick;
  const std::vector<int>& played_cards;
};

Position GetPosition(const Deal& deal, int trump, int lead_seat,
                     const std::vector<int>& played_cards) {
  Hands hands = ToHands(deal);
  assert(played_cards.size() < size_t(4 * hands.num_tricks()));
  return PlaySequence(hands, trump, lead_seat, played_cards).GetPosition();
}

int SolvePlay(const Deal& deal, int trump, int lead_seat, const std::vector<int>& played_cards,
              int guess_ns_tricks) {
  Hands hands = ToHands(deal);
  assert(!played_cards.empty() && played_cards.size() <= size_t(4 * hands.num_tricks()));
  UseCaches(hands, trump);
  std::vector<int> earlier_cards(played_cards.begin(), played_cards.end() - 1);
  if (guess_ns_tricks == -1) guess_ns_tricks = GuessTricks(hands, trump);
  return PlaySequence(hands, trump, lead_seat, earlier_cards)
      .SolvePlay(played_cards.back(), guess_ns_tricks);
}
