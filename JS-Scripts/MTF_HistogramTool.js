// =============================================================================
// MTF_HistogramTool.js
//
// Native-Replication MTF Tool for TStar.
//
// Features:
//   - Live Bin Transformation: Histogram bars stretch/squash in real-time.
//   - Dynamic Clipping Stats: Real-time update of Low/High clip percentages.
//   - Performance: Release-to-preview for image, real-time for histogram.
//   - Full History: Correct pushUndo order for persistence.
// =============================================================================

var view = App.activeWindow();
if (!view) {
    Console.error("MTF Tool: No image open.");
} else {

    // --- 1. Initial State & High-Res Baseline ---
    var img = view.image;
    var numBins = 1024;
    var originalHistData = img.computeHistogram(numBins);
    
    // Calculate total pixels for clipping stats
    var channelPixels = 0;
    if (originalHistData.length > 0) {
        for (var i = 0; i < originalHistData[0].length; i++) {
            channelPixels += originalHistData[0][i];
        }
    }

    var params = {
        shadows: 0.0,
        midtones: 0.5,
        highlights: 1.0,
        preview: true
    };

    // --- 2. Math & Logic ---

    function mtf(m, x) {
        if (x <= 0) return 0;
        if (x >= 1) return 1;
        if (m == 0.5) return x;
        return ((m - 1) * x) / ((2 * m - 1) * x - m);
    }

    /**
     * Replicates the C++ redistribute logic: maps original bins to new positions.
     */
    function updateHistogramWidget() {
        var lut = [];
        for (var i = 0; i < numBins; i++) {
            lut.push(mtf(params.midtones, (i / (numBins - 1) - params.shadows) / (params.highlights - params.shadows)));
        }

        var transformedBins = [];
        var lowClip = 0;
        var highClip = 0;

        for (var c = 0; c < originalHistData.length; c++) {
            var src = originalHistData[c];
            var dst = new Array(numBins).fill(0);
            
            for (var i = 0; i < numBins; i++) {
                var count = src[i];
                if (count === 0) continue;
                
                var outVal = lut[i];
                var outIdx = Math.round(outVal * (numBins - 1));
                if (outIdx < 0) outIdx = 0;
                if (outIdx >= numBins) outIdx = numBins - 1;
                
                dst[outIdx] += count;
            }
            transformedBins.push(dst);
            
            // Accumulate clipping stats for the 3 main channels (if relevant)
            if (c < 3) {
                lowClip += dst[0];
                highClip += dst[numBins - 1];
            }
        }

        // Push transformed data to widget
        hist.setData(transformedBins, img.channels);

        // Update clipping labels (TStar style)
        if (channelPixels > 0) {
            var divider = Math.min(originalHistData.length, 3) * channelPixels;
            var lowPct = (lowClip / divider * 100).toFixed(4);
            var highPct = (highClip / divider * 100).toFixed(4);
            blackClipLabel.text = "Low: " + lowPct + "%";
            whiteClipLabel.text = "High: " + highPct + "%";
        }
        
        // Update the overlay curve (orange)
        var curve = [];
        for (var i = 0; i < 256; i++) {
            curve.push(mtf(params.midtones, (i / 255.0 - params.shadows) / (params.highlights - params.shadows)));
        }
        hist.setCurve(curve);
    }

    function updateImagePreview() {
        if (params.preview) {
            try {
                var lut64k = [];
                for (var i = 0; i < 65536; i++) {
                    lut64k.push(mtf(params.midtones, (i / 65535.0 - params.shadows) / (params.highlights - params.shadows)));
                }
                var fullLut = [];
                for (var c = 0; c < img.channels; c++) fullLut.push(lut64k);
                view.setPreviewLUT(fullLut);
            } catch (e) {
                Console.warn("Preview failed: " + e);
            }
        } else {
            view.clearPreviewLUT();
        }
    }

    function applyOperation() {
        view.pushUndo("Histogram Transformation");

        var hs = new HistogramStretch();
        hs.shadows = params.shadows;
        hs.midtones = params.midtones;
        hs.highlights = params.highlights;
        hs.executeOn(img);

        view.refresh();
        view.clearPreviewLUT();

        // New baseline after application
        originalHistData = img.computeHistogram(numBins);
        hist.setGhostData(originalHistData, img.channels);
        resetParameters();
    }

    function resetParameters() {
        shadowsRow.updateValue(0.0);
        midtonesRow.updateValue(0.5);
        highlightsRow.updateValue(1.0);
        updateHistogramWidget();
        updateImagePreview();
    }

    // --- 3. UI Construction ---

    var d = new Dialog();
    d.windowTitle = "Histogram Transformation";
    d.width = 440; d.height = 540;

    var root = new VerticalSizer();
    root.margin = 10; root.spacing = 8;

    var hist = new ScriptHistogram();
    hist.minHeight = 220;
    hist.setGhostData(originalHistData, img.channels);
    root.add(hist, 1);

    var clipSizer = new HorizontalSizer();
    var blackClipLabel = new Label("Low: 0.0000%");
    blackClipLabel.style = "color: #ff8888;";
    var whiteClipLabel = new Label("High: 0.0000%");
    whiteClipLabel.style = "color: #8888ff;";
    clipSizer.add(blackClipLabel); clipSizer.addStretch(); clipSizer.add(whiteClipLabel);
    root.add(clipSizer);

    function makeRow(name, initialValue) {
        var row = new HorizontalSizer();
        row.spacing = 6;
        var lbl = new Label(name); lbl.minWidth = 70;

        var sl = new Slider();
        sl.min = 0; sl.max = 10000;

        var sp = new SpinBox();
        sp.min = 0.0; sp.max = 1.0; sp.step = 0.0001; sp.precision = 6;

        function syncValue(val, source) {
            if (name === "Shadows:") params.shadows = val;
            if (name === "Midtones:") params.midtones = val;
            if (name === "Highlights:") params.highlights = val;

            if (source !== "slider") sl.value = Math.round(val * 10000);
            if (source !== "spin") sp.value = val;

            updateHistogramWidget();
        }

        sl.onValueChanged = function (v) { syncValue(v / 10000.0, "slider"); };
        sl.onSliderReleased = function () { updateImagePreview(); };
        sp.onValueChanged = function (v) { syncValue(v, "spin"); updateImagePreview(); };

        row.add(lbl); row.add(sl, 1); row.add(sp);
        syncValue(initialValue);

        return {
            sizer: row,
            updateValue: function (v) { syncValue(v); }
        };
    }

    var shadowsRow = makeRow("Shadows:", 0.0);
    var midtonesRow = makeRow("Midtones:", 0.5);
    var highlightsRow = makeRow("Highlights:", 1.0);

    root.add(shadowsRow.sizer);
    root.add(midtonesRow.sizer);
    root.add(highlightsRow.sizer);

    var cbPreview = new CheckBox("Enable Preview");
    cbPreview.checked = true;
    cbPreview.onToggled = function (v) { params.preview = v; updateImagePreview(); };
    root.add(cbPreview);

    var btnRow = new HorizontalSizer();
    btnRow.spacing = 6;
    var btnReset = new PushButton("Reset"); btnReset.onClick = resetParameters;
    var btnApply = new PushButton("Apply"); btnApply.onClick = applyOperation;
    var btnCancel = new PushButton("Cancel"); btnCancel.onClick = function () { view.clearPreviewLUT(); d.cancel(); };
    var btnOK = new PushButton("OK"); btnOK.onClick = function () { applyOperation(); d.ok(); };

    btnRow.add(btnReset); btnRow.addStretch(); btnRow.add(btnCancel); btnRow.add(btnApply); btnRow.add(btnOK);
    root.add(btnRow);

    d.setSizer(root);
    updateHistogramWidget();
    updateImagePreview();
    d.execute();
}
