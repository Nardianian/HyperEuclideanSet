#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioPluginAudioProcessor::AudioPluginAudioProcessor()
    : parameters(*this, nullptr, "PARAMS", createParameterLayout())
{
    for (int i = 0; i < rhythmCount; ++i)
    {
        auto idx = juce::String(i);

        auto* active = dynamic_cast<juce::AudioParameterBool*>(parameters.getParameter("ACTIVE" + idx));
        auto* note = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter("NOTE" + idx));
        auto* steps = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter("STEPS" + idx));
        auto* pulses = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter("PULSES" + idx));
        auto* depth = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter("DEPTH" + idx));
        auto* accentSteps = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter("ACCENTS" + idx));

        auto* r = new Rhythm(active, note, steps, pulses, depth, accentSteps);

        r->useHyper = dynamic_cast<juce::AudioParameterBool*>(parameters.getParameter("HYPER" + idx));
        r->inputMode = dynamic_cast<juce::AudioParameterChoice*>(parameters.getParameter("MODE" + idx));
        r->melMode = dynamic_cast<juce::AudioParameterChoice*>(parameters.getParameter("MEL_MODE" + idx));
        r->gate = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("GATE" + idx));
        r->probability = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("PROB" + idx));
        r->swing = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("SWING" + idx));
        r->velMult = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("VEL_MULT" + idx));
        r->midiChannel = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter("CH" + idx));
        r->mainVolume = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter("MAIN_VOL" + idx));
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
    // --- MIDI LEARN LOGIC & REMOTE CONTROL ---
    for (const auto metadata : incomingMidi)
    {
        const auto msg = metadata.getMessage();
        if (msg.isController())
        {
            int ccNumber = msg.getControllerNumber();
            float ccValue = msg.getControllerValue() / 127.0f;

            for (auto* r : rhythms)
            {
                if (r->ccWaitingForAssignment == -2)
                {
                    r->midiMapping.erase(ccNumber);
                    r->midiMapping[ccNumber] = r->paramWaitingForAssignment;
                    r->ccWaitingForAssignment = -1; // Fine Learn
                    continue;
                }

                if (r->midiMapping.count(ccNumber))
                {
                    if (auto* param = parameters.getParameter(r->midiMapping[ccNumber]))
                    {
                        param->setValueNotifyingHost(ccValue);
                    }
                }
            }
        }
    }
    midi.clear();
    buffer.clear();

    for (const auto metadata : incomingMidi)
    {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn())
        {
            for (auto* r : rhythms)
                r->lastInputNote = msg.getNoteNumber();
        }
    }

    double currentSampleTime = 0;

    posInfo.isPlaying = isInternalPlaying;
    posInfo.bpm = internalBpm;

    if (posInfo.isPlaying)
    {
        manualSampleTime += (double)buffer.getNumSamples();
    }
    else
    {
        manualSampleTime = 0; 
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

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        double absoluteSample = currentSampleTime + (double)sample;
        int currentSixteenth = static_cast<int>(std::floor(absoluteSample / samplesPerSixteenth));
        int lastSixteenth = static_cast<int>(std::floor((absoluteSample - 1.0) / samplesPerSixteenth));

        bool isNewStep = (currentSixteenth > lastSixteenth);

        for (auto* r : rhythms)
        {
            if (!r->activated->get()) continue;

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

            if (isNewStep)
            {
                if (r->needsRebuild.exchange(false) || r->pattern.empty())
                    r->rebuildPattern();

                auto* advParam = parameters.getRawParameterValue("MEL_STEP_ADV" + juce::String(rhythms.indexOf(r)));
                if (advParam != nullptr && advParam->load() > 0.5f)
                    r->pitchIndex++;

                {
                    int baseNote = r->midiNote->get();
                    int noteToPlay = baseNote;
                    int mode = r->inputMode->getIndex();

                    auto* octParam = parameters.getRawParameterValue("OCTAVE" + juce::String(rhythms.indexOf(r)));
                    int octShift = (octParam != nullptr) ? (int)octParam->load() : 0;

                    if (mode == 2) // SCALE
                    {
                        auto scales = getScalePresets();
                        auto* typeParam = parameters.getRawParameterValue("TYPE" + juce::String(rhythms.indexOf(r)));
                        int scaleIdx = typeParam ? juce::jlimit(0, (int)scales.size() - 1, (int)typeParam->load()) : 0;
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
                        auto* typeParam = parameters.getRawParameterValue("TYPE" + juce::String(rhythms.indexOf(r)));
                        int chordIdx = typeParam ? juce::jlimit(0, (int)chords.size() - 1, (int)typeParam->load()) : 0;
                        if (chordIdx >= 0 && chordIdx < (int)chords.size())
                        {
                            const auto& intervals = chords[chordIdx].intervals;
                            if (!intervals.empty())
                                noteToPlay = baseNote + intervals[r->pitchIndex % intervals.size()];
                        }
                    }

                    noteToPlay = juce::jlimit(0, 127, noteToPlay + (octShift * 12));

                    auto* advParam = parameters.getRawParameterValue("MEL_STEP_ADV" + juce::String(rhythms.indexOf(r)));
                    if (advParam == nullptr || advParam->load() < 0.5f)
                        r->pitchIndex = (r->pitchIndex + 1) % 128;

                    int safeChannel = juce::jlimit(1, 16, r->midiChannel->get());

                    float multiplier = r->velMult != nullptr ? r->velMult->get() : 1.0f;
                    int baseVel = (r->accentPattern[r->accentIndex % r->accentPattern.size()] == 1) ? 120 : 80;
                    int finalVel = juce::jlimit(0, 127, (int)(baseVel * multiplier));
                    if (finalVel < 10 && multiplier > 0.1f) finalVel = 100; 

                    if (juce::JUCEApplication::isStandaloneApp() && r->hardwareOutput != nullptr)
                    {
                        int volVal = (r->mainVolume != nullptr) ? r->mainVolume->get() : 100;
                        auto volMsg = juce::MidiMessage::controllerEvent(safeChannel, 7, (juce::uint8)volVal);
                        r->hardwareOutput->sendMessageNow(volMsg);
                    }

                    float gateValue = r->gate != nullptr ? r->gate->get() : 0.5f;
                    r->samplesUntilNoteOff = static_cast<int>(samplesPerSixteenth * gateValue);
                    r->lastNotePlayed = noteToPlay;

                    auto onMsg = juce::MidiMessage::noteOn(safeChannel, noteToPlay, (juce::uint8)finalVel);
                    DBG("ROW TRIGGER: Note " << noteToPlay << " Chan " << safeChannel << " Vel " << finalVel);
                    if (r->hardwareOutput != nullptr) r->hardwareOutput->sendMessageNow(onMsg);
                    midi.addEvent(onMsg, sample);
                }

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
    // We don't reset the pitchIndex here if we want the melody to continue 
    // playing even if we change Steps/Pulses (Continuum mode)
    if (melMode != nullptr && melMode->getIndex() == 1) // Se "Looped"
        pitchIndex = 0;

    stepIndex = 0;
    accentIndex = 0;

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

    if (deviceIndex <= 0)
    {
        rhythms[rowIndex]->hardwareOutput = nullptr;
        DBG("MIDI OUT Row " << rowIndex << " -> Disattivato (Index: " << deviceIndex << ")");
        return;
    }

    int actualDeviceIdx = deviceIndex - 1;

    if (actualDeviceIdx >= 0 && actualDeviceIdx < devices.size())
    {
        rhythms[rowIndex]->hardwareOutput = nullptr; 

        auto newDevice = juce::MidiOutput::openDevice(devices[actualDeviceIdx].identifier);

        if (newDevice != nullptr)
        {
            rhythms[rowIndex]->hardwareOutput = std::move(newDevice);
            rhythms[rowIndex]->hardwareOutput->startBackgroundThread();
            // DBG("SUCCESS: Row " << rowIndex << " open on " << devices[actualDeviceIdx].name);
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

void AudioPluginAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());

    auto* midiXml = xml->createNewChildElement("MIDI_MAPPINGS");
    for (int i = 0; i < rhythms.size(); ++i) {
        auto* rowXml = midiXml->createNewChildElement("ROW_" + juce::String(i));
        for (auto const& [cc, paramID] : rhythms[i]->midiMapping) {
            auto* mapEntry = rowXml->createNewChildElement("MAP");
            mapEntry->setAttribute("CC", cc);
            mapEntry->setAttribute("ID", paramID);
        }
    }
    copyXmlToBinary(*xml, destData);
}

void AudioPluginAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(parameters.state.getType())) {
        parameters.replaceState(juce::ValueTree::fromXml(*xmlState));

        if (auto* midiXml = xmlState->getChildByName("MIDI_MAPPINGS")) {
            for (int i = 0; i < rhythms.size(); ++i) {
                rhythms[i]->midiMapping.clear();
                if (auto* rowXml = midiXml->getChildByName("ROW_" + juce::String(i))) {
                    auto* child = rowXml->getFirstChildElement();
                    while (child != nullptr) {
                        if (child->hasTagName("MAP")) {
                            int cc = child->getIntAttribute("CC");
                            juce::String id = child->getStringAttribute("ID");
                            rhythms[i]->midiMapping[cc] = id;
                        }
                        child = child->getNextElement();
                    }
                }
            }
        }
    }
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

void AudioPluginAudioProcessor::saveMidiMappingToFile(const juce::File& file)
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();

    for (int i = 0; i < rhythms.size(); ++i) {
        juce::DynamicObject::Ptr rowObj = new juce::DynamicObject();
        for (auto const& [cc, id] : rhythms[i]->midiMapping) {
            rowObj->setProperty(juce::String(cc), id);
        }
        obj->setProperty("Row" + juce::String(i), rowObj.get());
    }

    juce::FileOutputStream stream(file);
    if (stream.openedOk()) {
        stream.setPosition(0);
        stream.truncate();
        stream.writeString(juce::JSON::toString(obj.get()));
    }
}

void AudioPluginAudioProcessor::loadMidiMappingFromFile(const juce::File& file)
{
    auto json = juce::JSON::parse(file);
    if (auto* obj = json.getDynamicObject())
    {
        for (int i = 0; i < rhythms.size(); ++i)
        {
            rhythms[i]->midiMapping.clear();
            juce::String rowName = "Row" + juce::String(i);

            if (obj->hasProperty(rowName))
            {
                if (auto* rowObj = obj->getProperty(rowName).getDynamicObject())
                {
                    auto namedProps = rowObj->getProperties();
                    for (int p = 0; p < namedProps.size(); ++p)
                    {
                        juce::Identifier id = namedProps.getName(p);
                        juce::String ccNameString = id.toString();
                        int cc = ccNameString.getIntValue();

                        juce::String paramID = namedProps.getValueAt(p).toString();
                        rhythms[i]->midiMapping[cc] = paramID;
                    }
                }
            }
        }
    }
}

// Metodo obbligatorio per JUCE (da aggiungere in fondo se non presente)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AudioPluginAudioProcessor(); }
