#pragma once

#include "AnalysisPrep.h"
#include <JuceHeader.h>
#include <cmath>
#include <map>
#include <future>

extern "C"
{
    #include "libebur128/ebur128.h"
}

struct TrackAnalysisData
{
    // Duration
    double durationInSeconds = 0.0;

    // BPM
    double bpm = 0.0;
    double bpmConfidence = 0.0;

    // Key & Camelot
    juce::String musicalKey = "Unknown";
    double keyConfidence = 0.0;
    juce::String camelotKey = "Unknown";

    // Loudness
    double integratedLUFS = -100.0;
    double shortTermMaxLUFS = -100.0;
    double momentaryMaxLUFS = -100.0;
    double loudnessRange = 0.0;
    double averageDynamicsPLR = 0.0;
    double truePeakMax = -100.0;

    // Elapsed Time
    double timeAudioLoading = 0.0;
    double timeLoudnessAnalysis = 0.0;
    double timeBpmPrep = 0.0;
    double timeBpmEssentia = 0.0;
    double timeKeyPrep = 0.0;
    double timeKeyEssentia = 0.0;
    double timeSpectrumCalc = 0.0;
    double timeTotal = 0.0;
    
    juce::String getFormattedDuration()
    {
        if (durationInSeconds <= 0) return "00:00";

        int minutes = (int)durationInSeconds / 60;
        int seconds = (int)durationInSeconds % 60;

        return juce::String::formatted("%02d:%02d", minutes, seconds);
    }
};

class AnalysisEngine
{
public:
    AnalysisEngine() {}

    juce::String getCamelot(juce::String key, juce::String scale)
    {
        static const std::map<juce::String, juce::String> camelotMap =
        {
            // MAJOR KEYS
            {"B major", "1B"},
            {"F# major", "2B"}, {"Gb major", "2B"},
            {"Db major", "3B"}, {"C# major", "3B"},
            {"Ab major", "4B"}, {"G# major", "4B"},
            {"Eb major", "5B"}, {"D# major", "5B"},
            {"Bb major", "6B"}, {"A# major", "6B"},
            {"F major", "7B"},
            {"C major", "8B"},
            {"G major", "9B"},
            {"D major", "10B"},
            {"A major", "11B"},
            {"E major", "12B"},
            // MINOR KEYS
            {"Ab minor", "1A"}, {"G# minor", "1A"},
            {"Eb minor", "2A"}, {"D# minor", "2A"},
            {"Bb minor", "3A"}, {"A# minor", "3A"},
            {"F minor", "4A"},
            {"C minor", "5A"},
            {"G minor", "6A"},
            {"D minor", "7A"},
            {"A minor", "8A"},
            {"E minor", "9A"},
            {"B minor", "10A"},
            {"F# minor", "11A"}, {"Gb minor", "11A"},
            {"Db minor", "12A"}, {"C# minor", "12A"}
        };

        juce::String lookupKey = key + " " + scale.toLowerCase();
        auto it = camelotMap.find(lookupKey);

        if (it != camelotMap.end()) return it->second;

        return "Unknown";
    }

    juce::var parseEssentiaOutput(const juce::String& output, bool isJsonExpected)
    {
        // Key Analysis JSON File
        if (isJsonExpected)
        {
            juce::String trimmed = output.trim();

            if (trimmed.isNotEmpty() && (trimmed.startsWith("{") || trimmed.startsWith("[")))
            {
                auto jsonResult = juce::JSON::parse(output);

                if (!jsonResult.isVoid()) return jsonResult;
            }
        }
        
        // BPM Analysis Manual Parser (stdout)
        juce::DynamicObject* obj = new juce::DynamicObject();
        juce::StringArray lines;
        lines.addLines(output);

        for (auto& line : lines)
        {
            line = line.trim();

            if (line.isEmpty() || line.startsWith("#") || line.startsWith("-") || line.startsWith("{") || line.startsWith("}")) continue;

            int colonPos = line.indexOf(":");

            if (colonPos > 0)
            {
                juce::String key = line.substring(0, colonPos).trim();
                juce::String valueStr = line.substring(colonPos + 1).trim();

                if (valueStr.startsWith("[")) continue;

                key = key.replace("\"", "").replace("\'", "");
                valueStr = valueStr.replace("\"", "").replace("\'", "").replace(",", "");

                if (valueStr.containsOnly("0123456789.-+eE")) obj->setProperty(key, valueStr.getDoubleValue());
                else obj->setProperty(key, valueStr);
            }
        }

        return juce::var(obj);
    }

    juce::var runEssentiaProcess(juce::File exeFile, juce::File audioFile, juce::File outputFile, bool hasOutputFileArg)
    {
        if (!exeFile.existsAsFile()) return juce::var();

        if (outputFile.exists()) outputFile.deleteFile();

        juce::String command;

        if (hasOutputFileArg)
        {
            // Essentia Key Arguments: "input" "output"
            command = "\"" + exeFile.getFullPathName() + "\" \"" + audioFile.getFullPathName() + "\" \"" + outputFile.getFullPathName() + "\"";
        }
        else
        {
            // Essentia BPM Arguments: "input" (no output file, prints to stdout)
            command = "\"" + exeFile.getFullPathName() + "\" \"" + audioFile.getFullPathName() + "\"";
        }

        juce::ChildProcess process;

        if (process.start(command))
        {
            juce::String outputStr = process.readAllProcessOutput();
            process.waitForProcessToFinish(20000);

            // Read stdout for BPM values
            if (!hasOutputFileArg && outputStr.isNotEmpty())
            {
                /*outputFile.create();
                outputFile.replaceWithText(outputStr);*/ // Optional Log File

                return parseEssentiaOutput(outputStr, hasOutputFileArg);
            }

            // Read JSON for Key values
            if (hasOutputFileArg && outputFile.existsAsFile())
            {
                juce::String fileContent = outputFile.loadFileAsString();

                return parseEssentiaOutput(fileContent, hasOutputFileArg);
            }
        }

        return juce::var();
    }

    bool extractToolIfNeeded(juce::File targetFile, const char* resourceData, int resourceSize)
    {
        if (targetFile.existsAsFile() && targetFile.getSize() == resourceSize) return true;

        targetFile.getParentDirectory().createDirectory();

        if (targetFile.exists()) targetFile.deleteFile();

        return targetFile.replaceWithData(resourceData, resourceSize);
    }

    TrackAnalysisData analyzeLoudnessWithLib(juce::File audioFile)
    {
        TrackAnalysisData d;

        double tStart = juce::Time::getMillisecondCounterHiRes();

        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(audioFile));

        if (reader == nullptr) return d;

        ebur128_state* st = ebur128_init((unsigned)reader->numChannels, (unsigned)reader->sampleRate, EBUR128_MODE_I | EBUR128_MODE_LRA | EBUR128_MODE_TRUE_PEAK | EBUR128_MODE_S | EBUR128_MODE_M);

        if (!st) return d;

        const int bufferSize = 4096;
        juce::AudioBuffer<float> buffer(reader->numChannels, bufferSize);
        std::vector<float> interleavedBuffer(bufferSize * reader->numChannels);

        double maxMomentary = -1000.0;
        double maxShortTerm = -1000.0;

        int64_t position = 0;

        while (position < reader->lengthInSamples)
        {
            int numSamples = (int)std::min((int64_t)bufferSize, reader->lengthInSamples - position);
            reader->read(&buffer, 0, numSamples, position, true, true);

            for (int i = 0; i < numSamples; ++i)
            {
                for (int ch = 0; ch < reader->numChannels; ++ch)
                {
                    interleavedBuffer[i * reader->numChannels + ch] = buffer.getSample(ch, i);
                }
            }

            ebur128_add_frames_float(st, interleavedBuffer.data(), (size_t)numSamples);

            double currentMom = -1000.0;
            double currentST = -1000.0;

            if (ebur128_loudness_momentary(st, &currentMom) == EBUR128_SUCCESS)
            {
                if (currentMom > maxMomentary) maxMomentary = currentMom;
            }

            if (ebur128_loudness_shortterm(st, &currentST) == EBUR128_SUCCESS)
            {
                if (currentST > maxShortTerm) maxShortTerm = currentST;
            }

            position += numSamples;
        }

        double val = -100.0;

        if (ebur128_loudness_global(st, &val) == EBUR128_SUCCESS) d.integratedLUFS = val;

        if (ebur128_loudness_range(st, &val) == EBUR128_SUCCESS) d.loudnessRange = val;

        if (maxMomentary > -900.0) d.momentaryMaxLUFS = maxMomentary;

        if (maxShortTerm > -900.0) d.shortTermMaxLUFS = maxShortTerm;

        double maxPeak = 0.0;

        for (int i = 0; i < reader->numChannels; ++i)
        {
            double chPeak = 0.0;

            ebur128_true_peak(st, i, &chPeak);
            
            if (chPeak > maxPeak) maxPeak = chPeak;
        }

        if (maxPeak > 0.000001) d.truePeakMax = 20.0 * std::log10(maxPeak);

        if (d.integratedLUFS > -100.0 && d.truePeakMax > -100.0) d.averageDynamicsPLR = d.truePeakMax - d.integratedLUFS;

        ebur128_destroy(&st);

        d.timeLoudnessAnalysis = juce::Time::getMillisecondCounterHiRes() - tStart;

        return d;
    }

    TrackAnalysisData analyzeFile(juce::File audioFile)
    {
        TrackAnalysisData finalData;

        double tGlobalStart = juce::Time::getMillisecondCounterHiRes();

        juce::File appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
        juce::File toolsDir = appDataDir.getChildFile("AudioAnalyzer").getChildFile("Tools");
        juce::File exeBPM = toolsDir.getChildFile("essentia_bpm.exe");
        juce::File exeKey = toolsDir.getChildFile("essentia_key.exe");

        extractToolIfNeeded(exeBPM, BinaryData::essentia_streaming_rhythmextractor_multifeature_exe, BinaryData::essentia_streaming_rhythmextractor_multifeature_exeSize);
        extractToolIfNeeded(exeKey, BinaryData::essentia_streaming_key_exe, BinaryData::essentia_streaming_key_exeSize);

        double tLoadStart = juce::Time::getMillisecondCounterHiRes();

        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(audioFile));

        if (reader == nullptr) return finalData;

        double sampleRate = reader->sampleRate;

        finalData.durationInSeconds = reader->lengthInSamples / sampleRate;

        finalData.timeAudioLoading = juce::Time::getMillisecondCounterHiRes() - tLoadStart;

        juce::String uniqueId = juce::String::toHexString(juce::Random::getSystemRandom().nextInt64());

        // Loudness Analysis
        auto futureLoudness = std::async(std::launch::async, [this, audioFile]() -> TrackAnalysisData
        {
            return analyzeLoudnessWithLib(audioFile);
        });

        // BPM Analysis
        auto futureBPM = std::async(std::launch::async, [this, audioFile, exeBPM, sampleRate, uniqueId]() -> TrackAnalysisData
        {
            TrackAnalysisData d;

            juce::AudioFormatManager fm; fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(audioFile));
            juce::AudioBuffer<float> bpmBuffer(r->numChannels, (int)r->lengthInSamples);
            r->read(&bpmBuffer, 0, (int)r->lengthInSamples, 0, true, true);

            AnalysisPrep::normalizeAudio(bpmBuffer, -6.0f);

            double tStart = juce::Time::getMillisecondCounterHiRes();
                
            AnalysisPrep::applyBpmFilter(bpmBuffer, sampleRate);
            AnalysisPrep::cropToLoudestSection(bpmBuffer, sampleRate, 30.0);

            juce::File tempWav = audioFile.getParentDirectory().getChildFile("temp_bpm_" + uniqueId + ".wav");
            bool saved = AnalysisPrep::saveTempWav(bpmBuffer, sampleRate, tempWav);
            
            d.timeBpmPrep = juce::Time::getMillisecondCounterHiRes() - tStart;

            if (saved)
            {
                tStart = juce::Time::getMillisecondCounterHiRes();

                juce::File outLog = audioFile.getParentDirectory().getChildFile("temp_bpm_out_" + uniqueId + ".txt");

                auto json = runEssentiaProcess(exeBPM, tempWav, outLog, false);

                d.timeBpmEssentia = juce::Time::getMillisecondCounterHiRes() - tStart;

                if (json.isObject())
                {
                    if (json.hasProperty("bpm")) d.bpm = (double)json["bpm"];

                    double rawConf = 0.0;

                    if (json.hasProperty("ticks detection confidence")) rawConf = (double)json["ticks detection confidence"];
                    else if (json.hasProperty("confidence")) rawConf = (double)json["confidence"];

                    rawConf /= 5;
                    d.bpmConfidence = std::sqrt(rawConf) * 100.0;
                    d.bpmConfidence = juce::jlimit(0.0, 100.0, d.bpmConfidence);

                    while (d.bpm < 70.0 && d.bpm > 0.0)
                    {
                        d.bpm *= 2.0;
                    }

                    while (d.bpm > 190.0)
                    {
                        d.bpm /= 2.0;
                    }

                    d.bpm = std::round(d.bpm);
                }

                tempWav.deleteFile();

                if (outLog.exists()) outLog.deleteFile();
            }
            
            return d;
        });

        // Key Analysis
        auto futureKey = std::async(std::launch::async, [this, audioFile, exeKey, sampleRate, uniqueId]() -> TrackAnalysisData
        {
            TrackAnalysisData d;
            juce::AudioBuffer<float> keyBuffer;
            
            juce::AudioFormatManager fm; fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(audioFile));
            keyBuffer.setSize(r->numChannels, (int)r->lengthInSamples);
            r->read(&keyBuffer, 0, (int)r->lengthInSamples, 0, true, true);

            AnalysisPrep::normalizeAudio(keyBuffer, -6.0f);

            double tStart = juce::Time::getMillisecondCounterHiRes();
            
            AnalysisPrep::applyKeyFilter(keyBuffer, sampleRate);
            AnalysisPrep::cropToLoudestSection(keyBuffer, sampleRate, 60.0);

            juce::File tempWav = audioFile.getParentDirectory().getChildFile("temp_key_" + uniqueId + ".wav");
            bool saved = AnalysisPrep::saveTempWav(keyBuffer, sampleRate, tempWav);
                
            d.timeKeyPrep = juce::Time::getMillisecondCounterHiRes() - tStart;

            if (saved)
            {
                tStart = juce::Time::getMillisecondCounterHiRes();

                juce::File outLog = audioFile.getParentDirectory().getChildFile("temp_key_out_" + uniqueId + ".json");

                auto json = runEssentiaProcess(exeKey, tempWav, outLog, true);

                d.timeKeyEssentia = juce::Time::getMillisecondCounterHiRes() - tStart;

                if (json.isObject())
                {
                    juce::String key, scale;
                    double strength = 0.0;

                    if (json.hasProperty("tonal") && json["tonal"].isObject())
                    {
                        auto tonal = json["tonal"];

                        if (tonal.hasProperty("key")) key = tonal["key"].toString();

                        if (tonal.hasProperty("key_scale")) scale = tonal["key_scale"].toString();

                        if (scale.isEmpty() && tonal.hasProperty("scale")) scale = tonal["scale"].toString();

                        if (tonal.hasProperty("key_strength")) strength = (double)tonal["key_strength"];
                    }
                    else
                    {
                        if (json.hasProperty("key")) key = json["key"].toString();

                        if (json.hasProperty("key_scale")) scale = json["key_scale"].toString();
                        else if (json.hasProperty("scale")) scale = json["scale"].toString();

                        if (json.hasProperty("key_strength")) strength = (double)json["key_strength"];
                        else if (json.hasProperty("strength")) strength = (double)json["strength"];
                    }

                    if (key.isNotEmpty())
                    {
                        key = key.substring(0, 1).toUpperCase() + key.substring(1);

                        if (scale.isNotEmpty()) scale = scale.substring(0, 1).toUpperCase() + scale.substring(1).toLowerCase();

                        d.musicalKey = key + " " + scale;
                        d.camelotKey = getCamelot(key, scale);
                        d.keyConfidence = std::sqrt(strength) * 100.0;
                        d.keyConfidence = juce::jlimit(0.0, 100.0, d.keyConfidence);
                    }
                }

                tempWav.deleteFile();

                if (outLog.exists()) outLog.deleteFile();
            }

            return d;
        });

        auto r1 = futureLoudness.get();
        auto r2 = futureBPM.get();
        auto r3 = futureKey.get();

        finalData.bpm = r2.bpm;
        finalData.bpmConfidence = r2.bpmConfidence;
        finalData.timeBpmPrep = r2.timeBpmPrep;
        finalData.timeBpmEssentia = r2.timeBpmEssentia;
        finalData.musicalKey = r3.musicalKey;
        finalData.camelotKey = r3.camelotKey;
        finalData.keyConfidence = r3.keyConfidence;
        finalData.timeKeyPrep = r3.timeKeyPrep;
        finalData.timeKeyEssentia = r3.timeKeyEssentia;
        finalData.integratedLUFS = r1.integratedLUFS;
        finalData.loudnessRange = r1.loudnessRange;
        finalData.truePeakMax = r1.truePeakMax;
        finalData.averageDynamicsPLR = r1.averageDynamicsPLR;
        finalData.shortTermMaxLUFS = r1.shortTermMaxLUFS;
        finalData.momentaryMaxLUFS = r1.momentaryMaxLUFS;
        finalData.timeLoudnessAnalysis = r1.timeLoudnessAnalysis;
        
        finalData.timeTotal = juce::Time::getMillisecondCounterHiRes() - tGlobalStart;

        return finalData;
    }
};