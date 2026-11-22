// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shows the document controller's question, when there is one, and hands
// the choice back. Escape picks the choice that changes nothing.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lina.Ui

Dialog {
    id: dialog

    required property Shell shell
    readonly property Prompt prompt: shell.document.prompt
    readonly property Appearance look: shell.appearance

    objectName: "promptDialog"
    anchors.centerIn: Overlay.overlay
    width: Math.min(parent ? parent.width - 4 * look.unit : 0, 60 * look.unit)
    modal: true
    closePolicy: Popup.CloseOnEscape
    title: prompt.title

    // A question asked before the window existed (opening a project from
    // the command line) is shown as soon as it does.
    Component.onCompleted: {
        if (prompt.active)
            open();
    }

    onClosed: {
        // Escape: the question is still open, and is answered with the
        // choice that changes nothing.
        if (prompt.active)
            prompt.answer(prompt.cancelChoice);
    }

    Connections {
        target: dialog.prompt
        function onChanged() {
            if (dialog.prompt.active)
                dialog.open();
            else
                dialog.close();
        }
    }

    contentItem: Label {
        text: dialog.prompt.text
        wrapMode: Text.WordWrap
        color: dialog.look.text
    }

    footer: RowLayout {
        spacing: dialog.look.unit

        Item {
            Layout.fillWidth: true
        }

        Repeater {
            model: dialog.prompt.choices

            Button {
                id: choice

                required property int index
                required property string modelData

                Layout.margins: dialog.look.unit
                text: modelData
                focus: index === 0
                Accessible.name: modelData
                onClicked: dialog.prompt.answer(index)
                Keys.onReturnPressed: dialog.prompt.answer(index)
                Keys.onEnterPressed: dialog.prompt.answer(index)
            }
        }
    }
}
