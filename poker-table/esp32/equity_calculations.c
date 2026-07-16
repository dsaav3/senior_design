/*
 * equity_calculations.c — Texas Hold'em win-probability (equity) engine.
 *
 * Plain C, no Arduino dependencies. See equity_calculations.h for the
 * public interface and the reasoning behind the C/C++ split.
 *
 * Approach: hole cards for all live (non-folded) players are known exactly
 * (this table shows all hands), so this is NOT a "hero vs random ranges"
 * simulation — it only needs to simulate the community cards not yet
 * dealt. For each Monte Carlo trial: fill in the missing board cards by
 * sampling without replacement from the remaining deck, evaluate every
 * live player's best 5-card hand out of their 2 hole + 5 board cards, and
 * tally the winner(s) (splitting ties). Repeated over many trials this
 * converges to each seat's true win probability given the known hands and
 * however much of the board is still random.
 *
 * The 5-card hand evaluator uses a straightforward category + tiebreaker
 * scoring scheme (no lookup tables) — simple to verify correct, and cheap
 * enough at these iteration counts for an ESP32.
 */

#include "equity_calculations.h"
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------
 * Card helpers
 * ------------------------------------------------------------------- */

static int rank_value(char r) {
  switch (r) {
    case '2': return 2;
    case '3': return 3;
    case '4': return 4;
    case '5': return 5;
    case '6': return 6;
    case '7': return 7;
    case '8': return 8;
    case '9': return 9;
    case 'T': return 10;
    case 'J': return 11;
    case 'Q': return 12;
    case 'K': return 13;
    case 'A': return 14;
    default:  return 0;
  }
}

static int suit_index(char s) {
  switch (s) {
    case 'c': return 0;
    case 'd': return 1;
    case 'h': return 2;
    case 's': return 3;
    default:  return -1;
  }
}

/* ---------------------------------------------------------------------
 * 5-card hand ranking.
 *
 * Returns a single comparable score (higher = better hand). Encoded as
 * category (0-8) followed by up to 5 tiebreaker ranks packed in base 15
 * (ranks only go up to 14, so base 15 keeps each digit unambiguous).
 * ------------------------------------------------------------------- */

static long rank5(const EqCard hand[5]) {
  int rankCounts[15] = {0};
  int suitCounts[4] = {0};
  int rankPresent = 0; /* bitmask of ranks 2-14 present in this 5-card hand */

  for (int i = 0; i < 5; i++) {
    int rv = rank_value(hand[i].rank);
    int si = suit_index(hand[i].suit);
    rankCounts[rv]++;
    if (si >= 0) suitCounts[si]++;
    rankPresent |= (1 << rv);
  }

  int isFlush = 0;
  for (int s = 0; s < 4; s++) {
    if (suitCounts[s] == 5) { isFlush = 1; break; }
  }

  /* Straight detection, including the wheel (A-2-3-4-5, ace plays low). */
  int straightHigh = 0;
  int wheelMask = (1 << 14) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5);
  if ((rankPresent & wheelMask) == wheelMask) {
    straightHigh = 5;
  }
  for (int high = 14; high >= 6; high--) {
    int mask = 0;
    for (int k = 0; k < 5; k++) mask |= (1 << (high - k));
    if ((rankPresent & mask) == mask) { straightHigh = high; break; }
  }

  /* Group the (up to 5) distinct ranks present, sorted by (count desc, rank desc). */
  int items[5][2];
  int nItems = 0;
  for (int r = 14; r >= 2; r--) {
    if (rankCounts[r] > 0) {
      items[nItems][0] = r;
      items[nItems][1] = rankCounts[r];
      nItems++;
    }
  }
  for (int i = 1; i < nItems; i++) {
    int rr = items[i][0], cc = items[i][1];
    int j = i - 1;
    while (j >= 0 && (items[j][1] < cc || (items[j][1] == cc && items[j][0] < rr))) {
      items[j + 1][0] = items[j][0];
      items[j + 1][1] = items[j][1];
      j--;
    }
    items[j + 1][0] = rr;
    items[j + 1][1] = cc;
  }

  int category;
  int tie[5] = {0, 0, 0, 0, 0};

  if (isFlush && straightHigh) {
    category = 8; /* straight flush */
    tie[0] = straightHigh;
  } else if (items[0][1] == 4) {
    category = 7; /* four of a kind */
    tie[0] = items[0][0];
    tie[1] = items[1][0];
  } else if (items[0][1] == 3 && items[1][1] == 2) {
    category = 6; /* full house */
    tie[0] = items[0][0];
    tie[1] = items[1][0];
  } else if (isFlush) {
    category = 5;
    for (int i = 0; i < 5; i++) tie[i] = items[i][0];
  } else if (straightHigh) {
    category = 4;
    tie[0] = straightHigh;
  } else if (items[0][1] == 3) {
    category = 3; /* trips */
    tie[0] = items[0][0];
    tie[1] = items[1][0];
    tie[2] = items[2][0];
  } else if (items[0][1] == 2 && items[1][1] == 2) {
    category = 2; /* two pair */
    tie[0] = items[0][0];
    tie[1] = items[1][0];
    tie[2] = items[2][0];
  } else if (items[0][1] == 2) {
    category = 1; /* one pair */
    tie[0] = items[0][0];
    tie[1] = items[1][0];
    tie[2] = items[2][0];
    tie[3] = items[3][0];
  } else {
    category = 0; /* high card */
    for (int i = 0; i < 5; i++) tie[i] = items[i][0];
  }

  long score = category;
  for (int i = 0; i < 5; i++) {
    score = score * 15 + tie[i];
  }
  return score;
}

/* Best 5-card hand out of n (5, 6, or 7) cards, via combination enumeration
 * (at most C(7,5) = 21 combinations — cheap, and much simpler to get right
 * than an optimized single-pass 7-card evaluator). */
static long best_hand_score(const EqCard cards[], int n) {
  if (n == 5) return rank5(cards);

  long best = -1;
  int idx[5];
  for (idx[0] = 0; idx[0] < n - 4; idx[0]++)
  for (idx[1] = idx[0] + 1; idx[1] < n - 3; idx[1]++)
  for (idx[2] = idx[1] + 1; idx[2] < n - 2; idx[2]++)
  for (idx[3] = idx[2] + 1; idx[3] < n - 1; idx[3]++)
  for (idx[4] = idx[3] + 1; idx[4] < n; idx[4]++) {
    EqCard hand[5];
    for (int k = 0; k < 5; k++) hand[k] = cards[idx[k]];
    long s = rank5(hand);
    if (s > best) best = s;
  }
  return best;
}

/* ---------------------------------------------------------------------
 * Deck helpers
 * ------------------------------------------------------------------- */

static void build_remaining_deck(const EqPlayer *players, int numPlayers,
                                  const EqCard *community, int numCommunity,
                                  EqCard *deckOut, int *deckCountOut) {
  static const char RANKS[13] = {'2','3','4','5','6','7','8','9','T','J','Q','K','A'};
  static const char SUITS[4] = {'c','d','h','s'};

  EqCard used[52];
  int usedCount = 0;

  for (int p = 0; p < numPlayers; p++) {
    if (players[p].folded || !players[p].cardsKnown) continue;
    used[usedCount++] = players[p].hole[0];
    used[usedCount++] = players[p].hole[1];
  }
  for (int i = 0; i < numCommunity; i++) {
    used[usedCount++] = community[i];
  }

  int count = 0;
  for (int r = 0; r < 13; r++) {
    for (int s = 0; s < 4; s++) {
      EqCard c = { RANKS[r], SUITS[s] };
      int isUsed = 0;
      for (int u = 0; u < usedCount; u++) {
        if (used[u].rank == c.rank && used[u].suit == c.suit) { isUsed = 1; break; }
      }
      if (!isUsed) deckOut[count++] = c;
    }
  }
  *deckCountOut = count;
}

/* ---------------------------------------------------------------------
 * Public entry point
 * ------------------------------------------------------------------- */

void equity_calculate(const EqPlayer *players, int numPlayers,
                       const EqCard *community, int numCommunity,
                       int iterations, float *winPct) {
  for (int i = 0; i < numPlayers; i++) winPct[i] = 0.0f;

  int liveIdx[EQ_MAX_PLAYERS];
  int numLive = 0;
  for (int i = 0; i < numPlayers && i < EQ_MAX_PLAYERS; i++) {
    if (!players[i].folded && players[i].cardsKnown) {
      liveIdx[numLive++] = i;
    }
  }
  if (numLive == 0) return;
  if (numLive == 1) {
    winPct[liveIdx[0]] = 100.0f;
    return;
  }

  EqCard deck[52];
  int deckCount = 0;
  build_remaining_deck(players, numPlayers, community, numCommunity, deck, &deckCount);

  int needed = 5 - numCommunity;
  if (needed < 0) needed = 0;
  if (needed > deckCount) needed = deckCount; /* defensive; shouldn't happen */

  float winTally[EQ_MAX_PLAYERS] = {0, 0, 0, 0};
  EqCard board[5];
  EqCard sampleDeck[52];

  for (int trial = 0; trial < iterations; trial++) {
    memcpy(sampleDeck, deck, sizeof(EqCard) * deckCount);
    int pool = deckCount;

    for (int i = 0; i < numCommunity; i++) board[i] = community[i];

    for (int i = 0; i < needed; i++) {
      int pick = rand() % pool;
      board[numCommunity + i] = sampleDeck[pick];
      sampleDeck[pick] = sampleDeck[pool - 1];
      pool--;
    }

    long bestScore = -1;
    int winners[EQ_MAX_PLAYERS];
    int numWinners = 0;

    for (int li = 0; li < numLive; li++) {
      int p = liveIdx[li];
      EqCard sevenCards[7];
      sevenCards[0] = players[p].hole[0];
      sevenCards[1] = players[p].hole[1];
      for (int b = 0; b < 5; b++) sevenCards[2 + b] = board[b];

      long score = best_hand_score(sevenCards, 7);
      if (score > bestScore) {
        bestScore = score;
        winners[0] = p;
        numWinners = 1;
      } else if (score == bestScore) {
        winners[numWinners++] = p;
      }
    }

    float share = 1.0f / numWinners;
    for (int w = 0; w < numWinners; w++) {
      winTally[winners[w]] += share;
    }
  }

  for (int i = 0; i < numPlayers; i++) {
    winPct[i] = (winTally[i] / iterations) * 100.0f;
  }
}

/* ---------------------------------------------------------------------
 * Per-category hit-probability for a single seat (straight/flush/full
 * house) — a completely independent Monte Carlo pass from
 * equity_calculate() above; does not read or write anything it touches.
 * ------------------------------------------------------------------- */

void equity_calculate_category_odds(const EqPlayer *players, int numPlayers,
                                     const EqCard *community, int numCommunity,
                                     int targetIndex, int iterations,
                                     float *straightPct, float *flushPct,
                                     float *fullHousePct) {
  *straightPct = 0.0f;
  *flushPct = 0.0f;
  *fullHousePct = 0.0f;

  if (targetIndex < 0 || targetIndex >= numPlayers || targetIndex >= EQ_MAX_PLAYERS) return;
  if (players[targetIndex].folded || !players[targetIndex].cardsKnown) return;
  if (iterations <= 0) return;

  EqCard deck[52];
  int deckCount = 0;
  build_remaining_deck(players, numPlayers, community, numCommunity, deck, &deckCount);

  int needed = 5 - numCommunity;
  if (needed < 0) needed = 0;
  if (needed > deckCount) needed = deckCount; /* defensive; shouldn't happen */

  int straightCount = 0, flushCount = 0, fullHouseCount = 0;
  EqCard board[5];
  EqCard sampleDeck[52];

  for (int trial = 0; trial < iterations; trial++) {
    memcpy(sampleDeck, deck, sizeof(EqCard) * deckCount);
    int pool = deckCount;

    for (int i = 0; i < numCommunity; i++) board[i] = community[i];

    for (int i = 0; i < needed; i++) {
      int pick = rand() % pool;
      board[numCommunity + i] = sampleDeck[pick];
      sampleDeck[pick] = sampleDeck[pool - 1];
      pool--;
    }

    EqCard sevenCards[7];
    sevenCards[0] = players[targetIndex].hole[0];
    sevenCards[1] = players[targetIndex].hole[1];
    for (int b = 0; b < 5; b++) sevenCards[2 + b] = board[b];

    HandCategory category = equity_hand_category(sevenCards, 7);

    /* "Or better" containment: a straight flush / royal flush IS
     * structurally a straight AND a flush at once, so it counts toward
     * both. Full house has nothing above it that contains it, so that one
     * stays an exact match. */
    int hasStraight = (category == HAND_STRAIGHT ||
                        category == HAND_STRAIGHT_FLUSH ||
                        category == HAND_ROYAL_FLUSH);
    int hasFlush = (category == HAND_FLUSH ||
                     category == HAND_STRAIGHT_FLUSH ||
                     category == HAND_ROYAL_FLUSH);
    int hasFullHouse = (category == HAND_FULL_HOUSE);

    if (hasStraight) straightCount++;
    if (hasFlush) flushCount++;
    if (hasFullHouse) fullHouseCount++;
  }

  *straightPct = ((float)straightCount / iterations) * 100.0f;
  *flushPct = ((float)flushCount / iterations) * 100.0f;
  *fullHousePct = ((float)fullHouseCount / iterations) * 100.0f;
}

/* ---------------------------------------------------------------------
 * Hand category (for display, e.g. "Two Pair") — reuses the same rank5 /
 * best_hand_score scoring used for equity, just decoded back into a
 * category instead of compared as a raw number.
 *
 * rank5()'s score is packed as: category * 15^5 + tie0*15^4 + tie1*15^3 +
 * tie2*15^2 + tie3*15 + tie4 (see rank5() above), so both the category and
 * the top tiebreaker (needed to tell a royal flush from any other
 * straight flush) can be recovered with plain integer division.
 * ------------------------------------------------------------------- */

#define SCORE_CATEGORY_DIVISOR 759375L /* 15^5 */
#define SCORE_TIE0_DIVISOR 50625L      /* 15^4 */

HandCategory equity_hand_category(const EqCard *cards, int numCards) {
  if (numCards < 5) {
    int rankCounts[15] = {0};
    for (int i = 0; i < numCards; i++) {
      rankCounts[rank_value(cards[i].rank)]++;
    }
    for (int r = 2; r <= 14; r++) {
      if (rankCounts[r] >= 2) return HAND_PAIR;
    }
    return HAND_HIGH_CARD;
  }

  long score = best_hand_score(cards, numCards);
  long category = score / SCORE_CATEGORY_DIVISOR;
  long tie0 = (score % SCORE_CATEGORY_DIVISOR) / SCORE_TIE0_DIVISOR;

  if (category == HAND_STRAIGHT_FLUSH && tie0 == 14) {
    return HAND_ROYAL_FLUSH;
  }
  return (HandCategory)category;
}

const char *equity_hand_name(HandCategory category) {
  switch (category) {
    case HAND_HIGH_CARD:        return "High Card";
    case HAND_PAIR:              return "Pair";
    case HAND_TWO_PAIR:          return "Two Pair";
    case HAND_THREE_OF_A_KIND:   return "Three of a Kind";
    case HAND_STRAIGHT:          return "Straight";
    case HAND_FLUSH:             return "Flush";
    case HAND_FULL_HOUSE:        return "Full House";
    case HAND_FOUR_OF_A_KIND:    return "Four of a Kind";
    case HAND_STRAIGHT_FLUSH:    return "Straight Flush";
    case HAND_ROYAL_FLUSH:       return "Royal Flush";
    default:                     return "Unknown";
  }
}
