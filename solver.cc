/*
minmax(N, a, b, P)
{
    if is_leaf(N)
        return evaluate(N);
    foreach S in sons(N) {
        v = minmax(S, a, b, N);
        if is_max(N) {
            if is_min(P) and v >= b
                return v;
            a = max(a, v);
        } else {
            if is_max(P) and v <= a
                return v;
            b = min(b, v);
        }
    }
}
*/

#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <algorithm>

#define CHECK(statement)  if (!(statement)) printf("CHECK("#statement") failed")

const int CARDS_PER_HAND = 13;
const char suit_name[] = "SHDC";
const char rank_name[] = "23456789TJQKA";
const char *seat_name[] = { "West", "North", "East", "South" };

enum { SPADE, HEART, DIAMOND, CLUB, NUM_SUITS, NOTRUMP = NUM_SUITS };
enum { TWO, THREE, FOUR, FIVE, SIX, SEVEN, EIGHT, NINE, TEN,
       JACK, QUEEN, KING, ACE, NUM_RANKS };
enum { WEST, NORTH, EAST, SOUTH, NUM_SEATS };

int SuitOf(int card) {
  return card / 13;
}

int RankOf(int card) {
  return card % 13;
}

int CardFromSuitRank(int suit, int rank) {
  return suit * 13 + rank;
}

class Cards {
  public:
    Cards() : cards_(0) {}
    Cards(uint64_t cards) : cards_(cards) {}

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
        printf("%c%c ", suit_name[SuitOf(card)], rank_name[RankOf(card)]);
      printf("\n");
    }

  private:
    uint64_t Bit(int index) const { return uint64_t(1) << index; }

    uint64_t cards_;
};

struct Node {
  int   trump;
  int   ns_tricks;
  int   lead_seat;
  int   lead_suit;
  int   cards_in_trick;
  int   winning_card;
  int   winning_seat;
  Cards hands[NUM_SEATS];
  int   current_trick[NUM_SEATS];
};

bool IsNS(int seat) {
  return seat % 2;
}

bool WinOver(int c1, int c2, int trump) {
  return (SuitOf(c1) == SuitOf(c2) && RankOf(c1) > RankOf(c2)) ||
         (SuitOf(c1) != SuitOf(c2) && SuitOf(c1) == trump);
}

Cards RemoveRedundantCards(Cards cards, Cards all_cards, int suit) {
  assert(suit != NOTRUMP);
  if (cards.Size() <= 1)
    return cards;

  Cards redundant_cards;
  int prev_card = cards.Begin();
  int cur_card = cards.After(prev_card);
  while (cur_card != cards.End()) {
    if (cards.Slice(prev_card, cur_card + 1) == all_cards.Slice(prev_card, cur_card + 1)) {
      redundant_cards.Add(cur_card);
    } else {
      prev_card = cur_card;
    }
    cur_card = cards.After(cur_card);
  }
  return cards.Remove(redundant_cards);
}

Cards GetPlayableCards(Cards hand, Cards all_cards, int lead_suit) {
  if (lead_suit == NOTRUMP || hand.GetSuit(lead_suit).Empty()) {
    Cards cards;
    for (int s = 0; s < NUM_SUITS; ++s)
      cards.Add(RemoveRedundantCards(hand.GetSuit(s), all_cards, s));
    return cards;
  } else {
    return RemoveRedundantCards(hand.GetSuit(lead_suit), all_cards, lead_suit);
  }
}

Cards all_cards;
int depth = 0;

int MinMax(Node *node, int min_tricks, int max_tricks, int is_parent_NS) {
  int seat_to_play = (node->lead_seat + node->cards_in_trick) % NUM_SEATS;
  Cards& hand_to_play = node->hands[seat_to_play];
#if 0
  if (hand_to_play.Empty()) {
    return node->ns_tricks;
  }
#else
  if (hand_to_play.Size() == 1 && node->cards_in_trick == 0) {
    int winning_seat = seat_to_play;
    int winning_card = hand_to_play.Begin();
    for (int i = 1; i < NUM_SEATS; i++) {
      int seat = (seat_to_play + i) % NUM_SEATS;
      int card_to_play = node->hands[seat].Begin();
      if (WinOver(card_to_play, winning_card, node->trump)) {
        winning_card = card_to_play;
        winning_seat = seat;
      }
    }
    return node->ns_tricks + IsNS(winning_seat);
  }
#endif

  Cards playable_cards;
  if (node->cards_in_trick == 0)
    playable_cards = GetPlayableCards(hand_to_play, all_cards, NOTRUMP);
  else
    playable_cards = GetPlayableCards(hand_to_play, all_cards, node->lead_suit);

  // TODO: reorder playable cards

  int best_card = -1;
  for (int card_to_play = playable_cards.Begin(); card_to_play != playable_cards.End();
       card_to_play = playable_cards.After(card_to_play)) {
    int   prev_ns_tricks      = node->ns_tricks;
    int   prev_lead_seat      = node->lead_seat;
    int   prev_lead_suit      = node->lead_suit;
    int   prev_cards_in_trick = node->cards_in_trick;
    int   prev_winning_card   = node->winning_card;
    int   prev_winning_seat   = node->winning_seat;
    int   prev_card = node->current_trick[node->cards_in_trick];

    node->current_trick[node->cards_in_trick] = card_to_play;
    node->cards_in_trick ++;

    if (node->cards_in_trick == 1)
      node->lead_suit = SuitOf(card_to_play);

    // who's winning?
    if (node->cards_in_trick == 1
        || WinOver(card_to_play, node->winning_card, node->trump)) {
      node->winning_card = card_to_play;
      node->winning_seat = seat_to_play;
    }

    // who won?
    if (node->cards_in_trick == NUM_SEATS) {
      node->lead_seat = node->winning_seat;
      node->ns_tricks += IsNS(node->winning_seat);
      node->cards_in_trick = 0;
      // remove cards from global suits
      for (int i = 0; i < NUM_SEATS; ++i)
        all_cards.Remove(node->current_trick[i]);
    }

    // remove played card from hand
    hand_to_play.Remove(card_to_play);

    // recursive search
    depth ++;
    int num_tricks = MinMax(node, min_tricks, max_tricks, IsNS(seat_to_play));
    depth --;

    // add played card back to hand
    hand_to_play.Add(card_to_play);

    if (node->cards_in_trick == 0) {
      // add cards back to global suits
      for (int i = 0; i < NUM_SEATS; ++i)
        all_cards.Add(node->current_trick[i]);
    }

    // restore state
    node->ns_tricks      = prev_ns_tricks;
    node->lead_seat      = prev_lead_seat;
    node->lead_suit      = prev_lead_suit;
    node->cards_in_trick = prev_cards_in_trick;
    node->winning_card   = prev_winning_card;
    node->winning_seat   = prev_winning_seat;
    node->current_trick[node->cards_in_trick] = prev_card;

    // possible pruning
    if (IsNS(seat_to_play)) {
      if (!is_parent_NS && num_tricks >= max_tricks) {
        return num_tricks;
      }
      if (min_tricks < num_tricks) {
        min_tricks = num_tricks;
        best_card  = card_to_play;
      }
    } else {
      if (is_parent_NS && num_tricks <= min_tricks) {
        return num_tricks;
      }
      if (max_tricks > num_tricks) {
        max_tricks = num_tricks;
        best_card  = card_to_play;
      }
    }
  }
  if (best_card >= 0 && depth < 4) {
    for (int j = 0; j < depth * 2; j ++) putchar(' ');
    printf("%c%c => %d\n",
           suit_name[SuitOf(best_card)], rank_name[RankOf(best_card)],
           (IsNS(seat_to_play) ? min_tricks : max_tricks));
  }
  if (IsNS(seat_to_play))
    return min_tricks;
  else
    return max_tricks;
}

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
  Cards hand;
  for (int suit = 0; suit < NUM_SUITS; suit++) {
    while (line[0] && isspace(line[0])) line++;
    while (line[0] && !isspace(line[0]) && line[0] != '-') {
      int rank = CharToRank(line[0]);
      int card = CardFromSuitRank(suit, rank);
      if (all_cards.Has(card)) {
        printf("%c%c showed up twice.\n", suit_name[suit], rank_name[rank]);
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

int main() {
  // read hands
  char line[NUM_SEATS][80];
  CHECK(fgets(line[NORTH], sizeof(line[NORTH]), stdin));
  CHECK(fgets(line[WEST],  sizeof(line[WEST]),  stdin));
  CHECK(fgets(line[EAST],  sizeof(line[EAST]),  stdin));
  CHECK(fgets(line[SOUTH], sizeof(line[SOUTH]), stdin));

  Node node;
  int num_tricks = 0;
  for (int seat = 0; seat < NUM_SEATS; seat ++) {
    node.hands[seat] = ParseHand(line[seat]);
    if (seat == 0)
      num_tricks = node.hands[seat].Size();
    else
      if (num_tricks != node.hands[seat].Size()) {
        printf("%s has %d cards, while %s has %d.\n",
               seat_name[seat], node.hands[seat].Size(),
               seat_name[0], num_tricks);
        exit(-1);
      }
  }

  CHECK(fgets(line[0], sizeof(line[0]), stdin));
  node.trump = CharToSuit(line[0][0]);
  CHECK(fgets(line[0], sizeof(line[0]), stdin));
  node.lead_seat = CharToSeat(line[0][0]);

  printf("Solving ...\n");
  node.cards_in_trick = 0;
  node.ns_tricks      = 0;
  int ns_tricks = MinMax(&node, 0, num_tricks, IsNS(node.lead_seat));
  printf("NS tricks: %d      EW tricks: %d\n",
         ns_tricks, num_tricks - ns_tricks);
  return 0;
}
