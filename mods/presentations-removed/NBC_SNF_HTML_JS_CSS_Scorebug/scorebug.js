const defaultState = {
  away: {
    abbr: "LAR",
    record: "10-6",
    score: 42,
    primary: "#003594",
    light: "#2b67c7",
    dark: "#001b4d",
    logo: "assets/logos/LAR.png"
  },
  home: {
    abbr: "CIN",
    record: "9-7",
    score: 27,
    primary: "#7b2d23",
    light: "#c0523f",
    dark: "#3a140f",
    logo: "assets/logos/CIN.png"
  },
  clock: "15:00",
  quarter: "2ND",
  timeouts: {
    away: 3,
    home: 3
  }
};

let state = structuredClone(defaultState);

function setText(selector, value) {
  const el = document.querySelector(selector);
  if (el) el.textContent = value;
}

function setLogo(selector, src, alt) {
  const img = document.querySelector(selector);
  if (!img) return;
  img.src = src;
  img.alt = alt;
}

function setTimeouts(selector, count) {
  const track = document.querySelector(selector);
  if (!track) return;

  track.innerHTML = "";
  for (let i = 0; i < count; i++) {
    track.appendChild(document.createElement("span"));
  }
}

function applyTeamVars(side, team) {
  const root = document.querySelector(".snf-scorebug");
  root.style.setProperty(`--${side}-color`, team.primary);
  root.style.setProperty(`--${side}-color-light`, team.light);
  root.style.setProperty(`--${side}-color-dark`, team.dark);
}

function renderScorebug() {
  applyTeamVars("away", state.away);
  applyTeamVars("home", state.home);

  setText(".away-abbr", state.away.abbr);
  setText(".away-record", state.away.record);
  setText(".away-score", state.away.score);

  setText(".home-abbr", state.home.abbr);
  setText(".home-record", state.home.record);
  setText(".home-score", state.home.score);

  setText(".clock-text", state.clock);
  setText(".quarter-text", state.quarter);

  setLogo(".away-logo", state.away.logo, state.away.abbr);
  setLogo(".home-logo", state.home.logo, state.home.abbr);

  setTimeouts(".away-timeout-track", state.timeouts.away);
  setTimeouts(".home-timeout-track", state.timeouts.home);
}

function setScore(awayScore, homeScore) {
  state.away.score = awayScore;
  state.home.score = homeScore;
  renderScorebug();
}

function setClock(clock, quarter) {
  state.clock = clock;
  state.quarter = quarter;
  renderScorebug();
}

function setTeams(away, home) {
  state.away = { ...state.away, ...away };
  state.home = { ...state.home, ...home };
  renderScorebug();
}

function swapTeams() {
  const away = state.away;
  state.away = state.home;
  state.home = away;

  const awayTimeouts = state.timeouts.away;
  state.timeouts.away = state.timeouts.home;
  state.timeouts.home = awayTimeouts;

  renderScorebug();
}

document.getElementById("swapBtn")?.addEventListener("click", swapTeams);

document.getElementById("tdAwayBtn")?.addEventListener("click", () => {
  state.away.score += 7;
  renderScorebug();
});

document.getElementById("tdHomeBtn")?.addEventListener("click", () => {
  state.home.score += 7;
  renderScorebug();
});

document.getElementById("resetBtn")?.addEventListener("click", () => {
  state = structuredClone(defaultState);
  renderScorebug();
});

renderScorebug();

window.SNFScorebug = {
  get state() {
    return structuredClone(state);
  },
  setScore,
  setClock,
  setTeams,
  swapTeams,
  render: renderScorebug
};
