function uploadQueue() {
  if (!ready || uploading || queue.length === 0) return;
  uploading = true;
  renderAll();

  var nonce = lastNonce;
  var chain = Promise.resolve();

  queue.forEach(function(item) {
    chain = chain.then(function() {
      var size = item.data.length;
      var crc = crc32(item.data);
      return fetch("/upload/active/add", {
        method: "PUT",
        headers: {
          "Content-Type": "application/octet-stream",
          "X-Nonce": String(nonce),
          "X-Image-Size": String(size),
          "X-CRC32": "0x" + crc.toString(16).padStart(8, "0"),
          "X-Image-Name": item.name
        },
        body: item.data
      }).then(function(resp) {
        if (!resp.ok) throw new Error("upload failed");
        return resp.json().catch(function() { return {}; });
      });
    });
  });

  chain = chain.then(function() {
    return fetch("/upload/active/commit", {
      method: "POST",
      headers: {
        "X-Nonce": String(nonce)
      }
    }).then(function(resp) {
      if (!resp.ok) throw new Error("commit failed");
      return resp.json().catch(function() { return {}; });
    });
  }).then(function() {
    successUntil = nowMs() + SUCCESS_MS;
    waitingForReconnect = true;
    waitingSince = nowMs();
    ready = false;
  }).catch(function() {
    markError("UPLOAD FAIL");
  }).finally(function() {
    uploading = false;
    renderAll();
  });
}

function exportZip() {
  if (queue.length === 0) return;
  var zipped = buildZip(queue);
  var blob = new Blob([zipped], { type: "application/zip" });
  var url = URL.createObjectURL(blob);
  var name = "pi1541_active_set.zip";
  var a = document.createElement("a");
  a.href = url;
  a.download = name;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(function() { URL.revokeObjectURL(url); }, 1000);
}

function exportActiveZip() {
  fetch("/active/list", { cache: "no-store" }).then(function(r) {
    if (!r.ok) throw new Error("no list");
    return r.json();
  }).then(function(j) {
    var files = (j && j.files) ? j.files : [];
    if (!files.length) return;
    var items = [];
    var chain = Promise.resolve();
    files.forEach(function(f) {
      chain = chain.then(function() {
        return fetch("/active/download/" + f.i, { cache: "no-store" }).then(function(r) {
          if (!r.ok) throw new Error("download failed");
          return r.arrayBuffer();
        }).then(function(buf) {
          items.push({ name: f.name, data: new Uint8Array(buf) });
        });
      });
    });
    return chain.then(function() {
      if (!items.length) return;
      var zipped = buildZip(items);
      var blob = new Blob([zipped], { type: "application/zip" });
      var url = URL.createObjectURL(blob);
      var name = "pi1541_session.zip";
      var a = document.createElement("a");
      a.href = url;
      a.download = name;
      document.body.appendChild(a);
      a.click();
      a.remove();
      setTimeout(function() { URL.revokeObjectURL(url); }, 1000);
    });
  }).catch(function() {
    markError("EXPORT FAIL");
  });
}

function pollHello() {
  if (uploading) return;
  if (helloInFlight) return;
  helloInFlight = true;
  fetchHello().then(function(resp) {
    if (!resp.ok) throw new Error("not ready");
    return resp.json();
  }).then(function(data) {
    lastNonce = data.nonce || 0;
    if (!devDirty) {
      var mc = data.modified_count || 0;
      var mid = data.modified_id || 0;
      if (!mc) {
        modifiedCount = 0;
        modifiedId = 0;
        modifiedItems = [];
        modifiedListId = 0;
        dirtyDismissedId = 0;
        dirtyModalVisible = false;
      } else {
        modifiedCount = mc;
        if (mid !== modifiedId) {
          modifiedId = mid;
          modifiedItems = [];
          modifiedListId = 0;
          dirtyDismissedId = 0;
        }
        if (dirtyDismissedId !== modifiedId)
          dirtyModalVisible = true;
        if (modifiedListId !== modifiedId && !modifiedFetchInFlight) {
          modifiedFetchInFlight = true;
          fetch("/modified/list", { cache: "no-store" }).then(function(r) {
            if (!r.ok) throw new Error("no list");
            return r.json();
          }).then(function(j) {
            modifiedItems = (j && j.files) ? j.files : [];
            modifiedListId = modifiedId;
          }).catch(function() {
            modifiedItems = [];
            modifiedListId = 0;
          }).finally(function() {
            modifiedFetchInFlight = false;
            renderDirtyModal();
          });
        }
      }
    }

    if (waitingForReconnect) {
      if (nowMs() - waitingSince >= SUCCESS_MS) {
        waitingForReconnect = false;
        ready = true;
      } else {
        ready = false;
      }
    } else {
      ready = true;
    }
  }).catch(function() {
    ready = false;
  }).finally(function() {
    helloInFlight = false;
    renderStatus();
    renderActions();
    renderRightPanel();
    renderDirtyModal();
  });
}
