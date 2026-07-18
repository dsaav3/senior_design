/*
 * ESP32-WROOM-32: PN5180 Multi-Reader Debug/Test
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
 */

#include <SPI.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ============================================================
// Debug mode
// ============================================================
#define TEST_ONE_READER_ONLY 1   // 1 = test Turn only, 0 = scan Flop/Turn/River

// ESP-NOW requires both ends of a link to be on the EXACT same WiFi
// channel — a mismatch means packets are never seen at all in either
// direction, no matter how strong the signal is, which looks identical to
// a wiring/MAC-address problem and is easy to chase the wrong thing for.
// This board never joins the real WiFi network (it only speaks ESP-NOW),
// so left alone it just sits on whatever channel the radio happens to
// power up on — which has no reason to match the main board's actual
// channel (decided by the router it connects to). This MUST be set to
// whatever channel the main board's Serial Monitor prints on boot (see
// WIFI_CHANNEL in poker_table.ino) — reflash every player board any time
// that changes, e.g. if the router's channel changes.
const int WIFI_CHANNEL = 6;

// ============================================================
// ESP32 Pin Definitions
// ============================================================
const int SCK_PIN      = 18;
const int MISO_PIN     = 19;
const int MOSI_PIN     = 23;

const int PN5180_RST   = 17;

// const int FLOP_NSS     = 5;
const int PLAYER_NSS     = 4;
// const int RIVER_NSS    = 14;

// const int FLOP_BUSY    = 25;
const int PLAYER_BUSY    = 26;
// const int RIVER_BUSY   = 27;

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
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ============================================================
// PLAYER BOARD / ESP-NOW SETTINGS
// ============================================================

#define PLAYER_ID 4   // Change to 1, 2, 3, or 4 before uploading

// Main board MAC: 04:B2:47:95:02:58
uint8_t MAIN_BOARD_MAC[] = {0x04, 0xB2, 0x47, 0x95, 0x02, 0x58};

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

// Main board -> player board, sent whenever the main board recomputes
// equity for this seat. Distinct size from HostCommandPacket (12 bytes) so
// onDataReceived() can tell them apart by packet length.
//
// `sequence` guards against a real bug found in an earlier version of
// this: ESP-NOW is fire-and-forget with no delivery-order guarantee, so if
// two updates go out close together, the OLDER one can arrive after the
// newer one and silently overwrite the correct display (this is exactly
// how a made royal flush once showed up here as "High Card" — the website,
// reading the same underlying data but not subject to this race, showed it
// correctly). Any incoming packet whose sequence isn't newer than the last
// one displayed is dropped — see onDataReceived() below.
typedef struct {
  char handShortCode[4];  // e.g. "2P", "STR", "RF"
  float straightPct;
  float flushPct;
  float fullHousePct;
  uint32_t sequence;
} PlayerHandInfoPacket;

PlayerCardsPacket outgoingPacket;
uint32_t sequenceCounter = 0;

// ESP-NOW send retry — esp_now_send()'s return value only means "queued
// successfully," not "actually delivered." The real result comes back
// asynchronously via the onDataSent callback below. Without acting on a
// failure there, a failed transmission just leaves the board stuck in
// CARDS_SENT_WAITING_FOR_RESET forever, showing "Sent" for cards the main
// board never actually received.
const int MAX_SEND_ATTEMPTS = 3;
int sendAttemptCount = 0;
volatile bool sendCallbackPending = false;
volatile bool lastSendSucceeded = false;

enum PlayerState {
  WAITING_FOR_CARD_1,
  WAITING_FOR_CARD_REMOVAL_AFTER_CARD_1,
  WAITING_FOR_CARD_2,
  SENDING_CARDS_TO_MAIN,     // waiting on the async ESP-NOW delivery result, retrying on failure
  CARDS_SENT_WAITING_FOR_RESET,
  SEND_FAILED                // all 3 attempts failed — needs the dealer/next-hand RESET to recover
};

PlayerState playerState = WAITING_FOR_CARD_1;

String playerCard1 = "";
String playerCard2 = "";
String lastScannedCard = "None";

// Hand info pushed back from the main board once this seat (and at least
// one other) are ready — see PlayerHandInfoPacket above. Shown on the LCD
// once received (see updatePlayerLCD()), instead of "Sent".
bool handInfoReceived = false;
String currentHandCode = "";
float straightPct = 0;
float flushPct = 0;
float fullHousePct = 0;
uint32_t lastHandInfoSequence = 0;  // stale-packet guard — see PlayerHandInfoPacket above

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
PN5180Reader player  = { PLAYER_NSS,  PLAYER_BUSY,  "Player",  "None" };
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
  digitalWrite(player.nss_pin, HIGH);
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

  lcd.setCursor(0, 0);
  lcd.print("Player:  ");
  lcd.print(player.current_card);

  // lcd.setCursor(0, 1);
  // lcd.print("Turn:  ");
  // lcd.print(turn.current_card);

  // lcd.setCursor(0, 2);
  // lcd.print("River: ");
  // lcd.print(river.current_card);
}

// Line 0: "STR:12% FL:8%" style (worst case "STR:100% FL:100%" is exactly
// 16 chars). Line 1: "FH:12% " + short hand code (worst-case code is 3
// chars, e.g. "STR", so this never overflows either).
void updateHandInfoLCD() {
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("STR:");
  lcd.print((int)(straightPct + 0.5f));
  lcd.print("% FL:");
  lcd.print((int)(flushPct + 0.5f));
  lcd.print("%");

  lcd.setCursor(0, 1);
  lcd.print("FH:");
  lcd.print((int)(fullHousePct + 0.5f));
  lcd.print("% ");
  lcd.print(currentHandCode);
}

void updatePlayerLCD() {
  // Once cards are confirmed delivered AND the main board has pushed back
  // computed odds for this seat, show the odds screen instead of "Sent".
  if (playerState == CARDS_SENT_WAITING_FOR_RESET && handInfoReceived) {
    updateHandInfoLCD();
    return;
  }

  lcd.clear();

  // Line 0: Player number and current state
  lcd.setCursor(0, 0);
  lcd.print("P");
  lcd.print(PLAYER_ID);
  lcd.print(" ");

  if (playerState == WAITING_FOR_CARD_1) {
    lcd.print("Scan C1");
  } else if (playerState == WAITING_FOR_CARD_REMOVAL_AFTER_CARD_1) {
    lcd.print("Remove C1");
  } else if (playerState == WAITING_FOR_CARD_2) {
    lcd.print("Scan C2");
  } else if (playerState == SENDING_CARDS_TO_MAIN) {
    lcd.print("Sending");
  } else if (playerState == CARDS_SENT_WAITING_FOR_RESET) {
    lcd.print("Sent");
  } else if (playerState == SEND_FAILED) {
    lcd.print("Send Failed");
  }

  // Line 1: Both cards, or a hint to fix a failed send
  lcd.setCursor(0, 1);
  if (playerState == SEND_FAILED) {
    lcd.print("Ask the dealer");
  } else {
    lcd.print("C1:");
    lcd.print(playerCard1.length() > 0 ? playerCard1 : "--");
    lcd.print(" C2:");
    lcd.print(playerCard2.length() > 0 ? playerCard2 : "--");
  }
}

void buildOutgoingCardsPacket() {
  memset(&outgoingPacket, 0, sizeof(outgoingPacket));

  outgoingPacket.playerID = PLAYER_ID;
  outgoingPacket.bothCardsReady = true;
  outgoingPacket.sequence = sequenceCounter;  // same sequence across retries — this is one logical packet, not a new one each attempt

  strncpy(outgoingPacket.card1, playerCard1.c_str(), sizeof(outgoingPacket.card1) - 1);
  strncpy(outgoingPacket.card2, playerCard2.c_str(), sizeof(outgoingPacket.card2) - 1);
}

// Fires the actual ESP-NOW send for the current attempt. Called both for
// the first attempt and for each retry.
void attemptSendCardsToMainBoard() {
  sendAttemptCount++;
  sendCallbackPending = true;

  Serial.println();
  Serial.printf("Sending player cards to main board (attempt %d/%d)...\n",
                sendAttemptCount, MAX_SEND_ATTEMPTS);
  Serial.printf("Player %u: %s %s\n",
                PLAYER_ID,
                playerCard1.c_str(),
                playerCard2.c_str());

  esp_err_t result = esp_now_send(MAIN_BOARD_MAC,
                                  (uint8_t *)&outgoingPacket,
                                  sizeof(outgoingPacket));

  if (result != ESP_OK) {
    // Never even got queued — onDataSent will not fire for this attempt,
    // so resolve the failure immediately instead of waiting on a callback
    // that isn't coming.
    Serial.printf("ESP-NOW queue failed. Error: %d\n", result);
    sendCallbackPending = false;
    lastSendSucceeded = false;
  } else {
    Serial.println("ESP-NOW packet queued, waiting for delivery result...");
  }
}

void sendCardsToMainBoard() {
  sequenceCounter++;
  buildOutgoingCardsPacket();
  sendAttemptCount = 0;
  attemptSendCardsToMainBoard();
}

void resetForNextRound() {
  playerCard1 = "";
  playerCard2 = "";
  lastScannedCard = "None";
  playerState = WAITING_FOR_CARD_1;

  sendAttemptCount = 0;
  sendCallbackPending = false;
  handInfoReceived = false;
  currentHandCode = "";
  straightPct = 0;
  flushPct = 0;
  fullHousePct = 0;
  lastHandInfoSequence = 0;

  Serial.println();
  Serial.println("RESET command received. Ready for next round.");

  updatePlayerLCD();
}

// ESP32 Arduino core 3.x callback. This is the REAL send result — the
// return value of esp_now_send() itself only means "queued OK," not
// "delivered." loop()'s SENDING_CARDS_TO_MAIN case acts on these flags.
void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  lastSendSucceeded = (status == ESP_NOW_SEND_SUCCESS);
  sendCallbackPending = false;

  Serial.print("Send status: ");
  Serial.println(lastSendSucceeded ? "Success" : "Fail");
}

// ESP32 Arduino core 3.x callback
void onDataReceived(const esp_now_recv_info_t *recvInfo,
                    const uint8_t *incomingData,
                    int len) {
  if (len == sizeof(HostCommandPacket)) {
    HostCommandPacket cmd;
    memcpy(&cmd, incomingData, sizeof(cmd));

    Serial.print("Command received from host: ");
    Serial.println(cmd.command);

    if (strcmp(cmd.command, "RESET") == 0) {
      resetForNextRound();
    }
    return;
  }

  if (len == sizeof(PlayerHandInfoPacket)) {
    PlayerHandInfoPacket info;
    memcpy(&info, incomingData, sizeof(info));

    // Drop anything not newer than what's already displayed — ESP-NOW
    // doesn't guarantee delivery order, so an older update sent slightly
    // earlier can otherwise arrive after a newer one and silently show
    // stale info (see PlayerHandInfoPacket's comment for the real incident
    // this fixes: a made royal flush briefly displaying as "High Card").
    if (handInfoReceived && info.sequence < lastHandInfoSequence) {
      Serial.printf("Ignoring stale hand info packet (seq %lu < %lu)\n",
                    (unsigned long)info.sequence, (unsigned long)lastHandInfoSequence);
      return;
    }
    lastHandInfoSequence = info.sequence;

    // Receiving this at all is definitive proof the main board already has
    // our cards — it can only have computed and sent this in response to
    // them. That's a stronger signal than esp_now_send()'s own delivery
    // status, which is based on a low-level radio ACK that can genuinely
    // get lost on the way back even when the original data got through and
    // was fully processed (a real, observed case: main board shows the
    // cards fine, this board's own send still reports failed). So if we're
    // still stuck retrying, or already gave up after 3 failed attempts,
    // this recovers us immediately instead of leaving "Send Failed" up
    // for cards the main board actually has.
    if (playerState == SENDING_CARDS_TO_MAIN || playerState == SEND_FAILED) {
      Serial.println("Hand info arrived while send was still pending/failed — main board has our cards after all, recovering.");
      playerState = CARDS_SENT_WAITING_FOR_RESET;
      sendCallbackPending = false;
    }

    char code[5] = {0};
    strncpy(code, info.handShortCode, sizeof(code) - 1);
    currentHandCode = String(code);
    straightPct = info.straightPct;
    flushPct = info.flushPct;
    fullHousePct = info.fullHousePct;
    handInfoReceived = true;

    Serial.printf("Hand info received: %s STR:%.0f%% FL:%.0f%% FH:%.0f%%\n",
                  code, straightPct, flushPct, fullHousePct);

    if (playerState == CARDS_SENT_WAITING_FOR_RESET) {
      updatePlayerLCD();
    }
    return;
  }

  Serial.printf("Unknown packet received. Length: %d\n", len);
}

void initEspNow() {
  WiFi.mode(WIFI_STA);

  // Lock onto WIFI_CHANNEL before anything else — see its comment above.
  // Without this, this board sits on whatever channel it happens to power
  // up on (in practice, channel 1), with no reason to match the main
  // board's actual channel.
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

  // Disable WiFi power-save (modem sleep) — by default the ESP32's radio
  // periodically powers down even without an AP connection, which is a
  // well-known cause of exactly the symptom seen here: a send occasionally
  // reported as failed (its ACK missed while the radio was asleep) even
  // though the main board actually received it fine, and — separately —
  // incoming packets (like a RESET) sometimes just never arriving either,
  // since ESP-NOW rides on the same radio and is subject to the same sleep
  // cycle either side of the link. No real downside on a table unit like
  // this.
  WiFi.setSleep(false);

  WiFi.setTxPower(WIFI_POWER_2dBm);
  delay(500);

  Serial.print("This player MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataReceived);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, MAIN_BOARD_MAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add main board peer");
    return;
  }

  Serial.println("ESP-NOW ready");
}

// ============================================================
// Setup
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n--- PN5180 MULTI-READER DEBUG TEST ---");

  // LCD first, so we can show boot status
  Wire.begin(21, 22);

  // Bound every I2C transaction instead of risking an indefinite stall.
  // This matters a lot more here than it might look: the pro-mode switch
  // cuts 5V to this LCD in hardware, so it stops ACKing entirely, but the
  // code keeps calling lcd.clear()/setCursor()/print() on every state
  // change regardless — it has no way to know the switch is off. A
  // PCF8574-backed I2C LCD sends each character over several short I2C
  // writes (4-bit interface), so a single updatePlayerLCD() call can be
  // dozens of individual transactions — if each one blocks for the WiFi/
  // I2C default timeout waiting for an ACK that will never come with the
  // LCD unpowered, those add up fast and were almost certainly what was
  // delaying the PN5180 reader between card 1 and card 2. Kept short (5ms)
  // rather than reusing the main board's 50ms, specifically because it's
  // the cumulative total across many small transactions per update that
  // matters here, not any single one.
  Wire.setTimeOut(5);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Booting...");
  lcd.setCursor(0, 1);
  lcd.print("PN5180 Test");

  // CRITICAL: set all chip selects HIGH before reset/SPI
  // pinMode(flop.nss_pin, OUTPUT);
  pinMode(player.nss_pin, OUTPUT);
  // pinMode(river.nss_pin, OUTPUT);

  // digitalWrite(flop.nss_pin, HIGH);
  digitalWrite(player.nss_pin, HIGH);
  // digitalWrite(river.nss_pin, HIGH);

  // pinMode(flop.busy_pin, INPUT);
  pinMode(player.busy_pin, INPUT);
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
  debugReader(&player);
  // debugReader(&river);

#if TEST_ONE_READER_ONLY
  // Start with Turn only
  initReader(&player);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Player Ready");
  lcd.setCursor(0, 1);
  lcd.print("Scan Card");

  Serial.println("\nTEST_ONE_READER_ONLY = 1");
  // Serial.println("Only Turn reader will be scanned.");
#endif

  initEspNow();
  updatePlayerLCD();

  Serial.println("Setup complete.");
}

// ============================================================
// Loop
// ============================================================
void loop() {
  static uint32_t lastScanTime = 0;

  if (millis() - lastScanTime < 250) {
    return;
  }

  lastScanTime = millis();

  // Waiting on the async ESP-NOW delivery result for the cards packet —
  // retry (up to MAX_SEND_ATTEMPTS) on failure instead of getting stuck.
  // No card to scan for in this state, so handle it before scanCard().
  if (playerState == SENDING_CARDS_TO_MAIN) {
    if (sendCallbackPending) {
      return;  // still waiting on onDataSent for the current attempt
    }

    if (lastSendSucceeded) {
      Serial.println("Cards delivered to main board successfully.");
      playerState = CARDS_SENT_WAITING_FOR_RESET;
    } else if (sendAttemptCount < MAX_SEND_ATTEMPTS) {
      Serial.printf("Send failed, retrying (%d/%d)...\n", sendAttemptCount + 1, MAX_SEND_ATTEMPTS);
      attemptSendCardsToMainBoard();
    } else {
      Serial.println("Send failed after 3 attempts. Ask the dealer to check the connection.");
      playerState = SEND_FAILED;
    }
    updatePlayerLCD();
    return;
  }

  if (playerState == CARDS_SENT_WAITING_FOR_RESET || playerState == SEND_FAILED) {
    return;
  }

  String scannedCard = scanCard(&player);

  if (scannedCard != lastScannedCard) {
    Serial.print("Scanned: ");
    Serial.println(scannedCard);
    lastScannedCard = scannedCard;
  }

  switch (playerState) {
    case WAITING_FOR_CARD_1:
      if (scannedCard != "None") {
        playerCard1 = scannedCard;

        Serial.print("Card 1 stored: ");
        Serial.println(playerCard1);

        playerState = WAITING_FOR_CARD_REMOVAL_AFTER_CARD_1;
        updatePlayerLCD();
      }
      break;

    case WAITING_FOR_CARD_REMOVAL_AFTER_CARD_1:
      if (scannedCard == "None") {
        Serial.println("Card 1 removed. Ready for card 2.");

        playerState = WAITING_FOR_CARD_2;
        updatePlayerLCD();
      }
      break;

    case WAITING_FOR_CARD_2:
      if (scannedCard != "None" && scannedCard != playerCard1) {
        playerCard2 = scannedCard;

        Serial.print("Card 2 stored: ");
        Serial.println(playerCard2);

        sendCardsToMainBoard();

        playerState = SENDING_CARDS_TO_MAIN;
        updatePlayerLCD();
      }
      break;

    case SENDING_CARDS_TO_MAIN:
    case CARDS_SENT_WAITING_FOR_RESET:
    case SEND_FAILED:
      break;  // handled above, before scanCard()
  }
}
