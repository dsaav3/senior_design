import React, { useState } from "react";

/**
 * SessionCodeDisplay — shows the session code with copy button.
 * Props: code (string), spectatorCount (number)
 */
const SessionCodeDisplay = ({ code, spectatorCount = 0, compact = false }) => {
  const [copied, setCopied] = useState(false);

  const handleCopy = async () => {
    try {
      await navigator.clipboard.writeText(code);
      setCopied(true);
      setTimeout(() => setCopied(false), 2000);
    } catch (_) {
      // fallback
    }
  };

  return (
    <div
      className={`rounded-2xl flex flex-col items-center animate-pulse-gold flex-shrink-0 ${
        compact ? "px-5 py-1 gap-0" : "px-6 py-4 gap-2"
      }`}
      style={{
        background: "rgba(212,168,67,0.08)",
        border: "1px solid rgba(212,168,67,0.3)",
      }}
    >
      <span className="text-xs text-white/40 uppercase tracking-widest font-body">
        Session Code
      </span>
      <span
        className={`font-mono font-bold tracking-[0.3em] ${compact ? "text-2xl" : "text-4xl"}`}
        style={{ color: "#d4a843" }}
      >
        {code}
      </span>
      <div className={`flex items-center gap-3 ${compact ? "" : "mt-1"}`}>
        <button
          onClick={handleCopy}
          className="text-xs px-3 py-1 rounded-full transition-all duration-150"
          style={{
            background: copied ? "rgba(52,211,153,0.2)" : "rgba(212,168,67,0.15)",
            border: `1px solid ${copied ? "rgba(52,211,153,0.5)" : "rgba(212,168,67,0.3)"}`,
            color: copied ? "#34d399" : "#d4a843",
          }}
        >
          {copied ? "✓ Copied!" : "Copy Code"}
        </button>
        <span className="text-xs text-white/30 font-body">
          👁 {spectatorCount} watching
        </span>
      </div>
    </div>
  );
};

export default SessionCodeDisplay;
