#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================

void LookAndFeel::drawRotarySlider(juce::Graphics &g, int x, int y, int width, int height, float sliderPosProportional,
                                   float rotaryStartAngle, float rotaryEndAngle, juce::Slider &slider)
{
    using namespace juce;

    auto bounds = Rectangle<float>(x, y, width, height);
    
    g.setColour(Colour(46u, 46u, 46u));
    g.fillEllipse(bounds);

    g.setColour(Colour(255u, 255u, 255u));  
    g.drawEllipse(bounds, 1.0f);

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

        str = juce::String(val, (addK ? 2 : 0));
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

ResponseCurveComponent::ResponseCurveComponent(AudioPluginAudioProcessor &p) : processorRef(p)
{
    const auto &params = processorRef.getParameters();

    for (auto param : params)
    {
        param -> addListener(this);
    }

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
    if (parametersChanged.compareAndSetBool(false, true))
    {
        // Update the monochain
        auto chainSettings = getChainSettings(processorRef.apvts);
        auto peakCoefficients = makePeakFilter(chainSettings, processorRef.getSampleRate());
        auto lowCutCoefficients = makeLowCutFilter(chainSettings, processorRef.getSampleRate());
        auto highCutCoefficients = makeHighCutFilter(chainSettings, processorRef.getSampleRate());

        updateCoefficients(monoChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);
        updateCutFilter(monoChain.get<ChainPositions::LowCut>(), lowCutCoefficients, chainSettings.lowCutSlope);
        updateCutFilter(monoChain.get<ChainPositions::HighCut>(), highCutCoefficients, chainSettings.highCutSlope);

        // Signal a repaint
        repaint();
    }
}

void ResponseCurveComponent::paint (juce::Graphics &g)
{
    using namespace juce;

    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (Colours::black);

    auto responseArea = getLocalBounds();
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

    g.setColour(Colours::orange);
    g.drawRoundedRectangle(responseArea.toFloat(), 4.0f, 1.0f);

    g.setColour(Colours::white);
    g.strokePath(responseCurve, PathStrokeType(2.0f));
}

AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor &p)
    : AudioProcessorEditor (&p), processorRef (p), 

    peakFreqSlider(*processorRef.apvts.getParameter("Peak Freq"), "Hz"),
    peakGainSlider(*processorRef.apvts.getParameter("Peak Gain"), "dB"),
    peakQualitySlider(*processorRef.apvts.getParameter("Peak Quality"), ""),
    lowCutFreqSlider(*processorRef.apvts.getParameter("Low Cut Freq"), "Hz"),
    highCutFreqSlider(*processorRef.apvts.getParameter("High Cut Freq"), "Hz"),
    lowCutSlopeSlider(*processorRef.apvts.getParameter("Low Cut Slope"), "dB/oct"),
    highCutSlopeSlider(*processorRef.apvts.getParameter("High Cut Slop"), "dB/oct"),

    responseCurveComponent(processorRef),
    peakFreqSliderAttachment(processorRef.apvts, "Peak Freq", peakFreqSlider),
    peakGainSliderAttachment(processorRef.apvts, "Peak Gain", peakGainSlider),
    peakQualitySliderAttachment(processorRef.apvts, "Peak Quality", peakQualitySlider),
    lowCutFreqSliderAttachment(processorRef.apvts, "Low Cut Freq", lowCutFreqSlider),
    lowCutSlopeSliderAttachment(processorRef.apvts, "Low Cut Slope", lowCutSlopeSlider),
    highCutFreqSliderAttachment(processorRef.apvts, "High Cut Freq", highCutFreqSlider),
    highCutSlopeSliderAttachment(processorRef.apvts, "High Cut Slope", highCutSlopeSlider)
{
    juce::ignoreUnused (processorRef);
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.

    for (auto *comp : getComps())
    {
        addAndMakeVisible(comp);
    }

    setSize (600, 400);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
  
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint (juce::Graphics &g)
{
    using namespace juce;

    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (Colours::black);
}

void AudioPluginAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..

    auto bounds = getLocalBounds();
    auto responseArea = bounds.removeFromTop(bounds.getHeight() * 0.33);

    responseCurveComponent.setBounds(responseArea);

    auto lowCutArea = bounds.removeFromLeft(bounds.getWidth() * 0.33);
    auto highCutArea = bounds.removeFromRight(bounds.getWidth() * 0.5);

    lowCutFreqSlider.setBounds(lowCutArea.removeFromTop(bounds.getHeight() * 0.5));
    lowCutSlopeSlider.setBounds(lowCutArea);
    
    highCutFreqSlider.setBounds(highCutArea.removeFromTop(bounds.getHeight() * 0.5));
    highCutSlopeSlider.setBounds(highCutArea);

    peakFreqSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.33));
    peakGainSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.5));
    peakQualitySlider.setBounds(bounds);
}

std::vector<juce::Component*> AudioPluginAudioProcessorEditor::getComps()
{
    return {&peakFreqSlider, &peakGainSlider, &peakQualitySlider, &lowCutFreqSlider, &highCutFreqSlider, 
            &lowCutSlopeSlider, &highCutSlopeSlider, &responseCurveComponent};
}
