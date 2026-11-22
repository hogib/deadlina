// SPDX-License-Identifier: GPL-3.0-or-later
//
// A drag whose grab is taken away (by a gesture above it, or by the system)
// is cancelled, never kept. Qt reports such a drag as no longer active
// before it reports it cancelled, so the drags of the window end by how
// their grab ends; these tests hold Qt and the window to that.
import QtQuick
import QtTest
import Lina.Shell
import LinaTest

TestCase {
    id: testCase

    name: "DragCancel"
    width: 400
    height: 300
    visible: true
    when: windowShown

    // Above the dragged item, a drag that takes the grab over once it has
    // gone further than the one below.
    component Thief: Item {
        width: 300
        height: 200
        DragHandler {
            target: null
            dragThreshold: 40
            grabPermissions: PointerHandler.CanTakeOverFromAnything
        }
    }

    Component {
        id: orderComponent

        Thief {
            id: thief

            property var events: []

            Rectangle {
                id: box
                width: 100
                height: 100
                DragHandler {
                    target: null
                    dragThreshold: 5
                    onActiveChanged: thief.events.push(active ? "active" : "inactive")
                    onCanceled: thief.events.push("canceled")
                    onGrabChanged: transition => {
                        if (transition === PointerDevice.CancelGrabExclusive)
                            thief.events.push("grab cancelled");
                    }
                }
            }
        }
    }

    Component {
        id: tempoComponent

        Thief {
            property alias shell: field.shell

            TempoField {
                id: field
            }
        }
    }

    Component {
        id: dividerComponent

        Thief {
            property alias shell: divider.shell

            SplitDivider {
                id: divider
                width: 300
                available: 400
            }
        }
    }

    // Drags down from (x, y); `dragging` must hold once the item below has
    // taken the drag and before the one above takes it over.
    function stealDrag(item: Item, x: real, y: real, dragging) {
        mousePress(item, x, y);
        for (let step = 1; step <= 4; ++step)
            mouseMove(item, x, y + 8 * step);
        verify(dragging(), "the drag below had started");
        for (let step = 5; step <= 8; ++step)
            mouseMove(item, x, y + 8 * step);
        mouseRelease(item, x, y + 64);
    }

    function test_qt_reports_inactive_before_cancelled() {
        const thief = createTemporaryObject(orderComponent, testCase);
        stealDrag(thief, 50, 20, () => thief.events.indexOf("active") === 0);
        compare(thief.events, ["active", "inactive", "canceled", "grab cancelled"]);
    }

    function test_a_stolen_tempo_drag_is_not_kept() {
        const shell = Harness.makeShell();
        const thief = createTemporaryObject(tempoComponent, testCase, {
            shell: shell
        });
        const unit = findChild(thief, "tempoUnit");
        const origin = unit.mapToItem(thief, unit.width / 2, unit.height / 2);
        stealDrag(thief, origin.x, origin.y, () => shell.transport.tempoText !== "120.00");
        compare(shell.transport.tempoText, "120.00");
        verify(!shell.transport.tempoDragging);
        verify(!shell.document.modified);
        verify(!shell.commands.command("edit.undo").enabled);
    }

    function test_a_stolen_divider_drag_is_not_kept() {
        const shell = Harness.makeShell();
        const thief = createTemporaryObject(dividerComponent, testCase, {
            shell: shell
        });
        stealDrag(thief, 150, 4, () => shell.workspace.split !== 0.5);
        compare(shell.workspace.split, 0.5);
        verify(!shell.workspace.dividerDragging);
    }
}
