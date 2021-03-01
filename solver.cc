#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

#ifdef _DEBUG
#define CHECK(statement)  assert(statement)
#define VERBOSE(statement)  if (depth <= options.displaying_depth) statement
#else
#define CHECK(statement)  if (!(statement))
#define VERBOSE(statement)
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

const char* SuitName(int suit) {
  static const char* suit_names[NUM_SUITS + 1] = { "Spade", "Heart", "Diamond", "Club", "No" };
  return suit_names[suit];
}

const char* SuitSign(int suit) {
#ifdef _DEBUG
  static const char* suit_signs[NUM_SUITS + 1] = { "♠", "♥", "♦", "♣", "NT" };
#else
  static const char* suit_signs[NUM_SUITS + 1] = { "♠", "\e[31m♥\e[0m", "\e[31m♦\e[0m", "♣", "NT" };
#endif
  return suit_signs[suit];
}

const char RankName(int rank) {
  static const char rank_names[] = "23456789TJQKA";
  return rank_names[rank];
}

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
  CardInitializer() {
    memset(mask_of, 0, sizeof(mask_of));
    for (int card = 0; card < TOTAL_CARDS; ++card) {
      suit_of[card] = card / NUM_RANKS;
      rank_of[card] = NUM_RANKS - 1 - card % NUM_RANKS;
      card_of[SuitOf(card)][RankOf(card)] = card;
      mask_of[SuitOf(card)] |= uint64_t(1) << card;
      name_of[card][0] = SuitName(SuitOf(card))[0];
      name_of[card][1] = RankName(RankOf(card));
      name_of[card][2] = '\0';
    }
  }
};

struct Options {
  char* input = nullptr;
  int  guess = TOTAL_TRICKS;
  int  small_card = TWO;
  int  displaying_depth = -1;
  bool discard_suit_bottom = false;
  bool randomize = false;
  bool show_stats = false;
  bool full_analysis = false;
  bool interactive = false;
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
    bool operator !=(const Cards& c) const { return bits != c.bits; }

    Cards Slice(int begin, int end) const { return bits & (Bit(end) - Bit(begin)); }
    Cards Suit(int suit) const { return bits & MaskOf(suit); }
    int Top() const { return __builtin_ctzll(bits); }
    int Bottom() const { return BitSize(bits) - 1 - __builtin_clzll(bits); }
    int RightAbove(int card) const { return Slice(Top(), card).Bottom(); }

    Cards Union(const Cards& c) const { return bits | c.bits; }
    Cards Intersect(const Cards& c) const { return bits & c.bits; }
    Cards Different(const Cards& c) const { return bits & ~c.bits; }
    bool Include(const Cards& c) const { return Intersect(c) == c; }

    Cards Add(int card) { bits |= Bit(card); return bits; }
    Cards Remove(int card) { bits &= ~Bit(card); return bits; }

    Cards Add(const Cards& c) { bits |= c.bits; return bits; }
    Cards Remove(const Cards& c) { bits &= ~c.bits; return bits; }

    int CountPoints() const {
      int points = 0;
      for (int card : *this)
        if (RankOf(card) > TEN)
          points += RankOf(card) - TEN;
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
        for (int card : suit_cards)
          putchar(NameOf(card)[1]);
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

uint64_t GetShape(Cards hands[]) {
  uint64_t shape = 0;
  for (int seat = 0; seat < NUM_SEATS; ++seat)
    for (int suit = 0; suit < NUM_SUITS; ++suit)
      shape = (shape << 4) + hands[seat].Suit(suit).Size();
  return shape;
}

int GetSuitLength(uint64_t shape, int seat, int suit) {
  int bits = 60 - (seat * NUM_SUITS + suit) * 4;
  return (shape >> bits) & 0xf;
}

void ShowHands(Cards hands[]) {
  for (int seat = 0; seat < NUM_SEATS; ++seat) {
    hands[seat].Show();
    if (seat < NUM_SEATS - 1) printf(", ");
  }
  puts("");
}

template <class Entry, int input_size>
class Cache {
  public:
    Cache(const char* name, int bits)
      : cache_name(name), bits(bits), size(1 << bits), entries(new Entry[size]) {
      srand(1);
      for (int i = 0; i < input_size; ++i)
        hash_rand[i] = GenerateHashRandom();
      Reset();
    }

    void Reset() {
      probe_distance = 0;
      load_count = lookup_count = hit_count = update_count = collision_count = 0;
      for (int i = 0; i < size; ++i)
        entries[i].hash = 0;
    }

    void ShowStatistics() const {
      printf("--- %s Statistics ---\n", cache_name);
      printf("lookups: %8d   hits:     %8d (%5.2f%%)   probe distance: %d\n",
             lookup_count, hit_count, hit_count * 100.0 / lookup_count, probe_distance);
      printf("updates: %8d   collisions: %6d (%5.2f%%)\n",
             update_count, collision_count, collision_count * 100.0 / update_count);
      printf("entries: %8d   loaded:   %8d (%5.2f%%)\n",
             size, load_count, load_count * 100.0 / size);

      int recursive_load = 0;
      for (int i = 0; i < size; ++i)
        if (entries[i].hash != 0) {
          recursive_load += entries[i].Size();
          entries[i].Show();
        }
      if (recursive_load > load_count)
        printf("recursive load: %8d\n", recursive_load);
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
        sum += (cards[i * 2].Value() + hash_rand[i * 2])
          * (cards[i * 2 + 1].Value() + hash_rand[i * 2 + 1]);
      return sum;
    }

    uint64_t GenerateHashRandom() {
      static const int BITS_PER_RAND = 32;
      uint64_t r = 0;
      for (int i = 0; i < BitSize(r) / BITS_PER_RAND; ++i)
        r = (r << BITS_PER_RAND) + rand();
      return r | 1;
    }

    void Resize() {
      auto old_entries = std::move(entries);
      int old_size = size;

      // Double cache size.
      size = 1 << ++bits;
      entries.reset(new Entry[size]);
      CHECK(entries.get());
      for (int i = 0; i < size; ++i)
        entries[i].hash = 0;

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

    static const int max_probe_distance = 16;

    const char* cache_name;
    int bits;
    int size;
    int probe_distance;
    uint64_t hash_rand[input_size];
    std::unique_ptr<Entry[]> entries;

    mutable int load_count;
    mutable int lookup_count;
    mutable int hit_count;
    mutable int update_count;
    mutable int collision_count;
};

#pragma pack(push, 4)
struct Bounds {
  uint8_t lower : 4;
  uint8_t upper : 4;

  bool Empty() const { return upper < lower; }
  int Width() const { return upper - lower; }
  Bounds Intersect(Bounds b) const {
    return {std::max(lower, b.lower), std::min(upper, b.upper)};
  }
  bool Include(Bounds b) const { return Intersect(b) == b; }
  bool operator==(Bounds b) const { return b.lower == lower && b.upper == upper; }
};

struct Pattern {
  Cards hands[NUM_SEATS];
  uint64_t shape;
  char seat_to_play;
  Bounds bounds;
  mutable short cuts = 0;
  mutable int hits = 0;
  std::vector<Pattern> patterns;

  Pattern() = default;

  Pattern(Cards pattern_hands[]) {
    for (int seat = 0; seat < NUM_SEATS; ++seat)
      hands[seat] = pattern_hands[seat];
  }

  Pattern(Cards pattern_hands[], uint64_t shape, int seat_to_play, Bounds bounds)
    : shape(shape), seat_to_play(seat_to_play), bounds(bounds) {
      for (int seat = 0; seat < NUM_SEATS; ++seat)
        hands[seat] = pattern_hands[seat];
    }

  void Reset() {
    for (int seat = 0; seat < NUM_SEATS; ++seat) hands[seat] = 0;
    shape = 0;
    seat_to_play = -1;
    bounds.lower = 0;
    bounds.upper = TOTAL_TRICKS;
    hits = 0;
    cuts = 0;
    patterns.clear();
  }

  void MoveTo(Pattern& p) { p.MoveFrom(*this); }

  void MoveFrom(Pattern& p) {
    for (int seat = 0; seat < NUM_SEATS; ++seat) hands[seat] = p.hands[seat];
    shape = p.shape;
    seat_to_play = p.seat_to_play;
    bounds = p.bounds;
    hits = p.hits;
    cuts = p.cuts;
    std::swap(patterns, p.patterns);
  }

  bool IsRoot() const { return seat_to_play == -1; }

  const Pattern* Lookup(const Pattern& new_pattern, int alpha, int beta) const {
    if (!Match(new_pattern)) return nullptr;
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
      if (new_pattern.Same(pattern)) {
        pattern.UpdateBounds(new_pattern.bounds);
        return &pattern;
      } else if (new_pattern.Match(pattern)) {
        // Old pattern is more detailed, absorb sub-patterns.
        for (; i < patterns.size(); ++i) {
          auto& old_pattern = patterns[i];
          if (!new_pattern.Match(old_pattern)) continue;
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
      } else if (pattern.Match(new_pattern)) {
        // New pattern is more detailed.
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

    for (auto& pattern : patterns)
      pattern.UpdateBounds(bounds);
  }

  bool Match(const Pattern& d) const {
    return d.hands[WEST].Include(hands[WEST]) && d.hands[NORTH].Include(hands[NORTH]) &&
      d.hands[EAST].Include(hands[EAST]) && d.hands[SOUTH].Include(hands[SOUTH]);
  }

  bool Same(const Pattern& p) const {
    return p.hands[WEST] == hands[WEST] && p.hands[NORTH] == hands[NORTH] &&
      p.hands[EAST] == hands[EAST] && p.hands[SOUTH] == hands[SOUTH];
  }

  Cards GetWinners(Cards real_hands[]) const {
    Cards winners;
    for (int seat = 0; seat < NUM_SEATS; ++seat) {
      for (int suit = 0; suit < NUM_SUITS; ++suit) {
        int num_winners = hands[seat].Suit(suit).Size();
        if (num_winners == 0) continue;

        auto suit_cards = real_hands[seat].Suit(suit);
        for (int i = 0; i < num_winners; ++i) {
          winners.Add(suit_cards.Top());
          suit_cards.Remove(suit_cards.Top());
        }
      }
    }
    return winners;
  }

  int Size() const {
    int sum = IsRoot() ? 0 : 1;
    for (const auto& pattern : patterns) sum += pattern.Size();
    return sum;
  }

  void Show(int level = 0) const {
    if (!IsRoot()) {

      printf("%*d: %c (%d %d) ", level * 2, level, SeatLetter(seat_to_play),
             bounds.lower, bounds.upper);
      for (int seat = 0; seat < NUM_SEATS; ++seat) {
        for (int suit = 0; suit < NUM_SUITS; ++suit) {
          auto suit_length = GetSuitLength(shape, seat, suit);
          if (suit_length == 0)
            putchar('-');
          else {
            auto winners = hands[seat].Suit(suit);
            for (int card : winners) printf("%c", RankName(RankOf(card)));
            for (int i = winners.Size(); i < suit_length; ++i) putchar('x');
          }
          putchar(' ');
        }
        if (seat < NUM_SEATS - 1) printf(", ");
      }
      printf(" hits %d cuts %d\n", hits, cuts);
    }
    for (const auto& pattern : patterns) pattern.Show(level + 1);
  }
};

struct ShapeEntry {
  uint64_t hash;
  Pattern pattern;

  int Size() const { return pattern.Size(); }

  void Show() const {
    printf("%lx size %ld recursive size %d hits %d\n",
           hash, pattern.patterns.size(), Size(), pattern.hits);
    pattern.Show();
  }

  void Reset(uint64_t hash_in) {
    hash = hash_in;
    pattern.Reset();
  }

  void MoveTo(ShapeEntry& to) {
    to.hash = hash;
    to.pattern.MoveFrom(pattern);
  }
};

struct BoundsEntry {
  // Save only the hash instead of full hands to save memory.
  // The chance of getting a hash collision is practically zero.
  uint64_t hash;
  // Bounds depending on the lead seat.
  Bounds bounds[NUM_SEATS];

  int Size() const { return 1; }

  void Show() const {}

  void Reset(uint64_t hash_in) {
    hash = hash_in;
    for (int i = 0; i < NUM_SEATS; ++i) {
      bounds[i].lower = 0;
      bounds[i].upper = TOTAL_TRICKS;
    }
  }

  void MoveTo(BoundsEntry& to) { memcpy(&to, this, sizeof(*this)); }
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
  Cards pattern_hands[NUM_SEATS];
  char relative_cards[TOTAL_CARDS];
  char equivalence[TOTAL_CARDS];

  void IdentifyPatternCards(Cards hands[NUM_SEATS], int suit) {
    // Create the pattern using relative ranks. For example, when all the cards
    // remaining in a suit are J, 8, 7 and 2, they are treated as A, K, Q and J
    // respectively.
    Cards all_cards;
    int card_holder[TOTAL_CARDS];
    for (int seat = 0; seat < NUM_SEATS; ++seat) {
      Cards suit_cards = hands[seat].Suit(suit);
      all_cards.Add(suit_cards);
      for (int card : suit_cards)
        card_holder[card] = seat;
      pattern_hands[seat].Remove(pattern_hands[seat].Suit(suit));
    }
    int relative_rank = ACE;
    for (int card : all_cards) {
      int relative_card = CardOf(suit, relative_rank--);
      relative_cards[card] = relative_card;
      pattern_hands[card_holder[card]].Add(relative_card);
    }
  }

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
};

class Stats {
  public:
    Stats(bool e) : enabled(e) { Clear(); }
    void Clear() { if (enabled) memset(cutoff_at, 0, sizeof(cutoff_at)); }
    void CutoffAt(int depth, int card_count) { if (enabled) ++cutoff_at[depth][card_count]; }
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
        for (int i = 0; i <= last; ++i)
          printf("%d ", cutoff_at[d][i]);
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
    Play(Play* plays, Trick* trick, Cards* hands, int trump, int depth, int seat_to_play)
      : plays(plays), trick(trick), hands(hands), trump(trump), depth(depth),
        ns_tricks_won(0), seat_to_play(seat_to_play) {}

    int SearchWithCache(int alpha, int beta, Cards* winners) {
      if (TrickStarting()) {
        all_cards = hands[WEST].Union(hands[NORTH]).Union(hands[EAST]).Union(hands[SOUTH]);
        if (depth > 0) {
          ns_tricks_won = PreviousPlay().ns_tricks_won + PreviousPlay().NsWon();
          seat_to_play = PreviousPlay().WinningSeat();
        }

        if (NsToPlay() && ns_tricks_won >= beta)
          return ns_tricks_won;
        int remaining_tricks = hands[0].Size();
        if (!NsToPlay() && ns_tricks_won + remaining_tricks <= alpha)
          return ns_tricks_won + remaining_tricks;
      } else {
        all_cards = PreviousPlay().all_cards;
        ns_tricks_won = PreviousPlay().ns_tricks_won;
        seat_to_play = PreviousPlay().NextSeat();
      }

      if (!TrickStarting() || all_cards.Size() == 4)
        return Search(alpha, beta, winners);

      ComputePatternHands();

      const Pattern* cached_pattern = nullptr;
      Cards shape_index[2] = {GetShape(trick->pattern_hands), seat_to_play};
      auto* shape_entry = common_bounds_cache.Lookup(shape_index);
      if (shape_entry) {
        cached_pattern = shape_entry->pattern.Lookup(trick->pattern_hands,
                                                     alpha - ns_tricks_won,
                                                     beta - ns_tricks_won);
        if (cached_pattern && depth <= options.displaying_depth) {
          printf("%2d: matched, ", depth);
          cached_pattern->Show(0);
          printf(" / ");
          ShowHands(hands);
        }
      }

      int lower = 0, upper = TOTAL_TRICKS;
      if (cached_pattern) {
        lower = cached_pattern->bounds.lower + ns_tricks_won;
        upper = cached_pattern->bounds.upper + ns_tricks_won;
        if (lower >= beta) {
          VERBOSE(printf("%2d: beta cut %d\n", depth, lower));
          winners->Add(cached_pattern->GetWinners(hands));
          return lower;
        }
        if (upper <= alpha) {
          VERBOSE(printf("%2d: alpha cut %d\n", depth, upper));
          winners->Add(cached_pattern->GetWinners(hands));
          return upper;
        }
      }

      Cards branch_winners;
      int ns_tricks = Search(alpha, beta, &branch_winners);
      winners->Add(branch_winners);
      if (ns_tricks <= alpha) {
        lower = 0;
        upper = ns_tricks - ns_tricks_won;
      } else {
        lower = ns_tricks - ns_tricks_won;
        upper = hands[0].Size();
      }
      CHECK(lower <= upper);

      int min_relevant_ranks[NUM_SUITS];
      for (int suit = 0; suit < NUM_SUITS; ++suit) {
        min_relevant_ranks[suit] = ACE + 1;
        if (!branch_winners.Suit(suit).Empty())
          min_relevant_ranks[suit] = RankOf(branch_winners.Suit(suit).Bottom());
      }

      Cards pattern_hands[NUM_SEATS];
      Cards all_pattern_cards;
      for (int seat = 0; seat < NUM_SEATS; ++seat) {
        for (int card : hands[seat]) {
          // West to lead in a heart contract. South's small trump is going
          // to win if East can't get in to draw trumps.
          // (a)             ♠ xx ♥ - ♦ x ♣ -
          // ♠ Ax ♥ - ♦ x ♣ -                ♠ x ♥ A ♦ x ♣ -
          //                 ♠ x ♥ x ♦ - ♣ x
          // If West can gets in from spades, EW takes all three tricks.
          // (b)             ♠ xx ♥ - ♦ x ♣ -
          // ♠ Ax ♥ - ♦ A ♣ -                ♠ K ♥ A ♦ x ♣ -
          //                 ♠ x ♥ x ♦ - ♣ x
          // With (a) the fact that West holds two equivalent cards blocking
          // spades needs to be captured.
          if (RankOf(trick->equivalence[card]) < min_relevant_ranks[SuitOf(card)]) continue;
          pattern_hands[seat].Add(trick->relative_cards[card]);
        }
        all_pattern_cards.Add(pattern_hands[seat]);
      }

      Pattern new_pattern(pattern_hands, shape_index[0].Value(), seat_to_play,
                          {uint8_t(lower), uint8_t(upper)});
      auto* new_shape_entry = common_bounds_cache.Update(shape_index);
      cached_pattern = new_shape_entry->pattern.Update(new_pattern);
      if (depth <= options.displaying_depth) {
        printf("%2d: updated, ", depth);
        cached_pattern->Show(0);
        printf(" / ");
        ShowHands(hands);
      }
      return ns_tricks;
    }

  private:
    int Search(int alpha, int beta, Cards* winners) {
      if (all_cards.Size() == 4)
        return CollectLastTrick(winners);

      if (TrickStarting()) {
        ComputeEquivalence();
        Cards fast_winners;
        int fast_tricks = FastTricks(&fast_winners);
        if (NsToPlay() && ns_tricks_won + fast_tricks >= beta) {
          VERBOSE(printf("%2d: %c beta fast cut %d+%d\n", depth, SeatLetter(seat_to_play),
                         ns_tricks_won, fast_tricks));
          winners->Add(fast_winners);
          return ns_tricks_won + fast_tricks;
        }
        int remaining_tricks = hands[0].Size();
        if (!NsToPlay() && ns_tricks_won + (remaining_tricks - fast_tricks) <= alpha) {
          VERBOSE(printf("%2d: %c alpha fast cut %d+%d\n", depth, SeatLetter(seat_to_play),
                         ns_tricks_won, remaining_tricks - fast_tricks));
          winners->Add(fast_winners);
          return ns_tricks_won + (remaining_tricks - fast_tricks);
        }
      }

      Cards playable_cards = GetPlayableCards();
      Cards cutoff_cards = playable_cards.Intersect(LookupCutoffCards());
      if (cutoff_cards.Empty())
        cutoff_cards = HeuristicPlay(playable_cards);
      Cards sets_of_playables[2] = {
        cutoff_cards,
        playable_cards.Different(cutoff_cards)
      };
      int bounded_ns_tricks = NsToPlay() ? 0 : TOTAL_TRICKS;
      int card_count = 0;
      Cards root_winners;
      for (Cards playables : sets_of_playables)
        for (int card : playables) {
          if (trick->equivalence[card] != card) continue;
          Cards branch_winners;
          if (Cutoff(alpha, beta, card, &bounded_ns_tricks, &branch_winners)) {
            if (!cutoff_cards.Have(card))
              SaveCutoffCard(card);
            stats.CutoffAt(depth, card_count);
            VERBOSE(printf("%2d: %c search cut @%d\n", depth, SeatLetter(seat_to_play), card_count));
            winners->Add(branch_winners);
            return bounded_ns_tricks;
          }
          root_winners.Add(branch_winners);
          ++card_count;
        }
      stats.CutoffAt(depth, TOTAL_TRICKS - 1);
      winners->Add(root_winners);
      return bounded_ns_tricks;
    }

    Cards HeuristicPlay(Cards playable_cards) const {
      if (TrickStarting()) {  // lead
        if (trump != NOTRUMP) {
          Cards my_trumps = playable_cards.Suit(trump);
          Cards opp_trumps = hands[LeftHandOpp()].Union(hands[RightHandOpp()]).Suit(trump);
          if ((!my_trumps.Empty() && !opp_trumps.Empty()) || my_trumps == playable_cards)
            return Cards().Add(my_trumps.Top());
          else
            return Cards().Add(playable_cards.Different(my_trumps).Top());
        }
        return Cards();
      } else if (!playable_cards.Suit(LeadSuit()).Empty()) {  // follow
        if (!WinOver(playable_cards.Top(), PreviousPlay().WinningCard()))
          return Cards().Add(trick->equivalence[playable_cards.Bottom()]);
        return Cards();
      } else if (trump != NOTRUMP && !playable_cards.Suit(trump).Empty()) {  // ruff
        Cards my_trumps = playable_cards.Suit(trump);
        return Cards().Add(trick->equivalence[my_trumps.Bottom()]);
      } else {  // discard
        int max_length = 0, discard_suit = 0;
        for (int suit = 0; suit < NUM_SUITS; ++suit) {
          auto suit_cards = playable_cards.Suit(suit);
          if (suit_cards.Empty()) continue;
          if (max_length < suit_cards.Size()) {
            max_length = suit_cards.Size();
            discard_suit = suit;
          }
        }
        int discard = playable_cards.Suit(discard_suit).Bottom();
        return Cards().Add(trick->equivalence[discard]);
      }
    }

    void ComputePatternHands() {
      if (depth < 4) {
        for (int suit = 0; suit < NUM_SUITS; ++suit)
          trick->IdentifyPatternCards(hands, suit);
      } else {
        // Recompute the pattern for suits changed by the last trick.
        memcpy(trick->pattern_hands, (trick - 1)->pattern_hands,
               sizeof(trick->pattern_hands));
        memcpy(trick->relative_cards, (trick - 1)->relative_cards,
               sizeof(trick->relative_cards));
        for (int suit = 0; suit < NUM_SUITS; ++suit) {
          if (all_cards.Suit(suit) != plays[depth - 4].all_cards.Suit(suit))
            trick->IdentifyPatternCards(hands, suit);
        }
      }
    }

    void ComputeEquivalence() {
      if (depth < 4) {
        for (int suit = 0; suit < NUM_SUITS; ++suit)
          for (int seat = 0; seat < NUM_SEATS; ++seat)
            trick->IdentifyEquivalentCards(hands[seat].Suit(suit), all_cards.Suit(suit));
      } else {
        // Recompute the equivalence for suits changed by the last trick.
        memcpy(trick->equivalence, (trick - 1)->equivalence,
               sizeof(trick->equivalence));
        for (int suit = 0; suit < NUM_SUITS; ++suit) {
          if (all_cards.Suit(suit) != plays[depth - 4].all_cards.Suit(suit))
            for (int seat = 0; seat < NUM_SEATS; ++seat)
              trick->IdentifyEquivalentCards(hands[seat].Suit(suit), all_cards.Suit(suit));
        }
      }
    }

    Cards GetPlayableCards() const {
      const Cards& hand = hands[seat_to_play];
      if (TrickStarting())
        return hand;

      int lead_suit = SuitOf(LeadCard());
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
          playable_cards.Add(trick->equivalence[suit_cards.Bottom()]);
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
          cutoff_index[0] = all_cards.Suit(LeadSuit());
        cutoff_index[1].Add(PreviousPlay().WinningCard());
      }
    }

    Cards LookupCutoffCards() const {
      Cards cutoff_cards;
      Cards cutoff_index[2];
      BuildCutoffIndex(cutoff_index);
      if (const auto* entry = cutoff_cache.Lookup(cutoff_index))
        if (entry->card[seat_to_play] != TOTAL_CARDS)
          cutoff_cards.Add(entry->card[seat_to_play]);
      return cutoff_cards;
    }

    void SaveCutoffCard(int cutoff_card) const {
      Cards cutoff_index[2];
      BuildCutoffIndex(cutoff_index);
      if (auto* entry = cutoff_cache.Update(cutoff_index))
        entry->card[seat_to_play] = cutoff_card;
    }

    bool Cutoff(int alpha, int beta, int card_to_play, int* bounded_ns_tricks, Cards* winners) {
      PlayCard(card_to_play);
      if (TrickEnding()) {
        int winning_card = WinningCard();
        for (int d = depth / 4 * 4; d <= depth; ++d) {
          if (plays[d].card_played == winning_card) continue;
          if (SuitOf(winning_card) == SuitOf(plays[d].card_played)) {
            winners->Add(WinningCard());
            break;
          }
        }
      }
      VERBOSE(ShowTricks(alpha, beta, 0, true));
      int ns_tricks = NextPlay().SearchWithCache(alpha, beta, winners);
      VERBOSE(ShowTricks(alpha, beta, ns_tricks, false));
      UnplayCard();

      if (NsToPlay()) {
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

    int FastTricks(Cards* winners) const {
      Cards my_hand = hands[seat_to_play];
      Cards partner_hand = hands[Partner()];
      Cards both_hands = my_hand.Union(partner_hand);
      int fast_tricks = 0, my_tricks = 0, partner_tricks = 0;
      if (trump == NOTRUMP || all_cards.Suit(trump) == both_hands.Suit(trump)) {
        bool my_entry = false, partner_entry = false;
        Cards partner_winners_by_rank;
        for (int suit = 0; suit < NUM_SUITS; ++suit) {
          auto my_suit = my_hand.Suit(suit);
          auto partner_suit = partner_hand.Suit(suit);
          auto lho_suit = hands[LeftHandOpp()].Suit(suit);
          auto rho_suit = hands[RightHandOpp()].Suit(suit);
          int my_max_winners_by_rank = std::max({partner_suit.Size(),
                                                lho_suit.Size(), rho_suit.Size()});
          int partner_max_winners_by_rank = std::max({my_suit.Size(),
                                                     lho_suit.Size(), rho_suit.Size()});
          int my_winners = 0, partner_winners = 0;
          for (int card : all_cards.Suit(suit))
            if (my_hand.Have(card)) {
              ++my_winners;
              if (my_winners <= my_max_winners_by_rank)
                winners->Add(card);
            } else if (partner_hand.Have(card)) {
              ++partner_winners;
              if (partner_winners <= partner_max_winners_by_rank)
                partner_winners_by_rank.Add(card);
            } else
              break;
          my_tricks += SuitFastTricks(my_suit, my_winners, my_entry,
                                      partner_suit, partner_winners);
          partner_tricks += SuitFastTricks(partner_suit, partner_winners, partner_entry,
                                           my_suit, my_winners);
        }
        if (partner_entry) {
          fast_tricks = std::max(my_tricks, partner_tricks);
          winners->Add(partner_winners_by_rank);
        } else
          fast_tricks = my_tricks;
        fast_tricks = std::min(fast_tricks, my_hand.Size());
      } else {
        auto my_suit = my_hand.Suit(trump);
        if (my_suit == all_cards.Suit(trump)) return my_suit.Size();

        auto partner_suit = partner_hand.Suit(trump);
        if (partner_suit == all_cards.Suit(trump)) return partner_suit.Size();

        auto max_trump_tricks = std::max(my_suit.Size(), partner_suit.Size());
        for (int card : all_cards.Suit(trump))
          if (both_hands.Have(card)) {
            ++fast_tricks;
            if (fast_tricks <= max_trump_tricks)
              winners->Add(card);
          } else
            break;
        fast_tricks = std::min(fast_tricks, max_trump_tricks);
      }
      return fast_tricks;
    }

    int SuitFastTricks(Cards my_suit, int my_winners, bool& my_entry,
                       Cards partner_suit, int partner_winners) const {
      // Entry from partner if my top winner can cover partner's bottom card.
      if (!partner_suit.Empty() && my_winners > 0 &&
          RankOf(my_suit.Top()) > RankOf(partner_suit.Bottom()))
          my_entry = true;
      // Partner has no winners.
      if (partner_winners == 0)
        return my_winners;
      // Cash all my winners, then partner's.
      if (my_winners == 0)
        return my_suit.Empty() ? 0 : partner_winners;
      // Suit blocked by partner.
      if (RankOf(my_suit.Top()) < RankOf(partner_suit.Bottom()))
        return partner_winners;
      // Suit blocked by me.
      if (RankOf(my_suit.Bottom()) > RankOf(partner_suit.Top()))
        return my_winners;
      // If partner has no small cards, treat one winner as a small card.
      if (partner_winners == partner_suit.Size())
        --partner_winners;
      return std::min(my_suit.Size(), my_winners + partner_winners);
    }

    int CollectLastTrick(Cards* winners) {
      int winning_card = *hands[seat_to_play].begin();
      int winning_seat = seat_to_play;
      for (int i = 1; i < NUM_SEATS; ++i) {
        seat_to_play = NextSeat();
        int card_to_play = *hands[seat_to_play].begin();
        if (WinOver(card_to_play, winning_card)) {
          winning_card = card_to_play;
          winning_seat = seat_to_play;
        }
      }
      for (int seat = 0; seat < NUM_SEATS; ++seat) {
        if (seat == winning_seat) continue;
        if (SuitOf(winning_card) == SuitOf(*hands[seat].begin())) {
          winners->Add(winning_card);
          break;
        }
      }
      return ns_tricks_won + IsNs(winning_seat);
    }

    void ShowTricks(int alpha, int beta, int ns_tricks, bool starting) const {
      printf("%2d:", depth);
      for (int i = 0; i <= depth; ++i) {
        if ((i & 3) == 0)
          printf(" %c", SeatLetter(plays[i].seat_to_play));
        printf(" %s", NameOf(plays[i].card_played));
      }
      if (starting)
        printf(" (%d %d)\n", alpha, beta);
      else
        printf(" (%d %d) -> %d\n", alpha, beta, ns_tricks);
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
    int NextSeat(int count = 1) const { return (seat_to_play + count) & (NUM_SEATS -1 ); }
    int LeftHandOpp() const { return NextSeat(1); }
    int Partner() const { return NextSeat(2); }
    int RightHandOpp() const { return NextSeat(3); }
    Play& PreviousPlay() const { return plays[depth - 1]; }
    Play& NextPlay() const { return plays[depth + 1]; }

    // Fixed info.
    Play* plays;
    Trick* trick;
    Cards* hands;
    int trump;
    int depth;

    // Per play info.
    //int alpha;
    //int beta;
    int ns_tricks_won;
    int seat_to_play;
    int card_played;
    int winning_play;
    Cards all_cards;

    friend class InteractivePlay;
};

class MinMax {
  public:
    MinMax(Cards h[NUM_SEATS], int trump, int seat_to_play) {
      for (int seat = 0; seat < NUM_SEATS; ++seat)
        hands[seat] = h[seat];

      for (int i = 0; i < TOTAL_CARDS; ++i)
        new(&plays[i]) Play(plays, tricks + i / 4, hands, trump, i, seat_to_play);
      new (&stats) Stats(options.show_stats);
    }

    ~MinMax() {
      stats.Show();
    }

    int Search(int alpha, int beta) {
      Cards winners;
      return plays[0].SearchWithCache(alpha, beta, &winners);
    }

    Play& play(int i) { return plays[i]; }

  private:
    Cards  hands[NUM_SEATS];
    Play   plays[TOTAL_CARDS];
    Trick  tricks[TOTAL_TRICKS];
};

namespace {

int CharToSuit(char c) {
  for (int suit = SPADE; suit <= NOTRUMP; ++suit)
    if (toupper(c) == SuitName(suit)[0])
      return suit;
  printf("Unknown suit: %c\n", c);
  exit(-1);
}

int CharToRank(char c) {
  if (c == '1') return TEN;
  for (int rank = TWO; rank <= ACE; ++rank)
    if (toupper(c) == RankName(rank))
      return rank;
  printf("Unknown rank: %c\n", c);
  exit(-1);
}

int CharToSeat(char c) {
  for (int seat = WEST; seat <= SOUTH; ++seat)
    if (toupper(c) == SeatLetter(seat))
      return seat;
  printf("Unknown seat: %c\n", c);
  exit(-1);
}

Cards ParseHand(char *line) {
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
    if (line[0] == '-')
      ++line;
  }
  return hand;
}

void RandomizeHands(Cards hands[]) {
  struct timeval time;
  gettimeofday(&time, NULL);
  srand((time.tv_sec * 1000) + (time.tv_usec / 1000));

  int deck[TOTAL_CARDS];
  for (int card = 0; card < TOTAL_CARDS; ++card)
    deck[card] = card;

  int remaining_cards = TOTAL_CARDS;
  for (int seat = 0; seat < NUM_SEATS; ++seat)
    for (int i = 0; i < NUM_RANKS; ++i) {
      int r = rand() % remaining_cards;
      hands[seat].Add(deck[r]);
      --remaining_cards;
      std::swap(deck[r], deck[remaining_cards]);
    }
}

void ReadHands(Cards hands[], std::vector<int>& trumps, std::vector<int>& lead_seats) {
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
  CHECK(fgets(line[WEST],  sizeof(line[WEST]),  input_file));
  char* gap = strstr(line[WEST], "    ");
  if (!gap)
    gap = strstr(line[WEST], "\t");
  if (gap != NULL && gap != line[WEST]) {
    // East hand is on the same line as West.
    strcpy(line[EAST], gap);
    *gap = '\0';
  } else {
    CHECK(fgets(line[EAST],  sizeof(line[EAST]),  input_file));
  }
  CHECK(fgets(line[SOUTH], sizeof(line[SOUTH]), input_file));

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

  if (!options.full_analysis && fscanf(input_file, " %s ", line[0]) == 1)
    trumps = {CharToSuit(line[0][0])};

  if (!options.full_analysis && fscanf(input_file, " %s ", line[0]) == 1)
    lead_seats = {CharToSeat(line[0][0])};

  if (input_file != stdin) fclose(input_file);
}

void ShowHandInfo(Cards hand, int seat, int suit, int gap) {
  if (suit == SPADE)
    printf("%*s%c ", gap - 2, " ", SeatLetter(seat));
  else if (suit == CLUB)
    printf("%*s%2d ", gap - 3, " ", hand.CountPoints());
  else
    printf("%*s", gap, " ");
}

void ShowHands(Cards hands[], int rotation = 0) {
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

void ShowCompactHands(Cards hands[], int rotation = 0) {
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

int MemoryEnhancedTestDriver(std::function<int(int, int)> search, int num_tricks, int guess_tricks) {
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
    InteractivePlay(Cards hands[], int trump, int lead_seat, int target_ns_tricks)
      : min_max(hands, trump, lead_seat),
        target_ns_tricks(target_ns_tricks),
        num_tricks(hands[WEST].Size()),
        trump(trump) {
      ShowUsage();
      DetermineContract();

      int ns_tricks = target_ns_tricks;
      for (int p = 0; p < num_tricks * 4; ++p) {
        auto& play = min_max.play(p);
        if (play.TrickStarting()) {
          if (!SetupTrick(play))
            break;
        }

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
              if (num_choices > 1 && p % 4 == 0)
                break;
            }
            --p;
            break;
          case ROTATE:
            rotation = (rotation + 3) % 4;
            if (!play.TrickStarting())
              ShowHands(play.hands, rotation);
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
        puts("******\n"
             "<Enter> to accept the suggestion or input another card like 'CK' or 'KC'.\n"
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
        sprintf(contract, "%d%s by NS", level, SuitSign(trump));
      } else {
        starting_ew_tricks = TOTAL_TRICKS - num_tricks;
        ns_contract = false;
        int level = TOTAL_TRICKS - target_ns_tricks - 6;
        sprintf(contract, "%d%s by EW", level, SuitSign(trump));
      }
    }

    bool SetupTrick(Play& play) {
      // TODO: Clean up! SearchWithCache() recomputes the same info.
      play.all_cards = play.hands[WEST].Union(play.hands[NORTH]).
        Union(play.hands[EAST]).Union(play.hands[SOUTH]);
      if (play.depth > 0) {
        play.ns_tricks_won = play.PreviousPlay().ns_tricks_won + play.PreviousPlay().NsWon();
        play.seat_to_play = play.PreviousPlay().WinningSeat();
      }

      play.ComputePatternHands();
      play.ComputeEquivalence();

      int trick_index = play.depth / 4;
      printf("------ %s: NS %d EW %d ------\n",
             contract, starting_ns_tricks + play.ns_tricks_won,
             starting_ew_tricks + trick_index - play.ns_tricks_won);
      ShowHands(play.hands, rotation);
      if (trick_index == num_tricks - 1) {
        Cards winners;
        int ns_tricks_won = play.CollectLastTrick(&winners);
        printf("====== %s: NS %d EW %d ======\n",
               contract, starting_ns_tricks + ns_tricks_won,
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
      for (int card : play.GetPlayableCards()) {
        if (play.trick->equivalence[card] != card) continue;

        if (SuitOf(card) != last_suit) {
          last_suit = SuitOf(card);
          printf(" %s ", SuitSign(SuitOf(card)));
        }
        printf("%c?\b", NameOf(card)[1]);
        fflush(stdout);

        auto search = [&play, card](int alpha, int beta) {
          play.PlayCard(card);
          Cards winners;
          int ns_tricks = play.NextPlay().SearchWithCache(alpha, beta, &winners);
          play.UnplayCard();
          return ns_tricks;
        };
        int new_ns_tricks = MemoryEnhancedTestDriver(search, play.hands[play.seat_to_play].Size(), ns_tricks);
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
          if (pair.second > max_ns_tricks ||
              (pair.second == max_ns_tricks &&
               RankOf(pair.first) < RankOf(*card_to_play))) {
            *card_to_play = pair.first;
            max_ns_tricks = pair.second;
          }
        }
      } else {
        int min_ns_tricks = TOTAL_TRICKS + 1;
        for (const auto& pair : card_tricks) {
          if (pair.second < min_ns_tricks ||
              (pair.second == min_ns_tricks &&
               RankOf(pair.first) < RankOf(*card_to_play))) {
            *card_to_play = pair.first;
            min_ns_tricks = pair.second;
          }
        }
      }
      printf("%s?", ColoredNameOf(*card_to_play));
      fflush(stdout);

      std::set<int> playable_cards;
      for (const auto& pair : card_tricks)
        playable_cards.insert(pair.first);

      int suit = SuitOf(*card_to_play);
      int rank = RankOf(*card_to_play);
      while (true) {
        switch (int c = toupper(GetRawChar())) {
          case '\n':
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
          case 'S': case 'H': case 'D': case 'C':
            {
              std::set<int> matches;
              for (const auto& card : playable_cards) {
                if (strchr(NameOf(card), c))
                  matches.insert(card);
              }
              if (matches.empty())
                break;
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
              if (strchr(NameOf(card), c))
                matches.insert(card);
            }
            if (matches.empty())
              break;
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
      if (tcgetattr(0, &old) < 0)
        perror("tcsetattr()");
      old.c_lflag &= ~ICANON;
      old.c_lflag &= ~ECHO;
      old.c_cc[VMIN] = 1;
      old.c_cc[VTIME] = 0;
      if (tcsetattr(0, TCSANOW, &old) < 0)
        perror("tcsetattr ICANON");
      if (read(0, &buf, 1) < 0)
        perror ("read()");
      old.c_lflag |= ICANON;
      old.c_lflag |= ECHO;
      if (tcsetattr(0, TCSADRAIN, &old) < 0)
        perror ("tcsetattr ~ICANON");
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
  int c;
  while ((c = getopt(argc, argv, "dfg:i:rs:D:IS")) != -1) {
    switch (c) {
      case 'd': options.discard_suit_bottom = true; break;
      case 'f': options.full_analysis = true; break;
      case 'g': options.guess = atoi(optarg); break;
      case 'i': options.input = optarg; break;
      case 'r': options.randomize = true; break;
      case 's': options.small_card = CharToRank(optarg[0]); break;
      case 'D': options.displaying_depth = atoi(optarg); break;
      case 'I': options.interactive = true; break;
      case 'S': options.show_stats = true; break;
    }
  }

  CardInitializer card_initializer;

  Cards hands[NUM_SEATS];
  std::vector<int> trumps = { NOTRUMP, SPADE, HEART, DIAMOND, CLUB };
  std::vector<int> lead_seats = { WEST, EAST, NORTH, SOUTH };
  if (options.randomize) {
    RandomizeHands(hands);
    if (!options.interactive)
      ShowCompactHands(hands);
  } else
    ReadHands(hands, trumps, lead_seats);

  timeval now;
  gettimeofday(&now, NULL);
  timeval last_round = now;
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
      guess_tricks = ns_tricks;

      if (!options.interactive) {
        if (seat_to_play == WEST || seat_to_play == EAST)
          printf(" %2d", ns_tricks);
        else
          printf(" %2d", num_tricks - ns_tricks);
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
    if (!options.interactive) {
      gettimeofday(&now, NULL);
      printf(" %4.1f s\n", Elapse(last_round, now));
      fflush(stdout);
      last_round = now;
    }

    if (options.show_stats) {
      common_bounds_cache.ShowStatistics();
      cutoff_cache.ShowStatistics();
    }

    common_bounds_cache.Reset();
    cutoff_cache.Reset();
  }
  return 0;
}
