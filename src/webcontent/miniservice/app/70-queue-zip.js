function addToQueue(name, data) {
  if (queue.length >= MAX_ITEMS) return false;
  var safe = safeName(name);
  queue.push({
    name: safe,
    originalName: safe,
    data: data
  });
  return true;
}

function handleFiles(files) {
  if (!files || files.length === 0) return;
  var list = Array.prototype.slice.call(files);
  return list.reduce(function(p, file) {
    return p.then(function() {
      if (queue.length >= MAX_ITEMS) return;
      return handleFile(file);
    });
  }, Promise.resolve()).then(function() {
    renderAll();
  });
}

function handleFile(file) {
  var name = file.name;
  var ext = extOf(name);
  if (ext === "zip") {
    return file.arrayBuffer().then(function(buf) {
      return unzipAndAddAsync(new Uint8Array(buf));
    }).catch(function() {
      markError("ZIP ERROR");
    });
  }
  if (!supportedExt[ext]) return Promise.resolve();
  return file.arrayBuffer().then(function(buf) {
    addToQueue(name, new Uint8Array(buf));
  }).catch(function() {
    markError("FILE ERROR");
  });
}

function unzipAndAddAsync(bytes) {
  return readZipEntries(bytes).then(function(entries) {
    var hasSupported = false;
    entries.forEach(function(entry) {
      if (entry.name.endsWith("/")) return;
      var ext = extOf(entry.name);
      if (!supportedExt[ext]) return;
      hasSupported = true;
      if (queue.length >= MAX_ITEMS) return;
      addToQueue(entry.name.split("/").pop(), entry.data);
    });
    if (!hasSupported) {
      markError("NO SUPPORTED");
    }
  }).catch(function() {
    markError("ZIP ERROR");
  });
}

function readZipEntries(bytes) {
  var u8 = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  var dv = new DataView(u8.buffer, u8.byteOffset, u8.byteLength);
  var eocd = u8.length - 22;
  while (eocd >= 0 && dv.getUint32(eocd, true) !== 0x06054b50) eocd--;
  if (eocd < 0) return Promise.reject(new Error("invalid zip"));
  var cdCount = dv.getUint16(eocd + 10, true);
  var cdOffset = dv.getUint32(eocd + 16, true);
  var entries = [];
  var ptr = cdOffset;
  for (var i = 0; i < cdCount; i++) {
    if (dv.getUint32(ptr, true) !== 0x02014b50) break;
    var compMethod = dv.getUint16(ptr + 10, true);
    var compSize = dv.getUint32(ptr + 20, true);
    var nameLen = dv.getUint16(ptr + 28, true);
    var extraLen = dv.getUint16(ptr + 30, true);
    var commentLen = dv.getUint16(ptr + 32, true);
    var localOffset = dv.getUint32(ptr + 42, true);
    var name = new TextDecoder().decode(u8.subarray(ptr + 46, ptr + 46 + nameLen));
    entries.push({
      name: name,
      compMethod: compMethod,
      compSize: compSize,
      localOffset: localOffset
    });
    ptr += 46 + nameLen + extraLen + commentLen;
  }
  var tasks = entries.map(function(entry) {
    var lp = entry.localOffset;
    if (dv.getUint32(lp, true) !== 0x04034b50) return null;
    var lfNameLen = dv.getUint16(lp + 26, true);
    var lfExtraLen = dv.getUint16(lp + 28, true);
    var dataStart = lp + 30 + lfNameLen + lfExtraLen;
    var compData = u8.subarray(dataStart, dataStart + entry.compSize);
    if (entry.compMethod === 0) {
      return Promise.resolve({ name: entry.name, data: compData.slice() });
    }
    if (entry.compMethod === 8) {
      return inflateRaw(compData).then(function(data) {
        return { name: entry.name, data: data };
      });
    }
    return null;
  }).filter(function(t) { return t; });
  if (entries.length > 0 && tasks.length === 0) {
    return Promise.reject(new Error("unsupported zip entries"));
  }
  return Promise.all(tasks);
}

function inflateRaw(data) {
  if (typeof DecompressionStream === "undefined") {
    return Promise.reject(new Error("deflate not supported"));
  }
  var ds = new DecompressionStream("deflate-raw");
  var stream = new Blob([data]).stream().pipeThrough(ds);
  return new Response(stream).arrayBuffer().then(function(buf) {
    return new Uint8Array(buf);
  });
}

function moveUp(index) {
  if (index <= 0 || index >= queue.length) return;
  var tmp = queue[index - 1];
  queue[index - 1] = queue[index];
  queue[index] = tmp;
}

function renameItem(index) {
  var item = queue[index];
  if (!item) return;
  var next = prompt("Rename", item.name);
  if (!next) return;
  next = safeName(next.trim());
  if (!next) return;
  item.name = next;
}

function optimizeNames() {
  optimized = true;
  for (var i = 0; i < queue.length; i++) {
    var item = queue[i];
    if (!item) continue;
    item.originalName = item.originalName || item.name;
    var ext = extOf(item.originalName);
    var base = baseOf(item.originalName);
    var prefix = String(i + 1).padStart(2, "0") + "_";
    var extSuffix = ext ? "." + ext : "";
    var maxBase = NAME_MAIN - prefix.length;
    if (maxBase < 1) maxBase = 1;
    var shortBase = base.slice(0, maxBase);
    item.name = prefix + shortBase + extSuffix;
  }
}

function undoOptimize() {
  optimized = false;
  for (var i = 0; i < queue.length; i++) {
    var item = queue[i];
    if (!item) continue;
    if (item.originalName) item.name = item.originalName;
  }
}

function clearDisks() {
  queue = [];
  optimized = false;
}

function buildZip(items) {
  var localParts = [];
  var cdParts = [];
  var offset = 0;
  var now = new Date();
  var dosTime = ((now.getHours() << 11) | (now.getMinutes() << 5) | (now.getSeconds() / 2)) & 0xffff;
  var dosDate = (((now.getFullYear() - 1980) << 9) | ((now.getMonth() + 1) << 5) | now.getDate()) & 0xffff;

  function u16(v) { return [v & 255, (v >> 8) & 255]; }
  function u32(v) { return [v & 255, (v >> 8) & 255, (v >> 16) & 255, (v >> 24) & 255]; }

  items.forEach(function(item) {
    var data = item.data;
    var nameBytes = new TextEncoder().encode(item.name);
    var crc = crc32(data);
    var compMethod = 0;
    var compSize = data.length;
    var uncompSize = data.length;

    var local = [];
    local.push(0x50, 0x4b, 0x03, 0x04);
    local.push(20, 0);
    local.push(0, 0);
    local.push(compMethod, 0);
    local.push(dosTime & 255, (dosTime >> 8) & 255);
    local.push(dosDate & 255, (dosDate >> 8) & 255);
    local.push.apply(local, u32(crc));
    local.push.apply(local, u32(compSize));
    local.push.apply(local, u32(uncompSize));
    local.push.apply(local, u16(nameBytes.length));
    local.push(0, 0);
    localParts.push(new Uint8Array(local));
    localParts.push(nameBytes);
    localParts.push(data);

    var cd = [];
    cd.push(0x50, 0x4b, 0x01, 0x02);
    cd.push(20, 0);
    cd.push(20, 0);
    cd.push(0, 0);
    cd.push(compMethod, 0);
    cd.push(dosTime & 255, (dosTime >> 8) & 255);
    cd.push(dosDate & 255, (dosDate >> 8) & 255);
    cd.push.apply(cd, u32(crc));
    cd.push.apply(cd, u32(compSize));
    cd.push.apply(cd, u32(uncompSize));
    cd.push.apply(cd, u16(nameBytes.length));
    cd.push(0, 0);
    cd.push(0, 0);
    cd.push(0, 0);
    cd.push(0, 0);
    cd.push(0, 0, 0, 0);
    cd.push.apply(cd, u32(offset));
    cdParts.push(new Uint8Array(cd));
    cdParts.push(nameBytes);

    offset += localParts[localParts.length - 3].length + localParts[localParts.length - 2].length + data.length;
  });

  var cdStart = offset;
  var cdSize = 0;
  var out = [];
  localParts.forEach(function(p) { out.push(p); });
  cdParts.forEach(function(p) { out.push(p); cdSize += p.length; });

  var eocd = [];
  eocd.push(0x50, 0x4b, 0x05, 0x06);
  eocd.push(0, 0, 0, 0);
  eocd.push.apply(eocd, u16(items.length));
  eocd.push.apply(eocd, u16(items.length));
  eocd.push.apply(eocd, u32(cdSize));
  eocd.push.apply(eocd, u32(cdStart));
  eocd.push(0, 0);
  out.push(new Uint8Array(eocd));

  var total = 0;
  out.forEach(function(p) { total += p.length; });
  var res = new Uint8Array(total);
  var pos = 0;
  out.forEach(function(p) {
    res.set(p, pos);
    pos += p.length;
  });
  return res;
}

function crc32(bytes) {
  var table = crc32.table || (crc32.table = (function() {
    var c;
    var t = [];
    for (var n = 0; n < 256; n++) {
      c = n;
      for (var k = 0; k < 8; k++) {
        c = ((c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1));
      }
      t[n] = c >>> 0;
    }
    return t;
  })());
  var crc = 0 ^ (-1);
  for (var i = 0; i < bytes.length; i++) {
    crc = (crc >>> 8) ^ table[(crc ^ bytes[i]) & 0xFF];
  }
  return (crc ^ (-1)) >>> 0;
}
