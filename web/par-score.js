// Par contracts and score from a DD table, ported from bidding-practice's
// par-score.js.
const PAR_DECLARERS = ['S', 'N', 'W', 'E'];  // solve()'s column order
const PAR_STRAINS = ['N', 'S', 'H', 'D', 'C'];
const PAR_STRAIN_RANKS = { 'N': 4, 'S': 3, 'H': 2, 'D': 1, 'C': 0 };

function isVulnerable(seat, vulnerability) {
  return vulnerability === 'All' ||
    (vulnerability === 'N-S' && ['N', 'S'].includes(seat[0])) ||
    (vulnerability === 'E-W' && ['E', 'W'].includes(seat[0]));
}

// Returns par for solve()'s "<strain> <S> <N> <W> <E> ..." lines as
// [nsFirst, ewFirst] lists of contracts, the par result when that side bids
// first. Either can be empty (the other side's result applies); both empty
// means par is 0. Each contract has {level, strain, doubled, declarers,
// overtricks, nsScore}.
function computePar(ddLines, vulnerability) {
  return describePar(computeDoubleDummyPercentage(ddLines), vulnerability, false);
}

// Like computePar(), but from single-dummy trick distributions:
// counts[declarer][strain][t] is how many shuffles took exactly t tricks.
// nsScore is then an expected score, and each contract has makePercent
// instead of overtricks.
function computeSingleDummyPar(counts, vulnerability) {
  const percentage = {};
  for (const declarer of PAR_DECLARERS) {
    percentage[declarer] = {};
    for (const strain of PAR_STRAINS) {
      const c = counts[declarer][strain];
      const total = c.reduce((a, b) => a + b, 0);
      percentage[declarer][strain] = c.map(n => n * 100 / total);
    }
  }
  return describePar(percentage, vulnerability, true);
}

function describePar(percentage, vulnerability, singleDummy) {
  const [ns, ew] = computeParContracts(percentage, vulnerability);
  const describe = c => ({
    level: c.tricks - 6,
    strain: c.strain,
    doubled: c.score < 0,
    declarers: c.declarer + c.extraDeclarer,
    ...(singleDummy
      // Rounded down like the shuffle table's cells, so 100% means every shuffle.
      ? { makePercent: Math.floor(c.percentage.slice(c.tricks).reduce((a, b) => a + b, 0) + 1e-9) }
      : { overtricks: mostTricks(c.percentage) - c.tricks }),
    nsScore: ['N', 'S'].includes(c.declarer) ? c.score : -c.score,
  });
  return [ns.map(describe), ew.map(describe)];
}

// E.g. "4H= by N", "5DX-2 by NS", or "4H by N (62%)" for single dummy;
// strainLabel maps a strain letter to its display form.
function formatParContract(c, strainLabel = s => s) {
  const declarers = [...c.declarers].sort((a, b) => 'NSEW'.indexOf(a) - 'NSEW'.indexOf(b)).join('');
  const contract = `${c.level}${strainLabel(c.strain)}${c.doubled ? 'X' : ''}`;
  if (c.makePercent !== undefined) return `${contract} by ${declarers} (${c.makePercent}%)`;
  const result = c.overtricks == 0 ? '=' : (c.overtricks > 0 ? '+' : '') + c.overtricks;
  return `${contract}${result} by ${declarers}`;
}

// E.g. "NS +620", "EW -100", "0"; from the side whose contract it is.
function formatParScore(contracts) {
  if (!contracts.length) return '0';
  const c = contracts[0];
  const nsSide = 'NS'.includes(c.declarers[0]);
  const score = nsSide ? c.nsScore : -c.nsScore;
  return `${nsSide ? 'NS' : 'EW'} ${score > 0 ? '+' : ''}${score}`;
}

function computeDoubleDummyPercentage(ddLines) {
  const ddTricks = { 'S': {}, 'N': {}, 'W': {}, 'E': {} };
  for (const line of ddLines) {
    const items = line.trim().split(/\s+/).slice(0, 5);
    const strain = items[0];
    for (let pos = 0; pos < 4; pos++) {
      const declarer = PAR_DECLARERS[pos];
      const tricks = Number(items[pos + 1]);
      ddTricks[declarer][strain] = tricks;
    }
  }
  const percentage = {};
  for (const declarer of PAR_DECLARERS) {
    percentage[declarer] = {};
    for (const strain of PAR_STRAINS) {
      percentage[declarer][strain] = new Int32Array(14).fill(100);
      for (let tricks = 0; tricks <= 13; tricks++)
        percentage[declarer][strain][tricks] =
          tricks == ddTricks[declarer][strain] ? 100 : 0;
    }
  }
  return percentage;
}

function computeParContracts(percentage, vulnerability) {
  const nsContracts = [], ewContracts = [];
  for (const strain of PAR_STRAINS) {
    nsContracts.push(...scanLevels(strain, 'N', percentage, vulnerability));
    nsContracts.push(...scanLevels(strain, 'S', percentage, vulnerability));
    ewContracts.push(...scanLevels(strain, 'E', percentage, vulnerability));
    ewContracts.push(...scanLevels(strain, 'W', percentage, vulnerability));
  }

  // Highest scores first. For the same score,
  // 1) prefer the highest contract, if it's sacrificed;
  // 2) prefer the lowest contract, otherwise.
  const order = (a, b) => {
    if (a.scoreWithSacrifices != b.scoreWithSacrifices)
      return b.scoreWithSacrifices - a.scoreWithSacrifices;
    return (a.sacrifices.length ? b.rank - a.rank : a.rank - b.rank);
  }
  return [filterParContracts(nsContracts.sort(order)),
          filterParContracts(ewContracts.sort(order))];
}

function filterParContracts(contracts) {
  if (contracts.length == 0) return [];

  const parScore = contracts[0].scoreWithSacrifices;
  const parContractRank = contracts[0].rank;

  // Par contracts must have par score (the highest score) and
  // must be the highest contract if sacrificed.
  const pars = contracts.filter(c => c.scoreWithSacrifices == parScore &&
                                (!c.sacrifices.length || c.rank == parContractRank));

  // Flatten embedded sacrifices.
  const flatPars = [];
  if (pars[0].sacrifices.length) {
    pars.forEach(c => c.sacrifices.forEach(s => {
      if (-s.score == parScore) flatPars.push(s);
    }));
  } else {
    pars.forEach(c => flatPars.push(c));
  }

  // Remove duplicates by keeping only one contract for each strain.
  const uniquePars = [];
  for (const strain of PAR_STRAINS) {
    const topPars = flatPars.filter(c => c.strain == strain).slice(0, 2);
    if (topPars.length == 2 && topPars[0].rank == topPars[1].rank &&
        topPars[1].declarer != topPars[0].declarer)
      topPars[0].addDeclarer(topPars[1].declarer);
    uniquePars.push(...topPars.slice(0, 1));
  }
  return uniquePars;
}

function mostTricks(percentage) {
  for (let tricks = 13; tricks >= 0; tricks--)
    if (percentage[tricks] > 0) return tricks;
}

function scanLevels(strain, declarer, percentage, vulnerability) {
  const maxTricks = mostTricks(percentage[declarer][strain]);
  const contracts = [];
  for (let tricks = 7; tricks <= maxTricks; tricks++) {
    const contract = new ProbContract(tricks, strain, percentage[declarer][strain],
                                      declarer, isVulnerable(declarer, vulnerability));
    const opponents = ['N', 'S'].includes(declarer) ? ['E', 'W'] : ['N', 'S'];
    const competitions = competeWith(contract, percentage, opponents,
                                     isVulnerable(opponents[0], vulnerability));
    if (competitions.length == 0) {  // No competition
      if (contract.score > 0)
        contracts.push(contract);
    } else if (competitions[0].score > 0) {  // A higher and makable contract
    } else {  // Sacrifice
      contract.addSacrifices(competitions);
      contracts.push(contract);
    }
  }
  return contracts;
}

function competeWith(oppContract, percentage, seats, vulnerable) {
  const contracts = [];
  for (const strain of PAR_STRAINS) {
    const tricks = PAR_STRAIN_RANKS[strain] > PAR_STRAIN_RANKS[oppContract.strain] ?
      oppContract.tricks : oppContract.tricks + 1;
    if (tricks > 13) continue;

    for (const declarer of seats) {
      const contract = new ProbContract(tricks, strain, percentage[declarer][strain],
                                        declarer, vulnerable);
      if (contract.score > 0 ||  // A higher and makable contract
          contract.score > -oppContract.score) {  // Sacrifice
        contracts.push(contract);
      }
    }
  }
  return contracts.sort((a, b) => b.score - a.score);
}

class Contract {
  constructor(tricks, strain, actualTricks, declarer, vulnerable,
              doubledDown = 1, doubledMake = 0) {
    this.tricks = tricks;
    this.strain = strain;
    this.actualTricks = actualTricks;
    this.declarer = declarer;
    this.vulnerable = vulnerable;
    this.doubledDown = doubledDown;
    this.doubledMake = doubledMake;
    this.score = this.#score();
  }

  #score() {
    const downTricks = this.tricks - this.actualTricks;
    if (downTricks > 0) {
      if (!this.doubledDown)
        return -(downTricks * (this.vulnerable ? 100 : 50));

      if (this.vulnerable) {
        return -(200 + (downTricks - 1) * 300) * this.doubledDown;
      } else if (downTricks <= 2) {
        return -(100 + (downTricks - 1) * 200) * this.doubledDown;
      } else {
        return -(500 + (downTricks - 3) * 300) * this.doubledDown;
      }
    } else {
      let score = this.#trickScore() << this.doubledMake;
      if (score < 100) score += 50;  // Part score
      else score += this.vulnerable ? 500 : 300;  // Game
      if (this.tricks == 12) score += this.vulnerable ?  750 :  500;  // Slam
      if (this.tricks == 13) score += this.vulnerable ? 1500 : 1000;  // Grand
      return score + this.doubledMake * 50 + this.#overtrickScore();
    }
  }

  #trickScore() {
    switch (this.strain) {
      case 'N': return (this.tricks - 6) * 30 + 10;
      case 'S':
      case 'H': return (this.tricks - 6) * 30;
      case 'D':
      case 'C': return (this.tricks - 6) * 20;
    }
  }

  #overtrickScore() {
    const overTricks = this.actualTricks - this.tricks;
    if (this.doubledMake)
      return overTricks * (this.vulnerable ? 200 : 100) * this.doubledMake;

    switch (this.strain) {
      case 'N':
      case 'S':
      case 'H': return overTricks * 30;
      case 'D':
      case 'C': return overTricks * 20;
    }
  }
};

class ProbContract {
  constructor(tricks, strain, percentage, declarer, vulnerable,
              doubledDown = 1, doubledMake = 0) {
    this.tricks = tricks;
    this.strain = strain;
    this.percentage = percentage;
    this.declarer = declarer;
    this.extraDeclarer = '';
    this.vulnerable = vulnerable;
    this.doubledDown = doubledDown;
    this.doubledMake = doubledMake;
    this.rank = this.#rank();
    this.score = this.#score();
    this.sacrifices = [];
    this.scoreWithSacrifices = this.score;
  }

  addDeclarer(declarer) { this.extraDeclarer += declarer; }

  addSacrifices(sacrifices) {
    this.sacrifices = sacrifices;
    if (this.sacrifices.length)
      this.scoreWithSacrifices = -this.sacrifices[0].score;
  }

  #rank() { return PAR_STRAIN_RANKS[this.strain] + this.tricks * 5; }

  #score() {
    let expectedScore = 0;
    for (let actualTricks = 0; actualTricks <= 13; actualTricks++) {
      const p = this.percentage[actualTricks];
      if (p == 0) continue;
      const contract = new Contract(this.tricks, this.strain, actualTricks, this.declarer,
                                    this.vulnerable, this.doubledDown, this.doubledMake);
      expectedScore += p * contract.score;
    }
    return Math.round(expectedScore / 100);
  }
};

if (typeof module !== 'undefined') {
  module.exports = { computePar, computeSingleDummyPar, formatParContract, formatParScore };
}
