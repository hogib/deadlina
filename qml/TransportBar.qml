// SPDX-License-Identifier: GPL-3.0-or-later
//
// Play, stop, the playhead position, the tempo, undo and redo, and the
// way into the command list. What the next stop does is shown on its
// button: stop while playing, return to the start marker while stopped
// away from it (docs/design/04-transport-and-musical-time.md).
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lina.Ui

Rectangle {
    id: root

    required property Shell shell
    readonly property Appearance look: shell.appearance
    readonly property TransportController transport: shell.transport

    function editTempo() {
        tempo.startEditing();
    }

    implicitHeight: row.implicitHeight + 2 * look.unit
    color: look.surface

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: root.look.divider
    }

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: root.look.unit
        spacing: root.look.unit

        ShellButton {
            objectName: "playButton"
            shell: root.shell
            command: "transport.play"
            label: root.transport.playing ? qsTr("▶ Playing") : qsTr("▶ Play")
            highlighted: root.transport.playing
        }

        ShellButton {
            objectName: "stopButton"
            shell: root.shell
            command: "transport.stop"
            label: root.transport.stopReturns ? qsTr("⏮ To Start") : qsTr("■ Stop")
            explanation: root.transport.playing ? qsTr("Stops playing. The playhead stays where it stopped; stop again to return it to the start marker.") : (root.transport.stopReturns ? qsTr("Returns the playhead to the start marker.") : qsTr("Stops playing. The transport is stopped now."))
        }

        Label {
            id: position
            objectName: "position"
            Layout.preferredWidth: 10 * root.look.unit
            horizontalAlignment: Text.AlignHCenter
            text: root.transport.positionText
            font.family: "monospace"
            font.pointSize: root.look.fontPointSize * 1.25
            color: root.look.text
            Accessible.role: Accessible.StaticText
            Accessible.name: qsTr("Position %1 (bar, beat, quarter of a beat)").arg(text)

            HoverHandler {
                onHoveredChanged: {
                    if (hovered)
                        root.shell.workspace.showHelp(qsTr("Position"), qsTr("Where the playhead is: bar, beat, and quarter of a beat."));
                    else
                        root.shell.workspace.clearHelp(qsTr("Position"));
                }
            }
        }

        TempoField {
            id: tempo
            shell: root.shell
        }

        Item {
            Layout.fillWidth: true
        }

        Label {
            objectName: "projectTitle"
            text: root.shell.document.modified ? qsTr("%1 (unsaved changes)").arg(root.shell.document.title) : root.shell.document.title
            color: root.look.mutedText
            elide: Text.ElideMiddle
            Layout.maximumWidth: 30 * root.look.unit
        }

        ShellButton {
            objectName: "undoButton"
            shell: root.shell
            command: "edit.undo"
        }

        ShellButton {
            objectName: "redoButton"
            shell: root.shell
            command: "edit.redo"
        }

        ShellButton {
            objectName: "commandsButton"
            shell: root.shell
            command: "app.commands"
            label: qsTr("Commands")
        }
    }
}
