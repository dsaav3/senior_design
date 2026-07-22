const mongoose = require("mongoose");

// ── Sub-schemas ───────────────────────────────────────────────────────────────

const CardSchema = new mongoose.Schema(
  {
    rank: { type: String, required: true }, // "A","2"..."K"
    suit: { type: String, required: true }, // "hearts","diamonds","clubs","spades"
  },
  { _id: false }
);

const PlayerSchema = new mongoose.Schema(
  {
    seat: { type: Number, required: true },        // 1–4
    name: { type: String, default: "" },
    cards: { type: [CardSchema], default: [] },    // 2 hole cards
    winOdds: { type: Number, default: 0 },         // 0–100 percentage
    handName: { type: String, default: "" },       // e.g. "Two Pair", from the ESP32
    action: {
      type: String,
      enum: ["waiting", "call", "check", "raise", "fold", "all-in", ""],
      default: "waiting",
    },
    bet: { type: Number, default: 0 },
    isActive: { type: Boolean, default: true },    // false = folded
    chipCount: { type: Number, default: 1000 },    // Player's current chip stack
  },
  { _id: false }
);

// ── Main Session Schema ───────────────────────────────────────────────────────

const SessionSchema = new mongoose.Schema(
  {
    sessionCode: {
      type: String,
      required: true,
      unique: true,
      uppercase: true,
      trim: true,
    },
    status: {
      type: String,
      enum: ["waiting", "active", "ended"],
      default: "waiting",
    },
    phase: {
      type: String,
      enum: ["pre-flop", "flop", "turn", "river", "showdown", "idle"],
      default: "idle",
    },
    mode: {
      type: String,
      enum: ["training", "pro"],
      default: "training",
    },
    players: { type: [PlayerSchema], default: [] },
    communityCards: { type: [CardSchema], default: [] }, // up to 5
    pot: { type: Number, default: 0 },
    currentBet: { type: Number, default: 0 },
    activePlayerSeat: { type: Number, default: 1 },      // whose turn it is
    dealerSeat: { type: Number, default: 1 },
    buttonSeat: { type: Number, default: 1 },            // Dealer button position
    smallBlindSeat: { type: Number, default: 2 },        // Small blind player seat
    bigBlindSeat: { type: Number, default: 3 },          // Big blind player seat
    smallBlind: { type: Number, default: 10 },           // Small blind amount
    bigBlind: { type: Number, default: 20 },             // Big blind amount
    turnOrder: { type: [Number], default: [1, 2, 3, 4] }, // Ordered seat numbers for turn rotation
    lastAggressorSeat: { type: Number, default: null },  // Last player who bet/raised (for re-opening action)
    actionHistory: { type: Array, default: [] },         // History of actions for undo feature
    spectatorCount: { type: Number, default: 0 },
    lastEsp32Update: { type: Date, default: null },
  },
  { timestamps: true }
);

module.exports = mongoose.model("Session", SessionSchema);
