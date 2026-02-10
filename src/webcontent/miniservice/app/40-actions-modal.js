function renderActions() {
  var hasItems = queue.length > 0;
  var l1, e1, a1, l2, e2, a2;
  if (!hasItems) {
    l1 = "INSERT DISKS"; e1 = !uploading; a1 = "insertDisks";
    l2 = "INSERT FOLDER"; e2 = !uploading; a2 = "insertFolder";
  } else {
    l1 = "MOUNT DISKS"; e1 = ready && !uploading; a1 = "mountDisks";
    l2 = "CLEAR DISKS"; e2 = !uploading; a2 = "clearDisks";
  }

  var key = [l1, e1 ? "1" : "0", a1, l2, e2 ? "1" : "0", a2].join("|");
  if (key === lastActionsKey) return;
  lastActionsKey = key;
  actionLine1.innerHTML = renderActionHtml(l1, e1, a1);
  actionLine2.innerHTML = renderActionHtml(l2, e2, a2);
}

function renderBottomButtons() {
  exportLine.style.display = "none";
  exportLine.textContent = "";

  if (queue.length === 0) {
    optimizeLine.innerHTML = "";
    optimizeLine.style.display = "none";
    exportSpacer.textContent = "";
    exportSpacer.style.display = "none";
    return;
  }
  optimizeLine.style.display = "";
  exportSpacer.style.display = "";
  var compact = IS_MOBILE || (GRID_COLS !== 40);
  var label = compact
    ? (optimized ? "UNDO" : "OPTIMIZE")
    : (optimized ? "UNDO OPTIMIZE" : "OPTIMIZE NAMES");
  var exportLabel = compact ? "EXPORT" : "EXPORT ZIP";
  optimizeLine.innerHTML =
    '<span class="divider">' + FRAME_VL + '</span>' +
    '<span class="opt-col opt-left">' + renderActionHtml(label, !uploading, "toggleOptimize") + '</span>' +
    '<span class="opt-col opt-right">' + renderActionHtml(exportLabel, !uploading, "exportZip") + '</span>' +
    '<span class="divider">' + FRAME_VL + '</span>';
  exportSpacer.textContent = "";
}

function renderActionHtml(label, enabled, action) {
  var prefix = '<span class="accent">&gt;</span>&nbsp;';
  var btn = '<button class="action" data-action="' + action + '"' + (enabled ? '' : ' disabled') + '>' + label + '</button>';
  if (action === "mountDisks" && enabled) {
    return prefix + '<span class="action-hot">' + btn + '</span>';
  }
  return prefix + btn;
}

function renderDirtyModal() {
  if (!dirtyBackdrop || !dirtyModal) return;
  if (!dirtyModalVisible) {
    dirtyBackdrop.classList.add("hidden");
    dirtyBackdrop.setAttribute("aria-hidden", "true");
    dirtyModal.innerHTML = "";
    lastDirtyModalKey = "";
    return;
  }

  var items = [];
  if (devDirty) {
    var names = queue.length ? queue.map(function(q) { return q.name; }) : [
      "01_TESTSAVE.D64",
      "02_OTHERDISK.D64"
    ];
    items = names.map(function(n, idx) { return { action: "dirtyDownload", idx: idx, name: n }; });
  } else {
    items = (modifiedItems || []).map(function(it) {
      return { action: "modifiedDownload", idx: it.i, name: it.name };
    });
  }

  var key = "open|" + (devDirty ? "dev" : String(modifiedId)) + "|" + items.map(function(it) { return it.idx + ":" + it.name; }).join(",");
  if (key === lastDirtyModalKey) {
    dirtyBackdrop.classList.remove("hidden");
    dirtyBackdrop.setAttribute("aria-hidden", "false");
    return;
  }
  lastDirtyModalKey = key;

  dirtyBackdrop.classList.remove("hidden");
  dirtyBackdrop.setAttribute("aria-hidden", "false");

  var cols = IS_MOBILE
    ? Math.max(18, GRID_COLS - 2)   // 28-col screen -> 26-col modal
    : Math.min(34, Math.max(18, GRID_COLS - 4));
  var inner = cols - 2;
  var rows = 18;
  dirtyModal.style.setProperty("--cols", String(cols));
  dirtyModal.style.setProperty("--inner-cols", String(inner));
  dirtyModal.style.setProperty("--rows", String(rows));
  var innerPad = inner - 2; // 1-char padding on both sides inside the frame (30)

  var tl = "\uE0F0";
  var tr = "\uE0EE";
  var bl = "\uE0AD";
  var br = "\uEE7D";
  var hl = "\uE0C3";
  var vl = FRAME_VL;

  function padText(text, width) {
    var t = String(text);
    if (t.length >= width) return t.slice(0, width);
    return t + " ".repeat(width - t.length);
  }

  function lineHtml(left, middleHtml, right) {
    return '<div class="line modal-line">' +
      '<span class="divider">' + escapeHtml(left) + '</span>' +
      middleHtml +
      '<span class="divider">' + escapeHtml(right) + '</span>' +
    '</div>';
  }

  var out = [];

  out.push('<div class="line modal-line divider">' +
    escapeHtml(tl + hl.repeat(cols - 2) + tr) +
  '</div>');

  out.push(lineHtml(vl,
    '<span class="modal-btn">' + escapeHtml(" ".repeat(inner)) + '</span>',
    vl
  ));

  out.push(lineHtml(vl,
    '<span class="modal-btn c64-red">' + escapeHtml(" " + padText("MODIFIED DISKS:", innerPad) + " ") + '</span>',
    vl
  ));

  out.push(lineHtml(vl,
    '<span class="modal-btn">' + escapeHtml(" ".repeat(inner)) + '</span>',
    vl
  ));
  out.push(lineHtml(vl,
    '<span class="modal-btn">' + escapeHtml(" ".repeat(inner)) + '</span>',
    vl
  ));

  var fixed = 1 + 1 + 1 + 2 + 3 + 1 + 1 + 1 + 1 + 1;
  var avail = Math.max(0, rows - fixed);
  var maxItems = Math.max(0, Math.floor((avail + 1) / 2));
  var showItems = items.slice(0, maxItems);

  for (var di = 0; di < showItems.length; di++) {
    var num = String(di + 1).padStart(2, " ");
    var nameWidth = innerPad - 5;
    var shown = String(showItems[di].name || "").toUpperCase();
    if (shown.length > nameWidth) shown = shown.slice(0, nameWidth);
    var tail = " ".repeat(nameWidth - shown.length);
    var action = showItems[di].action;
    var rowHtml = "";
    if (action === "dirtyDownload") {
      rowHtml =
        '<button type="button" class="action modal-btn" data-action="' + action + '" data-dirty-index="' + showItems[di].idx + '">' +
        escapeHtml(" " + num + " ") + '<span class="accent">&gt;</span>' + escapeHtml(" " + shown + tail + " ") +
        '</button>';
    } else {
      var nm = safeName(String(showItems[di].name || "").trim()) || ("disk_" + showItems[di].idx + ".bin");
      var hrefName = encodeURIComponent(String(showItems[di].name || "").toUpperCase());
      rowHtml =
        '<a class="action modal-btn" href="/modified/download/' + showItems[di].idx + '/' + hrefName + '" download="' + escapeHtml(nm) + '">' +
        escapeHtml(" " + num + " ") + '<span class="accent">&gt;</span>' + escapeHtml(" " + shown + tail + " ") +
        '</a>';
    }
    out.push(lineHtml(vl, rowHtml, vl));

    if (di !== showItems.length - 1) {
      out.push(lineHtml(vl,
        '<span class="modal-btn">' + escapeHtml(" ".repeat(inner)) + '</span>',
        vl
      ));
    }
  }

  out.push(lineHtml(vl,
    '<span class="modal-btn">' + escapeHtml(" ".repeat(inner)) + '</span>',
    vl
  ));
  out.push(lineHtml(vl,
    '<span class="modal-btn">' + escapeHtml(" ".repeat(inner)) + '</span>',
    vl
  ));
  out.push(lineHtml(vl,
    '<span class="modal-btn">' + escapeHtml(" ".repeat(inner)) + '</span>',
    vl
  ));

  var exportLabel = (IS_MOBILE || (GRID_COLS !== 40)) ? "EXPORT" : "EXPORT SESSION";
  var maxLabel = 28;
  if (exportLabel.length > maxLabel) exportLabel = exportLabel.slice(0, maxLabel);
  var exportPad = " ".repeat(Math.max(0, maxLabel - exportLabel.length));
  out.push(lineHtml(vl,
    '<button type="button" class="action modal-btn" data-action="exportActiveZip">' +
    escapeHtml(" ") + '<span class="accent">&gt;</span>' + escapeHtml(" " + exportLabel + exportPad + " ") +
    '</button>',
    vl
  ));

  out.push(lineHtml(vl,
    '<span class="modal-btn">' + escapeHtml(" ".repeat(inner)) + '</span>',
    vl
  ));

  var discardText = "CLOSE ";
  var discardNeed = discardText.length + 1; // + "<"
  var discardPad = " ".repeat(Math.max(0, innerPad - discardNeed));
  out.push(lineHtml(vl,
    '<button type="button" class="action modal-btn" data-action="dirtyDismiss" style="text-align: right;">' +
    escapeHtml(" " + discardPad + discardText) + '<span class="accent">&lt;</span>' + escapeHtml(" ") +
    '</button>',
    vl
  ));

  out.push(lineHtml(vl,
    '<span class="modal-btn">' + escapeHtml(" ".repeat(inner)) + '</span>',
    vl
  ));

  out.push('<div class="line modal-line divider">' +
    escapeHtml(bl + hl.repeat(cols - 2) + br) +
  '</div>');

  dirtyModal.innerHTML = out.join("");
}
