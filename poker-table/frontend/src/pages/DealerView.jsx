import React, { useEffect, useState } from "react";
import { useParams, useNavigate } from "react-router-dom";
import PokerTable from "../components/PokerTable";
import BetControls from "../components/BetControls";
import SessionCodeDisplay from "../components/SessionCodeDisplay";
import useGameState from "../hooks/useGameState";
import { advancePhase, endSession, resetPlayer, joinSession as apiJoin, nextHand, undoAction } from "../api";

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
    <div className="min-h-screen flex flex-col">
      {/* Top bar */}
      <header
        className="flex items-center justify-between px-6 py-3 border-b"
        style={{ borderColor: "rgba(255,255,255,0.06)", background: "rgba(0,0,0,0.3)" }}
      >
        <div className="flex items-center gap-3">
          <span className="font-display text-lg" style={{ color: "#d4a843" }}>
            ♠ Dealer Control
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

      <div className="flex-1 flex flex-col lg:flex-row gap-4 p-4 overflow-auto">
        {/* ── Left: Table View ─────────────────────────────────────── */}
        <div className="flex-1 flex flex-col gap-4">
          <SessionCodeDisplay code={sessionCode} spectatorCount={spectatorCount} />
          <PokerTable gameState={session} />

          {/* Phase controls */}
          <div
            className="rounded-xl p-4 flex flex-col sm:flex-row items-center gap-3"
            style={{
              background: "rgba(255,255,255,0.03)",
              border: "1px solid rgba(255,255,255,0.07)",
            }}
          >
            <div className="flex-1">
              <div className="text-xs text-white/40 uppercase tracking-widest mb-1">
                Game Phase
              </div>
              <div className="flex gap-1.5 flex-wrap">
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
            </div>
            {nextPhase && (
              <button
                onClick={handleNextPhase}
                disabled={phaseLoading}
                className="px-6 py-2.5 rounded-xl text-sm font-bold transition-all active:scale-95 disabled:opacity-50"
                style={{
                  background: "linear-gradient(135deg, #d4a843, #e8c46a)",
                  color: "#1a0f07",
                }}
              >
                {phaseLoading ? "..." : `→ ${nextPhase.charAt(0).toUpperCase() + nextPhase.slice(1)}`}
              </button>
            )}
          </div>
        </div>

        {/* ── Right: Bet Controls per player ──────────────────────── */}
        <div
          className="lg:w-72 rounded-2xl p-4 space-y-3 overflow-y-auto"
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
              />
              {(player.action === "fold" || !player.isActive) && (
                <button
                  onClick={() => handleResetPlayer(player.seat)}
                  className="w-full text-xs py-1 rounded text-white/30 hover:text-white/60 transition-colors"
                >
                  ↺ Reset player
                </button>
              )}
            </div>
          ))}

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
                    Seat {p.seat} - {p.name}
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
    </div>
  );
};

export default DealerView;
