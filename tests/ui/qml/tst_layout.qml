// SPDX-License-Identifier: GPL-3.0-or-later
//
// The session above the arrangement with a divider that can be dragged;
// one pane has the keys and is marked; Tab and F6 move through the window.
import QtQuick
import Lina.Ui

ShellTestCase {
    name: "Layout"

    function test_divider_drag() {
        const view = makeView();
        const shell = view.shell;
        const session = findChild(view, "session");
        const divider = findChild(view, "divider");
        const views = findChild(view, "views");
        const shared = views.height - divider.height;
        compare(shell.workspace.split, 0.5);
        compare(session.height, Math.round(shared * 0.5));

        // Dragged in the coordinates of what does not move: the divider
        // itself follows the pointer.
        mouseDrag(views, divider.width / 2, divider.y + divider.height / 2, 0, 120);
        tryCompare(shell.workspace, "split", 0.5 + 120 / shared);
        tryCompare(session, "height", Math.round(shared * shell.workspace.split));
        compare(divider.y, session.height);

        // It stops where both views keep room.
        mouseDrag(views, divider.width / 2, divider.y + divider.height / 2, 0, -2000);
        tryCompare(shell.workspace, "split", 0.15);

        // A double-click shares the height evenly again.
        mouseDoubleClickSequence(divider, divider.width / 2, divider.height / 2);
        tryCompare(shell.workspace, "split", 0.5);
        // Dragging is no different from the commands.
        shell.run("view.dividerDown");
        compare(shell.workspace.split, 0.55);
    }

    function test_escape_cancels_a_divider_drag() {
        const view = makeView();
        const shell = view.shell;
        const divider = findChild(view, "divider");
        const views = findChild(view, "views");
        const x = divider.width / 2;
        const y = divider.y + divider.height / 2;
        mousePress(views, x, y);
        for (let step = 1; step <= 8; ++step)
            mouseMove(views, x, y + 10 * step);
        tryVerify(() => shell.workspace.split > 0.55);
        verify(shell.workspace.dividerDragging);
        keyClick(Qt.Key_Escape);
        compare(shell.workspace.split, 0.5);
        verify(!shell.workspace.dividerDragging);
        // Going on and letting go change nothing now.
        mouseMove(views, x, y + 150);
        mouseRelease(views, x, y + 150);
        compare(shell.workspace.split, 0.5);
    }

    function test_one_view_alone() {
        const view = makeView();
        const shell = view.shell;
        const session = findChild(view, "session");
        const arrangement = findChild(view, "arrangement");
        const views = findChild(view, "views");
        shell.run("view.sessionOnly");
        verify(!arrangement.visible);
        verify(!findChild(view, "divider").visible);
        compare(session.height, views.height);
        keyClick(Qt.Key_F6);
        tryVerify(() => session.activeFocus);

        shell.run("view.arrangementOnly");
        verify(!session.visible);
        compare(arrangement.y, 0);
        compare(arrangement.height, views.height);
        tryVerify(() => arrangement.activeFocus);
    }

    function test_pane_focus_is_marked() {
        const view = makeView();
        const shell = view.shell;
        const session = findChild(view, "session");
        const arrangement = findChild(view, "arrangement");
        tryVerify(() => session.activeFocus);
        verify(session.marked);
        verify(!arrangement.marked);

        keyClick(Qt.Key_F6);
        tryVerify(() => arrangement.activeFocus);
        verify(arrangement.marked);
        verify(!session.marked);
        keyClick(Qt.Key_F6, Qt.ShiftModifier);
        tryVerify(() => session.activeFocus);

        // A click gives a pane the keys.
        mouseClick(arrangement, arrangement.width / 2, arrangement.height - 10);
        tryVerify(() => arrangement.activeFocus);
        compare(shell.workspace.focusedPane, Workspace.Arrangement);

        keyClick(Qt.Key_1, Qt.ControlModifier);
        tryVerify(() => session.activeFocus);
        keyClick(Qt.Key_2, Qt.ControlModifier);
        tryVerify(() => arrangement.activeFocus);
    }

    function test_tab_reaches_every_control() {
        const view = makeView();
        // Undo and redo can do something, so that they take the keys.
        view.shell.run("track.addAudio");
        view.shell.run("track.addAudio");
        view.shell.run("edit.undo");
        const visited = [];
        tryVerify(() => findChild(view, "session").activeFocus);
        for (let step = 0; step < 20; ++step) {
            keyClick(Qt.Key_Tab);
            const item = view.Window.activeFocusItem;
            verify(item !== null);
            visited.push(item.objectName !== "" ? item.objectName : String(item));
            if (item.objectName === "session")
                break;
        }
        for (const name of ["playButton", "stopButton", "tempoField", "undoButton", "redoButton", "commandsButton", "arrangement", "session"])
            verify(visited.indexOf(name) >= 0, name + " in " + visited.join(", "));
        // While a control has the keys, the help area says what it is.
        for (let step = 0; step < 20 && view.Window.activeFocusItem.objectName !== "playButton"; ++step)
            keyClick(Qt.Key_Tab);
        compare(view.shell.workspace.helpTitle, "▶ Play");
    }

    function test_help_area_follows_the_pointer() {
        const view = makeView();
        const shell = view.shell;
        const play = findChild(view, "playButton");
        mouseMove(play, play.width / 2, play.height / 2);
        tryCompare(shell.workspace, "helpTitle", "▶ Play");
        compare(shell.workspace.helpText, "Starts playing from the start marker.");
        compare(findChild(view, "helpText").text, shell.workspace.helpText);
        const divider = findChild(view, "divider");
        mouseMove(divider, divider.width / 2, divider.height / 2);
        tryCompare(shell.workspace, "helpTitle", "Divider between session and arrangement");
    }

    function test_empty_areas_say_how_to_fill_them() {
        const view = makeView();
        const shell = view.shell;
        const headers = findChild(view, "trackHeaders");
        verify(!headers.visible);
        keyClick(Qt.Key_T, Qt.ControlModifier);
        keyClick(Qt.Key_T, Qt.ControlModifier | Qt.ShiftModifier);
        tryVerify(() => headers.visible);
        compare(headers.count, 2);
        tryVerify(() => headers.itemAtIndex(1) !== null);
        compare(headers.itemAtIndex(1).Accessible.name, "MIDI 1, MIDI track");
    }
}
