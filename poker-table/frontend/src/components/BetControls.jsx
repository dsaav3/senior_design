import React, { useState } from "react";
import { submitAction } from "../api";

const ACTIONS = [
  { key: "call",   label: "Call",   color: "bg-blue-600 hover:bg-blue-500",   hotkey: "C" },
  { key: "check",  label: "Check",  color: "bg-green-700 hover:bg-green-600", hotkey: "X" },
  { key: "fold",   label: "Fold",   color: "bg-gray-700 hover:bg-gray-600",   hotkey: "F" },
  { key: "all-in", label: "All-In", color: "bg-red-700 hover:bg-red-600",     hotkey: "A" },
];

/**
 * BetControls — dealer control panel for a single player seat.
 * Props: player, sessionCode, onUpdate(updatedSession), isActiveTurn, currentBet
 */
const BetControls = ({ player, sessionCode, onUpdate, isActiveTurn = false, currentBet = 0 }) => {
  const [raiseAmount, setRaiseAmount] = useState("");
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState("");

  if (!player) return null;

  const handleAction = async (action) => {
    if (loading) return;
    setLoading(true);
    setError("");
    try {
      const amount = action === "raise" ? Number(raiseAmount) || 0 : 0;
      const res = await submitAction(sessionCode, player.seat, action, amount);
      if (onUpdate) onUpdate(res.data.session);
      if (action === "raise") setRaiseAmount("");
    } catch (err) {
      setError(err.response?.data?.message || err.response?.data?.error || "Action failed");
    } finally {
      setLoading(false);
    }
  };

  const isFolded = player.action === "fold" || !player.isActive;
  const canCheck = currentBet === 0 || player.bet === currentBet;
  const isCheckDisabled = !canCheck;

  return (
    <div
      className="rounded-xl p-3 space-y-2"
      style={{
        background: "rgba(255,255,255,0.04)",
        border: "1px solid rgba(255,255,255,0.08)",
      }}
    >
      <div className="flex items-center justify-between">
        <span className="font-display text-sm text-white/80">
          Seat {player.seat} — {player.name}
        </span>
        {player.action && player.action !== "waiting" && (
          <span
            className={`text-xs font-mono px-2 py-0.5 rounded ${
              isFolded ? "bg-gray-700 text-gray-400" : "bg-gold-400/20 text-gold-300"
            }`}
            style={{ color: isFolded ? undefined : "#d4a843" }}
          >
            {player.action.toUpperCase()}
          </span>
        )}
      </div>

      {isFolded ? (
        <div className="text-xs text-gray-500 text-center py-1">Folded</div>
      ) : !isActiveTurn ? (
        <div className="text-xs text-white/40 text-center py-2 italic">Waiting for turn...</div>
      ) : (
        <>
          {/* Standard action buttons */}
          <div className="grid grid-cols-2 gap-1.5">
            {ACTIONS.map((a) => {
              const isDisabled = loading || (a.key === "check" && isCheckDisabled);
              return (
                <button
                  key={a.key}
                  onClick={() => handleAction(a.key)}
                  disabled={isDisabled}
                  title={a.key === "check" && isCheckDisabled ? "Cannot check — a bet has been made" : ""}
                  className={`${a.color} text-white text-xs font-semibold py-1.5 px-2 rounded-lg transition-all duration-150 disabled:opacity-40 active:scale-95`}
                >
                  {a.label}
                </button>
              );
            })}
          </div>

          {/* Raise amount */}
          <div className="flex gap-1.5">
            <input
              type="number"
              value={raiseAmount}
              onChange={(e) => setRaiseAmount(e.target.value)}
              placeholder="Raise $"
              disabled={loading}
              className="flex-1 bg-white/5 border border-white/10 rounded-lg px-2 py-1.5 text-xs text-white placeholder:text-white/30 focus:outline-none focus:border-gold-400 disabled:opacity-50"
              style={{ "--tw-border-opacity": 1 }}
              min="1"
            />
            <button
              onClick={() => handleAction("raise")}
              disabled={loading || !raiseAmount}
              className="bg-yellow-600 hover:bg-yellow-500 text-white text-xs font-semibold px-3 py-1.5 rounded-lg transition-all disabled:opacity-40 active:scale-95"
            >
              Raise
            </button>
          </div>
        </>
      )}

      {error && (
        <p className="text-red-400 text-xs bg-red-500/10 px-2 py-1.5 rounded border border-red-500/20">
          {error}
        </p>
      )}
    </div>
  );
};

export default BetControls;
