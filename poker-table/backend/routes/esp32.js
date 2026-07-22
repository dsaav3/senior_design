const express = require("express");
const router = express.Router();
const Session = require("../models/Session");

// ── Middleware: validate ESP32 API key ────────────────────────────────────────
const validateApiKey = (req, res, next) => {
  const key = req.headers["x-api-key"];
  if (!key || key !== process.env.ESP32_API_KEY) {
    return res.status(401).json({ error: "Unauthorized: invalid API key" });
  }
  next();
};

// ── Card token parsing ────────────────────────────────────────────────────────
// The main ESP32 board identifies cards from its NFC card map as compact
// tokens like "Ac", "10d", "Kh" (rank + suit letter). This converts those
// tokens (or already-structured { rank, suit } objects) into the CardSchema
// shape the Session model expects. Unrecognized tokens (e.g. "None", raw
// unmapped UIDs) are dropped rather than stored.
const SUIT_CODES = { c: "clubs", d: "diamonds", h: "hearts", s: "spades" };

const parseCardToken = (token) => {
  if (token && typeof token === "object" && token.rank && token.suit) {
    return { rank: String(token.rank), suit: String(token.suit).toLowerCase() };
  }

  if (typeof token !== "string") return null;

  const raw = token.trim();
  if (!raw || raw.toLowerCase() === "none") return null;

  const match = raw.match(/^(10|[2-9]|[AKQJakqj])([cdhsCDHS])$/);
  if (!match) return null;

  const suit = SUIT_CODES[match[2].toLowerCase()];
  if (!suit) return null;

  return { rank: match[1].toUpperCase(), suit };
};

// ── POST /api/esp32/update ────────────────────────────────────────────────────
/**
 * Main endpoint the ESP32 calls every time game state changes.
 *
 * Expected payload:
 * {
 *   "sessionCode": "ABC123",         // must match active session
 *   "phase": "flop",                 // optional phase override
 *   "players": [
 *     {
 *       "seat": 1,
 *       "cards": ["Ac", "Kh"],        // compact tokens, or [{rank,suit}, ...]
 *       "winOdds": 67.4,
 *       "handName": "Two Pair"
 *     },
 *     ...
 *   ],
 *   "communityCards": ["7c", "Jd", "2s"]
 * }
 */
router.post("/update", validateApiKey, async (req, res) => {
  try {
    const { sessionCode, players, communityCards, phase } = req.body;

    if (!sessionCode) {
      return res.status(400).json({ error: "sessionCode is required" });
    }

    const session = await Session.findOne({
      sessionCode: sessionCode.toUpperCase(),
      status: { $in: ["waiting", "active"] },
    });

    if (!session) {
      return res.status(404).json({
        error: "SESSION_NOT_FOUND",
        message: "No active session with that code",
      });
    }

    // Update community cards
    if (Array.isArray(communityCards)) {
      session.communityCards = communityCards.map(parseCardToken).filter(Boolean).slice(0, 5);
    }

    // Update player cards + odds from ESP32 data
    if (Array.isArray(players)) {
      players.forEach((esp32Player) => {
        const player = session.players.find((p) => p.seat === esp32Player.seat);
        if (player) {
          if (Array.isArray(esp32Player.cards)) {
            player.cards = esp32Player.cards.map(parseCardToken).filter(Boolean).slice(0, 2);
          }
          if (typeof esp32Player.winOdds === "number") {
            player.winOdds = Math.min(100, Math.max(0, esp32Player.winOdds));
          }
          if (typeof esp32Player.handName === "string") {
            player.handName = esp32Player.handName;
          }
        }
      });
    }

    // Optionally update phase
    if (phase) {
      session.phase = phase;
      session.status = "active";
    }

    session.lastEsp32Update = new Date();
    await session.save();

    // Broadcast to all connected clients in this session's room
    const io = req.app.get("io");
    io.to(session.sessionCode).emit("game:stateUpdate", session);

    res.json({ success: true, updated: new Date().toISOString() });
  } catch (err) {
    console.error("ESP32 update error:", err);
    res.status(500).json({ error: err.message });
  }
});

// ── GET /api/esp32/state ──────────────────────────────────────────────────────
/**
 * Polled by the ESP32 to learn the dealer-controlled game phase (pre-flop,
 * flop, turn, river, showdown), which seats have folded, and the session's
 * training/pro mode. The website is the source of truth for all of these —
 * the dealer advances phase via POST /api/game/phase, folds a player via
 * POST /api/game/action, and mode is fixed at session creation — so the
 * ESP32 reads them here instead of tracking any of it itself. A folded
 * seat's hole cards must be excluded from equity/odds calculations on the
 * ESP32. `mode` replaces the old physical 5V switch to the player LCDs:
 * in "pro" mode, the main board still computes everything as normal, but
 * tags each PlayerHandInfoPacket so the player board shows "PRO MODE"
 * instead of the hand odds, without ever cutting LCD power (which was
 * causing an inconsistent I2C hang on some player boards).
 */
router.get("/state", validateApiKey, async (req, res) => {
  try {
    const session = await Session.findOne({
      status: { $in: ["waiting", "active"] },
    });

    if (!session) {
      return res.status(404).json({ error: "SESSION_NOT_FOUND" });
    }

    res.json({
      success: true,
      sessionCode: session.sessionCode,
      status: session.status,
      phase: session.phase,
      mode: session.mode,
      communityCards: session.communityCards,
      players: session.players.map((p) => ({
        seat: p.seat,
        name: p.name,
        chipCount: p.chipCount,
        bet: p.bet,
        action: p.action,
        folded: !p.isActive,
      })),
    });
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// ── GET /api/esp32/ping ───────────────────────────────────────────────────────
// ESP32 can call this to verify connectivity before a game
router.get("/ping", validateApiKey, (req, res) => {
  res.json({ status: "online", timestamp: new Date().toISOString() });
});

module.exports = router;
