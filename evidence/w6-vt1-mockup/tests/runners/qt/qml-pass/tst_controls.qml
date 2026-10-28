import QtQuick
import QtTest

TestCase {
    name: "QuickTestControls"
    Item { id: box; width: 10; height: 4 }
    function test_passes() { compare(box.width * box.height, 40) }
}
