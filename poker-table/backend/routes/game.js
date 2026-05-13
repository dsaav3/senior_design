const express = require("express");
const router = express.Router();
const {
  createSession,
  getActiveSession,
  applyBetAction,
  endSession,
  advanceTurn,
  applyBlinds,
} = require("../services/sessionManager");

// ── GET /api/game/status ──────────────────────────────────────────────────────
// Check if table is in use and get current session info
router.get("/status", async (req, res) => {
  try {
    const session = await getActiveSession();
    if (!session) {
      return res.json({ tableAvailable: true, session: null });
    }
    return res.json({ tableAvailable: false, session });
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// ── POST /api/game/create ─────────────────────────────────────────────────────
// Dealer creates a new session
// Body: { playerNames, chipCounts?, smallBlind?, bigBlind? }
router.post("/create", async (req, res) => {
  try {
    const { playerNames, chipCounts = [], smallBlind = 10, bigBlind = 20 } = req.body;
    console.log("Creating session with:", { playerNames, chipCounts, smallBlind, bigBlind });
    
    const session = await createSession(playerNames || [], chipCounts, smallBlind, bigBlind);
    console.log("Session created:", session.sessionCode);
    
    // Apply blinds immediately after session creation
    session.phase = "pre-flop";
    applyBlinds(session);
    console.log("Blinds applied, saving...");
    
    await session.save();
    console.log("Session saved successfully");

    const io = req.app.get("io");
    io.emit("session:created", session);
    res.status(201).json({ success: true, session });
  } catch (err) {
    console.error("Error creating session:", err);
    if (err.message === "TABLE_IN_USE") {
      return res.status(409).json({
        error: "TABLE_IN_USE",
        message: "A session is already active. Only one table is supported.",
      });
    }
    res.status(500).json({ error: err.message });
  }
});

// ── GET /api/game/join/:code ──────────────────────────────────────────────────
// Spectator joins session by code
router.get("/join/:code", async (req, res) => {
  try {
    const Session = require("../models/Session");
    const session = await Session.findOne({
      sessionCode: req.params.code.toUpperCase(),
      status: { $in: ["waiting", "active"] },
    });
    if (!session) {
      return res.status(404).json({ error: "SESSION_NOT_FOUND", message: "No active session with that code." });
    }
    res.json({ success: true, session });
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// ── POST /api/game/action ─────────────────────────────────────────────────────
// Dealer submits a bet action
// Body: { sessionCode, seat, action, raiseAmount? }
router.post("/action", async (req, res) => {
  try {
    const { sessionCode, seat, action, raiseAmount } = req.body;
    if (!sessionCode || !seat || !action) {
      return res.status(400).json({ error: "Missing required fields: sessionCode, seat, action" });
    }

    const Session = require("../models/Session");
    const session = await Session.findOne({ sessionCode });
    if (!session) return res.status(404).json({ error: "SESSION_NOT_FOUND" });

    const player = session.players.find((p) => p.seat === Number(seat));
    if (!player) return res.status(400).json({ error: "PLAYER_NOT_FOUND" });

    // Validate check action
    if (action === "check") {
      if (session.currentBet > 0 && player.bet < session.currentBet) {
        return res.status(400).json({
          error: "INVALID_CHECK",
          message: "Cannot check — a bet has been made. You must call, raise, or fold.",
        });
      }
    }

    // Record action history snapshot before applying action
    const snapshot = {
      seat: player.seat,
      action: player.action,
      bet: player.bet,
      chipCount: player.chipCount,
      pot: session.pot,
      currentBet: session.currentBet,
      timestamp: new Date(),
    };
    session.actionHistory.push(snapshot);
    // Keep only last 10 entries
    if (session.actionHistory.length > 10) {
      session.actionHistory.shift();
    }

    // Apply action and handle chip deductions
    player.action = action;

    if (action === "fold") {
      player.isActive = false;
    } else if (action === "call") {
      const amountToCall = session.currentBet - player.bet;
      const amountDeducted = Math.min(amountToCall, player.chipCount);
      player.chipCount -= amountDeducted;
      player.bet = session.currentBet - (amountToCall - amountDeducted);
      if (player.chipCount === 0 && player.bet < session.currentBet) {
        player.action = "all-in";
      }
    } else if (action === "raise") {
      const newBet = session.currentBet + Number(raiseAmount);
      const totalRaise = newBet - player.bet;
      const amountDeducted = Math.min(totalRaise, player.chipCount);
      player.chipCount -= amountDeducted;
      player.bet = player.bet + amountDeducted;
      session.currentBet = player.bet;
      if (player.chipCount === 0 && player.bet < newBet) {
        player.action = "all-in";
      }
    } else if (action === "all-in") {
      player.chipCount = 0;
      player.action = "all-in";
    }

    // Update pot
    session.pot = session.players.reduce((sum, p) => sum + (p.bet || 0), 0);

    // Advance turn
    advanceTurn(session);

    await session.save();
    const io = req.app.get("io");
    io.to(sessionCode).emit("game:stateUpdate", session);
    res.json({ success: true, session });
  } catch (err) {
    if (err.message === "SESSION_NOT_FOUND") return res.status(404).json({ error: err.message });
    res.status(500).json({ error: err.message });
  }
});

// ── POST /api/game/phase ──────────────────────────────────────────────────────
// Dealer advances game phase (pre-flop → flop → turn → river → showdown)
// Body: { sessionCode, phase }
router.post("/phase", async (req, res) => {
  try {
    const { sessionCode, phase } = req.body;
    const Session = require("../models/Session");
    const session = await Session.findOne({ sessionCode });
    if (!session) return res.status(404).json({ error: "SESSION_NOT_FOUND" });

    session.phase = phase;
    session.status = "active";

    // Reset per-round bet tracking on new phase
    if (["flop", "turn", "river", "showdown"].includes(phase)) {
      session.players.forEach((p) => {
        if (p.isActive) {
          p.action = "waiting";
          p.bet = 0;
        }
      });
      session.currentBet = 0;

      // Reset activePlayerSeat to first active player in turnOrder (for new round)
      let activeFound = false;
      for (const seat of session.turnOrder) {
        const player = session.players.find((p) => p.seat === seat);
        if (player && player.isActive) {
          session.activePlayerSeat = seat;
          activeFound = true;
          break;
        }
      }
      if (!activeFound) {
        session.activePlayerSeat = session.turnOrder[0];
      }
    }

    await session.save();
    const io = req.app.get("io");
    io.to(sessionCode).emit("game:stateUpdate", session);
    res.json({ success: true, session });
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// ── POST /api/game/end ────────────────────────────────────────────────────────
// Dealer ends the session
// Body: { sessionCode }
// Supports both application/json and text/plain (from navigator.sendBeacon)
router.post("/end", async (req, res) => {
  try {
    let sessionCode = req.body?.sessionCode;
    
    // Handle text/plain from sendBeacon
    if (!sessionCode && req.body && typeof req.body === "string") {
      try {
        const parsed = JSON.parse(req.body);
        sessionCode = parsed.sessionCode;
      } catch (e) {
        // Ignore parse error
      }
    }

    if (!sessionCode) {
      return res.status(400).json({ error: "Missing sessionCode" });
    }

    const session = await endSession(sessionCode);
    const io = req.app.get("io");
    io.to(sessionCode).emit("game:ended", { sessionCode });
    io.emit("session:ended", { sessionCode });
    res.json({ success: true, session });
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// ── POST /api/game/reset-player ───────────────────────────────────────────────
// Reset a player's action/bet for a new round
// Body: { sessionCode, seat }
router.post("/reset-player", async (req, res) => {
  try {
    const { sessionCode, seat } = req.body;
    const Session = require("../models/Session");
    const session = await Session.findOne({ sessionCode });
    if (!session) return res.status(404).json({ error: "SESSION_NOT_FOUND" });

    const player = session.players.find((p) => p.seat === Number(seat));
    if (player) {
      player.action = "waiting";
      player.bet = 0;
      player.isActive = true;
    }
    await session.save();
    const io = req.app.get("io");
    io.to(sessionCode).emit("game:stateUpdate", session);
    res.json({ success: true, session });
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// ── POST /api/game/next-hand ──────────────────────────────────────────────────
// Transition to next hand after showdown
// Body: { sessionCode, winningSeat }
router.post("/next-hand", async (req, res) => {
  try {
    const { sessionCode, winningSeat } = req.body;
    const Session = require("../models/Session");
    const session = await Session.findOne({ sessionCode });
    if (!session) return res.status(404).json({ error: "SESSION_NOT_FOUND" });

    const winner = session.players.find((p) => p.seat === Number(winningSeat));
    if (winner) {
      winner.chipCount += session.pot;
    }

    // Rotate turnOrder left by 1
    const firstSeat = session.turnOrder.shift();
    session.turnOrder.push(firstSeat);

    // Reset all players
    session.players.forEach((p) => {
      p.isActive = true;
      p.action = "waiting";
      p.bet = 0;
      p.cards = [];
    });

    // Reset community cards and pot
    session.communityCards = [];
    session.pot = 0;
    session.currentBet = 0;
    session.phase = "pre-flop";

    // Re-apply blinds based on new turnOrder
    applyBlinds(session);

    await session.save();
    const io = req.app.get("io");
    io.to(sessionCode).emit("game:stateUpdate", session);
    res.json({ success: true, session });
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// ── POST /api/game/undo ───────────────────────────────────────────────────────
// Undo the last action
// Body: { sessionCode }
router.post("/undo", async (req, res) => {
  try {
    const { sessionCode } = req.body;
    const Session = require("../models/Session");
    const session = await Session.findOne({ sessionCode });
    if (!session) return res.status(404).json({ error: "SESSION_NOT_FOUND" });

    if (!session.actionHistory || session.actionHistory.length === 0) {
      return res.status(400).json({ error: "No actions to undo" });
    }

    const snapshot = session.actionHistory.pop();

    const player = session.players.find((p) => p.seat === snapshot.seat);
    if (player) {
      player.action = snapshot.action;
      player.bet = snapshot.bet;
      player.chipCount = snapshot.chipCount;
    }

    session.pot = snapshot.pot;
    session.currentBet = snapshot.currentBet;
    session.activePlayerSeat = snapshot.seat;

    await session.save();
    const io = req.app.get("io");
    io.to(sessionCode).emit("game:stateUpdate", session);
    res.json({ success: true, session });
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

module.exports = router;
