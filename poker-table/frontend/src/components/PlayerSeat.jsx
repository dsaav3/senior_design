import React from "react";
import PlayingCard from "./PlayingCard";
import OddsBar from "./OddsBar";

const ACTION_STYLES = {
  call:    { bg: "bg-blue-500/20",   border: "border-blue-400",   label: "CALL" },
  check:   { bg: "bg-green-500/20",  border: "border-green-400",  label: "CHECK" },
  raise:   { bg: "bg-yellow-500/20", border: "border-yellow-400", label: "RAISE" },
  fold:    { bg: "bg-gray-500/20",   border: "border-gray-500",   label: "FOLD" },
  "all-in":{ bg: "bg-red-500/20",    border: "border-red-400",    label: "ALL-IN" },
  waiting: { bg: "",                  border: "border-white/10",   label: "" },
};

/**
 * PlayerSeat — renders a player's position at the table.
 * Props: player, position, cardSize, isActiveTurn, role ("dealer"|"small-blind"|"big-blind"|null)
 */
const PlayerSeat = ({ player, position = "bottom", cardSize = "md", isActiveTurn = false, role = null }) => {
  if (!player) return null;

  const { name, cards = [], winOdds = 0, handName = "", action = "waiting", bet = 0, isActive = true, chipCount = 0 } = player;

  // Handle eliminated player (OUT state) — a player who went all-in also has
  // chipCount === 0, but keeps a nonzero bet for the rest of the hand, so
  // only treat them as OUT once both are zero.
  if (chipCount === 0 && bet === 0) {
    return (
      <div className="flex flex-col items-center gap-2">
        <div
          className="rounded-lg px-4 py-3 w-full text-center"
          style={{
            border: "2px solid rgba(239,68,68,0.5)",
            background: "rgba(239,68,68,0.1)",
          }}
        >
          <span className="font-display text-5xl text-white/90 block mb-1.5">
            {name || `Seat ${player.seat}`}
          </span>
          <span
            className="text-5xl font-bold px-3 py-1"
            style={{
              color: "#ef4444",
              background: "rgba(239,68,68,0.2)",
              borderRadius: "0.5rem",
              display: "inline-block",
            }}
          >
            OUT
          </span>
        </div>
      </div>
    );
  }

  const style = ACTION_STYLES[action] || ACTION_STYLES.waiting;
  const isFolded = action === "fold" || !isActive;
  const shouldHighlight = isActiveTurn && !isFolded;

  // Role badge text
  const roleLabel = role === "dealer" ? "D" : role === "small-blind" ? "SB" : role === "big-blind" ? "BB" : null;
  const roleColor = role === "dealer" 
    ? "rgba(255,255,255,0.2)" 
    : role === "small-blind"
    ? "rgba(59,130,246,0.3)"
    : role === "big-blind"
    ? "rgba(239,68,68,0.3)"
    : "transparent";

  return (
    <div
      className={`flex flex-col items-center gap-1 transition-all duration-300 ${
        isFolded ? "opacity-50" : "opacity-100"
      } ${shouldHighlight ? "scale-108" : "scale-100"}`}
    >
      {/* Cards with active turn glow (Issue 5 - intensified) */}
      <div 
        className={`flex gap-1.5 transition-all duration-300 ${shouldHighlight ? "scale-105" : "scale-100"}`}
        style={shouldHighlight ? {
          boxShadow: "0 0 0 3px #d4a843, 0 0 24px rgba(212,168,67,0.7)",
        } : {}}
      >
        {cards.length > 0 ? (
          cards.map((card, i) => (
            <PlayingCard
              key={i}
              rank={card.rank}
              suit={card.suit}
              size={cardSize}
              delay={i * 100}
            />
          ))
        ) : (
          <>
            <PlayingCard faceDown size={cardSize} />
            <PlayingCard faceDown size={cardSize} delay={100} />
          </>
        )}
      </div>

      {/* Active turn badge (Issue 5 - bright gold) */}
      {shouldHighlight && (
        <div
          className="text-3xl font-bold font-mono px-4 py-2 rounded-full animate-pulse-gold"
          style={{
            background: "rgba(212,168,67,0.3)",
            border: "1px solid #d4a843",
            color: "#d4a843",
          }}
        >
          ▶ TURN
        </div>
      )}

      {/* Name plate with role badges (Issue 6) */}
      <div
        className={`rounded-lg px-4 py-2 w-full transition-all duration-300 ${shouldHighlight ? "ring-2" : ""}`}
        style={{
          border: shouldHighlight
            ? "1px solid rgba(212,168,67,0.5)"
            : "1px solid rgba(255,255,255,0.1)",
          background: shouldHighlight
            ? "rgba(212,168,67,0.05)"
            : "rgba(255,255,255,0.02)",
          ringColor: "#d4a843",
        }}
      >
        {/* Header: Name + Role Badge + Action */}
        <div className="flex items-center justify-between mb-1.5 gap-2">
          <span className="font-display text-5xl text-white/90 truncate flex-1">
            {name || `Seat ${player.seat}`}
          </span>

          {/* Role badge (Issue 6) */}
          {roleLabel && (
            <div
              className="text-2xl font-bold font-mono px-3 py-1 rounded"
              style={{
                background: roleColor,
                border: "1px solid rgba(212,168,67,0.3)",
                color: role === "small-blind" ? "#3b82f6" : role === "big-blind" ? "#ef4444" : "#ffffff",
                minWidth: "50px",
                textAlign: "center",
              }}
            >
              {roleLabel}
            </div>
          )}

          {/* Action badge (Issue 5 - reduced intensity) */}
          {action && action !== "waiting" && (
            <span
              className="font-bold font-mono px-2.5 py-1 rounded"
              style={{
                background: "rgba(255,255,255,0.03)",
                border: "1px solid rgba(255,255,255,0.1)",
                color: isFolded ? "rgba(255,255,255,0.3)" : "#d4a843",
                fontSize: "1.6rem",
              }}
            >
              {style.label}
            </span>
          )}
        </div>

        {bet > 0 && !isFolded && (
          <div className="text-3xl text-gold-300 font-mono mb-1 leading-tight">Bet: ${bet}</div>
        )}

        <div className="text-3xl text-white/60 font-mono mb-1 leading-tight">Stack: ${player.chipCount || 0}</div>

        {handName && !isFolded && (
          <div className="text-3xl font-mono mb-1 leading-tight" style={{ color: "#d4a843" }}>
            Hand: {handName}
          </div>
        )}

        <OddsBar odds={winOdds} isActive={isActive} isFolded={isFolded} />
      </div>
    </div>
  );
};

export default PlayerSeat;
