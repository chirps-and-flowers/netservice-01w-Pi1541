var MAX_ITEMS = 14;
var NAME_MAIN = 16;
var GRID_ROWS = 25;
var GRID_COLS = 40;
var IS_MOBILE = false;
var NAME_MAX = GRID_COLS - 6;
var LIST_FRAME_WIDTH = GRID_COLS;
var FRAME_VL = "\uE0C2";
var POLL_MS = 1000;
var HELLO_TIMEOUT_MS = 1500;
var SUCCESS_MS = 5000;
var STATUS_CYCLE_MS = 3000;
var STATUS_CYCLE_STEP_MS = 120;
function cssColorVar(name, fallback) {
  var value = "";
  try {
    value = getComputedStyle(document.documentElement).getPropertyValue(name).trim();
  } catch (e) {}
  return value || fallback;
}
var STATUS_COLORS = [
  cssColorVar("--c64-dark-blue", "#352879"),
  cssColorVar("--c64-blue", "#6C5EB5"),
  cssColorVar("--c64-white", "#FFFFFF"),
  cssColorVar("--c64-blue", "#6C5EB5")
];
var STATUS_ERROR_COLORS = [
  cssColorVar("--c64-red", "#883932"),
  cssColorVar("--c64-light-red", "#D28074"),
  cssColorVar("--c64-white", "#FFFFFF"),
  cssColorVar("--c64-light-red", "#D28074")
];

var supportedExt = {
  d64: true,
  d81: true,
  g64: true,
  nib: true,
  nbz: true,
  t64: true,
  prg: true
};

var queue = [];
var optimized = false;
var ready = false;
var waitingForReconnect = false;
var waitingSince = 0;
var uploading = false;
var lastNonce = 0;
var successUntil = 0;
var errorUntil = 0;
var statusText = "";
var transientStatusText = "";
var transientStatusUntil = 0;
var transientStatusColors = STATUS_COLORS;
var statusCycleUntil = 0;
var statusCycleIndex = 0;
var statusCycleNext = 0;
var statusCycleColors = STATUS_COLORS;
var helloInFlight = false;
var lastActionsKey = "";

var fileInput = document.getElementById("fileInput");
var folderInput = document.getElementById("folderInput");
var listEl = document.getElementById("list");
var statusLine = document.getElementById("statusLine");
var statusValue = document.getElementById("statusValue");
var actionLine1 = document.getElementById("actionLine1");
var actionLine2 = document.getElementById("actionLine2");
var optimizeLine = document.getElementById("optimizeLine");
var exportLine = document.getElementById("exportLine");
var titleLine = document.getElementById("titleLine");
var svgEmpty = document.getElementById("svgEmpty");
var svgOpen = document.getElementById("svgOpen");
var svgFull = document.getElementById("svgFull");
var optBoxTop = document.getElementById("optBoxTop");
	      var optBoxTop2 = document.getElementById("optBoxTop2");
	      var optBoxMid = document.getElementById("optBoxMid");
	      var optBoxBottom = document.getElementById("optBoxBottom");
	      var exportSpacer = document.getElementById("exportSpacer");
	      var dirtyBackdrop = document.getElementById("dirtyModalBackdrop");
	      var dirtyModal = document.getElementById("dirtyModal");

	      var devDirty = false;
	      try {
	        var qs = new URLSearchParams(window.location.search || "");
	        devDirty = qs.has("dev_dirty");
	      } catch (e) {}
	      var dirtyModalVisible = devDirty;
	      var dirtyDismissedId = 0;
	      var lastDirtyModalKey = "";
	      var modifiedId = 0;
	      var modifiedCount = 0;
	      var modifiedListId = 0;
	      var modifiedItems = [];
	      var modifiedFetchInFlight = false;
