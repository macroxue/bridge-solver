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

#define CARDS_PER_HAND    13
#define NUM_SEATS          4
#define NUM_SUITS          4
#define NUM_RANKS         13

#define IS_NS(seat)       ((seat) % 2)

typedef enum { SPADE, HEART, DIAMOND, CLUB, NOTRUMP } suit_t;
typedef enum { TWO, THREE, FOUR, FIVE, SIX, SEVEN, EIGHT, NINE, 
               TEN, JACK, QUEEN, KING, ACE } rank_t;
typedef enum { MIN, MAX } min_max_t;
typedef enum { WEST, NORTH, EAST, SOUTH } seat_t;
typedef enum { false, true } bool;


typedef struct card {
    suit_t     suit;
    rank_t     rank;
    struct card *prev, *next;
} *card_t;

typedef struct hand {
    int        num_cards;
    card_t     cards[CARDS_PER_HAND];
    int        suit_count[NUM_SUITS];
} hand_t;

typedef struct node {
    suit_t     trump;
    int        NS_tricks;
    seat_t     lead_seat;
    suit_t     lead_suit;
    int        cards_in_trick;
    card_t     winning_card;
    seat_t     winning_seat;
    hand_t     hands[NUM_SEATS];
    card_t     current_trick[NUM_SEATS];
} node_t;

char suit_name[] = "SHDC";
char rank_name[] = "23456789TJQKA";
char *seat_name[] = { "West", "North", "East", "South" };

struct card _Card[NUM_SUITS][NUM_RANKS];
card_t _Suit[NUM_SUITS];

#if 1

#define WIN_OVER(p, w, t) \
    (((p)->suit == (w)->suit && (p)->rank > (w)->rank) \
               || ((p)->suit != (w)->suit && (p)->suit == (t)))

int gen_valid_cards(hand_t *hand, suit_t lead_suit, 
                    card_t valid_cards[], int valid_index[],
                    card_t winning_card, bool partner)
{
    suit_t suit;
    int    i, begin_index = 0;
    int    suit_count = hand->suit_count[lead_suit];
    int    num_valid_cards = 0;

    assert(suit_count > 0);

    for (suit = 0; suit < lead_suit; suit++)
        begin_index += hand->suit_count[suit];

    // big card first
    for (i = 0; i < suit_count; ) {
        card_t card;

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
        for (i = 0; i < num_valid_cards/2; i++) {
            card_t  temp_card;
            int     temp_index;

            temp_card = valid_cards[i];
            valid_cards[i] = valid_cards[num_valid_cards - 1 - i];
            valid_cards[num_valid_cards - 1 - i] = temp_card;

            temp_index = valid_index[i];
            valid_index[i] = valid_index[num_valid_cards - 1 - i];
            valid_index[num_valid_cards - 1 - i] = temp_index;
        }
    }
    return num_valid_cards;
}

int depth = 0;

int min_max(node_t *node, int min_tricks, int max_tricks, int is_parent_NS) 
{
    seat_t     seat_to_play;
    card_t     valid_cards[CARDS_PER_HAND];
    int        valid_index[CARDS_PER_HAND];
    hand_t     *hand_to_play;
    int        num_valid_cards;
    int        i, j;
    card_t     best_card;

    seat_to_play = (node->lead_seat + node->cards_in_trick) % NUM_SEATS;
    hand_to_play = &node->hands[seat_to_play];
#if 0
    if (hand_to_play->num_cards == 0) {
        return node->NS_tricks;
    }
#else
    if (hand_to_play->num_cards == 1 && node->cards_in_trick == 0) {
        seat_t  winning_seat = seat_to_play;
        card_t  winning_card = hand_to_play->cards[0];
        for (i = 1; i < NUM_SEATS; i++) {
            seat_t seat = (seat_to_play + i) % NUM_SEATS;
            if (WIN_OVER(node->hands[seat].cards[0], winning_card,
                         node->trump)) {
                winning_card = node->hands[seat].cards[0];
                winning_seat = seat;
            }
        }
        return node->NS_tricks + IS_NS(winning_seat);
    }
#endif

    if (node->cards_in_trick == 0 
            || hand_to_play->suit_count[node->lead_suit] == 0) {
        num_valid_cards = 0;
        for (i = 0; i < hand_to_play->num_cards; ) {
            card_t  card;

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
                                          IS_NS(seat_to_play) ==
                                          IS_NS(node->winning_seat));
    }

    best_card = NULL;
    for (i = 0; i < num_valid_cards; i++) {
        int        prev_NS_tricks      = node->NS_tricks;
        seat_t     prev_lead_seat      = node->lead_seat;
        suit_t     prev_lead_suit      = node->lead_suit;
        int        prev_cards_in_trick = node->cards_in_trick;
        card_t     prev_winning_card   = node->winning_card;
        seat_t     prev_winning_seat   = node->winning_seat;
        card_t     prev_card = node->current_trick[node->cards_in_trick];

        card_t     card_to_play = valid_cards[i];
        int        num_tricks;
        bool       good_play;

        node->current_trick[node->cards_in_trick] = card_to_play;
        node->cards_in_trick ++;

        if (node->cards_in_trick == 1)
            node->lead_suit = card_to_play->suit;

        // who's winning?
        if (node->cards_in_trick == 1 
                || WIN_OVER(card_to_play, node->winning_card, node->trump)) {
            node->winning_card = card_to_play;
            node->winning_seat = seat_to_play;
        } 

        // who won?
        if (node->cards_in_trick == NUM_SEATS) {
            node->lead_seat = node->winning_seat;
            node->NS_tricks += IS_NS(node->winning_seat);
            node->cards_in_trick = 0;

            // remove cards from global suits
            for (j = 0; j < NUM_SEATS; j++) {
                card_t card = node->current_trick[j];
                if (card->prev)
                    card->prev->next = card->next;
                else
                    _Suit[card->suit] = card->next;
                if (card->next) 
                    card->next->prev = card->prev;
            }
        }

        // remove played card from hand
        for (j = valid_index[i]; j < hand_to_play->num_cards - 1; j++) 
            hand_to_play->cards[j] = hand_to_play->cards[j + 1];
        hand_to_play->suit_count[card_to_play->suit]--;
        hand_to_play->num_cards--;

        // recursive search
        depth ++;
        num_tricks = min_max(node, min_tricks, max_tricks, IS_NS(seat_to_play)); 
        depth --;

        // add played card back to hand
        hand_to_play->num_cards++;
        hand_to_play->suit_count[card_to_play->suit]++;
        for (j = hand_to_play->num_cards - 1; j > valid_index[i]; j--) 
            hand_to_play->cards[j] = hand_to_play->cards[j - 1];
        hand_to_play->cards[j] = card_to_play;

        // add cards back to global suits
        if (node->cards_in_trick == 0) {
            for (j = NUM_SEATS - 1; j >= 0; j--) {
                card_t card = node->current_trick[j];
                if (card->prev) 
                    card->prev->next = card;
                else
                    _Suit[card->suit] = card;
                if (card->next)
                    card->next->prev = card;
            }
        }

        // restore state
        node->NS_tricks      = prev_NS_tricks;
        node->lead_seat      = prev_lead_seat;
        node->lead_suit      = prev_lead_suit;
        node->cards_in_trick = prev_cards_in_trick;
        node->winning_card   = prev_winning_card;
        node->winning_seat   = prev_winning_seat;
        node->current_trick[node->cards_in_trick] = prev_card;

        // possible pruning
        if (IS_NS(seat_to_play)) {
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
            good_play = (max_tricks > num_tricks);
            if (max_tricks > num_tricks) {
                max_tricks = num_tricks;
                best_card  = card_to_play;
            }
        }
    }
    if (best_card && depth < 4) {
        for (j = 0; j < depth * 2; j ++) putchar(' ');
        printf("%c%c => %d\n", 
                suit_name[best_card->suit], rank_name[best_card->rank],
                (IS_NS(seat_to_play) ? min_tricks : max_tricks));
    }
    if (IS_NS(seat_to_play))
        return min_tricks;
    else
        return max_tricks;
}

#endif

suit_t char_to_suit(char c)
{
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

rank_t char_to_rank(char c)
{
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

seat_t char_to_seat(char c)
{
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

void parse_hand(char *line, hand_t *hand)
{
    suit_t suit;

    hand->num_cards = 0;
    for (suit = 0; suit < NUM_SUITS; suit++) {
        hand->suit_count[suit] = 0;
        while (line[0] && isspace(line[0])) line++;
        while (line[0] && !isspace(line[0]) && line[0] != '-') {
            rank_t  rank = char_to_rank(line[0]);

            if (card_used[suit][rank]) {
                printf("%c%c showed up twice.\n",
                        suit_name[suit], rank_name[rank]);
                exit(-1);
            } else
                card_used[suit][rank] = true;

            hand->suit_count[suit]++;
            hand->cards[hand->num_cards] = &_Card[suit][rank];
            hand->num_cards++;

            {
                card_t card = _Suit[suit], prev_card = NULL;
                while (card) {
                    if (card->rank < rank) 
                        break;
                    prev_card = card;
                    card = card->next;
                }
                card = &_Card[suit][rank];
                card->prev = prev_card;
                if (prev_card) {
                    card->next = prev_card->next;
                    prev_card->next = card;
                } else {
                    card->next = _Suit[suit];
                    _Suit[suit] = card;
                }
                if (card->next) 
                    card->next->prev = card;
#if 0
                printf("After %c%c: ", suit_name[suit], rank_name[rank]);
                for (card = _Suit[suit]; card; card = card->next) 
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

show_suits()
{
    suit_t  suit;
    card_t  card;

    for (suit = 0; suit < NUM_SUITS; suit++) {
        for (card = _Suit[suit]; card; card = card->next) 
            printf("%c%c ", suit_name[card->suit], rank_name[card->rank]);
        printf("\n");
    }
}

main()
{
    node_t n;
    int    num_tricks, NS_tricks;
    char   line[NUM_SEATS][80];
    seat_t seat;
    suit_t suit;
    rank_t rank;
    
    // setup deck
    for (suit = 0; suit < NUM_SUITS; suit++) {
        for (rank = 0; rank < NUM_RANKS; rank++) {
            _Card[suit][rank].suit = suit;
            _Card[suit][rank].rank = rank;
            _Card[suit][rank].prev = NULL;
            _Card[suit][rank].next = NULL;
        }
        _Suit[suit] = NULL;
    }

    // read hands
    fgets(line[NORTH], sizeof(line[NORTH]), stdin);
    fgets(line[WEST],  sizeof(line[WEST]),  stdin);
    fgets(line[EAST],  sizeof(line[EAST]),  stdin);
    fgets(line[SOUTH], sizeof(line[SOUTH]), stdin);

    for (seat = 0; seat < NUM_SEATS; seat ++) {
        parse_hand(line[seat], &n.hands[seat]);
        if (seat == 0)
            num_tricks = n.hands[seat].num_cards;
        else
            if (num_tricks != n.hands[seat].num_cards) {
                printf("%s has %d cards, while %s has %d.\n",
                        seat_name[seat], n.hands[seat].num_cards, 
                        seat_name[0], num_tricks);
                exit(-1);
            }
    }

    fgets(line[0], sizeof(line[0]), stdin);
    n.trump = char_to_suit(line[0][0]);
    fgets(line[0], sizeof(line[0]), stdin);
    n.lead_seat = char_to_seat(line[0][0]);

    printf("Solving ...\n");
    n.cards_in_trick = 0;
    n.NS_tricks      = 0;
    NS_tricks = min_max(&n, 0, num_tricks, IS_NS(n.lead_seat));
    printf("NS tricks: %d      EW tricks: %d\n", 
            NS_tricks, num_tricks - NS_tricks);
}
