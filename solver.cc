#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <algorithm>
#include <vector>

#define CHECK(statement)  if (!(statement)) printf("CHECK("#statement") failed")

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

struct Options {
  int  alpha;
  int  beta;
  int  guess;
  bool use_cache;
  bool use_test_driver;
  int  small_card;
  int  displaying_depth;
  bool discard_suit_bottom;
  bool rank_first;
  bool show_stats;
  bool full_analysis;

  Options()
    : alpha(0), beta(TOTAL_TRICKS), guess(TOTAL_TRICKS),
      use_cache(true), use_test_driver(true),
      small_card(TWO), displaying_depth(0), discard_suit_bottom(true),
      rank_first(false), show_stats(false), full_analysis(false) {}
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
      : cache_name(name), probe_distance(0),
        lookup_count(0), hit_count(0), update_count(0), collision_count(0) {
      srand(1);
      for (int i = 0; i < input_size; ++i)
        hash_rand[i] = GenerateHashRandom();
      Reset();
    }

    void Reset() {
      for (int i = 0; i < size; ++i)
        entries_[i].hash = 0;
    }

    void ShowStatistics() const {
      int loaded_count = 0;
      for (int i = 0; i < size; ++i)
        loaded_count += (entries_[i].hash != 0);

      printf("--- %s Statistics ---\n", cache_name);
      printf("lookups: %8d   hits:     %8d (%5.2f%%)   probe distance: %d\n",
             lookup_count, hit_count, hit_count * 100.0 / lookup_count, probe_distance);
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
      for (int d = 0; d < max_probe_distance; ++d) {
        Entry& entry = entries_[(index + d) & (size - 1)];
        bool collided = entry.hash != 0 && entry.hash != hash;
        if (!collided || d == max_probe_distance - 1) {
          probe_distance = std::max(probe_distance, d + 1);
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

    static const int max_probe_distance = 16;
    static const int size = 1 << bits;

    const char* cache_name;
    int probe_distance;
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

struct CutoffEntry {
  uint64_t hash;
  // Cut-off card depending on the seat to play.
  char card[NUM_SEATS];

  void Reset(uint64_t hash_in) {
    hash = hash_in;
    memset(card, TOTAL_CARDS, sizeof(card));
  }
};
#pragma pack(pop)

Cache<BoundsEntry, 4, 22> bounds_cache("Bounds Cache");
Cache<CutoffEntry, 2, 19> cutoff_cache("Cut-off Cache");

struct Trick {
  Cards pattern_hands[NUM_SEATS];
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
    for (int card : all_cards)
      pattern_hands[card_holder[card]].Add(CardOf(suit, relative_rank--));
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

    int SearchWithCache(int alpha, int beta) {
      if (TrickStarting()) {
        all_cards = hands[WEST].Union(hands[NORTH]).Union(hands[EAST]).Union(hands[SOUTH]);
        if (depth > 0) {
          ns_tricks_won = PreviousPlay().ns_tricks_won + PreviousPlay().NsWon();
          seat_to_play = PreviousPlay().WinningSeat();
        }

        int fast_tricks = FastTricks();
        if (NsToPlay() && ns_tricks_won + fast_tricks >= beta)
          return ns_tricks_won + fast_tricks;
        int remaining_tricks = hands[0].Size();
        if (!NsToPlay() && ns_tricks_won + (remaining_tricks - fast_tricks) <= alpha)
          return ns_tricks_won + (remaining_tricks - fast_tricks);
      } else {
        all_cards = PreviousPlay().all_cards;
        ns_tricks_won = PreviousPlay().ns_tricks_won;
        seat_to_play = PreviousPlay().NextSeat();
      }

      if (!options.use_cache || !TrickStarting() || all_cards.Size() == 4)
        return Search(alpha, beta);

      ComputePatternHands();
      Cards* pattern_hands = trick->pattern_hands;

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

      int ns_tricks = Search(alpha, beta);
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

  private:
    int Search(int alpha, int beta) {
      if (all_cards.Size() == 4) {
        int ns_tricks = CollectLastTrick();
        //if (depth < options.displaying_depth)
        //  ShowTricks(alpha, beta, ns_tricks);
        return ns_tricks;
      }

      if (TrickStarting())
        ComputeEquivalence();

      Cards playable_cards = GetPlayableCards();
      Cards cutoff_cards = LookupCutoffCards();
      if (playable_cards.Intersect(cutoff_cards).Empty())
        cutoff_cards = HeuristicPlay(playable_cards);
      Cards sets_of_playables[2] = {
        playable_cards.Intersect(cutoff_cards),
        playable_cards.Different(cutoff_cards)
      };
      int bounded_ns_tricks = NsToPlay() ? 0 : TOTAL_TRICKS;
      int card_count = 0;
      for (Cards playables : sets_of_playables)
        for (int card : playables) {
          if (trick->equivalence[card] != card) continue;
          if (Cutoff(alpha, beta, card, &bounded_ns_tricks)) {
            if (!cutoff_cards.Have(card))
              SaveCutoffCard(card);
            stats.CutoffAt(depth, card_count);
            return bounded_ns_tricks;
          }
          ++card_count;
        }
      return bounded_ns_tricks;
    }

    Cards HeuristicPlay(Cards playable_cards) const {
      if (trump != NOTRUMP) {
        Cards my_trumps = playable_cards.Suit(trump);
        if (!my_trumps.Empty()) {
          if (TrickStarting())
            // Draw high trump.
            return Cards().Add(trick->equivalence[my_trumps.Top()]);
          else if (LeadSuit() != trump)
            // Ruff with low trump.
            return Cards().Add(trick->equivalence[my_trumps.Bottom()]);
        }
      }
      return Cards();
    }

    void ComputePatternHands() {
      // Only recompute pattern for suits touched in the previous trick.
      bool touched[NUM_SUITS] = { false, false, false, false };
      if (depth >= 4) {
        for (int i = depth - 4; i < depth; ++i)
          touched[SuitOf(plays[i].card_played)] = true;
        memcpy(trick->pattern_hands, (trick - 1)->pattern_hands, sizeof(trick->pattern_hands));
      } else {
        for (int suit = 0; suit < NUM_SUITS; ++suit)
          touched[suit] = true;
        memset(trick->pattern_hands, 0, sizeof(trick->pattern_hands));
      }
      for (int suit = 0; suit < NUM_SUITS; ++suit) {
        if (!touched[suit]) continue;
        trick->IdentifyPatternCards(hands, suit);
      }
    }

    void ComputeEquivalence() {
      // Only recompute equivalence for suits touched in the previous trick.
      bool touched[NUM_SUITS] = { false, false, false, false };
      if (depth > 0) {
        for (int i = depth - 4; i < depth; ++i)
          touched[SuitOf(plays[i].card_played)] = true;
        memcpy(trick->equivalence, (trick - 1)->equivalence, sizeof(trick->equivalence));
      } else {
        for (int suit = 0; suit < NUM_SUITS; ++suit)
          touched[suit] = true;
      }
      for (int suit = 0; suit < NUM_SUITS; ++suit) {
        if (!touched[suit]) continue;
        for (int seat = 0; seat < NUM_SEATS; ++seat)
          trick->IdentifyEquivalentCards(hands[seat].Suit(suit), all_cards.Suit(suit));
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

    bool Cutoff(int alpha, int beta, int card_to_play, int* bounded_ns_tricks) {
      PlayCard(card_to_play);
      int ns_tricks = NextPlay().SearchWithCache(alpha, beta);
      UnplayCard();
      if (depth < options.displaying_depth)
        ShowTricks(alpha, beta, ns_tricks);

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

    int FastTricks() const {
      Cards my_cards = hands[seat_to_play];
      Cards both_hands = my_cards.Union(hands[NextSeat(2)]);
      int fast_tricks = 0;
      if (trump == NOTRUMP || all_cards.Suit(trump) == both_hands.Suit(trump)) {
        for (int suit = 0; suit < NUM_SUITS; ++suit) {
          for (int card : all_cards.Suit(suit))
            if (my_cards.Have(card))
              ++fast_tricks;
            else
              break;
        }
      }
      return fast_tricks;
    }

    int CollectLastTrick() {
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
      return ns_tricks_won + IsNs(winning_seat);
    }

    void ShowTricks(int alpha, int beta, int ns_tricks) const {
      printf("%2d:", depth);
      for (int i = 0; i <= depth; ++i) {
        if ((i & 3) == 0)
          printf(" %c", SeatLetter(plays[i].seat_to_play));
        printf(" %s", NameOf(plays[i].card_played));
      }
      printf(" -> %d (%d %d)\n", ns_tricks, alpha, beta);
    }

    bool TrickStarting() const { return (depth & 3) == 0; }
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
      return plays[0].SearchWithCache(alpha, beta);
    }

  private:
    Cards  hands[NUM_SEATS];
    Play   plays[TOTAL_CARDS];
    Trick  tricks[TOTAL_TRICKS];
};

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
                             int guess_tricks) {
  int upperbound = TOTAL_TRICKS;
  int lowerbound = 0;
  int ns_tricks = guess_tricks;
  while (lowerbound < upperbound) {
    MinMax min_max(hands, trump, seat_to_play);
    int beta = (ns_tricks == lowerbound ? ns_tricks + 1 : ns_tricks);
    ns_tricks = min_max.Search(beta - 1, beta);
    if (ns_tricks < beta)
      upperbound = ns_tricks;
    else
      lowerbound = ns_tricks;
    if (options.displaying_depth > 0)
      printf("lowerbound: %d\tupperbound: %d\n", lowerbound, upperbound);
  }
  return ns_tricks;
}

double Elapse(const timeval& from, const timeval& to) {
  return (to.tv_sec - from.tv_sec) + (double(to.tv_usec) - from.tv_usec) * 1e-6;
}

}  // namespace

int main(int argc, char* argv[]) {
  int c;
  while ((c = getopt(argc, argv, "a:b:cdfg:rs:tD:S")) != -1) {
    switch (c) {
      case 'a': options.alpha = atoi(optarg); break;
      case 'b': options.beta = atoi(optarg); break;
      case 'c': options.use_cache = false; break;
      case 'd': options.discard_suit_bottom = false; break;
      case 'f': options.full_analysis = true; break;
      case 'g': options.guess = atoi(optarg); break;
      case 'r': options.rank_first = true; break;
      case 's': options.small_card = CharToRank(optarg[0]); break;
      case 't': options.use_test_driver = false; break;
      case 'D': options.displaying_depth = atoi(optarg); break;
      case 'S': options.show_stats = true; break;
    }
  }

  CardInitializer card_initializer(options.rank_first);

  // read hands
  char line[NUM_SEATS][80];
  CHECK(fgets(line[NORTH], sizeof(line[NORTH]), stdin));
  CHECK(fgets(line[WEST],  sizeof(line[WEST]),  stdin));
  char* gap = strstr(line[WEST], "    ");
  if (!gap)
    gap = strstr(line[WEST], "\t");
  if (gap != NULL && gap != line[WEST]) {
    // East hand is on the same line as West.
    strcpy(line[EAST], gap);
    *gap = '\0';
  } else {
    CHECK(fgets(line[EAST],  sizeof(line[EAST]),  stdin));
  }
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

  std::vector<int> trumps;
  if (!options.full_analysis && scanf(" %s ", line[0]) == 1)
    trumps.push_back(CharToSuit(line[0][0]));
  else
    trumps = { SPADE, HEART, DIAMOND, CLUB, NOTRUMP };

  std::vector<int> lead_seats;
  if (!options.full_analysis && scanf(" %s ", line[0]) == 1)
    lead_seats.push_back(CharToSeat(line[0][0]));
  else
    lead_seats = { WEST, EAST, NORTH, SOUTH };

  timeval now;
  gettimeofday(&now, NULL);
  timeval start = now, last_round = now;
  for (int trump : trumps) {
    // TODO: Best guess with losing trick count.
    int guess_tricks = std::min(options.guess, num_tricks);
    for (int seat_to_play : lead_seats) {
      int ns_tricks;
      int alpha = options.alpha;
      int beta = std::min(options.beta, num_tricks);
      if (options.use_test_driver) {
        ns_tricks = MemoryEnhancedTestDriver(hands, trump, seat_to_play, guess_tricks);
      } else {
        MinMax min_max(hands, trump, seat_to_play);
        ns_tricks = min_max.Search(alpha, beta);
      }
      guess_tricks = ns_tricks;
      gettimeofday(&now, NULL);
      printf("%s trump\t%s to lead\tNS %d\tEW %d\tTime %.3f s\n",
             SuitName(trump), SeatName(seat_to_play), ns_tricks, num_tricks - ns_tricks,
             Elapse(start, now));
    }
    if (trumps.size() > 1)
      printf("%s trump total time %.3f s\n", SuitName(trump), Elapse(last_round, now));
    last_round = now;

    if (options.show_stats) {
      bounds_cache.ShowStatistics();
      cutoff_cache.ShowStatistics();
    }
    bounds_cache.Reset();
    cutoff_cache.Reset();
  }
  return 0;
}
