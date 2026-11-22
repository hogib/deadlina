// SPDX-License-Identifier: GPL-3.0-or-later
//
// The one main window (docs/design/12-interaction-principles.md, principle 10).
// The application shows it once everything it shows is in place.
import QtQuick
import QtQuick.Controls.Basic
import Lina.Ui

ApplicationWindow {
    id: window

    required property Shell shell

    width: 1280
    height: 800
    minimumWidth: 640
    minimumHeight: 480
    title: shell.windowTitle
    color: shell.appearance.window

    // Closing is a request: unsaved changes are offered for saving first,
    // and the application ends when the document controller approves.
    onClosing: close => {
        close.accepted = false;
        window.shell.document.requestQuit();
    }

    MainView {
        anchors.fill: parent
        shell: window.shell
    }
}
