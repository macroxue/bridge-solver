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
#include <set>
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

const char* SuitSign(int suit) {
  static const char* suit_signs[NUM_SUITS + 1] = { "♠", "\e[31m♥\e[0m", "\e[31m♦\e[0m", "♣", "NT" };
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
  CardInitializer(bool rank_first) {
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
      name_of[card][0] = SuitName(SuitOf(card))[0];
      name_of[card][1] = RankName(RankOf(card));
      name_of[card][2] = '\0';
    }
  }
};

struct Options {
  char* input = nullptr;
  int  alpha = 0;
  int  beta = TOTAL_TRICKS;
  int  guess = TOTAL_TRICKS;
  bool use_cache = true;
  bool use_test_driver = true;
  int  small_card = TWO;
  int  displaying_depth = -1;
  bool discard_suit_bottom = true;
  bool rank_first = false;
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
    int Bottom() const { return  BitSize(bits) - 1 - __builtin_clzll(bits); }

    Cards Union(const Cards& c) const { return bits | c.bits; }
    Cards Intersect(const Cards& c) const { return bits & c.bits; }
    Cards Different(const Cards& c) const { return bits & ~c.bits; }

    Cards Add(int card) { bits |= Bit(card); return bits; }
    Cards Remove(int card) { bits &= ~Bit(card); return bits; }

    Cards Add(const Cards& c) { bits |= c.bits; return bits; }
    Cards Remove(const Cards& c) { bits &= ~c.bits; return bits; }

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

template <class Entry, int input_size, int bits>
class Cache {
  public:
    Cache(const char* name)
      : cache_name(name) {
      srand(1);
      for (int i = 0; i < input_size; ++i)
        hash_rand[i] = GenerateHashRandom();
      Reset();
    }

    void Reset() {
      probe_distance = 0;
      lookup_count = hit_count = update_count = collision_count = 0;
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
struct Bounds {
  uint8_t lower : 4;
  uint8_t upper : 4;
};

struct BoundsEntry {
  // Save only the hash instead of full hands to save memory.
  // The chance of getting a hash collision is practically zero.
  uint64_t hash;
  // Bounds depending on the lead seat.
  Bounds bounds[NUM_SEATS];

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

// Put bounds near the root in the VIP cache which is sized to have fewer
// collisions, so more expensive search results are cached.
Cache<BoundsEntry, 4, 21> vip_bounds_cache("VIP Bounds Cache");
Cache<BoundsEntry, 4, 24> common_bounds_cache("Common Bounds Cache");
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

      if (!options.use_cache || all_cards.Size() == 4)
        return Search(alpha, beta);

      const int max_vip_depth = 20;
      const Bounds* cached_bounds = nullptr;
      if (TrickStarting()) {
        ComputePatternHands();
        const auto* bounds_entry = depth <= max_vip_depth ?
          vip_bounds_cache.Lookup(trick->pattern_hands) :
          common_bounds_cache.Lookup(trick->pattern_hands);
        if (bounds_entry)
          cached_bounds = &bounds_entry->bounds[seat_to_play];
      }

      int lower = 0, upper = TOTAL_TRICKS;
      if (cached_bounds) {
        lower = cached_bounds->lower + ns_tricks_won;
        upper = cached_bounds->upper + ns_tricks_won;
        if (upper > TOTAL_TRICKS) upper = TOTAL_TRICKS;
        if (lower >= beta) {
          if (depth <= options.displaying_depth)
            printf("%2d: %c beta cut %d\n", depth, SeatLetter(seat_to_play), lower);
          return lower;
        }
        if (upper <= alpha) {
          if (depth <= options.displaying_depth)
            printf("%2d: %c alpha cut %d\n", depth, SeatLetter(seat_to_play), upper);
          return upper;
        }
        alpha = std::max(alpha, lower);
        beta = std::min(beta, upper);
      }

      int ns_tricks = Search(alpha, beta);
      if (ns_tricks <= alpha)
        upper = ns_tricks;
      else if (ns_tricks < beta)
        upper = lower = ns_tricks;
      else
        lower = ns_tricks;
      lower -= ns_tricks_won;
      upper -= ns_tricks_won;
      if (lower < 0) lower = 0;

      if (TrickStarting()) {
        auto* new_bounds_entry = depth <= max_vip_depth ?
          vip_bounds_cache.Update(trick->pattern_hands) :
          common_bounds_cache.Update(trick->pattern_hands);
        new_bounds_entry->bounds[seat_to_play].lower = lower;
        new_bounds_entry->bounds[seat_to_play].upper = upper;
      }
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
      stats.CutoffAt(depth, TOTAL_TRICKS - 1);
      return bounded_ns_tricks;
    }

    bool CanWinByRank(int seat) const {
      Cards suit = hands[seat].Suit(LeadSuit());
      return !suit.Empty() && WinOver(suit.Top(), PreviousPlay().WinningCard());
    }

    Cards HeuristicPlay(Cards playable_cards) const {
      if (trump != NOTRUMP) {
        Cards my_trumps = playable_cards.Suit(trump);
        Cards opp_hands = hands[NextSeat(1)].Union(hands[NextSeat(3)]);
        if (!my_trumps.Empty()) {
          if (TrickStarting()) {
            if (!opp_hands.Suit(trump).Empty())
              // Draw high trump.
              return Cards().Add(my_trumps.Top());
          } else if (LeadSuit() != trump) {
            // Don't ruff partner's winner unless opponent can win.
            if (PreviousPlay().WinningSeat() != Partner() ||
                (ThirdSeat() && CanWinByRank(LeftHandOpp())))
              return Cards().Add(trick->equivalence[my_trumps.Bottom()]);
          }
        }
        // Don't run winners if opponents still have trumps.
        if (!opp_hands.Suit(trump).Empty())
          return Cards();
      }
      if (TrickStarting()) {
        // Run winners.
        int suits[] = { SPADE, HEART, DIAMOND, CLUB };
        if (trump != NOTRUMP)
          std::swap(suits[NUM_SUITS - 1], suits[trump]);
        Cards our_hands = playable_cards.Union(hands[NextSeat(2)]);
        for (int suit : suits) {
          Cards my_suit = playable_cards.Suit(suit);
          if (!my_suit.Empty() && our_hands.Suit(suit).Top() == all_cards.Suit(suit).Top())
            return Cards().Add(my_suit.Top());
        }
      }
      return Cards();
    }

    void ComputePatternHands() {
      if (depth < 4) {
        for (int suit = 0; suit < NUM_SUITS; ++suit)
          trick->IdentifyPatternCards(hands, suit);
      } else {
        // Recompute the pattern for suits changed by the last trick.
        memcpy(trick->pattern_hands, (trick - 1)->pattern_hands,
               sizeof(trick->pattern_hands));
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

    bool Cutoff(int alpha, int beta, int card_to_play, int* bounded_ns_tricks) {
      PlayCard(card_to_play);
      if (depth < options.displaying_depth - 1)
        ShowTricks(alpha, beta, 0, true);
      int ns_tricks = NextPlay().SearchWithCache(alpha, beta);
      if (depth < options.displaying_depth)
        ShowTricks(alpha, beta, ns_tricks, false);
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
      } else {
        for (int card : all_cards.Suit(trump))
          if (my_cards.Have(card))
            ++fast_tricks;
          else
            break;
        if (depth == 0) {
          for (int suit = 0; suit < NUM_SUITS; ++suit) {
            if (suit == trump) continue;
            int min_suit_len = hands[0].Suit(suit).Size();
            for (int seat = 1; seat < NUM_SEATS; ++seat)
              min_suit_len = std::min(min_suit_len, hands[seat].Suit(suit).Size());
            for (int card : all_cards.Suit(suit))
              if (my_cards.Have(card) && min_suit_len--)
                ++fast_tricks;
              else
                break;
          }
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
      return plays[0].SearchWithCache(alpha, beta);
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

int MemoryEnhancedTestDriver(std::function<int(int, int)> search, int guess_tricks) {
  int upperbound = TOTAL_TRICKS;
  int lowerbound = 0;
  int ns_tricks = guess_tricks;
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
    InteractivePlay(Cards hands[],  int trump, int lead_seat)
      : min_max(hands, trump, lead_seat),
        target_ns_tricks(min_max.Search(0, TOTAL_TRICKS)),
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
              ShowHands(play.hands);
            --p;
            break;
          case EXIT:
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
             "Use 'U' to undo, 'R' to rotate the board or 'E' to exit.\n"
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
      ShowHands(play.hands);
      if (trick_index == num_tricks - 1) {
        int ns_tricks_won = play.CollectLastTrick();
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
          int ns_tricks = play.NextPlay().SearchWithCache(alpha, beta);
          play.UnplayCard();
          return ns_tricks;
        };
        int new_ns_tricks = MemoryEnhancedTestDriver(search, ns_tricks);
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

    enum Action { PLAY, UNDO, ROTATE, EXIT };

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
          case 'E':
            printf("\n");
            return EXIT;
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

    void ShowHands(Cards hands[]) const {
      int gap = 26;
      int seat = (NORTH + rotation) % NUM_SEATS;
      for (int suit = 0; suit < NUM_SUITS; ++suit) {
        if (suit == SPADE) printf("%*s%c ", gap - 2, " ", SeatLetter(seat));
        else printf("%*s", gap, " ");
        hands[seat].ShowSuit(suit);
        printf("\n");
      }
      for (int suit = 0; suit < NUM_SUITS; ++suit) {
        gap = 13;
        seat = (WEST + rotation) % NUM_SEATS;
        if (suit == SPADE) printf("%*s%c ", gap - 2, " ", SeatLetter(seat));
        else printf("%*s", gap, " ");
        hands[seat].ShowSuit(suit);

        gap = 26 - std::max(1, hands[seat].Suit(suit).Size());
        seat = (EAST + rotation) % NUM_SEATS;
        if (suit == SPADE) printf("%*s%c ", gap - 2, " ", SeatLetter(seat));
        else printf("%*s", gap, " ");
        hands[seat].ShowSuit(suit);
        printf("\n");
      }
      gap = 26;
      seat = (SOUTH + rotation) % NUM_SEATS;
      for (int suit = 0; suit < NUM_SUITS; ++suit) {
        if (suit == SPADE) printf("%*s%c ", gap - 2, " ", SeatLetter(seat));
        else printf("%*s", gap, " ");
        hands[seat].ShowSuit(suit);
        printf("\n");
      }
    }

    void ShowCompactHands(Cards hands[]) const {
      int seat = (NORTH + rotation) % NUM_SEATS;
      printf("%25s%c:", " ", SeatLetter(seat));
      hands[seat].Show();
      printf("\n");

      seat = (WEST + rotation) % NUM_SEATS;
      int num_cards = hands[seat].Size();
      printf("%*s%c:", 14 - num_cards, " ", SeatLetter(seat));
      hands[seat].Show();

      seat = (EAST + rotation) % NUM_SEATS;
      printf("%*s%c:", num_cards + 8, " ", SeatLetter(seat));
      hands[seat].Show();
      printf("\n");

      seat = (SOUTH + rotation) % NUM_SEATS;
      printf("%25s%c:", " ", SeatLetter(seat));
      hands[seat].Show();
      printf("\n");
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
  while ((c = getopt(argc, argv, "a:b:cdfg:i:rs:tD:IS")) != -1) {
    switch (c) {
      case 'a': options.alpha = atoi(optarg); break;
      case 'b': options.beta = atoi(optarg); break;
      case 'c': options.use_cache = false; break;
      case 'd': options.discard_suit_bottom = false; break;
      case 'f': options.full_analysis = true; break;
      case 'g': options.guess = atoi(optarg); break;
      case 'i': options.input = optarg; break;
      case 'r': options.rank_first = true; break;
      case 's': options.small_card = CharToRank(optarg[0]); break;
      case 't': options.use_test_driver = false; break;
      case 'D': options.displaying_depth = atoi(optarg); break;
      case 'I': options.interactive = true; break;
      case 'S': options.show_stats = true; break;
    }
  }

  CardInitializer card_initializer(options.rank_first);

  FILE* input_file = stdin;
  if (options.input) {
    input_file = fopen(options.input, "rt");
    if (!input_file) {
      fprintf(stderr, "Input file not found: '%s'.\n", options.input);
      exit(-1);
    }
  }
  // read hands
  char line[NUM_SEATS][80];
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
  if (!options.full_analysis && fscanf(input_file, " %s ", line[0]) == 1)
    trumps.push_back(CharToSuit(line[0][0]));
  else
    trumps = { SPADE, HEART, DIAMOND, CLUB, NOTRUMP };

  std::vector<int> lead_seats;
  if (!options.full_analysis && fscanf(input_file, " %s ", line[0]) == 1)
    lead_seats.push_back(CharToSeat(line[0][0]));
  else
    lead_seats = { WEST, EAST, NORTH, SOUTH };

  if (input_file != stdin) fclose(input_file);

  timeval now;
  gettimeofday(&now, NULL);
  timeval last_round = now;
  for (int trump : trumps) {
    if (!options.interactive) {
      printf("%c", SuitName(trump)[0]);
    }
    // TODO: Best guess with losing trick count.
    int guess_tricks = std::min(options.guess, num_tricks);
    for (int seat_to_play : lead_seats) {
      int ns_tricks;
      int alpha = options.alpha;
      int beta = std::min(options.beta, num_tricks);
      MinMax min_max(hands, trump, seat_to_play);
      if (options.use_test_driver) {
        auto search = [&min_max](int alpha, int beta) {
          return min_max.Search(alpha, beta);
        };
        ns_tricks = MemoryEnhancedTestDriver(search, guess_tricks);
      } else {
        ns_tricks = min_max.Search(alpha, beta);
      }
      guess_tricks = ns_tricks;

      if (!options.interactive) {
        if (seat_to_play == WEST || seat_to_play == EAST)
          printf(" %2d", ns_tricks);
        else
          printf(" %2d", num_tricks - ns_tricks);
        continue;
      }
      int num_tricks = hands[WEST].Size();
      if (num_tricks < TOTAL_TRICKS ||
          (num_tricks == TOTAL_TRICKS && ns_tricks >= 7 &&
           (seat_to_play == WEST || seat_to_play == EAST)) ||
          (num_tricks == TOTAL_TRICKS && ns_tricks < 7 &&
           (seat_to_play == NORTH || seat_to_play == SOUTH)))
        InteractivePlay(hands, trump, seat_to_play);
      else {
        int declarer = (seat_to_play + 3) % NUM_SEATS;
        printf("%s can't make a %s contract.\n", SeatName(declarer), SuitSign(trump));
      }
    }
    if (!options.interactive) {
      gettimeofday(&now, NULL);
      printf(" %4.1f s\n", Elapse(last_round, now));
      last_round = now;
    }

    if (options.show_stats) {
      vip_bounds_cache.ShowStatistics();
      common_bounds_cache.ShowStatistics();
      cutoff_cache.ShowStatistics();
    }

    vip_bounds_cache.Reset();
    common_bounds_cache.Reset();
    cutoff_cache.Reset();
  }
  return 0;
}
