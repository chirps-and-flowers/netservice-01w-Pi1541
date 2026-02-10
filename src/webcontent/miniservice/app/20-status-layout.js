function markError(label) {
  errorUntil = nowMs() + SUCCESS_MS;
  if (label) {
    setTransientStatus(label, SUCCESS_MS, STATUS_ERROR_COLORS);
  }
}

function setStatus(text, colors) {
  statusCycleColors = (colors && colors.length) ? colors : STATUS_COLORS;
  if (statusText !== text) {
    statusText = text;
    renderStatusValue(text);
    startStatusCycle();
    return;
  }
  if (!statusValue.firstChild) renderStatusValue(text);
}

function nowMs() {
  return Date.now();
}

function setTransientStatus(text, ms, colors) {
  var msg = String(text || "").trim().toUpperCase();
  if (!msg) return;
  transientStatusText = msg;
  transientStatusUntil = nowMs() + (ms || 2500);
  transientStatusColors = (colors && colors.length) ? colors : STATUS_COLORS;
}

function clearTransientStatus() {
  transientStatusText = "";
  transientStatusUntil = 0;
  transientStatusColors = STATUS_COLORS;
}

function fetchHello() {
  var opts = { cache: "no-store" };
  if (typeof AbortController === "undefined") {
    return fetch("/hello", opts);
  }
  var ctrl = new AbortController();
  opts.signal = ctrl.signal;
  var t = setTimeout(function() {
    try { ctrl.abort(); } catch (e) {}
  }, HELLO_TIMEOUT_MS);
  return fetch("/hello", opts).then(function(resp) {
    clearTimeout(t);
    return resp;
  }, function(err) {
    clearTimeout(t);
    throw err;
  });
}

function startStatusCycle() {
  statusCycleUntil = nowMs() + STATUS_CYCLE_MS;
  statusCycleIndex = 0;
  statusCycleNext = 0;
}

function renderStatusValue(text) {
  statusValue.textContent = "";
  for (var i = 0; i < text.length; i++) {
    var span = document.createElement("span");
    var ch = text.charAt(i);
    span.textContent = ch === " " ? "\u00a0" : ch;
    statusValue.appendChild(span);
  }
}

function tickStatusCycle() {
  if (statusCycleUntil <= nowMs()) {
    if (statusValue.firstChild) {
      var clearSpans = statusValue.children;
      for (var c = 0; c < clearSpans.length; c++) {
        if (clearSpans[c].style.color) clearSpans[c].style.color = "";
      }
    }
    return;
  }
  if (nowMs() < statusCycleNext) return;
  var spans = statusValue.children;
  for (var i = 0; i < spans.length; i++) {
    spans[i].style.color = statusCycleColors[(statusCycleIndex + i) % statusCycleColors.length];
  }
  statusCycleIndex += 1;
  statusCycleNext = nowMs() + STATUS_CYCLE_STEP_MS;
}

function clampCell() {
  var vw = document.documentElement.clientWidth || window.innerWidth || 0;
  var vh = document.documentElement.clientHeight || window.innerHeight || 0;
  if (window.visualViewport && window.visualViewport.width && window.visualViewport.height) {
    vw = Math.min(vw, window.visualViewport.width);
    vh = Math.min(vh, window.visualViewport.height);
  }
  var isMobile = (window.matchMedia && window.matchMedia("(max-width: 900px)").matches) || (vw <= 900);
  IS_MOBILE = isMobile;
  var wantCols = isMobile ? 28 : 40;

  var colsChanged = false;
  if (wantCols !== GRID_COLS) {
    GRID_COLS = wantCols;
    NAME_MAX = GRID_COLS - 6;
    LIST_FRAME_WIDTH = GRID_COLS;
    var optHalf = Math.floor((GRID_COLS - 2) / 2);
    document.documentElement.style.setProperty("--cols", String(GRID_COLS));
    document.documentElement.style.setProperty("--opt-half-cols", String(optHalf));
    if (titleLine) {
      titleLine.textContent = (GRID_COLS === 40)
        ? "--- PI1541-01W MINI LAN UI ---"
        : "- PI1541-01W MINI LAN UI -";
    }
    colsChanged = true;
  }

  var cell = 0;
  if (isMobile) {
    cell = Math.floor(Math.min(vw / GRID_COLS, vh / GRID_ROWS));

    var minCell = (vw < 320) ? 7 : 8;
    if (cell < minCell) cell = minCell;
    if (cell > 19) cell = 19;

    var pad = Math.floor((vw - (cell * GRID_COLS)) / 2);
    if (pad < 0) pad = 0;
    document.documentElement.style.setProperty("--mobile-pad", pad + "px");
  } else {
    var scale = 0.9;
    cell = Math.floor(Math.min((vw * scale) / GRID_COLS, (vh * scale) / GRID_ROWS)) - 1;
    if (cell < 9) cell = 9;
    if (cell > 19) cell = 19;
    document.documentElement.style.setProperty("--mobile-pad", "0px");
  }

  document.documentElement.style.setProperty("--cell", cell + "px");
  if (colsChanged) {
    lastActionsKey = "";
    lastDirtyModalKey = "";
    renderAll();
  }
}

window.addEventListener("resize", clampCell);
if (window.visualViewport && window.visualViewport.addEventListener) {
  window.visualViewport.addEventListener("resize", clampCell);
}
clampCell();
