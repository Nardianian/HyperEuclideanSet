#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioPluginAudioProcessor::AudioPluginAudioProcessor()
    : parameters(*this, nullptr, "PARAMS", createParameterLayout())
{
    for (int i = 0; i < rhythmCount; ++i)
    {
        auto idx = juce::String(i);

        // Get ALL parameters before creating the Rhythm object
        auto* active = dynamic_cast<juce::AudioParameterBool*>(parameters.getParameter("ACTIVE" + idx));
        auto* note = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter("NOTE" + idx));
        auto* steps = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter("STEPS" + idx));
        auto* pulses = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter("PULSES" + idx));
        auto* depth = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter("DEPTH" + idx));
        auto* accentSteps = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter("ACCENTS" + idx));

        // Object creation (no internal call to rebuildPattern)
        auto* r = new Rhythm(active, note, steps, pulses, depth, accentSteps);

        // Extra parameter connection
        r->useHyper = dynamic_cast<juce::AudioParameterBool*>(parameters.getParameter("HYPER" + idx));
        r->inputMode = dynamic_cast<juce::AudioParameterChoice*>(parameters.getParameter("MODE" + idx));
        r->melMode = dynamic_cast<juce::AudioParameterChoice*>(parameters.getParameter("MEL_MODE" + idx));
        r->gate = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("GATE" + idx));
        r->probability = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("PROB" + idx));
        r->swing = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("SWING" + idx));
        r->velMult = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("VEL_MULT" + idx));
        r->midiChannel = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter("CH" + idx));
        r->mainVolume = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter("MAIN_VOL" + idx));
        // Il puntatore Octave della struct Rhythm verrà letto direttamente dal ValueTree nel processBlock.
        // 4. SOLO ORA possiamo ricostruire il pattern in sicurezza
        r->rebuildPattern();

        rhythms.add(r);

        parameters.addParameterListener("STEPS" + idx, this);
        parameters.addParameterListener("PULSES" + idx, this);
        parameters.addParameterListener("HYPER" + idx, this);
        parameters.addParameterListener("DEPTH" + idx, this);
    }
    isInternalPlaying = false;
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor() {}

//==============================================================================
void AudioPluginAudioProcessor::prepareToPlay(double sampleRate, int)
{
    fs = sampleRate;
    sampleCounter = 0;
}

//==============================================================================
void AudioPluginAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::MidiBuffer incomingMidi;
    incomingMidi.addEvents(midi, 0, buffer.getNumSamples(), 0);
    midi.clear();
    buffer.clear();

    // Input MIDI
    for (const auto metadata : incomingMidi)
    {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn())
        {
            for (auto* r : rhythms)
                r->lastInputNote = msg.getNoteNumber();
        }
    }

    // Timing (linked to GUI controllers)
    double currentSampleTime = 0;

    posInfo.isPlaying = isInternalPlaying;
    posInfo.bpm = internalBpm;

    if (posInfo.isPlaying)
    {
        manualSampleTime += (double)buffer.getNumSamples();
    }
    else
    {
        manualSampleTime = 0; // Return to the starting point when you press stop
    }
    currentSampleTime = manualSampleTime;

    if (!posInfo.isPlaying)
    {
        sampleCounter = 0;
        return;
    }

    const double samplesPerQuarter = fs * (60.0 / (posInfo.bpm > 0 ? posInfo.bpm : 120.0));
    const double samplesPerSixteenth = samplesPerQuarter / 4.0;
    samplesPerStep = static_cast<int>(samplesPerSixteenth);

    // Samples Loop
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        double absoluteSample = currentSampleTime + (double)sample;
        int currentSixteenth = static_cast<int>(std::floor(absoluteSample / samplesPerSixteenth));
        int lastSixteenth = static_cast<int>(std::floor((absoluteSample - 1.0) / samplesPerSixteenth));

        // Determines whether the specific sample is the start of a new 16th
        bool isNewStep = (currentSixteenth > lastSixteenth);

        for (auto* r : rhythms)
        {
            if (!r->activated->get()) continue;

            // --- NOTE OFF MANAGEMENT (GATE) ---
            // This check is done on each sample for maximum precision.
            if (r->samplesUntilNoteOff > 0)
            {
                r->samplesUntilNoteOff--;
                if (r->samplesUntilNoteOff == 0)
                {
                    int safeChannel = juce::jlimit(1, 16, r->midiChannel->get());
                    auto offMsg = juce::MidiMessage::noteOff(safeChannel, r->lastNotePlayed);

                    if (r->hardwareOutput != nullptr)
                        r->hardwareOutput->sendMessageNow(offMsg);

                    midi.addEvent(offMsg, sample);
                    r->samplesUntilNoteOff = -1;
                }
            }

            // --- TRIGGER LOGIC (ONLY ON NEW STEP) ---
            if (isNewStep)
            {
                if (r->needsRebuild.exchange(false) || r->pattern.empty())
                    r->rebuildPattern();

                // PitchIndex advance logic (if active)
                auto* advParam = parameters.getRawParameterValue("MEL_STEP_ADV" + juce::String(rhythms.indexOf(r)));
                if (advParam != nullptr && advParam->load() > 0.5f)
                    r->pitchIndex++;

                {
                    int baseNote = r->midiNote->get();
                    int noteToPlay = baseNote;
                    int mode = r->inputMode->getIndex();

                    // Retrieve the octave directly from the parameter ("OCTAVE" + idx)
                    auto* octParam = parameters.getRawParameterValue("OCTAVE" + juce::String(rhythms.indexOf(r)));
                    int octShift = (octParam != nullptr) ? (int)octParam->load() : 0;

                    // --- MELODIC LOGIC ---
                    if (mode == 2) // SCALE
                    {
                        auto scales = getScalePresets();
                        int scaleIdx = parameters.getRawParameterValue("TYPE" + juce::String(rhythms.indexOf(r)))->load();
                        if (scaleIdx >= 0 && scaleIdx < (int)scales.size())
                        {
                            const auto& intervals = scales[scaleIdx].intervals;
                            if (!intervals.empty())
                                noteToPlay = baseNote + intervals[r->pitchIndex % intervals.size()];
                        }
                    }
                    else if (mode == 3) // CHORD
                    {
                        auto chords = getChordPresets();
                        int chordIdx = parameters.getRawParameterValue("TYPE" + juce::String(rhythms.indexOf(r)))->load();
                        if (chordIdx >= 0 && chordIdx < (int)chords.size())
                        {
                            const auto& intervals = chords[chordIdx].intervals;
                            if (!intervals.empty())
                                noteToPlay = baseNote + intervals[r->pitchIndex % intervals.size()];
                        }
                    }

                    // OCTAVE APPLICATION AND MIDI RANGE PROTECTION
                    noteToPlay = juce::jlimit(0, 127, noteToPlay + (octShift * 12));

                    // PitchIndex advance (if not controlled by the S/P key)
                    auto* advParam = parameters.getRawParameterValue("MEL_STEP_ADV" + juce::String(rhythms.indexOf(r)));
                    if (advParam == nullptr || advParam->load() < 0.5f)
                        r->pitchIndex = (r->pitchIndex + 1) % 128;

                    int safeChannel = juce::jlimit(1, 16, r->midiChannel->get());

                    // --- DYNAMIC VELOCITY & CC7 ---
                    float multiplier = r->velMult != nullptr ? r->velMult->get() : 1.0f;
                    // Accent: if the Euclidean accent pattern is 1, base velocity 120, otherwise 80
                    int baseVel = (r->accentPattern[r->accentIndex % r->accentPattern.size()] == 1) ? 120 : 80;
                    int finalVel = juce::jlimit(0, 127, (int)(baseVel * multiplier));
                    if (finalVel < 10 && multiplier > 0.1f) finalVel = 100; // Protection: If too low, strength to 100

                    if (juce::JUCEApplication::isStandaloneApp() && r->hardwareOutput != nullptr)
                    {
                        // Usiamo mainVolume definito nella tua struct Rhythm
                        int volVal = (r->mainVolume != nullptr) ? r->mainVolume->get() : 100;
                        auto volMsg = juce::MidiMessage::controllerEvent(safeChannel, 7, (juce::uint8)volVal);
                        r->hardwareOutput->sendMessageNow(volMsg);
                    }

                    // --- NOTE TRIGGER ---
                    float gateValue = r->gate != nullptr ? r->gate->get() : 0.5f;
                    r->samplesUntilNoteOff = static_cast<int>(samplesPerSixteenth * gateValue);
                    r->lastNotePlayed = noteToPlay;

                    auto onMsg = juce::MidiMessage::noteOn(safeChannel, noteToPlay, (juce::uint8)finalVel);
                    DBG("ROW TRIGGER: Note " << noteToPlay << " Chan " << safeChannel << " Vel " << finalVel);
                    if (r->hardwareOutput != nullptr) r->hardwareOutput->sendMessageNow(onMsg);
                    midi.addEvent(onMsg, sample);
                }

                // Increase in indices
                int nextStep = (r->stepIndex + 1) % (int)r->pattern.size();
                if (nextStep == 0 && r->melMode->getIndex() == 1)
                    r->pitchIndex = 0;

                r->stepIndex = nextStep;
                r->accentIndex = (r->accentIndex + 1) % (int)r->accentPattern.size();
            }
        }
    }
}
//================================================================================================================

AudioPluginAudioProcessor::Rhythm::Rhythm(juce::AudioParameterBool* a, juce::AudioParameterInt* n,
    juce::AudioParameterInt* s, juce::AudioParameterInt* p, juce::AudioParameterInt* d, juce::AudioParameterInt* ac)
    : activated(a), midiNote(n), steps(s), pulses(p), depth(d), accentSteps(ac)
{
    // Leave blank: the first call to rebuildPattern() is made by the Processor
    // after all pointers (HYPER, MODE, etc.) have been assigned.
}

void AudioPluginAudioProcessor::Rhythm::rebuildPattern()
{
    // Avoid resetting the pitch index here to continue the melody
    // even when changing Steps/Pulses (Continuum mode)
    if (melMode != nullptr && melMode->getIndex() == 1) // Se "Looped"
        pitchIndex = 0;

    stepIndex = 0;
    accentIndex = 0;

    // Null pointer protection
    bool hyperActive = (useHyper != nullptr) ? useHyper->get() : false;
    int currentDepth = (depth != nullptr) ? depth->get() : 1;
    if (!hyperActive || currentDepth <= 1) {
        Euclidean e(pulses->get(), steps->get());
        pattern = e.generateSequence();
        velocities.assign(pattern.size(), 100);
    }

    else {
        HyperEuclidean h(pulses->get(), steps->get(), currentDepth);
        pattern = h.generateSequence();
        velocities = h.velocities;
    }

    if (accentSteps != nullptr) {
        Euclidean accentEuclid(juce::jmax(1, pulses->get() / 2), accentSteps->get());
        accentPattern = accentEuclid.generateSequence();
    }

    // ADD DIAGNOSTICS:
    DBG("PATTERN REBUILT - Steps: " << steps->get() << " Pulses: " << pulses->get());
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout AudioPluginAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int i = 0; i < rhythmCount; ++i)
    {
        auto idx = juce::String(i);
        layout.add(std::make_unique<juce::AudioParameterBool>("ACTIVE" + idx, "Active " + idx, false));
        layout.add(std::make_unique<juce::AudioParameterBool>("HYPER" + idx, "Hyper " + idx, false));
        layout.add(std::make_unique<juce::AudioParameterInt>("NOTE" + idx, "Note " + idx, 24, 127, 36));
        layout.add(std::make_unique<juce::AudioParameterInt>("STEPS" + idx, "Steps " + idx, 1, 32, 8));
        layout.add(std::make_unique<juce::AudioParameterInt>("PULSES" + idx, "Pulses " + idx, 1, 32, 4));
        layout.add(std::make_unique<juce::AudioParameterInt>("DEPTH" + idx, "Depth " + idx, 1, 8, 1));
        layout.add(std::make_unique<juce::AudioParameterInt>("OCTAVE" + idx, "Octave " + idx, -4, 4, 0));
        layout.add(std::make_unique<juce::AudioParameterChoice>("MODE" + idx, "Mode " + idx, juce::StringArray{ "Fixed", "Input", "Scale", "Chord" }, 0));
        layout.add(std::make_unique<juce::AudioParameterInt>("ACCENTS" + idx, "Accents " + idx, 1, 32, 8));
        layout.add(std::make_unique<juce::AudioParameterChoice>("MEL_MODE" + idx, "Melody Mode " + idx, juce::StringArray{ "Continuum", "Looped" }, 0));
        layout.add(std::make_unique<juce::AudioParameterBool>("MEL_STEP_ADV" + idx, "Advance on Step " + idx, false));
        layout.add(std::make_unique<juce::AudioParameterFloat>("SWING" + idx, "Swing " + idx, 0.0f, 1.0f, 0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("VEL_MULT" + idx, "Velocity Mult " + idx, 0.0f, 2.0f, 1.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("GATE" + idx, "Gate " + idx, 0.1f, 1.0f, 0.5f));
        layout.add(std::make_unique<juce::AudioParameterInt>("MAIN_VOL" + idx, "Main Vol " + idx, 0, 127, 100));
        layout.add(std::make_unique<juce::AudioParameterFloat>("PROB" + idx, "Probability " + idx, 0.0f, 1.0f, 1.0f));
        layout.add(std::make_unique<juce::AudioParameterInt>("CH" + idx, "Channel " + idx, 1, 16, 1));
        juce::StringArray typeItems;
        for (auto& p : getScalePresets()) typeItems.add(p.name);
        layout.add(std::make_unique<juce::AudioParameterInt>("TYPE" + idx, "Type " + idx, 0, 100, 0));
        // Note: Make sure the saved index does not exceed the size of the presets in the processBlock
    }
    return layout;
}

void AudioPluginAudioProcessor::randomizeRow(int rowIndex)
{
    auto idx = juce::String(rowIndex);
    auto& rand = juce::Random::getSystemRandom();

    if (auto* pSteps = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter("STEPS" + idx)))
        pSteps->setValueNotifyingHost((float)pSteps->convertTo0to1((float)rand.nextInt({ 4, 32 })));

    if (auto* pPulses = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter("PULSES" + idx)))
        pPulses->setValueNotifyingHost((float)pPulses->convertTo0to1((float)rand.nextInt({ 1, 16 })));

    if (auto* pNote = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter("NOTE" + idx)))
        pNote->setValueNotifyingHost((float)pNote->convertTo0to1((float)rand.nextInt({ 36, 72 })));

    if (auto* pProb = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("PROB" + idx)))
        pProb->setValueNotifyingHost(rand.nextFloat());
}

juce::StringArray AudioPluginAudioProcessor::getMidiOutputList()
{
    juce::StringArray list;
    auto devices = juce::MidiOutput::getAvailableDevices();
    for (auto d : devices)
        list.add(d.name);
    return list;
}

void AudioPluginAudioProcessor::setRowMidiOutput(int rowIndex, int deviceIndex)
{
    auto devices = juce::MidiOutput::getAvailableDevices();

    // If the index from the GUI is <= 0 (so -1 or 0), close the hardware output
    if (deviceIndex <= 0)
    {
        rhythms[rowIndex]->hardwareOutput = nullptr;
        DBG("MIDI OUT Row " << rowIndex << " -> Deactivated (Index: " << deviceIndex << ")");
        return;
    }

    // Calculate the actual index (ComboBox index 1 = Device index 0)
    int actualDeviceIdx = deviceIndex - 1;

    if (actualDeviceIdx >= 0 && actualDeviceIdx < devices.size())
    {
        rhythms[rowIndex]->hardwareOutput = nullptr; // Previous Reset

        auto newDevice = juce::MidiOutput::openDevice(devices[actualDeviceIdx].identifier);

        if (newDevice != nullptr)
        {
            rhythms[rowIndex]->hardwareOutput = std::move(newDevice);
            rhythms[rowIndex]->hardwareOutput->startBackgroundThread();
            // DBG("SUCCESS: Row " << rowIndex << " open up " << devices[actualDeviceIdx].name);
        }
        else
        {
            DBG("ERROR: Unable to open " << devices[actualDeviceIdx].name);
        }
    }
    else
    {
        DBG("ERROR: Index out of range (" << actualDeviceIdx << ")");
        rhythms[rowIndex]->hardwareOutput = nullptr;
    }
}

juce::AudioProcessorEditor* AudioPluginAudioProcessor::createEditor()
{
    return new AudioPluginAudioProcessorEditor(*this);
}

void AudioPluginAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    for (int i = 0; i < rhythms.size(); ++i)
    {
        juce::String idx = juce::String(i);
        if (parameterID.contains(idx))
        {
            rhythms[i]->needsRebuild = true;
            break;
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AudioPluginAudioProcessor(); }
