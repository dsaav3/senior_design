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
 * Get pre-flop action order: UTG (left of BB) first, SB second-to-last, BB last.
 * @param {Number} buttonSeat - dealer button seat
 * @param {Array} players - array of player objects with seat numbers
 * @returns {Array} ordered seat numbers for pre-flop action
 */
const getPreFlopOrder = (buttonSeat, players) => {
  const allSeats = [1, 2, 3, 4];
  const sbIndex = allSeats.indexOf((buttonSeat % 4) + 1);
  const bbIndex = (sbIndex + 1) % 4;
  const utgIndex = (bbIndex + 1) % 4;

  const order = [];
  let i = utgIndex;
  while (order.length < allSeats.length) {
    order.push(allSeats[i]);
    i = (i + 1) % allSeats.length;
  }
  return order;
};

/**
 * Get post-flop action order: SB first, clockwise, button last.
 * @param {Number} buttonSeat - dealer button seat
 * @param {Number} smallBlindSeat - small blind seat
 * @param {Array} players - array of player objects with seat numbers
 * @returns {Array} ordered seat numbers for post-flop action
 */
const getPostFlopOrder = (buttonSeat, smallBlindSeat, players) => {
  const allSeats = [1, 2, 3, 4];
  const sbIndex = allSeats.indexOf(smallBlindSeat);
  
  const order = [];
  let i = sbIndex;
  while (order.length < allSeats.length) {
    order.push(allSeats[i]);
    i = (i + 1) % allSeats.length;
  }
  return order;
};

/**
 * Get next seat clockwise, skipping eliminated players (chipCount === 0).
 * @param {Number} fromSeat - starting seat number
 * @param {Array} players - array of player objects
 * @returns {Number} next active seat with chips
 */
const getNextActiveSeat = (fromSeat, players) => {
  const allSeats = [1, 2, 3, 4];
  const startIndex = allSeats.indexOf(fromSeat);
  let nextIndex = (startIndex + 1) % 4;
  let attempts = 0;

  while (attempts < 4) {
    const seat = allSeats[nextIndex];
    const player = players.find((p) => p.seat === seat);
    if (player && player.chipCount > 0) {
      return seat;
    }
    nextIndex = (nextIndex + 1) % 4;
    attempts++;
  }

  // Fallback: return any non-zero player, or first seat
  const nonZero = players.find((p) => p.chipCount > 0);
  return nonZero ? nonZero.seat : 1;
};

/**
 * Creates a fresh session with 4 player slots.
 * Throws if a session is already active.
 * Accepts chipCounts, smallBlind, and bigBlind parameters.
 */
const createSession = async (playerNames = [], chipCounts = [], smallBlind = 10, bigBlind = 20, mode = "training") => {
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

  // Initial positions: button=1, SB=2, BB=3
  const session = new Session({
    sessionCode: code,
    players,
    status: "waiting",
    buttonSeat: 1,
    smallBlindSeat: 2,
    bigBlindSeat: 3,
    smallBlind,
    bigBlind,
    mode: mode === "pro" ? "pro" : "training",
    turnOrder: getPreFlopOrder(1, players),
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
 * Advances activePlayerSeat to the next active (non-folded, non-eliminated) player in turnOrder.
 * Skips folded players, all-in players, and eliminated players (chipCount === 0).
 */
const advanceTurn = (session) => {
  const currentIndex = session.turnOrder.indexOf(session.activePlayerSeat);
  let nextIndex = (currentIndex + 1) % session.turnOrder.length;
  let attempts = 0;

  while (attempts < session.turnOrder.length) {
    const nextSeat = session.turnOrder[nextIndex];
    const nextPlayer = session.players.find((p) => p.seat === nextSeat);
    if (nextPlayer && nextPlayer.isActive && nextPlayer.action !== "all-in" && nextPlayer.chipCount > 0) {
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
 * Uses session.smallBlindSeat and session.bigBlindSeat.
 * Adds blind amounts to session.pot.
 * Sets activePlayerSeat to first active player in turnOrder (skipping all-in and eliminated players)
 */
const applyBlinds = (session) => {
  const smallBlindSeat = session.smallBlindSeat;
  const bigBlindSeat = session.bigBlindSeat;

  const smallBlindPlayer = session.players.find((p) => p.seat === smallBlindSeat);
  const bigBlindPlayer = session.players.find((p) => p.seat === bigBlindSeat);

  if (smallBlindPlayer) {
    const sbAmount = Math.min(session.smallBlind, smallBlindPlayer.chipCount);
    smallBlindPlayer.chipCount -= sbAmount;
    smallBlindPlayer.bet = sbAmount;
    session.pot += sbAmount;
    if (smallBlindPlayer.chipCount === 0 && sbAmount > 0) {
      smallBlindPlayer.action = "all-in";
    }
  }

  if (bigBlindPlayer) {
    const bbAmount = Math.min(session.bigBlind, bigBlindPlayer.chipCount);
    bigBlindPlayer.chipCount -= bbAmount;
    bigBlindPlayer.bet = bbAmount;
    session.pot += bbAmount;
    if (bigBlindPlayer.chipCount === 0 && bbAmount > 0) {
      bigBlindPlayer.action = "all-in";
    }
  }

  session.currentBet = session.bigBlind;
  
  // Find first active player with chips and not all-in
  let activePlayerFound = false;
  for (const seat of session.turnOrder) {
    const player = session.players.find((p) => p.seat === seat);
    if (player && player.isActive && player.chipCount > 0 && player.action !== "all-in") {
      session.activePlayerSeat = seat;
      activePlayerFound = true;
      break;
    }
  }
  
  // Fallback: if no such player, just use first in turnOrder
  if (!activePlayerFound) {
    session.activePlayerSeat = session.turnOrder[0];
  }
};

module.exports = {
  generateCode,
  getActiveSession,
  createSession,
  applyBetAction,
  endSession,
  advanceTurn,
  applyBlinds,
  getPreFlopOrder,
  getPostFlopOrder,
  getNextActiveSeat,
};
