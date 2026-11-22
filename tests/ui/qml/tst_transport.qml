// SPDX-License-Identifier: GPL-3.0-or-later
//
// The transport bar: play, stop and the second stop, the position read
// from the engine every frame, and the tempo as a control.
import QtQuick
import LinaTest

ShellTestCase {
    name: "Transport"

    function test_play_stop_and_return() {
        const view = makeView();
        const shell = view.shell;
        const play = findChild(view, "playButton");
        const stop = findChild(view, "stopButton");
        const position = findChild(view, "position");
        compare(position.text, "1.1.1");
        compare(stop.text, "■ Stop");

        mouseClick(play);
        verify(shell.transport.playing);
        compare(play.text, "▶ Playing");
        verify(play.highlighted);
        // Clicking a button leaves the keys with the pane.
        verify(findChild(view, "session").activeFocus);

        // The position follows the engine, frame by frame.
        Harness.setEnginePosition(shell, 6);
        tryCompare(position, "text", "2.3.1");
        Harness.setEnginePosition(shell, 9);
        tryCompare(position, "text", "3.2.1");

        // The first stop leaves the playhead where it is.
        mouseClick(stop);
        verify(!shell.transport.playing);
        compare(position.text, "3.2.1");
        compare(stop.text, "⏮ To Start");
        compare(play.text, "▶ Play");

        // The second returns it to the start marker.
        mouseClick(stop);
        compare(position.text, "1.1.1");
        compare(stop.text, "■ Stop");

        // Play again, and stop with Space at the fourth beat.
        mouseClick(play);
        compare(Harness.lastPlayedFrom(shell), 0);
        Harness.setEnginePosition(shell, 4);
        tryCompare(position, "text", "2.1.1");
        keyClick(Qt.Key_Space);
        verify(!shell.transport.playing);
        // Continue (Shift+Space) asks the engine to start where the playhead
        // stopped.
        keyClick(Qt.Key_Space, Qt.ShiftModifier);
        verify(shell.transport.playing);
        compare(Harness.lastPlayedFrom(shell), 4);
    }

    function test_without_output() {
        const view = makeView();
        const shell = view.shell;
        Harness.setOutput(shell, false);
        const play = findChild(view, "playButton");
        // Play stays available, so that it can say why nothing plays.
        verify(play.enabled);
        keyClick(Qt.Key_Space);
        verify(!shell.transport.playing);
        verify(shell.document.notice.indexOf("--no-audio") >= 0);
        verify(findChild(view, "noticeText").visible);
        shell.document.dismissNotice();
        mouseClick(play);
        verify(!shell.transport.playing);
        verify(shell.document.notice.indexOf("--no-audio") >= 0);
    }

    function test_escape_cancels_a_tempo_drag() {
        const view = makeView();
        const shell = view.shell;
        const unit = findChild(view, "tempoUnit");
        mousePress(unit, unit.width / 2, unit.height / 2);
        for (let step = 1; step <= 6; ++step)
            mouseMove(unit, unit.width / 2, unit.height / 2 - 10 * step);
        tryVerify(() => shell.transport.tempoText !== "120.00");
        verify(shell.transport.tempoDragging);
        keyClick(Qt.Key_Escape);
        compare(shell.transport.tempoText, "120.00");
        verify(!shell.transport.tempoDragging);
        // Going on and letting go change nothing now.
        mouseMove(unit, unit.width / 2, unit.height / 2 - 100);
        mouseRelease(unit, unit.width / 2, unit.height / 2 - 100);
        compare(shell.transport.tempoText, "120.00");
        verify(!shell.commands.command("edit.undo").enabled);
        verify(!shell.document.modified);
    }

    function test_tempo_drag_and_reset() {
        const view = makeView();
        const shell = view.shell;
        const unit = findChild(view, "tempoUnit");
        mouseDrag(unit, unit.width / 2, unit.height / 2, 0, -60);
        tryVerify(() => shell.transport.tempoText !== "120.00");
        const dragged = Number(shell.transport.tempoText);
        verify(dragged > 120);
        compare(findChild(view, "tempoField").text, shell.transport.tempoText);
        // One drag, one step.
        compare(shell.commands.command("edit.undo").name, "Undo Change Tempo");
        shell.run("edit.undo");
        compare(shell.transport.tempoText, "120.00");
        verify(!shell.commands.command("edit.undo").enabled);

        shell.transport.stepTempo(10, false);
        mouseDoubleClickSequence(unit, unit.width / 2, unit.height / 2);
        tryCompare(shell.transport, "tempoText", "120.00");
    }
}
