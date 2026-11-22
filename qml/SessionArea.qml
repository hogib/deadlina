// SPDX-License-Identifier: GPL-3.0-or-later
//
// The session's place in the window. Its clip grid will be a custom
// scene-graph item (docs/design/13-architecture.md, "The interface"); until
// it exists the pane says what it is for and how to start (principle 9).
import QtQuick
import QtQuick.Controls.Basic
import Lina.Ui

PaneFrame {
    id: root

    pane: Workspace.Session
    title: qsTr("Session")
    explanation: qsTr("Clips in slots, launched in time with each other: tracks are columns and scenes are rows.")

    Label {
        anchors.centerIn: parent
        width: Math.min(parent.width, 60 * root.look.unit)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: root.look.mutedText
        text: root.shell.tracks.count === 0 ? qsTr("The session is empty. Add a track with “Add Audio Track” (%1) or “Add MIDI Track” (%2), then put clips into its slots.").arg(root.shell.commands.command("track.addAudio").shortcut).arg(root.shell.commands.command("track.addMidi").shortcut) : qsTr("%n track(s). The grid of slots is drawn here in a later version of LINA.", "", root.shell.tracks.count)
    }
}
