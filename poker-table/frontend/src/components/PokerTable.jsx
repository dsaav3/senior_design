import React, { useLayoutEffect, useRef, useState } from "react";
import PlayerSeat from "./PlayerSeat";
import PlayingCard from "./PlayingCard";

export const PHASE_LABELS = {
  idle: "Waiting to start",
  "pre-flop": "Pre-Flop",
  flop: "Flop",
  turn: "Turn",
  river: "River",
  showdown: "Showdown",
};

// The felt's true natural (unscaled) content size, measured directly (via
// getBoundingClientRect() on each child, divided by the transform scale in
// effect at measurement time): the 5-card community row is 624px wide (the
// widest element), and the full stack (phase badge + gap + cards + gap +
// pot block) is 374px tall. The OLD FELT_BASE_WIDTH/HEIGHT below (397x273)
// were smaller than this on BOTH axes — the content was already overflowing
// its own reference box (and transitively the felt oval) before this fix;
// this was a real pre-existing bug, not just a "make it rounder" preference.
const NATURAL_CONTENT_WIDTH = 624;
const NATURAL_CONTENT_HEIGHT = 374;

// How much bigger than the natural content size the felt table's reference
// box is — i.e. how much margin the content has before it reaches the
// oval's edge. Deliberately more than a simple "fits the rectangle" margin
// (1.0) because an ellipse inscribed in a WxH rect covers LESS area than
// the rect — content spanning near-full width needs extra headroom to clear
// the curve, not just the bounding box.
const CONTENT_MARGIN = 1.18;

// Reference size the felt table's contents (phase badge, cards, pot) are
// laid out at, then uniformly scaled to match however big the table
// actually renders — so cards/text grow or shrink along with the table
// itself. = natural content size x CONTENT_MARGIN.
const FELT_BASE_WIDTH = NATURAL_CONTENT_WIDTH * CONTENT_MARGIN;
const FELT_BASE_HEIGHT = NATURAL_CONTENT_HEIGHT * CONTENT_MARGIN;

// The content's rendered (visual) scale factor — this is what determines
// how big the community cards/pot/phase-badge actually look, and it MUST
// stay exactly what it was before this change (210/273) so that visual size
// is unchanged, regardless of how FELT_BASE_HEIGHT/MIN_FELT_HEIGHT
// individually move. Expressed as a fraction (not a hand-rounded decimal)
// so the preserved ratio is exact rather than approximate.
const PRESERVED_CONTENT_SCALE = 210 / 273;
const MIN_FELT_HEIGHT = FELT_BASE_HEIGHT * PRESERVED_CONTENT_SCALE;

// The content's own natural aspect ratio — used as the felt's PREFERRED
// shape (e.g. deriving MIN_FELT_WIDTH, dealer's preferred/competing width
// target below) so a not-yet-stretched table stays proportioned like its
// contents rather than accidentally squeezed narrower than what's needed to
// enclose them with the same margin the height dimension gets.
const TARGET_ASPECT = NATURAL_CONTENT_WIDTH / NATURAL_CONTENT_HEIGHT;
const MIN_FELT_WIDTH = MIN_FELT_HEIGHT * TARGET_ASPECT;

// The felt is allowed to stretch wider than TARGET_ASPECT when there's
// genuinely free horizontal room (most visible in spectator mode, which has
// no sidebar competing for width) — up to MAX_FELT_ASPECT times its own
// height, rather than capping at the content's natural ratio and leaving a
// visible gap on the sides. A stretched-ellipse look is fine; a razor-thin
// one isn't, hence a real ceiling rather than removing the cap outright.
const MAX_FELT_ASPECT = 3;

// Space between the corner boxes and the felt table, and the outer padding
// around the whole layout. Kept tight so more of the available space goes
// to the felt table itself rather than to margins.
const GAP = 6;
// 4px read as uncomfortably tight against real devices' physical edges
// (reported on iPad) — nudged up slightly. Still small enough that it
// costs negligible budget compared to the felt/corner sizes involved.
const OUTER_PADDING = 10;

// Fixed "natural" width every corner box renders at before any shrink-to-fit
// scaling is applied.
export const CORNER_WIDTH = 460;

/**
 * FeltTable — the oval table surface, rendered at an exact caller-supplied
 * pixel size (computed by PokerTable below to exactly fill the leftover
 * space between the 4 corner boxes). Its contents are laid out at a fixed
 * reference size and uniformly scaled to match.
 */
const FeltTable = ({ left, top, width, height, phase, communityCards, pot, currentBet }) => {
  const scale = Math.max(0.05, Math.min(width / FELT_BASE_WIDTH, height / FELT_BASE_HEIGHT));

  return (
    <div
      className="absolute flex items-center justify-center"
      style={{
        left,
        top,
        width,
        height,
        background: "radial-gradient(ellipse at 50% 40%, #164a36 0%, #0d2d20 60%, #0a2218 100%)",
        boxShadow:
          "inset 0 0 60px rgba(0,0,0,0.5), 0 0 0 10px #2c1a0e, 0 0 0 14px #1a0f07, 0 8px 32px rgba(0,0,0,0.6)",
        borderRadius: "50%",
      }}
    >
      {/* Table rail highlight */}
      <div
        className="absolute inset-0 pointer-events-none"
        style={{
          borderRadius: "50%",
          background: "linear-gradient(180deg, rgba(255,255,255,0.04) 0%, transparent 40%)",
        }}
      />

      <div
        className="flex flex-col items-center justify-center relative flex-shrink-0"
        style={{
          width: FELT_BASE_WIDTH,
          height: FELT_BASE_HEIGHT,
          transform: `scale(${scale})`,
          transformOrigin: "center center",
        }}
      >
        {/* Phase badge — a normal flex child (not absolutely positioned) so
            it can never overlap the card row regardless of how big either
            one is; it just takes its place above the cards with a margin. */}
        <div
          className="px-5 py-2 rounded-full text-lg font-mono font-semibold tracking-widest uppercase mb-4"
          style={{
            background: "rgba(212,168,67,0.15)",
            border: "1px solid rgba(212,168,67,0.3)",
            color: "#d4a843",
          }}
        >
          {PHASE_LABELS[phase] || phase}
        </div>

        {/* Community cards */}
        <div className="flex gap-4 items-center justify-center">
          {Array.from({ length: 5 }, (_, i) => {
            const card = communityCards[i];
            return (
              <PlayingCard
                key={i}
                rank={card?.rank}
                suit={card?.suit}
                faceDown={false}
                size="xl"
                delay={i * 80}
              />
            );
          })}
        </div>

        {/* Pot */}
        <div className="mt-6 flex flex-col items-center gap-1.5">
          <span className="text-lg text-white/40 font-body uppercase tracking-widest">Pot</span>
          <span className="text-6xl font-display font-bold" style={{ color: "#d4a843" }}>
            ${pot.toLocaleString()}
          </span>
          {currentBet > 0 && (
            <span className="text-lg text-white/50 font-mono">Bet: ${currentBet}</span>
          )}
        </div>
      </div>
    </div>
  );
};

/**
 * PokerTable — 4 player seats pinned to the corners of whatever space this
 * component is given, with the felt table filling the leftover space between
 * them (computed in JS, not CSS flexible tracks — see the long comment below
 * for why).
 *
 * cornerScale is solved from the corners' full natural size (cards +
 * nameplate together) competing against the felt's preferred size — the same
 * floor→natural-size blend as before. How big the felt itself prefers to be
 * is controlled by `variant`: in "dealer" mode it's the previous fixed
 * reference size (MIN_FELT_HEIGHT/WIDTH); in "spectator" mode there's no
 * ceiling on it at all — it fills all the leftover space (capped only by
 * TARGET_ASPECT, so it stays a recognizable oval rather than a flat
 * stretched ellipse), since spectator view typically has much more room to
 * give it. Bigger player cards do cost a little cornerScale when space is
 * tight (a real, modest trade-off — see the comment above solveCornerScale).
 *
 * Note on why this isn't done with CSS Grid `1fr` / flexbox `flex-grow`:
 * those need a definite container height to distribute from. This component
 * is meant to fill whatever bounded space its parent already gives it
 * (SpectatorView's content area, or DealerView's left column) — which IS
 * definite (both views constrain it via a min-h-0 flex chain) — so plain
 * measurement + arithmetic here is enough; no uniform-scale-a-fixed-canvas
 * wrapper (like the old ScaleToFit) is needed anymore.
 *
 * Props: gameState (session object), variant ("dealer" | "spectator")
 */
const PokerTable = ({ gameState, variant = "dealer" }) => {
  const outerRef = useRef(null);
  const tlInnerRef = useRef(null);
  const trInnerRef = useRef(null);
  const blInnerRef = useRef(null);
  const brInnerRef = useRef(null);

  const [layout, setLayout] = useState({
    cornerScale: 1,
    feltLeft: 0,
    feltTop: 0,
    feltWidth: FELT_BASE_WIDTH,
    feltHeight: FELT_BASE_HEIGHT,
  });

  useLayoutEffect(() => {
    const outerEl = outerRef.current;
    if (!outerEl) return;

    const recompute = () => {
      const availW = outerEl.clientWidth;
      const availH = outerEl.clientHeight;
      if (!availW || !availH) return;

      const tlW = tlInnerRef.current?.offsetWidth || CORNER_WIDTH;
      const tlH = tlInnerRef.current?.offsetHeight || 0;
      const trW = trInnerRef.current?.offsetWidth || CORNER_WIDTH;
      const trH = trInnerRef.current?.offsetHeight || 0;
      const blW = blInnerRef.current?.offsetWidth || CORNER_WIDTH;
      const blH = blInnerRef.current?.offsetHeight || 0;
      const brW = brInnerRef.current?.offsetWidth || CORNER_WIDTH;
      const brH = brInnerRef.current?.offsetHeight || 0;

      // Spectator has no sidebar or bet-controls panel competing for room,
      // so it can afford a tighter gap/padding than dealer — a small, zero-
      // cost-to-boxes way to hand a bit more real budget straight to the
      // felt (this doesn't touch the corner-scale competition at all).
      const gap = variant === "spectator" ? 3 : GAP;
      const outerPadding = variant === "spectator" ? 2 : OUTER_PADDING;

      const budgetH = availH - gap * 2 - outerPadding * 2;
      const budgetW = availW - gap * 2 - outerPadding * 2;

      // Full corner assembly (cards + nameplate together) — this is the real
      // footprint that has to fit alongside the felt.
      const naturalLeftColW = Math.max(tlW, blW);
      const naturalRightColW = Math.max(trW, brW);
      const naturalTopRowH = Math.max(tlH, trH);
      const naturalBottomRowH = Math.max(blH, brH);

      // cornerScale is solved the same way as the last verified-good round
      // (a floor → natural-size blend, competing against the felt's
      // preferred size) using the REAL full assembly (cards included) — not
      // nameplate-only. An earlier draft tried nameplate-only specifically
      // so bigger cards couldn't touch cornerScale at all, but that let the
      // nameplate reach its full natural (223-264px) size — which is simply
      // too large to also fit bigger cards and a real table in the same
      // budget, so the felt got crushed to its hard floor regardless. Using
      // the actual card size here means growing the cards does cost a little
      // cornerScale (a real, modest, physical trade-off — worth a few
      // percent smaller nameplate in exchange for meaningfully bigger cards
      // and a table that reaches its preferred size), rather than an
      // unbounded one.
      const CORNER_FLOOR_SCALE = 0.25;
      const ABS_MIN_FELT_HEIGHT = 150;
      const ABS_MIN_FELT_WIDTH = ABS_MIN_FELT_HEIGHT * TARGET_ASPECT;
      // Dealer's felt now targets 2.5x its original preferred size (raised
      // twice on explicit request — first to 1.5x, then further here so the
      // table's content, especially its text, reads clearly at a glance).
      // This DOES cost a modest amount of cornerScale in the blend below (a
      // bigger target leaves less budget for corners) — measured at 1080p as
      // nameplate 158px (original) -> 145px (1.5x) -> 123px (2.5x) tall,
      // ~22% smaller cumulatively, in exchange for felt height going
      // 266px -> 342px -> 430px (~62% bigger cumulatively) and its internal
      // content (cards/pot/phase badge) scaling up by the same ratio, since
      // FeltTable's content transform scale is tied directly to felt height.
      // Chrome in DealerView/SpectatorView was also trimmed to claw back
      // some budget rather than relying on this trade-off alone. Spectator has no
      // fixed preferred size at all (it just fills whatever's left), so it
      // gets a generous stand-in target instead — big enough to never be the
      // binding constraint, letting cornerScale settle purely from
      // nameplate-vs-budget the way "dealer" does when there's plenty of
      // room, while the felt itself still fills real leftover space
      // afterward (see feltHeightCeiling below).
      const DEALER_FELT_GROWTH = 2.5;
      // Spectator's realistic felt height target is now capped the same way
      // its actual size will be (see feltHeightCeiling below) — using
      // budgetW as a stand-in for the width it'll end up with, since the
      // real leftColW/rightColW aren't known yet at this point (they
      // depend on cornerScale, which this very target feeds into). Without
      // this, corners were being shrunk to make room for a felt that (after
      // the height-cap fix) never actually grows that tall on narrow/tall
      // screens, leaving them smaller than they need to be.
      const preferredFeltH = variant === "spectator" ? Math.min(budgetH, budgetW / TARGET_ASPECT) : MIN_FELT_HEIGHT * DEALER_FELT_GROWTH;
      const preferredFeltW = variant === "spectator" ? budgetW : MIN_FELT_WIDTH * DEALER_FELT_GROWTH;

      const solveCornerScale = (naturalCornerSum, floorFelt, preferredFelt, hardFloorFelt, budget) => {
        if (naturalCornerSum <= 0) return 1;
        const floorCost = naturalCornerSum * CORNER_FLOOR_SCALE + floorFelt;
        const preferredCost = naturalCornerSum + preferredFelt;
        if (budget >= preferredCost) return 1;
        if (budget >= floorCost) {
          const fraction = (budget - floorCost) / (preferredCost - floorCost);
          return CORNER_FLOOR_SCALE + fraction * (1 - CORNER_FLOOR_SCALE);
        }
        return Math.max(0.02, (budget - hardFloorFelt) / naturalCornerSum);
      };

      const cornerScaleForHeight = solveCornerScale(
        naturalTopRowH + naturalBottomRowH,
        ABS_MIN_FELT_HEIGHT,
        preferredFeltH,
        40,
        budgetH
      );
      const cornerScaleForWidth = solveCornerScale(
        naturalLeftColW + naturalRightColW,
        ABS_MIN_FELT_WIDTH,
        preferredFeltW,
        60,
        budgetW
      );
      const cornerScale = Math.max(0.02, Math.min(1, cornerScaleForHeight, cornerScaleForWidth));

      const topRowH = naturalTopRowH * cornerScale;
      const bottomRowH = naturalBottomRowH * cornerScale;
      const leftColW = naturalLeftColW * cornerScale;
      const rightColW = naturalRightColW * cornerScale;

      // Felt sizing: "dealer" height is capped at the same 1.5x-grown
      // reference size used as its target above, so it doesn't overshoot
      // that target even if there happens to be more leftover room than
      // expected. "spectator" has no fixed ceiling and fills whatever's
      // left over — but on a narrow/tall (phone-portrait) screen, width is
      // the tight budget while height is comparatively huge, so without a
      // height cap the felt stretches into a tall narrow pillar: content
      // (cards/pot) crammed into a tiny cluster in the middle, with a lot
      // of dead green space above and below inside the ellipse. Capping
      // height at (available width / TARGET_ASPECT) stops exactly there:
      // since FELT_BASE_WIDTH/FELT_BASE_HEIGHT already equals TARGET_ASPECT,
      // this cap doesn't shrink the content scale at all versus the old
      // uncapped behavior (content scale was already the width-bound ratio
      // on these screens) — it only removes height the content could never
      // have used anyway, leaving a properly-proportioned oval instead.
      const availableForFeltWidthArea = availW - leftColW - rightColW - gap * 2 - outerPadding * 2;
      const feltWidthCandidate = Math.max(60, availableForFeltWidthArea);

      const feltHeightCeiling =
        variant === "spectator"
          ? Math.max(ABS_MIN_FELT_HEIGHT, feltWidthCandidate / TARGET_ASPECT)
          : MIN_FELT_HEIGHT * DEALER_FELT_GROWTH;
      const feltWidthCeiling =
        variant === "spectator" ? Infinity : MIN_FELT_HEIGHT * DEALER_FELT_GROWTH * MAX_FELT_ASPECT;

      const availableForFeltHeight = availH - topRowH - bottomRowH - gap * 2 - outerPadding * 2;
      const feltHeight = Math.min(feltHeightCeiling, Math.max(40, availableForFeltHeight));

      // Cap the felt's width at MAX_FELT_ASPECT times its height instead of
      // always filling 100% of the leftover width — otherwise, since there's
      // usually much more spare width than height, it stretches into a
      // razor-thin ellipse rather than a recognizable table shape. Set well
      // above TARGET_ASPECT (the content's own, narrower proportions) so it
      // only kicks in once the table would otherwise get unreasonably flat,
      // not as soon as it stops matching the content's exact shape.
      const feltAreaWidth = Math.min(feltWidthCeiling, feltWidthCandidate);
      const feltWidth = Math.min(feltAreaWidth, feltHeight * MAX_FELT_ASPECT, availableForFeltWidthArea);

      // Spectator's height cap (above) now often leaves real leftover
      // vertical room beyond the felt's own capped height, on a
      // narrow/tall screen — center the felt within that space instead of
      // pinning it flush under the top row, so the reclaimed space reads
      // as balanced breathing room rather than an odd gap dumped entirely
      // below the table. Dealer keeps its exact previous flush-top
      // positioning (it has its own fixed growth target, not this cap).
      const feltTop =
        variant === "spectator"
          ? outerPadding + topRowH + gap + (availableForFeltHeight - feltHeight) / 2
          : outerPadding + topRowH + gap;
      // Center the (possibly narrower-than-available) felt within its
      // horizontal area rather than pinning it flush left, so capping the
      // width for roundness doesn't leave it looking off-center.
      const feltAreaLeft = outerPadding + leftColW + gap;
      const feltLeft = feltAreaLeft + (availableForFeltWidthArea - feltWidth) / 2;

      setLayout({ cornerScale, feltLeft, feltTop, feltWidth, feltHeight });
    };

    recompute();
    const observer = new ResizeObserver(recompute);
    observer.observe(outerEl);
    [tlInnerRef, trInnerRef, blInnerRef, brInnerRef].forEach((r) => {
      if (r.current) observer.observe(r.current);
    });
    return () => observer.disconnect();
  }, [gameState, variant]);

  if (!gameState) return null;

  const { players = [], communityCards = [], pot = 0, phase = "idle", currentBet = 0, activePlayerSeat = 1, buttonSeat = 1, smallBlindSeat = 2, bigBlindSeat = 3 } = gameState;

  const [p1, p2, p3, p4] = [
    players.find((p) => p.seat === 1),
    players.find((p) => p.seat === 2),
    players.find((p) => p.seat === 3),
    players.find((p) => p.seat === 4),
  ];

  const getRole = (seat) => {
    if (seat === buttonSeat) return "dealer";
    if (seat === smallBlindSeat) return "small-blind";
    if (seat === bigBlindSeat) return "big-blind";
    return null;
  };

  const { cornerScale, feltLeft, feltTop, feltWidth, feltHeight } = layout;

  return (
    <div className="w-full h-full" style={{ padding: variant === "spectator" ? 2 : OUTER_PADDING }}>
      <div ref={outerRef} className="relative w-full h-full overflow-hidden">
        {/* Player 1 — top-left, scales anchored to the top-left corner */}
        <div
          className="absolute top-0 left-0"
          style={{ transform: `scale(${cornerScale})`, transformOrigin: "top left" }}
        >
          <div ref={tlInnerRef} style={{ width: CORNER_WIDTH }}>
            <PlayerSeat player={p1} position="top-left" cardSize="lg" isActiveTurn={p1?.seat === activePlayerSeat} role={getRole(p1?.seat)} />
          </div>
        </div>

        {/* Player 2 — top-right */}
        <div
          className="absolute top-0 right-0"
          style={{ transform: `scale(${cornerScale})`, transformOrigin: "top right" }}
        >
          <div ref={trInnerRef} style={{ width: CORNER_WIDTH }}>
            <PlayerSeat player={p2} position="top-right" cardSize="lg" isActiveTurn={p2?.seat === activePlayerSeat} role={getRole(p2?.seat)} />
          </div>
        </div>

        {/* Player 4 — bottom-left */}
        <div
          className="absolute bottom-0 left-0"
          style={{ transform: `scale(${cornerScale})`, transformOrigin: "bottom left" }}
        >
          <div ref={blInnerRef} style={{ width: CORNER_WIDTH }}>
            <PlayerSeat player={p4} position="bottom-left" cardSize="lg" isActiveTurn={p4?.seat === activePlayerSeat} role={getRole(p4?.seat)} />
          </div>
        </div>

        {/* Player 3 — bottom-right */}
        <div
          className="absolute bottom-0 right-0"
          style={{ transform: `scale(${cornerScale})`, transformOrigin: "bottom right" }}
        >
          <div ref={brInnerRef} style={{ width: CORNER_WIDTH }}>
            <PlayerSeat player={p3} position="bottom-right" cardSize="lg" isActiveTurn={p3?.seat === activePlayerSeat} role={getRole(p3?.seat)} />
          </div>
        </div>

        <FeltTable
          left={feltLeft}
          top={feltTop}
          width={feltWidth}
          height={feltHeight}
          phase={phase}
          communityCards={communityCards}
          pot={pot}
          currentBet={currentBet}
        />
      </div>
    </div>
  );
};

export default PokerTable;
