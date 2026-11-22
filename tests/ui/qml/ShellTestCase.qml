// SPDX-License-Identifier: GPL-3.0-or-later
//
// A test case with the main window's content in it, over a fresh shell for
// each test function.
import QtQuick
import QtTest
import Lina.Shell
import Lina.Ui
import LinaTest

TestCase {
    id: testCase

    width: 1100
    height: 760
    // A test case is invisible by default, and so would be the window's
    // content in it: no mouse event would reach it.
    visible: true
    when: windowShown

    Component {
        id: viewComponent

        MainView {
            anchors.fill: parent
        }
    }

    function makeView(): MainView {
        const view = createTemporaryObject(viewComponent, testCase, {
            shell: Harness.makeShell()
        });
        verify(view !== null);
        waitForPolish(view);
        return view;
    }

    function typeText(text: string) {
        for (const character of text)
            keyClick(character);
    }
}
