import React from "react";

// Maps suit names to symbols and colors
const SUIT_MAP = {
  hearts:   { symbol: "♥", color: "#c0392b", short: "H" },
  diamonds: { symbol: "♦", color: "#c0392b", short: "D" },
  clubs:    { symbol: "♣", color: "#1a1a1a", short: "C" },
  spades:   { symbol: "♠", color: "#1a1a1a", short: "S" },
};

// Display rank (10 stays as 10, others as-is)
const displayRank = (rank) => {
  if (rank === "10") return "10";
  return rank;
};

/**
 * PlayingCard — renders a realistic card face.
 * Props:
 *   rank: "A"|"2"..."K"
 *   suit: "hearts"|"diamonds"|"clubs"|"spades"
 *   faceDown: bool (show card back)
 *   size: "sm"|"md"|"lg"
 *   delay: number (animation delay in ms)
 */
const PlayingCard = ({ rank, suit, faceDown = false, size = "md", delay = 0 }) => {
  const sizeClasses = {
    sm: "w-10 h-14",
    md: "w-14 h-20",
    lg: "w-[118px] h-[172px]",
    xl: "w-28 h-40",
  };

  // No size shows a suit glyph next to the corner rank (see below) — the
  // freed-up space lets the rank itself run bigger.
  const textSizes = {
    sm: "text-sm",
    md: "text-lg",
    lg: "text-4xl",
    xl: "text-4xl",
  };

  const centerSizes = {
    sm: "text-lg",
    md: "text-2xl",
    lg: "text-7xl",
    xl: "text-7xl",
  };

  // Nudges the center suit down from dead-center, opening up clear space at
  // the top of the card for the (now bigger) corner rank so the two don't
  // visually crowd each other.
  const centerOffsets = {
    sm: "mt-1",
    md: "mt-2",
    lg: "mt-7",
    xl: "mt-7",
  };

  if (faceDown) {
    return (
      <div
        className={`${sizeClasses[size]} rounded-md shadow-card flex items-center justify-center relative overflow-hidden animate-deal`}
        style={{
          background: "linear-gradient(135deg, #1a237e 0%, #283593 50%, #1a237e 100%)",
          border: "1.5px solid rgba(255,255,255,0.15)",
          animationDelay: `${delay}ms`,
        }}
      >
        {/* Back pattern */}
        <div
          className="absolute inset-1 rounded opacity-30"
          style={{
            backgroundImage:
              "repeating-linear-gradient(45deg, rgba(255,255,255,0.15) 0px, rgba(255,255,255,0.15) 1px, transparent 1px, transparent 8px)",
          }}
        />
        <div className="text-white/20 text-xl">🂠</div>
      </div>
    );
  }

  if (!rank || !suit) {
    // Empty placeholder
    return (
      <div
        className={`${sizeClasses[size]} rounded-md border-2 border-dashed border-white/10 flex items-center justify-center`}
      >
        <span className="text-white/20 text-xs">?</span>
      </div>
    );
  }

  const suitInfo = SUIT_MAP[suit.toLowerCase()] || SUIT_MAP.spades;
  const r = displayRank(rank);

  return (
    <div
      className={`${sizeClasses[size]} rounded-md bg-white shadow-card relative flex items-center justify-center p-1 overflow-hidden animate-deal select-none`}
      style={{
        border: "1.5px solid rgba(0,0,0,0.12)",
        animationDelay: `${delay}ms`,
      }}
    >
      {/* Top-left corner — rank alone, no suit glyph next to it, so it can
          run as large as possible for readability at a glance */}
      <div
        className={`absolute top-1.5 left-2 leading-none ${textSizes[size]} font-bold font-mono`}
        style={{ color: suitInfo.color }}
      >
        {r}
      </div>

      {/* Center suit — nudged down from dead-center (see centerOffsets) so
          it doesn't crowd the bigger corner rank above it */}
      <div
        className={`${centerSizes[size]} ${centerOffsets[size]} leading-none`}
        style={{ color: suitInfo.color }}
      >
        {suitInfo.symbol}
      </div>
    </div>
  );
};

export default PlayingCard;
