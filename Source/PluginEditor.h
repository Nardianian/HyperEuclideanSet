#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class AudioPluginAudioProcessorEditor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    //==============================================================================
    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    // Riferimento al processor ridenominato
    AudioPluginAudioProcessor& audioProcessor;

    juce::OwnedArray<juce::Label> labels;

    // Struttura GUI aggiornata con i nuovi parametri della logica
    struct GuiRow
    {
        juce::Label rowLabel;               // Labels "R1", "R2"...
        juce::ToggleButton activeBtn;       // ACTIVE
        juce::ToggleButton hyperBtn;        // HYPER
        juce::Slider stepsSlider;           // STEPS
        juce::Slider pulsesSlider;          // PULSES
        juce::Slider noteSlider;            // NOTE
        juce::Slider octaveSlider;          // OCTAVE
        juce::ComboBox modeBox;             // MODE (Selezione Fixed/Input/Scale/Chord)
        juce::ComboBox melModeBox;          // MEL (Selezione Continuum/Looped)
        juce::ToggleButton melStepBtn;
        juce::ComboBox typeBox;             // Selected Scale/Chords
        juce::Slider probSlider;            // PROBABILITY
        juce::Slider swingSlider;           // SWING
        juce::Slider gateSlider;            // GATE
        juce::Slider velSlider;             // VELOCITY (VOLUME)
        juce::TextButton randBtn;           // RANDOMIZE
        juce::ComboBox chanBox;             // MIDI CHANNEL
        juce::TextButton learnBtn;          // Tasto LRN
        juce::Slider mainVolSlider;         // NUOVO: Slider orizzontale per CC7
    };

    juce::OwnedArray<GuiRow> rows;
    
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachments;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachments;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachments;
    juce::OwnedArray<juce::ComboBox> portSelectors;
    std::unique_ptr<juce::TextButton> globalPlayBtn;
    std::unique_ptr<juce::Slider> globalBpmSlider;
    std::unique_ptr<juce::ToggleButton> globalClockBtn;
    juce::TooltipWindow tooltipWindow{ this }; // Gestore automatico dei fumetti (Tooltip)
    // Pulsanti Globali per Mappatura MIDI
    std::unique_ptr<juce::TextButton> saveMidiMapBtn, loadMidiMapBtn;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessorEditor)
};
