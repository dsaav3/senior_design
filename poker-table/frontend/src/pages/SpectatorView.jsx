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
    <div className="min-h-screen flex flex-col">
      {/* Header */}
      <header
        className="flex items-center justify-between px-6 py-3 border-b"
        style={{ borderColor: "rgba(255,255,255,0.06)", background: "rgba(0,0,0,0.3)" }}
      >
        <span className="font-display text-lg" style={{ color: "#d4a843" }}>
          ♠ Smart Poker Table
        </span>
        <div className="flex items-center gap-3">
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

      <div className="flex-1 flex flex-col items-center p-4 gap-4 w-full">
        <SessionCodeDisplay code={sessionCode} spectatorCount={spectatorCount} />

        {session.lastEsp32Update && (
          <div className="text-xs text-white/25 font-mono">
            Last ESP32 update: {new Date(session.lastEsp32Update).toLocaleTimeString()}
          </div>
        )}

        <div className="w-full">
          <PokerTable gameState={session} />
        </div>

        {/* Spectator watermark */}
        <div className="text-xs text-white/20 font-mono tracking-widest uppercase mt-2">
          Spectator Mode · Read Only
        </div>
      </div>
    </div>
  );
};

export default SpectatorView;
