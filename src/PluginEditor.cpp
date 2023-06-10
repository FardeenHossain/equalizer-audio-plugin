#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================

void LookAndFeel::drawRotarySlider(juce::Graphics &g, int x, int y, int width, int height, float sliderPosProportional,
                                   float rotaryStartAngle, float rotaryEndAngle, juce::Slider &slider)
{
    using namespace juce;

    auto bounds = Rectangle<float>(x, y, width, height);
    
    g.setColour(Colour(33u, 33u, 33u));
    g.fillEllipse(bounds);

    g.setColour(Colour(187u, 134u, 252u));  
    g.drawEllipse(bounds, 2.0f);

    if (auto *rswl = dynamic_cast<RotarySliderWithLabels*>(&slider))
    {
        auto center = bounds.getCentre();

        Path p;
        Rectangle<float> r;

        r.setLeft(center.getX() - 2);
        r.setRight(center.getX() + 2);
        r.setTop(bounds.getY());
        r.setBottom(center.getY() - rswl -> getTextHeight() * 1.5);

        p.addRoundedRectangle(r, 2.0f);

        jassert(rotaryStartAngle < rotaryEndAngle);

        auto sliderAngRad = jmap(sliderPosProportional, 0.0f, 1.0f, rotaryStartAngle, rotaryEndAngle);

        p.applyTransform(AffineTransform().rotated(sliderAngRad, center.getX(), center.getY()));
        g.fillPath(p);

        g.setFont(rswl -> getTextHeight());
        
        auto text = rswl -> getDisplayString();
        auto strWidth = g.getCurrentFont().getStringWidth(text);

        r.setSize(strWidth + 4, rswl -> getTextHeight() + 2);
        r.setCentre(bounds.getCentre());

        g.setColour(Colours::white);  
        g.drawFittedText(text, r.toNearestInt(), juce::Justification::centred, 1);
    }
}

void LookAndFeel::drawToggleButton(juce::Graphics &g, juce::ToggleButton &toggleButton, 
                                   bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    using namespace juce;

    Path powerButton;

    auto bounds = toggleButton.getLocalBounds();
    auto size = jmin(bounds.getWidth(), bounds.getHeight()) - 6;
    auto r = bounds.withSizeKeepingCentre(size, size).toFloat();

    float ang = 30.0f;
    size -= 6;

    powerButton.addCentredArc(r.getCentreX(), r.getCentreY(), size * 0.5, size * 0.5, 0.0f, degreesToRadians(ang), 
                              degreesToRadians(360.0f - ang), true);

    powerButton.startNewSubPath(r.getCentreX(), r.getY());
    powerButton.lineTo(r.getCentre());

    PathStrokeType pst(2.0f, PathStrokeType::JointStyle::curved);

    auto color = toggleButton.getToggleState() ? Colour(33u, 33u, 33u) : Colour(3u, 218u, 197u);

    g.setColour(color);
    g.strokePath(powerButton, pst);
}

//==============================================================================

void RotarySliderWithLabels::paint(juce::Graphics &g)
{
    using namespace juce;

    auto startAng = degreesToRadians(180.0f + 45.0f);
    auto endAng = degreesToRadians(180.0f - 45.0f) + MathConstants<float>::twoPi;

    auto range = getRange();
    auto sliderBounds = getSliderBounds();

    getLookAndFeel().drawRotarySlider(g, sliderBounds.getX(), sliderBounds.getY(), 
        sliderBounds.getWidth(), sliderBounds.getHeight(), 
        jmap(getValue(), range.getStart(), range.getEnd(), 0.0, 1.0), startAng, endAng, *this);

    auto center = sliderBounds.toFloat().getCentre();
    auto radius = sliderBounds.getWidth() * 0.5f;

    g.setColour(Colours::grey);  
    g.setFont(getTextHeight());

    auto numChoices = labels.size();

    for (int i = 0; i < numChoices; i++)
    {
        auto pos = labels[i].pos;

        jassert(0.0f <= pos);
        jassert(pos <= 1.0f);

        auto ang = jmap(pos, 0.0f, 1.0f, startAng, endAng);
        auto c = center.getPointOnCircumference(radius + getTextHeight() * 0.5f + 1, ang); 
        auto str = labels[i].label; 

        Rectangle<float> r;

        r.setSize(g.getCurrentFont().getStringWidth(str), getTextHeight());
        r.setCentre(c);
        r.setY(r.getY() + getTextHeight());

        g.drawFittedText(str, r.toNearestInt(), juce::Justification::centred, 1);
    }
}

juce::Rectangle<int> RotarySliderWithLabels::getSliderBounds() const
{
    auto bounds = getLocalBounds();
    auto size = juce::jmin(bounds.getWidth(), bounds.getHeight());

    size -= getTextHeight() * 2;
    juce::Rectangle<int> r;

    r.setSize(size, size);
    r.setCentre(bounds.getCentreX(), 0);
    r.setY(2);

    return r;
}

juce::String RotarySliderWithLabels::getDisplayString() const
{
    if (auto *choiceParam = dynamic_cast<juce::AudioParameterChoice*>(param)){
        return choiceParam -> getCurrentChoiceName();
    }

    juce::String str;
    bool addK = false;

    if (auto *floatParam = dynamic_cast<juce::AudioParameterFloat*>(param))
    {
        float val = getValue();

        if (val > 999.0f)
        {
            val /= 1000.0f;
            addK = true;
        }

        str = juce::String(val, (addK ? 2 : 1));
    }

    if (suffix.isNotEmpty())
    {
        str << " ";

        if (addK){
            str << "k";
        }

        str << suffix;
    }

    return str;
}
 
//==============================================================================

ResponseCurveComponent::ResponseCurveComponent(AudioPluginAudioProcessor &p) : processorRef(p),
    leftChannelFifo(&processorRef.leftChannelFifo)
{
    const auto &params = processorRef.getParameters();

    for (auto param : params)
    {
        param -> addListener(this);
    }

    leftChannelFFTDataGenerator.changeOrder(FFTOrder::order2048);
    monoBuffer.setSize(1, leftChannelFFTDataGenerator.getFFTSize());

    updateChain();
    startTimerHz(60);
}

ResponseCurveComponent::~ResponseCurveComponent()
{
    const auto &params = processorRef.getParameters();

    for (auto param : params)
    {
        param -> removeListener(this);
    }
}

void ResponseCurveComponent::parameterValueChanged(int parameterIndex, float newValue)
{
    parametersChanged.set(true);
}

void ResponseCurveComponent::timerCallback()
{
    juce::AudioBuffer<float> tempIncomingBuffer;

    while (leftChannelFifo -> getNumCompleteBuffersAvailable() > 0)
    {
        if (leftChannelFifo -> getAudioBuffer(tempIncomingBuffer))
        {
            auto size = tempIncomingBuffer.getNumSamples();

            juce::FloatVectorOperations::copy(monoBuffer.getWritePointer(0, 0), monoBuffer.getReadPointer(0, size), 
                                              monoBuffer.getNumSamples() - size);

            juce::FloatVectorOperations::copy(monoBuffer.getWritePointer(0, monoBuffer.getNumSamples() - size),
                                              tempIncomingBuffer.getReadPointer(0, 0), size);
            
            leftChannelFFTDataGenerator.produceFFTDataForRendering(monoBuffer, -48.0f);
        }
    }

    // If there are FFT data buffers to pull
    // If we can pull a buffer
    // Generate a path
    auto fftBounds = getRenderArea().toFloat();
    auto fftSize = leftChannelFFTDataGenerator.getFFTSize();

    // Bin width -> 48,000/2,048 = 23Hz
    const auto binWidth = processorRef.getSampleRate() / (double) fftSize;

    while (leftChannelFFTDataGenerator.getNumAvailableFFTDataBlocks()) 
    {
        std::vector<float> fftData;

        if (leftChannelFFTDataGenerator.getFFTData(fftData))
        {
            pathProducer.generatePath(fftData, fftBounds, fftSize, binWidth, -48.0f);
        }
    }

    // While there are paths that we can pull
    // Pull as many as we can
    // Display the most recent path
    while (pathProducer.getNumPathsAvailable())
    {
        pathProducer.getPath(leftChannelFFTPath);
    }

    if (parametersChanged.compareAndSetBool(false, true))
    {
        // Update the monochain
        updateChain();

        // Signal a repaint
    }

    repaint();
}

void ResponseCurveComponent::updateChain()
{
    auto chainSettings = getChainSettings(processorRef.apvts);

    monoChain.setBypassed<ChainPositions::LowCut>(chainSettings.lowCutBypassed);
    monoChain.setBypassed<ChainPositions::Peak>(chainSettings.peakBypassed);
    monoChain.setBypassed<ChainPositions::HighCut>(chainSettings.highCutBypassed);

    auto peakCoefficients = makePeakFilter(chainSettings, processorRef.getSampleRate());
    auto lowCutCoefficients = makeLowCutFilter(chainSettings, processorRef.getSampleRate());
    auto highCutCoefficients = makeHighCutFilter(chainSettings, processorRef.getSampleRate());

    updateCoefficients(monoChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);
    updateCutFilter(monoChain.get<ChainPositions::LowCut>(), lowCutCoefficients, chainSettings.lowCutSlope);
    updateCutFilter(monoChain.get<ChainPositions::HighCut>(), highCutCoefficients, chainSettings.highCutSlope);
}

void ResponseCurveComponent::paint (juce::Graphics &g)
{
    using namespace juce;

    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (Colour(18u, 18u, 18u));

    auto responseArea = getRenderArea();
    auto w = responseArea.getWidth();

    auto &lowcut = monoChain.get<ChainPositions::LowCut>();
    auto &peak = monoChain.get<ChainPositions::Peak>();
    auto &highcut = monoChain.get<ChainPositions::HighCut>();

    auto sampleRate = processorRef.getSampleRate();

    std::vector<double> mags;
    mags.resize(w);

    for (int i = 0; i < w; i++)
    {
        double mag = 1.0f;
        auto freq = mapToLog10(double(i) / double(w), 20.0, 20000.0);

        if (!monoChain.isBypassed<ChainPositions::Peak>()){
            mag *= peak.coefficients -> getMagnitudeForFrequency(freq, sampleRate);
        }

        if (!monoChain.isBypassed<ChainPositions::LowCut>())
        {
            if (!lowcut.isBypassed<0>()){
                mag *= lowcut.get<0>().coefficients -> getMagnitudeForFrequency(freq, sampleRate);
            }
            if (!lowcut.isBypassed<1>()){
                mag *= lowcut.get<1>().coefficients -> getMagnitudeForFrequency(freq, sampleRate);
            }
            if (!lowcut.isBypassed<2>()){
                mag *= lowcut.get<2>().coefficients -> getMagnitudeForFrequency(freq, sampleRate);
            }
            if (!lowcut.isBypassed<3>()){
                mag *= lowcut.get<3>().coefficients -> getMagnitudeForFrequency(freq, sampleRate);
            }
        }

        if (!monoChain.isBypassed<ChainPositions::HighCut>())
        {
            if (!highcut.isBypassed<0>()){
                mag *= highcut.get<0>().coefficients -> getMagnitudeForFrequency(freq, sampleRate);
            }
            if (!highcut.isBypassed<1>()){
                mag *= highcut.get<1>().coefficients -> getMagnitudeForFrequency(freq, sampleRate);
            }
            if (!highcut.isBypassed<2>()){
                mag *= highcut.get<2>().coefficients -> getMagnitudeForFrequency(freq, sampleRate);
            }
            if (!highcut.isBypassed<3>()){
                mag *= highcut.get<3>().coefficients -> getMagnitudeForFrequency(freq, sampleRate);
            }
        }

        mags[i] = Decibels::gainToDecibels(mag);
    }

    Path responseCurve;

    const double outputMin = responseArea.getBottom();
    const double outputMax = responseArea.getY();

    auto map = [outputMin, outputMax](double input)
    {
        return jmap(input, -24.0, 24.0, outputMin, outputMax);
    };

    responseCurve.startNewSubPath(responseArea.getX(), map(mags.front()));

    for (size_t i = 1; i < mags.size(); i++)
    {
        responseCurve.lineTo(responseArea.getX() + i, map(mags[i]));
    }

    leftChannelFFTPath.applyTransform(AffineTransform().translation(responseArea.getX(), responseArea.getY()));

    g.setColour(Colours::black);  
    g.fillRoundedRectangle(getRenderArea().toFloat(), 0.0f);

    g.drawImage(background, getRenderArea().toFloat());

    g.setColour(Colour(187u, 134u, 252u));  
    g.strokePath(leftChannelFFTPath, PathStrokeType(2.0f));

    g.setColour(Colours::white);
    g.strokePath(responseCurve, PathStrokeType(2.0f));
}

void ResponseCurveComponent::resized()
{
    using namespace juce;

    background = Image(Image::PixelFormat::RGB, getWidth(), getHeight(), true);

    Graphics g(background);
    
    Array<float> freqs
    {
        10, 20, 30, 40, 50, 60, 70, 80, 90, 
        100, 200, 300, 400, 500, 600, 700, 800, 900,
        1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000,
        20000
    };

    Array<float> gain{-24, -18, -12, -6, 0, 6, 12, 18, 24};
  
    g.setColour(Colour(33u, 33u, 33u));

    for (auto f : freqs)
    {
        auto normX = mapFromLog10(f, 20.0f, 20000.0f);
        g.drawVerticalLine(getWidth()*normX, 0.0f, getHeight());
    }

    for (auto gDb : gain)
    {
        auto y = jmap(gDb, -24.0f, 24.0f, float(getHeight()), 0.0f);
        g.setColour(gDb == 0.0f ? Colour(3u, 218u, 197u) : Colour(33u, 33u, 33u));
        g.drawHorizontalLine(y, 0, getWidth());
    }
}

juce::Rectangle<int> ResponseCurveComponent::getRenderArea()
{
    auto bounds = getLocalBounds();
    
    bounds.removeFromTop(10);
    bounds.removeFromBottom(10);
    bounds.removeFromLeft(20);
    bounds.removeFromRight(20);

    return bounds;
}

//==============================================================================

AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor &p) : 
    AudioProcessorEditor (&p), processorRef (p), 

    peakFreqSlider(*processorRef.apvts.getParameter("Peak Freq"), "Hz"),
    peakGainSlider(*processorRef.apvts.getParameter("Peak Gain"), "dB"),
    peakQualitySlider(*processorRef.apvts.getParameter("Peak Quality"), ""),
    lowCutFreqSlider(*processorRef.apvts.getParameter("Low Cut Freq"), "Hz"),
    highCutFreqSlider(*processorRef.apvts.getParameter("High Cut Freq"), "Hz"),
    lowCutSlopeSlider(*processorRef.apvts.getParameter("Low Cut Slope"), "dB/oct"),
    highCutSlopeSlider(*processorRef.apvts.getParameter("High Cut Slope"), "dB/oct"),

    responseCurveComponent(processorRef),
    peakFreqSliderAttachment(processorRef.apvts, "Peak Freq", peakFreqSlider),
    peakGainSliderAttachment(processorRef.apvts, "Peak Gain", peakGainSlider),
    peakQualitySliderAttachment(processorRef.apvts, "Peak Quality", peakQualitySlider),
    lowCutFreqSliderAttachment(processorRef.apvts, "Low Cut Freq", lowCutFreqSlider),
    lowCutSlopeSliderAttachment(processorRef.apvts, "Low Cut Slope", lowCutSlopeSlider),
    highCutFreqSliderAttachment(processorRef.apvts, "High Cut Freq", highCutFreqSlider),
    highCutSlopeSliderAttachment(processorRef.apvts, "High Cut Slope", highCutSlopeSlider),

    lowCutBypassButtonAttachment(processorRef.apvts, "Low Cut Bypassed", lowCutBypassButton),
    peakBypassButtonAttachment(processorRef.apvts, "Peak Bypassed", peakBypassButton),
    HighCutBypassButtonAttachment(processorRef.apvts, "High Cut Bypassed", HighCutBypassButton)
{
    juce::ignoreUnused (processorRef);
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.

    peakFreqSlider.labels.add({0.0f, "20"});
    peakFreqSlider.labels.add({1.0f, "20k"});

    peakGainSlider.labels.add({0.0f, "-24"});
    peakGainSlider.labels.add({1.0f, "+24"});

    peakQualitySlider.labels.add({0.0f, "0.2"});
    peakQualitySlider.labels.add({1.0f, "12"});

    lowCutFreqSlider.labels.add({0.0f, "20"});
    lowCutFreqSlider.labels.add({1.0f, "20k"});

    lowCutSlopeSlider.labels.add({0.0f, "12"});
    lowCutSlopeSlider.labels.add({1.0f, "48"});

    highCutFreqSlider.labels.add({0.0f, "20"});
    highCutFreqSlider.labels.add({1.0f, "20k"});

    highCutSlopeSlider.labels.add({0.0f, "12"});
    highCutSlopeSlider.labels.add({1.0f, "48"});

    for (auto *comp : getComps())
    {
        addAndMakeVisible(comp);
    }

    peakBypassButton.setLookAndFeel(&lnf);
    lowCutBypassButton.setLookAndFeel(&lnf);
    HighCutBypassButton.setLookAndFeel(&lnf);

    setSize (600, 480);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    peakBypassButton.setLookAndFeel(nullptr);
    lowCutBypassButton.setLookAndFeel(nullptr);
    HighCutBypassButton.setLookAndFeel(nullptr);
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint (juce::Graphics &g)
{
    using namespace juce;

    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll(Colour(18u, 18u, 18u));

    auto bounds = getLocalBounds();
    auto appBarArea = bounds.removeFromTop(40);

    g.setColour(Colour(33u, 33u, 33u));
    g.fillRect(appBarArea);

    g.setColour(juce::Colours::white);

    appBarArea.removeFromLeft(20);
    g.setFont(16);

    g.drawFittedText("Equalizer Audio Plugin", appBarArea, juce::Justification::centredLeft, 1);
    g.drawFittedText("Low Cut", lowCutSlopeSlider.getBounds(), juce::Justification::centredBottom, 1);
    g.drawFittedText("Peak", peakQualitySlider.getBounds(), juce::Justification::centredBottom, 1);
    g.drawFittedText("High Cut", highCutSlopeSlider.getBounds(), juce::Justification::centredBottom, 1);
}

void AudioPluginAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..

    auto bounds = getLocalBounds();

    bounds.removeFromTop(40);
    bounds.removeFromBottom(5);
    
    float hRatio = 25.0f / 100.0f;
    auto responseArea = bounds.removeFromTop(bounds.getHeight() * hRatio);

    responseCurveComponent.setBounds(responseArea);
    bounds.removeFromTop(5);

    auto lowCutArea = bounds.removeFromLeft(bounds.getWidth() * 0.33);
    auto highCutArea = bounds.removeFromRight(bounds.getWidth() * 0.5);

    lowCutBypassButton.setBounds(lowCutArea.removeFromTop(25));
    lowCutFreqSlider.setBounds(lowCutArea.removeFromTop(bounds.getHeight() * 0.5));
    lowCutSlopeSlider.setBounds(lowCutArea);

    HighCutBypassButton.setBounds(highCutArea.removeFromTop(25));
    highCutFreqSlider.setBounds(highCutArea.removeFromTop(bounds.getHeight() * 0.5));
    highCutSlopeSlider.setBounds(highCutArea);

    peakBypassButton.setBounds(bounds.removeFromTop(25));
    peakFreqSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.33));
    peakGainSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.5));
    peakQualitySlider.setBounds(bounds);
}

std::vector<juce::Component*> AudioPluginAudioProcessorEditor::getComps()
{
    return {&peakFreqSlider, &peakGainSlider, &peakQualitySlider, 
            &lowCutFreqSlider, &highCutFreqSlider, &lowCutSlopeSlider, &highCutSlopeSlider, &responseCurveComponent, 
            &lowCutBypassButton, &peakBypassButton, &HighCutBypassButton};
}
