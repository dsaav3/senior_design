import React from "react";

/**
 * OddsBar — displays win probability as an animated bar.
 * Props: odds (0–100), isActive (bool), isFolded (bool)
 */
const OddsBar = ({ odds = 0, isActive = true, isFolded = false }) => {
  const pct = Math.round(odds);

  const barColor = () => {
    if (isFolded) return "bg-gray-600";
    if (pct >= 70) return "bg-emerald-400";
    if (pct >= 40) return "bg-yellow-400";
    return "bg-red-400";
  };

  return (
    <div className="w-full">
      <div className="flex justify-between items-center mb-1">
        <span className="text-2xl text-white/50 font-body">Win odds</span>
        <span
          className="text-5xl font-bold font-mono"
          style={{ color: isFolded ? "#555" : "#d4a843" }}
        >
          {isFolded ? "FOLD" : `${pct}%`}
        </span>
      </div>
      <div className="h-4 bg-white/10 rounded-full overflow-hidden">
        <div
          className={`h-full rounded-full transition-all duration-700 ease-out ${barColor()}`}
          style={{ width: isFolded ? "0%" : `${pct}%` }}
        />
      </div>
    </div>
  );
};

export default OddsBar;
