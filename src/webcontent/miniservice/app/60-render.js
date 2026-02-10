function renderStatus() {
  if (transientStatusText && transientStatusUntil > nowMs()) {
    setStatus(transientStatusText, transientStatusColors);
    return;
  }
  if (transientStatusText && transientStatusUntil <= nowMs()) {
    clearTransientStatus();
  }
  if (errorUntil > nowMs()) {
    setStatus("ERROR", STATUS_ERROR_COLORS);
    return;
  }
  if (successUntil > nowMs()) {
    setStatus("SUCCESS");
    return;
  }
  if (uploading) {
    setStatus("SENDING");
    return;
  }
  if (waitingForReconnect) {
    setStatus("SEARCHING");
    return;
  }
  setStatus(ready ? "READY" : "SEARCHING");
}

function renderAll() {
  renderStatus();
  renderActions();
  renderBottomButtons();
  renderList();
  renderRightPanel();
  renderDirtyModal();
}
