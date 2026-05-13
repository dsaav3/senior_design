import React from "react";
import PlayerSeat from "./PlayerSeat";
import PlayingCard from "./PlayingCard";

const PHASE_LABELS = {
  idle: "Waiting to start",
  "pre-flop": "Pre-Flop",
  flop: "Flop",
  turn: "Turn",
  river: "River",
  showdown: "Showdown",
};

/**
 * PokerTable — the main felt table layout.
 * Shows 4 player seats (corners) and community cards in center on an oval table.
 * Props: gameState (session object)
 */
const PokerTable = ({ gameState }) => {
  if (!gameState) return null;

  const { players = [], communityCards = [], pot = 0, phase = "idle", currentBet = 0, activePlayerSeat = 1, turnOrder = [] } = gameState;

  // Determine blind seats from turnOrder
  const smallBlindSeat = turnOrder[1];
  const bigBlindSeat = turnOrder[2];

  // Seat positions: p1=seat1, p2=seat2, p3=seat3, p4=seat4
  const [p1, p2, p3, p4] = [
    players.find((p) => p.seat === 1),
    players.find((p) => p.seat === 2),
    players.find((p) => p.seat === 3),
    players.find((p) => p.seat === 4),
  ];

  return (
    <div className="relative w-full flex flex-col items-center gap-4 py-4">
      {/* Top row: Player 1 (LEFT) | spacer | Player 2 (RIGHT) */}
      <div className="w-full flex justify-between items-start px-2 gap-4">
        {/* Player 1 — TOP LEFT */}
        <div className="w-44 flex-shrink-0">
          <PlayerSeat 
            player={p1} 
            position="top-left" 
            cardSize="sm" 
            isActiveTurn={p1?.seat === activePlayerSeat}
            blind={p1?.seat === smallBlindSeat ? "small" : p1?.seat === bigBlindSeat ? "big" : null}
          />
        </div>

        {/* Spacer */}
        <div className="flex-1" />

        {/* Player 2 — TOP RIGHT */}
        <div className="w-44 flex-shrink-0">
          <PlayerSeat 
            player={p2} 
            position="top-right" 
            cardSize="sm" 
            isActiveTurn={p2?.seat === activePlayerSeat}
            blind={p2?.seat === smallBlindSeat ? "small" : p2?.seat === bigBlindSeat ? "big" : null}
          />
        </div>
      </div>

      {/* Middle: Oval Felt Table */}
      <div
        className="relative w-full flex flex-col items-center justify-center px-6"
        style={{
          background: "radial-gradient(ellipse at 50% 40%, #164a36 0%, #0d2d20 60%, #0a2218 100%)",
          boxShadow:
            "inset 0 0 60px rgba(0,0,0,0.5), 0 0 0 10px #2c1a0e, 0 0 0 14px #1a0f07, 0 8px 32px rgba(0,0,0,0.6)",
          borderRadius: "50%",
          height: "280px",
          aspectRatio: "2 / 1",
        }}
      >
        {/* Table rail highlight */}
        <div
          className="absolute inset-0 pointer-events-none"
          style={{
            borderRadius: "50%",
            background:
              "linear-gradient(180deg, rgba(255,255,255,0.04) 0%, transparent 40%)",
          }}
        />

        {/* Phase badge */}
        <div
          className="absolute top-4 left-1/2 -translate-x-1/2 px-3 py-1 rounded-full text-xs font-mono font-semibold tracking-widest uppercase"
          style={{
            background: "rgba(212,168,67,0.15)",
            border: "1px solid rgba(212,168,67,0.3)",
            color: "#d4a843",
          }}
        >
          {PHASE_LABELS[phase] || phase}
        </div>

        {/* Community cards */}
        <div className="flex gap-2 items-center justify-center mt-2">
          {Array.from({ length: 5 }, (_, i) => {
            const card = communityCards[i];
            return (
              <PlayingCard
                key={i}
                rank={card?.rank}
                suit={card?.suit}
                faceDown={false}
                size="md"
                delay={i * 80}
              />
            );
          })}
        </div>

        {/* Pot */}
        <div className="mt-4 flex flex-col items-center gap-1">
          <span className="text-xs text-white/40 font-body uppercase tracking-widest">
            Pot
          </span>
          <span className="text-xl font-display font-bold" style={{ color: "#d4a843" }}>
            ${pot.toLocaleString()}
          </span>
          {currentBet > 0 && (
            <span className="text-xs text-white/50 font-mono">
              Current bet: ${currentBet}
            </span>
          )}
        </div>
      </div>

      {/* Bottom row: Player 4 (LEFT) | spacer | Player 3 (RIGHT) */}
      <div className="w-full flex justify-between items-end px-2 gap-4">
        {/* Player 4 — BOTTOM LEFT */}
        <div className="w-44 flex-shrink-0">
          <PlayerSeat 
            player={p4} 
            position="bottom-left" 
            cardSize="sm" 
            isActiveTurn={p4?.seat === activePlayerSeat}
            blind={p4?.seat === smallBlindSeat ? "small" : p4?.seat === bigBlindSeat ? "big" : null}
          />
        </div>

        {/* Spacer */}
        <div className="flex-1" />

        {/* Player 3 — BOTTOM RIGHT */}
        <div className="w-44 flex-shrink-0">
          <PlayerSeat 
            player={p3} 
            position="bottom-right" 
            cardSize="sm" 
            isActiveTurn={p3?.seat === activePlayerSeat}
            blind={p3?.seat === smallBlindSeat ? "small" : p3?.seat === bigBlindSeat ? "big" : null}
          />
        </div>
      </div>
    </div>
  );
};

export default PokerTable;
