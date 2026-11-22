// SPDX-License-Identifier: GPL-3.0-or-later
//
// The project tempo (principle 8): type a value and press Enter, Escape to
// leave it; Up and Down change it by one beat per minute, with Shift by a
// hundredth. The unit beside it can be dragged up and down (with Shift,
// finely) and double-clicked for 120. Every change is one undo step; a
// drag is one step however far it goes. Finishing or cancelling the entry
// gives the keys back to the pane that had them (principle 5).
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lina.Ui

RowLayout {
    id: root

    required property Shell shell
    readonly property Appearance look: shell.appearance
    readonly property TransportController transport: shell.transport
    readonly property string explanation: qsTr("Beats per minute, from 20 to 300. Type a tempo and press Enter; Up and Down change it by one, with Shift by a hundredth.")

    function startEditing() {
        field.forceActiveFocus();
        field.selectAll();
    }

    function finish() {
        field.text = Qt.binding(() => root.transport.tempoText);
        root.shell.workspace.returnFocus();
    }

    spacing: look.unit / 2

    TextField {
        id: field
        objectName: "tempoField"

        Layout.preferredWidth: 9 * root.look.unit
        text: root.transport.tempoText
        horizontalAlignment: TextInput.AlignRight
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        Accessible.name: qsTr("Tempo")
        Accessible.description: root.explanation

        onAccepted: {
            root.transport.setTempoText(text);
            root.finish();
        }
        Keys.onEscapePressed: root.finish()
        Keys.onUpPressed: event => {
            root.transport.stepTempo(1, (event.modifiers & Qt.ShiftModifier) !== 0);
            text = Qt.binding(() => root.transport.tempoText);
        }
        Keys.onDownPressed: event => {
            root.transport.stepTempo(-1, (event.modifiers & Qt.ShiftModifier) !== 0);
            text = Qt.binding(() => root.transport.tempoText);
        }
        onActiveFocusChanged: {
            if (activeFocus)
                root.shell.workspace.showHelp(Accessible.name, root.explanation);
            else
                text = Qt.binding(() => root.transport.tempoText);
        }

        HoverHandler {
            onHoveredChanged: {
                if (hovered)
                    root.shell.workspace.showHelp(field.Accessible.name, root.explanation);
                else
                    root.shell.workspace.clearHelp(field.Accessible.name);
            }
        }
    }

    Label {
        id: unit
        objectName: "tempoUnit"

        text: qsTr("BPM")
        color: root.look.mutedText
        Accessible.role: Accessible.StaticText
        Accessible.name: qsTr("Tempo unit: drag to change the tempo")
        Accessible.description: qsTr("Drag up or down to change the tempo, with Shift finely; double-click for 120.")

        // A drag ends by how its grab ends, not by `active`: when the grab
        // is taken away, `active` turns false before `canceled` comes, and
        // ending on `active` would keep a drag that was cancelled.
        DragHandler {
            objectName: "tempoDrag"
            target: null
            xAxis.enabled: false
            cursorShape: Qt.SizeVerCursor
            onActiveChanged: {
                if (active)
                    root.transport.beginTempoDrag();
            }
            onActiveTranslationChanged: {
                if (active)
                    root.transport.dragTempo(-activeTranslation.y, (centroid.modifiers & Qt.ShiftModifier) !== 0);
            }
            onGrabChanged: transition => {
                if (transition === PointerDevice.UngrabExclusive)
                    root.transport.endTempoDrag();
                else if (transition === PointerDevice.CancelGrabExclusive)
                    root.transport.cancelTempoDrag();
            }
            onCanceled: root.transport.cancelTempoDrag()
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeVerCursor
            onDoubleClicked: root.transport.resetTempo()
        }

        HoverHandler {
            cursorShape: Qt.SizeVerCursor
            onHoveredChanged: {
                if (hovered)
                    root.shell.workspace.showHelp(unit.Accessible.name, unit.Accessible.description);
                else
                    root.shell.workspace.clearHelp(unit.Accessible.name);
            }
        }
    }
}
