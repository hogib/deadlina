// SPDX-License-Identifier: GPL-3.0-or-later
//
// The searchable command list (docs/design/12-interaction-principles.md,
// principle 4): type to narrow it, Up and Down to choose, Enter to run,
// Escape to leave. Each entry shows its shortcut, so the list is also how
// shortcuts are learned. Closing gives the keys back to the pane that had
// them.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lina.Ui

Popup {
    id: popup

    required property Shell shell
    readonly property Appearance look: shell.appearance

    function openList() {
        search.text = "";
        popup.shell.palette.query = "";
        list.currentIndex = 0;
        open();
    }

    function runCurrent() {
        const row = list.currentIndex;
        close();
        popup.shell.palette.triggerRow(row);
    }

    parent: Overlay.overlay
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: 6 * look.unit
    width: parent ? Math.min(parent.width - 4 * look.unit, 64 * look.unit) : 0
    height: parent ? Math.min(parent.height - 8 * look.unit, 52 * look.unit) : 0
    modal: true
    focus: true
    padding: look.unit
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    onOpened: search.forceActiveFocus()
    onClosed: popup.shell.workspace.returnFocus()

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: popup.look.animationMs
        }
    }

    background: Rectangle {
        color: popup.look.surface
        border.color: popup.look.divider
        radius: popup.look.unit / 2
    }

    contentItem: ColumnLayout {
        spacing: popup.look.unit

        TextField {
            id: search
            objectName: "commandSearch"
            Layout.fillWidth: true
            placeholderText: qsTr("Type to find a command")
            Accessible.name: qsTr("Find a command")
            onTextChanged: {
                popup.shell.palette.query = text;
                list.currentIndex = 0;
            }
            Keys.onDownPressed: list.incrementCurrentIndex()
            Keys.onUpPressed: list.decrementCurrentIndex()
            Keys.onReturnPressed: popup.runCurrent()
            Keys.onEnterPressed: popup.runCurrent()
        }

        ListView {
            id: list
            objectName: "commandResults"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: popup.shell.palette
            keyNavigationWraps: true
            highlightMoveDuration: popup.look.animationMs
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Commands")

            delegate: ItemDelegate {
                id: entry

                required property int index
                required property string name
                required property string description
                required property string shortcut
                required property bool available

                width: ListView.view.width
                highlighted: ListView.isCurrentItem
                focusPolicy: Qt.NoFocus
                Accessible.name: shortcut === "" ? name : qsTr("%1, %2").arg(name).arg(shortcut)
                Accessible.description: available ? description : qsTr("%1 Not available now.").arg(description)
                onClicked: {
                    list.currentIndex = index;
                    popup.runCurrent();
                }

                contentItem: RowLayout {
                    spacing: popup.look.unit

                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: entry.available ? entry.name : qsTr("%1 (not available now)").arg(entry.name)
                        color: entry.highlighted ? popup.look.onAccent : (entry.available ? popup.look.text : popup.look.mutedText)
                    }

                    Label {
                        text: entry.shortcut
                        color: entry.highlighted ? popup.look.onAccent : popup.look.mutedText
                    }
                }

                background: Rectangle {
                    color: entry.highlighted ? popup.look.accent : "transparent"
                    radius: popup.look.unit / 2
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: list.count === 0
            text: qsTr("No command matches “%1”.").arg(search.text)
            color: popup.look.mutedText
        }
    }
}
