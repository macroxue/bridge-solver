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
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <algorithm>

#define CHECK(statement)  if (!(statement)) printf("CHECK("#statement") failed")

namespace {

const char *seat_name[] = { "West", "North", "East", "South" };

enum { SPADE, HEART, DIAMOND, CLUB, NUM_SUITS, NOTRUMP = NUM_SUITS };
enum { TWO, THREE, FOUR, FIVE, SIX, SEVEN, EIGHT, NINE, TEN,
       JACK, QUEEN, KING, ACE, NUM_RANKS };
enum { WEST, NORTH, EAST, SOUTH, NUM_SEATS };

int SuitOf(int card) { return card / 13; }
int RankOf(int card) { return card % 13; }
int CardFromSuitRank(int suit, int rank) { return suit * 13 + rank; }
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

}  // namespace

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
        printf("%s ", CardName(card));
      printf("\n");
    }

  private:
    uint64_t Bit(int index) const { return uint64_t(1) << index; }

    uint64_t cards_;
};

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
    if (cards.Slice(prev_card, cur_card + 1) == all_cards.Slice(prev_card, cur_card + 1)) {
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
  int max_tricks = INT_MIN;
  int min_tricks = INT_MAX;
  for (int card_to_play = playable_cards.Begin(); card_to_play != playable_cards.End();
       card_to_play = playable_cards.After(card_to_play)) {

    int next_seat_to_play = Play(seat_to_play, card_to_play, &state);
    int num_tricks = MinMax(alpha, beta, next_seat_to_play, depth + 1);
    Unplay(seat_to_play, card_to_play, state);
    if (depth < 4)
      printf("%*s => %d\n", depth * 2 + 2, CardName(card_to_play), num_tricks);

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

}  // namespace

int main() {
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
  Node node(hands, trump, seat_to_play);
  int ns_tricks = node.MinMax(0, num_tricks, seat_to_play, 0);
  printf("NS tricks: %d      EW tricks: %d\n",
         ns_tricks, num_tricks - ns_tricks);
  return 0;
}
