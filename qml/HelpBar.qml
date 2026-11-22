// SPDX-License-Identifier: GPL-3.0-or-later
//
// The fixed help area (docs/design/12-interaction-principles.md,
// principle 9): the name of what is pointed at or focused, and one sentence
// about it.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lina.Ui

Rectangle {
    id: root

    required property Shell shell
    readonly property Appearance look: shell.appearance
    readonly property bool idle: shell.workspace.helpTitle === ""

    implicitHeight: row.implicitHeight + 2 * look.unit
    color: look.surface
    Accessible.role: Accessible.StatusBar
    Accessible.name: qsTr("Help")
    Accessible.description: idle ? hint.text : root.shell.workspace.helpTitle + ": " + root.shell.workspace.helpText

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: root.look.divider
    }

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: root.look.unit
        spacing: root.look.unit

        Label {
            objectName: "helpTitle"
            visible: !root.idle
            text: root.shell.workspace.helpTitle
            font.bold: true
            color: root.look.text
        }

        Label {
            id: hint
            objectName: "helpText"
            Layout.fillWidth: true
            elide: Text.ElideRight
            color: root.look.mutedText
            text: root.idle ? qsTr("Point at anything to see what it does. %1 lists every command.").arg(root.shell.commands.command("app.commands").shortcut) : root.shell.workspace.helpText
        }
    }
}
