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
        // lock the bridge onto the wrong connection forever.
        function streamPc() {
            var best = null, bestRecv = -1;
            bridges.pcs.forEach(function (pc) {
                var r = pc && pc.getReceivers ? pc.getReceivers().length : 0;
                if (r > bestRecv) { bestRecv = r; best = pc; }
            });
            return bestRecv > 0 ? best : null;
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
                bridge.inputChannel.onclose = function () { L("input_channel_v1 closed"); };

                bridge.prInputChannel = pc.createDataChannel("input_channel_partially_reliable", { ordered: false, maxPacketLifeTime: 200 });
                bridge.prInputChannel.binaryType = "arraybuffer";
                bridge.prInputChannel.onopen = function () { L("input_channel_partially_reliable OPEN"); };
                bridge.prInputChannel.onerror = function (e) { L("pr channel error", e && e.message); };
                bridge.prInputChannel.onclose = function () { L("pr channel closed"); };
                L("created input channels on stream pc (receivers=" + recv + ")");
            } catch (e) {
                L("openChannels error", e && e.message);
            }
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
                    if (pc.connectionState === "connected") {
                        // pick this as stream pc when it has media receivers
                        var recv = pc.getReceivers ? pc.getReceivers().length : 0;
                        L("pc connected receivers=" + recv + " channels=" + !!bridge.inputChannel);
                    }
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

        L("injected; hook active");
    } catch (e) {
        console.log("[imwb-input] inject error", e && e.message);
    }
})();
