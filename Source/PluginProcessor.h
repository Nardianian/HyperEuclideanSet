#pragma once

#include <JuceHeader.h>
#include "EuclideanSet.h"

//==============================================================================
class AudioPluginAudioProcessor : public juce::AudioProcessor,
    public juce::AudioProcessorValueTreeState::Listener
{
public:
    AudioPluginAudioProcessor();
    ~AudioPluginAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override; 
    bool hasEditor() const override { return true; }     

    const juce::String getName() const override { return "EuclideanMidiGenerator"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Funzioni per file esterno (JSON)
    void saveMidiMappingToFile(const juce::File& file);
    void loadMidiMappingFromFile(const juce::File& file);

    void randomizeRow(int rowIndex);
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    //==============================================================================
    struct Rhythm
    {
        Rhythm(juce::AudioParameterBool* active,
            juce::AudioParameterInt* note,
            juce::AudioParameterInt* steps,
            juce::AudioParameterInt* pulses,
            juce::AudioParameterInt* depth,
            juce::AudioParameterInt* accentSteps);

        void rebuildPattern();
        std::atomic<bool> needsRebuild{ true };

        // Puntatori ai parametri
        juce::AudioParameterBool* activated;
        juce::AudioParameterInt* midiNote;
        juce::AudioParameterInt* steps;
        juce::AudioParameterInt* pulses;
        juce::AudioParameterInt* depth;
        juce::AudioParameterInt* accentSteps;
        juce::AudioParameterChoice* inputMode;
        juce::AudioParameterChoice* melMode;      
        juce::AudioParameterFloat* gate;
        juce::AudioParameterBool* useHyper;       
        juce::AudioParameterFloat* probability;   
        juce::AudioParameterFloat* swing;         
        juce::AudioParameterFloat* velMult;       
        juce::AudioParameterInt* midiChannel;     
        juce::AudioParameterInt* mainVolume;      

        int samplesUntilNoteOff = -1;  
        int lastNotePlayed = -1;       

        std::unique_ptr<juce::MidiOutput> hardwareOutput; 
        int currentDeviceIndex = -1;

        int lastInputNote = -1;
        int pitchIndex = 0;
        std::vector<int> pattern;
        std::vector<int> velocities;
        std::vector<int> accentPattern;

        int stepIndex = 0;
        int accentIndex = 0;
        // --- MIDI LEARN LOGIC ---
        // Map: Number CC -> ID Parameter (es: 74 -> "STEPS0")
        std::map<int, juce::String> midiMapping;
        int ccWaitingForAssignment = -1;
        juce::String paramWaitingForAssignment = "";
    };

    struct PitchPreset {
        juce::String name;
        std::vector<int> intervals;
    };

    static inline std::vector<PitchPreset> getScalePresets() {
        return {
            {"Z-DMixolydian", {2,4,6,7,9,11,0}}, {"Z-GMixolydian", {7,9,11,0,2,4,5}},
            {"Z-PrygianDominant", {4,5,8,9,11,0,2}}, {"Z-Ddorian", {2,4,5,7,9,10,0}},
            {"Z-Modified", {2,4,5,7,9,10,11,0}}, {"Z-CHexatonic", {0,2,4,6,8,10}},
            {"Z-CDiminished", {0,1,3,4,6,7,9,10}}, {"Z-CLydianDominant", {0,2,4,6,7,9,10}},
            {"Z-APentaMinor", {9,0,2,4,7,3}}, {"Z-APentaMinor2", {9,0,3,4,7}},
            {"Z-BlackPage", {0,1,4,7,8,10}}, {"Z-St.Alfonso", {6,4,9,7,11,10,1}},
            {"C-Blues1", {0,3,5,6,7,10}}, {"C-Blues2", {0,3,5,6,7,10}},
            {"B-Blues", {4,7,9,10,11,2}}, {"A-Blues1", {9,0,2,3,4,7}},
            {"G-Blues", {7,10,0,1,2,5}}, {"Min-Jazz", {0,2,3,5,7,9,11}},
            {"C7-Jazz", {0,4,7,10}}, {"Am7-Jazz", {9,0,4,7}}, {"Dm7-Jazz", {2,5,9,0}},
            {"G7-Jazz", {7,11,2,5}}, {"Cmaj7-Jazz", {0,4,7,11}}, {"Fm7b5-Jazz", {5,8,11,3}},
            {"PentaMinor1", {0,3,5,7,10}}, {"PentaMinor2", {9,0,2,4,7}}, {"PentaMajor", {0,2,4,7,9}},
            {"Major", {0,2,4,5,7,9,11}}, {"Minor", {0,2,3,5,7,8,10}}, {"Lydian", {5,7,9,11,0,2,4,6}},
            {"G-Major", {7,9,11,0,2,4,6}}, {"D-Major", {2,4,6,7,9,11,1}}, {"F-Major", {5,7,9,10,0,2,4}},
            {"A-Major", {9,11,1,2,4,6,8}}, {"A-Minor", {9,11,0,2,4,5,7}}, {"D-Minor", {2,4,5,7,9,10,0}},
            {"E-Minor", {4,5,7,9,11,0,2}}, {"F-Minor", {5,7,8,10,0,2,3}}, {"G-Minor", {7,9,10,0,2,4,5}},
            {"D-Lydian", {2,4,6,8,9,11,1}}, {"B-Lydian", {11,1,3,5,6,8,10}}, {"Phrygian", {0,1,3,5,7,8,10}},
            {"Mixolydian", {7,9,11,0,2,4,5}}, {"Dorian", {2,4,5,7,9,11,0}}
        };
    }

    static inline std::vector<PitchPreset> getChordPresets() {
        return {
            {"Z-St.Alfonso1", {6,4,9,7,11,10,1}}, {"Z-St.Alfonso2", {0,11,7}},
            {"Z-St.AlfonsoInv", {0,1,4}}, {"Z-Watermelon", {4,9,11}}, {"Z-Inc", {0,4,7,11}},
            {"Gsus4", {0,2,7}}, {"C-Major", {0,4,7}}, {"1st-Inversion", {4,7,0}},
            {"2nd-Inversion", {7,0,4}}, {"Augmented", {0,4,8}}, {"Major 7", {0,4,7,11}},
            {"Minor7", {0,3,7,10}}, {"Diminished", {0,3,6}}, {"C-Diminished", {0,3,6,9}}
        };
    }

    juce::AudioProcessorValueTreeState parameters; 
    juce::OwnedArray<Rhythm> rhythms;
    bool useInternalClock = false;
    bool isInternalPlaying = false; 
    double internalPhase = 0.0;     
    double internalBpm = 120.0;     
    double manualSampleTime = 0; 

    void setRowMidiOutput(int rowIndex, int deviceIndex);
    juce::StringArray getMidiOutputList();

private:
    juce::AudioPlayHead::CurrentPositionInfo posInfo;

    double fs = 44100.0;
    int samplesPerStep = 0;
    int sampleCounter = 0;

    static constexpr int rhythmCount = 6;

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)
};
