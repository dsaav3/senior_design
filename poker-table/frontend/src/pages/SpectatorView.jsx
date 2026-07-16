import React, { useEffect, useState } from "react";
import { useParams, useNavigate } from "react-router-dom";
import PokerTable from "../components/PokerTable";
import SessionCodeDisplay from "../components/SessionCodeDisplay";
import useGameState from "../hooks/useGameState";
import { joinSession as apiJoin } from "../api";

const SpectatorView = () => {
  const { sessionCode } = useParams();
  const navigate = useNavigate();
  const [initialSession, setInitialSession] = useState(null);
  const [notFound, setNotFound] = useState(false);

  useEffect(() => {
    apiJoin(sessionCode)
      .then((res) => setInitialSession(res.data.session))
      .catch(() => setNotFound(true));
  }, [sessionCode]);

  const { gameState, spectatorCount, connectionStatus } = useGameState(
    sessionCode,
    initialSession
  );

  // Listen for session end
  useEffect(() => {
    // If session ends, redirect home after a short delay
    if (gameState?.status === "ended") {
      setTimeout(() => navigate("/"), 3000);
    }
  }, [gameState?.status]);

  if (notFound) {
    return (
      <div className="min-h-screen flex flex-col items-center justify-center gap-4">
        <p className="text-white/50 font-mono">Session not found or has ended.</p>
        <button
          onClick={() => navigate("/")}
          className="text-sm px-4 py-2 rounded-lg"
          style={{ background: "rgba(212,168,67,0.15)", color: "#d4a843" }}
        >
          Back to Home
        </button>
      </div>
    );
  }

  if (!gameState && !initialSession) {
    return (
      <div className="min-h-screen flex items-center justify-center">
        <span className="text-white/50 animate-pulse font-mono">Loading game...</span>
      </div>
    );
  }

  const session = gameState || initialSession;

  if (session?.status === "ended") {
    return (
      <div className="min-h-screen flex flex-col items-center justify-center gap-3">
        <span className="text-4xl">🃏</span>
        <h2 className="font-display text-2xl" style={{ color: "#d4a843" }}>
          Game Over
        </h2>
        <p className="text-white/40 text-sm">Returning to home...</p>
      </div>
    );
  }

  return (
    <div className="flex flex-col" style={{ height: "100vh", overflow: "hidden" }}>
      {/* Header - fixed height */}
      <header
        className="flex items-center justify-between px-6 py-1 border-b flex-shrink-0"
        style={{ borderColor: "rgba(255,255,255,0.06)", background: "rgba(0,0,0,0.3)" }}
      >
        <span className="font-display text-lg" style={{ color: "#d4a843" }}>
          ♠ Smart Poker Table
        </span>
        <div className="flex items-center gap-3">
          <span className="text-xs text-white/20 font-mono tracking-widest uppercase hidden md:inline">
            Spectator Mode · Read Only
          </span>
          {session.lastEsp32Update && (
            <span className="text-xs text-white/25 font-mono hidden sm:inline">
              Last ESP32 update: {new Date(session.lastEsp32Update).toLocaleTimeString()}
            </span>
          )}
          <span
            className={`text-xs font-mono px-2 py-0.5 rounded-full ${
              connectionStatus === "connected"
                ? "bg-green-500/20 text-green-400"
                : "bg-yellow-500/20 text-yellow-400"
            }`}
          >
            {connectionStatus === "connected" ? "● Live" : "○ Reconnecting..."}
          </span>
          <button
            onClick={() => navigate("/")}
            className="text-xs text-white/30 hover:text-white/60 transition-colors"
          >
            ← Leave
          </button>
        </div>
      </header>

      {/* Main content area - flex-1 to fill remaining space. min-h-0 is required
          so this flex item actually shrinks to the space left by the header
          instead of growing to fit its (fixed-size) children. The ESP32
          timestamp and spectator watermark now live in the header row (free
          horizontal room there) instead of separate vertical lines here, so
          all of this area's height goes to the table. */}
      <div className="flex-1 min-h-0 flex flex-col items-center p-1 gap-1 w-full overflow-hidden">
        <SessionCodeDisplay code={sessionCode} spectatorCount={spectatorCount} compact />

        <div className="w-full flex-1 min-h-0 overflow-hidden">
          <PokerTable gameState={session} variant="spectator" />
        </div>
      </div>
    </div>
  );
};

export default SpectatorView;
