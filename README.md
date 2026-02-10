# AudioAnalyzer

AUDIO ANALYZER

An audio analysis application that analyzes the duration, BPM, key, Camelot, loudness (RMS/LUFS), frequency spectrum (mid/side/stereo) and chord information/data of an uploaded audio file, and presents the results to the user; usable both as a standalone application and as a VST plugin within DAWs.

![Spectrum Analyzer](images/screenshot.png)

## KEY FEATURES

#### Advanced Spectrum Analyzer

High-Resolution FFT: Uses a 16,384-point FFT window for precise frequency analysis.

Mid/Side & Full Stereo Processing: Visualizes Mid and Side information separately to analyze stereo width and mono compatibility. Also full stereo information is available.

Multi-View Modes: Toggle between Total, Mid, Side, Average, and Maximum Hold visualizations.

Pixel-Perfect Rendering: Custom-drawn grid and frequency response curves using JUCE Graphics API.

Hover Effect: Easily track any frequency and dB value over the grid.

#### Loudness Metering (libebur128)

Integrated LUFS: Measures the overall loudness of the track.

True Peak Detection: Accurately detects inter-sample peaks using 4x oversampling.

Dynamics Analysis: Calculates PLR (Peak-to-Loudness Ratio) and Loudness Range (LRA) to assess dynamic range.

Momentary & Short-Term Max: Monitors loudness fluctuations.

#### Music Information Retrieval (essentia)

BPM Detection: Accurate tempo estimation with confidence scoring via efficient pre-processing.

Key Detection: Identifies the musical key and scale with confidence scoring via efficient pre-processing.

Camelot Wheel Notation: Automatically converts musical keys to Camelot notation for DJ/harmonic mixing compatibility.

## Technical Architecture

This project demonstrates advanced C++ and JUCE techniques:

#### Hybrid Analysis Engine (Multi-threading)

The plugin uses a dual-threaded approach to ensure the UI remains responsive:

 - Real-Time Thread: Handles audio buffer processing, FFT calculations, and UI painting.
 - Background Thread: Handles heavy offline analysis (Loudness, BPM, Key) asynchronously.

#### Embedded External Tools (Portable App/Plugin)

To ensure the plugin works on any machine without requiring the user to install Python or any external libraries:

 - The Essentia CLI tools are embedded directly into the plugin binary using JUCE's BinaryData. At runtime, the plugin checks if these tools exist in the user's AppData folder. If not, it extracts them automatically. The app/plugin communicates with these tools and parses their standard output.

#### Clean Architecture

 - AnalysisEngine: Handles the logic for BPM/key detection, loudness calculation and external process management.
 - SpectrumAnalyzer: A self-contained component responsible for FFT signal processing and graphical rendering.
 - AnalysisPrep: Helper class for audio normalization and pre-processing before analysis.

## Installation

 1. Download the latest release (available in the Releases section).
 2. You can directly use AudioAnalyzer.exe (in the "Standalone Application" folder) as a standalone application. 
 3. If you want to use as a plugin, copy the AudioAnalyzer.vst3 (in the "VST3 Plugin" folder) file to your system's VST3 folder.
 4. Rescan plugins in your DAW.

#### Building From The Source Code

If you want to build the plugin from the source code:

Prerequisites:

 - Visual Studio 2022 or above (Windows), with "Desktop development with C++"
 - Projucer (JUCE Framework)

Steps:

 1. Clone this repository:

<pre> git clone https://github.com/berkay-bolat/AudioAnalyzer.git </pre>

 2. Open the AudioAnalyzer.jucer file with the Projucer.
 3. Ensure the suitable (2022 or above) "Visual Studio" exporter is selected.
 4. Click "Save and Open in IDE".
 5. In Visual Studio, select Release configuration and x64 platform.
 6. Build the solution (Build->Build Solution).
 7. The standalone application will be located in a path like "Builds/VisualStudio2026/x64/Release/Standalone Plugin/AudioAnalyzer.exe".
 8. The plugin output file will be located in a path like "Builds/VisualStudio2026/x64/Release/VST3/AudioAnalyzer.vst3".

## Dependencies & Credits

JUCE Framework: The core framework used for UI and DSP.

Essentia: Used for BPM and Key detection algorithms.

libebur128: Used for EBU R 128 standard loudness measurements.

## License

This project is open-source. Please refer to the LICENSE file for more details.