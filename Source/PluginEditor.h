#pragma once

#include "PluginProcessor.h"
#include "SpectrumAnalyzer.h"
#include <JuceHeader.h>

class AnalysisThread : public juce::Thread
{
public:

    AnalysisThread(AudioAnalyzerAudioProcessor& p, std::function<void()> onFinished) : Thread("AnalysisThread"), processor(p), onFinishedCallback(onFinished)
    {
    }

    void startAnalysis(juce::File f)
    {
        fileToAnalyze = f;

        startThread();
    }

    void run() override
    {
        double totalStart = juce::Time::getMillisecondCounterHiRes();

        processor.analyzeLoadedFile(fileToAnalyze);

        double specStart = juce::Time::getMillisecondCounterHiRes();

        prepareSpectrumBuffer();

        double specEnd = juce::Time::getMillisecondCounterHiRes();

        processor.currentData.timeSpectrumCalc = specEnd - specStart;
        processor.currentData.timeTotal = juce::Time::getMillisecondCounterHiRes() - totalStart;

        writeLogFile(processor.currentData); // Save Time Log File (Optional)

        juce::MessageManager::callAsync([this]()
        {
            if (onFinishedCallback) onFinishedCallback();
        });
    }

    juce::AudioBuffer<float> spectrumBuffer;
    double sampleRate = 0.0;

private:

    AudioAnalyzerAudioProcessor& processor;
    juce::File fileToAnalyze;
    std::function<void()> onFinishedCallback;

    void writeLogFile(const TrackAnalysisData& data)
    {
        juce::File analyzerDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("AudioAnalyzer");

        if (!analyzerDir.exists()) analyzerDir.createDirectory();

        juce::File logDir = analyzerDir.getChildFile("Log");

        if (!logDir.exists()) logDir.createDirectory();

        juce::File logFile = logDir.getChildFile("audio_analyzer_performance_log.txt");
        juce::String logEntry;

        logEntry << "/--------------------------------------------------\n\n";
        logEntry << "FILE NAME: " << fileToAnalyze.getFileName() << "\n";
        logEntry << "ANALYSIS DATE: " << juce::Time::getCurrentTime().toString(true, true) << "\n\n";
        logEntry << "1. AUDIO LOADING TIME: " << juce::String(data.timeAudioLoading, 2) << " ms\n";
        logEntry << "2. LOUDNESS ANALYSIS TIME: " << juce::String(data.timeLoudnessAnalysis, 2) << " ms\n";
        logEntry << "3. BPM ANALYSIS TIME:\n";
        logEntry << "   - Preparation Time: " << juce::String(data.timeBpmPrep, 2) << " ms\n";
        logEntry << "   - Algorithm Time: " << juce::String(data.timeBpmEssentia, 2) << " ms\n";
        logEntry << "   - Total BPM Analysis Time: " << juce::String(data.timeBpmPrep + data.timeBpmEssentia, 2) << " ms\n";
        logEntry << "4. KEY ANALYSIS TIME:\n";
        logEntry << "   - Preparation Time: " << juce::String(data.timeKeyPrep, 2) << " ms\n";
        logEntry << "   - Algorithm Time: " << juce::String(data.timeKeyEssentia, 2) << " ms\n";
        logEntry << "   - Total Key Analysis Time: " << juce::String(data.timeKeyPrep + data.timeKeyEssentia, 2) << " ms\n";
        logEntry << "5. SPECTRUM ANALYSIS TIME: " << juce::String(data.timeSpectrumCalc, 2) << " ms\n";
        logEntry << "\n>>> TOTAL TIME: " << juce::String(data.timeTotal, 2) << " ms\n\n";
        logEntry << "--------------------------------------------------/\n\n";

        logFile.appendText(logEntry);
    }

    void prepareSpectrumBuffer()
    {
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(fileToAnalyze));

        if (reader != nullptr)
        {
            sampleRate = reader->sampleRate;
            double targetDuration = 30.0;
            int windowSize = (int)(targetDuration * sampleRate);

            if (windowSize > reader->lengthInSamples) windowSize = (int)reader->lengthInSamples;

            int stepSize = (int)sampleRate;
            int numChannels = reader->numChannels;
            int64_t bestStartSample = 0;
            double maxRMS = -1.0;
            juce::AudioBuffer<float> scanBuffer(numChannels, stepSize);

            for (int64_t i = 0; i < reader->lengthInSamples - windowSize; i += stepSize * 5)
            {
                reader->read(&scanBuffer, 0, stepSize, i, true, true);
                double currentRMS = scanBuffer.getRMSLevel(0, 0, stepSize);

                if (numChannels > 1) currentRMS = (currentRMS + scanBuffer.getRMSLevel(1, 0, stepSize)) * 0.5;

                if (currentRMS > maxRMS)
                {
                    maxRMS = currentRMS;
                    bestStartSample = i;
                }
            }

            spectrumBuffer.setSize(numChannels, windowSize);
            reader->read(&spectrumBuffer, 0, windowSize, bestStartSample, true, true);
        }
    }
};

class AudioAnalyzerAudioProcessorEditor : public juce::AudioProcessorEditor,
    public juce::FileDragAndDropTarget, public juce::Timer
{
public:
    AudioAnalyzerAudioProcessorEditor(AudioAnalyzerAudioProcessor&);
    ~AudioAnalyzerAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void timerCallback() override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void paintOverChildren(juce::Graphics&) override;

private:

    void startAnalysis(juce::File file);
    void analysisFinished();

    bool isAnalyzing = false;
    float loadingAnimationPos = 0.0f;

    AudioAnalyzerAudioProcessor& audioProcessor;
    SpectrumAnalyzer spectrumAnalyzer;

    std::unique_ptr<AnalysisThread> analysisThread;
    juce::TextButton loadButton{"LOAD AUDIO FILE"};
    std::unique_ptr<juce::FileChooser> fileChooser;

    // Toggles
    juce::ToggleButton btnShowMidAvg{"MID AVERAGE"};
    juce::ToggleButton btnShowMidMax{"MID MAXIMUM"};
    juce::ToggleButton btnShowSideAvg{"SIDE AVERAGE"};
    juce::ToggleButton btnShowSideMax{"SIDE MAXIMUM"};
    juce::ToggleButton btnShowStereoAvg{"TOTAL AVERAGE"};
    juce::ToggleButton btnShowStereoMax{"TOTAL MAXIMUM"};

    // ComboBox
    juce::ComboBox smoothingCombo;

    // Labels
    juce::Label smoothingLabel;
    juce::Label durationLabel;
    juce::Label bpmLabel;
    juce::Label bpmConfidenceLabel;
    juce::Label keyLabel;
    juce::Label keyConfidenceLabel;
    juce::Label camelotLabel;
    juce::Label integratedLUFSLabel;
    juce::Label shortTermMaxLUFSLabel;
    juce::Label momentaryMaxLUFSLabel;
    juce::Label loudnessRangeLabel;
    juce::Label averageDynamicsPLRLabel;
    juce::Label truePeakMaxDbLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioAnalyzerAudioProcessorEditor)
};