// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shortcuts run through the registry from wherever the keys are, except
// while typing; finishing or cancelling a text entry gives the keys back.
import QtQuick
import LinaTest

ShellTestCase {
    name: "Keys"

    function test_transport_key_in_every_pane() {
        const view = makeView();
        const shell = view.shell;
        keyClick(Qt.Key_Space);
        verify(shell.transport.playing);
        verify(Harness.enginePlaying(shell));
        keyClick(Qt.Key_F6);
        tryVerify(() => findChild(view, "arrangement").activeFocus);
        keyClick(Qt.Key_Space);
        verify(!shell.transport.playing);
        verify(!Harness.enginePlaying(shell));
    }

    function test_undo_and_redo_keys() {
        const view = makeView();
        const shell = view.shell;
        keyClick(Qt.Key_T, Qt.ControlModifier);
        keyClick(Qt.Key_T, Qt.ControlModifier | Qt.ShiftModifier);
        compare(shell.tracks.count, 2);
        compare(findChild(view, "undoButton").text, "Undo Add MIDI Track");
        keyClick(Qt.Key_Z, Qt.ControlModifier);
        compare(shell.tracks.count, 1);
        compare(findChild(view, "redoButton").text, "Redo Add MIDI Track");
        keyClick(Qt.Key_Z, Qt.ControlModifier | Qt.ShiftModifier);
        compare(shell.tracks.count, 2);
        keyClick(Qt.Key_Z, Qt.ControlModifier);
        keyClick(Qt.Key_Y, Qt.ControlModifier);
        compare(shell.tracks.count, 2);
        // The buttons run the same commands.
        mouseClick(findChild(view, "undoButton"));
        compare(shell.tracks.count, 1);
    }

    function test_typing_keeps_its_keys() {
        const view = makeView();
        const shell = view.shell;
        const field = findChild(view, "tempoField");
        mouseClick(field);
        tryVerify(() => field.activeFocus);
        keyClick(Qt.Key_End);
        keyClick(Qt.Key_Space);
        verify(!shell.transport.playing);
        verify(field.text.endsWith(" "));
        // A command that typing does not use still runs.
        keyClick(Qt.Key_T, Qt.ControlModifier);
        compare(shell.tracks.count, 1);
        // Escape: the entry is cancelled and the pane has the keys again.
        keyClick(Qt.Key_Escape);
        compare(field.text, "120.00");
        tryVerify(() => findChild(view, "session").activeFocus);
    }

    function test_tempo_typed() {
        const view = makeView();
        const shell = view.shell;
        const field = findChild(view, "tempoField");
        keyClick(Qt.Key_F6);
        shell.run("transport.tempo");
        tryVerify(() => field.activeFocus);
        compare(field.selectedText, "120.00");
        typeText("96,5");
        keyClick(Qt.Key_Return);
        compare(shell.transport.tempoText, "96.50");
        compare(field.text, "96.50");
        compare(shell.commands.command("edit.undo").name, "Undo Change Tempo");
        tryVerify(() => findChild(view, "arrangement").activeFocus);

        // Up and Down step it, each an undo step.
        mouseClick(field);
        keyClick(Qt.Key_Up);
        compare(shell.transport.tempoText, "97.50");
        keyClick(Qt.Key_Down, Qt.ShiftModifier);
        compare(shell.transport.tempoText, "97.49");
        keyClick(Qt.Key_Escape);

        // Something that is not a tempo is refused, and the notice says so.
        mouseClick(field);
        field.selectAll();
        typeText("fast");
        keyClick(Qt.Key_Return);
        compare(shell.transport.tempoText, "97.49");
        compare(field.text, "97.49");
        verify(shell.document.noticeIsError);
        verify(findChild(view, "noticeText").text.indexOf("fast") >= 0);
    }
}
