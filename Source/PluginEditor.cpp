#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    // 1. Setup Labels (Row 1)
    juce::StringArray headers = { "", "ON", "HYPER", "STEPS", "PULSES", "NOTE", "OCT", "MODE", "MEL", "S/P", "TYPE", "PROB", "SWING", "GATE", "VEL", "RAND", "PORT", "VOL", "CH" };
    for (auto text : headers)
    {
        auto* l = labels.add(new juce::Label(text, text));
        l->setJustificationType(juce::Justification::centred);
        addAndMakeVisible(l);
    }

    // 2. Six Row Setup (Rows 2-7)
    for (int i = 0; i < 6; ++i)
    {
        auto* row = rows.add(new GuiRow());
        auto idx = juce::String(i);

        addAndMakeVisible(row->activeBtn);
        addAndMakeVisible(row->hyperBtn);

        auto setupSlider = [this](juce::Slider& s) {
            s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 45, 15);
            addAndMakeVisible(s);
            };

        setupSlider(row->stepsSlider);
        setupSlider(row->pulsesSlider);
        setupSlider(row->noteSlider);
        row->noteSlider.textFromValueFunction = [](double v) {
            return juce::MidiMessage::getMidiNoteName((int)v, true, true, 3);
            };
        row->noteSlider.updateText();

        setupSlider(row->octaveSlider);
        row->octaveSlider.textFromValueFunction = [](double v) {
            int val = (int)v;
            return (val > 0 ? "+" : "") + juce::String(val);
            };
        row->octaveSlider.updateText();
        setupSlider(row->probSlider);
        setupSlider(row->swingSlider);
        setupSlider(row->gateSlider);
        setupSlider(row->velSlider);

        row->randBtn.setButtonText("R");
        row->randBtn.onClick = [this, i] { audioProcessor.randomizeRow(i); };
        addAndMakeVisible(row->randBtn);

        row->modeBox.addItemList({ "Fixed", "Input", "Scale", "Chord" }, 1);
        addAndMakeVisible(row->modeBox);

        row->melModeBox.addItemList({ "Continuum", "Looped" }, 1);
        addAndMakeVisible(row->melModeBox);
        addAndMakeVisible(row->typeBox); 

        // --- ADD MIDI CHANNEL ---
        row->chanBox.addItem("Any", 1); // ID 1 = Parameter value 0
        for (int c = 1; c <= 16; ++c)
            row->chanBox.addItem(juce::String(c), c + 1);
        addAndMakeVisible(row->chanBox);

        row->modeBox.onChange = [this, i, row] {
            row->typeBox.clear();
            int mode = row->modeBox.getSelectedItemIndex();
            if (mode == 2) { // SCALE
                auto presets = audioProcessor.getScalePresets();
                for (int p = 0; p < (int)presets.size(); ++p) row->typeBox.addItem(presets[p].name, p + 1);
                row->typeBox.setEnabled(true);
            }
            else if (mode == 3) { // CHORD
                auto presets = audioProcessor.getChordPresets();
                for (int p = 0; p < (int)presets.size(); ++p) row->typeBox.addItem(presets[p].name, p + 1);
                row->typeBox.setEnabled(true);
            }
            else {
                row->typeBox.setEnabled(false);
                row->typeBox.setText("---", juce::dontSendNotification);
            }
            };
        row->modeBox.onChange();

        // Force the initial update of the TYPE menu based on the current value of the parameter
        // Change 'p' to 'typeParam' to avoid conflict with the constructor parameter
        if (auto* typeParam = audioProcessor.parameters.getRawParameterValue("TYPE" + idx))
            row->typeBox.setSelectedId((int)typeParam->load() + 1, juce::dontSendNotification);

        // Identification label R1, R2...
        row->rowLabel.setText("R" + juce::String(i + 1), juce::dontSendNotification);
        row->rowLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(row->rowLabel);

        // MIDI Port Selector specific to this row (Standalone only)
        if (juce::JUCEApplication::isStandaloneApp())
        {
            auto* ps = portSelectors.add(std::make_unique<juce::ComboBox>("Port" + idx));

            // Lambda to refresh the list
            auto updateList = [this, ps] {
                auto currentId = ps->getSelectedId();
                ps->clear(juce::dontSendNotification);
                ps->addItem("No Out", 1);
                auto outputs = audioProcessor.getMidiOutputList();
                for (int j = 0; j < outputs.size(); ++j)
                {
                    juce::String fullName = outputs[j];
                    juce::String displayName = fullName;
                    if (displayName.length() > 25)
                        displayName = displayName.substring(0, 22) + "...";
                    ps->addItem(displayName, j + 2);
                }

                // Resetting ID before setting tooltip
                ps->setSelectedId(currentId, juce::dontSendNotification);

                int selIdx = ps->getSelectedItemIndex();
                if (selIdx > 0 && (selIdx - 1) < outputs.size())
                    ps->setTooltip(outputs[selIdx - 1]); // Original full name
                else
                    ps->setTooltip("Seleziona uscita MIDI");
                };

            // Adding a listener to update the list when clicks on the box
            struct ClickListener : public juce::MouseListener {
                std::function<void()> onClick;
                void mouseDown(const juce::MouseEvent&) override { if (onClick) onClick(); }
            };
            auto* listener = new ClickListener();
            listener->onClick = updateList;
            ps->addMouseListener(listener, true); // true = It also intercepts clicks on derived classes

            // Listener memory management (prevents leaks)
            ps->addChildComponent(new juce::Label("", "")); // dummy to hook the listener if necessary, 

            updateList(); // First population
            ps->setSelectedId(1);
            ps->setTooltip("Select the MIDI port for this row");

            ps->onChange = [this, ps, i] {
                int selectedIdx = ps->getSelectedItemIndex();
                audioProcessor.setRowMidiOutput(i, selectedIdx);

                // Update the tooltip by fetching the real name from the processor
                auto currentOutputs = audioProcessor.getMidiOutputList();
                if (selectedIdx > 0 && (selectedIdx - 1) < currentOutputs.size())
                    ps->setTooltip(currentOutputs[selectedIdx - 1]);
                else
                    ps->setTooltip("No Out");
                };
            addAndMakeVisible(ps);
        }

        // Attachments
        buttonAttachments.add(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(audioProcessor.parameters, "ACTIVE" + idx, row->activeBtn));
        buttonAttachments.add(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(audioProcessor.parameters, "HYPER" + idx, row->hyperBtn));
        sliderAttachments.add(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.parameters, "STEPS" + idx, row->stepsSlider));
        sliderAttachments.add(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.parameters, "PULSES" + idx, row->pulsesSlider));
        sliderAttachments.add(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.parameters, "NOTE" + idx, row->noteSlider));
        sliderAttachments.add(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.parameters, "OCTAVE" + idx, row->octaveSlider));
        sliderAttachments.add(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.parameters, "PROB" + idx, row->probSlider));
        sliderAttachments.add(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.parameters, "SWING" + idx, row->swingSlider));
        sliderAttachments.add(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.parameters, "GATE" + idx, row->gateSlider));
        sliderAttachments.add(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.parameters, "VEL_MULT" + idx, row->velSlider));
        comboAttachments.add(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(audioProcessor.parameters, "MODE" + idx, row->modeBox));
        comboAttachments.add(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(audioProcessor.parameters, "MEL_MODE" + idx, row->melModeBox));
        comboAttachments.add(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(audioProcessor.parameters, "TYPE" + idx, row->typeBox));
        comboAttachments.add(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(audioProcessor.parameters, "CH" + idx, row->chanBox));
        addAndMakeVisible(row->melStepBtn);
        row->melStepBtn.setButtonText("S/P");
        buttonAttachments.add(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(audioProcessor.parameters, "MEL_STEP_ADV" + idx, row->melStepBtn));

        // Setup Main Vol Slider
        row->mainVolSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        row->mainVolSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible(row->mainVolSlider);
        sliderAttachments.add(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.parameters, "MAIN_VOL" + idx, row->mainVolSlider));
    }

    // Standalone Global Controls (Clock, Play, BPM)
    if (juce::JUCEApplication::isStandaloneApp())
    {
        globalClockBtn = std::make_unique<juce::ToggleButton>("EXT MIDI CLOCK");
        globalClockBtn->onClick = [this] { audioProcessor.useInternalClock = !globalClockBtn->getToggleState(); };
        addAndMakeVisible(*globalClockBtn);

        globalPlayBtn = std::make_unique<juce::TextButton>("PLAY");
        globalPlayBtn->setClickingTogglesState(true);
        globalPlayBtn->setColour(juce::TextButton::buttonOnColourId, juce::Colours::green);
        globalPlayBtn->setColour(juce::TextButton::textColourOnId, juce::Colours::black);
        globalPlayBtn->onClick = [this] {
            audioProcessor.isInternalPlaying = globalPlayBtn->getToggleState();
            globalPlayBtn->setButtonText(globalPlayBtn->getToggleState() ? "STOP" : "PLAY");
            };
        addAndMakeVisible(*globalPlayBtn);

        globalBpmSlider = std::make_unique<juce::Slider>(juce::Slider::LinearBar, juce::Slider::TextBoxLeft);
        globalBpmSlider->setRange(40.0, 240.0, 0.1);
        globalBpmSlider->setValue(audioProcessor.internalBpm);
        globalBpmSlider->setTextValueSuffix(" BPM");
        globalBpmSlider->onValueChange = [this] { audioProcessor.internalBpm = globalBpmSlider->getValue(); };
        addAndMakeVisible(*globalBpmSlider);
    }

    setResizable(true, true); // Enable edge dragging
    setResizeLimits(1100, 650, 2560, 1440); // Set minimum and maximum limits
    setSize(1400, 720);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor() {}

void AudioPluginAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Dark background for all rows
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    auto totalArea = getLocalBounds().reduced(10);
    auto headerHeight = 30;
    auto footerHeight = 40;

    // Dedicated background for the Header row (Labels)
    auto headerArea = totalArea.removeFromTop(headerHeight);
    g.setColour(juce::Colours::black.withAlpha(0.5f)); // Più scuro per far risaltare il bianco
    g.fillRect(headerArea);

    // Separator line under the header (slightly brighter)
    g.setColour(juce::Colours::white.withAlpha(0.15f));
    g.drawHorizontalLine(headerArea.getBottom(), (float)totalArea.getX(), (float)totalArea.getRight());

    auto areaSequencer = totalArea;

    // Thin horizontal separation lines
    g.setColour(juce::Colours::white.withAlpha(0.1f));

    int rowH = areaSequencer.getHeight() / 6;
    for (int i = 1; i <= 6; ++i)
    {
        int y = headerArea.getBottom() + (i * rowH);
        g.drawHorizontalLine(y, (float)totalArea.getX(), (float)totalArea.getRight());
    }

    // Adding Version Text to the bottom right corner
    g.setColour(juce::Colours::grey.withAlpha(0.6f));
    g.setFont(12.0f);
    juce::String ver = "v" + juce::String(ProjectInfo::versionString);
    auto versionArea = getLocalBounds().removeFromRight(50).removeFromBottom(25);
    g.drawText(ver, versionArea, juce::Justification::centred);
}

void AudioPluginAudioProcessorEditor::resized()
{
    // Increased overall dimensions in the constructor setSize(...);
    auto area = getLocalBounds().reduced(10);
    auto footerArea = area.removeFromBottom(40);
    auto headerArea = area.removeFromTop(30);

    const int numCols = 19; 
    const int cw = area.getWidth() / numCols;
    const int rowHeight = area.getHeight() / 6;

    // Footer Controls Positioning (Fixed for std::unique_ptr)
    int footerWidgetW = 150;

    if (globalPlayBtn != nullptr)
        globalPlayBtn->setBounds(footerArea.removeFromLeft(footerWidgetW).reduced(5));

    if (globalBpmSlider != nullptr)
        globalBpmSlider->setBounds(footerArea.removeFromLeft(footerWidgetW * 2).reduced(5));

    if (globalClockBtn != nullptr)
        globalClockBtn->setBounds(footerArea.removeFromLeft(footerWidgetW).reduced(5));

    // Label positioning
    for (int i = 0; i < labels.size(); ++i)
    {
        auto lArea = headerArea.removeFromLeft(cw);
        int offset = 0; // Pixel shift (negative = left, positive = right)

        switch (i)
        {
        case 0:  offset = 0;    break; // Empty Column (ID)
        case 1:  offset = -40;  break; // ON
        case 2:  offset = -64;  break; // HYPER
        case 3:  offset = -72;  break; // STEPS
        case 4:  offset = -70;  break; // PULSES
        case 5:  offset = -71;  break; // NOTE
        case 6:  offset = -71;  break; // OCTAVE
        case 7:  offset = -77;  break; // MODE
        case 8:  offset = -77;  break; // MEL
        case 9:  offset = -77;  break; // S/P
        case 10: offset = -84;  break; // TYPE
        case 11: offset = -78;  break; // PROB
        case 12: offset = -78;  break; // SWING
        case 13: offset = -78;  break; // GATE
        case 14: offset = -78;  break; // VEL
        case 15: offset = -92;  break; // RAND
        case 16: offset = -106;  break; // PORT
        case 17: offset = -112;  break; // VOL
        case 18: offset = -117;  break; // CH

        default: offset = 0;    break;
        }

        labels[i]->setBounds(lArea.translated(offset, 0));
    }
    auto setCentred = [](juce::Component& c, juce::Rectangle<int> cell, int w, int h) {
        c.setBounds(cell.getCentreX() - w / 2, cell.getCentreY() - h / 2, w, h);
        };

    for (int i = 0; i < rows.size(); ++i)
    {
        auto rArea = juce::Rectangle<int>(area.getX(), area.getY() + (i * rowHeight), area.getWidth(), rowHeight);
        auto* row = rows[i];

        auto setupSlider = [&](juce::Slider& s, juce::Rectangle<int> cell) {
            s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, cell.getWidth() - 5, 18);
            setCentred(s, cell, cell.getWidth() + 5, rowHeight - 10);

            // Dynamic text formatting injection
            if (&s == &row->noteSlider) {
                s.textFromValueFunction = [](double v) {
                    return juce::MidiMessage::getMidiNoteName((int)v, true, true, 3);
                    };
                s.updateText();
            }
            else if (&s == &row->octaveSlider) {
                s.textFromValueFunction = [](double v) {
                    int val = (int)v;
                    return (val > 0 ? "+" : "") + juce::String(val);
                    };
                s.updateText();
            }
            };

        // Positioning (without offset) relative to the columns
        row->rowLabel.setBounds(rArea.removeFromLeft(cw * 0.6));
        setCentred(row->activeBtn, rArea.removeFromLeft(cw * 0.7), 20, 20); 
        setCentred(row->hyperBtn, rArea.removeFromLeft(cw * 0.7), 20, 20);  

        setupSlider(row->stepsSlider, rArea.removeFromLeft(cw));
        setupSlider(row->pulsesSlider, rArea.removeFromLeft(cw));
        setupSlider(row->noteSlider, rArea.removeFromLeft(cw));
        setupSlider(row->octaveSlider, rArea.removeFromLeft(cw));

        int comboW = cw - 4;
        int comboH = 22;
        setCentred(row->modeBox, rArea.removeFromLeft(cw * 1.1), comboW + 5, comboH);
        setCentred(row->melModeBox, rArea.removeFromLeft(cw * 1.1), comboW + 5, comboH);
        setCentred(row->melStepBtn, rArea.removeFromLeft(cw * 0.6), 28, 20);
        setCentred(row->typeBox, rArea.removeFromLeft(cw * 1.2), comboW + 10, comboH);

        setupSlider(row->probSlider, rArea.removeFromLeft(cw));
        setupSlider(row->swingSlider, rArea.removeFromLeft(cw));
        setupSlider(row->gateSlider, rArea.removeFromLeft(cw));
        setupSlider(row->velSlider, rArea.removeFromLeft(cw));

        setCentred(row->randBtn, rArea.removeFromLeft(cw * 0.6), 28, comboH);

        // Shifting in MIDI Outputs zone
        auto midiGroupArea = rArea;
        auto portCell = rArea.removeFromLeft(cw * 1.1);
        auto chanCell = rArea.removeFromLeft(cw * 0.8).translated(70, 0); 

        if (juce::JUCEApplication::isStandaloneApp() && i < portSelectors.size())
        {
            setCentred(*portSelectors[i], portCell.removeFromTop(rowHeight * 0.5), comboW, comboH);
            setCentred(row->chanBox, chanCell.removeFromTop(rowHeight * 0.5), comboW - 5, comboH);

            // VOL Slider positioning
            int volWidth = chanCell.getRight() - portCell.getX() - 10;
            row->mainVolSlider.setBounds(portCell.getX() + 5, midiGroupArea.getCentreY() + 4, volWidth, 14);
        }
        else
        {
            setCentred(row->chanBox, chanCell, comboW - 5, comboH);
            row->mainVolSlider.setVisible(false);
        }
    }
}
