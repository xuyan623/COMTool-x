pragma Singleton
import QtQuick

QtObject {
    readonly property bool dark: App.theme !== "light"
    readonly property color canvas: "#111923"
    readonly property color panel: dark ? "#182330" : "#F4F6F8"
    readonly property color raised: dark ? "#213142" : "#E3EAF0"
    readonly property color border: dark ? "#2D3E50" : "#C2CFDA"
    readonly property color text: dark ? "#E6EDF5" : "#172D40"
    readonly property color muted: dark ? "#9FB0C3" : "#526B80"
    readonly property color blue: dark ? "#66B4F4" : "#1768A7"
    readonly property color error: dark ? "#F59A94" : "#AA312D"
    readonly property color green: dark ? "#83CFAD" : "#207950"
}
