/*
 * ESP32-WROOM-32: PN5180 Multi-Reader + Backend Sync
 *
 * Hardware Connections:
 * - SPI Bus: SCK = GPIO18, MISO = GPIO19, MOSI = GPIO23
 * - Flop:  NSS = GPIO5,  BUSY = GPIO25
 * - Turn:  NSS = GPIO4,  BUSY = GPIO26
 * - River: NSS = GPIO14, BUSY = GPIO27
 * - Shared Reset: RST = GPIO17
 * - LCD I2C: SDA = GPIO21, SCL = GPIO22
 *
 * Start with TEST_ONE_READER_ONLY = 1.
 * Once Turn reader works, change it to 0 to scan all three readers.
 *
 * Dependencies (install via Arduino Library Manager):
 *  - ArduinoJson (by Benoit Blanchon) >= 7.x
 *  - HTTPClient (built into ESP32 Arduino core)
 *
 * Backend sync:
 *  - This board discovers the active session and the dealer-controlled
 *    game phase by polling GET /api/esp32/state on the backend. There is
 *    only ever one active table/session, so no session code needs to be
 *    entered on this board.
 *  - Player hole cards, community cards, and win odds are pushed to the
 *    backend with POST /api/esp32/update.
 *  - When the polled phase transitions back to "pre-flop" (dealer clicked
 *    "Start Next Hand" on the website), this board automatically resets
 *    its local round data and sends RESET to the player boards.
 */

#include <SPI.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <esp_now.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ============================================================
// Debug mode
// ============================================================
#define TEST_ONE_READER_ONLY 1   // 1 = test Turn only, 0 = scan Flop/Turn/River

// ============================================================
// ESP32 Pin Definitions
// ============================================================
const int SCK_PIN      = 18;
const int MISO_PIN     = 19;
const int MOSI_PIN     = 23;

const int PN5180_RST   = 17;

// const int FLOP_NSS     = 5;
const int COMM_NSS     = 4;
// const int RIVER_NSS    = 14;

// const int FLOP_BUSY    = 25;
const int COMM_BUSY    = 26;
// const int RIVER_BUSY   = 27;

// ============================================================
// WiFi / Backend Configuration
// ============================================================
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Base URL only (no trailing slash) — e.g. "http://192.168.1.50:3001"
// or "https://your-backend.railway.app"
const char* BACKEND_HOST  = "http://192.168.1.50:3001";

// Must match ESP32_API_KEY in backend/.env
const char* ESP32_API_KEY = "your-secret-esp32-key-here";

const unsigned long BACKEND_POLL_INTERVAL_MS = 1000;  // how often to read phase/fold state
const unsigned long BACKEND_PUSH_INTERVAL_MS = 1000;  // how often to push cards/odds

// ============================================================
// PN5180 Commands and Registers
// ============================================================
#define PN5180_WRITE_REGISTER               0x00
#define PN5180_WRITE_REGISTER_OR_MASK       0x01
#define PN5180_WRITE_REGISTER_AND_MASK      0x02
#define PN5180_READ_REGISTER                0x04
#define PN5180_SEND_DATA                    0x09
#define PN5180_READ_DATA                    0x0A
#define PN5180_LOAD_RF_CONFIG               0x11
#define PN5180_RF_ON                        0x16
#define PN5180_RF_OFF                       0x17

#define REG_SYSTEM_CONFIG                   0x00
#define REG_IRQ_STATUS                      0x02
#define REG_IRQ_CLEAR                       0x03
#define REG_RX_STATUS                       0x13
#define REG_RF_STATUS                       0x1D

// Same IRQ bit definitions used in the working MSP430 code
#define IRQ_RX               (1UL << 0)
#define IRQ_TX               (1UL << 1)
#define IRQ_IDLE             (1UL << 2)
#define IRQ_TX_RFOFF         (1UL << 8)
#define IRQ_TX_RFON          (1UL << 9)
#define IRQ_RX_SOF_DET       (1UL << 14)

// ISO15693 RF configs
#define RF_ISO15693_TX       0x0D
#define RF_ISO15693_RX       0x8D

// ============================================================
// LCD
// ============================================================
LiquidCrystal_I2C lcd(0x27, 20, 4);

// ============================================================
// MAIN BOARD / ESP-NOW SETTINGS
// ============================================================

// Set this to however many player boards are active right now.
// Example: 3 while testing, 4 for final table.
#define ACTIVE_PLAYER_COUNT 4

// Enable/disable individual players.
// Index 0 unused.
bool playerEnabled[5] = {
  false,  // unused
  true,   // Player 1
  true,   // Player 2
  true,   // Player 3
  true   // Player 4, set true when connected
};

// Player MACs
uint8_t PLAYER_1_MAC[] = {0x00, 0x70, 0x07, 0x0E, 0x55, 0x58};
uint8_t PLAYER_2_MAC[] = {0x00, 0x70, 0x07, 0x0E, 0x56, 0x3C};
uint8_t PLAYER_3_MAC[] = {0x00, 0x70, 0x07, 0x0E, 0x55, 0x30};
uint8_t PLAYER_4_MAC[] = {0x00, 0x70, 0x07, 0x0E, 0x56, 0x34};

uint8_t *PLAYER_MACS[4] = {
  PLAYER_1_MAC,
  PLAYER_2_MAC,
  PLAYER_3_MAC,
  PLAYER_4_MAC
};

// ============================================================
// ESP-NOW Packet Structures
// Must match player board code
// ============================================================

typedef struct {
  uint8_t playerID;
  char card1[4];
  char card2[4];
  bool bothCardsReady;
  uint32_t sequence;
} PlayerCardsPacket;

typedef struct {
  char command[12];   // "RESET"
} HostCommandPacket;

// ============================================================
// Main Board State
// ============================================================

enum MainRoundState {
  ROUND_COLLECTING_DATA,
  ROUND_READY_FOR_EQUITY,
  ROUND_LOCKED_WAITING_FOR_RESET
};

enum CommunityState {
  WAITING_FOR_COMMUNITY_CARD,
  WAITING_FOR_COMMUNITY_CARD_REMOVAL,
  ALL_COMMUNITY_CARDS_READ
};

MainRoundState mainState = ROUND_COLLECTING_DATA;
CommunityState communityState = WAITING_FOR_COMMUNITY_CARD;

// ============================================================
// Website-Friendly Variables
// ============================================================

// Player cards, index 1-4 used.
String playerCard1[5] = {"", "", "", "", ""};
String playerCard2[5] = {"", "", "", "", ""};
bool playerCardsReady[5] = {false, false, false, false, false};
uint32_t playerLastSequence[5] = {0, 0, 0, 0, 0};

// Individual variables for website/equity code
String p1Card1 = "";
String p1Card2 = "";

String p2Card1 = "";
String p2Card2 = "";

String p3Card1 = "";
String p3Card2 = "";

String p4Card1 = "";
String p4Card2 = "";

// Community cards
String communityCards[5] = {"", "", "", "", ""};
bool communityCardsReady[5] = {false, false, false, false, false};

String flop1 = "";
String flop2 = "";
String flop3 = "";
String turnCard = "";
String riverCard = "";

uint8_t nextCommunityCardIndex = 0;

bool allPlayersReady = false;
bool allCommunityCardsReady = false;
bool fullTableReadyForEquity = false;

uint8_t readyPlayerCount = 0;
uint8_t enabledPlayerCount = 0;
uint8_t readyCommunityCount = 0;

uint32_t currentRoundNumber = 1;

// ============================================================
// Backend-Synced State
// ============================================================

bool backendSessionActive = false;      // is there an active session on the backend?
String activeSessionCode = "";          // discovered from GET /api/esp32/state
String backendPhase = "idle";           // dealer-controlled phase from the website
bool playerFoldedRemote[5] = {false, false, false, false, false}; // index 1-4 used
float playerWinOdds[5] = {0, 0, 0, 0, 0};

// ============================================================
// Reader Struct
// ============================================================
struct PN5180Reader{
  int nss_pin;
  int busy_pin;
  const char *name;
  String current_card;
};

// PN5180Reader flop  = { FLOP_NSS,  FLOP_BUSY,  "Flop",  "None" };
PN5180Reader comm  = { COMM_NSS,  COMM_BUSY,  "comm",  "None" };
// PN5180Reader river = { RIVER_NSS, RIVER_BUSY, "River", "None" };

// ============================================================
// Card Mapping
// ============================================================
typedef struct {
  uint8_t uid[8];
  const char *name;
} CardMap;

static const CardMap card_map[] = {
  {{0xEF, 0xA5, 0xEF, 0xE6, 0xC3, 0xA1, 0x1D, 0xE0}, "TEST CARD"},

  // Clubs
  {{0x96, 0x88, 0x2A, 0x1C, 0x53, 0x01, 0x04, 0xE0}, "Ac"},
  {{0xF9, 0x2E, 0x41, 0x1A, 0x53, 0x01, 0x04, 0xE0}, "2c"},
  {{0xF3, 0xE2, 0xCA, 0x1D, 0x53, 0x01, 0x04, 0xE0}, "3c"},
  {{0x39, 0xE4, 0xCA, 0x1D, 0x53, 0x01, 0x04, 0xE0}, "4c"},
  {{0x50, 0xDF, 0xCA, 0x1D, 0x53, 0x01, 0x04, 0xE0}, "5c"},
  {{0x25, 0x7A, 0x2A, 0x1C, 0x53, 0x01, 0x04, 0xE0}, "6c"},
  {{0x96, 0x0F, 0xCB, 0x1D, 0x53, 0x01, 0x04, 0xE0}, "7c"},
  {{0x31, 0x80, 0x2A, 0x1C, 0x53, 0x01, 0x04, 0xE0}, "8c"},
  {{0x9A, 0x02, 0xCB, 0x1D, 0x53, 0x01, 0x04, 0xE0}, "9c"},
  {{0xF6, 0x2F, 0x41, 0x1A, 0x53, 0x01, 0x04, 0xE0}, "10c"},
  {{0xE5, 0x12, 0xCB, 0x1D, 0x53, 0x01, 0x04, 0xE0}, "Jc"},
  {{0x69, 0x11, 0x41, 0x1A, 0x53, 0x01, 0x04, 0xE0}, "Qc"},
  {{0x5C, 0x2C, 0x41, 0x1A, 0x53, 0x01, 0x04, 0xE0}, "Kc"},

  // Spades
  {{0x4A, 0x2B, 0x41, 0x1A, 0x53, 0x01, 0x04, 0xE0}, "As"},
  {{0xB1, 0xB8, 0x2A, 0x1C, 0x53, 0x01, 0x04, 0xE0}, "2s"},
  {{0x74, 0x21, 0x41, 0x1A, 0x53, 0x01, 0x04, 0xE0}, "3s"},
  {{0xE9, 0x84, 0x2A, 0x1C, 0x53, 0x01, 0x04, 0xE0}, "4s"},
  {{0x72, 0xC0, 0x2A, 0x1C, 0x53, 0x01, 0x04, 0xE0}, "5s"},
  {{0x6E, 0x0E, 0xCB, 0x1D, 0x53, 0x01, 0x04, 0xE0}, "6s"},
  {{0x6E, 0xA6, 0x2A, 0x1C, 0x53, 0x01, 0x04, 0xE0}, "7s"},
  {{0x2D, 0xE4, 0xCA, 0x1D, 0x53, 0x01, 0x04, 0xE0}, "8s"},
  {{0xDB, 0xE1, 0xCA, 0x1D, 0x53, 0x01, 0x04, 0xE0}, "9s"},
  {{0xB4, 0xE6, 0xCA, 0x1D, 0x53, 0x01, 0x04, 0xE0}, "10s"},
  {{0x6C, 0x11, 0x41, 0x1A, 0x53, 0x01, 0x04, 0xE0}, "Js"},
  {{0xF1, 0x1F, 0x41, 0x1A, 0x53, 0x01, 0x04, 0xE0}, "Qs"},
  {{0x0C, 0x0C, 0xCB, 0x1D, 0x53, 0x01, 0x04, 0xE0}, "Ks"},

  // Diamonds
  {{0xB4, 0x8E, 0x2A, 0x1C, 0x53, 0x01, 0x04, 0xE0}, "Ad"},
  {{0x65, 0x02, 0x41, 0x1A, 0x53, 0x01, 0x04, 0xE0}, "2d"},
  {{0xA2, 0xBE, 0x2A, 0x1C, 0x53, 0x01, 0x04, 0xE0}, "3d"},
  {{0xB4, 0x0F, 0xCB, 0x1D, 0x53, 0x01, 0x04, 0xE0}, "4d"},
  {{0xF5, 0xBA, 0x2A, 0x1C, 0x53, 0x01, 0x04, 0xE0}, "5d"},
  {{0xD3, 0x00, 0xCB, 0x1D, 0x53, 0x01, 0x04, 0xE0}, "6d"},
  {{0xD6, 0x10, 0xCB, 0x1D, 0x53, 0x01, 0x04, 0xE0}, "7d"},
  {{0xD7, 0x10, 0xCB, 0x1D, 0x53, 0x01, 0x04, 0xE0}, "8d"},
  {{0x13, 0x06, 0x41, 0x1A, 0x53, 0x01, 0x04, 0xE0}, "9d"},
  {{0xA9, 0xF7, 0x40, 0x1A, 0x53, 0x01, 0x04, 0xE0}, "10d"},
  {{0x1B, 0x06, 0x41, 0x1A, 0x53, 0x01, 0x04, 0xE0}, "Jd"},
  {{0x68, 0xF0, 0x40, 0x1A, 0x53, 0x01, 0x04, 0xE0}, "Qd"},
  {{0x39, 0x07, 0x41, 0x1A, 0x53, 0x01, 0x04, 0xE0}, "Kd"},

  // Hearts
  {{0x8C, 0xAC, 0x2A, 0x1C, 0x53, 0x01, 0x04, 0xE0}, "Ah"},
  {{0xE8, 0x01, 0xCB, 0x1D, 0x53, 0x01, 0x04, 0xE0}, "2h"},
  {{0xE9, 0x01, 0xCB, 0x1D, 0x53, 0x01, 0x04, 0xE0}, "3h"},
  {{0xF8, 0xB9, 0x2A, 0x1C, 0x53, 0x01, 0x04, 0xE0}, "4h"},
  {{0x85, 0x15, 0xCB, 0x1D, 0x53, 0x01, 0x04, 0xE0}, "5h"},
  {{0x76, 0x03, 0xCB, 0x1D, 0x53, 0x01, 0x04, 0xE0}, "6h"},
  {{0xE5, 0x0B, 0x41, 0x1A, 0x53, 0x01, 0x04, 0xE0}, "7h"},
  {{0x6E, 0x15, 0xCB, 0x1D, 0x53, 0x01, 0x04, 0xE0}, "8h"},
  {{0x2B, 0x0E, 0xCB, 0x1D, 0x53, 0x01, 0x04, 0xE0}, "9h"},
  {{0x72, 0x09, 0xCB, 0x1D, 0x53, 0x01, 0x04, 0xE0}, "10h"},
  {{0x9B, 0xE0, 0xCA, 0x1D, 0x53, 0x01, 0x04, 0xE0}, "Jh"},
  {{0x9D, 0x03, 0xCB, 0x1D, 0x53, 0x01, 0x04, 0xE0}, "Qh"},
  {{0xA0, 0xFD, 0xCA, 0x1D, 0x53, 0x01, 0x04, 0xE0}, "Kh"}
};

#define CARD_MAP_COUNT (sizeof(card_map) / sizeof(card_map[0]))

// ============================================================
// Utility Functions
// ============================================================
void deselectAllReaders() {
  // digitalWrite(flop.nss_pin, HIGH);
  digitalWrite(comm.nss_pin, HIGH);
  // digitalWrite(river.nss_pin, HIGH);
  delayMicroseconds(2);
}

void cs_assert(PN5180Reader *r) {
  deselectAllReaders();
  digitalWrite(r->nss_pin, LOW);
  delayMicroseconds(5);
}

void cs_deassert(PN5180Reader *r) {
  delayMicroseconds(5);
  digitalWrite(r->nss_pin, HIGH);
  delayMicroseconds(5);
}

bool wait_not_busy(PN5180Reader *r) {
  uint32_t start = millis();

  while (digitalRead(r->busy_pin) == HIGH) {
    if (millis() - start > 100) {
      Serial.printf("%s: BUSY stuck HIGH\n", r->name);
      return false;
    }
  }

  return true;
}

bool wait_busy_high(PN5180Reader *r) {
  uint32_t start = millis();

  while (digitalRead(r->busy_pin) == LOW) {
    if (millis() - start > 100) {
      Serial.printf("%s: BUSY never went HIGH\n", r->name);
      return false;
    }
  }

  return true;
}

// ============================================================
// PN5180 Low-Level Functions
// ============================================================
void PN5180_WriteRegister(PN5180Reader *r, uint8_t reg, uint32_t value) {
  if (!wait_not_busy(r)) return;

  cs_assert(r);

  SPI.transfer(PN5180_WRITE_REGISTER);
  SPI.transfer(reg);
  SPI.transfer((uint8_t)(value));
  SPI.transfer((uint8_t)(value >> 8));
  SPI.transfer((uint8_t)(value >> 16));
  SPI.transfer((uint8_t)(value >> 24));

  cs_deassert(r);
}

uint32_t PN5180_ReadRegister(PN5180Reader *r, uint8_t reg) {
  uint8_t rx[4];

  if (!wait_not_busy(r)) return 0xFFFFFFFFUL;

  cs_assert(r);

  SPI.transfer(PN5180_READ_REGISTER);
  SPI.transfer(reg);

  if (!wait_busy_high(r)) {
    cs_deassert(r);
    return 0xFFFFFFFFUL;
  }

  cs_deassert(r);

  if (!wait_not_busy(r)) return 0xFFFFFFFFUL;

  cs_assert(r);

  rx[0] = SPI.transfer(0xFF);
  rx[1] = SPI.transfer(0xFF);
  rx[2] = SPI.transfer(0xFF);
  rx[3] = SPI.transfer(0xFF);

  if (!wait_busy_high(r)) {
    cs_deassert(r);
    return 0xFFFFFFFFUL;
  }

  cs_deassert(r);

  if (!wait_not_busy(r)) return 0xFFFFFFFFUL;

  return ((uint32_t)rx[0]) |
         ((uint32_t)rx[1] << 8) |
         ((uint32_t)rx[2] << 16) |
         ((uint32_t)rx[3] << 24);
}

void PN5180_WriteRegisterOrMask(PN5180Reader *r, uint8_t reg, uint32_t mask) {
  if (!wait_not_busy(r)) return;

  cs_assert(r);

  SPI.transfer(PN5180_WRITE_REGISTER_OR_MASK);
  SPI.transfer(reg);
  SPI.transfer((uint8_t)(mask));
  SPI.transfer((uint8_t)(mask >> 8));
  SPI.transfer((uint8_t)(mask >> 16));
  SPI.transfer((uint8_t)(mask >> 24));

  cs_deassert(r);
}

void PN5180_WriteRegisterAndMask(PN5180Reader *r, uint8_t reg, uint32_t mask) {
  if (!wait_not_busy(r)) return;

  cs_assert(r);

  SPI.transfer(PN5180_WRITE_REGISTER_AND_MASK);
  SPI.transfer(reg);
  SPI.transfer((uint8_t)(mask));
  SPI.transfer((uint8_t)(mask >> 8));
  SPI.transfer((uint8_t)(mask >> 16));
  SPI.transfer((uint8_t)(mask >> 24));

  cs_deassert(r);
}

void PN5180_ClearIRQStatus(PN5180Reader *r, uint32_t mask) {
  PN5180_WriteRegister(r, REG_IRQ_CLEAR, mask);
}

bool wait_irq_set(PN5180Reader *r, uint32_t mask, uint32_t loops) {
  while (loops--) {
    uint32_t irq = PN5180_ReadRegister(r, REG_IRQ_STATUS);

    if (irq & mask) {
      return true;
    }

    delayMicroseconds(100);
  }

  return false;
}

bool PN5180_SendData(PN5180Reader *r, uint8_t *txBuf, uint8_t txLen) {
  if (txLen == 0) return false;
  if (!wait_not_busy(r)) return false;

  cs_assert(r);

  SPI.transfer(PN5180_SEND_DATA);
  SPI.transfer(0x00);

  for (int i = 0; i < txLen; i++) {
    SPI.transfer(txBuf[i]);
  }

  cs_deassert(r);

  if (!wait_not_busy(r)) return false;

  return true;
}

bool PN5180_ReadData(PN5180Reader *r, uint8_t *rxBuf, uint8_t rxLen) {
  if (!wait_not_busy(r)) return false;

  cs_assert(r);

  SPI.transfer(PN5180_READ_DATA);
  SPI.transfer(0x00);

  if (!wait_busy_high(r)) {
    cs_deassert(r);
    return false;
  }

  cs_deassert(r);

  if (!wait_not_busy(r)) return false;

  cs_assert(r);

  for (int i = 0; i < rxLen; i++) {
    rxBuf[i] = SPI.transfer(0xFF);
  }

  if (!wait_busy_high(r)) {
    cs_deassert(r);
    return false;
  }

  cs_deassert(r);

  if (!wait_not_busy(r)) return false;

  return true;
}

// ============================================================
// PN5180 Reader Init
// ============================================================
void PN5180_RFOff(PN5180Reader *r) {
  if (!wait_not_busy(r)) return;

  cs_assert(r);
  SPI.transfer(PN5180_RF_OFF);
  SPI.transfer(0x00);
  cs_deassert(r);

  delay(5);
}

void initReader(PN5180Reader *r) {
  Serial.printf("\nInitializing %s reader...\n", r->name);

  PN5180_ClearIRQStatus(r, 0xFFFFFFFFUL);

  if (!wait_not_busy(r)) {
    Serial.printf("%s: Not ready before LOAD_RF_CONFIG\n", r->name);
    return;
  }

  cs_assert(r);
  SPI.transfer(PN5180_LOAD_RF_CONFIG);
  SPI.transfer(RF_ISO15693_TX);
  SPI.transfer(RF_ISO15693_RX);
  cs_deassert(r);

  delay(10);

  if (!wait_not_busy(r)) {
    Serial.printf("%s: Not ready before RF_ON\n", r->name);
    return;
  }

  cs_assert(r);
  SPI.transfer(PN5180_RF_ON);
  SPI.transfer(0x00);
  cs_deassert(r);

  if (wait_irq_set(r, IRQ_TX_RFON, 500)) {
    Serial.printf("%s: RF_ON OK\n", r->name);
    PN5180_ClearIRQStatus(r, IRQ_TX_RFON);
  } else {
    uint32_t irq = PN5180_ReadRegister(r, REG_IRQ_STATUS);
    Serial.printf("%s: RF_ON timeout, IRQ=0x%08lX\n", r->name, irq);
  }
}

// ============================================================
// ISO15693 Inventory
// ============================================================
uint8_t PN5180_ISO15693_Inventory(PN5180Reader *r, uint8_t *uid) {
  uint8_t cmd[3] = { 0x26, 0x01, 0x00 };
  uint8_t rxBuf[16];

  for (int i = 0; i < 8; i++) {
    uid[i] = 0;
  }

  PN5180_ClearIRQStatus(r, IRQ_RX_SOF_DET | IRQ_IDLE | IRQ_TX | IRQ_RX);

  PN5180_WriteRegisterAndMask(r, REG_SYSTEM_CONFIG, 0xFFFFFFF8UL);
  PN5180_WriteRegisterOrMask(r, REG_SYSTEM_CONFIG, 0x00000003UL);

  if (!PN5180_SendData(r, cmd, 3)) {
    Serial.printf("%s: SEND_DATA failed\n", r->name);
    return 0;
  }

  if (!wait_irq_set(r, IRQ_RX_SOF_DET, 500)) {
    uint32_t irq = PN5180_ReadRegister(r, REG_IRQ_STATUS);
    Serial.printf("%s: No SOF, IRQ=0x%08lX\n", r->name, irq);
    return 0;
  }

  if (!wait_irq_set(r, IRQ_RX, 500)) {
    uint32_t irq = PN5180_ReadRegister(r, REG_IRQ_STATUS);
    Serial.printf("%s: No RX complete, IRQ=0x%08lX\n", r->name, irq);
    PN5180_ClearIRQStatus(r, IRQ_RX_SOF_DET | IRQ_TX | IRQ_IDLE);
    return 0;
  }

  uint32_t rxStatus = PN5180_ReadRegister(r, REG_RX_STATUS);
  uint16_t rxLen = (uint16_t)(rxStatus & 0x01FF);

  Serial.printf("%s: RX_STATUS=0x%08lX LEN=%u\n", r->name, rxStatus, rxLen);

  if ((rxLen < 10) || (rxLen > sizeof(rxBuf))) {
    PN5180_ClearIRQStatus(r, IRQ_RX_SOF_DET | IRQ_IDLE | IRQ_TX | IRQ_RX);
    return 0;
  }

  if (!PN5180_ReadData(r, rxBuf, (uint8_t)rxLen)) {
    Serial.printf("%s: READ_DATA failed\n", r->name);
    return 0;
  }

  for (int i = 0; i < 8; i++) {
    uid[i] = rxBuf[2 + i];
  }

  PN5180_ClearIRQStatus(r, IRQ_RX_SOF_DET | IRQ_IDLE | IRQ_TX | IRQ_RX);

  return 8;
}

// ============================================================
// Card Lookup
// ============================================================
String uid_to_card(const uint8_t *uid) {
  for (int i = 0; i < CARD_MAP_COUNT; i++) {
    if (memcmp(uid, card_map[i].uid, 8) == 0) {
      return String(card_map[i].name);
    }
  }

  char rawUid[32];
  sprintf(rawUid,
          "%02X:%02X:%02X:%02X",
          uid[0], uid[1], uid[2], uid[3]);

  return String(rawUid);
}

String scanCard(PN5180Reader *r) {
  uint8_t uid[8];

  if (PN5180_ISO15693_Inventory(r, uid) == 8) {
    return uid_to_card(uid);
  }

  return "None";
}

// ============================================================
// Debug Functions
// ============================================================
void debugReader(PN5180Reader *r) {
  Serial.printf("\n--- %s Reader Debug ---\n", r->name);
  Serial.printf("NSS pin: %d\n", r->nss_pin);
  Serial.printf("BUSY pin: %d\n", r->busy_pin);
  Serial.printf("BUSY state: %s\n", digitalRead(r->busy_pin) ? "HIGH" : "LOW");

  uint32_t sys = PN5180_ReadRegister(r, REG_SYSTEM_CONFIG);
  uint32_t irq = PN5180_ReadRegister(r, REG_IRQ_STATUS);
  uint32_t rx  = PN5180_ReadRegister(r, REG_RX_STATUS);
  uint32_t rf  = PN5180_ReadRegister(r, REG_RF_STATUS);

  Serial.printf("SYSTEM_CONFIG = 0x%08lX\n", sys);
  Serial.printf("IRQ_STATUS    = 0x%08lX\n", irq);
  Serial.printf("RX_STATUS     = 0x%08lX\n", rx);
  Serial.printf("RF_STATUS     = 0x%08lX\n", rf);

  if (sys == 0xFFFFFFFFUL || irq == 0xFFFFFFFFUL || rx == 0xFFFFFFFFUL) {
    Serial.printf("%s WARNING: Register read is 0xFFFFFFFF. Check SPI/MISO/CS/power.\n", r->name);
  }
}

// ============================================================
// Display Helpers
// ============================================================
void updateLCDOneReader(PN5180Reader *r) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Testing ");
  lcd.print(r->name);

  lcd.setCursor(0, 1);
  lcd.print("Card: ");
  lcd.print(r->current_card);
}

void updateLCDAllReaders() {
  lcd.clear();

  // lcd.setCursor(0, 0);
  // lcd.print("Flop:  ");
  // lcd.print(flop.current_card);

  lcd.setCursor(0, 1);
  lcd.print("Comm:  ");
  lcd.print(comm.current_card);

  // lcd.setCursor(0, 2);
  // lcd.print("River: ");
  // lcd.print(river.current_card);
}

// ============================================================
// Website Variable Sync
// ============================================================

void syncWebsiteVariables() {
  p1Card1 = playerCard1[1];
  p1Card2 = playerCard2[1];

  p2Card1 = playerCard1[2];
  p2Card2 = playerCard2[2];

  p3Card1 = playerCard1[3];
  p3Card2 = playerCard2[3];

  p4Card1 = playerCard1[4];
  p4Card2 = playerCard2[4];

  flop1 = communityCards[0];
  flop2 = communityCards[1];
  flop3 = communityCards[2];
  turnCard = communityCards[3];
  riverCard = communityCards[4];
}

// ============================================================
// Readiness Tracking
// ============================================================

void updateReadyStatus() {
  readyPlayerCount = 0;
  enabledPlayerCount = 0;
  readyCommunityCount = 0;

  for (int i = 1; i <= 4; i++) {
    if (playerEnabled[i]) {
      enabledPlayerCount++;

      if (playerCardsReady[i]) {
        readyPlayerCount++;
      }
    }
  }

  for (int i = 0; i < 5; i++) {
    if (communityCardsReady[i]) {
      readyCommunityCount++;
    }
  }

  allPlayersReady = (readyPlayerCount == enabledPlayerCount);
  allCommunityCardsReady = (readyCommunityCount == 5);

  fullTableReadyForEquity = allPlayersReady && allCommunityCardsReady;

  if (fullTableReadyForEquity && mainState == ROUND_COLLECTING_DATA) {
    mainState = ROUND_READY_FOR_EQUITY;

    Serial.println();
    Serial.println("====================================");
    Serial.println(" FULL TABLE READY FOR EQUITY");
    Serial.println("====================================");
  }

  syncWebsiteVariables();
}

// ============================================================
// LCD / Debug Display
// ============================================================

void updateMainLCD() {
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Players: ");
  lcd.print(readyPlayerCount);
  lcd.print("/");
  lcd.print(enabledPlayerCount);

  lcd.setCursor(0, 1);
  lcd.print("Board: ");
  lcd.print(readyCommunityCount);
  lcd.print("/5");

  lcd.setCursor(0, 2);
  if (nextCommunityCardIndex < 5) {
    lcd.print("Scan board card ");
    lcd.print(nextCommunityCardIndex + 1);
  } else {
    lcd.print("Board complete");
  }

  lcd.setCursor(0, 3);
  if (fullTableReadyForEquity) {
    lcd.print("Ready for equity");
  } else {
    lcd.print("Collecting cards");
  }
}

void printCurrentGameState() {
  syncWebsiteVariables();

  Serial.println();
  Serial.println("========== CURRENT GAME STATE ==========");
  Serial.print("Round: ");
  Serial.println(currentRoundNumber);

  Serial.print("Backend phase: ");
  Serial.print(backendPhase);
  Serial.print(" | Session: ");
  Serial.println(backendSessionActive ? activeSessionCode : "(none)");

  Serial.print("Players ready: ");
  Serial.print(readyPlayerCount);
  Serial.print("/");
  Serial.println(enabledPlayerCount);

  for (int i = 1; i <= 4; i++) {
    Serial.print("Player ");
    Serial.print(i);
    Serial.print(": ");

    if (!playerEnabled[i]) {
      Serial.println("DISABLED");
    } else if (playerCardsReady[i]) {
      Serial.print(playerCard1[i]);
      Serial.print(" ");
      Serial.print(playerCard2[i]);
      Serial.println(playerFoldedRemote[i] ? " (FOLDED)" : "");
    } else {
      Serial.println("-- --");
    }
  }

  Serial.print("Community ready: ");
  Serial.print(readyCommunityCount);
  Serial.println("/5");

  Serial.print("Flop: ");
  Serial.print(flop1.length() ? flop1 : "--");
  Serial.print(" ");
  Serial.print(flop2.length() ? flop2 : "--");
  Serial.print(" ");
  Serial.println(flop3.length() ? flop3 : "--");

  Serial.print("Turn: ");
  Serial.println(turnCard.length() ? turnCard : "--");

  Serial.print("River: ");
  Serial.println(riverCard.length() ? riverCard : "--");

  Serial.print("Ready for equity: ");
  Serial.println(fullTableReadyForEquity ? "YES" : "NO");

  Serial.println("========================================");
}

// ============================================================
// Community Card State Machine
// IMPORTANT: Uses known-good scanCard(&turn)
// ============================================================

void handleCommunityCardReader() {
  static uint32_t lastScanTime = 0;
  static String lastScannedCard = "None";

  if (mainState != ROUND_COLLECTING_DATA) {
    return;
  }

  if (communityState == ALL_COMMUNITY_CARDS_READ) {
    return;
  }

  if (millis() - lastScanTime < 250) {
    return;
  }

  lastScanTime = millis();

  // DO NOT CHANGE THIS.
  // This is the known-good PN5180 reader call from your working code.
  String scannedCard = scanCard(&comm);

  if (scannedCard != lastScannedCard) {
    Serial.print("Community reader scanned: ");
    Serial.println(scannedCard);
    lastScannedCard = scannedCard;
  }

  switch (communityState) {
    case WAITING_FOR_COMMUNITY_CARD:
      if (scannedCard != "None") {

        if (communityCardAlreadyUsed(scannedCard)) {
          Serial.print("Duplicate community card ignored: ");
          Serial.println(scannedCard);

          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Duplicate card");
          lcd.setCursor(0, 1);
          lcd.print(scannedCard);
          lcd.setCursor(0, 2);
          lcd.print("Remove and scan");
          lcd.setCursor(0, 3);
          lcd.print("a different card");

          communityState = WAITING_FOR_COMMUNITY_CARD_REMOVAL;
          return;
        }

        communityCards[nextCommunityCardIndex] = scannedCard;
        communityCardsReady[nextCommunityCardIndex] = true;

        Serial.print("Stored community card ");
        Serial.print(nextCommunityCardIndex + 1);
        Serial.print(": ");
        Serial.println(scannedCard);

        nextCommunityCardIndex++;

        updateReadyStatus();
        updateMainLCD();
        printCurrentGameState();

        if (nextCommunityCardIndex >= 5) {
          communityState = ALL_COMMUNITY_CARDS_READ;
          updateReadyStatus();
          updateMainLCD();

          Serial.println("All 5 community cards have been read.");
        } else {
          communityState = WAITING_FOR_COMMUNITY_CARD_REMOVAL;
          Serial.println("Remove card before scanning next community card.");
        }
      }
      break;

    case WAITING_FOR_COMMUNITY_CARD_REMOVAL:
      if (scannedCard == "None") {
        communityState = WAITING_FOR_COMMUNITY_CARD;

        Serial.print("Ready for community card ");
        Serial.println(nextCommunityCardIndex + 1);

        updateMainLCD();
      }
      break;

    case ALL_COMMUNITY_CARDS_READ:
      break;
  }
}

// ============================================================
// Reset / New Round
// ============================================================

void clearRoundDataOnly() {
  for (int i = 1; i <= 4; i++) {
    playerCard1[i] = "";
    playerCard2[i] = "";
    playerCardsReady[i] = false;
    playerLastSequence[i] = 0;
  }

  for (int i = 0; i < 5; i++) {
    communityCards[i] = "";
    communityCardsReady[i] = false;
  }

  nextCommunityCardIndex = 0;

  p1Card1 = "";
  p1Card2 = "";
  p2Card1 = "";
  p2Card2 = "";
  p3Card1 = "";
  p3Card2 = "";
  p4Card1 = "";
  p4Card2 = "";

  flop1 = "";
  flop2 = "";
  flop3 = "";
  turnCard = "";
  riverCard = "";

  readyPlayerCount = 0;
  readyCommunityCount = 0;

  allPlayersReady = false;
  allCommunityCardsReady = false;
  fullTableReadyForEquity = false;

  mainState = ROUND_COLLECTING_DATA;
  communityState = WAITING_FOR_COMMUNITY_CARD;

  syncWebsiteVariables();
  updateMainLCD();
}

void sendResetToOnePlayer(uint8_t *playerMac, int playerNumber) {
  HostCommandPacket resetPacket;
  memset(&resetPacket, 0, sizeof(resetPacket));
  strcpy(resetPacket.command, "RESET");

  esp_err_t result = esp_now_send(playerMac,
                                  (uint8_t *)&resetPacket,
                                  sizeof(resetPacket));

  Serial.print("Sending RESET to Player ");
  Serial.print(playerNumber);
  Serial.print(": ");

  if (result == ESP_OK) {
    Serial.println("Queued");
  } else {
    Serial.print("Failed, error ");
    Serial.println(result);
  }
}

void sendResetToAllEnabledPlayers() {
  Serial.println("Sending RESET command to enabled player boards...");

  for (int i = 1; i <= 4; i++) {
    if (playerEnabled[i]) {
      sendResetToOnePlayer(PLAYER_MACS[i - 1], i);
      delay(50);
    }
  }
}

void resetForNextRound() {
  Serial.println();
  Serial.println("====================================");
  Serial.println(" Starting new round");
  Serial.println("====================================");

  mainState = ROUND_LOCKED_WAITING_FOR_RESET;

  sendResetToAllEnabledPlayers();

  clearRoundDataOnly();

  currentRoundNumber++;

  Serial.print("Now waiting for cards for round ");
  Serial.println(currentRoundNumber);

  printCurrentGameState();
}

// ============================================================
// ESP-NOW Receive
// ============================================================

void handleIncomingPlayerPacket(const uint8_t *incomingData, int len) {
  if (len != sizeof(PlayerCardsPacket)) {
    Serial.print("Unknown packet length received: ");
    Serial.println(len);
    return;
  }

  PlayerCardsPacket packet;
  memcpy(&packet, incomingData, sizeof(packet));

  uint8_t playerID = packet.playerID;

  if (playerID < 1 || playerID > 4) {
    Serial.print("Invalid player ID received: ");
    Serial.println(playerID);
    return;
  }

  if (!playerEnabled[playerID]) {
    Serial.print("Packet from disabled Player ");
    Serial.print(playerID);
    Serial.println(" ignored.");
    return;
  }

  if (mainState != ROUND_COLLECTING_DATA) {
    Serial.println("Main board is not accepting new player cards right now.");
    return;
  }

  if (!packet.bothCardsReady) {
    Serial.println("Player packet ignored because bothCardsReady is false.");
    return;
  }

  playerCard1[playerID] = String(packet.card1);
  playerCard2[playerID] = String(packet.card2);
  playerCardsReady[playerID] = true;
  playerLastSequence[playerID] = packet.sequence;

  Serial.println();
  Serial.println("----- Player Packet Received -----");
  Serial.print("Player ");
  Serial.print(playerID);
  Serial.print(": ");
  Serial.print(playerCard1[playerID]);
  Serial.print(" ");
  Serial.println(playerCard2[playerID]);
  Serial.println("----------------------------------");

  updateReadyStatus();
  updateMainLCD();
  printCurrentGameState();
}

// ESP32 Arduino core 3.x callback
void onDataReceived(const esp_now_recv_info_t *recvInfo,
                    const uint8_t *incomingData,
                    int len) {
  handleIncomingPlayerPacket(incomingData, len);
}

// ESP32 Arduino core 3.x callback
void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  Serial.print("ESP-NOW send status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

// ============================================================
// ESP-NOW Setup
// ============================================================

void addPlayerPeer(uint8_t *mac, int playerNumber) {
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, mac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  esp_err_t result = esp_now_add_peer(&peerInfo);

  Serial.print("Adding Player ");
  Serial.print(playerNumber);
  Serial.print(" peer: ");

  if (result == ESP_OK) {
    Serial.println("OK");
  } else if (result == ESP_ERR_ESPNOW_EXIST) {
    Serial.println("Already exists");
  } else {
    Serial.print("Failed, error ");
    Serial.println(result);
  }
}

void initEspNow() {
  // NOTE: WiFi is already connected (STA mode) by connectToWiFi() in setup(),
  // which ESP-NOW rides on top of. Do not call WiFi.mode()/WiFi.disconnect()
  // here or it will drop the backend connection.

  Serial.print("Main board MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW init failed");
    return;
  }

  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataReceived);

  for (int i = 1; i <= 4; i++) {
    if (playerEnabled[i]) {
      addPlayerPeer(PLAYER_MACS[i - 1], i);
    }
  }

  Serial.println("ESP-NOW initialized on main board.");
}

// ============================================================
// WiFi + Backend Sync
// ============================================================

void connectToWiFi() {
  Serial.print("\nConnecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("WiFi connected. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("WiFi connect FAILED. Backend sync and ESP-NOW peers on other");
    Serial.println("channels may not work until this board reconnects.");
  }
}

// Placeholder for the real hand-equity / Monte Carlo calculation.
// Folded seats (playerFoldedRemote[seat] == true) must be excluded from
// opponents' possible ranges when this is implemented.
float calculateWinOdds(int seat) {
  if (seat < 1 || seat > 4) return 0.0;
  if (playerFoldedRemote[seat]) return 0.0;
  if (!playerCardsReady[seat]) return 0.0;

  // TODO: plug in real equity calculation here, using:
  //  - playerCard1[seat], playerCard2[seat] for this seat's hole cards
  //  - communityCards[0..4] (only entries where communityCardsReady[i] is true)
  //  - skip any other seat where playerFoldedRemote[otherSeat] is true
  return 0.0;
}

// GET /api/esp32/state — learn the dealer-controlled phase and fold status.
// Returns true if an active session was found.
bool fetchBackendState() {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  String url = String(BACKEND_HOST) + "/api/esp32/state";
  http.begin(url);
  http.addHeader("x-api-key", ESP32_API_KEY);

  int code = http.GET();

  if (code == 404) {
    backendSessionActive = false;
    activeSessionCode = "";
    http.end();
    return false;
  }

  if (code != 200) {
    Serial.printf("Backend state poll failed, HTTP %d\n", code);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.print("JSON parse error (state): ");
    Serial.println(err.c_str());
    return false;
  }

  String previousPhase = backendPhase;

  backendSessionActive = true;
  activeSessionCode = doc["sessionCode"].as<String>();
  backendPhase = doc["phase"].as<String>();

  JsonArray players = doc["players"].as<JsonArray>();
  for (JsonObject p : players) {
    int seat = p["seat"].as<int>();
    if (seat >= 1 && seat <= 4) {
      playerFoldedRemote[seat] = p["folded"].as<bool>();
    }
  }

  // The dealer started a new hand on the website (phase reset to pre-flop) —
  // clear local round data and tell the player boards to reset automatically,
  // without needing the serial 'r' command.
  if (backendPhase == "pre-flop" && previousPhase != "pre-flop" && previousPhase != "idle") {
    Serial.println("Backend phase returned to pre-flop — starting new round.");
    resetForNextRound();
  } else if (backendPhase != previousPhase) {
    Serial.printf("Backend phase changed: %s -> %s\n", previousPhase.c_str(), backendPhase.c_str());
  }

  return true;
}

// POST /api/esp32/update — push hole cards, community cards, and win odds.
void pushGameStateToBackend() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!backendSessionActive || activeSessionCode.length() == 0) return;

  JsonDocument doc;
  doc["sessionCode"] = activeSessionCode;
  doc["phase"] = backendPhase;

  JsonArray players = doc["players"].to<JsonArray>();
  for (int i = 1; i <= 4; i++) {
    if (!playerEnabled[i]) continue;

    JsonObject p = players.add<JsonObject>();
    p["seat"] = i;

    JsonArray cards = p["cards"].to<JsonArray>();
    if (playerCardsReady[i]) {
      cards.add(playerCard1[i]);
      cards.add(playerCard2[i]);
    }

    playerWinOdds[i] = calculateWinOdds(i);
    p["winOdds"] = playerWinOdds[i];
  }

  JsonArray community = doc["communityCards"].to<JsonArray>();
  for (int i = 0; i < 5; i++) {
    if (communityCardsReady[i]) {
      community.add(communityCards[i]);
    }
  }

  String body;
  serializeJson(doc, body);

  HTTPClient http;
  String url = String(BACKEND_HOST) + "/api/esp32/update";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-api-key", ESP32_API_KEY);

  int code = http.POST(body);
  if (code != 200) {
    Serial.printf("Backend update push failed, HTTP %d\n", code);
  }

  http.end();
}

// Called every loop() iteration. Non-blocking — polls/pushes on their own
// timers so it never interferes with the PN5180/ESP-NOW timing above.
void handleBackendSync() {
  static uint32_t lastPoll = 0;
  static uint32_t lastPush = 0;
  uint32_t now = millis();

  if (now - lastPoll >= BACKEND_POLL_INTERVAL_MS) {
    lastPoll = now;
    fetchBackendState();
  }

  if (now - lastPush >= BACKEND_PUSH_INTERVAL_MS) {
    lastPush = now;
    pushGameStateToBackend();
  }
}

// ============================================================
// Serial Commands
// ============================================================

void handleSerialCommands() {
  if (!Serial.available()) {
    return;
  }

  char c = Serial.read();

  if (c == 'r' || c == 'R') {
    resetForNextRound();
  } else if (c == 'p' || c == 'P') {
    printCurrentGameState();
  } else if (c == 'c' || c == 'C') {
    clearRoundDataOnly();
    Serial.println("Local round data cleared only. No reset sent to players.");
    printCurrentGameState();
  }
}

bool communityCardAlreadyUsed(String card) {
  if (card == "" || card == "None") {
    return false;
  }

  for (int i = 0; i < 5; i++) {
    if (communityCardsReady[i] && communityCards[i] == card) {
      return true;
    }
  }

  return false;
}

// ============================================================
// Setup
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n--- PN5180 MULTI-READER + BACKEND SYNC ---");

  // LCD first, so we can show boot status
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Booting...");
  lcd.setCursor(0, 1);
  lcd.print("PN5180 Test");

  // CRITICAL: set all chip selects HIGH before reset/SPI
  // pinMode(flop.nss_pin, OUTPUT);
  pinMode(comm.nss_pin, OUTPUT);
  // pinMode(river.nss_pin, OUTPUT);

  // digitalWrite(flop.nss_pin, HIGH);
  digitalWrite(comm.nss_pin, HIGH);
  // digitalWrite(river.nss_pin, HIGH);

  // pinMode(flop.busy_pin, INPUT);
  pinMode(comm.busy_pin, INPUT);
  // pinMode(river.busy_pin, INPUT);

  // Shared PN5180 reset
  pinMode(PN5180_RST, OUTPUT);
  digitalWrite(PN5180_RST, LOW);
  delay(20);
  digitalWrite(PN5180_RST, HIGH);
  delay(100);

  // SPI bus
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, -1);

  // Slow SPI for debugging. Increase later if stable.
  SPI.beginTransaction(SPISettings(100000, MSBFIRST, SPI_MODE0));

  Serial.println("SPI initialized at 100 kHz");

  // Debug all readers before RF init
  // debugReader(&flop);
  debugReader(&comm);
  // debugReader(&river);

#if TEST_ONE_READER_ONLY
  // Start with Turn only
  initReader(&comm);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Turn Ready");
  lcd.setCursor(0, 1);
  lcd.print("Scan Card");

  Serial.println("\nTEST_ONE_READER_ONLY = 1");
  Serial.println("Only Turn reader will be scanned.");

#endif

  // Connect to WiFi BEFORE ESP-NOW init so both ride on the same channel.
  connectToWiFi();
  initEspNow();
  clearRoundDataOnly();

  // Do an initial poll so backendPhase/activeSessionCode are populated
  // before the first hand starts.
  fetchBackendState();

  Serial.println();
  Serial.println("Main board ready.");
  Serial.println("Commands:");
  Serial.println("  r = reset all enabled player boards and start new round");
  Serial.println("  p = print current game state");
  Serial.println("  c = clear local data only");
  Serial.println();

  printCurrentGameState();
  Serial.println("Setup complete.");
}

// ============================================================
// Loop
// ============================================================
void loop() {
  handleSerialCommands();

  // Reads 5 community cards using known-good PN5180 code.
  handleCommunityCardReader();

  // Polls dealer-controlled phase/fold state from the backend and pushes
  // hole cards, community cards, and win odds back to it.
  handleBackendSync();

  /*
   * Website developer variables:
   *
   * Player hole cards:
   *   p1Card1, p1Card2
   *   p2Card1, p2Card2
   *   p3Card1, p3Card2
   *   p4Card1, p4Card2
   *
   * Community cards:
   *   flop1, flop2, flop3, turnCard, riverCard
   *
   * Array form:
   *   playerCard1[1..4]
   *   playerCard2[1..4]
   *   communityCards[0..4]
   *
   * Readiness:
   *   allPlayersReady
   *   allCommunityCardsReady
   *   fullTableReadyForEquity
   *
   * Backend-synced state:
   *   backendPhase           — dealer-controlled phase from the website
   *   playerFoldedRemote[1..4] — true if that seat has folded
   *
   * Equity calculation should run when:
   *   fullTableReadyForEquity == true
   *   (see calculateWinOdds() — folded seats must be excluded)
   *
   * New round button should call:
   *   resetForNextRound();
   *   (this now also happens automatically when backendPhase returns to
   *   "pre-flop" after a hand ends)
   */
}
