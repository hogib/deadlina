// SPDX-License-Identifier: GPL-3.0-or-later
//
// The divider between the session and the arrangement. Drag it to share the
// height differently; double-click it for an even split. The same moves
// are commands, for the keyboard (principle 7).
import QtQuick
import Lina.Ui

Rectangle {
    id: root

    required property Shell shell
    /// The height the two views share.
    required property real available
    readonly property Appearance look: shell.appearance

    implicitHeight: root.look.unit
    color: drag.active || hover.hovered ? root.look.divider : root.look.window
    Accessible.role: Accessible.Separator
    Accessible.name: qsTr("Divider between session and arrangement")
    Accessible.description: qsTr("Drag up or down to give one view more room; double-click for an even split.")

    Rectangle {
        anchors.centerIn: parent
        width: 6 * root.look.unit
        height: Math.max(2, root.look.unit / 4)
        radius: height / 2
        color: root.look.mutedText
    }

    // Measured in the scene: the divider itself moves with the drag. It ends
    // by how its grab ends, as the tempo drag does (TempoField.qml).
    DragHandler {
        id: drag
        target: null
        xAxis.enabled: false
        cursorShape: Qt.SplitVCursor
        onActiveChanged: {
            if (active)
                root.shell.workspace.beginDividerDrag();
        }
        onCentroidChanged: {
            if (active)
                root.shell.workspace.dragDivider(centroid.scenePosition.y - centroid.scenePressPosition.y, root.available);
        }
        onGrabChanged: transition => {
            if (transition === PointerDevice.UngrabExclusive)
                root.shell.workspace.endDividerDrag();
            else if (transition === PointerDevice.CancelGrabExclusive)
                root.shell.workspace.cancelDividerDrag();
        }
        onCanceled: root.shell.workspace.cancelDividerDrag()
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.SplitVCursor
        onDoubleClicked: root.shell.workspace.resetDivider()
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.SplitVCursor
        onHoveredChanged: {
            if (hovered)
                root.shell.workspace.showHelp(root.Accessible.name, root.Accessible.description);
            else
                root.shell.workspace.clearHelp(root.Accessible.name);
        }
    }
}
