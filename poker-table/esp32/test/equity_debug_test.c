/*
 * equity_debug_test.c — standalone console harness for equity_calculations.c
 *
 * This is NOT part of the Arduino build. It has its own main(), so it must
 * stay in this subfolder (test/), never directly alongside the .ino files —
 * the Arduino sketch compiler pulls in every .c/.cpp file that sits next to
 * a .ino, and a second main() there would break the firmware build.
 *
 * Build and run (from this folder):
 *   gcc -Wall -o equity_debug_test equity_debug_test.c ../equity_calculations.c
 *   ./equity_debug_test
 *
 * Simulates the same 3 events the real firmware recomputes equity on —
 * entering a seat's hole cards, adding a community card, and toggling a
 * seat's folded status — and after each one prints exactly what would be
 * pushed to that seat's 1602 LCD and the website: current hand name, total
 * win odds (equity_calculate(), unmodified from production), and the
 * straight/flush/full-house "or better" probabilities
 * (equity_calculate_category_odds()).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../equity_calculations.h"

#define ITERATIONS 1500

typedef struct {
  int enabled;
  int folded;
  int cardsKnown;
  EqCard hole[2];
} DebugPlayer;

static DebugPlayer players[EQ_MAX_PLAYERS];
static EqCard community[5];
static int numCommunity = 0;

/* Parses tokens like "10h", "Ah", "Kd", "2C" into an EqCard. Returns 1 on
 * success, 0 on a malformed token (caller should re-prompt). Mirrors
 * tokenToEqCard()/parseCardToken() elsewhere in this project — rank 'T'
 * is the internal representation of ten, not the two-character "10". */
static int parse_card_token(const char *token, EqCard *out) {
  size_t len = strlen(token);
  if (len < 2 || len > 3) return 0;

  char suitChar = (char)tolower((unsigned char)token[len - 1]);
  if (suitChar != 'c' && suitChar != 'd' && suitChar != 'h' && suitChar != 's') return 0;

  size_t rankLen = len - 1;
  char rank;
  if (rankLen == 2 && token[0] == '1' && token[1] == '0') {
    rank = 'T';
  } else if (rankLen == 1) {
    rank = (char)toupper((unsigned char)token[0]);
    if (strchr("23456789TJQKA", rank) == NULL) return 0;
  } else {
    return 0;
  }

  out->rank = rank;
  out->suit = suitChar;
  return 1;
}

static void print_card(EqCard c) {
  if (c.rank == 'T') printf("10%c", c.suit);
  else printf("%c%c", c.rank, c.suit);
}

static void read_line(char *buf, size_t bufSize) {
  if (fgets(buf, bufSize, stdin) == NULL) {
    buf[0] = '\0';
    return;
  }
  size_t len = strlen(buf);
  if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
}

/* Keeps prompting until parse_card_token() succeeds. */
static EqCard prompt_for_card(const char *label) {
  char line[16];
  EqCard card;
  for (;;) {
    printf("  %s: ", label);
    read_line(line, sizeof(line));
    if (parse_card_token(line, &card)) return card;
    printf("  Not a valid card (try e.g. 10h, Ah, Kd, 2c) — try again.\n");
  }
}

static void print_dashboard(void) {
  printf("\n================ EQUITY DASHBOARD ================\n");
  printf("Community (%d): ", numCommunity);
  for (int i = 0; i < numCommunity; i++) {
    print_card(community[i]);
    printf(" ");
  }
  printf("\n");

  EqPlayer eqPlayers[EQ_MAX_PLAYERS];
  for (int i = 0; i < EQ_MAX_PLAYERS; i++) {
    eqPlayers[i].seat = i + 1;
    eqPlayers[i].folded = players[i].folded;
    eqPlayers[i].cardsKnown = players[i].cardsKnown;
    eqPlayers[i].hole[0] = players[i].hole[0];
    eqPlayers[i].hole[1] = players[i].hole[1];
  }

  /* Same call computeAllWinOdds() makes — totally unmodified from what's
   * flashed to the main board. */
  float winPct[EQ_MAX_PLAYERS];
  equity_calculate(eqPlayers, EQ_MAX_PLAYERS, community, numCommunity, ITERATIONS, winPct);

  for (int i = 0; i < EQ_MAX_PLAYERS; i++) {
    if (!players[i].enabled) continue;

    printf("\nSeat %d: ", i + 1);
    if (!players[i].cardsKnown) {
      printf("(no cards yet)\n");
      continue;
    }

    print_card(players[i].hole[0]);
    printf(" ");
    print_card(players[i].hole[1]);

    if (players[i].folded) {
      printf("  [FOLDED]\n");
      continue;
    }
    printf("\n");

    /* Current hand name — same call computeAllHandNames() makes. */
    EqCard cards[7];
    cards[0] = players[i].hole[0];
    cards[1] = players[i].hole[1];
    for (int c = 0; c < numCommunity; c++) cards[2 + c] = community[c];
    HandCategory category = equity_hand_category(cards, 2 + numCommunity);
    printf("  Hand:   %s\n", equity_hand_name(category));
    printf("  Equity: %.1f%%\n", winPct[i]);

    /* Same call computeAllCategoryOdds() makes for this seat. */
    float straightPct, flushPct, fullHousePct;
    equity_calculate_category_odds(eqPlayers, EQ_MAX_PLAYERS, community, numCommunity,
                                    i, ITERATIONS, &straightPct, &flushPct, &fullHousePct);
    printf("  STR: %.1f%%  FL: %.1f%%  FH: %.1f%%\n", straightPct, flushPct, fullHousePct);
  }
  printf("===================================================\n");
}

int main(void) {
  memset(players, 0, sizeof(players));

  printf("Equity engine debug console\n");
  printf("Card format: rank+suit, e.g. 10h, Ah, Kd, 2c (ten is \"10\", not \"T\")\n\n");

  for (int i = 0; i < EQ_MAX_PLAYERS; i++) {
    char line[16];
    printf("Enable seat %d? (y/n): ", i + 1);
    read_line(line, sizeof(line));
    if (line[0] != 'y' && line[0] != 'Y') continue;

    players[i].enabled = 1;

    char label[24];
    snprintf(label, sizeof(label), "Seat %d card 1", i + 1);
    players[i].hole[0] = prompt_for_card(label);
    snprintf(label, sizeof(label), "Seat %d card 2", i + 1);
    players[i].hole[1] = prompt_for_card(label);

    players[i].cardsKnown = 1;
  }

  print_dashboard();

  printf("\nCommands:\n");
  printf("  c        add the next community card\n");
  printf("  f <seat> toggle fold for a seat (e.g. \"f 2\")\n");
  printf("  p        reprint the dashboard\n");
  printf("  q        quit\n");

  char line[16];
  for (;;) {
    printf("\n> ");
    read_line(line, sizeof(line));
    if (line[0] == '\0') continue;

    if (line[0] == 'q') {
      break;
    } else if (line[0] == 'c') {
      if (numCommunity >= 5) {
        printf("Already have 5 community cards.\n");
        continue;
      }
      char label[24];
      snprintf(label, sizeof(label), "Community card %d", numCommunity + 1);
      community[numCommunity++] = prompt_for_card(label);
      print_dashboard();
    } else if (line[0] == 'f') {
      int seat = atoi(line + 1);
      if (seat < 1 || seat > EQ_MAX_PLAYERS || !players[seat - 1].enabled) {
        printf("Invalid seat.\n");
        continue;
      }
      players[seat - 1].folded = !players[seat - 1].folded;
      printf("Seat %d folded = %s\n", seat, players[seat - 1].folded ? "true" : "false");
      print_dashboard();
    } else if (line[0] == 'p') {
      print_dashboard();
    } else {
      printf("Unknown command.\n");
    }
  }

  return 0;
}
