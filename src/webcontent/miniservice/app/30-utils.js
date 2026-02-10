function extOf(name) {
  var idx = name.lastIndexOf(".");
  if (idx === -1) return "";
  return name.slice(idx + 1).toLowerCase();
}

function baseOf(name) {
  var idx = name.lastIndexOf(".");
  if (idx === -1) return name;
  return name.slice(0, idx);
}

function safeName(name) {
  return name.replace(/[\\/]/g, "_");
}

function escapeHtml(s) {
  return String(s)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

function truncateName(name, maxLen) {
  if (name.length <= maxLen) return name;
  return name.slice(0, maxLen);
}

function devDownloadDirty(index) {
  if (!queue[index] || !queue[index].data) return;
  var item = queue[index];
  var blob = new Blob([item.data], { type: "application/octet-stream" });
  var url = URL.createObjectURL(blob);
  var a = document.createElement("a");
  a.href = url;
  a.download = item.name || ("disk_" + (index + 1) + ".bin");
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(function() { URL.revokeObjectURL(url); }, 1000);
}

