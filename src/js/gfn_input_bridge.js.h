// Generated from src/js/gfn_input_bridge.js — do not edit by hand.
#pragma once
static const char gfnInputBridgeJS[] =
R"GFNJS(
// GFN input bridge (injected into play.geforcenow.com)
// Opens the NVST input data channels that GeForce NOW's in-page client fails to
// create in this WebKit build (its JS crashes on "f.protocol" before creating
// them), then lets the host app forward real keyboard/mouse via window.__imwbInput.
(function () {
    try {
        if (!window.__imwbBridge && typeof RTCPeerConnection === "undefined") {
            console.log("[imwb-input] no RTCPeerConnection");
            return;
        }
        if (window.__imwbBridge) {
            console.log("[imwb-input] already armed");
            return;
        }

        var L = function () {
            var a = Array.prototype.slice.call(arguments);
            a.unshift("[imwb-input]");
            console.log.apply(console, a);
        };

        // ---- Protocol v3 helpers (matches NVIDIA GFN web protocol) ----
        var INPUT_HEARTBEAT = 2, INPUT_KEY_DOWN = 3, INPUT_KEY_UP = 4;
        var INPUT_MOUSE_ABS = 5, INPUT_MOUSE_REL = 7;
        var INPUT_MOUSE_BUTTON_DOWN = 8, INPUT_MOUSE_BUTTON_UP = 9, INPUT_MOUSE_WHEEL = 10;

        function tsUs() {
            var m = (typeof performance !== "undefined") ? performance.timeOrigin : 0;
            var now = (typeof performance !== "undefined") ? performance.now() : Date.now();
            return BigInt(Math.round(m + now)) * 1000n;
        }
        function writeTsBE(view, off) {
            var t = tsUs();
            view.setBigUint64(off, t, false);
        }
        function tsUs() {
            // Session-relative microseconds since the input handshake completed,
            // matching the official client's session input clock (Or()/ed()).
            var start = bridge.sessionStartMs || performance.now();
            var elapsed = performance.now() - start;
            if (elapsed < 0) elapsed = 0;
            return Math.floor(elapsed * 1000);
        }
        // Write 8B big-endian session-relative microseconds (hi then lo).
        function writeTsUs(view, off) {
            var t = tsUs();
            var hi = Math.floor(t / 4294967296);
            var lo = t % 4294967296;
            view.setUint32(off, hi, false);
            view.setUint32(off + 4, lo, false);
        }
        function wrapSingle(payload) {
            // [0x23][8B ts BE][0x22][payload]
            var w = new Uint8Array(9 + 1 + payload.length);
            var v = new DataView(w.buffer);
            w[0] = 0x23;
            writeTsUs(v, 1);
            w[9] = 0x22;
            w.set(payload, 10);
            return w;
        }
        function wrapMouseMove(payload) {
            // [0x23][8B ts BE][0x21][2B len BE][payload]
            var w = new Uint8Array(9 + 1 + 2 + payload.length);
            var v = new DataView(w.buffer);
            w[0] = 0x23;
            writeTsUs(v, 1);
            w[9] = 0x21;
            v.setUint16(10, payload.length, false);
            w.set(payload, 12);
            return w;
        }

        // ---- Bridge state ----
        var bridge = {};
        bridge.inputChannel = null;      // input_channel_v1 (reliable)
        bridge.prInputChannel = null;    // input_channel_partially_reliable (mouse)
        bridge.inputReady = false;
        bridge.protocolVersion = 2;
        bridge.hbTimer = null;
        bridge.channelPc = null;         // which RTCPeerConnection the channels live on
        bridge.sessionStartMs = 0;       // session-relative input clock epoch (set on handshake)

        // ---- Senders ----
        function sendReliable(bytes) { if (bridge.inputChannel && bridge.inputChannel.readyState === "open") bridge.inputChannel.send(bytes); }
        function sendPR(bytes) { if (bridge.prInputChannel && bridge.prInputChannel.readyState === "open") bridge.prInputChannel.send(bytes); }

        function startHeartbeat() {
            if (bridge.hbTimer) return;
            bridge.hbTimer = setInterval(function () {
                var hb = new Uint8Array(4);
                new DataView(hb.buffer).setUint32(0, INPUT_HEARTBEAT, true);
                sendReliable(hb);
            }, 5000);
        }

        function onInputChannelMessage(ev) {
            var data = ev.data;
            var bytes = (data instanceof ArrayBuffer) ? new Uint8Array(data)
                : (data && data.buffer instanceof ArrayBuffer) ? new Uint8Array(data.buffer, data.byteOffset, data.byteLength)
                : new TextEncoder().encode(String(data));
            if (bytes.length < 2) return;
            var view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
            var firstWord = view.getUint16(0, true);
            if (!bridge.inputReady) {
                var version = 2;
                if (firstWord === 526) { version = bytes.length >= 4 ? view.getUint16(2, true) : 2; L("handshake firstWord=526 version=" + version); }
                else if (bytes[0] === 0x0e) { version = firstWord; L("handshake byte0=e version=" + version); }
                else { L("input msg not handshake firstWord=" + firstWord + " (0x" + firstWord.toString(16) + ")"); return; }
                bridge.protocolVersion = version;
                bridge.inputReady = true;
                bridge.sessionStartMs = performance.now();
                L("INPUT READY protocol v" + version);
                startHeartbeat();
            }
        }

        // The stream PC is the one carrying media receivers (the mall/queue PC
        // has none). Never open input channels on a receiver-less PC, or we
        // lock the bridge onto the wrong connection forever. GFN rebuilds its
        // streaming PC when the network path changes (e.g. cable plugged in
        // mid-session); a dead PC still lists its old receivers, so only
        // consider live connection states and strongly prefer "connected".
        function streamPc() {
            var best = null, bestScore = -1;
            bridges.pcs.forEach(function (pc) {
                var st = "";
                try { st = pc.connectionState || ""; } catch (e) {}
                if (st === "failed" || st === "disconnected" || st === "closed") return;
                var r = pc && pc.getReceivers ? pc.getReceivers().length : 0;
                if (r <= 0) return;
                var score = r + (st === "connected" ? 1000 : 0);
                if (score > bestScore) { bestScore = score; best = pc; }
            });
            return bestScore > 0 ? best : null;
        }

        // Drop the channel binding when the owning PC or its channels die, so
        // arm() can re-open input channels on the next live stream PC.
        function releaseChannels(pc) {
            if (bridge.channelPc !== pc) return;
            bridge.channelPc = null;
            bridge.inputChannel = null;
            bridge.prInputChannel = null;
            bridge.inputReady = false;
            L("released channels from dead stream pc");
        }

        function openChannels(pc) {
            if (!pc) return;
            if (bridge.channelPc === pc) { L("channels already open on stream pc"); return; }
            try {
                var recv = pc.getReceivers ? pc.getReceivers().length : 0;
                if (recv <= 0) { L("refusing to open channels on pc, receivers=" + recv); return; }
                bridge.channelPc = pc;
                bridge.inputChannel = pc.createDataChannel("input_channel_v1", { ordered: true });
                bridge.inputChannel.binaryType = "arraybuffer";
                bridge.inputChannel.onopen = function () { L("input_channel_v1 OPEN"); };
                bridge.inputChannel.onmessage = onInputChannelMessage;
                bridge.inputChannel.onerror = function (e) { L("input_channel_v1 error", e && e.message); };
                bridge.inputChannel.onclose = function () { L("input_channel_v1 closed"); releaseChannels(pc); };

                bridge.prInputChannel = pc.createDataChannel("input_channel_partially_reliable", { ordered: false, maxPacketLifeTime: 200 });
                bridge.prInputChannel.binaryType = "arraybuffer";
                bridge.prInputChannel.onopen = function () { L("input_channel_partially_reliable OPEN"); };
                bridge.prInputChannel.onerror = function (e) { L("pr channel error", e && e.message); };
                bridge.prInputChannel.onclose = function () { L("pr channel closed"); releaseChannels(pc); };
                L("created input channels on stream pc (receivers=" + recv + ")");
            } catch (e) {
                L("openChannels error", e && e.message);
            }
        }

        // GFN starts the game's stream media element muted and its own unmute
        // path never fires in this WebKit build, so the WebProcess audio sink
        // stays muted at the PulseAudio/PipeWire level (no game audio heard).
        // Unmute only elements playing a live MediaStream (the game stream);
        // the mall's http-sourced trailer videos stay muted.
        function unmuteStreamAudio() {
            try {
                var els = document.querySelectorAll("video, audio");
                for (var i = 0; i < els.length; i++) {
                    var e = els[i];
                    var so = e.srcObject;
                    if (!so || !(so instanceof MediaStream)) continue;
                    var live = false;
                    so.getAudioTracks().forEach(function (t) { if (t.readyState === "live") live = true; });
                    if (!live) continue;
                    if (e.muted) {
                        e.muted = false;
                        if (e.volume === 0) e.volume = 1;
                        L("unmuted stream element " + e.tagName + " (live audio tracks)");
                    }
                }
            } catch (e) {}
        }

        // ---- public send API ----
        var api = {
            get ready() { return bridge.inputReady; },
            key: function (type, vk, modifiers, scancode) {
                // type: 3=down, 4=up ; vk: Windows virtual key ; modifiers: per-key byte
                var p = new Uint8Array(18);
                var v = new DataView(p.buffer);
                v.setUint32(0, type, true);
                v.setUint16(4, vk & 0xffff, false);
                v.setUint16(6, modifiers & 0xffff, false);
                v.setUint16(8, scancode & 0xffff, false);
                writeTsUs(v, 10);
                sendReliable(wrapSingle(p));
            },
            mouseMove: function (dx, dy) {
                var p = new Uint8Array(22);
                var v = new DataView(p.buffer);
                v.setUint32(0, INPUT_MOUSE_REL, true);
                v.setInt16(4, dx, false);
                v.setInt16(6, dy, false);
                v.setUint16(8, 0, false);
                v.setUint32(10, 0, false);
                writeTsUs(v, 14);
                sendPR(wrapMouseMove(p));
            },
            mouseAbs: function (x, y, w, h) {
                var p = new Uint8Array(26);
                var v = new DataView(p.buffer);
                v.setUint32(0, INPUT_MOUSE_ABS, true);
                v.setUint16(4, Math.max(0, Math.min(65535, Math.round(x))), false);
                v.setUint16(6, Math.max(0, Math.min(65535, Math.round(y))), false);
                v.setUint16(8, 0, false);
                v.setUint16(10, Math.max(0, Math.min(65535, w)), false);
                v.setUint16(12, Math.max(0, Math.min(65535, h)), false);
                v.setUint32(14, 0, false);
                writeTsUs(v, 18);
                sendPR(wrapMouseMove(p));
            },
            mouseButton: function (type, button) {
                // type: 8=down, 9=up ; button: 1=left,2=mid,3=right,4=back,5=fwd
                var p = new Uint8Array(18);
                var v = new DataView(p.buffer);
                v.setUint32(0, type, true);
                v.setUint8(4, button & 0xff);
                v.setUint8(5, 0);
                v.setUint32(6, 0, false);
                writeTsUs(v, 10);
                sendReliable(wrapSingle(p));
            },
            mouseWheel: function (delta) {
                var p = new Uint8Array(22);
                var v = new DataView(p.buffer);
                v.setUint32(0, INPUT_MOUSE_WHEEL, true);
                v.setInt16(4, 0, false);
                v.setInt16(6, delta, false);
                v.setUint16(8, 0, false);
                v.setUint32(10, 0, false);
                writeTsUs(v, 14);
                sendReliable(wrapSingle(p));
            },
            // arm(): attempt channels on the stream PC (the one with media receivers)
            arm: function () {
                L("arm() called, pcCount=" + bridges.pcs.size + ", ready=" + bridge.inputReady);
                unmuteStreamAudio();
                var pc = streamPc();
                if (!pc) { L("no stream pc yet (no receivers)"); return; }
                openChannels(pc);
            },
            status: function () {
                return JSON.stringify({ ready: bridge.inputReady, channels: !!(bridge.inputChannel), pcCount: bridges.pcs.size });
            }
        };
        window.__imwbInput = api;

        // ---- Track PC instances ----
        var bridges = {};
        bridges.pcs = new Set();
        var OrigPC = RTCPeerConnection;
        function createPC() {
            var pc = new (Function.prototype.bind.apply(OrigPC, [null].concat(Array.prototype.slice.call(arguments))))();
            try {
                pc.addEventListener("connectionstatechange", function () {
                    var st = pc.connectionState;
                    if (st === "connected") {
                        // pick this as stream pc when it has media receivers
                        var recv = pc.getReceivers ? pc.getReceivers().length : 0;
                        L("pc connected receivers=" + recv + " channels=" + !!bridge.inputChannel);
                    }
                    if (st === "failed" || st === "disconnected" || st === "closed")
                        releaseChannels(pc);
                });
            } catch (e) {}
            bridges.pcs.add(pc);
            L("new RTCPeerConnection, count=" + bridges.pcs.size);
            return pc;
        }
        // Preserve static members
        Object.keys(OrigPC).forEach(function (k) { if (!(k in createPC)) createPC[k] = OrigPC[k]; });
        createPC.prototype = OrigPC.prototype;
        RTCPeerConnection = createPC;
        window.RTCPeerConnection = createPC;

        // ---- Stuck-shutdown watchdog ----
        // GFN's cloud rig teardown (the "Steam is closing" screen) sometimes
        // never completes in this WebKit build: the NVST signaling websocket
        // retries its handshake forever and the page sits on the closing
        // screen. Steam itself is closed cloud-side and nothing we do makes it
        // faster, but once the session is fully dead (no connected stream pc
        // and no live media tracks) for a sustained period, navigate back to
        // the mall so the kiosk recovers on its own instead of hanging.
        var sawLiveSession = false;
        var streamDeadSince = 0;
        var WATCHDOG_MS = 45000;
        function sessionAlive() {
            var alive = false;
            bridges.pcs.forEach(function (pc) {
                try {
                    if (pc.connectionState === "connected" && pc.getReceivers && pc.getReceivers().length > 0) alive = true;
                } catch (e) {}
            });
            var els = document.querySelectorAll("video, audio");
            for (var i = 0; i < els.length; i++) {
                var so = els[i].srcObject;
                if (so && so instanceof MediaStream) {
                    so.getTracks().forEach(function (t) { if (t.readyState === "live") alive = true; });
                }
            }
            return alive;
        }
        setInterval(function () {
            try {
                if (sessionAlive()) {
                    sawLiveSession = true;
                    streamDeadSince = 0;
                    return;
                }
                // Mall/queue browsing before any session: nothing to recover.
                if (!sawLiveSession) return;
                if (!streamDeadSince) {
                    streamDeadSince = Date.now();
                    L("watchdog: stream gone, waiting before recovery");
                    return;
                }
                if (Date.now() - streamDeadSince >= WATCHDOG_MS) {
                    L("watchdog: stream dead for " + Math.round((Date.now() - streamDeadSince) / 1000) +
                      "s (stuck shutdown?); returning to mall");
                    window.location.href = "https://play.geforcenow.com/mall/";
                }
            } catch (e) {}
        }, 2000);

        // ---- NVST signaling-reconnect watchdog ----
        // The "Steam is closing" hang streams its screen through a still-live
        // media session, so the dead-stream watchdog above cannot see it. The
        // signature of the hang is instead the NVST signaling client retrying
        // its reconnect handshake forever: every attempt fails the WebSocket
        // handshake. Count consecutive failed reconnects; past a threshold,
        // return to the mall.
        var nvstFailCount = 0;
        try {
            var OrigWS = window.WebSocket;
            function createWS(url, protocols) {
                var ws = (protocols === undefined) ? new OrigWS(url) : new OrigWS(url, protocols);
                try {
                    var u = String(url);
                    if (u.indexOf("/nvst/sign_in") !== -1 && u.indexOf("reconnect=1") !== -1) {
                        var opened = false;
                        ws.addEventListener("open", function () { opened = true; nvstFailCount = 0; });
                        ws.addEventListener("close", function () {
                            if (opened) return;
                            nvstFailCount++;
                            L("nvst reconnect attempt failed (" + nvstFailCount + ")");
                            if (nvstFailCount >= 12) {
                                nvstFailCount = 0;
                                L("watchdog: nvst reconnect loop (stuck shutdown?); returning to mall");
                                window.location.href = "https://play.geforcenow.com/mall/";
                            }
                        });
                    }
                } catch (e) {}
                return ws;
            }
            Object.keys(OrigWS).forEach(function (k) { if (!(k in createWS)) createWS[k] = OrigWS[k]; });
            createWS.prototype = OrigWS.prototype;
            window.WebSocket = createWS;
        } catch (e) { L("ws hook error", e && e.message); }

        L("injected; hook active");
    } catch (e) {
        console.log("[imwb-input] inject error", e && e.message);
    }
})();
)GFNJS";
