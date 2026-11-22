// SPDX-License-Identifier: GPL-3.0-or-later
//
// Everything inside the main window: the transport bar, the session above
// the arrangement with a divider between them, the help area, and the
// command list, questions and folder dialog over them. Layout and bindings
// only; every decision is made in C++ (Lina.Ui).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Lina.Ui

Control {
    id: root

    required property Shell shell
    readonly property Appearance look: shell.appearance

    font.pointSize: look.fontPointSize
    padding: 0

    palette {
        window: root.look.window
        windowText: root.look.text
        base: root.look.raised
        alternateBase: root.look.surface
        text: root.look.text
        button: root.look.raised
        buttonText: root.look.text
        highlight: root.look.accent
        highlightedText: root.look.onAccent
        placeholderText: root.look.mutedText
        light: root.look.raised
        midlight: root.look.surface
        mid: root.look.divider
        dark: root.look.divider
        shadow: root.look.divider
        accent: root.look.accent
        link: root.look.accent
        toolTipBase: root.look.raised
        toolTipText: root.look.text
    }

    background: Rectangle {
        color: root.look.window
    }

    function focusPane() {
        if (root.shell.workspace.focusedPane === Workspace.Session)
            session.forceActiveFocus();
        else
            arrangement.forceActiveFocus();
    }

    Component.onCompleted: focusPane()

    contentItem: ColumnLayout {
        spacing: 0

        TransportBar {
            id: transportBar
            Layout.fillWidth: true
            shell: root.shell
        }

        NoticeBar {
            Layout.fillWidth: true
            shell: root.shell
        }

        Item {
            id: views
            objectName: "views"

            readonly property bool both: root.shell.workspace.sessionVisible && root.shell.workspace.arrangementVisible
            readonly property real shared: height - (both ? divider.height : 0)

            Layout.fillWidth: true
            Layout.fillHeight: true

            SessionArea {
                id: session
                objectName: "session"
                shell: root.shell
                visible: root.shell.workspace.sessionVisible
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: views.both ? Math.round(views.shared * root.shell.workspace.split) : (visible ? views.height : 0)
            }

            SplitDivider {
                id: divider
                objectName: "divider"
                shell: root.shell
                available: views.shared
                visible: views.both
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: session.bottom
            }

            ArrangementArea {
                id: arrangement
                objectName: "arrangement"
                shell: root.shell
                visible: root.shell.workspace.arrangementVisible
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: views.both ? divider.bottom : parent.top
                anchors.bottom: parent.bottom
            }
        }

        HelpBar {
            Layout.fillWidth: true
            shell: root.shell
        }
    }

    ShortcutDispatcher {
        registry: root.shell.commands
    }

    CommandListPopup {
        id: commandList
        objectName: "commandList"
        shell: root.shell
    }

    PromptDialog {
        shell: root.shell
    }

    FolderDialog {
        id: folderDialog
        onAccepted: root.shell.document.folderChosen(selectedFolder)
        onRejected: root.shell.document.folderChoiceCancelled()
    }

    // The playhead is read from the engine once per frame, and only while
    // playing (docs/design/13-architecture.md, "The interface", item 5).
    FrameAnimation {
        running: root.shell.transport.playing
        onTriggered: root.shell.transport.poll()
    }

    Connections {
        target: root.shell
        function onCommandListRequested() {
            commandList.openList();
        }
        function onTempoEntryRequested() {
            transportBar.editTempo();
        }
    }

    Connections {
        target: root.shell.document
        function onFolderWanted(purpose: int) {
            folderDialog.title = purpose === DocumentController.Open ? qsTr("Open a Project Folder") : qsTr("Choose an Empty Folder for the Project");
            folderDialog.open();
        }
    }

    Connections {
        target: root.shell.workspace
        function onFocusReturned() {
            root.focusPane();
        }
    }
}
