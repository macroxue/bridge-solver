// Seat order/naming and every parser that turns pasted or typed deal text
// into a {seat: "S H D C"} hands object -- split out from app.js so this
// logic can be unit tested (see deal-parser-test.js) independent of the
// DOM/wasm/worker machinery the rest of the app needs. Loaded by index.html
// via <script> before app.js, same as deals.js/declarer-columns.js; also
// require()-able from Node for the test, via the module.exports at the
// bottom (harmless in the browser, where `module` is simply undefined).
const SEATS = ['west', 'north', 'east', 'south'];
const SEAT_NAME_BY_LETTER = { W: 'west', N: 'north', E: 'east', S: 'south' };

// Parses the deal layouts used by deals/hard/, deals/freak/, etc., plus
// whatever a few real sites' own deal panels happen to copy-paste as.
// Absent a seat label, hands are assumed North, West, East, South order,
// but the surrounding format varies a lot:
//  - seat letter + suit symbols: "N ♠.. ♥.. ♦.. ♣.."
//  - full seat name, suit symbols and ranks each their own line (e.g.
//    bridgewinners.com): "West\n♠\n7\n♥\n...", in whatever seat order
//  - suit symbols only (no seat label): "♠.. ♥.. ♦.. ♣.."
//  - plain positional groups: "- Q853 AJ962 KT74", either West and East
//    sharing one line (separated by a wide gap) or each on its own line
//  - optionally followed by trailing metadata lines (trump/lead seat,
//    e.g. "N", "C", "West") that must be ignored
function parseDealFile(text) {
  const positionalSeats = ['north', 'west', 'east', 'south'];
  const hands = {};

  // Suit captures are bounded by rank characters, not `\S`/`\S+` -- vugraph's
  // one-line form runs everything together with no separators at all (e.g.
  // "...♣K762♠KJ96..."), so anything looser than an explicit rank charclass
  // greedily eats straight through the next suit symbol. This also covers a
  // void suit written as nothing between one symbol and the next (e.g.
  // "♠♥AK94..."), captured empty -- and each empty (or literal "-", see
  // below) capture becomes an explicit '-' rather than silently dropping
  // out and shifting the other three suits into the wrong slots downstream.
  // Includes 0/1 (not just 2-9) so "10" (an alternative to "T" for ten)
  // doesn't split the token in two.
  const rank = '[0-9TJQKAXtjqkax]*';
  // A void suit can also be written as an explicit "-" instead of nothing
  // at all (e.g. one line per field: "West\n♠\n-\n♥\nKQJ96\n...") -- since
  // "-" isn't a rank character, without this alternative the capture group
  // would match zero characters there and then fail to reach the next
  // suit symbol (the "-" itself is in the way and isn't whitespace).
  const rankOrDash = `(?:${rank}|-)`;
  // Two ways a hand gets labeled, both optional and tried in this order:
  //  - a full seat name on its own (e.g. bridgewinners.com's copy-pasted
  //    "West\n♠\n7\n♥\n..."), which can cross newlines to reach the ♠ --
  //    a whole word is too deliberate a signal to plausibly be a
  //    coincidental stray one, unlike a bare letter (see below).
  //  - a single seat letter (deals/hard's "N ♠ A87 ..."), which must stay
  //    on the same line (only horizontal whitespace before the ♠) --
  //    crossing a newline risks grabbing an unrelated stray N/W/E/S, e.g.
  //    the "W" in a preceding "Vulnerable:E-W" line.
  const symbolPattern = new RegExp(
    `(?:\\b(North|South|East|West)\\b\\s*|([NWES])[ \\t]*)?` +
      `♠\\s*(${rankOrDash})\\s*♥\\s*(${rankOrDash})\\s*♦\\s*(${rankOrDash})\\s*♣\\s*(${rankOrDash})`,
    'gi',
  );
  const symbolMatches = [...text.matchAll(symbolPattern)];
  if (symbolMatches.length === 4) {
    const seatOf = ([, fullName, letter]) =>
      fullName ? fullName.toLowerCase() : letter ? SEAT_NAME_BY_LETTER[letter.toUpperCase()] : null;
    // Trust captured seat labels only if every match has one -- a mix (one
    // stray label alongside three blanks) isn't real labeling, just a
    // coincidence, so require all four or trust none.
    const allLabeled = symbolMatches.every(seatOf);
    symbolMatches.forEach((match, i) => {
      const [, , , spades, hearts, diamonds, clubs] = match;
      const suits = [spades, hearts, diamonds, clubs].map(s => s || '-');
      hands[allLabeled ? seatOf(match) : positionalSeats[i]] = suits.join(' ');
    });
    if (SEATS.every(seat => hands[seat])) return hands;
  }

  // Plain-text fallback: collect every group of 4 card tokens found, in
  // file order, whether a line holds one hand or two (West and East
  // sharing a line, split on the wide gap between them). Lines that
  // aren't a hand (blank, or a trailing trump/lead-seat annotation)
  // simply don't match and are skipped. 0/1 (not just 2-9) so "10" doesn't
  // split a token in two or fail to look like a card group at all.
  const isCardGroup = (tokens) =>
    tokens.length === 4 && tokens.every(t => /^(-|[0-9TJQKAX]+)$/i.test(t));

  const blocks = [];
  for (const rawLine of text.split('\n')) {
    const line = rawLine.trim();
    if (!line) continue;
    const gap = line.match(/\s{2,}/);
    if (gap) {
      const left = line.slice(0, gap.index).trim().split(/\s+/);
      const right = line.slice(gap.index + gap[0].length).trim().split(/\s+/);
      if (isCardGroup(left) && isCardGroup(right)) {
        blocks.push(left.join(' '), right.join(' '));
        continue;
      }
    }
    const tokens = line.split(/\s+/);
    if (isCardGroup(tokens)) blocks.push(tokens.join(' '));
  }

  // A real paste into #pasteBox (an <input>) replaces each newline with a
  // single space rather than deleting it (see the 'input' listener's own
  // comment), which collapses a file like this to one line and loses the
  // line/gap structure the block-matching above depends on. What survives
  // regardless: each hand is still exactly 4 whitespace-separated
  // rank-or-void tokens, and this format's own trailing metadata (a lone
  // lead-seat/trump letter, if present, e.g. deal.2's trailing "S"/"W")
  // never looks like one -- S/H/D/C/N/E/W aren't valid rank characters --
  // so extracting every rank-or-void token from the whole text and
  // chunking it into fours recovers the same 4 hands without needing any
  // line or gap structure at all. Also 0/1-inclusive for "10"; the
  // exact-16-tokens check right below is what keeps that from letting
  // through some unrelated 2-digit number (e.g. a board number) sitting
  // in text this fallback would otherwise ignore.
  if (blocks.length !== 4) {
    const flatTokens = text.match(/-|[0-9TJQKAX]+/gi) || [];
    if (flatTokens.length === 16) {
      for (let i = 0; i < 4; ++i) blocks[i] = flatTokens.slice(i * 4, i * 4 + 4).join(' ');
    }
  }

  if (blocks.length === 4) {
    positionalSeats.forEach((seat, i) => { hands[seat] = blocks[i]; });
  }
  return hands;
}

// Parses a PBN "Deal" field: "<first>:<hand1> <hand2> <hand3> <hand4>", each
// hand "S.H.D.C" (dot-separated ranks, empty for void), hands listed
// clockwise starting from <first>. The regex has no anchors, so this also
// picks the field out of a full PBN tag/file, e.g. `[Deal "N:..."]`. Strict
// PBN always writes "T", never "10", but 0/1 are included here too so a
// hand-typed or non-conforming "10" doesn't split a token in two.
function parsePBN(text) {
  const hand = '[0-9TJQKAXtjqkax]*';
  const re = new RegExp(`\\b([NESW]):((?:${hand}\\.){3}${hand}(?:\\s+(?:${hand}\\.){3}${hand}){3})`);
  const match = text.match(re);
  if (!match) return null;
  const [, first, rest] = match;
  const handTokens = rest.trim().split(/\s+/);
  // SEATS is declared in this same clockwise cyclic order (just starting
  // at 'west' instead of the seat PBN happens to name first), so indexing
  // into it modulo 4 walks clockwise the same way SEATS_CLOCKWISE did.
  const start = SEATS.indexOf(SEAT_NAME_BY_LETTER[first]);
  const hands = {};
  handTokens.forEach((token, i) => {
    const suits = token.split('.');
    hands[SEATS[(start + i) % 4]] = suits.map(s => s.toUpperCase() || '-').join(' ');
  });
  return hands;
}

// Tries every known deal format against pasted clipboard text, in order from
// most to least specific. Returns null (not four fully-populated hands) if
// nothing recognized it, so the caller can fall back to a normal paste.
function parsePastedDeal(text) {
  for (const parser of [parsePBN, parseDealFile]) {
    const hands = parser(text);
    if (hands && SEATS.every(seat => hands[seat])) return hands;
  }
  return null;
}

if (typeof module !== 'undefined') {
  module.exports = { SEATS, SEAT_NAME_BY_LETTER, parsePBN, parseDealFile, parsePastedDeal };
}
