import React, { useEffect, useState } from "react";
import { useParams, useNavigate } from "react-router-dom";
import PokerTable from "../components/PokerTable";
import BetControls from "../components/BetControls";
import SessionCodeDisplay from "../components/SessionCodeDisplay";
import MobileDealerPanel from "../components/MobileDealerPanel";
import useGameState from "../hooks/useGameState";
import { advancePhase, endSession, resetPlayer, joinSession as apiJoin, nextHand, undoAction, buyback } from "../api";

const PHASES = ["pre-flop", "flop", "turn", "river", "showdown"];

const PHASE_NEXT = {
  idle: "pre-flop",
  "pre-flop": "flop",
  flop: "turn",
  turn: "river",
  river: "showdown",
  showdown: null,
};

const DealerView = () => {
  const { sessionCode } = useParams();
  const navigate = useNavigate();
  const [initialSession, setInitialSession] = useState(null);
  const [phaseLoading, setPhaseLoading] = useState(false);
  const [endLoading, setEndLoading] = useState(false);
  const [nextHandLoading, setNextHandLoading] = useState(false);
  const [undoLoading, setUndoLoading] = useState(false);
  const [winningSeat, setWinningSeat] = useState(null);
  const [error, setError] = useState("");
  const [buybackSeat, setBuybackSeat] = useState(null);
  const [buybackAmount, setBuybackAmount] = useState("1000");

  // Load initial state from API
  useEffect(() => {
    if (!sessionCode) return;
    
    const timeout = setTimeout(() => {
      if (!initialSession) {
        setError("Session load timeout. Please try again.");
      }
    }, 5000);

    apiJoin(sessionCode)
      .then((res) => {
        clearTimeout(timeout);
        setInitialSession(res.data.session);
      })
      .catch((err) => {
        clearTimeout(timeout);
        console.error("Failed to join session:", err);
        setError("Failed to load session. Redirecting...");
        setTimeout(() => navigate("/"), 2000);
      });

    return () => clearTimeout(timeout);
  }, [sessionCode, navigate]);

  // Handle beforeunload to end session
  useEffect(() => {
    const handleBeforeUnload = () => {
      if (sessionCode) {
        navigator.sendBeacon(
          `${import.meta.env.VITE_API_URL || "http://localhost:3001"}/api/game/end`,
          JSON.stringify({ sessionCode })
        );
      }
    };

    window.addEventListener("beforeunload", handleBeforeUnload);
    return () => window.removeEventListener("beforeunload", handleBeforeUnload);
  }, [sessionCode]);

  const { gameState, setGameState, spectatorCount, connectionStatus } =
    useGameState(sessionCode, initialSession);

  const session = gameState || initialSession;

  const handleEndSession = async () => {
    if (!confirm("Are you sure you want to end this game session?")) return;
    setEndLoading(true);
    try {
      await endSession(sessionCode);
      navigate("/");
    } catch (err) {
      setError("Failed to end session");
      setEndLoading(false);
    }
  };

  const handleResetPlayer = async (seat) => {
    try {
      const res = await resetPlayer(sessionCode, seat);
      setGameState(res.data.session);
    } catch (err) {
      setError("Failed to reset player");
    }
  };

  const handleNextHand = async () => {
    if (!winningSeat) {
      setError("Please select a winning player");
      return;
    }
    setNextHandLoading(true);
    setError("");
    try {
      const res = await nextHand(sessionCode, winningSeat);
      // Issue 3: Update local gameState with returned session so chip counts reflect immediately
      setGameState(res.data.session);
      setWinningSeat(null);
    } catch (err) {
      setError("Failed to advance to next hand");
    } finally {
      setNextHandLoading(false);
    }
  };

  const handleUndo = async () => {
    setUndoLoading(true);
    setError("");
    try {
      const res = await undoAction(sessionCode);
      setGameState(res.data.session);
    } catch (err) {
      setError(err.response?.data?.error || "Failed to undo action");
    } finally {
      setUndoLoading(false);
    }
  };

  const handleNextPhase = async () => {
    const nextPhaseValue = PHASE_NEXT[session.phase];
    if (!nextPhaseValue) return;
    setPhaseLoading(true);
    setError("");
    try {
      const res = await advancePhase(sessionCode, nextPhaseValue);
      setGameState(res.data.session);
    } catch (err) {
      setError("Failed to advance phase");
    } finally {
      setPhaseLoading(false);
    }
  };

  const handleBuyback = async (seat) => {
    const amount = parseInt(buybackAmount);
    if (!amount || amount <= 0) {
      setError("Enter a valid chip amount");
      return;
    }
    try {
      const res = await buyback(sessionCode, seat, amount);
      setGameState(res.data.session);
      setBuybackSeat(null);
      setBuybackAmount("1000");
      setError("");
    } catch (err) {
      setError("Failed to process buyback");
    }
  };

  if (!session) {
    return (
      <div className="min-h-screen flex flex-col items-center justify-center gap-4">
        <span className="text-white/50 animate-pulse font-mono">Loading session...</span>
        {error && (
          <p className="text-red-400 text-sm">{error}</p>
        )}
      </div>
    );
  }

  const nextPhase = PHASE_NEXT[session.phase];

  return (
    <div className="flex flex-col full-viewport-height overflow-hidden">
      {/* Top bar - fixed height */}
      <header
        className="flex items-center justify-between px-6 py-1 border-b flex-shrink-0"
        style={{ borderColor: "rgba(255,255,255,0.06)", background: "rgba(0,0,0,0.3)" }}
      >
        <div className="flex items-center gap-3">
          <span className="font-display text-lg" style={{ color: "#d4a843" }}>
            ♠︎ Dealer Control
          </span>
          <span
            className={`text-xs font-mono px-2 py-0.5 rounded-full ${
              connectionStatus === "connected"
                ? "bg-green-500/20 text-green-400"
                : "bg-yellow-500/20 text-yellow-400"
            }`}
          >
            {connectionStatus === "connected" ? "● Live" : "○ " + connectionStatus}
          </span>
          {/* Issue 6: Display button and blind seats */}
          {session.buttonSeat !== undefined && (
            <span className="text-xs font-mono text-white/40 ml-2">
              Button: Seat {session.buttonSeat} | SB: Seat {session.smallBlindSeat} | BB: Seat {session.bigBlindSeat}
            </span>
          )}
        </div>
        <div className="flex items-center gap-3">
          {session.lastEsp32Update && (
            <span className="text-xs text-white/30 font-mono hidden sm:block">
              ESP32: {new Date(session.lastEsp32Update).toLocaleTimeString()}
            </span>
          )}
          <button
            onClick={handleEndSession}
            disabled={endLoading}
            className="text-xs px-3 py-1.5 rounded-lg font-semibold transition-all"
            style={{
              background: "rgba(239,68,68,0.1)",
              border: "1px solid rgba(239,68,68,0.3)",
              color: "#f87171",
            }}
          >
            {endLoading ? "Ending..." : "End Session"}
          </button>
        </div>
      </header>

      {/* Below `sm` (phones), the corner-table layout doesn't have room to
          stay legible — the felt shrinks to a sliver while controls end up
          scrolled far below the cards they refer to — so MobileDealerPanel
          replaces it there instead. `sm` and up (tablets/desktop) render
          exactly the previous layout, untouched. */}
      <div className="sm:hidden flex-1 min-h-0 overflow-y-auto flex flex-col gap-1 p-2">
        <SessionCodeDisplay code={sessionCode} spectatorCount={spectatorCount} compact />
        <MobileDealerPanel
          session={session}
          sessionCode={sessionCode}
          setGameState={setGameState}
          handleResetPlayer={handleResetPlayer}
          setBuybackSeat={setBuybackSeat}
          handleUndo={handleUndo}
          undoLoading={undoLoading}
          handleNextPhase={handleNextPhase}
          phaseLoading={phaseLoading}
          nextPhase={nextPhase}
          winningSeat={winningSeat}
          setWinningSeat={setWinningSeat}
          handleNextHand={handleNextHand}
          nextHandLoading={nextHandLoading}
          error={error}
        />
      </div>

      {/* Main content area - flex-1 to fill remaining space. min-h-0 (and
          min-w-0 on the row) is required at every level so these flex items
          actually shrink to the available space instead of growing to fit
          their (fixed-size) children. */}
      <div className="hidden sm:flex flex-1 min-h-0 flex-col lg:flex-row gap-2 p-2 overflow-hidden">
        {/* ── Left: Table View ─────────────────────────────────────── */}
        <div className="flex-1 min-h-0 min-w-0 flex flex-col gap-1 overflow-hidden">
          <SessionCodeDisplay code={sessionCode} spectatorCount={spectatorCount} compact />
          <div className="flex-1 min-h-0 overflow-hidden">
            <PokerTable gameState={session} />
          </div>
        </div>

        {/* ── Right: Bet Controls per player (scrollable) ──────────────────────────────────── */}
        <div
          className="max-h-[45vh] lg:max-h-none lg:w-72 rounded-2xl p-4 space-y-3 overflow-y-auto flex-shrink-0"
          style={{
            background: "rgba(0,0,0,0.2)",
            border: "1px solid rgba(255,255,255,0.06)",
          }}
        >
          <div className="flex items-center justify-between mb-2">
            <h2 className="font-display text-base text-white/80">Bet Controls</h2>
            <span className="text-xs text-white/30">Pot: ${session.pot}</span>
          </div>

          {/* Undo button */}
          <button
            onClick={handleUndo}
            disabled={undoLoading || !session.actionHistory?.length}
            className="w-full text-xs py-2 rounded-lg font-semibold transition-all disabled:opacity-40"
            style={{
              background: "rgba(212,168,67,0.1)",
              border: "1px solid rgba(212,168,67,0.3)",
              color: "#d4a843",
            }}
          >
            {undoLoading ? "Undoing..." : "↶ Undo Last Action"}
          </button>

          {session.players.map((player) => (
            <div key={player.seat} className="space-y-1">
              <BetControls
                player={player}
                sessionCode={sessionCode}
                onUpdate={setGameState}
                isActiveTurn={player.seat === session.activePlayerSeat}
                currentBet={session.currentBet}
                players={session.players}
              />
              {/* OUT - Show buy-back option (all-in players have chipCount === 0 too, but keep a nonzero bet) */}
              {player.chipCount === 0 && player.bet === 0 && (
                <button
                  onClick={() => setBuybackSeat(player.seat)}
                  className="w-full text-xs py-1 rounded text-red-400 hover:text-red-300 transition-colors"
                >
                  💰 Buy Back
                </button>
              )}
              {/* Folded - Show reset option */}
              {player.chipCount > 0 && (player.action === "fold" || !player.isActive) && (
                <button
                  onClick={() => handleResetPlayer(player.seat)}
                  className="w-full text-xs py-1 rounded text-white/30 hover:text-white/60 transition-colors"
                >
                  ↺ Reset player
                </button>
              )}
            </div>
          ))}

          {/* Phase controls */}
          <div
            className="rounded-xl p-3 flex flex-col gap-2 border-t border-white/10"
            style={{
              background: "rgba(255,255,255,0.02)",
              border: "1px solid rgba(255,255,255,0.07)",
            }}
          >
            <div className="text-xs text-white/40 uppercase tracking-widest font-semibold">
              Game Phase
            </div>
            <div className="flex gap-1 flex-wrap">
              {PHASES.map((p) => (
                <span
                  key={p}
                  className="text-xs px-2 py-0.5 rounded font-mono"
                  style={{
                    background:
                      session.phase === p
                        ? "rgba(212,168,67,0.25)"
                        : "rgba(255,255,255,0.05)",
                    color: session.phase === p ? "#d4a843" : "rgba(255,255,255,0.3)",
                    border: `1px solid ${
                      session.phase === p
                        ? "rgba(212,168,67,0.4)"
                        : "rgba(255,255,255,0.06)"
                    }`,
                  }}
                >
                  {p}
                </span>
              ))}
            </div>
            {nextPhase && (
              <button
                onClick={handleNextPhase}
                disabled={phaseLoading}
                className="w-full px-4 py-2 rounded-lg text-xs font-bold transition-all active:scale-95 disabled:opacity-50"
                style={{
                  background: "linear-gradient(135deg, #d4a843, #e8c46a)",
                  color: "#1a0f07",
                }}
              >
                {phaseLoading ? "..." : `→ ${nextPhase.charAt(0).toUpperCase() + nextPhase.slice(1)}`}
              </button>
            )}
          </div>

          {/* Next hand section - appears in showdown */}
          {session.phase === "showdown" && (
            <div className="pt-3 border-t border-white/10 space-y-2">
              <label className="text-xs text-white/40 uppercase tracking-widest font-semibold">
                Next Hand - Choose Winner
              </label>
              <select
                value={winningSeat || ""}
                onChange={(e) => setWinningSeat(Number(e.target.value))}
                className="w-full bg-white/5 border border-white/10 rounded-lg px-3 py-2 text-sm text-white focus:outline-none focus:border-gold-400"
              >
                <option value="">Select winning player...</option>
                {session.players.map((p) => (
                  <option key={p.seat} value={p.seat}>
                    Seat {p.seat} - {p.name} (Stack: ${p.chipCount})
                  </option>
                ))}
              </select>
              <button
                onClick={handleNextHand}
                disabled={nextHandLoading || !winningSeat}
                className="w-full py-2 rounded-lg text-sm font-semibold transition-all disabled:opacity-50"
                style={{
                  background: "linear-gradient(135deg, #d4a843, #e8c46a)",
                  color: "#1a0f07",
                }}
              >
                {nextHandLoading ? "Starting..." : "Start Next Hand"}
              </button>
            </div>
          )}

          {error && <p className="text-red-400 text-xs text-center">{error}</p>}
        </div>
      </div>

      {/* Buyback Modal */}
      {buybackSeat !== null && (
        <div
          className="fixed inset-0 flex items-center justify-center z-50"
          style={{ background: "rgba(0,0,0,0.5)" }}
          onClick={() => setBuybackSeat(null)}
        >
          <div
            className="rounded-2xl p-6 w-96 space-y-4"
            style={{
              background: "rgba(10,34,24,0.95)",
              border: "2px solid rgba(212,168,67,0.3)",
            }}
            onClick={(e) => e.stopPropagation()}
          >
            <h2 className="text-lg font-display text-center text-white">
              Buy Back - Seat {buybackSeat}
            </h2>
            <div>
              <label className="text-sm text-white/60 block mb-2">Chip Amount</label>
              <input
                type="number"
                value={buybackAmount}
                onChange={(e) => setBuybackAmount(e.target.value)}
                className="w-full bg-white/5 border border-white/20 rounded-lg px-3 py-2 text-white focus:outline-none focus:border-gold-400"
                placeholder="Enter chip amount"
              />
            </div>
            <div className="flex gap-2">
              <button
                onClick={() => setBuybackSeat(null)}
                className="flex-1 py-2 rounded-lg text-sm font-semibold"
                style={{
                  background: "rgba(255,255,255,0.05)",
                  border: "1px solid rgba(255,255,255,0.1)",
                  color: "#ffffff",
                }}
              >
                Cancel
              </button>
              <button
                onClick={() => handleBuyback(buybackSeat)}
                className="flex-1 py-2 rounded-lg text-sm font-semibold"
                style={{
                  background: "linear-gradient(135deg, #d4a843, #e8c46a)",
                  color: "#1a0f07",
                }}
              >
                Confirm Buy Back
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
};

export default DealerView;
