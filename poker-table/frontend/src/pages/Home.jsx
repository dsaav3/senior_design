import React, { useState, useEffect } from "react";
import { useNavigate } from "react-router-dom";
import { checkTableStatus, createSession, joinSession } from "../api";

const PHASES = ["pre-flop", "flop", "turn", "river", "showdown"];

const Home = () => {
  const navigate = useNavigate();
  const [tableStatus, setTableStatus] = useState(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState("");

  // Create session form
  const [playerNames, setPlayerNames] = useState(["", "", "", ""]);
  const [chipCounts, setChipCounts] = useState([1000, 1000, 1000, 1000]);
  const [smallBlind, setSmallBlind] = useState(10);
  const [bigBlind, setBigBlind] = useState(20);
  const [creating, setCreating] = useState(false);

  // Join session form
  const [joinCode, setJoinCode] = useState("");
  const [joining, setJoining] = useState(false);

  useEffect(() => {
    fetchStatus();
  }, []);

  const fetchStatus = async () => {
    try {
      setLoading(true);
      const res = await checkTableStatus();
      setTableStatus(res.data);
    } catch (err) {
      setError("Cannot reach the server. Make sure the backend is running.");
    } finally {
      setLoading(false);
    }
  };

  const handleCreate = async (e) => {
    e.preventDefault();
    setCreating(true);
    setError("");
    try {
      const res = await createSession(playerNames, chipCounts, smallBlind, bigBlind);
      navigate(`/dealer/${res.data.session.sessionCode}`);
    } catch (err) {
      if (err.response?.data?.error === "TABLE_IN_USE") {
        setError("The table is currently in use. Join as a spectator instead.");
        await fetchStatus();
      } else {
        setError(err.response?.data?.message || "Failed to create session.");
      }
    } finally {
      setCreating(false);
    }
  };

  const handleJoin = async (e) => {
    e.preventDefault();
    if (!joinCode.trim()) return;
    setJoining(true);
    setError("");
    try {
      const res = await joinSession(joinCode.trim().toUpperCase());
      navigate(`/spectate/${res.data.session.sessionCode}`);
    } catch (err) {
      setError("Session not found. Check the code and try again.");
    } finally {
      setJoining(false);
    }
  };

  const handleNameChange = (i, val) => {
    const updated = [...playerNames];
    updated[i] = val;
    setPlayerNames(updated);
  };

  const handleChipCountChange = (i, val) => {
    const updated = [...chipCounts];
    updated[i] = Math.max(0, Number(val) || 0);
    setChipCounts(updated);
  };

  return (
    <div className="full-viewport-height overflow-y-auto flex flex-col items-center p-6">
      <div className="w-full max-w-lg m-auto">
      {/* Header */}
      <div className="text-center mb-10">
        <h1 className="font-display text-3xl sm:text-5xl font-bold mb-2" style={{ color: "#d4a843" }}>
          ♠︎ Smart Poker Table
        </h1>
        <p className="text-white/40 font-body text-sm tracking-wider uppercase">
          RFID-powered live poker tracking
        </p>
      </div>

      {loading ? (
        <div className="text-white/50 animate-pulse font-mono text-center">Connecting to server...</div>
      ) : (
        <div className="w-full space-y-6">
          {/* Table status */}
          <div
            className="rounded-xl px-4 py-3 text-center text-sm font-mono"
            style={{
              background: tableStatus?.tableAvailable
                ? "rgba(52,211,153,0.1)"
                : "rgba(239,68,68,0.1)",
              border: `1px solid ${tableStatus?.tableAvailable ? "rgba(52,211,153,0.3)" : "rgba(239,68,68,0.3)"}`,
              color: tableStatus?.tableAvailable ? "#34d399" : "#f87171",
            }}
          >
            {tableStatus?.tableAvailable
              ? "✓ Table is available"
              : `⚠ Table in use — Session: ${tableStatus?.session?.sessionCode}`}
          </div>

          {/* ── Create Session ────────────────────────────────────────────── */}
          {tableStatus?.tableAvailable && (
            <div
              className="rounded-2xl p-6 space-y-4"
              style={{
                background: "rgba(255,255,255,0.03)",
                border: "1px solid rgba(255,255,255,0.08)",
              }}
            >
              <h2 className="font-display text-xl text-white/90">Start as Dealer</h2>
              <form onSubmit={handleCreate} className="space-y-3">
                {/* Player names and chip counts */}
                <div className="grid grid-cols-1 sm:grid-cols-2 gap-2">
                  {playerNames.map((name, i) => (
                    <div key={i} className="flex flex-col gap-1">
                      <input
                        type="text"
                        value={name}
                        onChange={(e) => handleNameChange(i, e.target.value)}
                        placeholder={`Player ${i + 1} name`}
                        maxLength={20}
                        className="bg-white/5 border border-white/10 rounded-lg px-3 py-2 text-sm text-white placeholder:text-white/25 focus:outline-none focus:border-gold-400 transition-colors"
                      />
                      <input
                        type="number"
                        value={chipCounts[i]}
                        onChange={(e) => handleChipCountChange(i, e.target.value)}
                        placeholder="Chip count"
                        min="0"
                        className="bg-white/5 border border-white/10 rounded-lg px-3 py-2 text-sm text-white placeholder:text-white/25 focus:outline-none focus:border-gold-400 transition-colors"
                      />
                    </div>
                  ))}
                </div>

                {/* Blind amounts */}
                <div className="grid grid-cols-2 gap-2">
                  <div className="flex flex-col gap-1">
                    <label className="text-xs text-white/40 uppercase tracking-widest font-semibold">
                      Small Blind
                    </label>
                    <input
                      type="number"
                      value={smallBlind}
                      onChange={(e) => setSmallBlind(Math.max(1, Number(e.target.value) || 10))}
                      min="1"
                      className="bg-white/5 border border-white/10 rounded-lg px-3 py-2 text-sm text-white placeholder:text-white/25 focus:outline-none focus:border-gold-400 transition-colors"
                    />
                  </div>
                  <div className="flex flex-col gap-1">
                    <label className="text-xs text-white/40 uppercase tracking-widest font-semibold">
                      Big Blind
                    </label>
                    <input
                      type="number"
                      value={bigBlind}
                      onChange={(e) => setBigBlind(Math.max(1, Number(e.target.value) || 20))}
                      min="1"
                      className="bg-white/5 border border-white/10 rounded-lg px-3 py-2 text-sm text-white placeholder:text-white/25 focus:outline-none focus:border-gold-400 transition-colors"
                    />
                  </div>
                </div>

                <button
                  type="submit"
                  disabled={creating}
                  className="w-full py-3 rounded-xl font-semibold text-sm tracking-wide transition-all duration-200 active:scale-98 disabled:opacity-50"
                  style={{
                    background: "linear-gradient(135deg, #d4a843, #e8c46a)",
                    color: "#1a0f07",
                  }}
                >
                  {creating ? "Creating session..." : "Create Game Session"}
                </button>
              </form>
            </div>
          )}

          {/* ── Join as Spectator ─────────────────────────────────────────── */}
          <div
            className="rounded-2xl p-6 space-y-4"
            style={{
              background: "rgba(255,255,255,0.03)",
              border: "1px solid rgba(255,255,255,0.08)",
            }}
          >
            <h2 className="font-display text-xl text-white/90">Join as Spectator</h2>
            <form onSubmit={handleJoin} className="flex gap-2">
              <input
                type="text"
                value={joinCode}
                onChange={(e) => setJoinCode(e.target.value.toUpperCase())}
                placeholder="Enter 6-digit code"
                maxLength={6}
                className="flex-1 min-w-0 bg-white/5 border border-white/10 rounded-lg px-3 py-2 text-sm text-white placeholder:text-white/25 focus:outline-none focus:border-gold-400 font-mono tracking-widest uppercase"
              />
              <button
                type="submit"
                disabled={joining || joinCode.length < 4}
                className="px-5 py-2 rounded-lg text-sm font-semibold transition-all active:scale-95 disabled:opacity-40"
                style={{
                  background: "rgba(212,168,67,0.15)",
                  border: "1px solid rgba(212,168,67,0.3)",
                  color: "#d4a843",
                }}
              >
                {joining ? "Joining..." : "Join"}
              </button>
            </form>

            {/* Quick-join if table in use */}
            {!tableStatus?.tableAvailable && tableStatus?.session && (
              <button
                onClick={() => navigate(`/spectate/${tableStatus.session.sessionCode}`)}
                className="w-full py-2 rounded-xl text-sm font-semibold transition-all"
                style={{
                  background: "rgba(212,168,67,0.1)",
                  border: "1px solid rgba(212,168,67,0.2)",
                  color: "#d4a843",
                }}
              >
                Watch Current Game →
              </button>
            )}
          </div>

          {error && (
            <p className="text-red-400 text-sm text-center font-body">{error}</p>
          )}
        </div>
      )}
      </div>
    </div>
  );
};

export default Home;
