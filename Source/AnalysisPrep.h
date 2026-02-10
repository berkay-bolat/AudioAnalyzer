#pragma once

#include <JuceHeader.h>

class AnalysisPrep
{
public:

    static void normalizeAudio(juce::AudioBuffer<float>& buffer, float targetDb = -9.0f)
    {
        float magnitude = buffer.getMagnitude(0, buffer.getNumSamples());

        if (magnitude < 0.001f) return;

        float currentDb = juce::Decibels::gainToDecibels(magnitude);
        float gainNeeded = targetDb - currentDb;
        buffer.applyGain(juce::Decibels::decibelsToGain(gainNeeded));
    }

    static void cropToLoudestSection(juce::AudioBuffer<float>& buffer, double sampleRate, double durationSeconds)
    {
        int totalSamples = buffer.getNumSamples();
        int windowSamples = (int)(durationSeconds * sampleRate);

        if (totalSamples <= windowSamples) return;

        int numChannels = buffer.getNumChannels();
        double maxRMS = -1.0;
        int bestStartSample = 0;
        int stepSize = (int)(sampleRate * 0.5);

        for (int i = 0; i < totalSamples - windowSamples; i += stepSize)
        {
            double currentRMS = 0.0;
            
            for (int ch = 0; ch < numChannels; ++ch)
            {
                currentRMS += buffer.getRMSLevel(ch, i, windowSamples);
            }

            if (currentRMS > maxRMS)
            {
                maxRMS = currentRMS;
                bestStartSample = i;
            }
        }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* channelData = buffer.getWritePointer(ch);
            std::copy(channelData + bestStartSample, channelData + bestStartSample + windowSamples, channelData);
        }

        buffer.setSize(numChannels, windowSamples, true, true, true);
    }

    static void applyBpmFilter(juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        juce::AudioBuffer<float> lowBandBuffer;
        lowBandBuffer.makeCopyOf(buffer);
        juce::AudioBuffer<float> highBandBuffer;
        highBandBuffer.makeCopyOf(buffer);

        {
            juce::dsp::AudioBlock<float> block(lowBandBuffer);
            juce::dsp::ProcessContextReplacing<float> context(block);

            auto coeffsHP = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 40.0f);
            auto coeffsLP = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, 1000.0f);

            for (int ch = 0; ch < lowBandBuffer.getNumChannels(); ++ch)
            {    
                juce::dsp::IIR::Filter<float> filterHP;
                filterHP.coefficients = coeffsHP;
                filterHP.prepare({ sampleRate, (juce::uint32)block.getNumSamples(), 1 });

                auto singleBlock = block.getSingleChannelBlock(ch);
                juce::dsp::ProcessContextReplacing<float> singleContext(singleBlock);
                filterHP.process(singleContext);

                juce::dsp::IIR::Filter<float> filterLP;
                filterLP.coefficients = coeffsLP;
                filterLP.prepare({ sampleRate, (juce::uint32)block.getNumSamples(), 1 });
                filterLP.process(singleContext);
            }
        }

        {
            juce::dsp::AudioBlock<float> block(highBandBuffer);

            auto coeffsHP = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 8000.0f);

            for (int ch = 0; ch < highBandBuffer.getNumChannels(); ++ch)
            {
                juce::dsp::IIR::Filter<float> filter;
                filter.coefficients = coeffsHP;
                filter.prepare({ sampleRate, (juce::uint32)block.getNumSamples(), 1 });

                auto singleBlock = block.getSingleChannelBlock(ch);
                juce::dsp::ProcessContextReplacing<float> singleContext(singleBlock);
                filter.process(singleContext);
            }
        }

        buffer.clear();

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            buffer.addFrom(ch, 0, lowBandBuffer, ch, 0, buffer.getNumSamples());
            buffer.addFrom(ch, 0, highBandBuffer, ch, 0, buffer.getNumSamples());
        }

        buffer.applyGain(0.707f);
    }

    static void applyKeyFilter(juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        juce::dsp::AudioBlock<float> block(buffer);

        auto coeffsHP = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 150.0f);
        auto coeffsLP = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, 5000.0f);
        auto coeffsBoost = juce::dsp::IIR::Coefficients<float>::makeLowShelf(sampleRate, 300.0f, 1.0f, 2.0f);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto singleChannelBlock = block.getSingleChannelBlock(ch);
            juce::dsp::ProcessContextReplacing<float> context(singleChannelBlock);

            juce::dsp::IIR::Filter<float> filterBoost;
            filterBoost.coefficients = coeffsBoost;
            filterBoost.prepare({ sampleRate, (juce::uint32)block.getNumSamples(), 1 });
            filterBoost.process(context);

            juce::dsp::IIR::Filter<float> filterHP;
            filterHP.coefficients = coeffsHP;
            filterHP.prepare({ sampleRate, (juce::uint32)block.getNumSamples(), 1 });
            filterHP.process(context);

            juce::dsp::IIR::Filter<float> filterLP;
            filterLP.coefficients = coeffsLP;
            filterLP.prepare({ sampleRate, (juce::uint32)block.getNumSamples(), 1 });
            filterLP.process(context);
        }
    }

    static bool saveTempWav(juce::AudioBuffer<float>& buffer, double sampleRate, juce::File targetFile)
    {
        targetFile.deleteFile();

        juce::AudioBuffer<float> monoBuffer;

        if (buffer.getNumChannels() > 1)
        {
            monoBuffer.setSize(1, buffer.getNumSamples());
            monoBuffer.clear();

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                monoBuffer.addFrom(0, 0, buffer, ch, 0, buffer.getNumSamples());
            }
            
            monoBuffer.applyGain(1.0f / buffer.getNumChannels());
        }
        else monoBuffer.makeCopyOf(buffer);

        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::FileOutputStream> fileStream(targetFile.createOutputStream());

        if (fileStream == nullptr) return false;

        std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(fileStream.get(), sampleRate, 1, 16, {}, 0));

        if (writer != nullptr)
        {
            fileStream.release();

            return writer->writeFromAudioSampleBuffer(monoBuffer, 0, monoBuffer.getNumSamples());
        }

        return false;
    }
};