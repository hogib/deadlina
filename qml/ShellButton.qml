// SPDX-License-Identifier: GPL-3.0-or-later
//
// A button that runs a registered command, so that a click, its shortcut
// and the command list all do the same thing. It shows the command's name,
// gives it to assistive technology, and explains the command in the help
// area while pointed at or focused. Clicking does not take the keys from
// the pane that has them; Tab reaches it, and Enter runs it.
import QtQuick
import QtQuick.Controls.Basic
import Lina.Ui

Button {
    id: control

    required property Shell shell
    required property string command
    /// What the help area says; the command's own description by default.
    property string explanation: userCommand ? userCommand.description : ""
    /// Shown instead of the command's name, for a button whose label says
    /// what it will do next.
    property string label: ""
    readonly property UserCommand userCommand: shell.commands.command(command)
    readonly property Appearance look: shell.appearance

    text: label !== "" ? label : (userCommand ? userCommand.name : "")
    enabled: userCommand !== null && userCommand.enabled && !shell.commands.suspended
    focusPolicy: Qt.TabFocus
    padding: look.unit
    Accessible.name: text
    Accessible.description: explanation

    ToolTip.visible: hovered && userCommand !== null && userCommand.shortcut !== ""
    ToolTip.delay: 600
    ToolTip.text: userCommand ? userCommand.shortcut : ""

    onClicked: userCommand.trigger()
    Keys.onReturnPressed: userCommand.trigger()
    Keys.onEnterPressed: userCommand.trigger()

    onActiveFocusChanged: {
        if (activeFocus)
            shell.workspace.showHelp(text, explanation);
    }

    HoverHandler {
        onHoveredChanged: {
            if (hovered)
                control.shell.workspace.showHelp(control.text, control.explanation);
            else
                control.shell.workspace.clearHelp(control.text);
        }
    }

    background: Rectangle {
        radius: control.look.unit / 2
        color: control.highlighted ? control.look.accent : (control.down ? control.look.divider : control.look.raised)
        border.color: control.visualFocus ? control.look.focus : control.look.divider
        border.width: control.visualFocus ? 2 : 1
    }

    contentItem: Text {
        text: control.text
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        color: control.highlighted ? control.look.onAccent : (control.enabled ? control.look.text : control.look.mutedText)
    }
}
