import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    width: 520; height: 400
    visible: true
    title: "Essai de Qt Quick"
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 16; spacing: 12
        Label { text: "Qt Quick " + Qt.platform.os + " / " + Qt.application.name; font.pixelSize: 18 }
        TextField { Layout.fillWidth: true; text: "Bonjour, Korin" }
        Slider { id: s; Layout.fillWidth: true; from: 0; to: 100; value: 40 }
        ProgressBar { Layout.fillWidth: true; from: 0; to: 100; value: s.value }
        RowLayout {
            Button { text: "Bouton"; onClicked: info.text = "cliqué à " + Math.round(s.value) }
            CheckBox { text: "Case" }
            ComboBox { model: ["Givre", "Onyx Noir"] }
        }
        Label { id: info; text: "…" }
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; radius: 8; color: palette.highlight
            RotationAnimation on rotation { from: -3; to: 3; duration: 1200; loops: Animation.Infinite; easing.type: Easing.InOutQuad }
            Text { anchors.centerIn: parent; text: "♪ Korin"; color: palette.highlightedText; font.pixelSize: 28 }
        }
    }
}
