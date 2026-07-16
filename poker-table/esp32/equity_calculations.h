/*
 * equity_calculations.h — Texas Hold'em win-probability (equity) engine.
 *
 * Plain C interface so this can be compiled as a .c file alongside the
 * poker_table.ino sketch (which is C++). Arduino's String/Serial/etc. are
 * C++-only and must never appear in this header or its .c implementation —
 * convert to the plain types below on the .ino side before calling in.
 */

#ifndef EQUITY_CALCULATIONS_H
#define EQUITY_CALCULATIONS_H

#ifdef __cplusplus
extern "C" {
#endif

#define EQ_MAX_PLAYERS 4

/*
 * A single card. rank is one of '2'..'9','T','J','Q','K','A' (use 'T' for
 * ten, NOT the two-character "10" — convert on the caller side).
 * suit is one of 'c','d','h','s'.
 */
typedef struct {
  char rank;
  char suit;
} EqCard;

typedef struct {
  int seat;         /* 1-4, informational only */
  EqCard hole[2];
  int folded;       /* 1 = excluded from equity (folded this hand) */
  int cardsKnown;   /* 1 = hole cards have been read for this seat */
} EqPlayer;

/*
 * Computes each seat's win probability (0.0-100.0, ties split
 * fractionally) via Monte Carlo simulation of the remaining community
 * cards. Hole cards for all live players are assumed KNOWN (this table
 * shows all hands to spectators) — the only randomness simulated is the
 * board cards not yet dealt.
 *
 * players/numPlayers: up to EQ_MAX_PLAYERS seats.
 * community/numCommunity: the community cards already dealt (0-5 of them).
 * iterations: Monte Carlo trial count (a few thousand is a reasonable
 *   accuracy/speed tradeoff on an ESP32).
 * winPct: caller-provided array of length numPlayers, receives results.
 *   Folded or not-yet-dealt seats always receive 0.
 */
void equity_calculate(const EqPlayer *players, int numPlayers,
                       const EqCard *community, int numCommunity,
                       int iterations, float *winPct);

/*
 * Computes, for ONE seat (targetIndex, 0-based into players[]), the
 * probability (0.0-100.0) that their FINAL best 5-card hand (hole cards +
 * all 5 community cards) is a straight/flush/full house "OR BETTER" —
 * i.e. structural containment, not exact category matching. A made
 * straight flush or royal flush counts toward BOTH straightPct and
 * flushPct (it structurally IS a straight and a flush at once), since
 * "probability of hitting a flush" means "will I end up with a flush or
 * better", not "will my final hand be a flush and nothing more". Full
 * house has no higher category that contains it, so fullHousePct is an
 * exact match. (An earlier version used exact matching for all three,
 * which meant a made royal flush showed 0/0/0 here — technically
 * "correct" under strict category equality, but not what a player means
 * by the question, and confusing next to a hand that clearly beats all
 * three.)
 *
 * This runs its own independent Monte Carlo simulation of the remaining
 * board cards — completely separate from equity_calculate() above, which
 * it does not call or share state with. Other live players' hole cards are
 * still excluded from the simulated deck (they're known real cards, not
 * available to be dealt as board cards), same as equity_calculate() does.
 *
 * If the target seat is folded, not yet known, or targetIndex is out of
 * range, all three outputs are set to 0.0.
 */
void equity_calculate_category_odds(const EqPlayer *players, int numPlayers,
                                     const EqCard *community, int numCommunity,
                                     int targetIndex, int iterations,
                                     float *straightPct, float *flushPct,
                                     float *fullHousePct);

/*
 * Hand categories, ordered low-to-high (numerically comparable).
 */
typedef enum {
  HAND_HIGH_CARD = 0,
  HAND_PAIR = 1,
  HAND_TWO_PAIR = 2,
  HAND_THREE_OF_A_KIND = 3,
  HAND_STRAIGHT = 4,
  HAND_FLUSH = 5,
  HAND_FULL_HOUSE = 6,
  HAND_FOUR_OF_A_KIND = 7,
  HAND_STRAIGHT_FLUSH = 8,
  HAND_ROYAL_FLUSH = 9
} HandCategory;

/*
 * Returns the category of the best hand made from 2-7 cards (hole cards
 * plus however many community cards have been dealt so far). With fewer
 * than 5 cards (pre-flop), only HAND_PAIR or HAND_HIGH_CARD can result —
 * there aren't enough cards for any higher category to be possible.
 */
HandCategory equity_hand_category(const EqCard *cards, int numCards);

/* Static human-readable name for a category, e.g. "Two Pair". */
const char *equity_hand_name(HandCategory category);

#ifdef __cplusplus
}
#endif

#endif /* EQUITY_CALCULATIONS_H */
