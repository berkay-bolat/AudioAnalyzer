#include "PluginProcessor.h"
#include "PluginEditor.h"

AudioAnalyzerAudioProcessorEditor::AudioAnalyzerAudioProcessorEditor(AudioAnalyzerAudioProcessor& p) : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(900, 700);

    analysisThread = std::make_unique<AnalysisThread>(p, [this]() { analysisFinished(); });

    addAndMakeVisible(loadButton);
    loadButton.setButtonText("LOAD AUDIO FILE");
    loadButton.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser>("Select Audio File", juce::File{}, "*.aiff;*.flac;*.mp3;*.ogg;*.wav");

        auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

        fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();

            if (file.existsAsFile())
            {
                startAnalysis(file);
            }
        });
    };

    addAndMakeVisible(spectrumAnalyzer);

    addAndMakeVisible(smoothingLabel);
    smoothingLabel.setText("SMOOTHING FACTOR:", juce::dontSendNotification);
    smoothingLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    smoothingLabel.attachToComponent(&smoothingCombo, true);

    addAndMakeVisible(smoothingCombo);
    smoothingCombo.addItem("RAW", 1);
    smoothingCombo.addItem("1/48 OCT", 2);
    smoothingCombo.addItem("1/24 OCT", 3);
    smoothingCombo.addItem("1/12 OCT", 4);
    smoothingCombo.addItem("1/6 OCT", 5);
    smoothingCombo.addItem("1/3 OCT", 6);
    smoothingCombo.addItem("1/2 OCT", 7);
    smoothingCombo.addItem("1 OCT", 8);
    smoothingCombo.onChange = [this]
    {
        float factor = 0.3f;

        switch (smoothingCombo.getSelectedId())
        {
            case 1: factor = 0.0f;   break;
            case 2: factor = 0.02f;  break;
            case 3: factor = 0.04f;  break;
            case 4: factor = 0.08f;  break;
            case 5: factor = 0.15f;  break;
            case 6: factor = 0.3f;   break;
            case 7: factor = 0.5f;   break;
            case 8: factor = 0.8f;   break;
        }
        
        spectrumAnalyzer.setSmoothingOctave(factor);
    };
    smoothingCombo.setSelectedId(6, juce::dontSendNotification);

    auto setupToggle = [&](juce::ToggleButton& btn, bool initialState, auto setterFunc)
    {
        addAndMakeVisible(btn);
        btn.setToggleState(initialState, juce::dontSendNotification);
        btn.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
        btn.setColour(juce::ToggleButton::tickColourId, juce::Colours::lightgreen);

        btn.onClick = [this, &btn, setterFunc]
        {
            setterFunc(btn.getToggleState());

            spectrumAnalyzer.repaint();
        };
    };

    setupToggle(btnShowMidAvg, false, [&](bool b) {spectrumAnalyzer.settings.showMidAvg = b;});
    setupToggle(btnShowMidMax, false, [&](bool b) {spectrumAnalyzer.settings.showMidMax = b;});
    setupToggle(btnShowSideAvg, false, [&](bool b) {spectrumAnalyzer.settings.showSideAvg = b;});
    setupToggle(btnShowSideMax, false, [&](bool b) {spectrumAnalyzer.settings.showSideMax = b;});
    setupToggle(btnShowStereoAvg, true, [&](bool b) {spectrumAnalyzer.settings.showStereoAvg = b;});
    setupToggle(btnShowStereoMax, true, [&](bool b) {spectrumAnalyzer.settings.showStereoMax = b;});

    auto setupLabel = [&](juce::Label& lbl, juce::String initText)
    {
        addAndMakeVisible(lbl);
        lbl.setText(initText, juce::dontSendNotification);
        lbl.setJustificationType(juce::Justification::centred);
        lbl.setColour(juce::Label::textColourId, juce::Colours::white);
    };

    setupLabel(durationLabel, "DURATION: Unknown");
    setupLabel(bpmLabel, "BPM: Unknown");
    setupLabel(bpmConfidenceLabel, "BPM CONFIDENCE: Unknown");
    setupLabel(keyLabel, "KEY: Unknown");
    setupLabel(keyConfidenceLabel, "KEY CONFIDENCE: Unknown");
    setupLabel(camelotLabel, "CAMELOT: Unknown");
    setupLabel(integratedLUFSLabel, "INTEGRATED LUFS: Unknown");
    setupLabel(shortTermMaxLUFSLabel, "SHORT TERM MAXIMUM LUFS: Unknown");
    setupLabel(momentaryMaxLUFSLabel, "MOMENTARY MAXIMUM LUFS: Unknown");
    setupLabel(loudnessRangeLabel, "LOUDNESS RANGE: Unknown");
    setupLabel(averageDynamicsPLRLabel, "AVERAGE DYNAMICS (PLR): Unknown");
    setupLabel(truePeakMaxDbLabel, "TRUE PEAK MAXIMUM dB: Unknown");
}

AudioAnalyzerAudioProcessorEditor::~AudioAnalyzerAudioProcessorEditor()
{
    if (analysisThread->isThreadRunning()) analysisThread->stopThread(2000);
}

void AudioAnalyzerAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromFloatRGBA(0.1f, 0.12f, 0.11f, 1.0f));
}

void AudioAnalyzerAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(15);

    // Load Button
    loadButton.setBounds(area.removeFromTop(30));

    area.removeFromTop(10);

    // Labels
    auto labelArea = area.removeFromTop(150);
    int w = labelArea.getWidth() / 2;
    int h = 25;

    durationLabel.setBounds(labelArea.getX(), labelArea.getY(), w, h);
    bpmLabel.setBounds(labelArea.getX(), labelArea.getY() + h, w, h);
    bpmConfidenceLabel.setBounds(labelArea.getX(), labelArea.getY() + h * 2, w, h);
    keyLabel.setBounds(labelArea.getX(), labelArea.getY() + h * 3, w, h);
    keyConfidenceLabel.setBounds(labelArea.getX(), labelArea.getY() + h * 4, w, h);
    camelotLabel.setBounds(labelArea.getX(), labelArea.getY() + h * 5, w, h);

    int col2X = labelArea.getX() + w;

    integratedLUFSLabel.setBounds(col2X, labelArea.getY(), w, h);
    shortTermMaxLUFSLabel.setBounds(col2X, labelArea.getY() + h, w, h);
    momentaryMaxLUFSLabel.setBounds(col2X, labelArea.getY() + h * 2, w, h);
    loudnessRangeLabel.setBounds(col2X, labelArea.getY() + h * 3, w, h);
    averageDynamicsPLRLabel.setBounds(col2X, labelArea.getY() + h * 4, w, h);
    truePeakMaxDbLabel.setBounds(col2X, labelArea.getY() + h * 5, w, h);

    area.removeFromTop(15);

    // Spectrum Analyzer
    auto spectrumArea = area.removeFromTop(380);

    spectrumAnalyzer.setBounds(spectrumArea);

    area.removeFromTop(10);

    // ComboBox
    auto smoothRow = area.removeFromTop(25);

    smoothRow.removeFromLeft(150);
    smoothingCombo.setBounds(smoothRow.removeFromLeft(100));

    area.removeFromTop(5);

    // Toggles
    int btnW = area.getWidth() / 5;
    int btnH = 25;
    auto row1 = area.removeFromTop(btnH);

    btnShowMidAvg.setBounds(row1.removeFromLeft(btnW));
    row1.removeFromLeft(btnW);
    btnShowSideAvg.setBounds(row1.removeFromLeft(btnW));
    row1.removeFromLeft(btnW);
    btnShowStereoAvg.setBounds(row1.removeFromLeft(btnW));

    auto row2 = area.removeFromTop(btnH);

    btnShowMidMax.setBounds(row2.removeFromLeft(btnW));
    row2.removeFromLeft(btnW);
    btnShowSideMax.setBounds(row2.removeFromLeft(btnW));
    row2.removeFromLeft(btnW);
    btnShowStereoMax.setBounds(row2.removeFromLeft(btnW));
}

void AudioAnalyzerAudioProcessorEditor::timerCallback()
{
    if (isAnalyzing)
    {
        loadingAnimationPos += 0.02f;

        if (loadingAnimationPos > 1.0f) loadingAnimationPos = 0.0f;

        repaint();
    }
}

bool AudioAnalyzerAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    if (files.size() != 1) return false;

    auto ext = files[0].substring(files[0].lastIndexOf("."));

    return (ext.equalsIgnoreCase(".aiff") || ext.equalsIgnoreCase(".flac") || ext.equalsIgnoreCase(".mp3") || ext.equalsIgnoreCase(".ogg") || ext.equalsIgnoreCase(".wav"));
}

void AudioAnalyzerAudioProcessorEditor::filesDropped(const juce::StringArray& files, int x, int y)
{
    if (files.size() == 1)
    {
        startAnalysis(juce::File(files[0]));
    }
}

void AudioAnalyzerAudioProcessorEditor::paintOverChildren(juce::Graphics& g)
{
    if (isAnalyzing)
    {
        g.setColour(juce::Colours::black.withAlpha(0.7f));
        g.fillAll();

        auto center = getLocalBounds().getCentre();
        int w = 220; int h = 70;
        juce::Rectangle<float> box(center.x - w / 2, center.y - h / 2, w, h);

        g.setColour(juce::Colour(0xff202221));
        g.fillRoundedRectangle(box, 12.0f);
        g.setColour(juce::Colours::white.withAlpha(0.2f));
        g.drawRoundedRectangle(box, 12.0f, 2.0f);
        g.setColour(juce::Colours::white);
        g.setFont(20.0f);
        g.drawText("ANALYZING...", box.translated(0.0f, -10.0f), juce::Justification::centred);
        g.setColour(juce::Colours::lightgreen);

        float barW = (float)(w - 40);
        float x = box.getX() + 20 + (barW * loadingAnimationPos);
        float width = barW * 0.2f;

        if (x + width > box.getRight() - 20)
        {
            float part1 = (box.getRight() - 20) - x;

            g.fillRect(x, box.getBottom() - 15, part1, 4.0f);
            g.fillRect((float)(box.getX() + 20), box.getBottom() - 15, width - part1, 4.0f);
        }
        else
        {
            g.fillRect(x, box.getBottom() - 15, width, 4.0f);
        }
    }
}

void AudioAnalyzerAudioProcessorEditor::startAnalysis(juce::File file)
{
    if (isAnalyzing) return;

    isAnalyzing = true;
    loadButton.setEnabled(false);

    startTimerHz(30);
    repaint();

    analysisThread->startAnalysis(file);
}

void AudioAnalyzerAudioProcessorEditor::analysisFinished()
{
    stopTimer();

    auto& data = audioProcessor.currentData;

    durationLabel.setText("DURATION: " + data.getFormattedDuration(), juce::dontSendNotification);
    bpmLabel.setText("BPM: " + juce::String(data.bpm), juce::dontSendNotification);
    bpmConfidenceLabel.setText("BPM CONFIDENCE: %" + juce::String::formatted("%.2f", data.bpmConfidence), juce::dontSendNotification);
    keyLabel.setText("KEY: " + data.musicalKey, juce::dontSendNotification);
    keyConfidenceLabel.setText("KEY CONFIDENCE: %" + juce::String::formatted("%.2f", data.keyConfidence), juce::dontSendNotification);
    camelotLabel.setText("CAMELOT: " + data.camelotKey, juce::dontSendNotification);
    integratedLUFSLabel.setText("INTEGRATED LUFS: " + juce::String::formatted("%.2f", data.integratedLUFS), juce::dontSendNotification);
    shortTermMaxLUFSLabel.setText("SHORT TERM MAXIMUM LUFS: " + juce::String::formatted("%.2f", data.shortTermMaxLUFS), juce::dontSendNotification);
    momentaryMaxLUFSLabel.setText("MOMENTARY MAXIMUM LUFS: " + juce::String::formatted("%.2f", data.momentaryMaxLUFS), juce::dontSendNotification);
    loudnessRangeLabel.setText("LOUDNESS RANGE: " + juce::String::formatted("%.2f", data.loudnessRange), juce::dontSendNotification);
    averageDynamicsPLRLabel.setText("AVERAGE DYNAMICS (PLR): " + juce::String::formatted("%.2f", data.averageDynamicsPLR), juce::dontSendNotification);
    truePeakMaxDbLabel.setText("TRUE PEAK MAXIMUM dB: " + juce::String::formatted("%.2f", data.truePeakMax), juce::dontSendNotification);

    if (analysisThread->spectrumBuffer.getNumSamples() > 0 && analysisThread->sampleRate > 0)
    {
        spectrumAnalyzer.analyzeBuffer(analysisThread->spectrumBuffer, analysisThread->sampleRate);
    }

    isAnalyzing = false;
    loadButton.setEnabled(true);

    repaint();
}
