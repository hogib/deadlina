// SPDX-License-Identifier: GPL-3.0-or-later
//
// The searchable command list: opened with one shortcut, narrowed by typing,
// run with Enter or a click, left with Escape; the keys go back to the pane.
import QtQuick

ShellTestCase {
    name: "CommandList"

    function test_type_and_run() {
        const view = makeView();
        const shell = view.shell;
        const popup = findChild(view, "commandList");
        verify(!popup.opened);

        keyClick(Qt.Key_K, Qt.ControlModifier);
        tryVerify(() => popup.opened);
        const search = findChild(view, "commandSearch");
        tryVerify(() => search.activeFocus);
        compare(shell.palette.count, shell.commands.rowCount());

        // A space is typed, not a transport command.
        typeText("add midi");
        compare(search.text, "add midi");
        compare(shell.palette.query, "add midi");
        compare(shell.palette.count, 1);
        verify(!shell.transport.playing);

        keyClick(Qt.Key_Return);
        tryCompare(shell.tracks, "count", 1);
        verify(!popup.opened);
        compare(shell.commands.command("edit.undo").name, "Undo Add MIDI Track");
        tryVerify(() => findChild(view, "session").activeFocus);
    }

    function test_choose_with_arrows() {
        const view = makeView();
        const shell = view.shell;
        const popup = findChild(view, "commandList");
        const results = findChild(view, "commandResults");
        keyClick(Qt.Key_K, Qt.ControlModifier);
        tryVerify(() => popup.opened);
        typeText("appearance");
        // Follow the Desktop, Light, Dark; the first is what is set now.
        compare(shell.palette.count, 3);
        compare(results.currentIndex, 0);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Down);
        compare(results.currentIndex, 2);
        keyClick(Qt.Key_Up);
        compare(results.currentIndex, 1);
        keyClick(Qt.Key_Enter);
        tryVerify(() => !popup.opened);
        compare(shell.appearance.mode, 1);
        verify(!shell.appearance.dark);
    }

    function test_entries_show_shortcuts() {
        const view = makeView();
        const shell = view.shell;
        const results = findChild(view, "commandResults");
        keyClick(Qt.Key_K, Qt.ControlModifier);
        tryVerify(() => findChild(view, "commandList").opened);
        typeText("add audio");
        tryVerify(() => results.itemAtIndex(0) !== null);
        const entry = results.itemAtIndex(0);
        compare(entry.name, "Add Audio Track");
        compare(entry.shortcut, shell.commands.command("track.addAudio").shortcut);
        verify(entry.shortcut !== "");
        verify(entry.Accessible.name.indexOf(entry.shortcut) > 0);

        // A click runs it too.
        mouseClick(entry);
        tryCompare(shell.tracks, "count", 1);
    }

    function test_escape_leaves() {
        const view = makeView();
        const shell = view.shell;
        const popup = findChild(view, "commandList");
        keyClick(Qt.Key_F6);
        tryVerify(() => findChild(view, "arrangement").activeFocus);
        keyClick(Qt.Key_K, Qt.ControlModifier);
        tryVerify(() => popup.opened);
        typeText("add");
        keyClick(Qt.Key_Escape);
        tryVerify(() => !popup.opened);
        compare(shell.tracks.count, 0);
        tryVerify(() => findChild(view, "arrangement").activeFocus);

        // Opened again, it starts empty.
        keyClick(Qt.Key_K, Qt.ControlModifier);
        tryVerify(() => popup.opened);
        compare(findChild(view, "commandSearch").text, "");
        compare(shell.palette.query, "");
        typeText("zzzz");
        compare(shell.palette.count, 0);
        keyClick(Qt.Key_Return);
        tryVerify(() => !popup.opened);
    }

    function test_button_opens_it() {
        const view = makeView();
        mouseClick(findChild(view, "commandsButton"));
        tryVerify(() => findChild(view, "commandList").opened);
    }
}
