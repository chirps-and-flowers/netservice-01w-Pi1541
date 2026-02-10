function renderList() {
  listEl.innerHTML = "";
  if (queue.length === 0) {
    listEl.style.display = "none";
    optBoxTop.style.display = "none";
    optBoxTop2.style.display = "none";
    optBoxMid.style.display = "none";
    optBoxBottom.style.display = "none";
    optBoxTop.innerHTML = "";
    optBoxTop2.innerHTML = "";
    optBoxMid.innerHTML = "";
    optBoxBottom.innerHTML = "";
    return;
  }

  listEl.style.display = "grid";
  optBoxTop.style.display = "";
  optBoxTop2.style.display = "";
  optBoxMid.style.display = "";
  optBoxBottom.style.display = "";

  var listStartRow = 16;
  var listRows = queue.length;
  var listSpanRows = listRows * 2 - 1;
  var bottomRow = listStartRow + listSpanRows + 3;

  listEl.style.gridRow = listStartRow + " / span " + listSpanRows;
  optBoxTop.style.gridRow = String(listStartRow - 2);
  optBoxBottom.style.gridRow = String(bottomRow);

  var tl = "\uE0F0"; // top-left corner
  var tr = "\uE0EE"; // top-right corner
  var bl = "\uE0AD"; // bottom-left corner
  var br = "\uEE7D"; // bottom-right corner
  var hl = "\uE0C3";  // horizontal line
  var top = tl + hl.repeat(LIST_FRAME_WIDTH - 2) + tr;
  var bottom = bl + hl.repeat(LIST_FRAME_WIDTH - 2) + br; //
  optBoxTop.innerHTML = top;
  optBoxBottom.innerHTML = bottom;

  optBoxTop2.style.gridRow = String(bottomRow - 3);
  optBoxTop2.textContent = " ".repeat(LIST_FRAME_WIDTH);
  optBoxMid.style.gridRow = String(bottomRow - 2);
  optBoxMid.textContent = FRAME_VL + " ".repeat(LIST_FRAME_WIDTH - 2) + FRAME_VL;
  optimizeLine.style.gridRow = String(bottomRow - 1);
  exportSpacer.style.gridRow = String(bottomRow + 1) + " / span 2";

  for (var i = 0; i < queue.length; i++) {
    var item = queue[i];
    if (!item) continue;
    var row = document.createElement("div");
    row.className = "list-row";
    row.style.gridRow = String(i * 2 + 1);

    var num = document.createElement("div");
    num.className = "row-num";
    num.textContent = String(i + 1);

    var nameBtn = document.createElement("button");
    nameBtn.className = "row-name";
    nameBtn.type = "button";
    nameBtn.disabled = uploading;
    nameBtn.title = item.originalName || item.name;

    var full = item.name;
    var dot = full.lastIndexOf(".");
    var extSuffix = dot !== -1 ? full.slice(dot) : "";
    var base = dot !== -1 ? full.slice(0, dot) : full;

    var baseShown = truncateName(base, Math.max(0, NAME_MAX - extSuffix.length));
    var display = baseShown + extSuffix;

    var mainText = display.slice(0, NAME_MAIN);
    var dimText = display.slice(NAME_MAIN);
    var padLen = NAME_MAX - display.length;
    nameBtn.textContent = "";
    var spacer = document.createElement("span");
    spacer.textContent = "\u00a0";
    nameBtn.appendChild(spacer);
    var mainSpan = document.createElement("span");
    mainSpan.textContent = mainText;
    nameBtn.appendChild(mainSpan);
    if (dimText) {
      var dimSpan = document.createElement("span");
      dimSpan.className = "name-dim";
      dimSpan.textContent = dimText;
      nameBtn.appendChild(dimSpan);
    }
    if (queue.length > 2 && padLen > 0) {
      var padSpan = document.createElement("span");
      padSpan.className = "name-dim";
      padSpan.textContent = "\u00a0" + "-".repeat(Math.max(0, padLen - 1));
      nameBtn.appendChild(padSpan);
    }
    nameBtn.setAttribute("data-index", i);

    var gap = document.createElement("div");
    gap.textContent = "";

    var upBtn = document.createElement("button");
    upBtn.className = "row-up";
    upBtn.type = "button";
    if (i > 0 && !uploading) {
      upBtn.textContent = "^";
      upBtn.setAttribute("data-up", i);
    } else {
      upBtn.textContent = "";
      upBtn.disabled = true;
    }

    row.appendChild(num);
    row.appendChild(nameBtn);
    row.appendChild(gap);
    row.appendChild(upBtn);
    listEl.appendChild(row);
  }
}

function renderRightPanel() {
  svgEmpty.classList.add("hidden");
  svgOpen.classList.add("hidden");
  svgFull.classList.add("hidden");

  if (ready && !uploading && !waitingForReconnect && queue.length === 0) {
    svgEmpty.classList.remove("hidden");
    return;
  }

  if (ready && !uploading && !waitingForReconnect && queue.length > 0) {
    svgOpen.classList.remove("hidden");
    return;
  }

  svgFull.classList.remove("hidden");
}
