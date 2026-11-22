// SPDX-License-Identifier: GPL-3.0-or-later
//
// The arrangement's place in the window: track headers from the track list
// model, and room for the timeline, which is a custom scene-graph item that
// is not built yet. Headers are a list view, which makes items only for the
// rows in sight (docs/design/13-architecture.md, "The interface").
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import Lina.Ui

PaneFrame {
    id: root

    pane: Workspace.Arrangement
    title: qsTr("Arrangement")
    explanation: qsTr("Clips at fixed places in time, played from left to right: tracks are rows.")

    ListView {
        id: headers
        objectName: "trackHeaders"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 24 * root.look.unit
        visible: count > 0
        clip: true
        spacing: root.look.unit / 2
        model: root.shell.tracks
        boundsBehavior: Flickable.StopAtBounds
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Track headers")

        delegate: Rectangle {
            id: header

            required property string name
            required property string kindText
            required property color trackColour

            width: ListView.view.width
            height: 4 * root.look.unit
            color: root.look.raised
            border.color: root.look.divider
            Accessible.role: Accessible.ListItem
            Accessible.name: qsTr("%1, %2 track").arg(header.name).arg(header.kindText)

            Rectangle {
                id: swatch
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: root.look.unit
                color: header.trackColour
            }

            Label {
                anchors.left: swatch.right
                anchors.leftMargin: root.look.unit
                anchors.right: kind.left
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                text: header.name
                color: root.look.text
            }

            Label {
                id: kind
                anchors.right: parent.right
                anchors.rightMargin: root.look.unit
                anchors.verticalCenter: parent.verticalCenter
                text: header.kindText
                color: root.look.mutedText
            }
        }
    }

    Label {
        anchors.left: headers.visible ? headers.right : parent.left
        anchors.leftMargin: 2 * root.look.unit
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: root.look.mutedText
        text: root.shell.tracks.count === 0 ? qsTr("The arrangement is empty. Tracks you add appear here as rows.") : qsTr("The timeline is drawn here in a later version of LINA.")
    }
}
