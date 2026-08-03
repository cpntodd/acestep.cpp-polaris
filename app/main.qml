// Polaris Studio — modern retro desktop shell.
// The visual language is intentionally dark, tactile, and terminal-adjacent:
// crisp borders, monospaced labels, electric accents, and dense information.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia

ApplicationWindow {
    id: root
    title: "Polaris Studio"
    width: 1240
    height: 780
    minimumWidth: 1040
    minimumHeight: 640
    visible: true
    color: bg

    readonly property bool amberTheme: themeSettings.theme === "amber"
    readonly property bool monoTheme: themeSettings.theme === "mono"
    readonly property color bg: amberTheme ? "#1a1515" : (monoTheme ? "#0f1515" : "#0b1020")
    readonly property color surface: amberTheme ? "#271d1a" : (monoTheme ? "#151e1e" : "#11192d")
    readonly property color surfaceRaised: amberTheme ? "#33251f" : (monoTheme ? "#1f2a2a" : "#17233d")
    readonly property color surfaceHot: amberTheme ? "#493426" : (monoTheme ? "#293838" : "#202f50")
    readonly property color line: amberTheme ? "#6d4c34" : (monoTheme ? "#3c5555" : "#2a3a5f")
    readonly property color ink: amberTheme ? "#ffe7c7" : (monoTheme ? "#edf4e6" : "#eaf2ff")
    readonly property color muted: amberTheme ? "#ba9878" : (monoTheme ? "#87a59c" : "#8190b0")
    readonly property color cyan: amberTheme ? "#ffbf59" : (monoTheme ? "#a4f6d2" : "#59e3ff")
    readonly property color coral: amberTheme ? "#ff775c" : (monoTheme ? "#d3e47b" : "#ff826f")
    readonly property color lime: amberTheme ? "#d8ed7c" : (monoTheme ? "#c5ef9c" : "#c7f36b")
    readonly property color violet: amberTheme ? "#ff9b73" : (monoTheme ? "#86d8ec" : "#ab91ff")
    readonly property color chromeTop: amberTheme ? "#8b5a21" : (monoTheme ? "#1f665c" : "#1768ba")
    readonly property color chromeBottom: amberTheme ? "#3b2716" : (monoTheme ? "#123b46" : "#092b63")
    readonly property color chromeLine: amberTheme ? "#d3983b" : (monoTheme ? "#59d7c2" : "#48a9ff")
    readonly property string mono: "DejaVu Sans Mono"
    // Scale the complete desktop surface in both directions. The previous
    // upper bound of 1.0 fixed compact windows but left maximized windows with
    // a small, unscaled workspace surrounded by unused space.
    readonly property real uiScale: Math.min(1.35,
                                             contentItem.width / 1500.0,
                                             contentItem.height / 860.0)
    readonly property bool compactMode: uiScale < 1.0
    readonly property bool showSummaryCards: !compactMode && (contentItem.height / uiScale) >= 940

    property var props: ({})
    property string engineState: engineProcess.state
    property bool engineRunning: engineProcess.running
    readonly property bool ipcConnected: engineClient.connected
    readonly property bool serverReady: engineRunning && ipcConnected
    readonly property bool serverBooting: engineState === "starting" || (engineRunning && !ipcConnected)
    readonly property bool serverStopped: !engineRunning && engineState === "off"
    readonly property color bootAmber: amberTheme ? "#ffd166" : "#ffc857"
    readonly property color stopRed: amberTheme ? "#ff6b4a" : (monoTheme ? "#f07178" : "#ff5c5c")
    property real bootPulse: 0
    property var songs: []
    property int currentSongId: -1
    property bool synthBusy: false
    property string synthJobId: ""
    property string synthStatus: ""
    property int modelsDownloaded: 0
    property int currentView: 0
    property string pixelProfileId: downloader.selectedImageProfile
    property string pixelAssetType: "Terrain Tile"
    property string pixelPrompt: ""
    property string pixelNegativePrompt: "blurry, low detail, text, watermark, photorealistic"
    property int pixelWidth: 512
    property int pixelHeight: 512
    property int pixelSteps: 20
    property int pixelSeed: 42
    readonly property var navItems: [
        { label: "STUDIO", icon: "✦", hint: "compose" },
        { label: "LIBRARY", icon: "▤", hint: "archive" },
        { label: "MONITOR", icon: "◫", hint: "telemetry" },
        { label: "MODELS", icon: "◇", hint: "vault" },
        { label: "PIXEL LAB", icon: "▧", hint: "image forge" }
    ]
    readonly property var viewTitles: ["Studio Deck", "Library Archive", "System Monitor", "Model Vault", "Pixel Lab"]
    readonly property var viewSubtitles: [
        "GENERATIVE CONSOLE / READY FOR INPUT",
        "LOCAL AUDIO INDEX / PRIVATE BY DEFAULT",
        "ENGINE TELEMETRY / RESOURCE OVERVIEW",
        "WEIGHTS & ADAPTERS / OFFLINE STORAGE",
        "LOCAL IMAGE WORKSHOP / PIXEL ASSET FORGE"
    ]

    onEngineStateChanged: {
        if (!root.serverBooting) root.bootPulse = 0
    }

    SequentialAnimation on bootPulse {
        running: root.serverBooting
        loops: Animation.Infinite
        NumberAnimation { from: 0; to: 1; duration: 1500; easing.type: Easing.InOutSine }
        NumberAnimation { from: 1; to: 0; duration: 1500; easing.type: Easing.InOutSine }
    }

    function refreshSongs() { songs = library.songList() }
    function formatBytes(bytes) {
        if (!bytes || bytes <= 0) return "—"
        var units = ["B", "KB", "MB", "GB"]
        var value = bytes
        var index = 0
        while (value >= 1024 && index < 3) { value /= 1024
 index++ }
        return value.toFixed(index > 0 ? 1 : 0) + " " + units[index]
    }
    function formatDuration(secs) {
        if (!secs || secs <= 0) return "—"
        var minutes = Math.floor(secs / 60)
        var seconds = Math.floor(secs % 60)
        return minutes + ":" + (seconds < 10 ? "0" : "") + seconds
    }
    function modelCount(key) {
        return displayedModels(key).length
    }
    function displayedModels(key) {
        if (props.models && props.models[key] && props.models[key].length > 0)
            return props.models[key]

        // Show the selected folder immediately while the engine is booting.
        // Engine GGUF-header classification replaces this fallback once IPC
        // props arrive.
        var patterns = {
            lm: /(lm|5hz)/i,
            embedding: /(embedding|qwen)/i,
            dit: /(dit|acestep-v15)/i,
            vae: /vae/i
        }
        var pattern = patterns[key]
        if (!pattern) return []
        return (downloader.localModelFiles || []).filter(function(file) {
            return pattern.test(file)
        })
    }
    function cpuGaugeValue() {
        return systemMonitor.cpuAvailable ? systemMonitor.cpuUsage : -1
    }
    function memoryGaugeValue() {
        return systemMonitor.ramAvailable ? systemMonitor.ramUsage : -1
    }
    function gpuGaugeValue() {
        return systemMonitor.gpuAvailable ? systemMonitor.gpuUsage : -1
    }
    function vramGaugeValue() {
        return systemMonitor.vramAvailable ? systemMonitor.vramUsage : -1
    }
    function ramGaugeDetail() {
        return systemMonitor.ramAvailable
                ? formatBytes(systemMonitor.ramUsed) + " / " + formatBytes(systemMonitor.ramTotal)
                : "NO DATA"
    }
    function vramGaugeDetail() {
        return systemMonitor.vramAvailable
                ? formatBytes(systemMonitor.vramUsed) + " / " + formatBytes(systemMonitor.vramTotal)
                : "NO DATA"
    }
    function pixelProfileIndex() {
        for (var i = 0; i < downloader.imageProfiles.length; i++) {
            if (downloader.imageProfiles[i].id === root.pixelProfileId) return i
        }
        return 0
    }
    function pixelProfileStateLabel() {
        var state = downloader.imageProfileStatus
        if (state === "installed") return "INSTALLED / READY"
        if (state === "downloading") return "DOWNLOADING"
        if (state === "cancelled") return "CANCELLED"
        if (state === "error") return "DOWNLOAD ERROR"
        if (state === "busy") return "WAITING FOR DOWNLOAD SLOT"
        return "DOWNLOAD REQUIRED"
    }
    function refreshModelVault() {
        root.props = ({})
        root.modelsDownloaded = 0
        downloader.scanLocalModels(engineProcess.modelsDir)
        downloader.inspectImageProfile(root.pixelProfileId, engineProcess.modelsDir)
        engineProcess.refreshModels()
        modelPropsRefresh.restart()
    }
    function refreshEngineProps() {
        if (!root.engineRunning || !root.ipcConnected) return
        try {
            var result = engineClient.call("props")
            if (result && Object.keys(result).length > 0) {
                root.props = result
                root.modelsDownloaded = modelCount("lm") + modelCount("dit")
                        + modelCount("embedding") + modelCount("vae")
            } else {
                modelPropsRefresh.restart()
            }
        } catch (error) {
            modelPropsRefresh.restart()
        }
    }
    function doGenerate() {
        if (synthBusy || !engineRunning) return
        synthBusy = true
        synthStatus = "Submitting…"
        var params = {
            caption: captionField.text,
            synth_model: ditModelCombo.currentText,
            lm_model: lmModelCombo.currentText,
            vae: vaeModelCombo.currentText,
            output_format: formatCombo.currentText,
            inference_steps: stepsSpin.value
        }
        var result = engineClient.call("synth", params)
        if (result && result.id) {
            synthJobId = result.id
            synthStatus = "Job " + synthJobId + " running…"
        } else {
            synthStatus = "Failed to start synth job"
            synthBusy = false
        }
    }
    function checkSynthJob() {
        if (!synthBusy || synthJobId === "") return
        var status = engineClient.call("job_status", { id: synthJobId })
        if (!status || !status.status) return
        if (status.status === "done") {
            engineClient.call("job_result", { id: synthJobId })
            synthStatus = "Generation complete — result added to library"
            synthBusy = false
            synthJobId = ""
            refreshSongs()
        } else if (status.status === "failed" || status.status === "cancelled") {
            synthStatus = "Generation " + status.status
            synthBusy = false
            synthJobId = ""
        }
    }

    component RetroButton: Button {
        id: control
        property color accent: root.cyan
        implicitHeight: 38
        leftPadding: 14
        rightPadding: 14
        contentItem: Text {
            text: control.text
            color: control.enabled ? root.ink : root.muted
            font.family: root.mono
            font.pixelSize: 11
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 6
            color: control.pressed ? control.accent : (control.hovered ? root.surfaceHot : root.surfaceRaised)
            border.color: control.enabled ? control.accent : root.line
            border.width: control.hovered ? 2 : 1
            opacity: control.enabled ? 1 : 0.55
        }
    }

    component RetroToolButton: Button {
        id: tool
        property color accent: root.cyan
        implicitWidth: 30
        implicitHeight: 28
        padding: 0
        contentItem: Text {
            text: tool.text
            color: tool.enabled ? root.ink : root.muted
            font.family: root.mono
            font.pixelSize: 15
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 2
            color: tool.pressed ? tool.accent : (tool.hovered ? root.surfaceHot : root.surfaceRaised)
            border.color: tool.hovered ? tool.accent : root.chromeLine
            border.width: tool.hovered ? 2 : 1
        }
    }

    component RetroField: TextField {
        id: field
        implicitHeight: 40
        color: root.ink
        placeholderTextColor: root.muted
        font.family: root.mono
        font.pixelSize: 12
        leftPadding: 12
        rightPadding: 12
        background: Rectangle {
            radius: 5
            color: root.bg
            border.color: field.activeFocus ? root.cyan : root.line
            border.width: field.activeFocus ? 2 : 1
        }
    }

    component RetroCombo: ComboBox {
        id: combo
        implicitHeight: 38
        font.family: root.mono
        font.pixelSize: 11
        contentItem: Text {
            leftPadding: 12
            rightPadding: 28
            text: combo.displayText || "not scanned"
            color: combo.enabled ? root.ink : root.muted
            font.family: root.mono
            font.pixelSize: 11
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Text {
            x: combo.width - width - 12
            y: (combo.height - height) / 2
            text: "▾"
            color: root.cyan
            font.family: root.mono
        }
        background: Rectangle {
            radius: 5
            color: root.bg
            border.color: combo.pressed || combo.activeFocus ? root.cyan : root.line
            border.width: combo.activeFocus ? 2 : 1
        }
    }

    component RetroSpin: SpinBox {
        id: spin
        implicitHeight: 38
        editable: false
        contentItem: Text {
            text: spin.value
            color: root.ink
            font.family: root.mono
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        up.indicator: Text { text: "+"
 color: root.cyan
 font.family: root.mono
 anchors.centerIn: parent }
        down.indicator: Text { text: "−"
 color: root.cyan
 font.family: root.mono
 anchors.centerIn: parent }
        background: Rectangle { radius: 5
 color: root.bg
 border.color: root.line }
    }

    component RetroProgress: ProgressBar {
        id: progress
        implicitHeight: 8
        background: Rectangle { radius: 4
 color: root.bg
 border.color: root.line }
        contentItem: Item {
            Rectangle {
                width: parent.width * progress.position
                height: parent.height
                radius: 4
                color: progress.indeterminate ? root.violet : root.cyan
            }
        }
    }

    component RetroGauge: Item {
        id: gauge
        property string label: "RESOURCE"
        property real value: -1
        property real animatedValue: value
        property string detail: "WAITING"
        property color accent: root.cyan
        implicitWidth: 116
        implicitHeight: 136

        Behavior on animatedValue {
            SmoothedAnimation {
                velocity: 80
                maximumEasingTime: 500
                reversingMode: SmoothedAnimation.Eased
            }
        }

        Canvas {
            id: dial
            width: 108
            height: 108
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            onPaint: {
                var context = getContext("2d")
                context.clearRect(0, 0, width, height)
                var centerX = width / 2
                var centerY = height / 2
                var radius = 42
                var start = -Math.PI * 0.75
                var span = Math.PI * 1.5
                for (var tick = 0; tick <= 10; tick++) {
                    var tickAngle = start + span * tick / 10
                    context.beginPath()
                    context.moveTo(centerX + Math.cos(tickAngle) * 48,
                                   centerY + Math.sin(tickAngle) * 48)
                    context.lineTo(centerX + Math.cos(tickAngle) * 51,
                                   centerY + Math.sin(tickAngle) * 51)
                    context.lineWidth = 1
                    context.strokeStyle = root.muted
                    context.stroke()
                }
                context.beginPath()
                context.arc(centerX, centerY, radius, start, start + span)
                context.lineWidth = 9
                context.lineCap = "butt"
                context.strokeStyle = root.line
                context.stroke()
                if (gauge.value >= 0) {
                    var normalized = Math.max(0, Math.min(1, gauge.animatedValue / 100))
                    var angle = start + span * normalized
                    context.beginPath()
                    context.arc(centerX, centerY, radius, start, angle)
                    context.strokeStyle = gauge.accent
                    context.stroke()
                    context.beginPath()
                    context.moveTo(centerX, centerY)
                    context.lineTo(centerX + Math.cos(angle) * 33,
                                   centerY + Math.sin(angle) * 33)
                    context.lineWidth = 2
                    context.strokeStyle = gauge.accent
                    context.stroke()
                    context.beginPath()
                    context.arc(centerX, centerY, 4, 0, Math.PI * 2)
                    context.fillStyle = gauge.accent
                    context.fill()
                }
            }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            Connections {
                target: gauge
                function onAnimatedValueChanged() { dial.requestPaint() }
                function onAccentChanged() { dial.requestPaint() }
            }
        }
        Label {
            anchors.top: parent.top
            anchors.topMargin: 34
            anchors.horizontalCenter: parent.horizontalCenter
            text: gauge.label
            color: root.muted
            font.family: root.mono
            font.pixelSize: 9
            font.bold: true
        }
        Label {
            anchors.top: parent.top
            anchors.topMargin: 50
            anchors.horizontalCenter: parent.horizontalCenter
            text: gauge.value >= 0 ? Math.round(gauge.animatedValue) + "%" : "—"
            color: gauge.value >= 0 ? gauge.accent : root.muted
            font.family: root.mono
            font.pixelSize: 19
            font.bold: true
        }
        Label {
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            text: gauge.detail
            color: root.muted
            font.family: root.mono
            font.pixelSize: 9
            elide: Text.ElideRight
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
        }
    }

    component GaugePair: Rectangle {
        id: pair
        property string groupLabel: "RESOURCE GROUP"
        property string firstLabel: "A"
        property real firstValue: -1
        property string firstDetail: "NO DATA"
        property color firstAccent: root.cyan
        property string secondLabel: "B"
        property real secondValue: -1
        property string secondDetail: "NO DATA"
        property color secondAccent: root.lime
        implicitHeight: 166
        color: root.bg
        radius: 6
        border.color: root.line

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 2
            Label {
                text: pair.groupLabel
                color: root.muted
                font.family: root.mono
                font.pixelSize: 9
                font.bold: true
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                RetroGauge {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    label: pair.firstLabel
                    value: pair.firstValue
                    detail: pair.firstDetail
                    accent: pair.firstAccent
                }
                RetroGauge {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    label: pair.secondLabel
                    value: pair.secondValue
                    detail: pair.secondDetail
                    accent: pair.secondAccent
                }
            }
        }
    }

    Component.onCompleted: {
        refreshSongs()
        pixelProfileCombo.currentIndex = pixelProfileIndex()
        downloader.scanLocalModels(engineProcess.modelsDir)
        downloader.inspectImageProfile(root.pixelProfileId, engineProcess.modelsDir)
        imageEngine.refresh()
    }

    onClosing: function(close) {
        close.accepted = false
        root.hide()
    }

    menuBar: MenuBar {
        palette.window: root.surface
        palette.button: root.surface
        palette.buttonText: root.ink
        palette.text: root.ink
        palette.highlight: root.surfaceHot
        palette.highlightedText: root.cyan
        background: Rectangle {
            color: root.surface
            border.color: root.line
            border.width: 1
        }
        Menu {
            title: "FILE"
            MenuItem {
                text: "Import Audio"
                onTriggered: importDialog.open()
            }
            MenuSeparator {}
            MenuItem {
                text: "Quit"
                onTriggered: Qt.quit()
            }
        }
        Menu {
            title: "EDIT"
            MenuItem { text: "Undo" }
            MenuItem { text: "Redo" }
            MenuSeparator {}
            MenuItem {
                text: "Preferences"
                onTriggered: themeSettings.theme = themeSettings.theme
            }
        }
        Menu {
            title: "FRAME"
            MenuItem { text: "Previous Frame" }
            MenuItem { text: "Next Frame" }
            MenuItem {
                text: "Render Current Frame"
                onTriggered: root.doGenerate()
            }
        }
        Menu {
            title: "ANIMATION"
            MenuItem { text: "Play Preview" }
            MenuItem { text: "Stop Preview" }
            MenuItem {
                text: "Loop Playback"
                checkable: true
                checked: true
            }
        }
        Menu {
            title: "VIEW"
            MenuItem {
                text: "Studio Deck"
                onTriggered: root.currentView = 0
            }
            MenuItem {
                text: "Library Archive"
                onTriggered: root.currentView = 1
            }
            MenuItem {
                text: "System Monitor"
                onTriggered: root.currentView = 2
            }
            MenuItem {
                text: "Model Vault"
                onTriggered: root.currentView = 3
            }
            MenuItem {
                text: "Pixel Lab"
                onTriggered: root.currentView = 4
            }
        }
        Menu {
            title: "SETTINGS"
            Menu {
                title: "COLOR THEME"
                MenuItem {
                    text: "Night Terminal"
                    checkable: true
                    checked: themeSettings.theme === "night"
                    onTriggered: themeSettings.theme = "night"
                }
                MenuItem {
                    text: "Amber CRT"
                    checkable: true
                    checked: themeSettings.theme === "amber"
                    onTriggered: themeSettings.theme = "amber"
                }
                MenuItem {
                    text: "Mono Mint"
                    checkable: true
                    checked: themeSettings.theme === "mono"
                    onTriggered: themeSettings.theme = "mono"
                }
            }
        }
    }

    Connections {
        target: engineProcess
        function onStateChanged() {
            root.engineState = engineProcess.state
            root.engineRunning = engineProcess.running
            if (root.engineRunning) modelPropsRefresh.restart()
        }
        function onModelsDirChanged() {
            root.refreshModelVault()
        }
        function onEngineOutput(line) {
            logView.text += line + "\n"
            var lines = logView.text.split("\n")
            if (lines.length > 400) logView.text = lines.slice(-400).join("\n")
        }
    }

    Connections {
        target: engineClient
        function onConnectedChanged() {
            if (engineClient.connected) modelPropsRefresh.restart()
        }
    }

    FileDialog {
        id: importDialog
        title: "Import Audio"
        nameFilters: ["Audio files (*.wav *.mp3 *.flac *.ogg *.m4a)", "All files (*)"]
        onAccepted: {
            for (var index = 0; index < selectedFiles.length; index++) {
                library.importFile(selectedFiles[index])
            }
        }
    }

    MediaPlayer {
        id: mediaPlayer
        audioOutput: AudioOutput {}
        onErrorOccurred: console.log("MediaPlayer error:", errorString)
    }

    Item {
        id: uiCanvas
        width: parent.width / root.uiScale
        height: parent.height / root.uiScale
        scale: root.uiScale
        transformOrigin: Item.TopLeft

    Rectangle {
        anchors.fill: parent
        color: root.bg
        z: -2
        clip: true
        Repeater {
            model: 24
            delegate: Rectangle {
                x: index * 72
                width: 1
                height: parent.height
                color: root.cyan
                opacity: 0.025
            }
        }
        Repeater {
            model: 14
            delegate: Rectangle {
                y: index * 72
                width: parent.width
                height: 1
                color: root.cyan
                opacity: 0.025
            }
        }
    }

    Rectangle {
        id: topBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 112
        color: root.surface
        border.color: root.line
        border.width: 1
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 4
            gradient: Gradient {
                GradientStop {
                    position: 0
                    color: root.chromeTop
                }
                GradientStop {
                    position: 1
                    color: root.chromeBottom
                }
            }
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.topMargin: 8
            anchors.bottomMargin: 8
            spacing: 5
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 54
                spacing: 12
                Rectangle {
                    width: 38
                    height: 38
                    radius: 3
                    color: root.coral
                    border.color: root.chromeLine
                    border.width: 1
                    Text {
                        anchors.centerIn: parent
                        text: "✦"
                        color: root.bg
                        font.pixelSize: 23
                        font.bold: true
                    }
                }
                Column {
                    spacing: 1
                    Label {
                        text: "POLARIS STUDIO"
                        color: root.ink
                        font.family: root.mono
                        font.pixelSize: 15
                        font.bold: true
                    }
                    Label {
                        text: "LOCAL AI MUSIC WORKSTATION / 0.1"
                        color: root.muted
                        font.family: root.mono
                        font.pixelSize: 9
                        font.letterSpacing: 0.8
                    }
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: "ENGINE"
                    color: root.muted
                    font.family: root.mono
                    font.pixelSize: 10
                    font.bold: true
                }
                Column {
                    width: 100
                    spacing: 3
                    Layout.alignment: Qt.AlignVCenter
                    Rectangle {
                        id: engineBadge
                        width: parent.width
                        height: 28
                        radius: 2
                        color: root.serverReady ? "#17382e" : (root.serverBooting ? "#3b2d18" : "#3b2027")
                        border.color: root.serverReady ? root.lime : (root.serverBooting ? root.bootAmber : root.stopRed)
                        Rectangle {
                            anchors.fill: parent
                            radius: parent.radius
                            color: root.bootAmber
                            opacity: root.serverBooting ? 0.08 + root.bootPulse * 0.18 : 0
                        }
                        Row {
                            anchors.centerIn: parent
                            spacing: 7
                            z: 1
                            Rectangle {
                                width: 7
                                height: 7
                                radius: 4
                                anchors.verticalCenter: parent.verticalCenter
                                color: root.serverReady ? root.lime : (root.serverBooting ? root.bootAmber : root.stopRed)
                            }
                            Label {
                                text: root.serverReady ? "RUNNING" : (root.serverBooting ? "BOOTING" : "STOPPED")
                                color: root.serverReady ? root.lime : (root.serverBooting ? root.bootAmber : root.stopRed)
                                font.family: root.mono
                                font.pixelSize: 10
                                font.bold: true
                            }
                        }
                    }
                    Rectangle {
                        width: parent.width
                        height: 4
                        radius: 2
                        color: root.surfaceRaised
                        border.color: root.line
                        clip: true
                        Rectangle {
                            width: root.serverReady ? parent.width : (root.serverBooting ? 30 : 0)
                            height: parent.height
                            x: root.serverBooting ? (parent.width - width) * root.bootPulse : 0
                            radius: 2
                            color: root.serverReady ? root.lime : (root.serverBooting ? root.bootAmber : root.stopRed)
                            Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                            Behavior on width { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                        }
                    }
                    Label {
                        width: parent.width
                        text: root.serverReady ? "IPC SOCKET / READY" : (root.serverBooting ? "IPC SOCKET / WAITING" : "IPC SOCKET / STOPPED")
                        color: root.serverReady ? root.lime : (root.serverBooting ? root.bootAmber : root.stopRed)
                        font.family: root.mono
                        font.pixelSize: 7
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }
                }
                RetroButton {
                    text: root.serverReady ? "STOP" : "START"
                    accent: root.serverReady ? root.coral : root.lime
                    enabled: !root.serverBooting
                    onClicked: root.serverReady ? engineProcess.stop() : engineProcess.start()
                }
            }
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: root.chromeLine
                opacity: 0.55
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                spacing: 4
                Label {
                    text: "TOOLS"
                    color: root.muted
                    font.family: root.mono
                    font.pixelSize: 9
                    font.bold: true
                    Layout.rightMargin: 4
                }
                RetroToolButton {
                    text: "＋"
                    accent: root.cyan
                    onClicked: importDialog.open()
                }
                RetroToolButton {
                    text: "▣"
                    accent: root.cyan
                    onClicked: importDialog.open()
                }
                RetroToolButton {
                    text: "▤"
                    accent: root.cyan
                }
                Rectangle {
                    width: 1
                    height: 24
                    color: root.chromeLine
                    opacity: 0.65
                    Layout.leftMargin: 3
                    Layout.rightMargin: 3
                }
                RetroToolButton {
                    text: "✂"
                    accent: root.violet
                }
                RetroToolButton {
                    text: "⧉"
                    accent: root.violet
                }
                RetroToolButton {
                    text: "×"
                    accent: root.coral
                }
                RetroToolButton {
                    text: "↶"
                    accent: root.violet
                }
                RetroToolButton {
                    text: "↷"
                    accent: root.violet
                }
                Rectangle {
                    width: 1
                    height: 24
                    color: root.chromeLine
                    opacity: 0.65
                    Layout.leftMargin: 3
                    Layout.rightMargin: 3
                }
                RetroToolButton {
                    text: "▶"
                    accent: root.lime
                    onClicked: root.doGenerate()
                }
                RetroToolButton {
                    text: "■"
                    accent: root.coral
                    onClicked: if (root.engineRunning) engineProcess.stop()
                }
                Label {
                    text: "FRAME"
                    color: root.muted
                    font.family: root.mono
                    font.pixelSize: 9
                    font.bold: true
                    Layout.leftMargin: 8
                }
                Rectangle {
                    width: 92
                    height: 26
                    color: root.bg
                    border.color: root.chromeLine
                    Text {
                        anchors.centerIn: parent
                        text: "01 / 01"
                        color: root.ink
                        font.family: root.mono
                        font.pixelSize: 10
                        font.bold: true
                    }
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: "TIMELINE  00:00:00"
                    color: root.muted
                    font.family: root.mono
                    font.pixelSize: 9
                }
            }
        }
    }

    RowLayout {
        anchors.top: topBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: 0

        Rectangle {
            id: navRail
            Layout.preferredWidth: 196
            Layout.fillHeight: true
            color: "#0e1628"
            border.color: root.line
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                Label { text: "MEDIA LIBRARY"
 color: root.muted
 font.family: root.mono
 font.pixelSize: 9
 font.bold: true
 leftPadding: 8
 Layout.topMargin: 8 }
                Repeater {
                    model: root.navItems
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        height: 52
                        radius: 7
                        color: root.currentView === index ? root.surfaceHot : "transparent"
                        border.color: root.currentView === index ? root.line : "transparent"
                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 10
                            spacing: 11
                            Text { text: modelData.icon
 color: root.currentView === index ? root.cyan : root.muted
 font.family: root.mono
 font.pixelSize: 18
 anchors.verticalCenter: parent.verticalCenter }
                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2
                                Label { text: modelData.label
 color: root.currentView === index ? root.ink : root.muted
 font.family: root.mono
 font.pixelSize: 11
 font.bold: true }
                                Label { text: modelData.hint
 color: root.currentView === index ? root.cyan : root.muted
 font.family: root.mono
 font.pixelSize: 9 }
                            }
                        }
                        Rectangle { visible: root.currentView === index
 width: 3
 height: 24
 radius: 2
 color: root.coral
 anchors.right: parent.right
 anchors.verticalCenter: parent.verticalCenter }
                        MouseArea { anchors.fill: parent
 onClicked: root.currentView = index
 cursorShape: Qt.PointingHandCursor }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Layout.topMargin: 7
                    Label {
                        text: "PLAYLISTS"
                        color: root.muted
                        font.family: root.mono
                        font.pixelSize: 9
                        font.bold: true
                        leftPadding: 8
                    }
                    Label {
                        text: "▸  REFERENCE TRACKS"
                        color: root.muted
                        font.family: root.mono
                        font.pixelSize: 9
                        leftPadding: 12
                    }
                    Label {
                        text: "▸  GENERATED"
                        color: root.muted
                        font.family: root.mono
                        font.pixelSize: 9
                        leftPadding: 12
                    }
                    Label {
                        text: "▸  FAVORITES"
                        color: root.muted
                        font.family: root.mono
                        font.pixelSize: 9
                        leftPadding: 12
                    }
                }
                Item { Layout.fillHeight: true }
                Rectangle {
                    Layout.fillWidth: true
 height: 84
 radius: 7
                    color: root.surface
                    border.color: root.line
                    Column {
                        anchors.fill: parent
 anchors.margins: 12
 spacing: 6
                        Label { text: "LOCAL NODE"
 color: root.muted
 font.family: root.mono
 font.pixelSize: 9
 font.bold: true }
                        Row { spacing: 7
 Rectangle { width: 7
 height: 7
 radius: 4
 color: root.lime
 anchors.verticalCenter: parent.verticalCenter }
 Label { text: "PRIVATE / OFFLINE"
 color: root.lime
 font.family: root.mono
 font.pixelSize: 10 } }
                        Label { text: "No cloud account required"
 color: root.muted
 font.family: root.mono
 font.pixelSize: 9 }
                    }
                }
                Label { text: "© 2026 POLARIS"
 color: root.muted
 opacity: 0.65
 font.family: root.mono
 font.pixelSize: 9
 Layout.leftMargin: 8
 Layout.bottomMargin: 6 }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                height: 82
                color: "transparent"
                Column {
                    anchors.left: parent.left
 anchors.leftMargin: 28
 anchors.verticalCenter: parent.verticalCenter
 spacing: 5
                    Label { text: "// " + root.viewSubtitles[root.currentView]
 color: root.cyan
 font.family: root.mono
 font.pixelSize: 10
 font.bold: true
 font.letterSpacing: 0.6 }
                    Label { text: root.viewTitles[root.currentView]
 color: root.ink
 font.family: root.mono
 font.pixelSize: 25
 font.bold: true }
                }
                Label { anchors.right: parent.right
 anchors.rightMargin: 28
 anchors.verticalCenter: parent.verticalCenter
 text: "LOCAL  /  48kHz  /  STEREO"
 color: root.muted
 font.family: root.mono
 font.pixelSize: 9 }
            }

            StackLayout {
                id: pages
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: root.currentView

                // Studio / generation deck
                Item {
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 28
 anchors.rightMargin: 28
 anchors.bottomMargin: 24
                        spacing: 16
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 330
                            Layout.minimumHeight: 300
                            Layout.fillHeight: true
                            spacing: 16
                            Rectangle {
                                Layout.fillWidth: true
 Layout.fillHeight: true
 Layout.preferredWidth: 2
                                color: root.surface
 radius: 9
 border.color: root.line
                                clip: true
                                ColumnLayout {
                                    anchors.fill: parent
 anchors.margins: 20
 spacing: 13
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label { text: "01 / COMPOSE"
 color: root.coral
 font.family: root.mono
 font.pixelSize: 11
 font.bold: true }
                                        Item { Layout.fillWidth: true }
                                        Label { text: root.synthBusy ? "● RENDERING" : "● STANDBY"
 color: root.synthBusy ? root.coral : root.lime
 font.family: root.mono
 font.pixelSize: 10
 font.bold: true }
                                    }
                                    Rectangle { Layout.fillWidth: true
 height: 1
 color: root.line }
                                    Label { text: "Describe the next signal"
 color: root.ink
 font.family: root.mono
 font.pixelSize: 18
 font.bold: true }
                                    RetroField { id: captionField
 Layout.fillWidth: true
 placeholderText: "e.g. dusty breakbeat, glassy synths, midnight drive..." }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label { text: "LM"
 color: root.muted
 font.family: root.mono
 font.pixelSize: 10
 font.bold: true
 Layout.preferredWidth: 34 }
                                        RetroCombo { id: lmModelCombo
 Layout.fillWidth: true
 model: (root.props.models && root.props.models.lm) || []
 enabled: root.engineRunning }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label { text: "DiT"
 color: root.muted
 font.family: root.mono
 font.pixelSize: 10
 font.bold: true
 Layout.preferredWidth: 34 }
                                        RetroCombo { id: ditModelCombo
 Layout.fillWidth: true
 model: (root.props.models && root.props.models.dit) || []
 enabled: root.engineRunning }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label { text: "VAE"
 color: root.muted
 font.family: root.mono
 font.pixelSize: 10
 font.bold: true
 Layout.preferredWidth: 34 }
                                        RetroCombo { id: vaeModelCombo
 Layout.fillWidth: true
 model: (root.props.models && root.props.models.vae) || []
 enabled: root.engineRunning }
                                    }
                                    Item { Layout.fillHeight: true }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label { text: "FORMAT"
 color: root.muted
 font.family: root.mono
 font.pixelSize: 10 }
                                        RetroCombo { id: formatCombo
 model: ["wav16", "mp3"]
 currentIndex: 0
 Layout.preferredWidth: 110 }
                                        Label { text: "STEPS"
 color: root.muted
 font.family: root.mono
 font.pixelSize: 10
 Layout.leftMargin: 8 }
                                        RetroSpin { id: stepsSpin
 from: 1
 to: 100
 value: 8
 Layout.preferredWidth: 76 }
                                        Item { Layout.fillWidth: true }
                                        RetroButton { text: root.synthBusy ? "RENDERING…" : "▶  GENERATE"
 accent: root.coral
 enabled: !root.synthBusy && captionField.text.length > 0 && root.engineRunning
 onClicked: root.doGenerate() }
                                    }
                                    Label { text: root.synthStatus
 visible: root.synthStatus !== ""
 color: root.synthBusy ? root.coral : root.lime
 font.family: root.mono
 font.pixelSize: 10
 elide: Text.ElideRight
 Layout.fillWidth: true }
                                }
                            }
                            Rectangle {
                                Layout.preferredWidth: 360
 Layout.fillHeight: true
                                color: root.surface
 radius: 9
 border.color: root.line
                                ColumnLayout {
                                    anchors.fill: parent
 anchors.margins: 18
 spacing: 12
                                    Label { text: "02 / SIGNAL"
 color: root.violet
 font.family: root.mono
 font.pixelSize: 11
 font.bold: true }
                                    Rectangle { Layout.fillWidth: true
 height: 1
 color: root.line }
                                    Label { text: "OUTPUT MONITOR"
 color: root.muted
 font.family: root.mono
 font.pixelSize: 9
 font.bold: true }
                                    Item {
                                        Layout.fillWidth: true
 height: 96
                                        Row {
                                            anchors.bottom: parent.bottom
 anchors.horizontalCenter: parent.horizontalCenter
 spacing: 3
                                            Repeater {
                                                model:  thirtyTwoBars
                                                delegate: Rectangle { width: 4
 height: 13 + ((index * 19) % 66)
 radius: 2
 color: index % 5 === 0 ? root.coral : root.cyan
 opacity: 0.82 }
                                            }
                                        }
                                    }
                                    Label { text: root.synthBusy ? "PROCESSING INPUT BUFFER" : "NO ACTIVE RENDER"
 color: root.muted
 font.family: root.mono
 font.pixelSize: 10
 Layout.fillWidth: true
 elide: Text.ElideRight }
                                    Item { Layout.fillHeight: true }
                                    Label { text: "RESOURCE GAUGES  /  " + systemMonitor.refreshInterval + " MS"
 color: root.muted
 font.family: root.mono
 font.pixelSize: 9
 font.bold: true
 Layout.fillWidth: true }
                                    GaugePair {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 166
                                        groupLabel: "SYSTEM / HOST"
                                        firstLabel: "CPU UTIL"
                                        firstValue: root.cpuGaugeValue()
                                        firstDetail: systemMonitor.cpuAvailable ? systemMonitor.cpuCores + " CORES" : "NO DATA"
                                        firstAccent: root.cyan
                                        secondLabel: "SYSTEM RAM"
                                        secondValue: root.memoryGaugeValue()
                                        secondDetail: root.ramGaugeDetail()
                                        secondAccent: root.lime
                                    }
                                    GaugePair {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 166
                                        groupLabel: "GRAPHICS / DEVICE"
                                        firstLabel: "GPU UTIL"
                                        firstValue: root.gpuGaugeValue()
                                        firstDetail: systemMonitor.gpuAvailable ? systemMonitor.gpuName : "NO DATA"
                                        firstAccent: root.coral
                                        secondLabel: "GPU VRAM"
                                        secondValue: root.vramGaugeValue()
                                        secondDetail: root.vramGaugeDetail()
                                        secondAccent: root.violet
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
 Layout.preferredHeight: root.showSummaryCards ? 102 : 0
 visible: root.showSummaryCards
 spacing: 12
                            Rectangle { Layout.fillWidth: true
 Layout.fillHeight: true
 color: root.surface
 radius: 8
 border.color: root.line
                                Column { anchors.fill: parent
 anchors.margins: 14
 spacing: 6
 Label { text: "LIBRARY"
 color: root.muted
 font.family: root.mono
 font.pixelSize: 9
 font.bold: true }
 Label { text: songs.length + " TRACKS"
 color: root.ink
 font.family: root.mono
 font.pixelSize: 19
 font.bold: true }
 Label { text: "local audio index"
 color: root.cyan
 font.family: root.mono
 font.pixelSize: 9 } }
                            }
                            Rectangle { Layout.fillWidth: true
 Layout.fillHeight: true
 color: root.surface
 radius: 8
 border.color: root.line
                                Column { anchors.fill: parent
 anchors.margins: 14
 spacing: 6
 Label { text: "MODEL SET"
 color: root.muted
 font.family: root.mono
 font.pixelSize: 9
 font.bold: true }
 Label { text: modelCount("lm") + modelCount("dit") + modelCount("vae") + " LOADED"
 color: root.ink
 font.family: root.mono
 font.pixelSize: 19
 font.bold: true }
 Label { text: "gguf / local weights"
 color: root.violet
 font.family: root.mono
 font.pixelSize: 9 } }
                            }
                            Rectangle { Layout.fillWidth: true
 Layout.fillHeight: true
 color: root.surface
 radius: 8
 border.color: root.line
                                Column { anchors.fill: parent
 anchors.margins: 14
 spacing: 6
 Label { text: "POWER MODE"
 color: root.muted
 font.family: root.mono
 font.pixelSize: 9
 font.bold: true }
 Label { text: "VULKAN / GPU"
 color: root.ink
 font.family: root.mono
 font.pixelSize: 19
 font.bold: true }
 Label { text: "private acceleration"
 color: root.lime
 font.family: root.mono
 font.pixelSize: 9 } }
                            }
                        }
                    }
                }

                // Library
                Item {
                    ColumnLayout { anchors.fill: parent
 anchors.leftMargin: 28
 anchors.rightMargin: 28
 anchors.bottomMargin: 24
 spacing: 14
                        RowLayout { Layout.fillWidth: true
 RetroButton { text: "+  IMPORT AUDIO"
 accent: root.cyan
 onClicked: importDialog.open() }
 RetroButton { text: "↻  REFRESH"
 accent: root.violet
 onClicked: { refreshSongs()
 library.refresh() } }
 Item { Layout.fillWidth: true }
 Label { text: songs.length + " TRACKS"
 color: root.muted
 font.family: root.mono
 font.pixelSize: 10 } }
                        Rectangle { Layout.fillWidth: true
 Layout.fillHeight: true
 color: root.surface
 radius: 9
 border.color: root.line
                            ColumnLayout { anchors.fill: parent
 anchors.margins: 16
 spacing: 0
                                RowLayout { Layout.fillWidth: true
 height: 28
 Label { text: "PLAY"
 color: root.muted
 font.family: root.mono
 font.pixelSize: 9
 Layout.preferredWidth: 48 }
 Label { text: "TRACK"
 color: root.muted
 font.family: root.mono
 font.pixelSize: 9
 Layout.fillWidth: true }
 Label { text: "TYPE"
 color: root.muted
 font.family: root.mono
 font.pixelSize: 9
 Layout.preferredWidth: 90 }
 Label { text: "SIZE"
 color: root.muted
 font.family: root.mono
 font.pixelSize: 9
 Layout.preferredWidth: 80 }
 Label { text: "STATE"
 color: root.muted
 font.family: root.mono
 font.pixelSize: 9
 Layout.preferredWidth: 90 } }
                                Rectangle { Layout.fillWidth: true
 height: 1
 color: root.line }
                                ListView { id: songList
 Layout.fillWidth: true
 Layout.fillHeight: true
 clip: true
 model: root.songs
 delegate: Rectangle { width: songList.width
 height: 46
 color: root.currentSongId === modelData.id ? root.surfaceHot : (index % 2 ? "#121c31" : "transparent")
 RowLayout { anchors.fill: parent
 anchors.leftMargin: 2
 anchors.rightMargin: 2
 spacing: 8
 RetroButton { text: mediaPlayer.playbackState === MediaPlayer.PlayingState && root.currentSongId === modelData.id ? "Ⅱ" : "▶"
 accent: root.coral
 implicitWidth: 42
 onClicked: { var filePath = library.fullPath(modelData.relativePath)
 if (root.currentSongId === modelData.id && mediaPlayer.playbackState === MediaPlayer.PlayingState) mediaPlayer.pause()
 else { root.currentSongId = modelData.id
 mediaPlayer.source = "file://" + filePath
 mediaPlayer.play() } } }
 Label { text: modelData.relativePath.split("/").pop()
 color: root.ink
 font.family: root.mono
 font.pixelSize: 11
 Layout.fillWidth: true
 elide: Text.ElideRight }
 Label { text: modelData.kind || "—"
 color: modelData.kind === "reference" ? root.cyan : root.lime
 font.family: root.mono
 font.pixelSize: 10
 Layout.preferredWidth: 90 }
 Label { text: root.formatBytes(modelData.size)
 color: root.muted
 font.family: root.mono
 font.pixelSize: 10
 Layout.preferredWidth: 80 }
 Label { text: modelData.analysisState || "pending"
 color: modelData.analysisState === "ready" ? root.lime : root.muted
 font.family: root.mono
 font.pixelSize: 10
 Layout.preferredWidth: 90 }
 Button { text: "×"
 flat: true
 onClicked: { library.removeSong(modelData.id)
 root.refreshSongs() } } } } }
                            }
                        }
                        Rectangle { Layout.fillWidth: true
 height: 48
 visible: root.currentSongId >= 0
 color: root.surfaceRaised
 radius: 6
 border.color: root.line
 RowLayout { anchors.fill: parent
 anchors.margins: 10
 Label { text: "NOW PLAYING"
 color: root.coral
 font.family: root.mono
 font.pixelSize: 10
 font.bold: true }
 Slider { Layout.fillWidth: true
 from: 0
 to: mediaPlayer.duration || 1
 value: mediaPlayer.position
 onMoved: mediaPlayer.position = value }
 Label { text: root.formatDuration(mediaPlayer.position / 1000) + " / " + root.formatDuration(mediaPlayer.duration / 1000)
 color: root.muted
 font.family: root.mono
 font.pixelSize: 10 } } }
                    }
                }

                // Monitor
                Item {
                    ColumnLayout { anchors.fill: parent
 anchors.leftMargin: 28
 anchors.rightMargin: 28
 anchors.bottomMargin: 24
 spacing: 14
                        RowLayout { Layout.fillWidth: true
 Layout.preferredHeight: 190
 spacing: 12
                            GaugePair {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                groupLabel: "SYSTEM / HOST"
                                firstLabel: "CPU UTIL"
                                firstValue: root.cpuGaugeValue()
                                firstDetail: systemMonitor.cpuAvailable ? systemMonitor.cpuCores + " CORES" : "NO DATA"
                                firstAccent: root.cyan
                                secondLabel: "SYSTEM RAM"
                                secondValue: root.memoryGaugeValue()
                                secondDetail: root.ramGaugeDetail()
                                secondAccent: root.lime
                            }
                            GaugePair {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                groupLabel: "GRAPHICS / DEVICE"
                                firstLabel: "GPU UTIL"
                                firstValue: root.gpuGaugeValue()
                                firstDetail: systemMonitor.gpuAvailable ? systemMonitor.gpuName : "NO DATA"
                                firstAccent: root.coral
                                secondLabel: "GPU VRAM"
                                secondValue: root.vramGaugeValue()
                                secondDetail: root.vramGaugeDetail()
                                secondAccent: root.violet
                            }
                            Rectangle { Layout.fillWidth: true
 Layout.fillHeight: true
 color: root.surface
 radius: 8
 border.color: root.line
 Column { anchors.fill: parent
 anchors.margins: 16
 spacing: 10
 Label { text: "BACKEND"
 color: root.muted
 font.family: root.mono
 font.pixelSize: 10
 font.bold: true }
 Label { text: systemMonitor.gpuAvailable ? systemMonitor.gpuName : "Vulkan / local"
 color: root.ink
 font.family: root.mono
 font.pixelSize: 15
 font.bold: true
 elide: Text.ElideRight }
 Label { text: root.engineRunning ? "READY TO PROCESS" : "ENGINE OFFLINE"
 color: root.engineRunning ? root.lime : root.violet
 font.family: root.mono
 font.pixelSize: 10 } } }
                        }
                        Rectangle { Layout.fillWidth: true
 Layout.fillHeight: true
 color: root.surface
 radius: 8
 border.color: root.line
 ColumnLayout { anchors.fill: parent
 anchors.margins: 16
 spacing: 10
 RowLayout { Layout.fillWidth: true
 Label { text: "ENGINE LOG / LIVE"
 color: root.cyan
 font.family: root.mono
 font.pixelSize: 10
 font.bold: true }
 Item { Layout.fillWidth: true }
 Label { text: "tail -f polaris"
 color: root.muted
 font.family: root.mono
 font.pixelSize: 10 } }
 Rectangle { Layout.fillWidth: true
 height: 1
 color: root.line }
 ScrollView { Layout.fillWidth: true
 Layout.fillHeight: true
 clip: true
 TextArea { id: logView
 readOnly: true
 color: root.lime
 selectionColor: root.surfaceHot
 font.family: root.mono
 font.pixelSize: 10
 background: Rectangle { color: "transparent" }
 placeholderText: "Waiting for engine output…"
 placeholderTextColor: root.muted } } } }
                    }
                }

                // Models
                Item {
                    ColumnLayout { anchors.fill: parent
 anchors.leftMargin: 28
 anchors.rightMargin: 28
 anchors.bottomMargin: 24
 spacing: 14
                        Rectangle { Layout.fillWidth: true
 height: 72
 visible: !root.engineRunning && root.modelsDownloaded <= 0 && downloader.localModelFiles.length === 0
 color: "#2b2036"
 radius: 7
 border.color: root.violet
 Row { anchors.fill: parent
 anchors.margins: 14
 spacing: 12
 Text { text: "!"
 color: root.violet
 font.family: root.mono
 font.pixelSize: 24
 font.bold: true
 anchors.verticalCenter: parent.verticalCenter }
 Column { anchors.verticalCenter: parent.verticalCenter
 spacing: 4
 Label { text: "MODEL VAULT IS EMPTY"
 color: root.ink
 font.family: root.mono
 font.pixelSize: 11
 font.bold: true }
 Label { text: "Download the default GGUF set to boot the local engine."
 color: root.muted
 font.family: root.mono
 font.pixelSize: 10 } } } }
                        RowLayout { Layout.fillWidth: true
 Layout.preferredHeight: 132
 Layout.minimumHeight: 132
 Layout.maximumHeight: 132
 spacing: 12
                            Rectangle { Layout.fillWidth: true
 height: 132
 color: root.surface
 radius: 8
 border.color: root.line
 Column { anchors.fill: parent
 anchors.margins: 16
 spacing: 8
 Label { text: "STORAGE PATH"
 color: root.muted
 font.family: root.mono
 font.pixelSize: 10
 font.bold: true }
 Label { text: engineProcess.modelsDir || "Not set"
 color: root.ink
 font.family: root.mono
 font.pixelSize: 11
 elide: Text.ElideMiddle
 width: parent.width }
 RetroButton { text: "CHOOSE FOLDER"
 accent: root.cyan
 onClicked: engineProcess.chooseModelsDir() } } }
                            Rectangle { Layout.fillWidth: true
 height: 132
 color: root.surface
 radius: 8
 border.color: root.line
 Column { anchors.fill: parent
 anchors.margins: 16
 spacing: 8
 Label { text: "MODEL DOWNLOAD"
 color: root.muted
 font.family: root.mono
 font.pixelSize: 10
 font.bold: true }
 Label { text: downloader.busy ? downloader.currentFile : "DEFAULT SET / ~9 GB"
 color: root.ink
 font.family: root.mono
 font.pixelSize: 11
 elide: Text.ElideMiddle
 width: parent.width }
 RetroButton { text: downloader.busy ? "CANCEL" : "↓  DOWNLOAD"
 accent: root.coral
 onClicked: downloader.busy ? downloader.cancel() : downloader.downloadDefaults(engineProcess.modelsDir) } } }
                        }
                        Rectangle { Layout.fillWidth: true
 Layout.fillHeight: true
 Layout.minimumHeight: 220
 color: root.surface
 radius: 8
 border.color: root.line
 ColumnLayout { anchors.fill: parent
 anchors.margins: 16
 spacing: 12
                            RowLayout { Layout.fillWidth: true
 Label { text: "SCANNED WEIGHTS"
 color: root.cyan
 font.family: root.mono
 font.pixelSize: 10
 font.bold: true }
 Item { Layout.fillWidth: true }
 RetroButton { text: "↻  REFRESH LIST"
 accent: root.cyan
 enabled: !downloader.busy
 implicitHeight: 30
 onClicked: root.refreshModelVault() }
 Label { text: modelCount("lm") + modelCount("dit") + modelCount("embedding") + modelCount("vae") + " FILES"
 color: root.muted
 font.family: root.mono
 font.pixelSize: 10 } }
 Rectangle { Layout.fillWidth: true
 height: 1
 color: root.line }
 Repeater { model: [{label: "LM", key: "lm"}, {label: "TEXT ENC", key: "embedding"}, {label: "DiT", key: "dit"}, {label: "VAE", key: "vae"}]
 delegate: RowLayout { Layout.fillWidth: true
 Label { text: modelData.label
 color: root.muted
 font.family: root.mono
 font.pixelSize: 10
 Layout.preferredWidth: 90 }
 Label { text: root.displayedModels(modelData.key).join(", ") || "none detected"
 color: root.ink
 font.family: root.mono
 font.pixelSize: 10
 Layout.fillWidth: true
 elide: Text.ElideMiddle } } } } }
                        Label { text: root.modelsDownloaded > 0 ? root.modelsDownloaded + " MODEL(S) DOWNLOADED — RESTART ENGINE"
                            : (downloader.localModelFiles.length > 0 ? downloader.localModelFiles.length + " LOCAL GGUF FILE(S) FOUND — ENGINE BOOTING"
                                                                       : "Models stay on this machine and are never uploaded.")
 color: root.modelsDownloaded > 0 ? root.lime : root.muted
 font.family: root.mono
 font.pixelSize: 10 }
                        }
                    }
                }

                // Pixel Lab / local image asset workshop
                Item {
                    ColumnLayout { anchors.fill: parent
                        anchors.leftMargin: 28
                        anchors.rightMargin: 28
                        anchors.bottomMargin: 24
                        spacing: 14

                        RowLayout { Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredHeight: 420
                            spacing: 14

                            Rectangle { Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.preferredWidth: 1.2
                                color: root.surface
                                radius: 8
                                border.color: root.line
                                clip: true
                                ColumnLayout { anchors.fill: parent
                                    anchors.margins: 18
                                    spacing: 11
                                    RowLayout { Layout.fillWidth: true
                                        Label { text: "01 / ASSET RECIPE"
                                            color: root.coral
                                            font.family: root.mono
                                            font.pixelSize: 11
                                            font.bold: true }
                                        Item { Layout.fillWidth: true }
                                        Label { text: root.pixelProfileStateLabel()
                                            color: downloader.imageProfileStatus === "installed" ? root.lime : root.violet
                                            font.family: root.mono
                                            font.pixelSize: 9
                                            font.bold: true }
                                    }
                                    Rectangle { Layout.fillWidth: true
                                        height: 1
                                        color: root.line }
                                    Label { text: "Generate a game-ready image asset"
                                        color: root.ink
                                        font.family: root.mono
                                        font.pixelSize: 18
                                        font.bold: true }

                                    RowLayout { Layout.fillWidth: true
                                        Label { text: "ASSET"
                                            color: root.muted
                                            font.family: root.mono
                                            font.pixelSize: 10
                                            font.bold: true
                                            Layout.preferredWidth: 70 }
                                        RetroCombo { id: pixelAssetCombo
                                            Layout.fillWidth: true
                                            model: ["Terrain Tile", "Decoration", "Unit", "Building", "Icon"]
                                            currentIndex: 0
                                            onActivated: root.pixelAssetType = currentText }
                                    }

                                    Label { text: "PROMPT"
                                        color: root.muted
                                        font.family: root.mono
                                        font.pixelSize: 10
                                        font.bold: true }
                                    TextArea { id: pixelPromptField
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 82
                                        color: root.ink
                                        placeholderText: "e.g. mossy isometric stone ruin, readable silhouette, 32px game asset..."
                                        placeholderTextColor: root.muted
                                        wrapMode: TextArea.Wrap
                                        font.family: root.mono
                                        font.pixelSize: 11
                                        leftPadding: 12
                                        rightPadding: 12
                                        topPadding: 10
                                        bottomPadding: 10
                                        background: Rectangle { radius: 5
                                            color: root.bg
                                            border.color: pixelPromptField.activeFocus ? root.cyan : root.line
                                            border.width: pixelPromptField.activeFocus ? 2 : 1 }
                                    }

                                    RowLayout { Layout.fillWidth: true
                                        Label { text: "MODEL"
                                            color: root.muted
                                            font.family: root.mono
                                            font.pixelSize: 10
                                            font.bold: true
                                            Layout.preferredWidth: 70 }
                                        RetroCombo { id: pixelProfileCombo
                                            Layout.fillWidth: true
                                            textRole: "label"
                                            model: downloader.imageProfiles
                                            currentIndex: root.pixelProfileIndex()
                                            onActivated: {
                                                var selected = model[index]
                                                if (selected) {
                                                    root.pixelProfileId = selected.id
                                                    downloader.selectImageProfile(selected.id, engineProcess.modelsDir)
                                                }
                                            }
                                        }
                                    }
                                    RowLayout { Layout.fillWidth: true
                                        Label { text: "SIZE"
                                            color: root.muted
                                            font.family: root.mono
                                            font.pixelSize: 10
                                            font.bold: true
                                            Layout.preferredWidth: 70 }
                                        RetroCombo { id: pixelSizeCombo
                                            Layout.fillWidth: true
                                            model: ["512 × 512", "768 × 768", "1024 × 1024", "768 × 512", "512 × 768"]
                                            currentIndex: 0
                                            onActivated: {
                                                var dimensions = currentText.split(" × ")
                                                root.pixelWidth = Number(dimensions[0])
                                                root.pixelHeight = Number(dimensions[1])
                                            }
                                        }
                                        Label { text: "STEPS"
                                            color: root.muted
                                            font.family: root.mono
                                            font.pixelSize: 9
                                            Layout.leftMargin: 8 }
                                        RetroSpin { id: pixelStepsSpin
                                            from: 1
                                            to: 100
                                            value: root.pixelSteps
                                            Layout.preferredWidth: 66
                                            onValueChanged: root.pixelSteps = value }
                                    }
                                    RowLayout { Layout.fillWidth: true
                                        Label { text: "SEED"
                                            color: root.muted
                                            font.family: root.mono
                                            font.pixelSize: 9
                                            Layout.preferredWidth: 70 }
                                        RetroSpin { id: pixelSeedSpin
                                            from: -1
                                            to: 2147483647
                                            value: root.pixelSeed
                                            Layout.preferredWidth: 110
                                            onValueChanged: root.pixelSeed = value }
                                        Label { text: "−1 = RANDOM"
                                            color: root.muted
                                            font.family: root.mono
                                            font.pixelSize: 8 }
                                    }
                                    Label { Layout.fillWidth: true
                                        text: pixelProfileCombo.currentIndex >= 0 && pixelProfileCombo.currentIndex < downloader.imageProfiles.length
                                              ? downloader.imageProfiles[pixelProfileCombo.currentIndex].description
                                              : "Choose a local image model profile."
                                        color: root.cyan
                                        font.family: root.mono
                                        font.pixelSize: 9
                                        wrapMode: Text.WordWrap }

                                    RowLayout { Layout.fillWidth: true
                                        Label { text: "INSTALL"
                                            color: root.muted
                                            font.family: root.mono
                                            font.pixelSize: 9 }
                                        RetroProgress { Layout.fillWidth: true
                                            value: downloader.busy && downloader.currentTotal > 0
                                                    ? downloader.currentReceived / downloader.currentTotal : 0 }
                                        Label { text: downloader.busy ? downloader.completedFiles + "/" + downloader.totalFiles : "LOCAL"
                                            color: root.muted
                                            font.family: root.mono
                                            font.pixelSize: 9 }
                                    }
                                    RowLayout { Layout.fillWidth: true
                                        Label { Layout.fillWidth: true
                                            text: downloader.imageProfileMessage
                                            color: downloader.imageProfileStatus === "error" ? root.coral : root.muted
                                            font.family: root.mono
                                            font.pixelSize: 9
                                            elide: Text.ElideRight }
                                        RetroButton { text: downloader.busy ? "CANCEL" : (downloader.imageProfileStatus === "installed" ? "READY" : "↓  INSTALL")
                                            accent: downloader.busy ? root.coral : root.lime
                                            enabled: downloader.busy || downloader.imageProfileStatus !== "installed"
                                            onClicked: downloader.busy
                                                       ? downloader.cancel()
                                                       : downloader.selectImageProfile(root.pixelProfileId, engineProcess.modelsDir) }
                                    }

                                    Item { Layout.fillHeight: true }
                                    Rectangle { Layout.fillWidth: true
                                        height: 1
                                        color: root.line }
                                    RowLayout { Layout.fillWidth: true
                                        Label { text: "SIZE"
                                            color: root.muted
                                            font.family: root.mono
                                            font.pixelSize: 9 }
                                        Label { text: pixelProfileCombo.currentIndex >= 0 && pixelProfileCombo.currentIndex < downloader.imageProfiles.length
                                                      ? downloader.imageProfiles[pixelProfileCombo.currentIndex].size : "—"
                                            color: root.ink
                                            font.family: root.mono
                                            font.pixelSize: 10 }
                                        Label { text: "SOURCE"
                                            color: root.muted
                                            font.family: root.mono
                                            font.pixelSize: 9
                                            Layout.leftMargin: 12 }
                                        Label { text: pixelProfileCombo.currentIndex >= 0 && pixelProfileCombo.currentIndex < downloader.imageProfiles.length
                                                      ? downloader.imageProfiles[pixelProfileCombo.currentIndex].source : "—"
                                            color: root.ink
                                            font.family: root.mono
                                            font.pixelSize: 9
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true }
                                    }
                                }
                            }

                            Rectangle { Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.preferredWidth: 1.4
                                color: root.surface
                                radius: 8
                                border.color: root.line
                                ColumnLayout { anchors.fill: parent
                                    anchors.margins: 18
                                    spacing: 10
                                    RowLayout { Layout.fillWidth: true
                                        Label { text: "02 / CANVAS"
                                            color: root.violet
                                            font.family: root.mono
                                            font.pixelSize: 11
                                            font.bold: true }
                                        Item { Layout.fillWidth: true }
                                        Label { text: "PNG / TRANSPARENT"
                                            color: root.muted
                                            font.family: root.mono
                                            font.pixelSize: 9 }
                                    }
                                    Rectangle { Layout.fillWidth: true
                                        height: 1
                                        color: root.line }
                                    Rectangle { Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        color: root.bg
                                        border.color: root.chromeLine
                                        border.width: 1
                                        clip: true
                                        Image { anchors.fill: parent
                                            anchors.margins: 14
                                            source: imageEngine.outputRevision > 0
                                                    ? "file://" + imageEngine.outputPath : ""
                                            visible: imageEngine.outputRevision > 0
                                            fillMode: Image.PreserveAspectFit
                                            smooth: false
                                            mipmap: false }
                                        Repeater { model: 12
                                            delegate: Rectangle { x: index * 48
                                                width: 1
                                                height: parent.height
                                                color: root.cyan
                                                opacity: 0.07 }
                                        }
                                        Repeater { model: 10
                                            delegate: Rectangle { y: index * 48
                                                width: parent.width
                                                height: 1
                                                color: root.cyan
                                                opacity: 0.07 }
                                        }
                                        Column { anchors.centerIn: parent
                                            visible: imageEngine.outputRevision <= 0
                                            spacing: 9
                                            Label { anchors.horizontalCenter: parent.horizontalCenter
                                                text: "▧"
                                                color: root.chromeLine
                                                font.family: root.mono
                                                font.pixelSize: 42 }
                                            Label { anchors.horizontalCenter: parent.horizontalCenter
                                                text: downloader.imageProfileStatus === "installed"
                                                      ? (imageEngine.available ? "READY TO RENDER" : "INSTALL SD-CLI")
                                                      : "INSTALL A MODEL PROFILE"
                                                color: downloader.imageProfileStatus === "installed" && imageEngine.available ? root.lime : root.violet
                                                font.family: root.mono
                                                font.pixelSize: 11
                                                font.bold: true }
                                            Label { anchors.horizontalCenter: parent.horizontalCenter
                                                text: imageEngine.status
                                                color: root.muted
                                                font.family: root.mono
                                                font.pixelSize: 9 }
                                        }
                                    }
                                    RowLayout { Layout.fillWidth: true
                                        RetroButton { text: "＋  ADD REFERENCE"
                                            accent: root.cyan
                                            enabled: false }
                                        Item { Layout.fillWidth: true }
                                        RetroButton { text: imageEngine.busy ? "■  CANCEL RENDER" : (imageEngine.available ? "▶  GENERATE IMAGE" : "INSTALL SD-CLI")
                                            accent: imageEngine.busy ? root.coral : root.coral
                                            enabled: imageEngine.busy || (imageEngine.available
                                                     && downloader.imageProfileStatus === "installed"
                                                     && pixelPromptField.text.trim().length > 0)
                                            onClicked: imageEngine.busy
                                                       ? imageEngine.cancel()
                                                       : imageEngine.generate(root.pixelProfileId,
                                                                               engineProcess.modelsDir,
                                                                               pixelPromptField.text,
                                                                               root.pixelNegativePrompt,
                                                                               root.pixelAssetType,
                                                                               root.pixelWidth,
                                                                               root.pixelHeight,
                                                                               root.pixelSteps,
                                                                               root.pixelSeed) }
                                    }
                                }
                            }

                            Rectangle { Layout.preferredWidth: 350
                                Layout.fillHeight: true
                                color: root.surface
                                radius: 8
                                border.color: root.line
                                ColumnLayout { anchors.fill: parent
                                    anchors.margins: 16
                                    spacing: 11
                                    Label { text: "03 / SYSTEM"
                                        color: root.cyan
                                        font.family: root.mono
                                        font.pixelSize: 11
                                        font.bold: true }
                                    Rectangle { Layout.fillWidth: true
                                        height: 1
                                        color: root.line }
                                    Label { text: "RESOURCE GAUGES  /  " + systemMonitor.refreshInterval + " MS"
                                        color: root.muted
                                        font.family: root.mono
                                        font.pixelSize: 9
                                        font.bold: true }
                                    GaugePair {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 166
                                        groupLabel: "SYSTEM / HOST"
                                        firstLabel: "CPU UTIL"
                                        firstValue: root.cpuGaugeValue()
                                        firstDetail: systemMonitor.cpuAvailable ? systemMonitor.cpuCores + " CORES" : "NO DATA"
                                        firstAccent: root.cyan
                                        secondLabel: "SYSTEM RAM"
                                        secondValue: root.memoryGaugeValue()
                                        secondDetail: root.ramGaugeDetail()
                                        secondAccent: root.lime
                                    }
                                    GaugePair {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 166
                                        groupLabel: "GRAPHICS / DEVICE"
                                        firstLabel: "GPU UTIL"
                                        firstValue: root.gpuGaugeValue()
                                        firstDetail: systemMonitor.gpuAvailable ? systemMonitor.gpuName : "NO DATA"
                                        firstAccent: root.coral
                                        secondLabel: "GPU VRAM"
                                        secondValue: root.vramGaugeValue()
                                        secondDetail: root.vramGaugeDetail()
                                        secondAccent: root.violet
                                    }
                                    Item { Layout.fillHeight: true }
                                    Rectangle { Layout.fillWidth: true
                                        height: 100
                                        color: root.bg
                                        border.color: root.line
                                        Column { anchors.fill: parent
                                            anchors.margins: 10
                                            spacing: 6
                                            Label { text: "POST PROCESS"
                                                color: root.muted
                                                font.family: root.mono
                                                font.pixelSize: 9
                                                font.bold: true }
                                            Label { text: "NEAREST-NEIGHBOR"
                                                color: root.lime
                                                font.family: root.mono
                                                font.pixelSize: 10 }
                                            Label { text: "PALETTE / OUTLINE / ALPHA"
                                                color: root.cyan
                                                font.family: root.mono
                                                font.pixelSize: 9 }
                                            Label { text: imageEngine.state.toUpperCase() + " / " + (imageEngine.available ? "LOCAL RUNTIME" : "RUNTIME REQUIRED")
                                                color: imageEngine.available ? root.lime : root.violet
                                                font.family: root.mono
                                                font.pixelSize: 8 }
                                            Label { text: imageEngine.status
                                                color: root.muted
                                                font.family: root.mono
                                                font.pixelSize: 8
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true }
                                        }
                                    }
                                }
                            }
                        }

                        RowLayout { Layout.fillWidth: true
                            Layout.preferredHeight: root.showSummaryCards ? 100 : 0
                            visible: root.showSummaryCards
                            spacing: 12
                            Repeater { model: [
                                { label: "TERRAIN", value: "tile edges / 32px", accent: root.cyan },
                                { label: "DECORATION", value: "transparent props", accent: root.lime },
                                { label: "UNIT", value: "3/4 isometric", accent: root.coral },
                                { label: "BUILDING", value: "readable silhouette", accent: root.violet }
                            ]
                                delegate: Rectangle { Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    color: root.surface
                                    radius: 7
                                    border.color: root.line
                                    Column { anchors.fill: parent
                                        anchors.margins: 13
                                        spacing: 6
                                        Label { text: modelData.label
                                            color: root.muted
                                            font.family: root.mono
                                            font.pixelSize: 9
                                            font.bold: true }
                                        Label { text: modelData.value
                                            color: modelData.accent
                                            font.family: root.mono
                                            font.pixelSize: 12
                                            font.bold: true }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    property int thirtyTwoBars: 32

    Timer { interval: 3000
 running: root.engineRunning
 repeat: true
 onTriggered: root.refreshEngineProps() }
    Timer { id: modelPropsRefresh
 interval: 400
 repeat: false
 onTriggered: root.refreshEngineProps() }
    Timer { interval: 1500
 running: root.synthBusy
 repeat: true
 onTriggered: root.checkSynthJob() }

    Connections {
        target: library
        function onSongsChanged() { root.refreshSongs() }
        function onImportFinished(songId) { root.refreshSongs() }
    }
    Connections {
        target: downloader
        function onDownloadFinished(allOk) {
            if (allOk) {
                root.modelsDownloaded = downloader.totalFiles
                modelPropsRefresh.restart()
            }
        }
    }
}
