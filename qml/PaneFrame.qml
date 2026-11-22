// SPDX-License-Identifier: GPL-3.0-or-later
//
// A view that can have the keys (docs/design/12-interaction-principles.md,
// principle 5). The pane that has them is marked by a thicker frame in the
// focus colour, so the mark does not rest on colour alone.
import QtQuick
import QtQuick.Controls.Basic
import Lina.Ui

FocusScope {
    id: root

    required property Shell shell
    required property int pane
    required property string title
    required property string explanation

    readonly property Appearance look: shell.appearance
    readonly property bool marked: shell.workspace.focusedPane === pane
    default property alias content: inner.data

    activeFocusOnTab: true
    Accessible.role: Accessible.Pane
    Accessible.name: title
    Accessible.description: explanation

    onActiveFocusChanged: {
        if (activeFocus)
            shell.workspace.focusedPane = pane;
    }

    Rectangle {
        anchors.fill: parent
        color: root.look.surface
        border.color: root.marked ? root.look.focus : root.look.divider
        border.width: root.marked ? Math.max(2, Math.round(root.look.unit / 3)) : 1
    }

    Label {
        id: heading
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: root.look.unit
        text: root.title
        font.bold: true
        color: root.look.text
    }

    Item {
        id: inner
        anchors.fill: parent
        anchors.topMargin: heading.height + 2 * root.look.unit
        anchors.leftMargin: root.look.unit
        anchors.rightMargin: root.look.unit
        anchors.bottomMargin: root.look.unit
    }

    TapHandler {
        onTapped: root.forceActiveFocus()
    }

    HoverHandler {
        onHoveredChanged: {
            if (hovered)
                root.shell.workspace.showHelp(root.title, root.explanation);
            else
                root.shell.workspace.clearHelp(root.title);
        }
    }
}
