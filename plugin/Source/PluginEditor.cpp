#include "PluginEditor.h"
#include <ceo/Mapping.h>

namespace
{
    constexpr int kKnobSize   = 60;
    constexpr int kExtraTop   = 82;     // from the top of a knob to the small control under its label
    constexpr int kRowPitch   = 116;    // from one knob row to the next
}

//==============================================================================
// Layout (all in the fixed 784 x 672 space)
//   title                                           y 10 to 42
//   left block (x 20 to 220): Input level / Sense, Hold / Dub, Mod amount / Dry/wet, Output level (alone)
//   right block (x 564 to 764): Intensity / Grain window, Grain direction / Length, Scan / Spray, Pitch / Pitch deviation
//   rows 116 apart from y 52; the small controls sit under the label of their knob (y +82)
//   Pitch Snap menu under Pitch, Pitch Scale menu under Pitch deviation (milestone 8)
//   layer dial, centre at (392, 212), 2 times the old size
//   Capture (+ Onset under it) left and Strike (+ Erase, Undo under it) right, under the dial, y 372 to 452
//   stream display: glass x 112 to 672, y 526 to 652, with a 6 wide bezel round it (component x 106 to 678, y 520 to 658)
//==============================================================================

int CeoPanel::rowTop (int row)
{
    return 52 + kRowPitch * row;
}

CeoPanel::CeoPanel (CeoAudioProcessor& p)
    : processor (p),
      state (p.getValueTreeState()),
      layerDial (*p.getValueTreeState().getParameter (CeoParams::layer)),
      display (p.getEngine(), p.getValueTreeState())
{
    // The 15 knobs. Left block: record and output.
    addKnob (CeoParams::inputLevel,     "Input level",     kLeftColumn0,     0);
    addKnob (CeoParams::sense,          "Sense",           kLeftColumn1,     0);
    addKnob (CeoParams::hold,           "Hold",            kLeftColumn0,     1);
    addKnob (CeoParams::dub,            "Dub",             kLeftColumn1,     1);
    addKnob (CeoParams::modAmount,      "Mod amount",      kLeftColumn0,     2);
    addKnob (CeoParams::dryWet,         "Dry/wet",         kLeftColumn1,     2);
    addKnob (CeoParams::outputLevel,    "Output level",    kLeftBlockMiddle, 3);

    // Right block: the grains.
    addKnob (CeoParams::intensity,      "Intensity",       kRightColumn0,    0);
    addKnob (CeoParams::grainWindow,    "Grain window",    kRightColumn1,    0);
    addKnob (CeoParams::grainDirection, "Grain direction", kRightColumn0,    1);
    addKnob (CeoParams::length,         "Length",          kRightColumn1,    1);
    addKnob (CeoParams::scan,           "Scan",            kRightColumn0,    2);
    addKnob (CeoParams::spray,          "Spray",           kRightColumn1,    2);
    setUpPitchReadout (addKnob (CeoParams::pitch, "Pitch", kRightColumn0, 3));
    addKnob (CeoParams::pitchDeviation, "Pitch deviation", kRightColumn1,    3);

    // Buttons. Capture, Onset and Follow stay on or off. Strike, Erase and Undo act on every click.
    setUpLatchingButton (captureButton, "Capture", CeoParams::capture,      captureAttachment);
    setUpLatchingButton (onsetButton,   "Onset",   CeoParams::onsetCapture, onsetAttachment);
    setUpLatchingButton (followButton,  "Follow",  CeoParams::follow,       followAttachment);
    setUpClickButton    (strikeButton,  "Strike",  CeoParams::strike);
    setUpClickButton    (eraseButton,   "Erase",   CeoParams::erase);
    setUpClickButton    (undoButton,    "Undo",    CeoParams::undo);

    // Milestone 10: saving and loading layers as WAV files. No parameters: the files are part of the project, not a control.
    saveWavButton.setButtonText ("SAVE WAV");
    loadWavButton.setButtonText ("LOAD WAV");
    folderButton.setButtonText  ("FOLDER...");
    saveWavButton.onClick = [this] { startSave (selectedLayer()); };
    loadWavButton.onClick = [this] { startLoad (selectedLayer()); };
    folderButton.onClick  = [this] { chooseHome(); };
    for (auto* b : { &saveWavButton, &loadWavButton, &folderButton })
        addAndMakeVisible (*b);
    saveWavButton.setTooltip ("Save the selected layer as a WAV file; the project then stores only a link to it");
    loadWavButton.setTooltip ("Load a WAV file into the selected layer");
    folderButton.setTooltip ("Choose the home folder: where saved layers go, and where moved files are looked for");

    // Milestone 11: Scenes. Two boxes in the free corners beside the title. No parameters: a Scene is files plus a link in the project.
    for (auto* b : { &sceneBox, &loadBox })
    {
        b->setEditableText (false);
        b->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (*b);
    }
    sceneBox.onShowPopup = [this] { showSceneMenu(); };
    loadBox.onShowPopup  = [this] { showLoadMenu(); };
    sceneBox.setTooltip ("Pick a Scene (a folder of six WAV files plus a settings file), or save one");
    loadBox.setTooltip ("What picking a Scene loads: as the file says, layers only, layers + settings, or settings only. Also: join several short files into one layer");
    updateSceneBoxes();

    setUpDropdown (inputModeBox, CeoParams::inputMode, inputModeAttachment);
    setUpDropdown (modTargetBox, CeoParams::modTarget, modTargetAttachment);
    setUpDropdown (pitchSnapBox,  CeoParams::pitchSnap,  pitchSnapAttachment);     // under Pitch
    setUpDropdown (pitchScaleBox, CeoParams::pitchScale, pitchScaleAttachment);    // under Pitch deviation

    addAndMakeVisible (layerDial);
    addAndMakeVisible (display);
    controlIds.push_back (CeoParams::layer);          // the dial is attached to the layer parameter

    reducedMotion = CeoStyle::prefersReducedMotion();
    display.setReducedMotion (reducedMotion);
    layerDial.setReducedMotion (reducedMotion);

    setSize (kWidth, kHeight);
    startTimerHz (30);
}

CeoPanel::~CeoPanel()
{
    stopTimer();
}

CeoKnob& CeoPanel::addKnob (const char* parameterId, const char* label, int centreX, int row)
{
    auto cell = std::make_unique<KnobCell>();
    cell->label   = label;
    cell->centreX = centreX;
    cell->row     = row;
    controlIds.push_back (parameterId);

    auto* parameter = state.getParameter (parameterId);
    const double defaultValue = parameter != nullptr ? (double) parameter->getDefaultValue() : 0.5;

    cell->attachment = std::make_unique<SliderAttachment> (state, parameterId, cell->knob);
    cell->knob.setDoubleClickReturnValue (true, defaultValue);
    cell->knob.setCentreDetent (std::abs (defaultValue - 0.5) < 1.0e-6);
    cell->knob.setPopupDisplayEnabled (true, false, this);
    addAndMakeVisible (cell->knob);

    CeoKnob& knob = cell->knob;
    knobs.push_back (std::move (cell));
    return knob;
}

// The Pitch knob's value bubble (shown while it is dragged) gives the note that really plays, in semitones, as
// the engine works it out: with Pitch Snap on a whole number such as -12 or +7, with Snap Off the smooth value
// to two decimals. It uses the same maths as the engine (ceo/Mapping.h), so the two cannot disagree.
void CeoPanel::setUpPitchReadout (CeoKnob& knob)
{
    knob.textFromValueFunction = [this] (double knobPosition)
    {
        const int mode = (int) (state.getRawParameterValue (CeoParams::pitchSnap)->load() + 0.5f);
        const float semitones = ceo::map::pitchSemitonesSnapped ((float) knobPosition, mode);

        if (mode == ceo::map::kSnapModeOff)
        {
            const double rounded = std::round ((double) semitones * 100.0) / 100.0;
            if (rounded == 0.0)
                return juce::String ("0.00");
            return (rounded > 0.0 ? juce::String ("+") : juce::String()) + juce::String (rounded, 2);
        }

        const int whole = (int) std::lround (semitones);
        return whole > 0 ? "+" + juce::String (whole) : juce::String (whole);
    };
}

void CeoPanel::flipParameter (const char* parameterId)
{
    if (auto* parameter = state.getParameter (parameterId))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->getValue() > 0.5f ? 0.0f : 1.0f);
        parameter->endChangeGesture();
    }
}

void CeoPanel::setUpLatchingButton (juce::TextButton& button, const char* text, const char* parameterId,
                                    std::unique_ptr<ButtonAttachment>& attachment)
{
    controlIds.push_back (parameterId);
    button.setButtonText (text);
    button.setClickingTogglesState (true);
    attachment = std::make_unique<ButtonAttachment> (state, parameterId, button);
    addAndMakeVisible (button);
}

// Strike, Erase and Undo count every change of their switch as one click (the engine works that way), so each
// click of the button flips the switch once. The button only looks pressed while the mouse is held down.
void CeoPanel::setUpClickButton (juce::TextButton& button, const char* text, const char* parameterId)
{
    controlIds.push_back (parameterId);
    button.setButtonText (text);
    button.setClickingTogglesState (false);
    button.onClick = [this, parameterId] { flipParameter (parameterId); };
    addAndMakeVisible (button);
}

void CeoPanel::setUpDropdown (juce::ComboBox& box, const char* parameterId, std::unique_ptr<ComboBoxAttachment>& attachment)
{
    controlIds.push_back (parameterId);
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (parameterId)))
        box.addItemList (choice->choices, 1);

    attachment = std::make_unique<ComboBoxAttachment> (state, parameterId, box);
    addAndMakeVisible (box);
}

juce::Rectangle<int> CeoPanel::knobBounds (const KnobCell& cell) const
{
    // Drawn in an 84 x 84 square round the same centre as the old 60 x 60 knob (its scale ticks reach beyond it).
    return juce::Rectangle<int> (CeoGeo::knobBox, CeoGeo::knobBox).withCentre ({ cell.centreX, rowTop (cell.row) + kKnobSize / 2 });
}

juce::Rectangle<int> CeoPanel::ledArea() const
{
    const int cx = kRightColumn0;
    return { cx - 40, rowTop (1) + kExtraTop, 80, 20 };       // under the Grain direction label, clear of the value bubble
}

void CeoPanel::resized()
{
    for (auto& cell : knobs)
        cell->knob.setBounds (knobBounds (*cell));

    // The layer dial, centred; its middle is at (392, 212).
    layerDial.setBounds (kWidth / 2 - LayerDial::kWidth / 2, 72, LayerDial::kWidth, LayerDial::kHeight);

    // Under the dial: Capture (and Onset under it) on the left, Strike (and Erase, Undo under it) on the right.
    captureButton.setBounds (240, 372, 100, 44);
    onsetButton.setBounds   (240, 424, 100, 28);
    strikeButton.setBounds  (444, 372, 100, 44);
    eraseButton.setBounds   (444, 424,  48, 28);
    undoButton.setBounds    (496, 424,  48, 28);

    // Milestone 10: SAVE WAV, LOAD WAV, FOLDER... in the free band under Capture and Strike (the two status lines are painted under them).
    saveWavButton.setBounds (249, 460, 90, 26);
    loadWavButton.setBounds (347, 460, 90, 26);
    folderButton.setBounds  (445, 460, 90, 26);

    // Milestone 11: the Scene box and the Load box, in the free corners at the top (the title is in the middle).
    sceneBox.setBounds (20, 11, 226, 26);
    loadBox.setBounds  (538, 11, 226, 26);

    display.setBounds (106, 520, CeoGeo::displayW, CeoGeo::displayH);     // the glass stays at x 112, y 526, 560 x 126

    auto extraUnder = [this] (int row, int centreX, int width, int height)
    {
        return juce::Rectangle<int> (width, height).withCentre ({ centreX, rowTop (row) + kExtraTop + height / 2 });
    };
    followButton.setBounds (extraUnder (2, kRightColumn0, 76, 24));      // under Scan
    modTargetBox.setBounds (extraUnder (2, kLeftColumn0, 92, 24));       // under Mod amount
    inputModeBox.setBounds (extraUnder (0, kLeftColumn1, 92, 24));       // under Sense
    pitchSnapBox.setBounds  (pitchSnapSlot());                           // under Pitch
    pitchScaleBox.setBounds (pitchScaleSlot());                          // under Pitch deviation
}

void CeoPanel::renderGround (float scale)
{
    using namespace CeoStyle;

    const int pw = juce::jmax (1, juce::roundToInt ((float) kWidth * scale));
    const int ph = juce::jmax (1, juce::roundToInt ((float) kHeight * scale));
    ground = juce::Image (juce::Image::ARGB, pw, ph, true);
    juce::Graphics g (ground);
    g.addTransform (juce::AffineTransform::scale ((float) pw / (float) kWidth, (float) ph / (float) kHeight));

    // Panel, grain, bevel, screws and the title "Ceo" with its grave accent
    art->paintGround (g, scale);

    // Knob labels
    g.setColour (lettering());
    g.setFont (font (12.5f).withExtraKerningFactor (0.5f / 12.5f));
    for (const auto& cell : knobs)
        g.drawSingleLineText (cell->label, cell->centreX, rowTop (cell->row) + 75, juce::Justification::horizontallyCentred);
}

void CeoPanel::paint (juce::Graphics& g)
{
    using namespace CeoStyle;

    // The static art is one cached picture, made at the pixel scale the panel is really shown at.
    const float physical = g.getInternalContext().getPhysicalPixelScaleFactor();
    const float quantised = CeoArt::quantise (physical);
    if (ground.isNull() || groundScale != quantised)
    {
        renderGround (quantised);
        groundScale = quantised;
    }
    g.drawImage (ground, getLocalBounds().toFloat());

    // The two status lines of milestone 10 (line A: the selected layer's file; line B: the home folder and what is unsaved).
    // Skipped when only a small corner (the direction lights, say) is being repainted.
    if (g.getClipBounds().intersects (juce::Rectangle<int> (92, 487, kWidth - 184, 34)))
    {
        g.setColour (statusColour);
        drawMixedText (g, statusA, juce::Rectangle<float> (92.0f, 487.0f, (float) kWidth - 184.0f, 18.0f), font (12.5f), fontGreek (14.0f));
        g.setColour (lettering().withAlpha (0.6f));
        drawMixedText (g, statusB, juce::Rectangle<float> (92.0f, 504.0f, (float) kWidth - 184.0f, 16.0f), font (11.0f), fontGreek (12.0f));
    }

    // The two direction lights under Grain direction: backwards on the left, forwards on the right.
    const auto leds = ledArea();
    const float forward = display.forwardActivity();
    const float reverse = display.reverseActivity();
    const float cy = (float) leds.getCentreY();
    const float cx = (float) leds.getCentreX();

    auto drawLed = [&] (float x, float level)
    {
        g.setColour (juce::Colour (0xFF0B0604));
        g.fillEllipse (x - 4.0f, cy - 4.0f, 8.0f, 8.0f);
        g.setColour (goldRim().withAlpha (0.6f));
        g.drawEllipse (x - 4.0f, cy - 4.0f, 8.0f, 8.0f, 1.0f);
        if (level > 0.5f)
        {
            g.setOpacity (level);
            g.drawImage (art->halo (7), juce::Rectangle<float> (x - 9.0f, cy - 9.0f, 18.0f, 18.0f));
        }
        g.setColour (glow().withAlpha (0.15f + 0.85f * level));
        g.fillEllipse (x - 3.0f, cy - 3.0f, 6.0f, 6.0f);
    };
    drawLed (cx - 14.0f, reverse);
    drawLed (cx + 14.0f, forward);

    g.setColour (goldRim().withAlpha (0.8f));
    juce::Path back, fwd;
    // The arrows point outwards: left = backwards, right = forwards.
    back.addTriangle (cx - 27.0f, cy, cx - 22.0f, cy - 3.0f, cx - 22.0f, cy + 3.0f);
    fwd.addTriangle  (cx + 27.0f, cy, cx + 22.0f, cy - 3.0f, cx + 22.0f, cy + 3.0f);
    g.fillPath (back);
    g.fillPath (fwd);
}

int CeoPanel::selectedLayer() const
{
    return (int) (state.getRawParameterValue (CeoParams::layer)->load() + 0.5f);     // 0..5 = alpha..zeta, 6 = Omega
}

void CeoPanel::showMessage (const juce::String& text)
{
    messageText = text;
    messageTicks = 270;         // about 9 seconds
}

void CeoPanel::startSave (int layer)
{
    if (layer < 0 || layer >= ceo::kNumLayers)
        return;
    auto& lib = processor.getLayerLibrary();
    const auto home = processor.getHomeFolder();
    home.createDirectory();
    chooser = std::make_unique<juce::FileChooser> ("Save layer " + CeoStyle::layerName (layer) + " as a WAV file (the project then stores a link to it)",
                                                   home.getChildFile (juce::String::fromUTF8 (lib.suggestedFileName (layer).c_str())), "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, layer] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File())
            return;                                             // cancelled
        if (! f.hasFileExtension ("wav"))
            f = f.withFileExtension ("wav");
        std::string why;
        if (! processor.getLayerLibrary().saveLayer (layer, f.getFullPathName().toStdString(), why))
            showMessage (juce::String::fromUTF8 (why.c_str()));
    });
}

void CeoPanel::startLoad (int layer)
{
    if (layer < 0 || layer >= ceo::kNumLayers)
        return;
    chooser = std::make_unique<juce::FileChooser> ("Load a WAV file into layer " + CeoStyle::layerName (layer) + " (the project then stores a link to it)",
                                                   processor.getHomeFolder(), "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, layer] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f == juce::File())
            return;                                             // cancelled
        std::string why;
        if (! processor.getLayerLibrary().loadLayer (layer, f.getFullPathName().toStdString(), why))
            showMessage (juce::String::fromUTF8 (why.c_str()));
    });
}

void CeoPanel::chooseHome()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose the home folder for layer files (saved layers go here; moved files are looked for here)",
                                                   processor.getHomeFolder(), "");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
    {
        const auto d = fc.getResult();
        if (d == juce::File() || ! d.isDirectory())
            return;
        processor.setHomeFolder (d);
        showMessage ("Home folder set.");
    });
}

//==============================================================================
// Milestone 11: Scenes
namespace
{
    const char* const kLoadNames[4] = { "As the file says", "Layers only", "Scene (layers + settings)", "Settings only" };
}

void CeoPanel::updateSceneBoxes()
{
    const auto ss = processor.getSceneStatus();
    const auto amber = juce::Colour (0xFFE0B050), red = juce::Colour (0xFFD9776F);
    juce::String t = "Scene: ";
    t << (ss.state == 0 ? juce::String ("none") : ss.name);
    if (ss.state == 2) t << " (changed)";
    if (ss.state == 3) t << " (missing)";
    const auto colour = ss.state == 2 ? amber : ss.state == 3 ? red : CeoStyle::cream();
    if (t != sceneBox.getText())
        sceneBox.setText (t, juce::dontSendNotification);
    if (colour != sceneBoxColour)
    {
        sceneBoxColour = colour;
        sceneBox.setColour (juce::ComboBox::textColourId, colour);
        sceneBox.repaint();
    }
    const auto l = juce::String ("Load: ") + kLoadNames[juce::jlimit (0, 3, processor.getLoadWhat())];
    if (l != loadBox.getText())
        loadBox.setText (l, juce::dontSendNotification);
}

void CeoPanel::showLoadMenu()
{
    juce::PopupMenu m;
    m.setLookAndFeel (&getLookAndFeel());
    for (int i = 0; i < 4; ++i)
        m.addItem (i + 1, kLoadNames[i], true, processor.getLoadWhat() == i);
    m.addSeparator();
    m.addItem (10, "Join short files into one layer", true, processor.getJoinShort());      // milestone 12
    juce::Component::SafePointer<CeoPanel> self (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&loadBox), [self] (int r)
    {
        if (self == nullptr || r <= 0)
            return;
        if (r == 10)
            self->processor.setJoinShort (! self->processor.getJoinShort());
        else
            self->processor.setLoadWhat (r - 1);
        self->updateSceneBoxes();
    });
}

void CeoPanel::showSceneMenu()
{
    const auto scenesFolder = processor.getScenesFolder();
    const auto current = processor.getSceneFolder();
    const auto scenes = ceo::scene::listScenes (scenesFolder.getFullPathName().toStdString());

    juce::PopupMenu m;
    m.setLookAndFeel (&getLookAndFeel());
    juce::StringArray paths;
    for (const auto& s : scenes)
    {
        const juce::File f (juce::String::fromUTF8 (s.c_str()));
        paths.add (f.getFullPathName());
        m.addItem (1 + paths.size() - 1, f.getFileName(), true, f == current);
    }
    if (paths.isEmpty())
        m.addItem (999, "(no Scenes in the Scenes folder yet)", false, false);
    m.addSeparator();
    m.addItem (1000, "Browse for a Scene folder...");
    m.addItem (1001, "Save Scene...");
    m.addItem (1002, "Scenes folder...");
    m.addItem (1003, "What the last load set and ignored...");

    juce::Component::SafePointer<CeoPanel> self (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sceneBox), [self, paths] (int r)
    {
        if (self == nullptr || r <= 0)
            return;
        if (r >= 1 && r <= paths.size())      self->beginSceneLoad (juce::File (paths[r - 1]));
        else if (r == 1000)                   self->chooseSceneToLoad();
        else if (r == 1001)                   self->startSaveScene();
        else if (r == 1002)                   self->chooseScenesFolder();
        else if (r == 1003)                   self->showSceneReport();
    });
}

void CeoPanel::chooseSceneToLoad()
{
    auto start = processor.getScenesFolder();
    if (! start.isDirectory())
        start = processor.getHomeFolder();
    chooser = std::make_unique<juce::FileChooser> ("Choose a Scene folder (a folder of WAV files, for example an Arbhar scene folder)", start, "");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
    {
        const auto d = fc.getResult();
        if (d != juce::File() && d.isDirectory())
            beginSceneLoad (d);
    });
}

void CeoPanel::beginSceneLoad (const juce::File& folder)
{
    const auto plan = processor.planSceneLoad (folder);
    if (! plan.ok)
    {
        showMessage (plan.message);
        return;
    }
    if (plan.unsavedLayers.isEmpty())
    {
        finishSceneLoad (plan);
        return;
    }
    const int n = plan.unsavedLayers.size();
    const auto text = juce::String (n) + (n == 1 ? " layer (" : " layers (") + plan.unsavedLayers.joinIntoString (", ")
                    + (n == 1 ? ") is not saved to a file and will be replaced." : ") are not saved to a file and will be replaced.")
                    + "\n\n(A layer that has no file in a Scene stays as it is.)";
    juce::Component::SafePointer<CeoPanel> self (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle ("Load Scene " + folder.getFileName() + "?")
                                      .withMessage (text)
                                      .withButton ("Load")
                                      .withButton ("Cancel"),
                                  [self, plan] (int result)
    {
        if (result == 1 && self != nullptr)
            self->finishSceneLoad (plan);
    });
}

void CeoPanel::finishSceneLoad (const CeoAudioProcessor::ScenePlan& plan)
{
    const auto r = processor.runSceneLoad (plan);
    showMessage (r.message);
    updateSceneBoxes();
}

void CeoPanel::startSaveScene()
{
    auto start = processor.getScenesFolder();
    start.createDirectory();
    chooser = std::make_unique<juce::FileChooser> ("Save Scene: choose or make a folder. Files already in it are renamed (_was_...), never deleted.", start, "");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
    {
        const auto d = fc.getResult();
        if (d == juce::File() || ! d.isDirectory())
            return;
        juce::String why;
        if (processor.startSceneSave (d, why))
            showMessage ("Saving Scene " + d.getFileName() + "...");
        else
            showMessage (why);
    });
}

void CeoPanel::chooseScenesFolder()
{
    auto start = processor.getScenesFolder();
    if (! start.isDirectory())
        start = processor.getHomeFolder();
    chooser = std::make_unique<juce::FileChooser> ("Choose the Scenes folder (the Scene menu lists the folders inside it)", start, "");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
    {
        const auto d = fc.getResult();
        if (d == juce::File() || ! d.isDirectory())
            return;
        processor.setScenesFolder (d);
        showMessage ("Scenes folder set.");
    });
}

void CeoPanel::showSceneReport()
{
    auto r = processor.getSceneReport();
    if (r.isEmpty())
        r = "No Scene has been loaded yet in this session.";
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::InfoIcon)
                                      .withTitle ("What the last Scene load did")
                                      .withMessage (r)
                                      .withButton ("OK"),
                                  nullptr);
}

void CeoPanel::updateFileStrip()
{
    using namespace CeoStyle;
    auto& lib = processor.getLayerLibrary();
    const auto& engine = processor.getEngine();
    const int layer = selectedLayer();
    const bool isOmega = layer >= ceo::kNumLayers;
    const bool recording = engine.anyLayerRecording() || state.getRawParameterValue (CeoParams::capture)->load() > 0.5f;
    const auto amber = juce::Colour (0xFFE0B050), red = juce::Colour (0xFFD9776F);

    int unsaved = 0;
    for (int i = 0; i < ceo::kNumLayers; ++i)
        unsaved += lib.info (i).unsaved ? 1 : 0;

    juce::String shown = processor.getHomeFolder().getFullPathName();
    const auto userHome = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
    if (shown.startsWith (userHome))
        shown = "~" + shown.substring (userHome.length());
    juce::String b = "Home: " + shown + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  "))
                   + (unsaved == 0 ? juce::String ("all saved") : juce::String (unsaved) + (unsaved == 1 ? " layer not saved" : " layers not saved"));

    juce::String a;
    auto colour = cream();
    bool busy = false;
    if (isOmega)
    {
        a = layerName (6) + "  Pick a layer " + layerName (0) + " to " + layerName (5) + " to save or load";
        colour = cream().withAlpha (0.7f);
    }
    else
    {
        const auto li = lib.info (layer);
        const auto name = juce::String::fromUTF8 (li.fileName.c_str());
        auto msg = juce::String::fromUTF8 (li.message.c_str());
        msg = msg.substring (0, 1).toUpperCase() + msg.substring (1);       // "missing: x.wav" is shown as "Missing: x.wav"
        const auto head = layerName (layer) + "  ";
        busy = li.status == ceo::LayerLibrary::Waiting || li.status == ceo::LayerLibrary::Working;
        if (busy)               { a = head + msg; colour = amber; b = "(if nothing happens, press Play: Logic must run audio)"; }
        else if (li.status == ceo::LayerLibrary::Missing)
                                { a = head + msg; colour = red; b = "Put it back, or point FOLDER... at it"; }
        else if (li.status == ceo::LayerLibrary::Failed)
                                { a = head + msg; colour = red; }
        else if (li.bound)      { a = head + "linked to " + name + (li.files > 1 ? " + " + juce::String (li.files - 1) + " more (joined)" : juce::String()) + (li.status == ceo::LayerLibrary::Changed ? "  (file changed since the project was saved)" : ""); colour = li.status == ceo::LayerLibrary::Changed ? amber : cream(); }
        else if (li.unsaved)    { a = head + "Not saved to a file: SAVE WAV to keep it"; colour = amber; }
        else                    { a = head + "empty  -  LOAD WAV to bring a file in"; colour = cream().withAlpha (0.7f); }
    }
    if (recording && ! isOmega && ! busy)
        a = layerName (layer) + "  recording...";

    {
        const auto done = processor.takeSceneMessage();          // a Scene save that has just finished
        if (done.isNotEmpty())
            showMessage (done);
    }
    if (messageTicks == 0 && ! busy && processor.getSceneStatus().state == 3)
        b = "Put the Scene folder back, or point Scene at it";
    if (messageTicks > 0)
    {
        --messageTicks;
        b = messageText;
    }

    // Anything running (any layer) greys everything but FOLDER...
    const bool anyBusy = lib.busy();
    const bool ready = engine.layerFormat().valid;
    const bool canUse = ! isOmega && ! recording && ! anyBusy && ready;
    saveWavButton.setEnabled (canUse && engine.layerState (juce::jmin (layer, ceo::kNumLayers - 1)) == ceo::Engine::LayerFilled);
    loadWavButton.setEnabled (canUse);
    sceneBox.setEnabled (! recording && ! anyBusy && ready);
    updateSceneBoxes();

    if (a != statusA || b != statusB || colour != statusColour)
    {
        statusA = a; statusB = b; statusColour = colour;
        repaint (92, 487, kWidth - 184, 34);
    }
}

void CeoPanel::timerCallback()
{
    // Once a second: has the system been asked for less motion?
    if (--motionCheckTicks <= 0)
    {
        motionCheckTicks = 30;
        const bool reduce = CeoStyle::prefersReducedMotion();
        if (reduce != reducedMotion)
        {
            reducedMotion = reduce;
            display.setReducedMotion (reduce);
            layerDial.setReducedMotion (reduce);
        }
    }

    display.tick();
    layerDial.tickAnimation();
    updateFileStrip();
    repaint (ledArea());
}

//==============================================================================
CeoEditor::CeoEditor (CeoAudioProcessor& p)
    : juce::AudioProcessorEditor (p),
      panel (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (panel);

    setResizable (true, true);
    setResizeLimits ((int) (CeoPanel::kWidth * 0.6), (int) (CeoPanel::kHeight * 0.6),
                     (int) (CeoPanel::kWidth * 1.8), (int) (CeoPanel::kHeight * 1.8));
    if (auto* aspect = getConstrainer())
        aspect->setFixedAspectRatio ((double) CeoPanel::kWidth / (double) CeoPanel::kHeight);

    setSize (CeoPanel::kWidth, CeoPanel::kHeight);
}

CeoEditor::~CeoEditor()
{
    setLookAndFeel (nullptr);
}

void CeoEditor::paint (juce::Graphics& g)
{
    g.fillAll (CeoStyle::windowBackground());
}

void CeoEditor::resized()
{
    panel.setBounds (0, 0, CeoPanel::kWidth, CeoPanel::kHeight);
    panel.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) CeoPanel::kWidth));
}
