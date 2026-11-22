// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reports that need no answer: what happened and what to do next. Shown
// under the transport bar without blocking anything (principle 2). A
// problem and a note differ in their first word as well as in colour.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lina.Ui

Rectangle {
    id: root

    required property Shell shell
    readonly property Appearance look: shell.appearance
    readonly property DocumentController document: shell.document

    visible: document.notice !== ""
    implicitHeight: visible ? row.implicitHeight + 2 * look.unit : 0
    color: look.raised
    border.color: document.noticeIsError ? look.error : look.warning
    border.width: 2
    Accessible.role: Accessible.AlertMessage
    Accessible.name: message.text

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: root.look.unit
        spacing: root.look.unit

        Label {
            id: message
            objectName: "noticeText"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: root.look.text
            text: (root.document.noticeIsError ? qsTr("Problem: %1") : qsTr("Note: %1")).arg(root.document.notice)
        }

        Button {
            text: qsTr("Dismiss")
            focusPolicy: Qt.TabFocus
            Accessible.name: qsTr("Dismiss message")
            onClicked: root.document.dismissNotice()
            Keys.onReturnPressed: root.document.dismissNotice()
        }
    }
}
