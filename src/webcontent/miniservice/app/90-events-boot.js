document.getElementById("screen").addEventListener("click", function(e) {
  var target = e.target;
  if (target && target.nodeType === 3) target = target.parentElement;
  var actionEl = target && target.closest ? target.closest("[data-action]") : null;
  if (actionEl && actionEl.dataset && actionEl.dataset.action) {
    if (dirtyModalVisible && dirtyModal && !dirtyModal.contains(actionEl)) return;

    var action = actionEl.dataset.action;
    if (action === "dirtyDismiss") {
      dirtyModalVisible = false;
      if (!devDirty)
        dirtyDismissedId = modifiedId;
      renderDirtyModal();
      return;
    }
    if (action === "dirtyDownload") {
      var idx = parseInt(actionEl.dataset.dirtyIndex || "0", 10);
      if (devDirty) devDownloadDirty(idx);
      return;
    }
    if (action === "modifiedDownload") {
      var i = parseInt(actionEl.dataset.modifiedI || "0", 10);
      var nm = actionEl.dataset.modifiedName || ("disk_" + i + ".bin");
      if (!i) return;
      var a = document.createElement("a");
      a.href = "/modified/download/" + i;
      a.download = safeName(String(nm || "").trim()) || ("disk_" + i + ".bin");
      document.body.appendChild(a);
      a.click();
      a.remove();
      return;
    }
    if (action === "exportActiveZip") {
      exportActiveZip();
      return;
    }

    if (action === "insertDisks") fileInput.click();
    if (action === "insertFolder") folderInput.click();
    if (action === "mountDisks") uploadQueue();
    if (action === "clearDisks") { clearDisks(); renderAll(); }
    if (action === "toggleOptimize") {
      if (optimized) undoOptimize(); else optimizeNames();
      renderAll();
    }
    if (action === "exportZip") exportZip();
  }
  var upEl = target && target.closest ? target.closest("[data-up]") : null;
  if (upEl && upEl.dataset && upEl.dataset.up) {
    moveUp(parseInt(upEl.dataset.up, 10));
    renderAll();
  }
  var indexEl = target && target.closest ? target.closest("[data-index]") : null;
  if (indexEl && indexEl.dataset && indexEl.dataset.index) {
    renameItem(parseInt(indexEl.dataset.index, 10));
    renderAll();
  }
});

document.addEventListener("keydown", function(e) {
  if (e.key === "Escape" && dirtyModalVisible) {
    dirtyModalVisible = false;
    if (!devDirty)
      dirtyDismissedId = modifiedId;
    renderDirtyModal();
    e.preventDefault();
  }
});

fileInput.addEventListener("change", function(e) {
  handleFiles(e.target.files).then(function() {
    fileInput.value = "";
  });
});

folderInput.addEventListener("change", function(e) {
  handleFiles(e.target.files).then(function() {
    folderInput.value = "";
  });
});

setInterval(pollHello, POLL_MS);
setInterval(renderStatus, 250);
setInterval(tickStatusCycle, 50);
renderAll();
