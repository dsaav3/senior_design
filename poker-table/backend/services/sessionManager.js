const Session = require("../models/Session");

// ── Helpers ───────────────────────────────────────────────────────────────────

/**
 * Generates a random 6-character alphanumeric session code.
 */
const generateCode = () => {
  const chars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // no confusable chars
  let code = "";
  for (let i = 0; i < 6; i++) {
    code += chars[Math.floor(Math.random() * chars.length)];
  }
  return code;
};

/**
 * Returns the single active session, or null.
 */
const getActiveSession = async () => {
  return Session.findOne({ status: { $in: ["waiting", "active"] } });
};

/**
 * Creates a fresh session with 4 player slots.
 * Throws if a session is already active.
 * Accepts chipCounts, smallBlind, and bigBlind parameters.
 */
const createSession = async (playerNames = [], chipCounts = [], smallBlind = 10, bigBlind = 20) => {
  const existing = await getActiveSession();
  if (existing) {
    throw new Error("TABLE_IN_USE");
  }

  let code;
  let attempts = 0;
  // Ensure unique code (virtually guaranteed on first try)
  while (attempts < 10) {
    code = generateCode();
    const collision = await Session.findOne({ sessionCode: code });
    if (!collision) break;
    attempts++;
  }

  const players = Array.from({ length: 4 }, (_, i) => ({
    seat: i + 1,
    name: playerNames[i] || `Player ${i + 1}`,
    cards: [],
    winOdds: 0,
    action: "waiting",
    bet: 0,
    isActive: true,
    chipCount: chipCounts[i] || 1000, // Use provided chip count or default to 1000
  }));

  const session = new Session({
    sessionCode: code,
    players,
    status: "waiting",
    smallBlind,
    bigBlind,
    turnOrder: [1, 2, 3, 4],
  });

  await session.save();
  return session;
};

/**
 * Applies a dealer bet action to a player in the active session.
 */
const applyBetAction = async (sessionCode, seat, action, raiseAmount = 0) => {
  const session = await Session.findOne({ sessionCode });
  if (!session) throw new Error("SESSION_NOT_FOUND");

  const player = session.players.find((p) => p.seat === seat);
  if (!player) throw new Error("PLAYER_NOT_FOUND");

  player.action = action;

  if (action === "fold") {
    player.isActive = false;
  }

  if (action === "call") {
    player.bet = session.currentBet;
  }

  if (action === "raise") {
    const newBet = session.currentBet + raiseAmount;
    session.currentBet = newBet;
    player.bet = newBet;
  }

  if (action === "all-in") {
    player.action = "all-in";
  }

  // Update pot
  session.pot = session.players.reduce((sum, p) => sum + (p.bet || 0), 0);

  await session.save();
  return session;
};

/**
 * Ends the current session.
 */
const endSession = async (sessionCode) => {
  const session = await Session.findOne({ sessionCode });
  if (!session) throw new Error("SESSION_NOT_FOUND");
  session.status = "ended";
  await session.save();
  return session;
};

/**
 * Advances activePlayerSeat to the next active (non-folded) player in turnOrder.
 * Skips folded players.
 */
const advanceTurn = (session) => {
  const currentIndex = session.turnOrder.indexOf(session.activePlayerSeat);
  let nextIndex = (currentIndex + 1) % session.turnOrder.length;
  let attempts = 0;

  while (attempts < session.turnOrder.length) {
    const nextSeat = session.turnOrder[nextIndex];
    const nextPlayer = session.players.find((p) => p.seat === nextSeat);
    if (nextPlayer && nextPlayer.isActive) {
      session.activePlayerSeat = nextSeat;
      return;
    }
    nextIndex = (nextIndex + 1) % session.turnOrder.length;
    attempts++;
  }
  // If no active player found, set to first seat
  session.activePlayerSeat = session.turnOrder[0];
};

/**
 * Applies blinds at the start of a hand.
 * Small blind: player at turnOrder[1], Big blind: player at turnOrder[2]
 * Sets activePlayerSeat to turnOrder[0] (first to act pre-flop)
 */
const applyBlinds = (session) => {
  const smallBlindSeat = session.turnOrder[1];
  const bigBlindSeat = session.turnOrder[2];

  const smallBlindPlayer = session.players.find((p) => p.seat === smallBlindSeat);
  const bigBlindPlayer = session.players.find((p) => p.seat === bigBlindSeat);

  if (smallBlindPlayer) {
    const sbAmount = Math.min(session.smallBlind, smallBlindPlayer.chipCount);
    smallBlindPlayer.chipCount -= sbAmount;
    smallBlindPlayer.bet = sbAmount;
    if (smallBlindPlayer.chipCount === 0 && sbAmount > 0) {
      smallBlindPlayer.action = "all-in";
    }
  }

  if (bigBlindPlayer) {
    const bbAmount = Math.min(session.bigBlind, bigBlindPlayer.chipCount);
    bigBlindPlayer.chipCount -= bbAmount;
    bigBlindPlayer.bet = bbAmount;
    if (bigBlindPlayer.chipCount === 0 && bbAmount > 0) {
      bigBlindPlayer.action = "all-in";
    }
  }

  session.currentBet = session.bigBlind;
  session.pot = session.players.reduce((sum, p) => sum + (p.bet || 0), 0);
  session.activePlayerSeat = session.turnOrder[0];
};

module.exports = {
  createSession,
  getActiveSession,
  applyBetAction,
  endSession,
  advanceTurn,
  applyBlinds,
};
