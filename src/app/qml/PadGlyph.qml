import QtQuick
import QtCraft

// One pad button as a symbol (decision E-612): a disc with a letter or word (Xbox and Nintendo
// letters, Back and Start), the four PlayStation shapes drawn as lines, a tab for shoulders and
// triggers, a stick, the d-pad cross with one arm lit, or for pads of unknown make a diamond of four
// dots with one lit. Plain shapes and letters, no logos. The glyph comes from
// GameController.gamepad.pads.glyph(family, input) ({shape, label, colour, direction}). Painted
// once per change; exists only while the pad leads (in the pad's loaders).
Item {
    id: glyph

    required property var symbol
    readonly property string shape: symbol.shape ?? ""
    readonly property string label: symbol.label ?? ""
    readonly property string direction: symbol.direction ?? ""
    readonly property color ink: ({ green: Theme.padGreen, red: Theme.padRed, blue: Theme.padBlue, yellow: Theme.padYellow,
                                    pink: Theme.padPink })[symbol.colour ?? ""] ?? Theme.text
    // Shoulders, triggers and words are wider than a disc.
    readonly property bool wide: shape === "shoulder" || shape === "trigger" || (shape === "label" && label.length > 1)
    property real size: 22

    implicitWidth: wide ? Math.max(size * 1.5, text.implicitWidth + 10) : size
    implicitHeight: size

    Canvas {
        id: canvas

        anchors.fill: parent
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            const w = width, h = height, r = Math.min(w, h) / 2;
            ctx.fillStyle = Theme.padGlyph;
            ctx.strokeStyle = glyph.ink;
            ctx.lineWidth = Math.max(1.5, h / 11);
            // The backing: a tab for shoulders (round corners) and triggers (round top), else a disc
            // or a pill.
            ctx.beginPath();
            if (glyph.shape === "trigger") {
                ctx.moveTo(1, h - 1);
                ctx.lineTo(1, h * 0.45);
                ctx.quadraticCurveTo(1, 1, w * 0.45, 1);
                ctx.lineTo(w * 0.55, 1);
                ctx.quadraticCurveTo(w - 1, 1, w - 1, h * 0.45);
                ctx.lineTo(w - 1, h - 1);
                ctx.closePath();
            } else if (glyph.wide) {
                ctx.roundedRect(1, 1, w - 2, h - 2, glyph.shape === "shoulder" ? h / 4 : h / 2 - 1, glyph.shape === "shoulder" ? h / 4 : h / 2 - 1);
            } else {
                ctx.arc(w / 2, h / 2, r - 1, 0, 2 * Math.PI);
            }
            ctx.fill();

            const cx = w / 2, cy = h / 2, s = r * 0.45;
            ctx.beginPath();
            if (glyph.shape === "cross") {
                ctx.moveTo(cx - s, cy - s);
                ctx.lineTo(cx + s, cy + s);
                ctx.moveTo(cx + s, cy - s);
                ctx.lineTo(cx - s, cy + s);
                ctx.stroke();
            } else if (glyph.shape === "circle") {
                ctx.arc(cx, cy, s, 0, 2 * Math.PI);
                ctx.stroke();
            } else if (glyph.shape === "square") {
                ctx.rect(cx - s * 0.85, cy - s * 0.85, s * 1.7, s * 1.7);
                ctx.stroke();
            } else if (glyph.shape === "triangle") {
                ctx.moveTo(cx, cy - s);
                ctx.lineTo(cx + s * 1.05, cy + s * 0.75);
                ctx.lineTo(cx - s * 1.05, cy + s * 0.75);
                ctx.closePath();
                ctx.stroke();
            } else if (glyph.shape === "stick") {
                ctx.arc(cx, cy, r * 0.72, 0, 2 * Math.PI);
                ctx.stroke();
            } else if (glyph.shape === "dpad" || glyph.shape === "position") {
                glyph.paintDirections(ctx, cx, cy, r);
            }
        }
    }

    // The d-pad's arms or the four face button dots, the one of `direction` lit: larger, in full
    // contrast, and the others faint, so the direction reads at hint size.
    function paintDirections(ctx, cx, cy, r) {
        const arms = { up: [0, -1], down: [0, 1], left: [-1, 0], right: [1, 0] };
        for (const name in arms) {
            const lit = name === direction;
            const x = cx + arms[name][0] * r * 0.5, y = cy + arms[name][1] * r * 0.5;
            ctx.beginPath();
            if (shape === "dpad") {
                const a = r * (lit ? 0.34 : 0.22);
                ctx.rect(x - a, y - a, 2 * a, 2 * a);
            } else {
                ctx.arc(x, y, r * (lit ? 0.26 : 0.17), 0, 2 * Math.PI);
            }
            ctx.fillStyle = lit ? Theme.text : Theme.textMuted;
            ctx.globalAlpha = lit ? 1.0 : 0.3;
            ctx.fill();
        }
        ctx.globalAlpha = 1.0;
    }

    onSymbolChanged: canvas.requestPaint()

    Text {
        id: text

        anchors.centerIn: parent
        anchors.verticalCenterOffset: glyph.shape === "trigger" ? 1 : 0
        visible: glyph.label.length > 0
        text: glyph.label
        color: glyph.ink
        // Inside the stick's ring and in words the letters are smaller.
        font.pixelSize: glyph.label.length > 2 || glyph.shape === "stick" ? glyph.size * 0.4 : glyph.size * 0.55
        font.bold: true
    }
}
