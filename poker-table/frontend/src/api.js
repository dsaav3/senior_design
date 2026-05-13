import axios from "axios";

const API_URL = import.meta.env.VITE_API_URL || "http://localhost:3001";
const api = axios.create({ baseURL: `${API_URL}/api` });

export const checkTableStatus = () => api.get("/game/status");

export const createSession = (playerNames, chipCounts = [], smallBlind = 10, bigBlind = 20) =>
  api.post("/game/create", { playerNames, chipCounts, smallBlind, bigBlind });

export const joinSession = (code) =>
  api.get(`/game/join/${code}`);

export const submitAction = (sessionCode, seat, action, raiseAmount = 0) =>
  api.post("/game/action", { sessionCode, seat, action, raiseAmount });

export const advancePhase = (sessionCode, phase) =>
  api.post("/game/phase", { sessionCode, phase });

export const endSession = (sessionCode) =>
  api.post("/game/end", { sessionCode });

export const resetPlayer = (sessionCode, seat) =>
  api.post("/game/reset-player", { sessionCode, seat });

export const nextHand = (sessionCode, winningSeat) =>
  api.post("/game/next-hand", { sessionCode, winningSeat });

export const undoAction = (sessionCode) =>
  api.post("/game/undo", { sessionCode });
