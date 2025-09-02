#include <assert.h>
#include <ctype.h>
#ifdef __BMI2__
#include <immintrin.h>
#endif
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
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
#define STATS(statement) statement
#else
#define CHECK(statement)
#define VERBOSE(statement)
#define STATS(statement)
#endif
// clang-format on

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

const char RankName(int rank) {
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

char suit_of[TOTAL_CARDS];
char rank_of[TOTAL_CARDS];
char card_of[NUM_SUITS][16];
char name_of[TOTAL_CARDS][4];

int SuitOf(int card) { return suit_of[card]; }
int RankOf(int card) { return rank_of[card]; }
int CardOf(int suit, int rank) { return card_of[suit][rank]; }
uint64_t MaskOf(int suit) { return 0x1fffULL << (suit * NUM_RANKS); }
const char* NameOf(int card) { return name_of[card]; }

bool LowerRank(int card1, int card2) { return card1 > card2; }
bool HigherRank(int card1, int card2) { return card1 < card2; }

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
  char* code = nullptr;
  char* input_file = nullptr;
  char* shuffle_seats = nullptr;
  int trump = -1;
  int guess_tricks = -1;
  int displaying_depth = -1;
  int stats_level = 0;
  int show_hands_mask = 2;
  bool deal_only = false;
  bool discard_suit_bottom = false;
  bool randomize = false;
  bool ignore_trump_and_lead = false;
  bool play_interactively = false;

  void Read(int argc, char* argv[]) {
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

  void ShowUsage(char* name) {
    printf("%s  A fast double-dummy solver for the card game of Bridge.\n", name);
    printf("\t-r           Solve a random deal.\n"
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
           "\t-i           Ignore the trump and the lead specified in the input file.\n"
           "\t-t <trump>   Solve for the specified trump, one of {N, S, H, D, C}.\n"
           "\t-d           Discard only the smallest card in a suit, imprecise but faster.\n");
    exit(0);
  }
} options;

double Now() {
  timeval now;
  gettimeofday(&now, nullptr);
  return now.tv_sec + now.tv_usec * 1e-6;
}

template <class T>
int BitSize(T v) {
  return sizeof(v) * 8;
}

uint64_t PackBits(uint64_t source, uint64_t mask) {
#ifdef __BMI2__
  return _pext_u64(source, mask);
#else
  if (source == 0) return 0;
  uint64_t packed = 0;
  for (uint64_t bit = 1; mask; bit <<= 1, mask &= mask - 1)
    if (source & mask & -mask) packed |= bit;
  return packed;
#endif
}

uint64_t UnpackBits(uint64_t source, uint64_t mask) {
#ifdef __BMI2__
  return _pdep_u64(source, mask);
#else
  if (source == 0) return 0;
  uint64_t unpacked = 0;
  for (uint64_t bit = 1; source; bit <<= 1, mask &= mask - 1)
    if (source & bit) {
      unpacked |= mask & -mask;
      source &= ~bit;
    }
  return unpacked;
#endif
}

class Cards {
 public:
  Cards() : bits(0) {}
  Cards(uint64_t b) : bits(b) {}
  uint64_t Value() const { return bits; }

  int Size() const { return __builtin_popcountll(bits); }
  bool Have(int card) const { return bits & Bit(card); }
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
  Cards Complement() const { return ((1ULL << TOTAL_CARDS) - 1) ^ bits; }
  bool Include(const Cards& c) const { return Intersect(c) == c; }
  bool StrictlyInclude(const Cards& c) const { return Include(c) && bits != c.bits; }

  Cards Add(int card) { return bits |= Bit(card); }
  Cards Remove(int card) { return bits &= ~Bit(card); }

  Cards Add(const Cards& c) { return bits |= c.bits; }
  Cards Remove(const Cards& c) { return bits &= ~c.bits; }
  Cards ClearSuit(int suit) { return bits &= ~MaskOf(suit); }

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

  void Deal(Cards cards, std::vector<int> seats) {
    std::mt19937 random(static_cast<uint64_t>(Now() * 1000));
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

  void Decode(char* code) {
    uint64_t values[3];
    int num_values = sscanf(code, "%lX,%lX,%lX", values, values + 1, values + 2);
    assert(num_values == 3);
    auto mask = (1ULL << TOTAL_CARDS) - 1;
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

  void ShowCode() const {
    uint64_t values[3];
    auto mask = (1ULL << TOTAL_CARDS) - 1;
    for (int seat = 0; seat < NUM_SEATS - 1; ++seat) {
      values[seat] = PackBits(hands[seat].Value(), mask);
      mask &= ~hands[seat].Value();
    }
    printf("# %lX,%lX,%lX\n", values[0], values[1], values[2]);
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

template <class Entry, int input_size>
class Cache {
 public:
  Cache(const char* name, int bits)
      : cache_name(name), bits(bits), size(1 << bits), entries(new Entry[size]) {
    Reset();
  }

  void Reset() {
    probe_distance = 0;
    load_count = lookups = lookup_probes = hits = updates = update_probes = 0;
    for (int i = 0; i < size; ++i) entries[i].Reset(0);
  }

  void ShowStatistics() const {
    printf("--- %s Statistics ---\n", cache_name);
    printf("lookups: %8d   probes: %8d (%.2f/lookup)   hits: %8d (%5.2f%%)\n",
           lookups, lookup_probes, lookup_probes * 1.0 / lookups, hits, hits * 100.0 / lookups);
    printf("updates: %8d   probes: %8d (%.2f/update)\n",
           updates, update_probes, update_probes * 1.0 / updates);
    printf("entries: %8d   loaded: %8d (%5.2f%%)\n", size, load_count,
           load_count * 100.0 / size);

    int recursive_load = 0;
    for (int i = 0; i < size; ++i)
      if (entries[i].hash != 0) {
        recursive_load += entries[i].Size();
        STATS(if (options.stats_level > 1) entries[i].Show());
      }
    if (recursive_load > load_count) printf("recursive load: %8d\n", recursive_load);
  }

  const Entry* Lookup(Cards cards[input_size]) const {
    STATS(++lookups);
    uint64_t hash = Hash(cards);
    uint64_t index = hash >> (BitSize(hash) - bits);

    for (int d = 0; d < probe_distance; ++d) {
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

  Entry* Update(Cards cards[input_size]) {
    if (load_count >= size * 3 / 4) Resize();

    STATS(++updates);
    uint64_t hash = Hash(cards);
    uint64_t index = hash >> (BitSize(hash) - bits);

    // Linear probing benefits from hardware prefetch.
    for (int d = 0; ; ++d) {
      Entry& entry = entries[(index + d) & (size - 1)];
      if (entry.hash == hash) return &entry;
      if (entry.hash == 0) {
        probe_distance = std::max(probe_distance, d + 1);
        ++load_count;
        entry.Reset(hash);
        return &entry;
      }
      STATS(++update_probes);
    }
  }

 private:
  uint64_t Hash(Cards cards[input_size]) const {
    static constexpr uint64_t hash_rand[2] = {0x9b8b4567327b23c7ULL, 0x643c986966334873ULL};
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
      for (int d = 0; ; ++d) {
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

  const char* cache_name;
  int bits;
  int size;
  int probe_distance;
  std::unique_ptr<Entry[]> entries;

  mutable int load_count;
  mutable int lookups, lookup_probes, hits;
  mutable int updates, update_probes;
};

#pragma pack(push, 4)
struct Bounds {
  char lower;
  char upper;

  bool Empty() const { return upper < lower; }
  Bounds Intersect(Bounds b) const {
    return {std::max(lower, b.lower), std::min(upper, b.upper)};
  }
  bool Include(Bounds b) const { return Intersect(b) == b; }
  bool operator==(Bounds b) const { return b.lower == lower && b.upper == upper; }
  bool operator!=(Bounds b) const { return !(*this == b); }
  bool Cutoff(int beta) const { return lower >= beta || upper < beta; }
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

template <class T>
class Vector {
 public:
  ~Vector() { clear(); }

  void clear() {
    for (size_t i = 0; i < count; ++i) (*this)[i].~T();
    count = capacity = 0;
    delete[] items;
    items = nullptr;
  }

  void resize(size_t new_size) {
    if (capacity < new_size) {
      capacity = std::max(capacity * 2, (int)new_size);
      auto old_items = items;
      items = new char[capacity * sizeof(T)];
      memcpy(items, old_items, count * sizeof(T));
      delete[] old_items;
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
  Hands hands;
  Bounds bounds;
  uint16_t padding;
  Vector<Pattern> patterns;

  Pattern(const Hands& hands = Hands(), Bounds bounds = Bounds())
      : hands(hands), bounds(bounds) {}

  void Reset() {
    hands = Hands();
    bounds = {0, TOTAL_TRICKS};
    patterns.clear();
  }

  void MoveFrom(Pattern& p) {
    hands = p.hands;
    bounds = p.bounds;
    patterns.swap(p.patterns);
  }

  const Pattern* Lookup(const Pattern& new_pattern, int beta) const {
    for (size_t i = 0; i < patterns.size(); ++i) {
      auto& pattern = patterns[i];
      if (!(new_pattern <= pattern)) continue;
      if (pattern.bounds.Cutoff(beta)) return &pattern;
      if (auto detail = pattern.Lookup(new_pattern, beta)) return detail;
    }
    return nullptr;
  }

  void Update(Pattern& new_pattern) {
    for (size_t i = 0; i < patterns.size(); ++i) {
      auto& pattern = patterns[i];
      if (new_pattern == pattern) {
        pattern.UpdateBounds(new_pattern.bounds);
        return;
      } else if (new_pattern <= pattern) {
        // Old pattern is more generic. Add new pattern under.
        new_pattern.bounds = new_pattern.bounds.Intersect(pattern.bounds);
        CHECK(!new_pattern.bounds.Empty());
        if (new_pattern.bounds != pattern.bounds) pattern.Update(new_pattern);
        return;
      } else if (pattern <= new_pattern) {
        // New pattern is more generic. Absorb sub-patterns.
        pattern.UpdateBounds(new_pattern.bounds);
        if (pattern.bounds != new_pattern.bounds) new_pattern.Append(pattern);
        else new_pattern.patterns.swap(pattern.patterns);
        for (++i; i < patterns.size(); ++i) {
          auto& old_pattern = patterns[i];
          if (!(old_pattern <= new_pattern)) continue;
          old_pattern.UpdateBounds(new_pattern.bounds);
          if (old_pattern.bounds != new_pattern.bounds) new_pattern.Append(old_pattern);
          else if (!new_pattern.patterns.size()) new_pattern.patterns.swap(old_pattern.patterns);
          else new_pattern.Append(old_pattern.patterns);
          Delete(i);
          --i;
        }
        pattern.MoveFrom(new_pattern);
        return;
      }
    }
    Append(new_pattern);
  }

  void UpdateBounds(Bounds new_bounds) {
    auto old_bounds = bounds;
    bounds = bounds.Intersect(new_bounds);
    CHECK(!bounds.Empty());
    if (bounds == old_bounds) return;
    for (size_t i = 0; i < patterns.size(); ++i) {
      patterns[i].UpdateBounds(bounds);
      if (patterns[i].bounds != bounds) continue;
      // Get the subpatterns out as resizing patterns in Append renders
      // patterns[i].patterns invalid.
      Vector<Pattern> subpatterns;
      subpatterns.swap(patterns[i].patterns);
      Append(subpatterns);
      Delete(i);
      --i;
    }
  }

  void Append(Pattern& new_pattern) {
    patterns.resize(patterns.size() + 1);
    patterns.back().MoveFrom(new_pattern);
  }

  void Append(Vector<Pattern>& new_patterns) {
    auto new_size = new_patterns.size();
    if (new_size == 0) return;
    auto size = patterns.size();
    patterns.resize(size + new_size);
    for (size_t i = 0; i < new_size; ++i)
      patterns[size + i].MoveFrom(new_patterns[i]);
  }

  void Delete(size_t i) {
    patterns[i].MoveFrom(patterns.back());
    patterns.pop_back();
  }

  // This pattern is more detailed than (a subset of) the other pattern.
  bool operator<=(const Pattern& p) const {
    return hands[WEST].Include(p.hands[WEST]) & hands[NORTH].Include(p.hands[NORTH]) &
           hands[EAST].Include(p.hands[EAST]) & hands[SOUTH].Include(p.hands[SOUTH]);
  }

  bool operator==(const Pattern& p) const {
    return (p.hands[WEST] == hands[WEST]) & (p.hands[NORTH] == hands[NORTH]) &
           (p.hands[EAST] == hands[EAST]) & (p.hands[SOUTH] == hands[SOUTH]);
  }

  Cards GetRankWinners(Cards all_cards) const {
    Cards relative_rank_winners = hands.all_cards();
    Cards rank_winners;
    for (int suit = 0; suit < NUM_SUITS; ++suit) {
      if (!relative_rank_winners.Suit(suit)) continue;
      auto packed = relative_rank_winners.Suit(suit).Value() >> (suit * NUM_RANKS);
      rank_winners.Add(Cards(UnpackBits(packed, all_cards.Suit(suit).Value())));
    }
    return rank_winners;
  }

  int Size() const {
    int sum = 1;
    for (size_t i = 0; i < patterns.size(); ++i) sum += patterns[i].Size();
    return sum;
  }

  void Show(Shape shape, int level = 1, Bounds parent_bounds = {0, TOTAL_TRICKS}) const {
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
      puts(level > 1 && bounds == parent_bounds ? " dup" : "");
    }
    for (size_t i = 0; i < patterns.size(); ++i) patterns[i].Show(shape, level + 1, bounds);
  }
};

struct ShapeEntry {
  uint64_t hash;
  Pattern pattern;
#ifdef _DEBUG
  Shape shape;
  short seat_to_play;
  mutable uint16_t hits, cuts;

  void Show() const {
    printf("hash %016lx shape %016lx seat %c size %ld total size %d hits %d cuts %d\n",
           hash, shape.Value(), SeatLetter(seat_to_play), pattern.patterns.size(), Size(),
           hits, cuts);
    pattern.Show(shape, 0);
  }
#endif

  int Size() const { return pattern.Size() - 1; }

  void Reset(uint64_t hash_in) {
    hash = hash_in;
    pattern.Reset();
#ifdef _DEBUG
    shape = Shape();
    seat_to_play = -1;
    hits = cuts = 0;
#endif
  }

  void MoveTo(ShapeEntry& to) {
    to.hash = hash;
    to.pattern.MoveFrom(pattern);
#ifdef _DEBUG
    to.shape = shape;
    to.seat_to_play = seat_to_play;
    to.hits = hits;
    to.cuts = cuts;
#endif
  }

  std::pair<const Hands*, Bounds> Lookup(const Pattern& new_pattern, int beta) const {
    STATS(++hits);
    if (pattern.bounds.Cutoff(beta) && new_pattern <= pattern) {
      STATS(++cuts);
      CHECK(pattern.Lookup(new_pattern, beta));
      return {&pattern.hands, pattern.bounds};
    }
    auto cached_pattern = pattern.Lookup(new_pattern, beta);
    if (cached_pattern) {
      STATS(++cuts);
      const_cast<Pattern*>(&pattern)->hands = cached_pattern->hands;
      const_cast<Pattern*>(&pattern)->bounds = cached_pattern->bounds;
      return {&cached_pattern->hands, cached_pattern->bounds};
    }
    return {nullptr, Bounds{}};
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

Cache<ShapeEntry, 2> common_bounds_cache("Common Bounds Cache", 15);
Cache<CutoffEntry, 2> cutoff_cache("Cut-off Cache", 16);

struct Trick {
  Shape shape;
  Cards all_cards;
  Hands relative_hands;
  int lead_suit;

  // A relative hand contains relative cards.
  void ComputeRelativeHands(int depth, const Hands& hands) {
    if (depth < 4) {
      for (int suit = 0; suit < NUM_SUITS; ++suit)
        ConvertToRelativeSuit(hands, suit, all_cards.Suit(suit));
    } else {
      // Recompute the relative cards for suits changed by the last trick.
      auto prev_trick = this - 1;
      auto prev_trick_cards = prev_trick->all_cards.Different(all_cards);
      relative_hands = prev_trick->relative_hands;
      while (prev_trick_cards) {
        auto suit = SuitOf(prev_trick_cards.Top());
        prev_trick_cards.ClearSuit(suit);
        ConvertToRelativeSuit(hands, suit, all_cards.Suit(suit));
      }
    }
  }

  // Whether a card is equivalent to one of the tried cards.
  bool IsEquivalent(int card, Cards tried_suit_cards, Cards hand) const {
    if (!tried_suit_cards) return false;
    if (auto above = tried_suit_cards.Slice(0, card))
      if (all_cards.Slice(above.Bottom(), card) == hand.Slice(above.Bottom(), card))
        return true;
    if (auto below = tried_suit_cards.Slice(card + 1, TOTAL_CARDS))
      if (all_cards.Slice(card, below.Top()) == hand.Slice(card, below.Top()))
        return true;
    return false;
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
        if (RelativeRank(prev_card, suit) != RelativeRank(card, suit) + 1)
          filtered_cards.Add(card);
        prev_card = card;
      }
    }
    return filtered_cards;
  }

  // A pattern hand contains relative cards and rank-irrelevant cards.
  std::pair<Hands, Cards> ComputePatternHands(Cards rank_winners) const {
    Cards relative_rank_winners, extended_rank_winners;
    for (int suit = 0; suit < NUM_SUITS; ++suit) {
      if (!rank_winners.Suit(suit)) continue;

      int bottom_rank_winner = RelativeCard(rank_winners.Suit(suit).Bottom(), suit);
      for (int seat = 0; seat < NUM_SEATS; ++seat) {
        if (!relative_hands[seat].Have(bottom_rank_winner)) continue;
        auto suit_cards = relative_hands[seat].Suit(suit);
        // Extend bottom rank winner to its lowest equivalent card.
        bottom_rank_winner += __builtin_ctzll(~(suit_cards.Value() >> (bottom_rank_winner + 1)));
        // Suit bottom can't win by rank. Compensate the inaccuracy of fast tricks.
        if (bottom_rank_winner == relative_hands.all_cards().Suit(suit).Bottom())
          // Extend bottom rank winner to its highest equivalent card and go one rank higher.
          bottom_rank_winner -= __builtin_clzll(~(suit_cards.Value() << (63 - bottom_rank_winner)));
        break;
      }
      relative_rank_winners.Add(Cards(MaskOf(suit)).Slice(0, bottom_rank_winner + 1));
      auto packed = relative_rank_winners.Suit(suit).Value() >> (suit * NUM_RANKS);
      extended_rank_winners.Add(Cards(UnpackBits(packed, all_cards.Suit(suit).Value())));
    }

    Hands pattern_hands;
    for (int seat = 0; seat < NUM_SEATS; ++seat)
      pattern_hands[seat] = relative_hands[seat].Intersect(relative_rank_winners);
    return {pattern_hands, extended_rank_winners};
  }

 private:
  void ConvertToRelativeSuit(const Hands& hands, int suit, Cards all_suit_cards) {
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

struct Stat {
  int num_visits = 0;
  int num_branches = 0;

  void Show(int depth) {
    if (num_visits)
      printf("%2d: %7d * %.2f\n", depth, num_visits, double(num_branches) / num_visits);
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

  Result SearchWithCache(int beta) {
    if (!TrickStarting()) {
      ns_tricks_won = PreviousPlay().ns_tricks_won;
      seat_to_play = PreviousPlay().NextSeat();
      return EvaluatePlayableCards(beta);
    }

    if (depth > 0) {
      ns_tricks_won = PreviousPlay().ns_tricks_won + PreviousPlay().NsWon();
      seat_to_play = PreviousPlay().WinningSeat();
    }

    if (ns_tricks_won >= beta) return {ns_tricks_won, {}};
    int remaining_tricks = hands.num_tricks();
    if (ns_tricks_won + remaining_tricks < beta) return {ns_tricks_won + remaining_tricks, {}};

    if (remaining_tricks == 1) return CollectLastTrick();

    trick->all_cards = hands.all_cards();
    ComputeShape();
    trick->ComputeRelativeHands(depth, hands);

    Cards shape_index[2] = {trick->shape.Value(), seat_to_play};
    auto* shape_entry = common_bounds_cache.Lookup(shape_index);
    if (shape_entry) {
      auto [hands, bounds] =
          shape_entry->Lookup(trick->relative_hands, beta - ns_tricks_won);
      if (hands) {
        Pattern matched_pattern(*hands, bounds);
        auto rank_winners = matched_pattern.GetRankWinners(trick->all_cards);
        VERBOSE(ShowPattern("match", matched_pattern, trick->shape));
        int lower = bounds.lower + ns_tricks_won;
        if (lower >= beta) {
          VERBOSE(printf("%2d: beta cut %d\n", depth, lower));
          return {lower, rank_winners};
        }
        int upper = bounds.upper + ns_tricks_won;
        VERBOSE(printf("%2d: alpha cut %d\n", depth, upper));
        return {upper, rank_winners};
      }
    }

    auto [ns_tricks, rank_winners] = SearchAtTrickStart(beta);
    auto bounds = ns_tricks < beta
                      ? Bounds{0, char(ns_tricks - ns_tricks_won)}
                      : Bounds{char(ns_tricks - ns_tricks_won), char(remaining_tricks)};

    auto [pattern_hands, extended_rank_winners] = trick->ComputePatternHands(rank_winners);
    Pattern new_pattern(pattern_hands, bounds);
    VERBOSE(ShowPattern("update", new_pattern, trick->shape));
    auto* new_shape_entry = common_bounds_cache.Update(shape_index);
#ifdef _DEBUG
    new_shape_entry->shape = trick->shape;
    new_shape_entry->seat_to_play = seat_to_play;
#endif
    new_shape_entry->pattern.Update(new_pattern);
    return {ns_tricks, extended_rank_winners};
  }

 private:
  Result SearchAtTrickStart(int beta) {
    auto [fast_tricks, fast_rank_winners] = FastTricks();
    if (fast_tricks == 0 && trump != NOTRUMP)
      std::tie(fast_tricks, fast_rank_winners) =
        SlowTrumpTricks(hands[seat_to_play].Suit(trump), hands[Partner()].Suit(trump),
                        hands[LeftHandOpp()].Suit(trump), hands[RightHandOpp()].Suit(trump), true);
    if (NsToPlay() && ns_tricks_won + fast_tricks >= beta) {
      VERBOSE(printf("%2d: beta fast cut %d+%d\n", depth, ns_tricks_won, fast_tricks));
      return {ns_tricks_won + fast_tricks, fast_rank_winners};
    }
    int remaining_tricks = hands.num_tricks();
    if (!NsToPlay() && ns_tricks_won + (remaining_tricks - fast_tricks) < beta) {
      VERBOSE(printf("%2d: alpha fast cut %d+%d\n", depth, ns_tricks_won,
                     remaining_tricks - fast_tricks));
      return {ns_tricks_won + (remaining_tricks - fast_tricks), fast_rank_winners};
    }
    auto [slow_tricks, slow_rank_winners] = trick->all_cards.Suit(trump)
        ? TopTrumpTricks(hands[LeftHandOpp()].Suit(trump), hands[RightHandOpp()].Suit(trump))
        : SlowNoTrumpTricks(hands[seat_to_play], hands[Partner()]);
    if (slow_tricks == 0 && trick->all_cards.Suit(trump))
      std::tie(slow_tricks, slow_rank_winners) =
        SlowTrumpTricks(hands[LeftHandOpp()].Suit(trump), hands[RightHandOpp()].Suit(trump),
                        hands[Partner()].Suit(trump), hands[seat_to_play].Suit(trump), false);
    if (NsToPlay() && ns_tricks_won + (remaining_tricks - slow_tricks) < beta) {
      VERBOSE(printf("%2d: alpha slow cut %d+%d\n", depth, ns_tricks_won,
                     remaining_tricks - slow_tricks));
      return {ns_tricks_won + (remaining_tricks - slow_tricks), slow_rank_winners};
    }
    if (!NsToPlay() && ns_tricks_won + slow_tricks >= beta) {
      VERBOSE(printf("%2d: beta slow cut %d+%d\n", depth, ns_tricks_won, slow_tricks));
      return {ns_tricks_won + slow_tricks, slow_rank_winners};
    }
    return EvaluatePlayableCards(beta);
  }

  Result EvaluatePlayableCards(int beta) {
    STATS(++stats[depth].num_visits);
    ordered_cards.Reset();
    auto playable_cards = GetPlayableCards();
    Cards cutoff_index[2];
    BuildCutoffIndex(cutoff_index);
    Cards cutoff_cards = playable_cards.Intersect(LookupCutoffCards(cutoff_index));
    if (cutoff_cards) {
      ordered_cards.AddCard(cutoff_cards.Top());
      playable_cards.Remove(cutoff_cards);
    } else {
      OrderCards(playable_cards);
      playable_cards = Cards();
    }

    int ns_tricks = NsToPlay() ? 0 : TOTAL_TRICKS;
    int min_relevant_ranks[NUM_SUITS] = {TWO, TWO, TWO, TWO};
    Cards rank_winners, tried_cards;
    for (int i = 0; i < ordered_cards.Size(); ++i) {
      int card = ordered_cards.Card(i), suit = SuitOf(card), rank = RankOf(card);
      // Try a card if its rank is still relevant and it isn't equivalent to a tried card.
      if (rank >= min_relevant_ranks[suit] &&
          !trick->IsEquivalent(card, tried_cards.Suit(suit), hands[seat_to_play])) {
        STATS(++stats[depth].num_branches);
        PlayCard(card);
        VERBOSE(ShowTricks(beta, 0, true));
        auto [branch_ns_tricks, branch_rank_winners] = NextPlay().SearchWithCache(beta);
        if (TrickEnding()) branch_rank_winners.Add(GetTrickRankWinner());
        VERBOSE(ShowTricks(beta, branch_ns_tricks, false));
        UnplayCard();

        ns_tricks = NsToPlay() ? std::max(ns_tricks, branch_ns_tricks)
                               : std::min(ns_tricks, branch_ns_tricks);
        if (NsToPlay() ? ns_tricks >= beta : ns_tricks < beta) {  // cut-off
          if (!cutoff_cards.Have(card)) SaveCutoffCard(cutoff_index, card);
          VERBOSE(printf("%2d: search cut @%d\n", depth, i));
          return {ns_tricks, branch_rank_winners};
        }

        rank_winners.Add(branch_rank_winners);
        // If this card's rank is irrelevant, a relevant rank must be higher.
        auto suit_rank_winners = branch_rank_winners.Suit(suit);
        if (!suit_rank_winners) min_relevant_ranks[suit] = NUM_RANKS;
        else if (LowerRank(card, suit_rank_winners.Bottom()))
          min_relevant_ranks[suit] = std::max(min_relevant_ranks[suit],
                                              RankOf(suit_rank_winners.Bottom()));
      }
      tried_cards.Add(card);
      if (playable_cards) {
        OrderCards(playable_cards);
        playable_cards = Cards();
      }
    }
    return {ns_tricks, rank_winners};
  }

  template <bool SUIT_CONTRACT>
  void Lead(Cards playable_cards) {
    Cards good_leads, high_leads, leads, bad_leads, trump_leads, ruff_leads;
    auto pd_hand = hands[Partner()];
    auto lho_hand = hands[LeftHandOpp()], rho_hand = hands[RightHandOpp()];
    for (int suit = 0; suit < NUM_SUITS; ++suit) {
      auto my_suit = playable_cards.Suit(suit);
      if (!my_suit) continue;
      if (SUIT_CONTRACT) {
        if (suit == trump) {
          trump_leads.Add(my_suit.Top());
          trump_leads.Add(my_suit.Bottom());
          continue;
        }
        if (lho_hand.Suit(trump) && !lho_hand.Suit(suit)) continue;
        if (rho_hand.Suit(trump) && !rho_hand.Suit(suit)) continue;
      }
      auto pd_suit = pd_hand.Suit(suit), our_suits = my_suit.Union(pd_suit);
      auto lho_suit = lho_hand.Suit(suit);
      auto all_suit_cards = trick->all_cards.Suit(suit);
      int a = all_suit_cards.Top();
      int k = all_suit_cards.Remove(a).Top();
      int q = all_suit_cards.Remove(k).Top();
      int j = all_suit_cards.Remove(q).Top();
      int t = all_suit_cards.Remove(j).Top();
      if (pd_suit.Size() >= 2 && lho_suit.Size() >= 2) {
        if ((pd_suit.Have(k) && lho_suit.Have(a)) ||
            (pd_suit.Have(a) && lho_suit.Have(k) &&
             (pd_suit.Have(q) || our_suits.Have(Cards().Add(q).Add(j)))) ||
            (pd_suit.Have(k) && lho_suit.Have(q) &&
             (pd_suit.Have(j) || our_suits.Have(Cards().Add(j).Add(t))))) {
          good_leads.Add(my_suit.Top());
          good_leads.Add(my_suit.Bottom());
          continue;
        }
      }
      auto rho_suit = rho_hand.Suit(suit);
      auto partnership_cards = hands.partnership_cards(seat_to_play);
      if (my_suit.Size() >= 2 && rho_suit.Size() >= 2) {
        if ((my_suit.Have(a) && rho_suit.Have(k)) ||
            (my_suit.Have(k) && rho_suit.Have(a) && !partnership_cards.Have(q))) {
          if (SUIT_CONTRACT) {
            bad_leads.Add(my_suit.Top());
            bad_leads.Add(my_suit.Bottom());
          }
          continue;
        }
      }
      Cards akq = Cards().Add(a).Add(k).Add(q);
      if (lho_suit && rho_suit && partnership_cards.Intersect(akq).Size() >= 2) {
        high_leads.Add(my_suit.Top());
        high_leads.Add(my_suit.Bottom());
        continue;
      }
      if (SUIT_CONTRACT && !pd_suit && lho_suit && rho_suit && pd_hand.Suit(trump) &&
          pd_hand.Suit(trump).Size() <= playable_cards.Suit(trump).Size() &&
          my_suit.Bottom() != a) {
        ruff_leads.Add(my_suit.Bottom());
        continue;
      }
      leads.Add(my_suit.Top());
      leads.Add(my_suit.Bottom());
    }
    if (SUIT_CONTRACT) {
      ordered_cards.AddCards(ruff_leads);
      playable_cards.Remove(ruff_leads);
    }
    ordered_cards.AddCards(good_leads);
    playable_cards.Remove(good_leads);
    ordered_cards.AddCards(high_leads);
    playable_cards.Remove(high_leads);
    ordered_cards.AddCards(leads);
    playable_cards.Remove(leads);
    if (SUIT_CONTRACT) {
      ordered_cards.AddCards(bad_leads);
      playable_cards.Remove(bad_leads);
      ordered_cards.AddCards(trump_leads);
      playable_cards.Remove(trump_leads);
    }
    ordered_cards.AddCards(playable_cards);
  }

  void OrderCards(Cards playable_cards) {
    if (!playable_cards) return;
    if (playable_cards.Size() == 1) {
      ordered_cards.AddCard(playable_cards.Top());
      return;
    }
    if (TrickStarting()) {  // lead
      return trump == NOTRUMP ? Lead<false>(playable_cards) : Lead<true>(playable_cards);
    }
    int winning_seat = PreviousPlay().WinningSeat();
    int winning_card = PreviousPlay().WinningCard();
    Cards pd_suit = hands[Partner()].Suit(LeadSuit());
    Cards lho_suit = hands[LeftHandOpp()].Suit(LeadSuit());
    if (playable_cards.Suit(LeadSuit())) {  // follow
      if (!WinOver(playable_cards.Top(), winning_card))
        return ordered_cards.AddReversedCards(playable_cards);
      if (winning_seat == Partner() &&
          (TrickEnding() || !lho_suit || HigherRank(winning_card, lho_suit.Top()) ||
           lho_suit.Slice(0, winning_card) == lho_suit.Slice(0, playable_cards.Top())))
        // Partner can win or force LHO's winner.
        return ordered_cards.AddReversedCards(playable_cards);
      if (SecondSeat() && pd_suit && HigherRank(pd_suit.Top(), winning_card)) {
        if (lho_suit && HigherRank(lho_suit.Top(), pd_suit.Union(playable_cards).Top()) &&
            lho_suit.Slice(0, pd_suit.Top()) == lho_suit.Slice(0, playable_cards.Top()))
          // Play low as LHO may play a winner to prevent partner from winning.
          return ordered_cards.AddReversedCards(playable_cards);
        if (!lho_suit || HigherRank(pd_suit.Top(), lho_suit.Top()))
          // Play low as partner can win later.
          return ordered_cards.AddReversedCards(playable_cards);
      }
      auto higher_cards = playable_cards.Slice(0, winning_card);
      if (TrickEnding() || !lho_suit || HigherRank(higher_cards.Bottom(), lho_suit.Top()))
        ordered_cards.AddReversedCards(higher_cards);
      else
        ordered_cards.AddCards(higher_cards);
      return ordered_cards.AddReversedCards(playable_cards.Different(higher_cards));
    }
    if (trump != NOTRUMP && playable_cards.Suit(trump)) {  // ruff
      if (winning_seat == Partner() &&
          (TrickEnding() || (lho_suit && WinOver(winning_card, lho_suit.Top())))) {
        // Partner can win.
      } else {
        Cards my_trumps = playable_cards.Suit(trump);
        if (SuitOf(winning_card) == trump) {
          if (winning_seat != Partner() && WinOver(my_trumps.Top(), winning_card)) {
            auto higher_trumps = my_trumps.Slice(my_trumps.Top(), winning_card);
            ordered_cards.AddReversedCards(higher_trumps);
            playable_cards.Remove(higher_trumps);
          }
        } else if (TrickEnding() || lho_suit || !hands[LeftHandOpp()].Suit(trump)) {
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
    for (int suit = 0; suit < NUM_SUITS; ++suit) {
      if (suit == trump) continue;
      auto my_suit = playable_cards.Suit(suit);
      if (my_suit) {
        ordered_cards.AddCard(my_suit.Bottom());
        playable_cards.Remove(my_suit.Bottom());
        ++num_discards;
      }
    }
    ordered_cards.SortDiscards(num_discards, playable_cards);
    ordered_cards.AddCards(playable_cards);
  }

  class OrderedCards {
   public:
    void Reset() { num_ordered_cards = 0; }

    void AddCard(int card) { ordered_cards[num_ordered_cards++] = card; }

    void AddCards(Cards cards) {
      for (int card : cards) AddCard(card);
    }

    void AddReversedCards(Cards cards) {
      while (cards) {
        AddCard(cards.Bottom());
        cards.Remove(cards.Bottom());
      }
    }

    void SortDiscards(int num_discards, const Cards& playable_cards) {
      auto compare = [playable_cards](int c1, int c2) {
        return playable_cards.Suit(SuitOf(c1)).Size() >
               playable_cards.Suit(SuitOf(c2)).Size();
      };
      std::sort(ordered_cards + num_ordered_cards - num_discards,
                ordered_cards + num_ordered_cards, compare);
    }

    int Size() const { return num_ordered_cards; }
    int Card(int i) const { return ordered_cards[i]; }

   private:
    short num_ordered_cards = 0;
    char ordered_cards[TOTAL_TRICKS];
  };

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

  void PlayCard(int card_to_play) {
    // remove played card from hand
    card_played = card_to_play;
    hands[seat_to_play].Remove(card_played);

    // who's winning?
    if (TrickStarting()) trick->lead_suit = SuitOf(card_played);
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
    } else if (hands[seat_to_play].Suit(LeadSuit())) {
      cutoff_index[0] = trick->all_cards.Suit(LeadSuit());
      cutoff_index[1].Add(PreviousPlay().WinningCard());
    } else {
      cutoff_index[0] = hands[seat_to_play];
      if (trump == NOTRUMP) cutoff_index[1].Add(PreviousPlay().WinningSeat());
      else cutoff_index[1].Add(PreviousPlay().WinningCard());
    }
    cutoff_index[1].Add(TOTAL_CARDS + (depth & 3));
  }

  Cards LookupCutoffCards(Cards cutoff_index[]) const {
    Cards cutoff_cards;
    if (const auto* entry = cutoff_cache.Lookup(cutoff_index))
      if (entry->card[seat_to_play] != TOTAL_CARDS)
        cutoff_cards.Add(entry->card[seat_to_play]);
    return cutoff_cards;
  }

  void SaveCutoffCard(Cards cutoff_index[], int cutoff_card) const {
    auto* entry = cutoff_cache.Update(cutoff_index);
    entry->card[seat_to_play] = cutoff_card;
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
      if (both_trumps.Have(card) && sure_tricks < max_trump_tricks) {
        ++sure_tricks;
        rank_winners.Add(card);
      } else
        break;
    return {sure_tricks, rank_winners};
  }

  Result SlowTrumpTricks(Cards my_trumps, Cards pd_trumps,
                         Cards lho_trumps, Cards rho_trumps, bool leading) const {
    auto all_trumps = trick->all_cards.Suit(trump);
    if (all_trumps.Size() >= 3) {
      auto a = Cards().Add(all_trumps.Top());
      auto k = Cards().Add(all_trumps.Different(a).Top());
      // Kx behind A
      if ((pd_trumps.StrictlyInclude(k) && lho_trumps.Include(a)) ||
          (my_trumps.StrictlyInclude(k) && rho_trumps.Include(a) &&
           (!leading || hands.num_tricks() >= 3)))
        return {1, a.Union(k)};
      // KQ against A
      auto q = Cards().Add(all_trumps.Different(a.Union(k)).Top());
      if (lho_trumps.Union(rho_trumps).Have(a) &&
          my_trumps.Union(pd_trumps).Have(k.Union(q)) &&
          (my_trumps.Size() >= 1 || pd_trumps.Size() >= 1))
        return {1, a.Union(k).Union(q)};
      // Qxx behind AK
      if (all_trumps.Size() >= 5)
        if ((pd_trumps.Include(q) && pd_trumps.Size() >= 3 &&
             lho_trumps.Include(a.Union(k))) ||
            (my_trumps.Include(q) && my_trumps.Size() >= 3 &&
             rho_trumps.Include(a.Union(k)) && (!leading || hands.num_tricks() >= 4)))
          return {1, a.Union(k).Union(q)};
    }
    return {0, {}};
  }

  Result SlowNoTrumpTricks(Cards my_hand, Cards pd_hand) const {
    auto both_hands = my_hand.Union(pd_hand);
    Cards rank_winners;
    for (int suit = 0; suit < NUM_SUITS; ++suit) {
      if (!my_hand.Suit(suit)) continue;
      auto top = trick->all_cards.Suit(suit).Top();
      if (both_hands.Have(top)) return {0, {}};
      rank_winners.Add(top);
    }
    if (hands[LeftHandOpp()].Include(rank_winners) ||
        hands[RightHandOpp()].Include(rank_winners)) {
      return {rank_winners.Size(), rank_winners};
    } else
      return {1, rank_winners};
  }

  Result FastTricks() const {
    Cards my_hand = hands[seat_to_play], pd_hand = hands[Partner()];
    Cards lho_hand = hands[LeftHandOpp()], rho_hand = hands[RightHandOpp()];
    Cards pd_rank_winners;
    auto [trump_tricks, rank_winners] = trump == NOTRUMP ?
        Result{0, {}} : TopTrumpTricks(my_hand.Suit(trump), pd_hand.Suit(trump));
    int fast_tricks = 0, my_tricks = 0, pd_tricks = 0;
    bool my_entry = false, pd_entry = false;
    for (int suit = 0; suit < NUM_SUITS; ++suit) {
      if (suit == trump) continue;
      auto my_suit = my_hand.Suit(suit);
      auto pd_suit = pd_hand.Suit(suit);
      auto lho_suit = lho_hand.Suit(suit);
      auto rho_suit = rho_hand.Suit(suit);
      int my_max_rank_winners =
          std::max({pd_suit.Size(), lho_suit.Size(), rho_suit.Size()});
      int pd_max_rank_winners =
          std::max({my_suit.Size(), lho_suit.Size(), rho_suit.Size()});

      auto max_suit_winners = TOTAL_TRICKS;
      if (trump != NOTRUMP) {
        if (lho_hand.Suit(trump)) max_suit_winners = lho_hand.Suit(suit).Size();
        if (rho_hand.Suit(trump))
          max_suit_winners = std::min(max_suit_winners, rho_hand.Suit(suit).Size());
        while (my_suit.Size() > max_suit_winners) my_suit.Remove(my_suit.Bottom());
        while (pd_suit.Size() > max_suit_winners)
          pd_suit.Remove(pd_suit.Bottom());
      }

      int my_winners = 0, pd_winners = 0;
      for (int card : trick->all_cards.Suit(suit))
        if (my_suit.Have(card)) {
          ++my_winners;
          if (my_winners <= my_max_rank_winners) rank_winners.Add(card);
        } else if (pd_suit.Have(card)) {
          ++pd_winners;
          if (pd_winners <= pd_max_rank_winners) pd_rank_winners.Add(card);
        } else
          break;
      my_tricks +=
          SuitFastTricks(my_suit, my_winners, my_entry, pd_suit, pd_winners);
      pd_tricks += SuitFastTricks(pd_suit, pd_winners, pd_entry,
                                       my_suit, my_winners);
    }
    if (pd_entry) {
      fast_tricks = std::max(my_tricks, pd_tricks);
      rank_winners.Add(pd_rank_winners);
    } else
      fast_tricks = my_tricks;
    return {std::min(trump_tricks + fast_tricks, my_hand.Size()), rank_winners};
  }

  int SuitFastTricks(Cards my_suit, int my_winners, bool& my_entry, Cards pd_suit,
                     int pd_winners) const {
    // Entry from partner if my top winner can cover partner's bottom card.
    if (pd_suit && my_winners > 0 && HigherRank(my_suit.Top(), pd_suit.Bottom()))
      my_entry = true;
    // Partner has no winners.
    if (pd_winners == 0) return my_winners;
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
    for (int d = depth - 3; d <= depth; ++d) {
      if (plays[d].card_played == winning_card) continue;
      if (SuitOf(winning_card) == SuitOf(plays[d].card_played))
        return Cards().Add(winning_card);
    }
    return Cards();
  }

  Result CollectLastTrick() const {
    int winning_card = hands[seat_to_play].Top();
    int winning_seat = seat_to_play;
    for (int seat = (seat_to_play + 1) % NUM_SEATS; seat != seat_to_play;
         seat = (seat + 1) % NUM_SEATS) {
      int card_to_play = hands[seat].Top();
      if (WinOver(card_to_play, winning_card)) {
        winning_card = card_to_play;
        winning_seat = seat;
      }
    }
    Cards rank_winners;
    if (hands.all_cards().Remove(winning_card).Suit(SuitOf(winning_card)))
      rank_winners.Add(winning_card);
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

  bool TrickStarting() const { return (depth & 3) == 0; }
  bool SecondSeat() const { return (depth & 3) == 1; }
  bool ThirdSeat() const { return (depth & 3) == 2; }
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
  const Play& PreviousPlay() const { return *(this - 1); }
  Play& NextPlay() { return *(this + 1); }

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

  friend class InteractivePlay;
#ifdef _WEB
  friend class WebPlay;
#endif  // _WEB
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

  int Search(int beta) { return plays[0].SearchWithCache(beta).first; }

  Play& play(int i) { return plays[i]; }

 private:
  Hands hands;
  Play plays[TOTAL_CARDS];
  Trick tricks[TOTAL_TRICKS];
};

Cards ParseHand(char* line, Cards all_cards) {
  // Filter out invalid characters.
  int pos = 0;
  for (char* c = line; *c; ++c)
    if (strchr("AaKkQqJjTt1098765432Xx- ", *c)) line[pos++] = *c;
  line[pos] = '\0';

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
      if (all_cards.Have(card)) {
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

void ReadHands(Hands& hands, std::vector<int>& trumps, std::vector<int>& lead_seats) {
  auto input_file = fopen(options.input_file, "rt");
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
      fprintf(stderr, "%s has %d cards, while %s has %d.\n",
              SeatName(seat), hands[seat].Size(), SeatName(0), num_tricks);
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

int MemoryEnhancedTestDriver(std::function<int(int)> search, int num_tricks,
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
    if (ns_points < ew_points &&
        (std::max(n_trumps, s_trumps) < std::max(e_trumps, w_trumps) ||
         (std::max(n_trumps, s_trumps) == std::max(e_trumps, w_trumps) &&
          n_trumps + s_trumps < e_trumps + w_trumps)))
      return 0;
  }
  return hands.num_tricks();
}

void Solve(const Hands& hands, const std::vector<int>& trumps,
           const std::vector<int>& lead_seats, std::function<void(int trump)> trump_start,
           std::function<void(int trump, int lead_seat, int ns_tricks)> seat_done,
           std::function<void(int trump)> trump_done) {
  int num_tricks = hands[WEST].Size();
  for (int trump : trumps) {
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
      }
      seat_done(trump, lead_seat, ns_tricks);
    }
    common_bounds_cache.Reset();
    cutoff_cache.Reset();
    trump_done(trump);
  }
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
      snprintf(contract, sizeof(contract), "%d%s by %s", level, SuitSign(trump), declarer);
    } else {
      starting_ew_tricks = TOTAL_TRICKS - num_tricks;
      ns_contract = false;
      int level = TOTAL_TRICKS - target_ns_tricks - 6;
      auto declarer = starting_ew_tricks == 0 ? SeatName((lead_seat + 3) % 4) : "EW";
      snprintf(contract, sizeof(contract), "%d%s by %s", level, SuitSign(trump), declarer);
    }
  }

  bool SetupTrick(Play& play) {
    // TODO: Clean up! SearchWithCache() recomputes the same info.
    if (play.depth > 0) {
      play.ns_tricks_won =
          play.PreviousPlay().ns_tricks_won + play.PreviousPlay().NsWon();
      play.seat_to_play = play.PreviousPlay().WinningSeat();
    }

    play.trick->all_cards = play.hands.all_cards();
    play.ComputeShape();
    play.trick->ComputeRelativeHands(play.depth, play.hands);

    int trick_index = play.depth / 4;
    printf("------ %s: NS %d EW %d ------\n", contract,
           starting_ns_tricks + play.ns_tricks_won,
           starting_ew_tricks + trick_index - play.ns_tricks_won);
    play.hands.ShowDetailed(rotation);
    if (trick_index == num_tricks - 1) {
      int ns_tricks_won = play.CollectLastTrick().first;
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

      auto search = [&play, card](int beta) {
        play.PlayCard(card);
        auto [ns_tricks, _] = play.NextPlay().SearchWithCache(beta);
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
    if (IsNs(play.seat_to_play)) {
      int max_ns_tricks = -1;
      for (const auto& pair : card_tricks) {
        if (pair.second > max_ns_tricks || (pair.second == max_ns_tricks &&
                                            LowerRank(pair.first, *card_to_play))) {
          *card_to_play = pair.first;
          max_ns_tricks = pair.second;
        }
      }
    } else {
      int min_ns_tricks = TOTAL_TRICKS + 1;
      for (const auto& pair : card_tricks) {
        if (pair.second < min_ns_tricks || (pair.second == min_ns_tricks &&
                                            LowerRank(pair.first, *card_to_play))) {
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
    snprintf(name, sizeof(name), "%s %c", SuitSign(SuitOf(card)), NameOf(card)[1]);
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

#ifdef _WEB
class WebPlay {
 public:
  WebPlay(const Hands& hands, int trump, int lead_seat, int target_ns_tricks)
      : min_max(hands, trump, lead_seat),
        target_ns_tricks(target_ns_tricks),
        num_tricks(hands.num_tricks()),
        trump(trump) {}

  typedef std::map<int, int> CardTricks;

  CardTricks EvaluateLeads(int ns_tricks, bool ns_contract) {
    auto& play = min_max.play(0);
    play.trick->all_cards = play.hands.all_cards();
    play.ComputeShape();
    play.trick->ComputeRelativeHands(play.depth, play.hands);

    CardTricks card_tricks;
    for (int card : play.trick->FilterEquivalent(play.GetPlayableCards())) {
      auto search = [&play, card](int beta) {
        play.PlayCard(card);
        auto [ns_tricks, _] = play.NextPlay().SearchWithCache(beta);
        play.UnplayCard();
        return ns_tricks;
      };
      int new_ns_tricks = MemoryEnhancedTestDriver(search, num_tricks, ns_tricks);
      int trick_diff = ns_contract ? new_ns_tricks - target_ns_tricks
        : target_ns_tricks - new_ns_tricks;
      card_tricks[card] = trick_diff;
    }
    return card_tricks;
  }

 private:
  MinMax min_max;
  const int target_ns_tricks;
  const int num_tricks;
  const int trump;
};

Hands CollectHands(char* west, char* north, char* east, char* south) {
  Cards all_cards;
  Hands hands;
  hands[WEST]  = ParseHand(west,  all_cards); all_cards.Add(hands[WEST]);
  hands[NORTH] = ParseHand(north, all_cards); all_cards.Add(hands[NORTH]);
  hands[EAST]  = ParseHand(east,  all_cards); all_cards.Add(hands[EAST]);
  hands[SOUTH] = ParseHand(south, all_cards); all_cards.Add(hands[SOUTH]);
  return hands;
}

extern "C" {
  char *solve(char* west, char* north, char* east, char* south) {
    auto hands = CollectHands(west, north, east, south);

    static char buffer[256];
    buffer[0] = '\0';
    auto start_time = Now();
    auto trump_start = [&](int trump) {
      sprintf(buffer + strlen(buffer), "%c", SuitName(trump)[0]);
    };
    auto seat_done = [&](int trump, int lead_seat, int ns_tricks) {
      sprintf(buffer + strlen(buffer), " %2d",
              IsNs(lead_seat) ? hands.num_tricks() - ns_tricks : ns_tricks);
    };
    auto trump_done = [start_time](int trump) {
      sprintf(buffer + strlen(buffer), " %5.2f s\n", Now() - start_time);
    };
    std::vector<int> trumps = {NOTRUMP, SPADE, HEART, DIAMOND, CLUB};
    std::vector<int> lead_seats = {WEST, EAST, NORTH, SOUTH};
    Solve(hands, trumps, lead_seats, trump_start, seat_done, trump_done);
    return buffer;
  }

  char *solve_leads(char* west, char* north, char* east, char* south,
                    int level, int trump, int lead_seat) {
    auto hands = CollectHands(west, north, east, south);
    bool ns_contract = !IsNs(lead_seat);
    int target_ns_tricks = ns_contract ? level + 6 : 7 - level;

    static char buffer[256];
    buffer[0] = '\0';
    auto do_nothing = [](int trump) {};
    auto seat_done = [&](int trump, int lead_seat, int ns_tricks) {
      auto web_play = WebPlay(hands, trump, lead_seat, target_ns_tricks);
      auto card_tricks = web_play.EvaluateLeads(ns_tricks, ns_contract);
      for (const auto& card_trick : card_tricks) {
        sprintf(buffer + strlen(buffer), "%s:%+d ", NameOf(card_trick.first),
                card_trick.second);
      }
    };
    std::vector<int> trumps = {trump};
    std::vector<int> lead_seats = {lead_seat};
    Solve(hands, trumps, lead_seats, do_nothing, seat_done, do_nothing);
    return buffer;
  }
}
#else  // _WEB
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
      fflush(stdout);
    };
    auto trump_done = [start_time](int trump) {
      struct rusage usage;
      getrusage(RUSAGE_SELF, &usage);
      printf(" %5.2f s %5.1f M\n", Now() - start_time, usage.ru_maxrss / 1024.0);
      fflush(stdout);
    };
    Solve(hands, trumps, lead_seats, trump_start, seat_done, trump_done);
  }
  return 0;
}
#endif  // _WEB
