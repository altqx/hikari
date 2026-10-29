import QtQuick
import QtTest

TestCase {
    name: "QuickTestFailingControl"
    function test_fails_on_purpose() { compare(1 + 1, 3) }
}
