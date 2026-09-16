function publishCursorPosition() {
    var pos = workspace.cursorPos;
    var geometry = workspace.virtualScreenGeometry;

    // KWin reports logical screen coordinates. Do not multiply by scale.
    callDBus(
        "org.example.EdgeVolume",
        "/EdgeVolume",
        "org.example.EdgeVolume",
        "setCursor",
        Math.round(pos.x),
        Math.round(pos.y),
        Math.round(geometry.x)
    );
}

publishCursorPosition();
workspace.cursorPosChanged.connect(publishCursorPosition);
workspace.virtualScreenGeometryChanged.connect(publishCursorPosition);
