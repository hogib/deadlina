// SPDX-License-Identifier: GPL-3.0-or-later
//
// Light and dark, following the desktop unless chosen; size steps; reduced
// motion.
import QtQuick
import Lina.Ui
import LinaTest

ShellTestCase {
    name: "Appearance"

    function test_switch_light_and_dark() {
        const view = makeView();
        const shell = view.shell;
        Harness.setDesktopDark(shell, false);
        compare((view.background as Rectangle).color, Qt.color("#ece8df"));
        compare(view.palette.windowText, shell.appearance.text);

        shell.appearance.mode = Appearance.Dark;
        compare((view.background as Rectangle).color, Qt.color("#141619"));
        compare(view.palette.windowText, shell.appearance.text);
        compare(view.palette.windowText, Qt.color("#e9e6e0"));

        // Through the command list, as a user would.
        keyClick(Qt.Key_K, Qt.ControlModifier);
        tryVerify(() => findChild(view, "commandList").opened);
        typeText("appearance light");
        keyClick(Qt.Key_Return);
        compare((view.background as Rectangle).color, Qt.color("#ece8df"));
    }

    function test_follow_the_desktop() {
        const view = makeView();
        const shell = view.shell;
        compare(shell.appearance.mode, Appearance.FollowDesktop);
        Harness.setDesktopDark(shell, true);
        verify(shell.appearance.dark);
        compare((view.background as Rectangle).color, Qt.color("#141619"));
        Harness.setDesktopDark(shell, false);
        compare((view.background as Rectangle).color, Qt.color("#ece8df"));
    }

    function test_larger_and_smaller() {
        const view = makeView();
        const shell = view.shell;
        const before = view.font.pointSize;
        const unit = shell.appearance.unit;
        keyClick(Qt.Key_Equal, Qt.ControlModifier);
        compare(shell.appearance.scale, 1.125);
        verify(view.font.pointSize > before);
        verify(shell.appearance.unit > unit);
        keyClick(Qt.Key_0, Qt.ControlModifier);
        compare(view.font.pointSize, before);
        keyClick(Qt.Key_Minus, Qt.ControlModifier);
        verify(view.font.pointSize < before);
    }

    function test_reduced_motion() {
        const view = makeView();
        const shell = view.shell;
        verify(shell.appearance.animationMs > 0);
        Harness.setReducedMotion(shell, true);
        compare(shell.appearance.animationMs, 0);
        compare(findChild(view, "commandResults").highlightMoveDuration, 0);
    }
}
