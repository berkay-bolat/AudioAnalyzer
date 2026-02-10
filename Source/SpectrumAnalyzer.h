#pragma once

#include <JuceHeader.h>

class SpectrumAnalyzer : public juce::Component
{
public:

    struct DisplaySettings
    {
        bool showStereoAvg = true;
        bool showStereoMax = true;
        bool showMidAvg = false;
        bool showMidMax = false;
        bool showSideAvg = false;
        bool showSideMax = false;
    } settings;

    SpectrumAnalyzer()
    {
        fftOrder = 14;
        fftSize = 1 << fftOrder;
        forwardFFT = std::make_unique<juce::dsp::FFT>(fftOrder);
        window = std::make_unique<juce::dsp::WindowingFunction<float>>(fftSize, juce::dsp::WindowingFunction<float>::hann);

        setInterceptsMouseClicks(true, false);
    }

    void mouseEnter(const juce::MouseEvent& e) override
    {
        isMouseOverGraph = true;

        repaint();
    }
    
    void mouseExit(const juce::MouseEvent& e) override
    {
        isMouseOverGraph = false;
        
        repaint();
    }
    
    void mouseMove(const juce::MouseEvent& e) override
    {
        isMouseOverGraph = true;
        mousePos = e.getPosition();

        repaint();
    }

    void setSmoothingOctave(float octaveFactor)
    {
        if (std::abs(currentSmoothingFactor - octaveFactor) < 0.001f) return;

        currentSmoothingFactor = octaveFactor;

        reprocessSmoothing();
    }

    void analyzeBuffer(const juce::AudioBuffer<float>& inputBuffer, double sampleRate)
    {
        currentSampleRate = sampleRate;

        if (currentSampleRate <= 0 || inputBuffer.getNumSamples() == 0) return;

        juce::AudioBuffer<float> buffer;
        buffer.makeCopyOf(inputBuffer);

        AnalysisPrep::cropToLoudestSection(buffer, sampleRate, 20.0);

        int numBins = fftSize / 2;

        accMid.assign(numBins, 0.0f);
        accSide.assign(numBins, 0.0f);
        accStereo.assign(numBins, 0.0f);

        float minMag = 1e-9f;

        rawMaxMidMag.assign(numBins, minMag);
        rawMaxSideMag.assign(numBins, minMag);
        rawMaxStereoMag.assign(numBins, minMag);

        std::vector<float> midData(fftSize * 2, 0.0f);
        std::vector<float> sideData(fftSize * 2, 0.0f);

        int numSamples = buffer.getNumSamples();
        int hopSize = fftSize / 4;
        int numBlocks = 0;
        float windowCorrection = 2.0f;

        for (int i = 0; i < numSamples - fftSize; i += hopSize)
        {
            for (int j = 0; j < fftSize; ++j)
            {
                float l = buffer.getSample(0, i + j);
                float r = (buffer.getNumChannels() > 1) ? buffer.getSample(1, i + j) : l;

                midData[j] = (l + r) * 0.5f;
                sideData[j] = (l - r) * 0.5f;
            }

            window->multiplyWithWindowingTable(midData.data(), fftSize);
            window->multiplyWithWindowingTable(sideData.data(), fftSize);
            forwardFFT->performFrequencyOnlyForwardTransform(midData.data());
            forwardFFT->performFrequencyOnlyForwardTransform(sideData.data());

            for (int j = 0; j < numBins; ++j)
            {
                if (j == 0)
                {
                    midData[j] = 0.0f;
                    sideData[j] = 0.0f;
                }

                float midMag = midData[j] * windowCorrection;
                float sideMag = sideData[j] * windowCorrection;
                float stereoPower = (midMag * midMag) + (sideMag * sideMag);
                float stereoMag = std::sqrt(stereoPower);

                accMid[j] += midMag * midMag;
                accSide[j] += sideMag * sideMag;
                accStereo[j] += stereoPower;

                if (midMag > rawMaxMidMag[j]) rawMaxMidMag[j] = midMag;

                if (sideMag > rawMaxSideMag[j]) rawMaxSideMag[j] = sideMag;
                
                if (stereoMag > rawMaxStereoMag[j]) rawMaxStereoMag[j] = stereoMag;
            }

            numBlocks++;
        }

        if (numBlocks > 0)
        {
            rawAvgMidMag = calculateAverageMagnitude(accMid, numBlocks);
            rawAvgSideMag = calculateAverageMagnitude(accSide, numBlocks);
            rawAvgStereoMag = calculateAverageMagnitude(accStereo, numBlocks);

            reprocessSmoothing();
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour::fromFloatRGBA(0.12f, 0.14f, 0.13f, 1.0f));

        auto area = getAnalysisArea();

        drawLegend(g);
        drawGrid(g, area);

        if (currentSampleRate <= 0) return;

        g.saveState();
        g.reduceClipRegion(area.toNearestInt());

        if (settings.showStereoAvg) drawPixelPerfectLayer(g, avgStereoDB, area, juce::Colours::lightgreen, true);

        if (settings.showStereoMax) drawPixelPerfectLayer(g, maxStereoDB, area, juce::Colours::lightgreen.withAlpha(0.7f), false);

        if (settings.showMidAvg)    drawPixelPerfectLayer(g, avgMidDB, area, juce::Colours::gold, true);

        if (settings.showMidMax)    drawPixelPerfectLayer(g, maxMidDB, area, juce::Colours::gold.withAlpha(0.7f), false);

        if (settings.showSideAvg)   drawPixelPerfectLayer(g, avgSideDB, area, juce::Colours::dodgerblue, true);

        if (settings.showSideMax)
        {
            if (settings.showMidMax && !maxMidDB.empty()) drawSideMaxSplitColor(g, maxSideDB, maxMidDB, area);
            else drawPixelPerfectLayer(g, maxSideDB, area, juce::Colours::dodgerblue.withAlpha(0.7f), false);
        }

        if (settings.showMidAvg && settings.showSideAvg) drawOverlapWarning(g, avgMidDB, avgSideDB, area);

        g.restoreState();

        if (isMouseOverGraph && area.contains(mousePos.toFloat())) drawHoverOverlay(g, area);
    }

private:

    int fftOrder;
    int fftSize;
    std::unique_ptr<juce::dsp::FFT> forwardFFT;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window;
    std::vector<float> accMid, accSide, accStereo;
    std::vector<float> rawAvgMidMag, rawAvgSideMag, rawAvgStereoMag;
    std::vector<float> rawMaxMidMag, rawMaxSideMag, rawMaxStereoMag;
    std::vector<float> avgMidDB, avgSideDB, avgStereoDB;
    std::vector<float> maxMidDB, maxSideDB, maxStereoDB;
    double currentSampleRate = 0.0;
    float currentSmoothingFactor = 0.3f;
    juce::Point<int> mousePos;
    bool isMouseOverGraph = false;

    juce::Rectangle<float> getAnalysisArea()
    {
        auto bounds = getLocalBounds().toFloat();
        
        return bounds.withTrimmedTop(30).withTrimmedLeft(30).withTrimmedRight(30).withTrimmedBottom(30);
    }

    std::vector<float> calculateAverageMagnitude(const std::vector<float>& accumulated, int numBlocks)
    {
        std::vector<float> result(accumulated.size());

        float norm = 1.0f / (float)numBlocks;

        for (size_t i = 0; i < accumulated.size(); ++i)
        {
            result[i] = std::sqrt(accumulated[i] * norm);
        }

        return result;
    }

    void reprocessSmoothing()
    {
        auto tempAvgMid = rawAvgMidMag; auto tempAvgSide = rawAvgSideMag; auto tempAvgStereo = rawAvgStereoMag;
        auto tempMaxMid = rawMaxMidMag; auto tempMaxSide = rawMaxSideMag; auto tempMaxStereo = rawMaxStereoMag;

        applyMagnitudeSmoothing(tempAvgMid); applyMagnitudeSmoothing(tempAvgSide); applyMagnitudeSmoothing(tempAvgStereo);
        applyMagnitudeSmoothing(tempMaxMid); applyMagnitudeSmoothing(tempMaxSide); applyMagnitudeSmoothing(tempMaxStereo);

        float slope = 4.5f;

        avgMidDB = convertToDbWithSlope(tempAvgMid, slope);
        avgSideDB = convertToDbWithSlope(tempAvgSide, slope);
        avgStereoDB = convertToDbWithSlope(tempAvgStereo, slope);
        maxMidDB = convertToDbWithSlope(tempMaxMid, slope);
        maxSideDB = convertToDbWithSlope(tempMaxSide, slope);
        maxStereoDB = convertToDbWithSlope(tempMaxStereo, slope);

        repaint();
    }

    void applyMagnitudeSmoothing(std::vector<float>& data)
    {
        if (data.empty() || currentSampleRate <= 0) return;

        if (currentSmoothingFactor <= 0.001f) return;

        auto temp = data;

        performSinglePassSmoothing(data, temp);
        performSinglePassSmoothing(temp, data);
    }

    void performSinglePassSmoothing(const std::vector<float>& input, std::vector<float>& output)
    {
        int size = (int)input.size();

        for (int i = 0; i < size; ++i)
        {
            float freq = (i * currentSampleRate) / fftSize;
            float bandwidth = freq * currentSmoothingFactor;

            if (bandwidth < 10.0f) bandwidth = 10.0f;

            int radius = (int)((bandwidth / currentSampleRate) * fftSize * 0.5f);

            if (radius < 1) radius = 1;

            int spaceRight = (size - 1) - i;
            int spaceLeft = i;
            int effectiveRadius = radius;

            if (effectiveRadius > spaceRight) effectiveRadius = spaceRight;

            if (effectiveRadius > spaceLeft)  effectiveRadius = spaceLeft;

            int start = i - effectiveRadius;
            int end = i + effectiveRadius;

            float sum = 0.0f;
            int count = 0;

            for (int k = start; k <= end; ++k)
            {
                sum += input[k];
                count++;
            }

            if (count > 0) output[i] = sum / count;
            else output[i] = input[i];
        }
    }

    std::vector<float> convertToDbWithSlope(const std::vector<float>& magData, float slope)
    {
        std::vector<float> dbData(magData.size());

        for (size_t i = 0; i < magData.size(); ++i)
        {
            float val = magData[i];

            if (val < 1e-9f) val = 1e-9f;

            float db = juce::Decibels::gainToDecibels(val / fftSize);
            float freq = (i * currentSampleRate) / fftSize;
            float effectiveFreq = juce::jlimit(20.0f, 20000.0f, freq);
            float tilt = slope * std::log2(effectiveFreq / 1000.0f);
            db += tilt;
            db += 3.0f;
            dbData[i] = db;
        }

        return dbData;
    }

    float getVisualDB(float freq, const std::vector<float>& data)
    {
        if (data.empty()) return -144.0f;

        float db = -144.0f;
        float nyquist = currentSampleRate * 0.5f;

        if (freq >= nyquist) db = data.back();
        else
        {
            float binPos = (freq / nyquist) * (data.size() - 1);
            int index = (int)binPos; float frac = binPos - index;

            if (index < 0) db = data.front();
            else if (index >= data.size() - 1) db = data.back();
            else db = data[index] * (1.0f - frac) + data[index + 1] * frac;
        }

        if (freq > 20000.0f)
        {
            float t = juce::jmap(freq, 20000.0f, 22000.0f, 0.0f, 1.0f);

            t = juce::jlimit(0.0f, 1.0f, t);
            db = db * (1.0f - t) + (-84.0f * t);
        }
        else if (freq < 20.0f)
        {
            float t = 1.0f - (freq / 20.0f);

            t = juce::jlimit(0.0f, 1.0f, t);
            db = db * (1.0f - t) + (-84.0f * t);
        }

        return db;
    }

    void drawOverlapWarning(juce::Graphics& g, const std::vector<float>& midDBs, const std::vector<float>& sideDBs, juce::Rectangle<float> bounds)
    {
        if (midDBs.empty() || sideDBs.empty()) return;

        g.setColour(juce::Colours::red.withAlpha(0.6f));

        for (int x = 1; x < bounds.getWidth(); ++x)
        {
            float normX = (float)x / bounds.getWidth();
            float freq = 20.0f * std::pow(20000.0f / 20.0f, normX);
            float midDB = getVisualDB(freq, midDBs);
            float sideDB = getVisualDB(freq, sideDBs);

            if (sideDB > midDB)
            {
                float midY = bounds.getY() + (juce::jmap(midDB, -84.0f, 0.0f, 1.0f, 0.0f) * bounds.getHeight());
                float sideY = bounds.getY() + (juce::jmap(sideDB, -84.0f, 0.0f, 1.0f, 0.0f) * bounds.getHeight());

                midY = juce::jlimit(bounds.getY(), bounds.getBottom(), midY);
                sideY = juce::jlimit(bounds.getY(), bounds.getBottom(), sideY);

                g.drawVerticalLine(bounds.getX() + x, sideY, midY);
            }
        }
    }

    void drawSideMaxSplitColor(juce::Graphics& g, const std::vector<float>& sideDBs, const std::vector<float>& midDBs, juce::Rectangle<float> bounds)
    {
        if (sideDBs.empty() || midDBs.empty()) return;

        juce::Path normalPath;
        juce::Path alertPath;

        auto getY = [&](int xPixel) -> float
        {
            float normX = (float)xPixel / bounds.getWidth();
            float freq = 20.0f * std::pow(20000.0f / 20.0f, normX);
            float db = getVisualDB(freq, sideDBs);

            if (db < -84.0f) db = -84.0f;

            if (db > 0.0f) db = 0.0f;

            float normY = juce::jmap(db, -84.0f, 0.0f, 1.0f, 0.0f);

            return bounds.getY() + (normY * bounds.getHeight());
        };

        float currentY = getY(0);
        float currentX = bounds.getX();
        float startFreq = 20.0f;
        float startSideVal = getVisualDB(startFreq, sideDBs);
        float startMidVal = getVisualDB(startFreq, midDBs);
        bool prevWasAlert = (startSideVal > startMidVal);

        if (prevWasAlert) alertPath.startNewSubPath(currentX, currentY);
        else normalPath.startNewSubPath(currentX, currentY);

        for (int x = 1; x < bounds.getWidth(); ++x)
        {
            float normX = (float)x / bounds.getWidth();
            float freq = 20.0f * std::pow(20000.0f / 20.0f, normX);
            float sideVal = getVisualDB(freq, sideDBs);
            float midVal = getVisualDB(freq, midDBs);
            bool isAlert = (sideVal > midVal);
            float nextX = bounds.getX() + x;
            float nextY = getY(x);

            if (isAlert)
            {
                if (!prevWasAlert) alertPath.startNewSubPath(currentX, currentY);
                
                alertPath.lineTo(nextX, nextY);
            }
            else
            {
                if (prevWasAlert) normalPath.startNewSubPath(currentX, currentY);
                
                normalPath.lineTo(nextX, nextY);
            }

            prevWasAlert = isAlert;
            currentX = nextX;
            currentY = nextY;
        }

        g.setColour(juce::Colours::dodgerblue);
        g.strokePath(normalPath, juce::PathStrokeType(1.2f));
        g.setColour(juce::Colours::red);
        g.strokePath(alertPath, juce::PathStrokeType(1.2f));
    }

    void drawPixelPerfectLayer(juce::Graphics& g, const std::vector<float>& dbs, juce::Rectangle<float> bounds, juce::Colour baseColor, bool isFilled)
    {
        if (dbs.empty()) return;

        juce::Path path;
        float minDB = -84.0f; float maxDB = 0.0f;
        path.startNewSubPath(bounds.getX(), bounds.getBottom());
        float startFreq = 20.0f;
        float startDB = getVisualDB(startFreq, dbs);
        float startY = bounds.getY() + (juce::jmap(startDB, minDB, maxDB, 1.0f, 0.0f) * bounds.getHeight());
        path.lineTo(bounds.getX(), startY);

        for (int x = 1; x < bounds.getWidth(); ++x)
        {
            float normX = (float)x / bounds.getWidth();
            float freq = 20.0f * std::pow(20000.0f / 20.0f, normX);
            float db = getVisualDB(freq, dbs);

            if (db < minDB) db = minDB;

            float normY = juce::jmap(db, minDB, maxDB, 1.0f, 0.0f);
            normY = juce::jlimit(0.0f, 1.0f, normY);
            path.lineTo(bounds.getX() + x, bounds.getY() + (normY * bounds.getHeight()));
        }

        path.lineTo(bounds.getRight(), bounds.getBottom());
        path.lineTo(bounds.getX(), bounds.getBottom());
        path.closeSubPath();

        if (isFilled)
        {
            g.setColour(baseColor.withAlpha(0.4f)); g.fillPath(path);
            g.setColour(baseColor.withAlpha(0.9f)); g.strokePath(path, juce::PathStrokeType(1.5f));
        }
        else g.setColour(baseColor); g.strokePath(path, juce::PathStrokeType(1.2f));
    }

    float getXForFrequency(float freq, float width)
    {
        float minFreq = 20.0f; float maxFreq = 20000.0f;

        if (freq < minFreq) freq = minFreq;
        
        if (freq > maxFreq) freq = maxFreq;

        return (float)(std::log10(freq / minFreq) / std::log10(maxFreq / minFreq)) * width;
    }

    void drawGrid(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        float freqs[] = {20, 30, 40, 50, 60, 80, 100, 200, 300, 400, 500, 600, 800, 1000, 2000, 3000, 4000, 5000, 6000, 8000, 10000, 15000, 20000};

        g.setFont(10.0f);

        for (float f : freqs)
        {
            float xOffset = getXForFrequency(f, bounds.getWidth());
            float xPos = bounds.getX() + xOffset;

            g.setColour(juce::Colours::white.withAlpha(0.18f));
            g.drawVerticalLine((int)xPos, bounds.getY(), bounds.getBottom());
            juce::String label;

            if (f >= 1000) label = (f == 10000 || f == 20000) ? juce::String(f / 1000) + "k" : juce::String(f / 1000.0f, 1) + "k";
            else label = juce::String((int)f);

            if (f == 20000.0f) label += " Hz";

            g.setColour(juce::Colours::lightgrey);
            g.drawText(label, (int)xPos - 25, (int)bounds.getBottom() + 2, 50, 15, juce::Justification::centredTop);
        }

        float minDB = -84.0f;
        float maxDB = 0.0f;

        for (float db = 0.0f; db >= -84.0f; db -= 6.0f)
        {
            float normY = juce::jmap(db, minDB, maxDB, 1.0f, 0.0f);
            float yPos = bounds.getY() + (normY * bounds.getHeight());

            g.setColour(juce::Colours::white.withAlpha(0.18f));
            g.drawHorizontalLine((int)yPos, bounds.getX(), bounds.getRight());

            juce::String label = juce::String((int)db);

            if (db == 0.0f) label += " dB";

            g.setColour(juce::Colours::lightgrey);
            g.drawText(label, (int)bounds.getX() - 48, (int)yPos - 6, 45, 12, juce::Justification::centredRight);
        }

        g.setColour(juce::Colours::grey);
        g.drawRect(bounds, 1.0f);
    }

    void drawHoverOverlay(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        float mouseX = juce::jlimit(bounds.getX(), bounds.getRight(), (float)mousePos.x);
        float mouseY = juce::jlimit(bounds.getY(), bounds.getBottom(), (float)mousePos.y);

        g.setColour(juce::Colours::white.withAlpha(0.5f));
        g.drawVerticalLine((int)mouseX, bounds.getY(), bounds.getBottom());
        g.drawHorizontalLine((int)mouseY, bounds.getX(), bounds.getRight());

        float normX = (mouseX - bounds.getX()) / bounds.getWidth();
        float freq = 20.0f * std::pow(20000.0f / 20.0f, normX);
        float normY = (mouseY - bounds.getY()) / bounds.getHeight();
        float db = 0.0f - (normY * (0.0f - (-84.0f)));

        juce::String text = juce::String((int)freq) + " Hz | " + juce::String(db, 1) + " dB";

        int boxW = 110;
        int boxH = 20;
        int boxX = (int)mouseX + 10;
        int boxY = (int)mouseY - 25;

        if (boxX + boxW > bounds.getRight()) boxX = (int)mouseX - boxW - 10;

        if (boxY < bounds.getY()) boxY = (int)mouseY + 10;

        g.setColour(juce::Colours::black.withAlpha(0.8f));
        g.fillRoundedRectangle((float)boxX, (float)boxY, (float)boxW, (float)boxH, 4.0f);
        g.setColour(juce::Colours::white);
        g.drawRoundedRectangle((float)boxX, (float)boxY, (float)boxW, (float)boxH, 4.0f, 1.0f);
        g.setFont(12.0f);
        g.drawText(text, boxX, boxY, boxW, boxH, juce::Justification::centred);
        g.fillEllipse(mouseX - 3, mouseY - 3, 6, 6);
    }

    void drawLegend(juce::Graphics& g)
    {
        int x = getWidth() - 250;
        int y = 5;

        g.setFont(12.0f);

        auto drawItem = [&](juce::String text, juce::Colour col, int& xPos)
        {
            g.setColour(col);
            g.fillRect(xPos, y + 4, 10, 10);
            g.setColour(juce::Colours::lightgrey);
            g.drawText(text, xPos + 14, y, 40, 18, juce::Justification::left);

            xPos += 60;
        };

        int currentX = getWidth() - 200;

        if (settings.showMidAvg || settings.showMidMax) drawItem("MID", juce::Colours::gold, currentX);

        if (settings.showSideAvg || settings.showSideMax)  drawItem("SIDE", juce::Colours::dodgerblue, currentX);

        if (settings.showStereoAvg || settings.showStereoMax) drawItem("TOTAL", juce::Colours::lightgreen, currentX);
    }
};