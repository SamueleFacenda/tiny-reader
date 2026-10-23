R"uploadpage9(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>TinyReader Upload</title>
<style>
body { font-family: sans-serif; margin: 2em auto; max-width: 30em; padding: 0 1em; }
button { padding: 0.6em 1.2em; font-size: 1em; margin-top: 1em; }
label { display: block; margin-top: 0.8em; }
#status { min-height: 3em; }
</style>
<script src="/bz.js"></script>
</head>
<body>
<h2>TinyReader Upload</h2>
<p>Pick a plain text book. Your browser converts it to Latin-1 and reflows the
paragraphs before sending, so the reader stores text it can display as is.</p>
<input type="file" id="file" accept=".txt,text/plain">
<label><input type="checkbox" id="compress"> Compress with bzip2: about 3x
smaller on the reader, but a slower upload and some battery cost per page
turn. Off by default.</label>
<label><input type="checkbox" id="keepbreaks"> Keep line breaks (poetry,
lists): don't join lines into paragraphs; double newlines still become one.
Off by default.</label>
<button id="go">Upload</button>
<p id="status"></p>
<script>
// Characters the reader font (0x20-0xFE) lacks and that NFKD does not decompose.
var EXTRA = {
  0x2018:"'", 0x2019:"'", 0x201A:"'", 0x201B:"'",
  0x201C:'"', 0x201D:'"', 0x201E:'"', 0x00AB:'"', 0x00BB:'"',
  0x2013:'-', 0x2014:'-', 0x2015:'-', 0x2212:'-',
  0x2022:'*', 0x00B7:'*', 0x2192:'->', 0x2190:'<-',
  0x00A0:' ', 0x2007:' ', 0x2009:' ', 0x202F:' ',
  0x00AD:'',  0xFEFF:'',  0x00FF:'y'
};

// True for text that already holds one paragraph per line, so reflowing it a second
// time cannot collapse its paragraphs into one. Books are hard wrapped at 60 to 80
// columns, so a line past 120 characters is an already-unwrapped paragraph.
function alreadyReflowed(text) {
  if (/\n[ \t]*\n/.test(text)) return false;   // blank lines mean raw wrapping
  return text.split('\n').some(function (line) { return line.length > 120; });
}

// Reflow to one line per paragraph: the reader wraps text itself, on a screen far
// narrower than the ~70 columns books are usually hard wrapped at. The keepbreaks
// checkbox opts out of the joining: every newline survives and runs collapse to a
// single one, which is what the already-unwrapped path below does anyway.
function reflow(text) {
  var keep = document.getElementById('keepbreaks').checked;
  var unwrap = !alreadyReflowed(text) && !keep;
  return text
    .replace(/[\u0000-\u0008\u000B-\u001F]/g, '')
    .replace(/\r\n?/g, '\n')
    .replace(/[ \t]+$/gm, '')                             // trailing blanks
    .replace(/([a-zß-ÿ])-\n(?=[a-zß-ÿ])/g, '$1')  // join hyphenated words
    .replace(/\n[ \t]+/g, '\n\n')                         // an indent starts a paragraph
    .replace(/\n{2,}/g, '\u0001')                        // park the real breaks
    .replace(/\n/g, unwrap ? ' ' : '\u0001')             // unwrap everything else
    .replace(/\u0001/g, '\n')                            // and put them back
    .replace(/^[ \t]+/gm, '')                             // never leave an indent
    .replace(/\t/g, ' ')
    .replace(/ {2,}/g, ' ')
    .trim() + '\n';
}

// The iconv -t ISO-8859-1//TRANSLIT equivalent: keep what Latin-1 covers and
// approximate the rest by stripping the combining marks NFKD exposes.
function toLatin1(text) {
  var out = [];
  for (var ch of text) {
    var cp = ch.codePointAt(0);
    if (cp === 10) { out.push(10); continue; }
    var rep = EXTRA[cp];
    if (rep === undefined) {
      if (cp >= 32 && cp <= 0xFE) { out.push(cp); continue; }
      rep = ch.normalize('NFKD').replace(/\p{M}/gu, '');
    }
    for (var r of rep) {
      var rc = r.codePointAt(0);
      out.push(rc >= 32 && rc <= 0xFE ? rc : 63);         // 63 is '?'
    }
  }
  return new Uint8Array(out);
}

function say(msg) { document.getElementById('status').textContent = msg; }

// TRZ container, the twin of src/TrzFormat.h: one independent bzip2 stream
// per uncompressed chunk, a front index of chunk offsets, every multi-byte
// field little-endian. The reader serves page reads from the chunk the index
// points at, so positions stay plain text offsets.
var TRZ_LEVEL = 4;      // bzip2 blockSize100k, recorded in the header
var TRZ_CHUNK = 400000; // uncompressed bytes per chunk

function w32(bytes, value, at) {
  bytes[at] = value & 255;
  bytes[at + 1] = (value >>> 8) & 255;
  bytes[at + 2] = (value >>> 16) & 255;
  bytes[at + 3] = (value >>> 24) & 255;
}

function buildTrz(bytes) {
  var count = Math.max(1, Math.ceil(bytes.length / TRZ_CHUNK));
  var chunks = [];
  var offsets = [];
  var cursor = 0;
  for (var i = 0; i < count; i++) {
    var slice = bytes.subarray(i * TRZ_CHUNK, Math.min((i + 1) * TRZ_CHUNK, bytes.length));
    var out = [];
    TrzBzip2.compressFile(slice, { writeByte: function (b) { out.push(b); } }, TRZ_LEVEL);
    var chunk = new Uint8Array(out);
    chunks.push(chunk);
    offsets.push(cursor);
    cursor += chunk.length;
  }
  var trz = new Uint8Array(20 + 4 * count + cursor);
  trz[0] = 84; trz[1] = 82; trz[2] = 90; trz[3] = 49;   // "TRZ1"
  trz[4] = 1;                                          // container version
  trz[5] = TRZ_LEVEL;
  w32(trz, TRZ_CHUNK, 8);
  w32(trz, bytes.length, 12);
  w32(trz, count, 16);
  for (var j = 0; j < count; j++) { w32(trz, offsets[j], 20 + 4 * j); }
  var at = 20 + 4 * count;
  chunks.forEach(function (c) { trz.set(c, at); at += c.length; });
  return trz;
}

async function upload() {
  var picked = document.getElementById('file').files[0];
  if (!picked) { say('Pick a file first.'); return; }
  var compress = document.getElementById('compress').checked;
  if (compress && typeof TrzBzip2 === 'undefined') {
    say('The compressor failed to load; reload the page.');
    return;
  }
  say('Converting...');
  var buffer = await picked.arrayBuffer();
  var text;
  try {
    text = new TextDecoder('utf-8', {fatal: true}).decode(buffer);
  } catch (e) {
    text = new TextDecoder('iso-8859-1').decode(buffer);  // already converted
  }
  var bytes = toLatin1(reflow(text));
  var payload = bytes;
  if (compress) {
    say('Compressing ' + bytes.length + ' bytes... this can take a while');
    await new Promise(function (r) { setTimeout(r, 30); });  // let the status paint
    try {
      payload = buildTrz(bytes);
    } catch (e) {
      say('Compression failed: ' + e.message);
      return;
    }
  }
  say((compress
    ? 'Uploading ' + payload.length + ' of ' + bytes.length + ' bytes...'
    : 'Uploading ' + bytes.length + ' bytes...'));
  var form = new FormData();
  form.append('file', new Blob([payload], {type: 'application/octet-stream'}), picked.name);
  try {
    var response = await fetch('/upload', {method: 'POST', body: form});
    var body = await response.text();
    say(response.ok
      ? 'Stored ' + picked.name + ' (' + buffer.byteLength + ' -> ' + payload.length + ' bytes'
        + (compress ? ', compressed' : '') + ')'
      : 'Failed: ' + body);
  } catch (e) {
    say('Failed: ' + e.message);
  }
}

document.getElementById('go').addEventListener('click', upload);
</script>
</body>
</html>
)uploadpage9"
