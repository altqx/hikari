import QtQuick
import QtTest

// Must fail; registered with WILL_FAIL.
TestCase {
    name: "QuickTestFailingControl"
    function test_fails_on_purpose() { compare(1 + 1, 3) }
}
