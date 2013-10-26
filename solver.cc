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
#include <stdio.h>
#include <algorithm>

#define CHECK(statement)  if (!(statement)) printf("CHECK("#statement") failed")

const int CARDS_PER_HAND = 13;
const char suit_name[] = "SHDC";
const char rank_name[] = "23456789TJQKA";
const char *seat_name[] = { "West", "North", "East", "South" };


enum Suit { SPADE, HEART, DIAMOND, CLUB, NUM_SUITS, NOTRUMP = NUM_SUITS };
enum Rank { TWO, THREE, FOUR, FIVE, SIX, SEVEN, EIGHT, NINE, TEN,
            JACK, QUEEN, KING, ACE, NUM_RANKS };
enum Seat { WEST, NORTH, EAST, SOUTH, NUM_SEATS };

struct Card {
  int   suit;
  int   rank;
  Card* prev;
  Card* next;
};

struct Hand {
  int   num_cards;
  Card* cards[CARDS_PER_HAND];
  int   suit_count[NUM_SUITS];
};

struct Node {
  int   trump;
  int   ns_tricks;
  int   lead_seat;
  int   lead_suit;
  int   cards_in_trick;
  Card* winning_card;
  int   winning_seat;
  Hand  hands[NUM_SEATS];
  Card* current_trick[NUM_SEATS];
};

Card all_cards[NUM_SUITS][NUM_RANKS];
Card* all_suits[NUM_SUITS];

#if 1

bool is_ns(int seat) {
  return seat % 2;
}

bool win_over(Card* c1, Card* c2, int trump) {
  return c1->suit == c2->suit && c1->rank > c2->rank ||
         c1->suit != c2->suit && c1->suit == trump;
}

int gen_valid_cards(Hand *hand, int lead_suit,
                    Card* valid_cards[], int valid_index[],
                    Card* winning_card, bool partner) {
  int begin_index = 0;
  for (int suit = 0; suit < lead_suit; suit++)
    begin_index += hand->suit_count[suit];

  int suit_count = hand->suit_count[lead_suit];
  assert(suit_count > 0);

  // big card first
  int num_valid_cards = 0;
  for (int i = 0; i < suit_count; ) {
    Card* card;

    valid_index[num_valid_cards] = begin_index + i;
    valid_cards[num_valid_cards] = card = hand->cards[begin_index + i];
    num_valid_cards++;

    // skip cards that are equivalent to the previous one
    i++;
    while (i < suit_count && card->next == hand->cards[begin_index + i]) {
      card = card->next;
      i++;
    }
  }

  if (hand->cards[begin_index]->rank < winning_card->rank
      /*|| partner && winning_card->rank >= JACK*/) {
    // reverse the order, small card first
    for (int i = 0; i < num_valid_cards/2; i++) {
      std::swap(valid_cards[i], valid_cards[num_valid_cards - 1 - i]);
      std::swap(valid_index[i], valid_index[num_valid_cards - 1 - i]);
    }
  }
  return num_valid_cards;
}

int depth = 0;

int min_max(Node *node, int min_tricks, int max_tricks, int is_parent_NS) {
  int seat_to_play = (node->lead_seat + node->cards_in_trick) % NUM_SEATS;
  Hand* hand_to_play = &node->hands[seat_to_play];
#if 0
  if (hand_to_play->num_cards == 0) {
    return node->ns_tricks;
  }
#else
  if (hand_to_play->num_cards == 1 && node->cards_in_trick == 0) {
    int winning_seat = seat_to_play;
    Card* winning_card = hand_to_play->cards[0];
    for (int i = 1; i < NUM_SEATS; i++) {
      int seat = (seat_to_play + i) % NUM_SEATS;
      if (win_over(node->hands[seat].cards[0], winning_card,
                   node->trump)) {
        winning_card = node->hands[seat].cards[0];
        winning_seat = seat;
      }
    }
    return node->ns_tricks + is_ns(winning_seat);
  }
#endif

  int   num_valid_cards;
  Card* valid_cards[CARDS_PER_HAND];
  int   valid_index[CARDS_PER_HAND];

  if (node->cards_in_trick == 0
      || hand_to_play->suit_count[node->lead_suit] == 0) {
    num_valid_cards = 0;
    for (int i = 0; i < hand_to_play->num_cards; ) {
      Card*  card;

      /*
         j = num_valid_cards;
         if (j && valid_cards[j-1]->suit == hand_to_play->cards[i]->suit
         && valid_cards[j-1]->rank == hand_to_play->cards[i]->rank+1)
         assert(valid_cards[j-1]->next == hand_to_play->cards[i]);
         */

      valid_index[num_valid_cards] = i;
      valid_cards[num_valid_cards] = card = hand_to_play->cards[i];
      num_valid_cards++;

      // skip cards that are equivalent to the previous one
      i++;
      while (i < hand_to_play->num_cards
             && card->next == hand_to_play->cards[i]) {
        card = card->next;
        i++;
      }
    }
  } else {
    num_valid_cards = gen_valid_cards(hand_to_play,
                                      node->lead_suit,
                                      valid_cards, valid_index,
                                      node->winning_card,
                                      is_ns(seat_to_play) ==
                                      is_ns(node->winning_seat));
  }

  Card* best_card = NULL;
  for (int i = 0; i < num_valid_cards; i++) {
    int   prev_ns_tricks      = node->ns_tricks;
    int   prev_lead_seat      = node->lead_seat;
    int   prev_lead_suit      = node->lead_suit;
    int   prev_cards_in_trick = node->cards_in_trick;
    Card* prev_winning_card   = node->winning_card;
    int   prev_winning_seat   = node->winning_seat;
    Card* prev_card = node->current_trick[node->cards_in_trick];

    Card* card_to_play = valid_cards[i];
    node->current_trick[node->cards_in_trick] = card_to_play;
    node->cards_in_trick ++;

    if (node->cards_in_trick == 1)
      node->lead_suit = card_to_play->suit;

    // who's winning?
    if (node->cards_in_trick == 1
        || win_over(card_to_play, node->winning_card, node->trump)) {
      node->winning_card = card_to_play;
      node->winning_seat = seat_to_play;
    }

    // who won?
    if (node->cards_in_trick == NUM_SEATS) {
      node->lead_seat = node->winning_seat;
      node->ns_tricks += is_ns(node->winning_seat);
      node->cards_in_trick = 0;

      // remove cards from global suits
      for (int j = 0; j < NUM_SEATS; j++) {
        Card* card = node->current_trick[j];
        if (card->prev)
          card->prev->next = card->next;
        else
          all_suits[card->suit] = card->next;
        if (card->next)
          card->next->prev = card->prev;
      }
    }

    // remove played card from hand
    for (int j = valid_index[i]; j < hand_to_play->num_cards - 1; j++)
      hand_to_play->cards[j] = hand_to_play->cards[j + 1];
    hand_to_play->suit_count[card_to_play->suit]--;
    hand_to_play->num_cards--;

    // recursive search
    depth ++;
    int num_tricks = min_max(node, min_tricks, max_tricks, is_ns(seat_to_play));
    depth --;

    // add played card back to hand
    hand_to_play->num_cards++;
    hand_to_play->suit_count[card_to_play->suit]++;
    for (int j = hand_to_play->num_cards - 1; j > valid_index[i]; j--)
      hand_to_play->cards[j] = hand_to_play->cards[j - 1];
    hand_to_play->cards[valid_index[i]] = card_to_play;

    // add cards back to global suits
    if (node->cards_in_trick == 0) {
      for (int j = NUM_SEATS - 1; j >= 0; j--) {
        Card* card = node->current_trick[j];
        if (card->prev)
          card->prev->next = card;
        else
          all_suits[card->suit] = card;
        if (card->next)
          card->next->prev = card;
      }
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
    if (is_ns(seat_to_play)) {
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
  if (best_card && depth < 4) {
    for (int j = 0; j < depth * 2; j ++) putchar(' ');
    printf("%c%c => %d\n",
           suit_name[best_card->suit], rank_name[best_card->rank],
           (is_ns(seat_to_play) ? min_tricks : max_tricks));
  }
  if (is_ns(seat_to_play))
    return min_tricks;
  else
    return max_tricks;
}

#endif

int char_to_suit(char c) {
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

int char_to_rank(char c) {
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

int char_to_seat(char c) {
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

bool card_used[NUM_SUITS][NUM_RANKS] = { false };

void parse_hand(char *line, Hand *hand) {
  hand->num_cards = 0;
  for (int suit = 0; suit < NUM_SUITS; suit++) {
    hand->suit_count[suit] = 0;
    while (line[0] && isspace(line[0])) line++;
    while (line[0] && !isspace(line[0]) && line[0] != '-') {
      int rank = char_to_rank(line[0]);

      if (card_used[suit][rank]) {
        printf("%c%c showed up twice.\n",
               suit_name[suit], rank_name[rank]);
        exit(-1);
      } else
        card_used[suit][rank] = true;

      hand->suit_count[suit]++;
      hand->cards[hand->num_cards] = &all_cards[suit][rank];
      hand->num_cards++;

      {
        Card* card = all_suits[suit];
        Card* prev_card = NULL;
        while (card) {
          if (card->rank < rank)
            break;
          prev_card = card;
          card = card->next;
        }
        card = &all_cards[suit][rank];
        card->prev = prev_card;
        if (prev_card) {
          card->next = prev_card->next;
          prev_card->next = card;
        } else {
          card->next = all_suits[suit];
          all_suits[suit] = card;
        }
        if (card->next)
          card->next->prev = card;
#if 0
        printf("After %c%c: ", suit_name[suit], rank_name[rank]);
        for (card = all_suits[suit]; card; card = card->next)
          printf("%c%c ", suit_name[card->suit], rank_name[card->rank]);
        printf("\n");
#endif
      }

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
}

void show_suits() {
  for (int suit = 0; suit < NUM_SUITS; suit++) {
    for (Card* card = all_suits[suit]; card; card = card->next)
      printf("%c%c ", suit_name[card->suit], rank_name[card->rank]);
    printf("\n");
  }
}

int main() {
  // setup deck
  for (int suit = 0; suit < NUM_SUITS; suit++) {
    for (int rank = 0; rank < NUM_RANKS; rank++) {
      all_cards[suit][rank].suit = suit;
      all_cards[suit][rank].rank = rank;
      all_cards[suit][rank].prev = NULL;
      all_cards[suit][rank].next = NULL;
    }
    all_suits[suit] = NULL;
  }

  // read hands
  char line[NUM_SEATS][80];
  CHECK(fgets(line[NORTH], sizeof(line[NORTH]), stdin));
  CHECK(fgets(line[WEST],  sizeof(line[WEST]),  stdin));
  CHECK(fgets(line[EAST],  sizeof(line[EAST]),  stdin));
  CHECK(fgets(line[SOUTH], sizeof(line[SOUTH]), stdin));

  Node node;
  int num_tricks;
  for (int seat = 0; seat < NUM_SEATS; seat ++) {
    parse_hand(line[seat], &node.hands[seat]);
    if (seat == 0)
      num_tricks = node.hands[seat].num_cards;
    else
      if (num_tricks != node.hands[seat].num_cards) {
        printf("%s has %d cards, while %s has %d.\n",
               seat_name[seat], node.hands[seat].num_cards,
               seat_name[0], num_tricks);
        exit(-1);
      }
  }

  CHECK(fgets(line[0], sizeof(line[0]), stdin));
  node.trump = char_to_suit(line[0][0]);
  CHECK(fgets(line[0], sizeof(line[0]), stdin));
  node.lead_seat = char_to_seat(line[0][0]);

  printf("Solving ...\n");
  node.cards_in_trick = 0;
  node.ns_tricks      = 0;
  int ns_tricks = min_max(&node, 0, num_tricks, is_ns(node.lead_seat));
  printf("NS tricks: %d      EW tricks: %d\n",
         ns_tricks, num_tricks - ns_tricks);
  return 0;
}
