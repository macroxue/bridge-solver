#include <assert.h>
#include <ctype.h>
#include <immintrin.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <vector>

// clang-format off
#ifdef _DEBUG
#define CHECK(statement) assert(statement)
#define VERBOSE(statement) if (depth <= options.displaying_depth) statement
#else
#define CHECK(statement) if (statement) {}
#define VERBOSE(statement)
#endif
// clang-format on

namespace {

enum { SPADE, HEART, DIAMOND, CLUB, NUM_SUITS, NOTRUMP = NUM_SUITS };
enum { TWO, TEN = 8, JACK, QUEEN, KING, ACE, NUM_RANKS };
enum { WEST, NORTH, EAST, SOUTH, NUM_SEATS };

const int TOTAL_TRICKS = NUM_RANKS;
const int TOTAL_CARDS = NUM_RANKS * NUM_SUITS;

const char* SeatName(int seat) {
  static const char* seat_names[] = {"West", "North", "East", "South"};
  return seat_names[seat];
}
char SeatLetter(int seat) { return SeatName(seat)[0]; }

const char* SuitName(int suit) {
  static const char* suit_names[] = {"Spade", "Heart", "Diamond", "Club", "NoTrump"};
  return suit_names[suit];
}

const char* SuitSign(int suit) {
#ifdef _DEBUG
  static const char* suit_signs[] = {"♠", "♥", "♦", "♣", "NT"};
#else
  static const char* suit_signs[] = {"♠", "\e[31m♥\e[0m", "\e[31m♦\e[0m", "♣", "NT"};
#endif
  return suit_signs[suit];
}

const char RankName(int rank) {
  static const char rank_names[] = "23456789TJQKA";
  return rank_names[rank];
}

int CharToSuit(char c) {
  for (int suit = SPADE; suit <= NOTRUMP; ++suit)
    if (toupper(c) == SuitName(suit)[0]) return suit;
  printf("Unknown suit: %c\n", c);
  exit(-1);
}

int CharToRank(char c) {
  if (c == '1') return TEN;
  for (int rank = TWO; rank <= ACE; ++rank)
    if (toupper(c) == RankName(rank)) return rank;
  printf("Unknown rank: %c\n", c);
  exit(-1);
}

int CharToSeat(char c) {
  for (int seat = WEST; seat <= SOUTH; ++seat)
    if (toupper(c) == SeatLetter(seat)) return seat;
  printf("Unknown seat: %c\n", c);
  exit(-1);
}

char suit_of[TOTAL_CARDS];
char rank_of[TOTAL_CARDS];
char card_of[NUM_SUITS][16];
char name_of[TOTAL_CARDS][4];

int SuitOf(int card) { return suit_of[card]; }
int RankOf(int card) { return rank_of[card]; }
int CardOf(int suit, int rank) { return card_of[suit][rank]; }
uint64_t MaskOf(int suit) { return 0x1fffULL << (suit * NUM_RANKS); }
const char* NameOf(int card) { return name_of[card]; }

struct CardInitializer {
  CardInitializer() {
    for (int card = 0; card < TOTAL_CARDS; ++card) {
      suit_of[card] = card / NUM_RANKS;
      rank_of[card] = NUM_RANKS - 1 - card % NUM_RANKS;
      card_of[SuitOf(card)][RankOf(card)] = card;
      name_of[card][0] = SuitName(SuitOf(card))[0];
      name_of[card][1] = RankName(RankOf(card));
      name_of[card][2] = '\0';
    }
  }
} card_initializer;

struct Options {
  char* input = nullptr;
  int trump = -1;
  int guess = TOTAL_TRICKS;
  int small_card = TWO;
  int displaying_depth = -1;
  bool discard_suit_bottom = false;
  bool randomize = false;
  bool show_stats = false;
  bool full_analysis = false;
  bool interactive = false;

  void Read(int argc, char* argv[]) {
    int c;
    while ((c = getopt(argc, argv, "dfg:i:rs:t:D:IS")) != -1) {
      switch (c) {
        // clang-format off
        case 'd': discard_suit_bottom = true; break;
        case 'f': full_analysis = true; break;
        case 'g': guess = atoi(optarg); break;
        case 'i': input = optarg; break;
        case 'r': randomize = true; break;
        case 's': small_card = CharToRank(optarg[0]); break;
        case 't': trump = CharToSuit(optarg[0]); break;
        case 'D': displaying_depth = atoi(optarg); break;
        case 'I': interactive = true; break;
        case 'S': show_stats = true; break;
          // clang-format on
      }
    }
  }
} options;

template <class T>
int BitSize(T v) {
  return sizeof(v) * 8;
}

uint64_t PackBits(uint64_t source, uint64_t mask) {
#ifdef _BMI2
  return _pext_u64(source, mask);
#else
  if (source == 0) return 0;
  uint64_t packed = 0;
  for (uint64_t bit = 1; mask; bit <<= 1) {
    if (source & mask & -mask) packed |= bit;
    mask &= mask - 1;
  }
  return packed;
#endif
}

uint64_t UnpackBits(uint64_t source, uint64_t mask) {
#ifdef _BMI2
  return _pdep_u64(source, mask);
#else
  if (source == 0) return 0;
  uint64_t unpacked = 0;
  for (uint64_t bit = 1; mask; bit <<= 1) {
    if (source & bit) unpacked |= mask & -mask;
    mask &= mask - 1;
  }
  return unpacked;
#endif
}

}  // namespace

class Cards {
 public:
  Cards() : bits(0) {}
  Cards(uint64_t b) : bits(b) {}
  uint64_t Value() const { return bits; }

  int Size() const { return __builtin_popcountll(bits); }
  bool Empty() const { return bits == 0; }
  bool Have(int card) const { return bits & Bit(card); }
  bool operator==(const Cards& c) const { return bits == c.bits; }
  bool operator!=(const Cards& c) const { return bits != c.bits; }

  Cards Slice(int begin, int end) const { return bits & (Bit(end) - Bit(begin)); }
  Cards Suit(int suit) const { return bits & MaskOf(suit); }
  int Top() const { return __builtin_ctzll(bits); }
  int Bottom() const { return BitSize(bits) - 1 - __builtin_clzll(bits); }

  Cards Union(const Cards& c) const { return bits | c.bits; }
  Cards Intersect(const Cards& c) const { return bits & c.bits; }
  Cards Different(const Cards& c) const { return bits & ~c.bits; }
  bool Include(const Cards& c) const { return Intersect(c) == c; }

  Cards Add(int card) { return bits |= Bit(card); }
  Cards Remove(int card) { return bits &= ~Bit(card); }

  Cards Add(const Cards& c) { return bits |= c.bits; }
  Cards Remove(const Cards& c) { return bits &= ~c.bits; }
  Cards ClearSuit(int suit) { return bits &= ~MaskOf(suit); }

  int CountPoints() const {
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
    Cards suit_cards = Suit(suit);
    printf("%s ", SuitSign(suit));
    if (suit_cards.Empty())
      putchar('-');
    else
      for (int card : suit_cards) putchar(NameOf(card)[1]);
  }

  class Iterator {
   public:
    Iterator() : pos(TOTAL_CARDS) {}
    Iterator(uint64_t b) : bits(b | Bit(TOTAL_CARDS)), pos(__builtin_ctzll(bits)) {}
    void operator++() { pos = __builtin_ctzll(bits &= ~Bit(pos)); }
    bool operator!=(const Iterator& it) const { return pos != *it; }
    int operator*() const { return pos; }

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

class Hands {
 public:
  void Randomize() {
    struct timeval time;
    gettimeofday(&time, NULL);
    std::mt19937 random(time.tv_sec * 1000 + time.tv_usec / 1000);

    int deck[TOTAL_CARDS];
    for (int card = 0; card < TOTAL_CARDS; ++card) deck[card] = card;
    std::shuffle(deck, deck + TOTAL_CARDS, random);

    for (int seat = 0; seat < NUM_SEATS; ++seat)
      for (int i = 0; i < NUM_RANKS; ++i) hands[seat].Add(deck[seat * NUM_RANKS + i]);
  }

  Cards all_cards() const {
    return hands[WEST].Union(hands[NORTH]).Union(hands[EAST]).Union(hands[SOUTH]);
  }
  Cards partnership_cards(int seat) const {
    return hands[seat].Union(hands[(seat + 2) % NUM_SEATS]);
  }
  Cards opponent_cards(int seat) const {
    return hands[(seat + 1) % NUM_SEATS].Union(hands[(seat + 3) % NUM_SEATS]);
  }

  const Cards& operator[](int seat) const { return hands[seat]; }
  Cards& operator[](int seat) { return hands[seat]; }

  int num_tricks() const { return hands[WEST].Size(); }

  void Show() const {
    for (int seat = 0; seat < NUM_SEATS; ++seat) {
      hands[seat].Show();
      if (seat < NUM_SEATS - 1) printf(", ");
    }
    puts("");
  }

  void ShowCompact(int rotation = 0) const {
    int seat = (NORTH + rotation) % NUM_SEATS;
    printf("%25s%c ", " ", SeatLetter(seat));
    hands[seat].Show();
    printf("\n");

    seat = (WEST + rotation) % NUM_SEATS;
    int num_cards = hands[seat].Size();
    printf("%*s%c ", 14 - num_cards, " ", SeatLetter(seat));
    hands[seat].Show();

    seat = (EAST + rotation) % NUM_SEATS;
    printf("%*s%c ", num_cards + 8, " ", SeatLetter(seat));
    hands[seat].Show();
    printf("\n");

    seat = (SOUTH + rotation) % NUM_SEATS;
    printf("%25s%c ", " ", SeatLetter(seat));
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
      printf("%*s%2d ", gap - 3, " ", hand.CountPoints());
    else
      printf("%*s", gap, " ");
  }

  Cards hands[NUM_SEATS];
};

Hands empty_hands;

template <class Entry, int input_size>
class Cache {
 public:
  Cache(const char* name, int bits)
      : cache_name(name), bits(bits), size(1 << bits), entries(new Entry[size]) {
    Reset();
  }

  void Reset() {
    probe_distance = 0;
    load_count = lookup_count = hit_count = update_count = collision_count = 0;
    for (int i = 0; i < size; ++i) entries[i].Reset(0);
  }

  void ShowStatistics() const {
    printf("--- %s Statistics ---\n", cache_name);
    printf("lookups: %8d   hits:     %8d (%5.2f%%)   probe distance: %d\n", lookup_count,
           hit_count, hit_count * 100.0 / lookup_count, probe_distance);
    printf("updates: %8d   collisions: %6d (%5.2f%%)\n", update_count, collision_count,
           collision_count * 100.0 / update_count);
    printf("entries: %8d   loaded:   %8d (%5.2f%%)\n", size, load_count,
           load_count * 100.0 / size);

    int recursive_load = 0;
    for (int i = 0; i < size; ++i)
      if (entries[i].hash != 0) {
        recursive_load += entries[i].Size();
        entries[i].Show();
      }
    if (recursive_load > load_count) printf("recursive load: %8d\n", recursive_load);
  }

  const Entry* Lookup(Cards cards[input_size]) const {
    ++lookup_count;

    uint64_t hash = Hash(cards);
    if (hash == 0) return NULL;
    uint64_t index = hash >> (BitSize(hash) - bits);

    for (int d = 0; d < probe_distance; ++d) {
      const Entry& entry = entries[(index + d) & (size - 1)];
      if (entry.hash == hash) {
        ++hit_count;
        return &entry;
      }
      if (entry.hash == 0) break;
    }
    return NULL;
  }

  Entry* Update(Cards cards[input_size]) {
    if (load_count >= size * 3 / 4) Resize();

    ++update_count;

    uint64_t hash = Hash(cards);
    if (hash == 0) return NULL;
    uint64_t index = hash >> (BitSize(hash) - bits);

    // Linear probing benefits from hardware prefetch and thus is faster
    // than collision resolution with multiple hash functions.
    for (int d = 0; d < max_probe_distance; ++d) {
      Entry& entry = entries[(index + d) & (size - 1)];
      bool collided = entry.hash != 0 && entry.hash != hash;
      if (!collided || d == max_probe_distance - 1) {
        probe_distance = std::max(probe_distance, d + 1);
        collision_count += collided;
        if (entry.hash != hash) {
          if (entry.hash == 0) ++load_count;
          entry.Reset(hash);
        }
        return &entry;
      }
    }
    return NULL;
  }

 private:
  uint64_t Hash(Cards cards[input_size]) const {
    uint64_t sum = 0;
    for (int i = 0; i < (input_size + 1) / 2; ++i)
      sum += (cards[i * 2].Value() + hash_rand[i * 2]) *
             (cards[i * 2 + 1].Value() + hash_rand[i * 2 + 1]);
    return sum;
  }

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
    probe_distance = 0;
    for (int i = 0; i < old_size; ++i) {
      auto hash = old_entries[i].hash;
      if (hash == 0) continue;
      uint64_t index = hash >> (BitSize(hash) - bits);
      for (int d = 0; d < max_probe_distance; ++d) {
        Entry& entry = entries[(index + d) & (size - 1)];
        if (entry.hash == 0) {
          probe_distance = std::max(probe_distance, d + 1);
          old_entries[i].MoveTo(entry);
          ++load_count;
          break;
        }
      }
    }
  }

  static constexpr int max_probe_distance = 16;
  static constexpr uint64_t hash_rand[2] = {0x6b8b4567327b23c7ULL, 0x643c986966334873ULL};

  const char* cache_name;
  int bits;
  int size;
  int probe_distance;
  std::unique_ptr<Entry[]> entries;

  mutable int load_count;
  mutable int lookup_count;
  mutable int hit_count;
  mutable int update_count;
  mutable int collision_count;
};

#pragma pack(push, 4)
struct Bounds {
  char lower;
  char upper;

  bool Empty() const { return upper < lower; }
  int Width() const { return upper - lower; }
  Bounds Intersect(Bounds b) const {
    return {std::max(lower, b.lower), std::min(upper, b.upper)};
  }
  bool Include(Bounds b) const { return Intersect(b) == b; }
  bool operator==(Bounds b) const { return b.lower == lower && b.upper == upper; }
};

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
    value -= 1ULL << Offset(seat, SuitOf(c1));
    value -= 1ULL << Offset((seat + 1) % NUM_SEATS, SuitOf(c2));
    value -= 1ULL << Offset((seat + 2) % NUM_SEATS, SuitOf(c3));
    value -= 1ULL << Offset((seat + 3) % NUM_SEATS, SuitOf(c4));
  }

  int SuitLength(int seat, int suit) const { return (value >> Offset(seat, suit)) & 0xf; }

  uint64_t Value() const { return value; }

  bool operator==(const Shape& s) const { return value == s.value; }

 private:
  static int Offset(int seat, int suit) { return 60 - (seat * NUM_SUITS + suit) * 4; }

  uint64_t value;
};

struct Pattern {
  Hands hands;
  Bounds bounds;
  mutable uint16_t cuts = 0;
  mutable uint16_t hits = 0;
  std::vector<Pattern> patterns;

  Pattern() = default;

  Pattern(const Hands& hands, Bounds bounds = Bounds()) : hands(hands), bounds(bounds) {}

  void Reset() {
    hands = Hands();
    bounds.lower = 0;
    bounds.upper = TOTAL_TRICKS;
    hits = 0;
    cuts = 0;
    patterns.clear();
    patterns.shrink_to_fit();
  }

  void MoveTo(Pattern& p) { p.MoveFrom(*this); }

  void MoveFrom(Pattern& p) {
    hands = p.hands;
    bounds = p.bounds;
    hits = p.hits;
    cuts = p.cuts;
    std::swap(patterns, p.patterns);
  }

  const Pattern* Lookup(const Pattern& new_pattern, int alpha, int beta) const {
    if (!(new_pattern <= *this)) return nullptr;
    ++hits;
    if (bounds.lower >= beta || bounds.upper <= alpha) {
      ++cuts;
      return this;
    }
    for (auto& pattern : patterns) {
      auto detail = pattern.Lookup(new_pattern, alpha, beta);
      if (detail) return detail;
    }
    return nullptr;
  }

  Pattern* Update(Pattern& new_pattern) {
    for (size_t i = 0; i < patterns.size(); ++i) {
      auto& pattern = patterns[i];
      if (new_pattern == pattern) {
        pattern.UpdateBounds(new_pattern.bounds);
        return &pattern;
      } else if (pattern <= new_pattern) {
        // New pattern is more generic. Absorb sub-patterns.
        for (; i < patterns.size(); ++i) {
          auto& old_pattern = patterns[i];
          if (!(old_pattern <= new_pattern)) continue;
          old_pattern.UpdateBounds(new_pattern.bounds);
          if (old_pattern.bounds == new_pattern.bounds) {
            new_pattern.hits += old_pattern.hits;
            new_pattern.cuts += old_pattern.cuts;
          } else {
            new_pattern.patterns.resize(new_pattern.patterns.size() + 1);
            new_pattern.patterns.back().MoveFrom(old_pattern);
          }
          if (&old_pattern != &pattern) {
            old_pattern.MoveFrom(patterns.back());
            patterns.pop_back();
            --i;
          }
        }
        pattern.MoveFrom(new_pattern);
        return &pattern;
      } else if (new_pattern <= pattern) {
        // Old pattern is more generic. Add new pattern under.
        new_pattern.bounds = new_pattern.bounds.Intersect(pattern.bounds);
        CHECK(!new_pattern.bounds.Empty());
        if (new_pattern.bounds == pattern.bounds) return &pattern;
        return pattern.Update(new_pattern);
      }
    }
    patterns.resize(patterns.size() + 1);
    patterns.back().MoveFrom(new_pattern);
    return &patterns.back();
  }

  void UpdateBounds(Bounds new_bounds) {
    auto old_bounds = bounds;
    bounds = bounds.Intersect(new_bounds);
    CHECK(!bounds.Empty());
    if (bounds.Width() == old_bounds.Width()) return;
    if (bounds.Width() == 0) {
      patterns.clear();
      return;
    }

    for (auto& pattern : patterns) pattern.UpdateBounds(bounds);
  }

  // This pattern is more detailed than (a subset of) the other pattern.
  bool operator<=(const Pattern& p) const {
    return hands[WEST].Include(p.hands[WEST]) && hands[NORTH].Include(p.hands[NORTH]) &&
           hands[EAST].Include(p.hands[EAST]) && hands[SOUTH].Include(p.hands[SOUTH]);
  }

  bool operator==(const Pattern& p) const {
    return p.hands[WEST] == hands[WEST] && p.hands[NORTH] == hands[NORTH] &&
           p.hands[EAST] == hands[EAST] && p.hands[SOUTH] == hands[SOUTH];
  }

  Cards GetRankWinners(Cards all_cards) const {
    Cards relative_rank_winners = hands.all_cards();
    Cards rank_winners;
    for (int suit = 0; suit < NUM_SUITS; ++suit) {
      if (relative_rank_winners.Suit(suit).Empty()) continue;
      auto unpacked =
          UnpackBits(relative_rank_winners.Suit(suit).Value() >> (suit * NUM_RANKS),
                     all_cards.Suit(suit).Value());
      rank_winners.Add(Cards(unpacked));
    }
    return rank_winners;
  }

  int Size() const {
    int sum = 1;
    for (const auto& pattern : patterns) sum += pattern.Size();
    return sum;
  }

  void Show(Shape shape, int level = 1) const {
    if (level > 0) {
      printf("%*d: (%d %d) ", level * 2, level, bounds.lower, bounds.upper);
      for (int seat = 0; seat < NUM_SEATS; ++seat) {
        for (int suit = 0; suit < NUM_SUITS; ++suit) {
          auto suit_length = shape.SuitLength(seat, suit);
          if (suit_length == 0)
            putchar('-');
          else {
            auto rank_winners = hands[seat].Suit(suit);
            for (int card : rank_winners) printf("%c", RankName(RankOf(card)));
            for (int i = rank_winners.Size(); i < suit_length; ++i) putchar('x');
          }
          putchar(' ');
        }
        if (seat < NUM_SEATS - 1) printf(", ");
      }
      printf(" hits %d cuts %d\n", hits, cuts);
    }
    for (const auto& pattern : patterns) pattern.Show(shape, level + 1);
  }
};

struct ShapeEntry {
  uint64_t hash;
  Shape shape;
  int seat_to_play;
  Pattern pattern;

  int Size() const { return pattern.Size() - 1; }

  void Show() const {
    printf("hash %016lx shape %016lx seat %c size %ld recursive size %d hits %d\n", hash,
           shape.Value(), SeatLetter(seat_to_play), pattern.patterns.size(), Size(),
           pattern.hits);
    pattern.Show(shape, 0);
  }

  void Reset(uint64_t hash_in) {
    hash = hash_in;
    shape = Shape();
    seat_to_play = -1;
    pattern.Reset();
  }

  void MoveTo(ShapeEntry& to) {
    to.hash = hash;
    to.shape = shape;
    to.seat_to_play = seat_to_play;
    to.pattern.MoveFrom(pattern);
  }
};

struct CutoffEntry {
  uint64_t hash;
  // Cut-off card depending on the seat to play.
  char card[NUM_SEATS];

  int Size() const { return 1; }

  void Show() const {}

  void Reset(uint64_t hash_in) {
    hash = hash_in;
    memset(card, TOTAL_CARDS, sizeof(card));
  }

  void MoveTo(CutoffEntry& to) { memcpy(&to, this, sizeof(*this)); }
};
#pragma pack(pop)

Cache<ShapeEntry, 2> common_bounds_cache("Common Bounds Cache", 16);
Cache<CutoffEntry, 2> cutoff_cache("Cut-off Cache", 16);

struct Trick {
  Shape shape;
  Cards all_cards;
  Hands relative_hands;

  // A relative hand contains relative cards.
  void ComputeRelativeHands(int depth, const Hands& hands) {
    if (depth < 4) {
      for (int suit = 0; suit < NUM_SUITS; ++suit)
        ConvertToRelativeSuit(hands, suit, all_cards.Suit(suit));
    } else {
      // Recompute the relative cards for suits changed by the last trick.
      auto prev_trick = this - 1;
      relative_hands = prev_trick->relative_hands;
      for (int suit = 0; suit < NUM_SUITS; ++suit) {
        if (all_cards.Suit(suit) != prev_trick->all_cards.Suit(suit))
          ConvertToRelativeSuit(hands, suit, all_cards.Suit(suit));
      }
    }
  }

  // Whether a card is equivalent to one of the tried cards.
  bool IsEquivalent(int card, int suit, Cards tried_suit_cards) const {
    if (tried_suit_cards.Empty()) return false;
    int relative_rank = RelativeRank(card, suit);
    for (int tried_card : tried_suit_cards) {
      if (std::abs(relative_rank - RelativeRank(tried_card, suit)) == 1) return true;
    }
    return false;
  }

  Cards FilterEquivalent(Cards playable_cards) const {
    Cards filtered_cards;
    for (int suit = 0; suit < NUM_SUITS; ++suit) {
      auto suit_cards = playable_cards.Suit(suit);
      if (suit_cards.Empty()) continue;
      int prev_card = suit_cards.Top();
      filtered_cards.Add(prev_card);
      suit_cards.Remove(prev_card);
      for (int card : suit_cards) {
        if (RelativeRank(prev_card, suit) != RelativeRank(card, suit) + 1)
          filtered_cards.Add(card);
        prev_card = card;
      }
    }
    return filtered_cards;
  }

  // A pattern hand contains relative cards and rank-irrelevant cards.
  Hands ComputePatternHands(Cards rank_winners) const {
    Cards relative_rank_winners;
    for (int suit = 0; suit < NUM_SUITS; ++suit) {
      if (rank_winners.Suit(suit).Empty()) continue;

      // Extend bottom rank winner to its lowest equivalent card.
      int bottom_rank_winner = RelativeCard(rank_winners.Suit(suit).Bottom(), suit);
      for (int seat = 0; seat < NUM_SEATS; ++seat) {
        if (!relative_hands[seat].Have(bottom_rank_winner)) continue;
        auto suit_cards = relative_hands[seat].Suit(suit);
        for (int card : suit_cards.Slice(bottom_rank_winner + 1, TOTAL_CARDS)) {
          if (bottom_rank_winner + 1 != card) break;
          bottom_rank_winner = card;
        }
        break;
      }
      relative_rank_winners.Add(Cards(MaskOf(suit)).Slice(0, bottom_rank_winner + 1));
      // Suit bottom can't win by rank. Compensate the inaccuracy with fast tricks.
      relative_rank_winners.Remove(RelativeCard(all_cards.Suit(suit).Bottom(), suit));
    }

    Hands pattern_hands;
    for (int seat = 0; seat < NUM_SEATS; ++seat)
      pattern_hands[seat] = relative_hands[seat].Intersect(relative_rank_winners);
    return pattern_hands;
  }

 private:
  void ConvertToRelativeSuit(const Hands& hands, int suit, Cards all_suit_cards) {
    if (all_suit_cards.Empty()) {
      for (int seat = 0; seat < NUM_SEATS; ++seat) relative_hands[seat].ClearSuit(suit);
      return;
    }
    for (int seat = 0; seat < NUM_SEATS; ++seat) {
      auto packed = PackBits(hands[seat].Suit(suit).Value(), all_suit_cards.Value());
      relative_hands[seat].ClearSuit(suit);
      relative_hands[seat].Add(Cards(packed << (suit * NUM_RANKS)));
    }
  }

  int RelativeRank(int card, int suit) const {
    return ACE - all_cards.Suit(suit).Slice(0, card).Size();
  }

  int RelativeCard(int card, int suit) const {
    return CardOf(suit, RelativeRank(card, suit));
  }
};

class Stats {
 public:
  Stats(bool e) : enabled(e) { Clear(); }
  void Clear() {
    if (enabled) memset(cutoff_at, 0, sizeof(cutoff_at));
  }
  void CutoffAt(int depth, int card_count) {
    if (enabled) ++cutoff_at[depth][card_count];
  }
  void Show() {
    if (!enabled) return;
    puts("---- Cut-off Quality ----");
    for (int d = 0; d < TOTAL_CARDS; ++d) {
      int sum = 0;
      int last = TOTAL_TRICKS - 1;
      for (int i = 0; i < TOTAL_TRICKS; ++i) {
        if (cutoff_at[d][i] == 0) continue;
        last = i;
        sum += cutoff_at[d][i];
      }
      if (sum == 0) continue;

      printf("%d: ", d);
      printf("%.1f%% ", cutoff_at[d][0] * 100.0 / sum);
      for (int i = 0; i <= last; ++i) printf("%d ", cutoff_at[d][i]);
      puts("");
    }
  }

 private:
  bool enabled;
  int cutoff_at[TOTAL_CARDS][16];
};

Stats stats(false);

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

  int SearchWithCache(int alpha, int beta, Cards* rank_winners) {
    if (!TrickStarting()) {
      ns_tricks_won = PreviousPlay().ns_tricks_won;
      seat_to_play = PreviousPlay().NextSeat();
      return Search(alpha, beta, rank_winners);
    }

    if (depth > 0) {
      trick->all_cards = hands.all_cards();
      ns_tricks_won = PreviousPlay().ns_tricks_won + PreviousPlay().NsWon();
      seat_to_play = PreviousPlay().WinningSeat();
    }

    if (NsToPlay() && ns_tricks_won >= beta) return ns_tricks_won;
    int remaining_tricks = hands.num_tricks();
    if (!NsToPlay() && ns_tricks_won + remaining_tricks <= alpha)
      return ns_tricks_won + remaining_tricks;

    if (remaining_tricks == 1) return CollectLastTrick(rank_winners);

    ComputeShape();
    trick->ComputeRelativeHands(depth, hands);

    const Pattern* cached_pattern = nullptr;
    Cards shape_index[2] = {trick->shape.Value(), seat_to_play};
    auto* shape_entry = common_bounds_cache.Lookup(shape_index);
    if (shape_entry) {
      cached_pattern = shape_entry->pattern.Lookup(
          trick->relative_hands, alpha - ns_tricks_won, beta - ns_tricks_won);
      if (cached_pattern) {
        VERBOSE(ShowPattern("match", cached_pattern, trick->shape));
        rank_winners->Add(cached_pattern->GetRankWinners(trick->all_cards));
        int lower = cached_pattern->bounds.lower + ns_tricks_won;
        if (lower >= beta) {
          VERBOSE(printf("%2d: beta cut %d\n", depth, lower));
          return lower;
        }
        int upper = cached_pattern->bounds.upper + ns_tricks_won;
        VERBOSE(printf("%2d: alpha cut %d\n", depth, upper));
        return upper;
      }
    }

    Cards branch_rank_winners;
    int ns_tricks = SearchAtTrickStart(alpha, beta, &branch_rank_winners);
    rank_winners->Add(branch_rank_winners);
    auto bounds = ns_tricks <= alpha
                      ? Bounds{0, char(ns_tricks - ns_tricks_won)}
                      : Bounds{char(ns_tricks - ns_tricks_won), char(remaining_tricks)};

    Hands pattern_hands = trick->ComputePatternHands(branch_rank_winners);
    Pattern new_pattern(pattern_hands, bounds);
    auto* new_shape_entry = common_bounds_cache.Update(shape_index);
    new_shape_entry->shape = trick->shape;
    new_shape_entry->seat_to_play = seat_to_play;
    cached_pattern = new_shape_entry->pattern.Update(new_pattern);
    VERBOSE(ShowPattern("update", cached_pattern, trick->shape));
    return ns_tricks;
  }

 private:
  int SearchAtTrickStart(int alpha, int beta, Cards* rank_winners) {
    Cards fast_rank_winners;
    int fast_tricks = FastTricks(&fast_rank_winners);
    if (NsToPlay() && ns_tricks_won + fast_tricks >= beta) {
      VERBOSE(printf("%2d: beta fast cut %d+%d\n", depth, ns_tricks_won, fast_tricks));
      rank_winners->Add(fast_rank_winners);
      return ns_tricks_won + fast_tricks;
    }
    int remaining_tricks = hands.num_tricks();
    if (!NsToPlay() && ns_tricks_won + (remaining_tricks - fast_tricks) <= alpha) {
      VERBOSE(printf("%2d: alpha fast cut %d+%d\n", depth, ns_tricks_won,
                     remaining_tricks - fast_tricks));
      rank_winners->Add(fast_rank_winners);
      return ns_tricks_won + (remaining_tricks - fast_tricks);
    }
    if (trump != NOTRUMP) {
      Cards slow_rank_winners;
      int slow_tricks = SureTrumpTricks(hands[LeftHandOpp()], hands[RightHandOpp()],
                                        &slow_rank_winners);
      if (NsToPlay() && ns_tricks_won + (remaining_tricks - slow_tricks) <= alpha) {
        VERBOSE(printf("%2d: alpha slow cut %d+%d\n", depth, ns_tricks_won,
                       remaining_tricks - slow_tricks));
        rank_winners->Add(slow_rank_winners);
        return ns_tricks_won + (remaining_tricks - slow_tricks);
      }
      if (!NsToPlay() && ns_tricks_won + slow_tricks >= beta) {
        VERBOSE(printf("%2d: beta slow cut %d+%d\n", depth, ns_tricks_won, slow_tricks));
        rank_winners->Add(slow_rank_winners);
        return ns_tricks_won + slow_tricks;
      }
    }

    return Search(alpha, beta, rank_winners);
  }

  int Search(int alpha, int beta, Cards* rank_winners) {
    int ordered_cards[TOTAL_TRICKS], num_ordered_cards = 0;
    auto playable_cards = GetPlayableCards();
    Cards cutoff_index[2];
    BuildCutoffIndex(cutoff_index);
    Cards cutoff_cards = playable_cards.Intersect(LookupCutoffCards(cutoff_index));
    if (!cutoff_cards.Empty()) {
      ordered_cards[num_ordered_cards++] = cutoff_cards.Top();
      playable_cards.Remove(cutoff_cards);
    } else {
      OrderCards(playable_cards, ordered_cards, num_ordered_cards);
      playable_cards = Cards();
    }

    int bounded_ns_tricks = NsToPlay() ? 0 : TOTAL_TRICKS;
    int min_relevant_ranks[NUM_SUITS] = {TWO, TWO, TWO, TWO};
    Cards root_rank_winners;
    Cards tried_cards;
    for (int i = 0; i < num_ordered_cards; ++i) {
      int card = ordered_cards[i], suit = SuitOf(card), rank = RankOf(card);
      // Try a card if its rank is still relevant and it isn't equivalent to a tried card.
      if (rank >= min_relevant_ranks[suit] &&
          !trick->IsEquivalent(card, suit, tried_cards.Suit(suit))) {
        Cards branch_rank_winners;
        if (Cutoff(alpha, beta, card, &bounded_ns_tricks, &branch_rank_winners)) {
          if (!cutoff_cards.Have(card)) SaveCutoffCard(cutoff_index, card);
          stats.CutoffAt(depth, i);
          VERBOSE(printf("%2d: search cut @%d\n", depth, i));
          rank_winners->Add(branch_rank_winners);
          return bounded_ns_tricks;
        }
        root_rank_winners.Add(branch_rank_winners);
        // If this card's rank is irrelevant, a relevant rank must be higher.
        auto suit_rank_winners = branch_rank_winners.Suit(suit);
        if (suit_rank_winners.Empty() || rank < RankOf(suit_rank_winners.Bottom()))
          min_relevant_ranks[suit] = std::max(min_relevant_ranks[suit], rank + 1);
      }
      tried_cards.Add(card);
      if (!playable_cards.Empty()) {
        OrderCards(playable_cards, ordered_cards, num_ordered_cards);
        playable_cards = Cards();
      }
    }
    stats.CutoffAt(depth, TOTAL_TRICKS - 1);
    rank_winners->Add(root_rank_winners);
    return bounded_ns_tricks;
  }

  void OrderCards(Cards playable_cards, int ordered_cards[], int& num_ordered_cards) {
    if (playable_cards.Empty()) return;
    if (playable_cards.Size() == 1) {
      ordered_cards[num_ordered_cards++] = playable_cards.Top();
      return;
    }
    if (TrickStarting()) {  // lead
      if (trump != NOTRUMP) {
        Cards my_trumps = playable_cards.Suit(trump);
        Cards opp_trumps = hands.opponent_cards(seat_to_play).Suit(trump);
        if (!my_trumps.Empty() && !opp_trumps.Empty()) {
          AddCards(my_trumps, ordered_cards, num_ordered_cards);
          playable_cards.Remove(my_trumps);
        }
      }
      Cards top_bottom;
      for (int suit = 0; suit < NUM_SUITS; ++suit) {
        if (suit == trump) continue;
        auto suit_cards = playable_cards.Suit(suit);
        if (suit_cards.Empty()) continue;
        top_bottom.Add(suit_cards.Top());
        top_bottom.Add(suit_cards.Bottom());
      }
      AddCards(top_bottom, ordered_cards, num_ordered_cards);
      AddCards(playable_cards.Different(top_bottom), ordered_cards, num_ordered_cards);
      return;
    }
    if (!playable_cards.Suit(LeadSuit()).Empty()) {  // follow
      int winning_card = PreviousPlay().WinningCard();
      if (WinOver(playable_cards.Top(), winning_card)) {
        auto higher_cards = playable_cards.Slice(playable_cards.Top(), winning_card);
        AddCards(higher_cards, ordered_cards, num_ordered_cards);
        playable_cards.Remove(higher_cards);
      }
      AddReversedCards(playable_cards, ordered_cards, num_ordered_cards);
      return;
    }
    if (trump != NOTRUMP && !playable_cards.Suit(trump).Empty()) {  // ruff
      int winning_card = PreviousPlay().WinningCard();
      Cards my_trumps = playable_cards.Suit(trump);
      if (SuitOf(winning_card) == trump) {
        int winning_seat = PreviousPlay().WinningSeat();
        if (winning_seat != Partner() && WinOver(my_trumps.Top(), winning_card)) {
          auto higher_trumps = my_trumps.Slice(my_trumps.Top(), winning_card);
          AddReversedCards(higher_trumps, ordered_cards, num_ordered_cards);
          playable_cards.Remove(higher_trumps);
        }
      } else {
        AddReversedCards(my_trumps, ordered_cards, num_ordered_cards);
        playable_cards.Remove(my_trumps);
      }
    }
    // discard
    int num_discards = 0;
    for (int suit = 0; suit < NUM_SUITS; ++suit) {
      if (suit == trump) continue;
      auto suit_cards = playable_cards.Suit(suit);
      if (!suit_cards.Empty()) {
        ordered_cards[num_ordered_cards++] = suit_cards.Bottom();
        ++num_discards;
      }
    }
    auto compare = [playable_cards](int c1, int c2) {
      return playable_cards.Suit(SuitOf(c1)).Size() >
             playable_cards.Suit(SuitOf(c2)).Size();
    };
    std::sort(ordered_cards + num_ordered_cards - num_discards,
              ordered_cards + num_ordered_cards, compare);
    for (int i = num_ordered_cards - num_discards; i < num_ordered_cards; ++i)
      playable_cards.Remove(ordered_cards[i]);
    AddCards(playable_cards, ordered_cards, num_ordered_cards);
  }

  void AddCards(Cards cards, int ordered_cards[], int& num_ordered_cards) const {
    for (int card : cards) ordered_cards[num_ordered_cards++] = card;
  }

  void AddReversedCards(Cards cards, int ordered_cards[], int& num_ordered_cards) const {
    while (!cards.Empty()) {
      ordered_cards[num_ordered_cards++] = cards.Bottom();
      cards.Remove(cards.Bottom());
    }
  }

  void ComputeShape() const {
    if (depth < 4) {
      trick->shape = Shape(hands);
    } else {
      trick->shape = (trick - 1)->shape;
      trick->shape.PlayCards(plays[depth - 4].seat_to_play, plays[depth - 4].card_played,
                             plays[depth - 3].card_played, plays[depth - 2].card_played,
                             plays[depth - 1].card_played);
      CHECK(trick->shape == Shape(hands));
    }
  }

  Cards GetPlayableCards() const {
    const Cards& hand = hands[seat_to_play];
    if (TrickStarting()) return hand;

    int lead_suit = SuitOf(LeadCard());
    Cards suit_cards = hand.Suit(lead_suit);
    if (!suit_cards.Empty()) return suit_cards;

    if (!options.discard_suit_bottom) return hand;

    Cards playable_cards;
    for (int suit = 0; suit < NUM_SUITS; ++suit) {
      Cards suit_cards = hand.Suit(suit);
      if (suit_cards.Empty()) continue;
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

  void PlayCard(int card_to_play) {
    // remove played card from hand
    card_played = card_to_play;
    hands[seat_to_play].Remove(card_played);

    // who's winning?
    if (TrickStarting() || WinOver(card_played, PreviousPlay().WinningCard())) {
      winning_play = depth;
    } else {
      winning_play = PreviousPlay().winning_play;
    }
  }

  void UnplayCard() {
    // add played card back to hand
    hands[seat_to_play].Add(card_played);
  }

  void BuildCutoffIndex(Cards cutoff_index[2]) const {
    if (TrickStarting()) {
      cutoff_index[0] = hands[seat_to_play];
    } else {
      if (hands[seat_to_play].Suit(LeadSuit()).Empty())
        cutoff_index[0] = hands[seat_to_play];
      else
        cutoff_index[0] = trick->all_cards.Suit(LeadSuit());
      cutoff_index[1].Add(PreviousPlay().WinningCard());
    }
  }

  Cards LookupCutoffCards(Cards cutoff_index[]) const {
    Cards cutoff_cards;
    if (const auto* entry = cutoff_cache.Lookup(cutoff_index))
      if (entry->card[seat_to_play] != TOTAL_CARDS)
        cutoff_cards.Add(entry->card[seat_to_play]);
    return cutoff_cards;
  }

  void SaveCutoffCard(Cards cutoff_index[], int cutoff_card) const {
    if (auto* entry = cutoff_cache.Update(cutoff_index))
      entry->card[seat_to_play] = cutoff_card;
  }

  bool Cutoff(int alpha, int beta, int card_to_play, int* bounded_ns_tricks,
              Cards* rank_winners) {
    PlayCard(card_to_play);
    if (TrickEnding()) {
      int winning_card = WinningCard();
      for (int d = depth / 4 * 4; d <= depth; ++d) {
        if (plays[d].card_played == winning_card) continue;
        if (SuitOf(winning_card) == SuitOf(plays[d].card_played)) {
          rank_winners->Add(winning_card);
          break;
        }
      }
    }
    VERBOSE(ShowTricks(alpha, beta, 0, true));
    int ns_tricks = NextPlay().SearchWithCache(alpha, beta, rank_winners);
    VERBOSE(ShowTricks(alpha, beta, ns_tricks, false));
    UnplayCard();

    if (NsToPlay()) {
      *bounded_ns_tricks = std::max(*bounded_ns_tricks, ns_tricks);
      if (*bounded_ns_tricks >= beta) return true;  // beta cut-off
    } else {
      *bounded_ns_tricks = std::min(*bounded_ns_tricks, ns_tricks);
      if (*bounded_ns_tricks <= alpha) return true;  // alpha cut-off
    }
    return false;  // no cut-off
  }

  int SureTrumpTricks(Cards my_hand, Cards partner_hand, Cards* rank_winners) const {
    auto my_suit = my_hand.Suit(trump);
    if (my_suit == trick->all_cards.Suit(trump)) return my_suit.Size();

    auto partner_suit = partner_hand.Suit(trump);
    if (partner_suit == trick->all_cards.Suit(trump)) return partner_suit.Size();

    auto both_suits = my_suit.Union(partner_suit);
    auto max_trump_tricks = std::max(my_suit.Size(), partner_suit.Size());
    int sure_tricks = 0;
    for (int card : trick->all_cards.Suit(trump))
      if (both_suits.Have(card)) {
        ++sure_tricks;
        if (sure_tricks <= max_trump_tricks) rank_winners->Add(card);
      } else
        break;
    return std::min(sure_tricks, max_trump_tricks);
  }

  int FastTricks(Cards* rank_winners) const {
    Cards my_hand = hands[seat_to_play], partner_hand = hands[Partner()];
    Cards lho_hand = hands[LeftHandOpp()], rho_hand = hands[RightHandOpp()];
    int trump_tricks =
        trump == NOTRUMP ? 0 : SureTrumpTricks(my_hand, partner_hand, rank_winners);
    int fast_tricks = 0, my_tricks = 0, partner_tricks = 0;
    bool my_entry = false, partner_entry = false;
    Cards partner_rank_winners;
    for (int suit = 0; suit < NUM_SUITS; ++suit) {
      if (suit == trump) continue;
      auto my_suit = my_hand.Suit(suit);
      auto partner_suit = partner_hand.Suit(suit);
      auto lho_suit = lho_hand.Suit(suit);
      auto rho_suit = rho_hand.Suit(suit);
      int my_max_rank_winners =
          std::max({partner_suit.Size(), lho_suit.Size(), rho_suit.Size()});
      int partner_max_rank_winners =
          std::max({my_suit.Size(), lho_suit.Size(), rho_suit.Size()});

      auto max_suit_winners = TOTAL_TRICKS;
      if (trump != NOTRUMP) {
        if (!lho_hand.Suit(trump).Empty()) max_suit_winners = lho_hand.Suit(suit).Size();
        if (!rho_hand.Suit(trump).Empty())
          max_suit_winners = std::min(max_suit_winners, rho_hand.Suit(suit).Size());
        while (my_suit.Size() > max_suit_winners) my_suit.Remove(my_suit.Bottom());
        while (partner_suit.Size() > max_suit_winners)
          partner_suit.Remove(partner_suit.Bottom());
      }

      int my_winners = 0, partner_winners = 0;
      for (int card : trick->all_cards.Suit(suit))
        if (my_suit.Have(card)) {
          ++my_winners;
          if (my_winners <= my_max_rank_winners) rank_winners->Add(card);
        } else if (partner_suit.Have(card)) {
          ++partner_winners;
          if (partner_winners <= partner_max_rank_winners) partner_rank_winners.Add(card);
        } else
          break;
      my_tricks +=
          SuitFastTricks(my_suit, my_winners, my_entry, partner_suit, partner_winners);
      partner_tricks += SuitFastTricks(partner_suit, partner_winners, partner_entry,
                                       my_suit, my_winners);
    }
    if (partner_entry) {
      fast_tricks = std::max(my_tricks, partner_tricks);
      rank_winners->Add(partner_rank_winners);
    } else
      fast_tricks = my_tricks;
    return std::min(trump_tricks + fast_tricks, my_hand.Size());
  }

  int SuitFastTricks(Cards my_suit, int my_winners, bool& my_entry, Cards partner_suit,
                     int partner_winners) const {
    // Entry from partner if my top winner can cover partner's bottom card.
    if (!partner_suit.Empty() && my_winners > 0 &&
        RankOf(my_suit.Top()) > RankOf(partner_suit.Bottom()))
      my_entry = true;
    // Partner has no winners.
    if (partner_winners == 0) return my_winners;
    // Cash all my winners, then partner's.
    if (my_winners == 0) return my_suit.Empty() ? 0 : partner_winners;
    // Suit blocked by partner.
    if (RankOf(my_suit.Top()) < RankOf(partner_suit.Bottom())) return partner_winners;
    // Suit blocked by me.
    if (RankOf(my_suit.Bottom()) > RankOf(partner_suit.Top())) return my_winners;
    // If partner has no small cards, treat one winner as a small card.
    if (partner_winners == partner_suit.Size()) --partner_winners;
    return std::min(my_suit.Size(), my_winners + partner_winners);
  }

  int CollectLastTrick(Cards* rank_winners) {
    int winning_card = hands[seat_to_play].Top();
    int winning_seat = seat_to_play;
    for (int i = 1; i < NUM_SEATS; ++i) {
      seat_to_play = NextSeat();
      int card_to_play = hands[seat_to_play].Top();
      if (WinOver(card_to_play, winning_card)) {
        winning_card = card_to_play;
        winning_seat = seat_to_play;
      }
    }
    for (int seat = 0; seat < NUM_SEATS; ++seat) {
      if (seat == winning_seat) continue;
      if (SuitOf(winning_card) == SuitOf(hands[seat].Top())) {
        rank_winners->Add(winning_card);
        break;
      }
    }
    return ns_tricks_won + IsNs(winning_seat);
  }

  void ShowTricks(int alpha, int beta, int ns_tricks, bool starting) const {
    printf("%2d:", depth);
    for (int i = 0; i <= depth; ++i) {
      if ((i & 3) == 0) printf(" %c", SeatLetter(plays[i].seat_to_play));
      printf(" %s", NameOf(plays[i].card_played));
    }
    if (starting)
      printf(" (%d %d)\n", alpha, beta);
    else
      printf(" (%d %d) -> %d\n", alpha, beta, ns_tricks);
  }

  void ShowPattern(const char* action, const Pattern* pattern, Shape shape) const {
    printf("%2d: %s ", depth, action);
    pattern->Show(shape);
    printf(" / ");
    hands.Show();
  }

  bool TrickStarting() const { return (depth & 3) == 0; }
  bool SecondSeat() const { return (depth & 3) == 1; }
  bool ThirdSeat() const { return (depth & 3) == 2; }
  bool TrickEnding() const { return (depth & 3) == 3; }
  bool IsNs(int seat) const { return seat & 1; }
  bool NsToPlay() const { return IsNs(seat_to_play); }
  bool NsWon() const { return IsNs(WinningSeat()); }
  bool WinOver(int c1, int c2) const {
    return SuitOf(c1) == SuitOf(c2) ? RankOf(c1) > RankOf(c2) : SuitOf(c1) == trump;
  }
  int WinningCard() const { return plays[winning_play].card_played; }
  int WinningSeat() const { return plays[winning_play].seat_to_play; }
  int LeadCard() const { return plays[depth & ~3].card_played; }
  int LeadSuit() const { return SuitOf(LeadCard()); }
  int NextSeat(int count = 1) const { return (seat_to_play + count) & (NUM_SEATS - 1); }
  int LeftHandOpp() const { return NextSeat(1); }
  int Partner() const { return NextSeat(2); }
  int RightHandOpp() const { return NextSeat(3); }
  Play& PreviousPlay() const { return plays[depth - 1]; }
  Play& NextPlay() const { return plays[depth + 1]; }

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

  friend class InteractivePlay;
};

class MinMax {
 public:
  MinMax(const Hands& hands_in, int trump, int seat_to_play) : hands(hands_in) {
    tricks[0].all_cards = hands.all_cards();
    for (int i = 0; i < TOTAL_CARDS; ++i)
      new (&plays[i]) Play(plays, tricks + i / 4, hands, trump, i, seat_to_play);
    new (&stats) Stats(options.show_stats);
  }

  ~MinMax() { stats.Show(); }

  int Search(int alpha, int beta) {
    Cards rank_winners;
    return plays[0].SearchWithCache(alpha, beta, &rank_winners);
  }

  Play& play(int i) { return plays[i]; }

 private:
  Hands hands;
  Play plays[TOTAL_CARDS];
  Trick tricks[TOTAL_TRICKS];
};

namespace {

Cards ParseHand(char* line) {
  // Filter out invalid characters.
  int pos = 0;
  for (char* c = line; *c; ++c)
    if (strchr("AaKkQqJjTt1098765432- ", *c)) line[pos++] = *c;
  line[pos] = '\0';

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
    if (line[0] == '-') ++line;
  }
  return hand;
}

void ReadHands(Hands& hands, std::vector<int>& trumps, std::vector<int>& lead_seats) {
  FILE* input_file = stdin;
  if (options.input) {
    input_file = fopen(options.input, "rt");
    if (!input_file) {
      fprintf(stderr, "Input file not found: '%s'.\n", options.input);
      exit(-1);
    }
  }
  // read hands
  char line[NUM_SEATS][120];
  CHECK(fgets(line[NORTH], sizeof(line[NORTH]), input_file));
  CHECK(fgets(line[WEST], sizeof(line[WEST]), input_file));
  char* gap = strstr(line[WEST], "    ");
  if (!gap) gap = strstr(line[WEST], "\t");
  if (gap != NULL && gap != line[WEST]) {
    // East hand is on the same line as West.
    strcpy(line[EAST], gap);
    *gap = '\0';
  } else {
    CHECK(fgets(line[EAST], sizeof(line[EAST]), input_file));
  }
  CHECK(fgets(line[SOUTH], sizeof(line[SOUTH]), input_file));

  int num_tricks = 0;
  for (int seat = 0; seat < NUM_SEATS; ++seat) {
    hands[seat] = ParseHand(line[seat]);
    if (seat == 0)
      num_tricks = hands[seat].Size();
    else if (num_tricks != hands[seat].Size()) {
      printf("%s has %d cards, while %s has %d.\n", SeatName(seat), hands[seat].Size(),
             SeatName(0), num_tricks);
      exit(-1);
    }
  }

  if (!options.full_analysis && fscanf(input_file, " %s ", line[0]) == 1)
    trumps = {CharToSuit(line[0][0])};

  if (!options.full_analysis && fscanf(input_file, " %s ", line[0]) == 1)
    lead_seats = {CharToSeat(line[0][0])};

  if (input_file != stdin) fclose(input_file);
}

int MemoryEnhancedTestDriver(std::function<int(int, int)> search, int num_tricks,
                             int guess_tricks) {
  int upperbound = num_tricks;
  int lowerbound = 0;
  int ns_tricks = guess_tricks;
  if (options.displaying_depth > 0)
    printf("Lowerbound: %d\tUpperbound: %d\n", lowerbound, upperbound);
  while (lowerbound < upperbound) {
    int beta = (ns_tricks == lowerbound ? ns_tricks + 1 : ns_tricks);
    ns_tricks = search(beta - 1, beta);
    if (ns_tricks < beta)
      upperbound = ns_tricks;
    else
      lowerbound = ns_tricks;
    if (options.displaying_depth > 0)
      printf("Lowerbound: %d\tUpperbound: %d\n", lowerbound, upperbound);
  }
  return ns_tricks;
}

double Elapse(const timeval& from, const timeval& to) {
  return (to.tv_sec - from.tv_sec) + (double(to.tv_usec) - from.tv_usec) * 1e-6;
}

}  // namespace

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
  void ShowUsage() {
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
      sprintf(contract, "%d%s by %s", level, SuitSign(trump), declarer);
    } else {
      starting_ew_tricks = TOTAL_TRICKS - num_tricks;
      ns_contract = false;
      int level = TOTAL_TRICKS - target_ns_tricks - 6;
      auto declarer = starting_ew_tricks == 0 ? SeatName((lead_seat + 3) % 4) : "EW";
      sprintf(contract, "%d%s by %s", level, SuitSign(trump), declarer);
    }
  }

  bool SetupTrick(Play& play) {
    // TODO: Clean up! SearchWithCache() recomputes the same info.
    if (play.depth > 0) {
      play.trick->all_cards = play.hands.all_cards();
      play.ns_tricks_won =
          play.PreviousPlay().ns_tricks_won + play.PreviousPlay().NsWon();
      play.seat_to_play = play.PreviousPlay().WinningSeat();
    }

    play.ComputeShape();
    play.trick->ComputeRelativeHands(play.depth, play.hands);

    int trick_index = play.depth / 4;
    printf("------ %s: NS %d EW %d ------\n", contract,
           starting_ns_tricks + play.ns_tricks_won,
           starting_ew_tricks + trick_index - play.ns_tricks_won);
    play.hands.ShowDetailed(rotation);
    if (trick_index == num_tricks - 1) {
      Cards rank_winners;
      int ns_tricks_won = play.CollectLastTrick(&rank_winners);
      printf("====== %s: NS %d EW %d ======\n", contract,
             starting_ns_tricks + ns_tricks_won,
             starting_ew_tricks + trick_index + 1 - ns_tricks_won);
      return false;
    }
    return true;
  }

  typedef std::map<int, int> CardTricks;

  CardTricks EvaluateCards(Play& play, int ns_tricks, bool ns_contract) {
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

      auto search = [&play, card](int alpha, int beta) {
        play.PlayCard(card);
        Cards rank_winners;
        int ns_tricks = play.NextPlay().SearchWithCache(alpha, beta, &rank_winners);
        play.UnplayCard();
        return ns_tricks;
      };
      int new_ns_tricks = MemoryEnhancedTestDriver(search, num_tricks, ns_tricks);
      card_tricks[card] = new_ns_tricks;

      int trick_diff = ns_contract ? new_ns_tricks - target_ns_tricks
                                   : target_ns_tricks - new_ns_tricks;
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

  Action SelectCard(const CardTricks& card_tricks, const Play& play, int* card_to_play) {
    // Auto-play when there is only one choice.
    if (card_tricks.size() == 1) {
      *card_to_play = card_tricks.begin()->first;
      printf("%s.\n", ColoredNameOf(*card_to_play));
      return PLAY;
    }

    // Choose the optimal play, using rank as the tie-breaker.
    *card_to_play = -1;
    if (play.IsNs(play.seat_to_play)) {
      int max_ns_tricks = -1;
      for (const auto& pair : card_tricks) {
        if (pair.second > max_ns_tricks || (pair.second == max_ns_tricks &&
                                            RankOf(pair.first) < RankOf(*card_to_play))) {
          *card_to_play = pair.first;
          max_ns_tricks = pair.second;
        }
      }
    } else {
      int min_ns_tricks = TOTAL_TRICKS + 1;
      for (const auto& pair : card_tricks) {
        if (pair.second < min_ns_tricks || (pair.second == min_ns_tricks &&
                                            RankOf(pair.first) < RankOf(*card_to_play))) {
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
            if (playable_cards.find(CardOf(suit, rank)) == playable_cards.end())
              rank = -1;
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
            if (playable_cards.find(CardOf(suit, rank)) == playable_cards.end())
              suit = -1;
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

  char* ColoredNameOf(int card) {
    static char name[32];
    sprintf(name, "%s %c", SuitSign(SuitOf(card)), NameOf(card)[1]);
    return name;
  }

  char GetRawChar() {
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

int main(int argc, char* argv[]) {
  options.Read(argc, argv);

  Hands hands;
  std::vector<int> trumps = {NOTRUMP, SPADE, HEART, DIAMOND, CLUB};
  std::vector<int> lead_seats = {WEST, EAST, NORTH, SOUTH};
  if (options.randomize) {
    hands.Randomize();
    if (!options.interactive) hands.ShowCompact();
  } else
    ReadHands(hands, trumps, lead_seats);

  if (options.trump != -1) {
    trumps.clear();
    trumps.push_back(options.trump);
  }

  timeval now;
  gettimeofday(&now, NULL);
  timeval start_time = now;
  for (int trump : trumps) {
    if (!options.interactive) {
      printf("%c", SuitName(trump)[0]);
    }
    // TODO: Best guess with losing trick count.
    int num_tricks = hands[WEST].Size();
    int guess_tricks = std::min(options.guess, num_tricks);
    for (int seat_to_play : lead_seats) {
      MinMax min_max(hands, trump, seat_to_play);
      auto search = [&min_max](int alpha, int beta) {
        return min_max.Search(alpha, beta);
      };
      int ns_tricks = MemoryEnhancedTestDriver(search, num_tricks, guess_tricks);
      guess_tricks = std::min(ns_tricks + 1, TOTAL_TRICKS);

      if (!options.interactive) {
        if (seat_to_play == WEST || seat_to_play == EAST)
          printf(" %2d", ns_tricks);
        else
          printf(" %2d", num_tricks - ns_tricks);
        fflush(stdout);
        continue;
      }
      if (num_tricks < TOTAL_TRICKS ||
          (num_tricks == TOTAL_TRICKS && ns_tricks >= 7 &&
           (seat_to_play == WEST || seat_to_play == EAST)) ||
          (num_tricks == TOTAL_TRICKS && ns_tricks < 7 &&
           (seat_to_play == NORTH || seat_to_play == SOUTH)))
        InteractivePlay(hands, trump, seat_to_play, ns_tricks);
      else {
        int declarer = (seat_to_play + 3) % NUM_SEATS;
        printf("%s can't make a %s contract.\n", SeatName(declarer), SuitSign(trump));
      }
    }
    if (options.show_stats) {
      common_bounds_cache.ShowStatistics();
      cutoff_cache.ShowStatistics();
    }

    common_bounds_cache.Reset();
    cutoff_cache.Reset();

    if (!options.interactive) {
      struct rusage usage;
      getrusage(RUSAGE_SELF, &usage);
      gettimeofday(&now, NULL);
      printf(" %4.1f s %5.1f M\n", Elapse(start_time, now), usage.ru_maxrss / 1024.0);
      fflush(stdout);
    }
  }
  return 0;
}
