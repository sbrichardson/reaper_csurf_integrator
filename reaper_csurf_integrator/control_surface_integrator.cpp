//
//  control_surface_integrator.cpp
//  reaper_control_surface_integrator
//
//

#include "control_surface_integrator.h"
#include "control_surface_midi_widgets.h"
#include "control_surface_action_contexts.h"
#include "control_surface_Reaper_actions.h"
#include "control_surface_manager_actions.h"
#include "control_surface_integrator_ui.h"

extern reaper_plugin_info_t *g_reaper_plugin_info;

string GetLineEnding()
{
#ifdef WIN32
    return "\n";
#else
    return "\r\n" ;
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct MidiInputPort
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
    int port_ = 0;
    midi_Input* midiInput_ = nullptr;
    
    MidiInputPort(int port, midi_Input* midiInput) : port_(port), midiInput_(midiInput) {}
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct MidiOutputPort
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
    int port_ = 0;
    midi_Output* midiOutput_ = nullptr;
    
    MidiOutputPort(int port, midi_Output* midiOutput) : port_(port), midiOutput_(midiOutput) {}
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Midi I/O Manager
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static map<int, MidiInputPort*> midiInputs_;
static map<int, MidiOutputPort*> midiOutputs_;

static midi_Input* GetMidiInputForPort(int inputPort)
{
    if(midiInputs_.count(inputPort) > 0)
        return midiInputs_[inputPort]->midiInput_; // return existing
    
    // otherwise make new
    midi_Input* newInput = DAW::CreateMIDIInput(inputPort);
    
    if(newInput)
    {
        newInput->start();
        midiInputs_[inputPort] = new MidiInputPort(inputPort, newInput);
        return newInput;
    }
    
    return nullptr;
}

static midi_Output* GetMidiOutputForPort(int outputPort)
{
    if(midiOutputs_.count(outputPort) > 0)
        return midiOutputs_[outputPort]->midiOutput_; // return existing
    
    // otherwise make new
    midi_Output* newOutput = DAW::CreateMIDIOutput(outputPort, false, NULL);
    
    if(newOutput)
    {
        midiOutputs_[outputPort] = new MidiOutputPort(outputPort, newOutput);
        return newOutput;
    }
    
    return nullptr;
}

void ShutdownMidiIO()
{
    for(auto [index, input] : midiInputs_)
        input->midiInput_->stop();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// OSC I/O Manager
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static map<string, oscpkt::UdpSocket*> inputSockets_;
static map<string, oscpkt::UdpSocket*> outputSockets_;

static oscpkt::UdpSocket* GetInputSocketForPort(string surfaceName, int inputPort)
{
    if(inputSockets_.count(surfaceName) > 0)
        return inputSockets_[surfaceName]; // return existing
    
    // otherwise make new
    oscpkt::UdpSocket* newInputSocket = new oscpkt::UdpSocket();
    
    if(newInputSocket)
    {
        newInputSocket->bindTo(inputPort);
        
        if (! newInputSocket->isOk())
        {
            //cerr << "Error opening port " << PORT_NUM << ": " << inSocket_.errorMessage() << "\n";
            return nullptr;
        }
        
        inputSockets_[surfaceName] = newInputSocket;
        
        return inputSockets_[surfaceName];
    }
    
    return nullptr;
}

static oscpkt::UdpSocket* GetOutputSocketForAddressAndPort(string surfaceName, string address, int outputPort)
{
    if(outputSockets_.count(surfaceName) > 0)
        return outputSockets_[surfaceName]; // return existing
    
    // otherwise make new
    oscpkt::UdpSocket* newOutputSocket = new oscpkt::UdpSocket();
    
    if(newOutputSocket)
    {
        if( ! newOutputSocket->connectTo(address, outputPort))
        {
            //cerr << "Error connecting " << remoteDeviceIP_ << ": " << outSocket_.errorMessage() << "\n";
            return nullptr;
        }
        
        newOutputSocket->bindTo(outputPort);
        
        if ( ! newOutputSocket->isOk())
        {
            //cerr << "Error opening port " << outPort_ << ": " << outSocket_.errorMessage() << "\n";
            return nullptr;
        }

        outputSockets_[surfaceName] = newOutputSocket;
        
        return outputSockets_[surfaceName];
    }
    
    return nullptr;
}

//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
// Parsing
//////////////////////////////////////////////////////////////////////////////////////////////
static vector<string> GetTokens(string line)
{
    vector<string> tokens;
    
    istringstream iss(line);
    string token;
    while (iss >> quoted(token))
        tokens.push_back(token);
    
    return tokens;
}

static void listZoneFiles(const string &path, vector<string> &results)
{
    regex rx(".*\\.zon$");
    
    if (auto dir = opendir(path.c_str())) {
        while (auto f = readdir(dir)) {
            if (!f->d_name || f->d_name[0] == '.') continue;
            if (f->d_type == DT_DIR)
                listZoneFiles(path + f->d_name + "/", results);
            
            if (f->d_type == DT_REG)
                if(regex_match(f->d_name, rx))
                    results.push_back(path + f->d_name);
        }
        closedir(dir);
    }
}

static void GetWidgetNameAndProperties(string line, string &widgetName, string &modifier, bool &isPressRelease, bool &isInverted, bool &shouldToggle, double &delayAmount)
{
    istringstream modified_role(line);
    vector<string> modifier_tokens;
    vector<string> modifierSlots = { "", "", "", "", "", "" };
    string modifier_token;
    
    while (getline(modified_role, modifier_token, '+'))
        modifier_tokens.push_back(modifier_token);
    
    if(modifier_tokens.size() > 1)
    {
        for(int i = 0; i < modifier_tokens.size() - 1; i++)
        {
            if(modifier_tokens[i] == Shift)
                modifierSlots[0] = Shift + "+";
            else if(modifier_tokens[i] == Option)
                modifierSlots[1] = Option + "+";
            else if(modifier_tokens[i] == Control)
                modifierSlots[2] = Control + "+";
            else if(modifier_tokens[i] == Alt)
                modifierSlots[3] = Alt + "+";
            else if(modifier_tokens[i] == "FaderTouch")
                modifierSlots[4] = "FaderTouch+";
            else if(modifier_tokens[i] == "RotaryTouch")
                modifierSlots[5] = "RotaryTouch+";

            else if(modifier_tokens[i] == "PR")
                isPressRelease = true;
            else if(modifier_tokens[i] == "Invert")
                isInverted = true;
            else if(modifier_tokens[i] == "Toggle")
                shouldToggle = true;
            else if(modifier_tokens[i] == "Hold")
                delayAmount = 1.0;
        }
    }

    widgetName = modifier_tokens[modifier_tokens.size() - 1];
    
    modifier = modifierSlots[0] + modifierSlots[1] + modifierSlots[2] + modifierSlots[3] + modifierSlots[4] + modifierSlots[5];
}

static void ProcessZoneFile(string filePath, ControlSurface* surface)
{
    vector<string> includedZones;
    bool isInIncludedZonesSection = false;
    
    map<string, map<string, vector<ActionTemplate*>>> widgetActions;
    
    string zoneName = "";
    string zoneAlias = "";
    string navigatorName = "";
    string actionName = "";
    int lineNumber = 0;
    
    try
    {
        ifstream file(filePath);
        
        for (string line; getline(file, line) ; )
        {           
            line = regex_replace(line, regex(TabChars), " ");
            line = regex_replace(line, regex(CRLFChars), "");
            
            line = line.substr(0, line.find("//")); // remove trailing commewnts
            
            lineNumber++;
            
            // Trim leading and trailing spaces
            line = regex_replace(line, regex("^\\s+|\\s+$"), "", regex_constants::format_default);
            
            if(line == "" || (line.size() > 0 && line[0] == '/')) // ignore blank lines and comment lines
                continue;
            
            vector<string> tokens(GetTokens(line));
            
            if(tokens.size() > 0)
            {
                if(tokens[0] == "Zone")
                {
                    zoneName = tokens.size() > 1 ? tokens[1] : "";
                    zoneAlias = tokens.size() > 2 ? tokens[2] : zoneName;
                }
                
                else if(tokens[0] == "ZoneEnd" && zoneName != "")
                {
                    vector<WidgetActionTemplate*> widgetActionTemplates;

                    for(auto [widgetName, modifierActions] : widgetActions)
                    {
                        WidgetActionTemplate* widgetActionTemplate = new WidgetActionTemplate(widgetName);
                        
                        if(actionName == Shift || actionName == Option || actionName == Control || actionName == Alt)
                            widgetActionTemplate->isModifier = true;

                        for(auto [modifier, actions] : modifierActions)
                        {
                            ActionBundleTemplate* modifierTemplate = new ActionBundleTemplate(modifier);
                            
                            for(auto action : actions)
                                modifierTemplate->members.push_back(action);
                            
                            widgetActionTemplate->actionBundleTemplates.push_back(modifierTemplate);
                        }
                        
                        widgetActionTemplates.push_back(widgetActionTemplate);
                    }
                    
                    vector<Navigator*> navigators;

                    if(navigatorName == "")
                        navigators.push_back(surface->GetPage()->GetTrackNavigationManager()->GetDefaultNavigator());
                    if(navigatorName == "SelectedTrackNavigator")
                        navigators.push_back(surface->GetPage()->GetTrackNavigationManager()->GetSelectedTrackNavigator());
                    else if(navigatorName == "FocusedFXNavigator")
                        navigators.push_back(surface->GetPage()->GetTrackNavigationManager()->GetFocusedFXNavigator());
                    else if(navigatorName == "MasterTrackNavigator")
                        navigators.push_back(surface->GetPage()->GetTrackNavigationManager()->GetMasterTrackNavigator());
                    else if(navigatorName == "TrackNavigator")
                    {
                        for(int i = 0; i < surface->GetNumChannels(); i++)
                            navigators.push_back(surface->GetNavigatorForChannel(i));
                    }
                    else if(navigatorName == "SendNavigator")
                    {
                        for(int i = 0; i < surface->GetNumSends(); i++)
                            navigators.push_back(surface->GetPage()->GetSendNavigationManager()->AddNavigator());
                    }
                    
                    surface->AddZoneTemplate(new ZoneTemplate(navigators, zoneName, zoneAlias, filePath, includedZones, widgetActionTemplates));
                    
                    includedZones.clear();
                    widgetActions.clear();
                }
                
                else if(tokens[0] == "TrackNavigator" || tokens[0] == "MasterTrackNavigator" || tokens[0] == "SelectedTrackNavigator" || tokens[0] == "FocusedFXNavigator" || tokens[0] == "ParentNavigator" || tokens[0] == "SendNavigator")
                    navigatorName = tokens[0];
                
                else if(tokens[0] == "IncludedZones")
                    isInIncludedZonesSection = true;
                
                else if(tokens[0] == "IncludedZonesEnd")
                    isInIncludedZonesSection = false;
                
                else if(tokens.size() == 1 && isInIncludedZonesSection)
                    includedZones.push_back(tokens[0]);
                
                else if(tokens.size() > 1)
                {
                    actionName = tokens[1];
                    
                    string widgetName = "";
                    string modifier = "";
                    bool isPressRelease = false;
                    bool isInverted = false;
                    bool shouldToggle = false;
                    double delayAmount = 0.0;
                    
                    GetWidgetNameAndProperties(tokens[0], widgetName, modifier, isPressRelease, isInverted, shouldToggle, delayAmount);
                    
                    vector<string> params;
                    for(int i = 1; i < tokens.size(); i++)
                        params.push_back(tokens[i]);
                    
                    widgetActions[widgetName][modifier].push_back(new ActionTemplate(actionName, params, isPressRelease, isInverted, shouldToggle, delayAmount));
                }
            }
        }
    }
    catch (exception &e)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", filePath.c_str(), lineNumber);
        DAW::ShowConsoleMsg(buffer);
    }
}

void SetRGB(vector<string> params, bool &supportsRGB, bool &supportsTrackColor, vector<rgb_color> &RGBValues)
{
    vector<int> rawValues;
    
    auto openCurlyBrace = find(params.begin(), params.end(), "{");
    auto closeCurlyBrace = find(params.begin(), params.end(), "}");
    
    if(openCurlyBrace != params.end() && closeCurlyBrace != params.end())
    {
        for(auto it = openCurlyBrace + 1; it != closeCurlyBrace; ++it)
        {
            string strVal = *(it);
            
            if(strVal == "Track")
            {
                supportsTrackColor = true;
                break;
            }
            else
            {
                if(regex_match(strVal, regex("[0-9]+")))
                {
                    int value = stoi(strVal);
                    value = value < 0 ? 0 : value;
                    value = value > 255 ? 255 : value;
                    
                    rawValues.push_back(value);
                }
            }
        }
        
        if(rawValues.size() % 3 == 0 && rawValues.size() > 2)
        {
            supportsRGB = true;
            
            for(int i = 0; i < rawValues.size(); i += 3)
            {
                rgb_color color;
                
                color.r = rawValues[i];
                color.g = rawValues[i + 1];
                color.b = rawValues[i + 2];
                
                RGBValues.push_back(color);
            }
        }
    }
}

void SetSteppedValues(vector<string> params, double &deltaValue, vector<double> &acceleratedDeltaValues, double &rangeMinimum, double &rangeMaximum, vector<double> &steppedValues, vector<int> &acceleratedTickValues)
{
    auto openSquareBrace = find(params.begin(), params.end(), "[");
    auto closeSquareBrace = find(params.begin(), params.end(), "]");
    
    if(openSquareBrace != params.end() && closeSquareBrace != params.end())
    {
        for(auto it = openSquareBrace + 1; it != closeSquareBrace; ++it)
        {
            string strVal = *(it);
            
            if(regex_match(strVal, regex("-?[0-9]+[.][0-9]+")) || regex_match(strVal, regex("-?[0-9]+")))
                steppedValues.push_back(stod(strVal));
            else if(regex_match(strVal, regex("[(]-?[0-9]+[.][0-9]+[)]")))
                deltaValue = stod(strVal.substr( 1, strVal.length() - 2 ));
            else if(regex_match(strVal, regex("[(]-?[0-9]+[)]")))
                acceleratedTickValues.push_back(stod(strVal.substr( 1, strVal.length() - 2 )));
            else if(regex_match(strVal, regex("[(](-?[0-9]+[.][0-9]+[,])+-?[0-9]+[.][0-9]+[)]")))
            {
                istringstream acceleratedDeltaValueStream(strVal.substr( 1, strVal.length() - 2 ));
                string deltaValue;
                
                while (getline(acceleratedDeltaValueStream, deltaValue, ','))
                    acceleratedDeltaValues.push_back(stod(deltaValue));
            }
            else if(regex_match(strVal, regex("[(](-?[0-9]+[,])+-?[0-9]+[)]")))
            {
                istringstream acceleratedTickValueStream(strVal.substr( 1, strVal.length() - 2 ));
                string tickValue;
                
                while (getline(acceleratedTickValueStream, tickValue, ','))
                    acceleratedTickValues.push_back(stod(tickValue));
            }
            else if(regex_match(strVal, regex("-?[0-9]+[.][0-9]+[>]-?[0-9]+[.][0-9]+")) || regex_match(strVal, regex("[0-9]+[-][0-9]+")))
            {
                istringstream range(strVal);
                vector<string> range_tokens;
                string range_token;
                
                while (getline(range, range_token, '>'))
                    range_tokens.push_back(range_token);
                
                if(range_tokens.size() == 2)
                {
                    double firstValue = stod(range_tokens[0]);
                    double lastValue = stod(range_tokens[1]);
                    
                    if(lastValue > firstValue)
                    {
                        rangeMinimum = firstValue;
                        rangeMaximum = lastValue;
                    }
                    else
                    {
                        rangeMinimum = lastValue;
                        rangeMaximum = firstValue;
                    }
                }
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////////
// Widgets
//////////////////////////////////////////////////////////////////////////////
static int strToHex(string valueStr)
{
    return strtol(valueStr.c_str(), nullptr, 16);
}

static double strToDouble(string valueStr)
{
    return strtod(valueStr.c_str(), nullptr);
}

static void ProcessMidiWidget(int &lineNumber, ifstream &surfaceTemplateFile, vector<string> tokens,  Midi_ControlSurface* surface, vector<Widget*> &widgets)
{
    if(tokens.size() < 2)
        return;
    
    string widgetName = tokens[1];

    Widget* widget = new Widget(surface, widgetName);
    
    if(! widget)
        return;
    
    surface->AddWidget(widget);

    vector<vector<string>> tokenLines;
    
    for (string line; getline(surfaceTemplateFile, line) ; )
    {
        line = regex_replace(line, regex(TabChars), " ");
        line = regex_replace(line, regex(CRLFChars), "");

        lineNumber++;
        
        if(line == "" || line[0] == '\r' || line[0] == '/') // ignore comment lines and blank lines
            continue;
        
        vector<string> tokens(GetTokens(line));
        
        if(tokens[0] == "WidgetEnd")    // finito baybay - Widget list complete
            break;
        
        tokenLines.push_back(tokens);
    }
    
    if(tokenLines.size() < 1)
        return;
    
    for(int i = 0; i < tokenLines.size(); i++)
    {
        int size = tokenLines[i].size();
        
        string widgetClass = tokenLines[i][0];

        // Control Signal Generators
        if(widgetClass == "AnyPress" && (size == 4 || size == 7))
            new AnyPress_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        if(widgetClass == "Press" && size == 4)
            new PressRelease_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        else if(widgetClass == "Press" && size == 7)
            new PressRelease_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])), new MIDI_event_ex_t(strToHex(tokenLines[i][4]), strToHex(tokenLines[i][5]), strToHex(tokenLines[i][6])));
        else if(widgetClass == "Fader14Bit" && size == 4)
            new Fader14Bit_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        else if(widgetClass == "Fader7Bit" && size== 4)
            new Fader7Bit_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        else if(widgetClass == "Encoder" && size == 4)
            new Encoder_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        else if(widgetClass == "Encoder" && size > 4)
            new AcceleratedEncoder_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])), tokenLines[i]);
        else if(widgetClass == "EncoderPlain" && size == 4)
            new EncoderPlain_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        else if(widgetClass == "EncoderPlainReverse" && size == 4)
            new EncoderPlainReverse_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        
        // Feedback Processors
        FeedbackProcessor* feedbackProcessor = nullptr;

        if(widgetClass == "FB_TwoState" && (size == 7 || size == 8))
        {
            feedbackProcessor = new TwoState_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])), new MIDI_event_ex_t(strToHex(tokenLines[i][4]), strToHex(tokenLines[i][5]), strToHex(tokenLines[i][6])));
            
            if(size == 8)
                feedbackProcessor->SetRefreshInterval(strToDouble(tokenLines[i][7]));
        }
        else if(widgetClass == "FB_NovationLaunchpadMiniRGB7Bit" && size == 4)
        {
            feedbackProcessor = new NovationLaunchpadMiniRGB7Bit_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(widgetClass == "FB_MFT_RGB" && size == 4)
        {
            feedbackProcessor = new MFT_RGB_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(widgetClass == "FB_FaderportRGB7Bit" && size == 4)
        {
            feedbackProcessor = new FaderportRGB7Bit_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(size == 4 || size== 5)
        {
            if(widgetClass == "FB_Fader14Bit")
                feedbackProcessor = new Fader14Bit_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
            else if(widgetClass == "FB_Fader7Bit")
                feedbackProcessor = new Fader7Bit_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
            else if(widgetClass == "FB_Encoder")
                feedbackProcessor = new Encoder_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
            else if(widgetClass == "FB_VUMeter")
                feedbackProcessor = new VUMeter_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
            else if(widgetClass == "FB_GainReductionMeter")
                feedbackProcessor = new GainReductionMeter_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
            
            if(size == 5 && feedbackProcessor != nullptr)
                feedbackProcessor->SetRefreshInterval(strToDouble(tokenLines[i][4]));
        }
        else if((widgetClass == "FB_MCUTimeDisplay" || widgetClass == "FB_QConProXMasterVUMeter") && size == 1)
        {
            if(widgetClass == "FB_MCUTimeDisplay")
                feedbackProcessor = new MCU_TimeDisplay_Midi_FeedbackProcessor(surface, widget);
            else if(widgetClass == "FB_QConProXMasterVUMeter")
                feedbackProcessor = new QConProXMasterVUMeter_Midi_FeedbackProcessor(surface, widget);
        }
        else if((widgetClass == "FB_MCUVUMeter" || widgetClass == "FB_MCUXTVUMeter") && (size == 2 || size == 3))
        {
            int displayType = widgetClass == "FB_MCUVUMeter" ? 0x14 : 0x15;
            
            feedbackProcessor = new MCUVUMeter_Midi_FeedbackProcessor(surface, widget, displayType, stoi(tokenLines[i][1]));
            
            if(size == 3 && feedbackProcessor != nullptr)
                feedbackProcessor->SetRefreshInterval(strToDouble(tokenLines[i][2]));
        }
        else if((widgetClass == "FB_MCUDisplayUpper" || widgetClass == "FB_MCUDisplayLower" || widgetClass == "FB_MCUXTDisplayUpper" || widgetClass == "FB_MCUXTDisplayLower") && (size == 2 || size == 3))
        {
            if(widgetClass == "FB_MCUDisplayUpper")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(surface, widget, 0, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetClass == "FB_MCUDisplayLower")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(surface, widget, 1, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetClass == "FB_MCUXTDisplayUpper")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(surface, widget, 0, 0x15, 0x12, stoi(tokenLines[i][1]));
            else if(widgetClass == "FB_MCUXTDisplayLower")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(surface, widget, 1, 0x15, 0x12, stoi(tokenLines[i][1]));
            
            if(size == 3 && feedbackProcessor != nullptr)
                feedbackProcessor->SetRefreshInterval(strToDouble(tokenLines[i][2]));
        }
        
        else if((widgetClass == "FB_C4DisplayUpper" || widgetClass == "FB_C4DisplayLower") && (size == 3 || size == 4))
        {
            if(widgetClass == "FB_C4DisplayUpper")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(surface, widget, 0, 0x17, stoi(tokenLines[i][1]) + 0x30, stoi(tokenLines[i][2]));
            else if(widgetClass == "FB_C4DisplayLower")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(surface, widget, 1, 0x17, stoi(tokenLines[i][1]) + 0x30, stoi(tokenLines[i][2]));
            
            if(size == 4 && feedbackProcessor != nullptr)
                feedbackProcessor->SetRefreshInterval(strToDouble(tokenLines[i][3]));
        }
        
        else if((widgetClass == "FB_FP8Display" || widgetClass == "FB_FP16Display") && (size == 2 || size == 3))
        {
            if(widgetClass == "FB_FP8Display")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x02, stoi(tokenLines[i][1]));
            else if(widgetClass == "FB_FP16Display")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x16, stoi(tokenLines[i][1]));
            
            if(size == 3 && feedbackProcessor != nullptr)
                feedbackProcessor->SetRefreshInterval(strToDouble(tokenLines[i][2]));
        }
        
        if(feedbackProcessor != nullptr)
            widget->AddFeedbackProcessor(feedbackProcessor);
    }
}

static void ProcessOSCWidget(int &lineNumber, ifstream &surfaceTemplateFile, vector<string> tokens,  OSC_ControlSurface* surface, vector<Widget*> &widgets)
{
    if(tokens.size() < 2)
        return;
    
    Widget* widget = new Widget(surface, tokens[1]);
    
    if(! widget)
        return;
    
    widgets.push_back(widget);

    vector<vector<string>> tokenLines;

    for (string line; getline(surfaceTemplateFile, line) ; )
    {
        line = regex_replace(line, regex(TabChars), " ");
        line = regex_replace(line, regex(CRLFChars), "");

        lineNumber++;
        
        if(line == "" || line[0] == '\r' || line[0] == '/') // ignore comment lines and blank lines
            continue;
        
        vector<string> tokens(GetTokens(line));
        
        if(tokens[0] == "WidgetEnd")    // finito baybay - Widget list complete
            break;
        
        tokenLines.push_back(tokens);
    }

    for(auto tokenLine : tokenLines)
    {
        if(tokenLine.size() > 1 && tokenLine[0] == "Control")
            new OSC_CSIMessageGenerator(surface, widget, tokenLine[1]);
        else if(tokenLine.size() > 1 && tokenLine[0] == "FB_Processor")
            widget->AddFeedbackProcessor(new OSC_FeedbackProcessor(surface, widget, tokenLine[1]));
    }
}

static void ProcessWidgetFile(string filePath, ControlSurface* surface, vector<Widget*> &widgets)
{
    int lineNumber = 0;
    
    try
    {
        ifstream file(filePath);
        
        for (string line; getline(file, line) ; )
        {
            line = regex_replace(line, regex(TabChars), " ");
            line = regex_replace(line, regex(CRLFChars), "");
            
            lineNumber++;
            
            if(line == "" || line[0] == '\r' || line[0] == '/') // ignore comment lines and blank lines
                continue;
            
            vector<string> tokens(GetTokens(line));
            
            if(tokens.size() > 0 && tokens[0] == "Widget")
            {
                if(filePath[filePath.length() - 3] == 'm')
                    ProcessMidiWidget(lineNumber, file, tokens, (Midi_ControlSurface*)surface, widgets);
                if(filePath[filePath.length() - 3] == 'o')
                    ProcessOSCWidget(lineNumber, file, tokens, (OSC_ControlSurface*)surface, widgets);
            }
        }
    }
    catch (exception &e)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", filePath.c_str(), lineNumber);
        DAW::ShowConsoleMsg(buffer);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Manager
////////////////////////////////////////////////////////////////////////////////////////////////////////
void Manager::InitActionsDictionary()
{    
    actions_["SelectTrackRelative"] =               new SelectTrackRelative();
    actions_["TrackAutoMode"] =                     new TrackAutoMode();
    actions_["TimeDisplay"] =                       new TimeDisplay();
    actions_["EuConTimeDisplay"] =                  new EuConTimeDisplay();
    actions_["NoAction"] =                          new NoAction();
    actions_["Reaper"] =                            new ReaperAction();
    actions_["FixedTextDisplay"] =                  new FixedTextDisplay(); ;
    actions_["FixedRGBColourDisplay"] =             new FixedRGBColourDisplay();
    actions_["Rewind"] =                            new Rewind();
    actions_["FastForward"] =                       new FastForward();
    actions_["Play"] =                              new Play();
    actions_["Stop"] =                              new Stop();
    actions_["Record"] =                            new Record();
    actions_["CycleTimeline"] =                     new CycleTimeline();
    actions_["SetShowFXWindows"] =                  new SetShowFXWindows();
    actions_["ToggleScrollLink"] =                  new ToggleScrollLink();
    actions_["ForceScrollLink"] =                   new ForceScrollLink();
    actions_["ToggleVCAMode"] =                     new ToggleVCAMode();
    actions_["CycleTimeDisplayModes"] =             new CycleTimeDisplayModes();
    actions_["NextPage"] =                          new GoNextPage();
    actions_["GoPage"] =                            new GoPage();
    actions_["GoZone"] =                            new GoZone();
    actions_["TrackBank"] =                         new TrackBank();
    actions_["ClearAllSolo"] =                      new ClearAllSolo();
    actions_["Shift"] =                             new SetShift();
    actions_["Option"] =                            new SetOption();
    actions_["Control"] =                           new SetControl();
    actions_["Alt"] =                               new SetAlt();
    actions_["ToggleMapSelectedTrackSends"] =       new ToggleMapSelectedTrackSends();
    actions_["MapSelectedTrackSendsToWidgets"] =    new MapSelectedTrackSendsToWidgets();
    actions_["ToggleMapSelectedTrackFX"] =          new ToggleMapSelectedTrackFX();
    actions_["MapSelectedTrackFXToWidgets"] =       new MapSelectedTrackFXToWidgets();
    actions_["ToggleMapSelectedTrackFXMenu"] =      new ToggleMapSelectedTrackFXMenu();
    actions_["MapSelectedTrackFXToMenu"] =          new MapSelectedTrackFXToMenu();
    actions_["ToggleMapFocusedFX"] =                new ToggleMapFocusedFX();
    actions_["MapFocusedFXToWidgets"] =             new MapFocusedFXToWidgets();
    actions_["GoFXSlot"] =                          new GoFXSlot();
    //actio["CycleTrackAutoMode"] =                 new CycleTrackAutoMode();
    //actio["EuConCycleTrackAutoMode"] =            new EuConCycleTrackAutoMode();
    actions_["GlobalAutoMode"] =                    new GlobalAutoMode();
    actions_["FocusedFXParam"] =                    new FocusedFXParam();
    actions_["FocusedFXParamNameDisplay"] =         new FocusedFXParamNameDisplay();
    actions_["FocusedFXParamValueDisplay"] =        new FocusedFXParamValueDisplay();

    actions_["TrackVolume"] =                       new TrackVolume();
    actions_["SoftTakeover7BitTrackVolume"] =       new SoftTakeover7BitTrackVolume();
    actions_["SoftTakeover14BitTrackVolume"] =      new SoftTakeover14BitTrackVolume();
    actions_["TrackVolumeDB"] =                     new TrackVolumeDB();
    actions_["TrackToggleVCASpill"] =               new TrackToggleVCASpill();
    actions_["TrackSelect"] =                       new TrackSelect();
    actions_["TrackUniqueSelect"] =                 new TrackUniqueSelect();
    actions_["TrackRangeSelect"] =                  new TrackRangeSelect();
    actions_["TrackRecordArm"] =                    new TrackRecordArm();
    actions_["TrackMute"] =                         new TrackMute();
    actions_["TrackSolo"] =                         new TrackSolo();
    actions_["FaderTouch"] =                        new SetFaderTouch();
    actions_["RotaryTouch"] =                       new SetRotaryTouch();
    actions_["TrackPan"] =                          new TrackPan();
    actions_["TrackPanPercent"] =                   new TrackPanPercent();
    actions_["TrackPanWidth"] =                     new TrackPanWidth();
    actions_["TrackPanWidthPercent"] =              new TrackPanWidthPercent();
    actions_["TrackPanLPercent"] =                  new TrackPanLPercent();
    actions_["TrackPanRPercent"] =                  new TrackPanRPercent();
    actions_["TogglePin"] =                         new TogglePin();
    actions_["TrackNameDisplay"] =                  new TrackNameDisplay();
    actions_["TrackVolumeDisplay"] =                new TrackVolumeDisplay();
    actions_["TrackPanDisplay"] =                   new TrackPanDisplay();
    actions_["TrackPanWidthDisplay"] =              new TrackPanWidthDisplay();
    actions_["TrackOutputMeter"] =                  new TrackOutputMeter();
    actions_["TrackOutputMeterAverageLR"] =         new TrackOutputMeterAverageLR();
    actions_["TrackOutputMeterMaxPeakLR"] =         new TrackOutputMeterMaxPeakLR();
    
    actions_["FXParam"] =                           new FXParam();
    actions_["FXParamRelative"] =                   new FXParamRelative();
    actions_["FXNameDisplay"] =                     new FXNameDisplay();
    actions_["FXParamNameDisplay"] =                new FXParamNameDisplay();
    actions_["FXParamValueDisplay"] =               new FXParamValueDisplay();
    actions_["FXGainReductionMeter"] =              new FXGainReductionMeter();
    actions_["TrackSendVolume"] =                   new TrackSendVolume();
    actions_["TrackSendVolumeDB"] =                 new TrackSendVolumeDB();
    actions_["TrackSendPan"] =                      new TrackSendPan();
    actions_["TrackSendMute"] =                     new TrackSendMute();
    actions_["TrackSendInvertPolarity"] =           new TrackSendInvertPolarity();
    actions_["TrackSendPrePost"] =                  new TrackSendPrePost();
    actions_["TrackSendNameDisplay"] =              new TrackSendNameDisplay();
    actions_["TrackSendVolumeDisplay"] =            new TrackSendVolumeDisplay();
}

void Manager::Init()
{
    pages_.clear();

    Page* currentPage = nullptr;
    
    string iniFilePath = string(DAW::GetResourcePath()) + "/CSI/CSI.ini";
    
    int lineNumber = 0;
    
    try
    {
        ifstream iniFile(iniFilePath);
        
        for (string line; getline(iniFile, line) ; )
        {
            line = regex_replace(line, regex(TabChars), " ");
            line = regex_replace(line, regex(CRLFChars), "");
            
            vector<string> tokens(GetTokens(line));
            
            if(tokens.size() > 4) // ignore comment lines and blank lines
            {
                if(tokens[0] == PageToken)
                {
                    if(tokens.size() != 11)
                        continue;
                    
                    rgb_color pageColour;
                    
                    if(tokens[6] == "{" && tokens[10] == "}")
                    {
                        pageColour.r = atoi(tokens[7].c_str());
                        pageColour.g = atoi(tokens[8].c_str());
                        pageColour.b = atoi(tokens[9].c_str());
                    }

                    currentPage = new Page(tokens[1], pageColour, tokens[2] == "FollowMCP" ? true : false, tokens[3] == "SynchPages" ? true : false);
                    pages_.push_back(currentPage);
                    
                    if(tokens[4] == "UseScrollLink")
                        currentPage->GetTrackNavigationManager()->SetScrollLink(true);
                    else
                        currentPage->GetTrackNavigationManager()->SetScrollLink(false);
                }
                else if(tokens[0] == MidiSurfaceToken || tokens[0] == OSCSurfaceToken || tokens[0] == EuConSurfaceToken)
                {
                    int inPort = 0;
                    int outPort = 0;
                    
                    if(tokens[0] == MidiSurfaceToken || tokens[0] == OSCSurfaceToken)
                    {
                        inPort = atoi(tokens[2].c_str());
                        outPort = atoi(tokens[3].c_str());
                    }
                    
                    if(currentPage)
                    {
                        ControlSurface* surface = nullptr;
                        
                        if(tokens[0] == MidiSurfaceToken && tokens.size() == 10)
                            surface = new Midi_ControlSurface(CSurfIntegrator_, currentPage, tokens[1], tokens[4], tokens[5], atoi(tokens[6].c_str()), atoi(tokens[7].c_str()), atoi(tokens[8].c_str()), atoi(tokens[9].c_str()), GetMidiInputForPort(inPort), GetMidiOutputForPort(outPort));
                        else if(tokens[0] == OSCSurfaceToken && tokens.size() == 11)
                            surface = new OSC_ControlSurface(CSurfIntegrator_, currentPage, tokens[1], tokens[4], tokens[5], atoi(tokens[6].c_str()), atoi(tokens[7].c_str()), atoi(tokens[8].c_str()), atoi(tokens[9].c_str()), GetInputSocketForPort(tokens[1], inPort), GetOutputSocketForAddressAndPort(tokens[1], tokens[10], outPort));
                        else if(tokens[0] == EuConSurfaceToken && tokens.size() == 7)
                            surface = new EuCon_ControlSurface(CSurfIntegrator_, currentPage, tokens[1], tokens[2], atoi(tokens[3].c_str()), atoi(tokens[4].c_str()), atoi(tokens[5].c_str()), atoi(tokens[6].c_str()));

                        currentPage->AddSurface(surface);
                    }
                }
            }
        }
    }
    catch (exception &e)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", iniFilePath.c_str(), lineNumber);
        DAW::ShowConsoleMsg(buffer);
    }
}
//////////////////////////////////////////////////////////////////////////////////////////////
// Parsing end
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////
// TrackNavigator
////////////////////////////////////////////////////////////////////////////////////////////////////////
void TrackNavigator::PinChannel()
{
    if( ! isChannelPinned_)
    {
        pinnedTrack_ = GetTrack();
        
        isChannelPinned_ = true;
        
        manager_->IncChannelBias(pinnedTrack_, channelNum_);
    }
}

void TrackNavigator::UnpinChannel()
{
    if(isChannelPinned_)
    {
        manager_->DecChannelBias(pinnedTrack_, channelNum_);
        
        isChannelPinned_ = false;
        
        pinnedTrack_ = nullptr;
    }
}

MediaTrack* TrackNavigator::GetTrack()
{
    if(isChannelPinned_)
        return pinnedTrack_;
    else
        return manager_->GetTrackFromChannel(channelNum_ - bias_);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// MasterTrackNavigator
////////////////////////////////////////////////////////////////////////////////////////////////////////
MediaTrack* MasterTrackNavigator::GetTrack()
{
    return DAW::GetMasterTrack(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// SelectedTrackNavigator
////////////////////////////////////////////////////////////////////////////////////////////////////////
MediaTrack* SelectedTrackNavigator::GetTrack()
{
    return page_->GetTrackNavigationManager()->GetSelectedTrack();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// SendNavigator
////////////////////////////////////////////////////////////////////////////////////////////////////////
MediaTrack* SendNavigator::GetTrack()
{
    return page_->GetTrackNavigationManager()->GetSelectedTrack();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// FocusedFXNavigator
////////////////////////////////////////////////////////////////////////////////////////////////////////
MediaTrack* FocusedFXNavigator::GetTrack()
{
    int trackNumber = 0;
    int itemNumber = 0;
    int fxIndex = 0;
    
    if(DAW::GetFocusedFX(&trackNumber, &itemNumber, &fxIndex) == 1) // Track FX
        return page_->GetTrackNavigationManager()->GetTrackFromId(trackNumber);
    else
        return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// TrackNavigationManager
////////////////////////////////////////////////////////////////////////////////////////////////////////
void TrackNavigationManager::ForceScrollLink()
{
    // Make sure selected track is visble on the control surface
    MediaTrack* selectedTrack = GetSelectedTrack();
    
    if(selectedTrack != nullptr)
    {
        for(auto navigator : navigators_)
            if(selectedTrack == navigator->GetTrack())
                return;
        
        for(int i = 0; i < tracks_.size(); i++)
            if(selectedTrack == tracks_[i])
                trackOffset_ = i;
        
        trackOffset_ -= targetScrollLinkChannel_;
        
        if(trackOffset_ <  0)
            trackOffset_ =  0;
        
        int top = GetNumTracks() - navigators_.size();
        
        if(trackOffset_ >  top)
            trackOffset_ = top;
    }
}

void TrackNavigationManager::OnTrackSelectionBySurface(MediaTrack* track)
{
    if(scrollLink_)
    {
        if(DAW::IsTrackVisible(track, true))
            DAW::SetMixerScroll(track); // scroll selected MCP tracks into view
        
        if(DAW::IsTrackVisible(track, false))
            DAW::SendCommandMessage(40913); // scroll selected TCP tracks into view
    }
}

void TrackNavigationManager::AdjustTrackBank(int amount)
{
    int numTracks = GetNumTracks();
    
    if(numTracks <= navigators_.size())
        return;
   
    trackOffset_ += amount;
    
    if(trackOffset_ <  0)
        trackOffset_ =  0;
    
    int top = numTracks - navigators_.size();
    
    if(trackOffset_ >  top)
        trackOffset_ = top;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ActionContext
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ActionContext::ActionContext(Action* action, Widget* widget, Zone* zone, vector<string> params): action_(action), widget_(widget), zone_(zone)
{
    string actionName = "";
    
    if(params.size() > 0)
        actionName = params[0];
    
    // Action with int param, could include leading minus sign
    if(params.size() > 1 && (isdigit(params[1][0]) ||  params[1][0] == '-'))  // C++ 11 says empty strings can be queried without catastrophe :)
    {
        intParam_= atol(params[1].c_str());
    }
    
    // Action with param index, must be positive
    if(params.size() > 1 && isdigit(params[1][0]))  // C++ 11 says empty strings can be queried without catastrophe :)
    {
        paramIndex_ = atol(params[1].c_str());
    }
    
    // Action with string param
    if(params.size() > 1)
        stringParam_ = params[1];
    
    if(actionName == "TrackVolumeDB" || actionName == "TrackSendVolumeDB")
    {
        rangeMinimum_ = -144.0;
        rangeMaximum_ = 24.0;
    }
    
    if(actionName == "TrackPanPercent" || actionName == "TrackPanWidthPercent" || actionName == "TrackPanLPercent" || actionName == "TrackPanRPercent")
    {
        rangeMinimum_ = -100.0;
        rangeMaximum_ = 100.0;
    }
   
    if(actionName == "Reaper" && params.size() > 1)
    {
        if (isdigit(params[1][0]))
        {
            commandId_ =  atol(params[1].c_str());
        }
        else // look up by string
        {
            commandId_ = DAW::NamedCommandLookup(params[1].c_str());
            
            if(commandId_ == 0) // can't find it
                commandId_ = 65535; // no-op
        }
    }
    
    if(actionName == "FXParam" && params.size() > 1 && isdigit(params[1][0]))  // C++ 11 says empty strings can be queried without catastrophe :)
    {
        paramIndex_ = atol(params[1].c_str());
        
        if(params.size() > 2 && isalpha(params[2][0]))  // C++ 11 says empty strings can be queried without catastrophe :)
            fxParamDisplayName_ = params[2];
        
        if(params.size() > 3 && params[3] != "[" && params[3] != "{")
        {
            shouldUseDisplayStyle_ = true;
            displayStyle_ = atol(params[3].c_str());
        }
    }
    
    /*
     //////////////////////////////////////////////////
     // CycleTrackAutoMode and EuConCycleTrackAutoMode
     
     if(params.size() > 1 && params[1].size() > 0 && isalpha(params[1].at(0)))
     {
     for(auto widget : GetSurface()->GetWidgets())
     {
     if(widget->GetName() == params[1])
     {
     displayWidget_ = widget;
     break;
     }
     }
     }
     
     // CycleTrackAutoMode and EuConCycleTrackAutoMode
     //////////////////////////////////////////////////
     */

    
    
    
    
    
    
    if(params.size() > 0)
    {
        SetRGB(params, supportsRGB_, supportsTrackColor_, RGBValues_);
        SetSteppedValues(params, deltaValue_, acceleratedDeltaValues_, rangeMinimum_, rangeMaximum_, steppedValues_, acceleratedTickValues_);
    }
    
    if(acceleratedTickValues_.size() < 1)
        acceleratedTickValues_.push_back(10);

}

Page* ActionContext::GetPage()
{
    return widget_->GetSurface()->GetPage();
}

ControlSurface* ActionContext::GetSurface()
{
    return widget_->GetSurface();
}

TrackNavigationManager* ActionContext::GetTrackNavigationManager()
{
    return GetPage()->GetTrackNavigationManager();
}

MediaTrack* ActionContext::GetTrack()
{
    return zone_->GetNavigator()->GetTrack();
}

int ActionContext::GetSlotIndex()
{
    return zone_->GetSlotIndex();
}

int ActionContext::GetSendIndex()
{
    return zone_->GetNavigator()->GetSendNum();
}

void ActionContext::RequestUpdate()
{   
    action_->RequestUpdate(this);
}

void ActionContext::ClearWidget()
{
    widget_->Clear();
}

void ActionContext::UpdateWidgetValue(double value)
{
    value = isInverted_ == false ? value : 1.0 - value;
    
    SetSteppedValueIndex(value);
    
    lastValue_ = value;
    
    widget_->UpdateValue(value);

    if(supportsRGB_)
    {
        currentRGBIndex_ = value == 0 ? 0 : 1;
        widget_->UpdateRGBValue(RGBValues_[currentRGBIndex_].r, RGBValues_[currentRGBIndex_].g, RGBValues_[currentRGBIndex_].b);
    }
    else if(supportsTrackColor_)
    {
        if(MediaTrack* track = zone_->GetNavigator()->GetTrack())
        {
            unsigned int* rgb_colour = (unsigned int*)DAW::GetSetMediaTrackInfo(track, "I_CUSTOMCOLOR", NULL);
            
            int r = (*rgb_colour >> 0) & 0xff;
            int g = (*rgb_colour >> 8) & 0xff;
            int b = (*rgb_colour >> 16) & 0xff;
            
            widget_->UpdateRGBValue(r, g, b);
        }
    }
}

void ActionContext::UpdateWidgetValue(int param, double value)
{
    value = isInverted_ == false ? value : 1.0 - value;
    
    SetSteppedValueIndex(value);
    
    lastValue_ = value;
    
    widget_->UpdateValue(param, value);
    
    currentRGBIndex_ = value == 0 ? 0 : 1;
    
    if(supportsRGB_)
    {
        currentRGBIndex_ = value == 0 ? 0 : 1;
        widget_->UpdateRGBValue(RGBValues_[currentRGBIndex_].r, RGBValues_[currentRGBIndex_].g, RGBValues_[currentRGBIndex_].b);
    }
    else if(supportsTrackColor_)
    {
        if(MediaTrack* track = zone_->GetNavigator()->GetTrack())
        {
            unsigned int* rgb_colour = (unsigned int*)DAW::GetSetMediaTrackInfo(track, "I_CUSTOMCOLOR", NULL);
            
            int r = (*rgb_colour >> 0) & 0xff;
            int g = (*rgb_colour >> 8) & 0xff;
            int b = (*rgb_colour >> 16) & 0xff;
            
            widget_->UpdateRGBValue(r, g, b);
        }
    }
}

void ActionContext::UpdateWidgetValue(string value)
{
    widget_->UpdateValue(value);
    
    if(supportsTrackColor_)
    {
        if(MediaTrack* track = zone_->GetNavigator()->GetTrack())
        {
            unsigned int* rgb_colour = (unsigned int*)DAW::GetSetMediaTrackInfo(track, "I_CUSTOMCOLOR", NULL);
            
            int r = (*rgb_colour >> 0) & 0xff;
            int g = (*rgb_colour >> 8) & 0xff;
            int b = (*rgb_colour >> 16) & 0xff;
            
            widget_->UpdateRGBValue(r, g, b);
        }
    }
}

void ActionContext::DoAction(double value)
{
    if(steppedValues_.size() > 0 && value != 0.0)
    {
        if(steppedValuesIndex_ == steppedValues_.size() - 1)
        {
            if(steppedValues_[0] < steppedValues_[steppedValuesIndex_]) // GAW -- only wrap if 1st value is lower
                steppedValuesIndex_ = 0;
        }
        else
            steppedValuesIndex_++;
        
        DoRangeBoundAction(steppedValues_[steppedValuesIndex_]);
    }
    else
        DoRangeBoundAction(value);
}

void ActionContext::DoRelativeAction(double delta)
{
    if(steppedValues_.size() > 0)
        DoAcceleratedSteppedValueAction(0, delta);
    else
    {
        if(deltaValue_ != 0.0)
        {
            if(delta > 0.0)
                delta = deltaValue_;
            else if(delta < 0.0)
                delta = -deltaValue_;
        }
        
        DoRangeBoundAction(lastValue_ + delta);
    }
}

void ActionContext::DoRelativeAction(int accelerationIndex, double delta)
{
    if(steppedValues_.size() > 0)
        DoAcceleratedSteppedValueAction(accelerationIndex, delta);
    else if(acceleratedDeltaValues_.size() > 0)
        DoAcceleratedDeltaValueAction(accelerationIndex, delta);
    else
    {
        if(deltaValue_ != 0.0)
        {
            if(delta >= 0.0)
                delta = deltaValue_;
            else if(delta < 0.0)
                delta = -deltaValue_;
        }
        
        DoRangeBoundAction(lastValue_ + delta);
    }
}

void ActionContext::DoRangeBoundAction(double value)
{
    if(delayAmount_ != 0.0)
    {
        if(value == 0.0)
        {
            delayStartTime_ = 0.0;
            deferredValue_ = 0.0;
        }
        else
        {
            delayStartTime_ = DAW::GetCurrentNumberOfMilliseconds();
            deferredValue_ = value;
        }
    }
    else
    {
        if(shouldToggle_ && value != 0.0)
            value = ! GetCurrentValue();
        
        if(value > rangeMaximum_)
            value = rangeMaximum_;
        
        if(value < rangeMinimum_)
            value = rangeMinimum_;
        
        action_->Do(this, value);
    }
}

void ActionContext::DoAcceleratedSteppedValueAction(int accelerationIndex, double delta)
{
    if(delta > 0)
    {
        accumulatedIncTicks_++;
        accumulatedDecTicks_ = accumulatedDecTicks_ - 1 < 0 ? 0 : accumulatedDecTicks_ - 1;
    }
    else if(delta < 0)
    {
        accumulatedDecTicks_++;
        accumulatedIncTicks_ = accumulatedIncTicks_ - 1 < 0 ? 0 : accumulatedIncTicks_ - 1;
    }
    
    accelerationIndex = accelerationIndex > acceleratedTickValues_.size() - 1 ? acceleratedTickValues_.size() - 1 : accelerationIndex;
    accelerationIndex = accelerationIndex < 0 ? 0 : accelerationIndex;
    
    if(delta > 0 && accumulatedIncTicks_ >= acceleratedTickValues_[accelerationIndex])
    {
        accumulatedIncTicks_ = 0;
        accumulatedDecTicks_ = 0;
        
        steppedValuesIndex_++;
        
        if(steppedValuesIndex_ > steppedValues_.size() - 1)
            steppedValuesIndex_ = steppedValues_.size() - 1;
        
        DoRangeBoundAction(steppedValues_[steppedValuesIndex_]);
    }
    else if(delta < 0 && accumulatedDecTicks_ >= acceleratedTickValues_[accelerationIndex])
    {
        accumulatedIncTicks_ = 0;
        accumulatedDecTicks_ = 0;
        
        steppedValuesIndex_--;
        
        if(steppedValuesIndex_ < 0 )
            steppedValuesIndex_ = 0;
        
        DoRangeBoundAction(steppedValues_[steppedValuesIndex_]);
    }
}

void ActionContext::DoAcceleratedDeltaValueAction(int accelerationIndex, double delta)
{
    accelerationIndex = accelerationIndex > acceleratedDeltaValues_.size() - 1 ? acceleratedDeltaValues_.size() - 1 : accelerationIndex;
    accelerationIndex = accelerationIndex < 0 ? 0 : accelerationIndex;
    
    if(delta > 0.0)
        DoRangeBoundAction(lastValue_ + acceleratedDeltaValues_[accelerationIndex]);
    else
        DoRangeBoundAction(lastValue_ - acceleratedDeltaValues_[accelerationIndex]);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// WidgetActionBroker
////////////////////////////////////////////////////////////////////////////////////////////////////////
WidgetActionBroker::WidgetActionBroker(Widget* widget) : widget_(widget), zone_(widget->GetSurface()->GetDefaultZone())
{
    vector<string> memberParams;
   
    defaultBundle_.AddActionContext(TheManager->GetActionContext("NoAction", widget, zone_, memberParams));
}

ActionBundle &WidgetActionBroker::GetActionBundle()
{
    string modifier = "";
    
    if( ! widget_->GetIsModifier())
        modifier = widget_->GetSurface()->GetPage()->GetModifier();
    
    string touchModifier = modifier;
    
    Navigator* navigator = zone_->GetNavigator();
    
    if(navigator != nullptr)
    {
        if(navigator->GetIsFaderTouched())
            touchModifier += "FaderTouch+";
        
        if(navigator->GetIsRotaryTouched())
            touchModifier += "RotaryTouch+";
    }
    
    if(actionBundles_.count(touchModifier) > 0)
        return actionBundles_[touchModifier];
    else if(actionBundles_.count(modifier) > 0)
        return actionBundles_[modifier];
    else if(actionBundles_.count("") > 0)
        return actionBundles_[""];
    else
        return defaultBundle_;
}

void WidgetActionBroker::GetFormattedFXParamValue(char *buffer, int bufferSize)
{
    if(zone_->GetNavigator()->GetTrack() != nullptr)
        GetActionBundle().GetFormattedFXParamValue(zone_->GetNavigator()->GetTrack(), zone_->GetSlotIndex(), buffer, bufferSize);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Zone
////////////////////////////////////////////////////////////////////////////////////////////////////////
void Zone::Deactivate()
{
    if(hwnd_ != nullptr && IsWindow(hwnd_))
        DestroyWindow(hwnd_);
        
    for(auto widget : widgets_)
        widget->Deactivate();
    
    widgets_.clear();
    
    // GAW TBD Leaving Zone - if needed
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ZoneTemplate
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ZoneTemplate::ProcessWidgetActionTemplates(ControlSurface* surface, Zone* zone, string channelNumStr, bool shouldUseNoAction)
{
    for(auto  widgetActionTemplate :  widgetActionTemplates)
    {
        string widgetName = regex_replace(widgetActionTemplate->widgetName, regex("[|]"), channelNumStr);
        
        if(Widget* widget = surface->GetWidgetByName(widgetName))
        {
            if(widgetActionTemplate->isModifier)
                widget->SetIsModifier();
            
            WidgetActionBroker broker = WidgetActionBroker(widget, zone);
            
            for(auto actionsForModifierTemplate : widgetActionTemplate->actionBundleTemplates)
            {
                ActionBundle actionBundle = ActionBundle(actionsForModifierTemplate->modifier);
                
                for(auto member : actionsForModifierTemplate->members)
                {
                    string actionName = regex_replace(member->actionName, regex("[|]"), channelNumStr);
                    vector<string> memberParams;
                    for(int i = 0; i < member->params.size(); i++)
                        memberParams.push_back(regex_replace(member->params[i], regex("[|]"), channelNumStr));
                    
                    if(shouldUseNoAction)
                    {
                        ActionContext context = TheManager->GetActionContext("NoAction", widget, zone, memberParams);
                        member->SetProperties(context);
                        actionBundle.AddActionContext(context);
                    }
                    else
                    {
                        ActionContext context = TheManager->GetActionContext(actionName, widget, zone, memberParams);
                        member->SetProperties(context);
                        actionBundle.AddActionContext(context);
                    }
                }
                
                broker.AddActionBundle(actionBundle);
            }
            
            zone->AddWidget(widget);
            widget->Activate(broker);
        }
    }
}

void ZoneTemplate::Activate(ControlSurface* surface, vector<Zone*> &activeZones)
{
    for(auto includedZoneTemplateStr : includedZoneTemplates)
        if(ZoneTemplate* includedZoneTemplate = surface->GetZoneTemplate(includedZoneTemplateStr))
            includedZoneTemplate->Activate(surface, activeZones);

    for(int i = 0; i < navigators.size(); i++)
    {
        string newZoneName = name;
        
        string channelNumStr = to_string(i + 1);

        if(navigators.size() > 1)
            newZoneName += channelNumStr;
        
        surface->LoadingZone(newZoneName);
        
        Zone* zone = new Zone(surface, navigators[i], newZoneName, alias, sourceFilePath);

        ProcessWidgetActionTemplates(surface, zone, channelNumStr, false);

        activeZones.push_back(zone);
    }
}

void ZoneTemplate::Activate(ControlSurface*  surface, vector<Zone*> &activeZones, int slotIndex, bool shouldShowFXWindows, bool shouldUseNoAction)
{
    for(auto includedZoneTemplateStr : includedZoneTemplates)
        if(ZoneTemplate* includedZoneTemplate = surface->GetZoneTemplate(includedZoneTemplateStr))
            includedZoneTemplate->Activate(surface, activeZones, slotIndex, shouldShowFXWindows, shouldUseNoAction);
    
    if(navigators.size() == 1)
    {
        surface->LoadingZone(name);
        
        Zone* zone = new Zone(surface, navigators[0], name, alias, sourceFilePath);
        
        zone->SetSlotIndex(slotIndex);
        
        ProcessWidgetActionTemplates(surface, zone, "", shouldUseNoAction);
        
        if(shouldShowFXWindows)
            if(MediaTrack* track = navigators[0]->GetTrack())
                zone->OpenFXWindow();

        activeZones.push_back(zone);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ActionTemplate
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ActionTemplate::SetProperties(ActionContext &context)
{
    if(supportsRelease)
        context.SetSupportsRelease();
    
    if(isInverted)
        context.SetIsInverted();
    
    if(shouldToggle)
        context.SetShouldToggle();
    
    if(delayAmount != 0.0)
        context.SetDelayAmount(delayAmount * 1000.0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Widget
////////////////////////////////////////////////////////////////////////////////////////////////////////
void Widget::GetFormattedFXParamValue(char *buffer, int bufferSize)
{
    currentWidgetActionBroker_.GetFormattedFXParamValue(buffer, bufferSize);
}

void Widget::Deactivate()
{
    currentWidgetActionBroker_ = defaultWidgetActionBroker_;
}

void Widget::RequestUpdate()
{
    currentWidgetActionBroker_.GetActionBundle().RequestUpdate();
}

void Widget::DoAction(double value)
{
    LogInput(value);
    
    currentWidgetActionBroker_.GetActionBundle().DoAction(value);
}

void Widget::DoRelativeAction(double delta)
{
    LogInput(delta);

    currentWidgetActionBroker_.GetActionBundle().DoRelativeAction(delta);
}

void Widget::DoRelativeAction(int accelerationIndex, double delta)
{
    LogInput(accelerationIndex);

    currentWidgetActionBroker_.GetActionBundle().DoRelativeAction(accelerationIndex, delta);
}

void Widget::SilentSetValue(string displayText)
{
    for(auto processor : feedbackProcessors_)
        processor->SilentSetValue(displayText);
}

void  Widget::UpdateValue(double value)
{
    for(auto processor : feedbackProcessors_)
        processor->UpdateValue(value);
}

void  Widget::UpdateValue(int mode, double value)
{
    for(auto processor : feedbackProcessors_)
        processor->UpdateValue(mode, value);
}

void  Widget::UpdateValue(string value)
{
    for(auto processor : feedbackProcessors_)
        processor->UpdateValue(value);
}

void  Widget::UpdateRGBValue(int r, int g, int b)
{
    for(auto processor : feedbackProcessors_)
        processor->UpdateRGBValue(r, g, b);
}

void  Widget::Clear()
{
    for(auto processor : feedbackProcessors_)
        processor->Clear();
}

void  Widget::ForceClear()
{
    for(auto processor : feedbackProcessors_)
        processor->ForceClear();
}

void Widget::ClearCache()
{
    for(auto processor : feedbackProcessors_)
        processor->ClearCache();
}

void Widget::LogInput(double value)
{
    if( TheManager->GetSurfaceInDisplay())
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "IN <- %s %s %f\n", GetSurface()->GetName().c_str(), GetName().c_str(), value);
        DAW::ShowConsoleMsg(buffer);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// OSC_CSIMessageGenerator : public CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
OSC_CSIMessageGenerator::OSC_CSIMessageGenerator(OSC_ControlSurface* surface, Widget* widget, string message) : CSIMessageGenerator(widget)
{
    surface->AddCSIMessageGenerator(message, this);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// EuCon_CSIMessageGenerator : public CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
EuCon_CSIMessageGenerator::EuCon_CSIMessageGenerator(EuCon_ControlSurface* surface, Widget* widget, string message) : CSIMessageGenerator(widget)
{
    surface->AddCSIMessageGenerator(message, this);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Midi_FeedbackProcessor
////////////////////////////////////////////////////////////////////////////////////////////////////////
void Midi_FeedbackProcessor::SendMidiMessage(MIDI_event_ex_t* midiMessage)
{
    surface_->SendMidiMessage(this, midiMessage);
}

void Midi_FeedbackProcessor::SendMidiMessage(int first, int second, int third)
{
    if(mustForce_ || first != lastMessageSent_->midi_message[0] || second != lastMessageSent_->midi_message[1] || third != lastMessageSent_->midi_message[2])
    {
        ForceMidiMessage(first, second, third);
    }
    else if(shouldRefresh_ && DAW::GetCurrentNumberOfMilliseconds() > lastRefreshed_ + refreshInterval_)
    {
        lastRefreshed_ = DAW::GetCurrentNumberOfMilliseconds();
        ForceMidiMessage(first, second, third);
    }
}

void Midi_FeedbackProcessor::ForceMidiMessage(int first, int second, int third)
{
    lastMessageSent_->midi_message[0] = first;
    lastMessageSent_->midi_message[1] = second;
    lastMessageSent_->midi_message[2] = third;
    surface_->SendMidiMessage(this, first, second, third);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// OSC_FeedbackProcessor
////////////////////////////////////////////////////////////////////////////////////////////////////////
void OSC_FeedbackProcessor::UpdateValue(double value)
{
    if(lastDoubleValue_ != value)
        ForceValue(value);
}

void OSC_FeedbackProcessor::UpdateValue(int param, double value)
{
    if(lastDoubleValue_ != value)
        ForceValue(value);
}

void OSC_FeedbackProcessor::UpdateValue(string value)
{
    if(lastStringValue_ != value)
        ForceValue(value);
}

void OSC_FeedbackProcessor::ForceValue(double value)
{
    lastDoubleValue_ = value;
    surface_->SendOSCMessage(this, oscAddress_, value);
}

void OSC_FeedbackProcessor::ForceValue(int param, double value)
{
    lastDoubleValue_ = value;
    surface_->SendOSCMessage(this, oscAddress_, value);
}

void OSC_FeedbackProcessor::ForceValue(string value)
{
    lastStringValue_ = value;
    surface_->SendOSCMessage(this, oscAddress_, value);
}

void OSC_FeedbackProcessor::SilentSetValue(string value)
{
    surface_->SendOSCMessage(this, oscAddress_, value);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// EuCon_FeedbackProcessor
////////////////////////////////////////////////////////////////////////////////////////////////////////
void EuCon_FeedbackProcessor::UpdateValue(double value)
{
    if(lastDoubleValue_ != value)
        ForceValue(value);
}

void EuCon_FeedbackProcessor::UpdateValue(int param, double value)
{
    if(lastDoubleValue_ != value)
        ForceValue(param, value);
}

void EuCon_FeedbackProcessor::UpdateValue(string value)
{
    if(lastStringValue_ != value)
        ForceValue(value);
}

void EuCon_FeedbackProcessor::ForceValue(double value)
{
    lastDoubleValue_ = value;
    surface_->SendEuConMessage(this, address_, value);
}

void EuCon_FeedbackProcessor::ForceValue(int param, double value)
{
    lastDoubleValue_ = value;
    surface_->SendEuConMessage(this, address_, value, param);
}

void EuCon_FeedbackProcessor::ForceValue(string value)
{
    lastStringValue_ = value;
    surface_->SendEuConMessage(this, address_, value);
}

void EuCon_FeedbackProcessor::SilentSetValue(string value)
{
    surface_->SendEuConMessage(this, address_, value);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// EuCon_FeedbackProcessorDB
////////////////////////////////////////////////////////////////////////////////////////////////////////
void EuCon_FeedbackProcessorDB::Clear()
{
    if(lastDoubleValue_ != -100.0)
        ForceClear();
}

void EuCon_FeedbackProcessorDB::ForceClear()
{
    lastDoubleValue_ = -100.0;
    surface_->SendEuConMessage(this, address_, -100.0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// FXActivationManager
////////////////////////////////////////////////////////////////////////////////////////////////////////
void FXActivationManager::ToggleMapSelectedTrackFX()
{
    shouldMapSelectedTrackFX_ = ! shouldMapSelectedTrackFX_;
    
    if( ! shouldMapSelectedTrackFX_)
    {
        for(auto selectedZone : activeSelectedTrackFXZones_)
        {
            surface_->LoadingZone(selectedZone->GetName());
            selectedZone->Deactivate();
        }
        
        for(auto selectedZone : activeSelectedTrackFXZones_)
            selectedZone->CloseFXWindow();
        
        activeSelectedTrackFXZones_.clear();
    }
    else
        surface_->GetPage()->OnTrackSelection();
}

void FXActivationManager::ToggleMapFocusedFX()
{
    shouldMapFocusedFX_ = ! shouldMapFocusedFX_;
    
    MapFocusedFXToWidgets();
}

void FXActivationManager::ToggleMapSelectedTrackFXMenu()
{
    shouldMapSelectedTrackFXMenus_ = ! shouldMapSelectedTrackFXMenus_;
    
    if( ! shouldMapSelectedTrackFXMenus_)
    {
        for(auto zone : activeSelectedTrackFXMenuZones_)
        {
            //surface_->LoadingZone(zone->GetName());
            surface_->Deactivate(zone);
        }

        activeSelectedTrackFXMenuZones_.clear();
    }

    surface_->GetPage()->OnTrackSelection();
}

void FXActivationManager::MapSelectedTrackFXToMenu()
{
    for(auto zone : activeSelectedTrackFXMenuZones_)
        surface_->Deactivate(zone);

    activeSelectedTrackFXMenuZones_.clear();
    
    for(auto zone : activeSelectedTrackFXMenuFXZones_)
        surface_->Deactivate(zone);

    activeSelectedTrackFXMenuFXZones_.clear();
    
    MediaTrack* selectedTrack = surface_->GetPage()->GetTrackNavigationManager()->GetSelectedTrack();
    
    if(selectedTrack == nullptr)
        return;
   
    int numTrackFX = DAW::TrackFX_GetCount(selectedTrack);
    
    for(int i = 0; i < numFXSlots_; i ++)
    {
        string zoneName = "FXMenu" + to_string(i + 1);
        
        if(shouldMapSelectedTrackFXMenus_)
        {
            if(ZoneTemplate* zoneTemplate = surface_->GetZoneTemplate(zoneName))
            {
                 if(i < numTrackFX)
                     zoneTemplate->Activate(surface_, activeSelectedTrackFXZones_, i, false, false);
                else
                    zoneTemplate->Activate(surface_, activeSelectedTrackFXZones_, i, false, true);
            }

        }
    }
}

void FXActivationManager::MapSelectedTrackFXToWidgets()
{
    if(shouldMapSelectedTrackFX_)
    {
       for(auto zone : activeSelectedTrackFXZones_)
           surface_->Deactivate(zone);

        activeSelectedTrackFXZones_.clear();
        
        if(MediaTrack* selectedTrack = surface_->GetPage()->GetTrackNavigationManager()->GetSelectedTrack())
            for(int i = 0; i < DAW::TrackFX_GetCount(selectedTrack); i++)
                MapSelectedTrackFXSlotToWidgets(selectedTrack, i);
    }
}

void FXActivationManager::MapSelectedTrackFXSlotToWidgets(MediaTrack* selectedTrack, int fxSlot)
{
    char FXName[BUFSZ];
    
    DAW::TrackFX_GetFXName(selectedTrack, fxSlot, FXName, sizeof(FXName));
    
    if(ZoneTemplate* zoneTemplate = surface_->GetZoneTemplate(FXName))
    {
        if(zoneTemplate->navigators.size() == 1 && ! zoneTemplate->navigators[0]->GetIsFocusedFXNavigator())
            zoneTemplate->Activate(surface_, activeSelectedTrackFXZones_, fxSlot, shouldShowFXWindows_, false);
    }
}

void FXActivationManager::MapFocusedFXToWidgets()
{
    int trackNumber = 0;
    int itemNumber = 0;
    int fxIndex = 0;
    MediaTrack* focusedTrack = nullptr;
    
    if(DAW::GetFocusedFX(&trackNumber, &itemNumber, &fxIndex) == 1)
        if(trackNumber > 0)
            focusedTrack = surface_->GetPage()->GetTrackNavigationManager()->GetTrackFromId(trackNumber);
    
    for(auto zone : activeFocusedFXZones_)
    {
        surface_->LoadingZone("Home");
        surface_->Deactivate(zone);
    }
    
    activeFocusedFXZones_.clear();
    
    if(shouldMapFocusedFX_ && focusedTrack)
    {
        char FXName[BUFSZ];
        DAW::TrackFX_GetFXName(focusedTrack, fxIndex, FXName, sizeof(FXName));
        /*
        if(surface_->GetZones().count(FXName) > 0 && surface_->GetZones()[FXName]->GetHasFocusedFXTrackNavigator())
        {
            ZoneOld* zone = surface_->GetZones()[FXName];
            zone->SetIndex(fxIndex);
            
            surface_->LoadingZone(FXName);
            zone->Activate();
            activeFocusedFXZones_.push_back(zone);
        }
        */
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// ControlSurface
////////////////////////////////////////////////////////////////////////////////////////////////////////
ControlSurface::ControlSurface(CSurfIntegrator* CSurfIntegrator, Page* page, const string name, string zoneFolder, int numChannels, int numSends, int numFX, int options) :  CSurfIntegrator_(CSurfIntegrator), page_(page), name_(name), zoneFolder_(zoneFolder), fxActivationManager_(new FXActivationManager(this, numFX)), numChannels_(numChannels), numSends_(numSends), options_(options), defaultZone_(new Zone(this, GetPage()->GetDefaultNavigator(), "Default", "Default", ""))
{
    for(int i = 0; i < numChannels; i++)
        navigators_[i] = GetPage()->GetTrackNavigationManager()->AddNavigator();
}

void ControlSurface::InitZones(string zoneFolder)
{
    try
    {
        vector<string> zoneFilesToProcess;
        listZoneFiles(DAW::GetResourcePath() + string("/CSI/Zones/") + zoneFolder + "/", zoneFilesToProcess); // recursively find all the .zon files, starting at zoneFolder
        
        for(auto zoneFilename : zoneFilesToProcess)
            ProcessZoneFile(zoneFilename, this);
    }
    catch (exception &e)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble parsing Zone folders\n");
        DAW::ShowConsoleMsg(buffer);
    }
}

Navigator* ControlSurface::GetNavigatorForChannel(int channelNum)
{
    if(channelNum < 0)
        return nullptr;
    
    if(navigators_.count(channelNum) > 0)
        return navigators_[channelNum];
    else
        return nullptr;
}

void ControlSurface::ToggleMapSends()
{
    shouldMapSends_ = ! shouldMapSends_;
    
    if( ! shouldMapSends_)
    {
        for(auto zone : activeSendZones_)
            Deactivate(zone);

        activeSendZones_.clear();
    }
    
    GetPage()->OnTrackSelection();
}

void ControlSurface::MapSelectedTrackSendsToWidgets()
{
    for(auto zone : activeSendZones_)
        Deactivate(zone);

    activeSendZones_.clear();
    
    MediaTrack* selectedTrack = GetPage()->GetTrackNavigationManager()->GetSelectedTrack();
    
    if(selectedTrack == nullptr)
        return;
    
    int numTrackSends = DAW::GetTrackNumSends(selectedTrack, 0);
    
    for(int i = 0; i < numSends_; i++)
    {
        string zoneName = "Send" + to_string(i + 1);
        
        if(shouldMapSends_)
        {
            /*
            if(ZoneTemplate* zoneTemplate = GetZoneTemplate(zoneName))
            {
                if(i < numTrackSends && zoneTemplate->navigators.size() == 1)
                    zoneTemplate->Activate(this, activeSendZones_, i, false);

                
            }
*/
            
            
            
            /*
             Zone* zone =  zones[zoneName];
             zone->SetIndex(i);
             
             if(i < numTrackSends)
             {
             zone->Activate();
             activeSendZones_.push_back(zone);
             }
             else
             {
             surface_->ActivateNoActionForZone(zone->GetName());
             activeSendZones_.push_back(zone);
             }
             */
        }
    }
}

void ControlSurface::SurfaceOutMonitor(Widget* widget, string address, string value)
{
    if(TheManager->GetSurfaceOutDisplay())
        DAW::ShowConsoleMsg(("OUT->" + name_ + " " + address + " " + value + "\n").c_str());
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Midi_ControlSurface
////////////////////////////////////////////////////////////////////////////////////////////////////////
void Midi_ControlSurface::InitWidgets(string templateFilename, string zoneFolder)
{
    ProcessWidgetFile(string(DAW::GetResourcePath()) + "/CSI/Surfaces/Midi/" + templateFilename, this, widgets_);
    InitHardwiredWidgets();
    InitZones(zoneFolder);
    MakeHomeDefault();
    ForceClearAllWidgets();
    GetPage()->ForceRefreshTimeDisplay();
}

void Midi_ControlSurface::ProcessMidiMessage(const MIDI_event_ex_t* evt)
{
    bool isMapped = false;
    
    // At this point we don't know how much of the message comprises the key, so try all three
    if(CSIMessageGeneratorsByMidiMessage_.count(evt->midi_message[0] * 0x10000 + evt->midi_message[1] * 0x100 + evt->midi_message[2]) > 0)
    {
        isMapped = true;
        for( auto generator : CSIMessageGeneratorsByMidiMessage_[evt->midi_message[0] * 0x10000 + evt->midi_message[1] * 0x100 + evt->midi_message[2]])
            generator->ProcessMidiMessage(evt);
    }
    else if(CSIMessageGeneratorsByMidiMessage_.count(evt->midi_message[0] * 0x10000 + evt->midi_message[1] * 0x100) > 0)
    {
        isMapped = true;
        for( auto generator : CSIMessageGeneratorsByMidiMessage_[evt->midi_message[0] * 0x10000 + evt->midi_message[1] * 0x100])
            generator->ProcessMidiMessage(evt);
    }
    else if(CSIMessageGeneratorsByMidiMessage_.count(evt->midi_message[0] * 0x10000) > 0)
    {
        isMapped = true;
        for( auto generator : CSIMessageGeneratorsByMidiMessage_[evt->midi_message[0] * 0x10000])
            generator->ProcessMidiMessage(evt);
        
    }
    
    if( ! isMapped && TheManager->GetSurfaceInDisplay())
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "IN <- %s %02x  %02x  %02x \n", name_.c_str(), evt->midi_message[0], evt->midi_message[1], evt->midi_message[2]);
        DAW::ShowConsoleMsg(buffer);
        
    }
}

void Midi_ControlSurface::SendMidiMessage(Midi_FeedbackProcessor* feedbackProcessor, MIDI_event_ex_t* midiMessage)
{
    if(midiOutput_)
        midiOutput_->SendMsg(midiMessage, -1);
    
    if(TheManager->GetSurfaceOutDisplay())
        DAW::ShowConsoleMsg(("OUT->" + name_ + " SysEx\n").c_str());
}

void Midi_ControlSurface::SendMidiMessage(Midi_FeedbackProcessor* feedbackProcessor, int first, int second, int third)
{
    if(midiOutput_)
        midiOutput_->Send(first, second, third, -1);
    
    if(TheManager->GetSurfaceOutDisplay())
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "%s  %02x  %02x  %02x \n", ("OUT->" + name_).c_str(), first, second, third);
        DAW::ShowConsoleMsg(buffer);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// OSC_ControlSurface
////////////////////////////////////////////////////////////////////////////////////////////////////////
void OSC_ControlSurface::InitWidgets(string templateFilename, string zoneFolder)
{
    ProcessWidgetFile(string(DAW::GetResourcePath()) + "/CSI/Surfaces/OSC/" + templateFilename, this, widgets_);
    
    InitHardwiredWidgets();
    InitZones(zoneFolder);
    MakeHomeDefault();
    ForceClearAllWidgets();
    GetPage()->ForceRefreshTimeDisplay();
}

void OSC_ControlSurface::ProcessOSCMessage(string message, double value)
{
    if(CSIMessageGeneratorsByOSCMessage_.count(message) > 0)
        CSIMessageGeneratorsByOSCMessage_[message]->ProcessOSCMessage(message, value);
    
    if(TheManager->GetSurfaceInDisplay())
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "IN <- %s %s  %f  \n", name_.c_str(), message.c_str(), value);
        DAW::ShowConsoleMsg(buffer);
    }
}

void OSC_ControlSurface::LoadingZone(string zoneName)
{
    string oscAddress(zoneName);
    oscAddress = regex_replace(oscAddress, regex(BadFileChars), "_");
    oscAddress = "/" + oscAddress;

    if(outSocket_ != nullptr && outSocket_->isOk())
    {
        oscpkt::Message message;
        message.init(oscAddress);
        packetWriter_.init().addMessage(message);
        outSocket_->sendPacket(packetWriter_.packetData(), packetWriter_.packetSize());
    }
    
    if(TheManager->GetSurfaceOutDisplay())
        DAW::ShowConsoleMsg((zoneName + "->" + "LoadingZone---->" + name_ + "\n").c_str());
}

void OSC_ControlSurface::SendOSCMessage(OSC_FeedbackProcessor* feedbackProcessor, string oscAddress, double value)
{
    if(outSocket_ != nullptr && outSocket_->isOk())
    {
        oscpkt::Message message;
        message.init(oscAddress).pushFloat(value);
        packetWriter_.init().addMessage(message);
        outSocket_->sendPacket(packetWriter_.packetData(), packetWriter_.packetSize());
    }
    
    if(TheManager->GetSurfaceOutDisplay())
    {
        if(TheManager->GetSurfaceOutDisplay())
            DAW::ShowConsoleMsg(("OUT->" + name_ + " " + oscAddress + " " + to_string(value) + "\n").c_str());
    }
}

void OSC_ControlSurface::SendOSCMessage(OSC_FeedbackProcessor* feedbackProcessor, string oscAddress, string value)
{
    if(outSocket_ != nullptr && outSocket_->isOk())
    {
        oscpkt::Message message;
        message.init(oscAddress).pushStr(value);
        packetWriter_.init().addMessage(message);
        outSocket_->sendPacket(packetWriter_.packetData(), packetWriter_.packetSize());
    }
    
    SurfaceOutMonitor(feedbackProcessor->GetWidget(), oscAddress, value);
    
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
// For EuCon
/////////////////////////////////////////////////////////////////////////////
class MarshalledFunctionCall
/////////////////////////////////////////////////////////////////////////////
{
protected:
    EuCon_ControlSurface* surface_ = nullptr;
    MarshalledFunctionCall(EuCon_ControlSurface * surface) : surface_(surface) {}
    
public:
    virtual void Execute() {}
    virtual ~MarshalledFunctionCall() {}
};

/////////////////////////////////////////////////////////////////////////////
class Marshalled_Double : public MarshalledFunctionCall
/////////////////////////////////////////////////////////////////////////////
{
private:
    string address_ = "";
    double value_ = 0;
public:
    Marshalled_Double(EuCon_ControlSurface* surface, string address, double value) : MarshalledFunctionCall(surface), address_(address), value_(value)  { }
    virtual ~Marshalled_Double() {}
    
    virtual void Execute() override { surface_->HandleEuConMessage(address_, value_); }
};

/////////////////////////////////////////////////////////////////////////////
class Marshalled_String : public MarshalledFunctionCall
/////////////////////////////////////////////////////////////////////////////
{
private:
    string address_ = "";
    string  value_ = "";
    
public:
    Marshalled_String(EuCon_ControlSurface* surface, string address, string value) : MarshalledFunctionCall(surface), address_(address), value_(value)  { }
    virtual ~Marshalled_String() {}
    
    virtual void Execute() override { surface_->HandleEuConMessage(address_, value_); }
};

/////////////////////////////////////////////////////////////////////////////
class Marshalled_VisibilityChange : public MarshalledFunctionCall
/////////////////////////////////////////////////////////////////////////////
{
private:
    string groupName_ = "";
    int  channelNumber_ = 0;
    bool isVisible_ = false;
    
public:
    Marshalled_VisibilityChange(EuCon_ControlSurface* surface, string groupName, int channelNumber, bool isVisible) : MarshalledFunctionCall(surface), groupName_(groupName), channelNumber_(channelNumber), isVisible_(isVisible)  { }
    virtual ~Marshalled_VisibilityChange() {}
    
    virtual void Execute() override { surface_->HandleEuConGroupVisibilityChange(groupName_, channelNumber_, isVisible_); }
};

void EuConRequestsInitialization()
{
    if(TheManager)
        TheManager->InitializeEuCon();
}

void InitializeEuConWidgets(vector<CSIWidgetInfo> *assemblyInfoItems)
{
    if(TheManager)
        TheManager->InitializeEuConWidgets(assemblyInfoItems);
}

void HandleEuConMessageWithDouble(const char *address, double value)
{
    if(TheManager)
        TheManager->ReceiveEuConMessage(string(address), value);
}

void HandleEuConMessageWithString(const char *address, const char *value)
{
    if(TheManager)
        TheManager->ReceiveEuConMessage(string(address), string(value));
}

void HandleEuConGroupVisibilityChange(const char *groupName, int channelNumber, bool isVisible)
{
    if(TheManager)
        TheManager->ReceiveEuConGroupVisibilityChange(string(groupName), channelNumber, isVisible);
}

void HandleEuConGetMeterValues(int id, int iLeg, float& oLevel, float& oPeak, bool& oLegClip)
{
    if(TheManager)
        TheManager->ReceiveEuConGetMeterValues(id, iLeg, oLevel, oPeak, oLegClip);
}

void GetFormattedFXParamValue(const char* address, char *buffer, int bufferSize)
{
    if(TheManager)
        TheManager->GetFormattedFXParamValue(address, buffer, bufferSize);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// EuCon_ControlSurface
////////////////////////////////////////////////////////////////////////////////////////////////////////
EuCon_ControlSurface::EuCon_ControlSurface(CSurfIntegrator* CSurfIntegrator, Page* page, const string name, string zoneFolder, int numChannels, int numSends, int numFX, int options)
: ControlSurface(CSurfIntegrator, page, name, zoneFolder, numChannels, numSends, numFX, options)
{
    fxActivationManager_->SetShouldShowFXWindows(true);
    
    if( ! plugin_register("API_EuConRequestsInitialization", (void *)::EuConRequestsInitialization))
        LOG::InitializationFailure("EuConRequestsInitialization failed to register");

    if( ! plugin_register("API_InitializeEuConWidgets", (void *)::InitializeEuConWidgets))
        LOG::InitializationFailure("InitializeEuConWidgets failed to register");
   
    if( ! plugin_register("API_HandleEuConMessageWithDouble", (void *)::HandleEuConMessageWithDouble))
        LOG::InitializationFailure("HandleEuConMessageWithDouble failed to register");
    
    if( ! plugin_register("API_HandleEuConMessageWithString", (void *)::HandleEuConMessageWithString))
        LOG::InitializationFailure("HandleEuConMessageWithString failed to register");
    
    if( ! plugin_register("API_HandleEuConGroupVisibilityChange", (void *)::HandleEuConGroupVisibilityChange))
        LOG::InitializationFailure("HandleEuConGroupVisibilityChange failed to register");
    
    if( ! plugin_register("API_HandleEuConGetMeterValues", (void *)::HandleEuConGetMeterValues))
        LOG::InitializationFailure("HandleEuConGetMeterValues failed to register");
    
    if( ! plugin_register("API_GetFormattedFXParamValue", (void *)::GetFormattedFXParamValue))
        LOG::InitializationFailure("GetFormattedFXParamValue failed to register");
    
    InitializeEuCon();
}

void EuCon_ControlSurface::InitializeEuCon()
{
    static void (*InitializeEuConWithParameters)(int numChannels, int numSends, int numFX, int panOptions) = nullptr;

    if(g_reaper_plugin_info && InitializeEuConWithParameters == nullptr)
        InitializeEuConWithParameters = (void (*)(int, int, int, int))g_reaper_plugin_info->GetFunc("InitializeEuConWithParameters");

    if(InitializeEuConWithParameters)
        InitializeEuConWithParameters(numChannels_, numSends_, fxActivationManager_->GetNumFXSlots(), options_);
}

Widget*  EuCon_ControlSurface::InitializeEuConWidget(CSIWidgetInfo &widgetInfo)
{
    if(widgetInfo.name != "")
    {
        Widget* widget = new Widget(this, widgetInfo.name);
        
        if(!widget)
            return nullptr;
        
        if(widgetInfo.control != "")
            new EuCon_CSIMessageGenerator(this, widget, widgetInfo.control);
       
        if(widgetInfo.FB_Processor != "")
        {
            if(widgetInfo.FB_Processor.find("FaderDB") != string::npos)
                widget->AddFeedbackProcessor(new EuCon_FeedbackProcessorDB(this, widget, widgetInfo.FB_Processor));
            else
                widget->AddFeedbackProcessor(new EuCon_FeedbackProcessor(this, widget, widgetInfo.FB_Processor));
        }
        
        return widget;
    }
    
    return nullptr;
}

void EuCon_ControlSurface::InitializeEuConWidgets(vector<CSIWidgetInfo> *widgetInfoItems)
{
    for(auto item : *widgetInfoItems)
    {
        if(Widget* widget = InitializeEuConWidget(item))
        {
            AddWidget(widget);
            
            if(item.channelNumber > 0 && channelGroups_.count(item.channelNumber) < 1 )
                channelGroups_[item.channelNumber] = new WidgetGroup();

            if(item.group == "General")
                generalWidgets_.push_back(widget);
            
            if(channelGroups_.count(item.channelNumber) > 0)
            {
                if(item.group == "Channel")
                    channelGroups_[item.channelNumber]->AddWidget(widget);
                else
                    channelGroups_[item.channelNumber]->AddWidgetToSubgroup(item.group, widget);
            }
        }
    }
    
    InitHardwiredWidgets();
    InitZones(zoneFolder_);
    MakeHomeDefault();
    ForceClearAllWidgets();
    GetPage()->ForceRefreshTimeDisplay();
}

void EuCon_ControlSurface::SendEuConMessage(EuCon_FeedbackProcessor* feedbackProcessor, string address, double value)
{
    static void (*HandleReaperMessageWthDouble)(const char *, double) = nullptr;
    
    if(g_reaper_plugin_info && HandleReaperMessageWthDouble == nullptr)
        HandleReaperMessageWthDouble = (void (*)(const char *, double))g_reaper_plugin_info->GetFunc("HandleReaperMessageWthDouble");
    
    if(HandleReaperMessageWthDouble)
        HandleReaperMessageWthDouble(address.c_str(), value);
    
    SurfaceOutMonitor(feedbackProcessor->GetWidget(), address, to_string(value));
}

void EuCon_ControlSurface::SendEuConMessage(EuCon_FeedbackProcessor* feedbackProcessor, string address, double value, int param)
{
    static void (*HandleReaperMessageWthParam)(const char *, double, int) = nullptr;
    
    if(g_reaper_plugin_info && HandleReaperMessageWthParam == nullptr)
        HandleReaperMessageWthParam = (void (*)(const char *, double, int))g_reaper_plugin_info->GetFunc("HandleReaperMessageWthParam");
    
    if(HandleReaperMessageWthParam)
        HandleReaperMessageWthParam(address.c_str(), value, param);
    
    SurfaceOutMonitor(feedbackProcessor->GetWidget(), address, to_string(value));
}

void EuCon_ControlSurface::SendEuConMessage(EuCon_FeedbackProcessor* feedbackProcessor, string address, string value)
{
    if(address.find("Pan_Display") != string::npos
       || address.find("Width_Display") != string::npos
       || address.find("PanL_Display") != string::npos
       || address.find("PanR_Display") != string::npos)
    {
        return; // GAW -- Hack to prevent overwrite of Pan, Width, etc. labels
    }
    
    static void (*HandleReaperMessageWthString)(const char *, const char *) = nullptr;
    
    if(g_reaper_plugin_info && HandleReaperMessageWthString == nullptr)
        HandleReaperMessageWthString = (void (*)(const char *, const char *))g_reaper_plugin_info->GetFunc("HandleReaperMessageWithString");
    
    if(HandleReaperMessageWthString)
        HandleReaperMessageWthString(address.c_str(), value.c_str());
    
    if(TheManager->GetSurfaceOutDisplay())
        DAW::ShowConsoleMsg(("OUT-> " + name_ + " " + address + " " + value + "\n").c_str());
}

void EuCon_ControlSurface::SendEuConMessage(string address, string value)
{
    static void (*HandleReaperMessageWthString)(const char *, const char *) = nullptr;
    
    if(g_reaper_plugin_info && HandleReaperMessageWthString == nullptr)
        HandleReaperMessageWthString = (void (*)(const char *, const char *))g_reaper_plugin_info->GetFunc("HandleReaperMessageWithString");
    
    if(HandleReaperMessageWthString)
        HandleReaperMessageWthString(address.c_str(), value.c_str());
}

void EuCon_ControlSurface::ReceiveEuConGetMeterValues(int id, int iLeg, float& oLevel, float& oPeak, bool& oLegClip)
{
    if(MediaTrack* track = GetPage()->GetTrackNavigationManager()->GetTrackFromChannel(id))
    {
        float left = VAL2DB(DAW::Track_GetPeakInfo(track, 0));
        float right = VAL2DB(DAW::Track_GetPeakInfo(track, 1));
        
        oLevel = (left + right) / 2.0;
       
        float max = left > right ? left : right;
        
        if(peakInfo_.count(id) > 0 && peakInfo_[id].peakValue < max)
        {
            peakInfo_[id].timePeakSet  = DAW::GetCurrentNumberOfMilliseconds();
            peakInfo_[id].peakValue = max;
            if(max > 0.0)
                peakInfo_[id].isClipping = true;
        }
        
        if(peakInfo_.count(id) < 1)
        {
            peakInfo_[id].timePeakSet  = DAW::GetCurrentNumberOfMilliseconds();
            peakInfo_[id].peakValue = max;
            if(max > 0.0)
                peakInfo_[id].isClipping = true;
        }
        
        if(peakInfo_.count(id) > 0 && (DAW::GetCurrentNumberOfMilliseconds() - peakInfo_[id].timePeakSet > 2000))
        {
            peakInfo_[id].timePeakSet  = DAW::GetCurrentNumberOfMilliseconds();
            peakInfo_[id].peakValue = max;
            peakInfo_[id].isClipping = false;
        }
        
        oPeak = peakInfo_[id].peakValue;
        oLegClip = peakInfo_[id].isClipping;
    }
    else
    {
        oLevel = -144.0;
        oPeak = -144.0;
        oLegClip = false;
    }
}

void EuCon_ControlSurface::GetFormattedFXParamValue(const char* address, char *buffer, int bufferSize)
{
    if(widgetsByName_.count(address) > 0)
        widgetsByName_[address]->GetFormattedFXParamValue(buffer, bufferSize);
}

void EuCon_ControlSurface::ReceiveEuConMessage(string address, double value)
{
    mutex_.Enter();
    workQueue_.push_front(new Marshalled_Double(this, address, value));
    mutex_.Leave();
}

void EuCon_ControlSurface::ReceiveEuConMessage(string address, string value)
{
    mutex_.Enter();
    workQueue_.push_front(new Marshalled_String(this, address, value));
    mutex_.Leave();
}

void EuCon_ControlSurface::ReceiveEuConGroupVisibilityChange(string groupName, int channelNumber, bool isVisible)
{
    mutex_.Enter();
    workQueue_.push_front(new Marshalled_VisibilityChange(this, groupName, channelNumber, isVisible));
    mutex_.Leave();
}

void EuCon_ControlSurface::HandleEuConGroupVisibilityChange(string groupName, int channelNumber, bool isVisible)
{
    if(groupName == "FX")
    {
        
        if(isVisible && widgetsByName_.count("OnEuConFXAreaGainedFocus") > 0)
        {
            isEuConFXAreaFocused_ = true;
            widgetsByName_["OnEuConFXAreaGainedFocus"]->DoAction(1.0);
        }
        
        if( ! isVisible && widgetsByName_.count("OnEuConFXAreaLostFocus") > 0)
        {
            isEuConFXAreaFocused_ = false;
            widgetsByName_["OnEuConFXAreaLostFocus"]->DoAction(1.0);
        }
    }
    
    if(groupName == "FX")
        for(auto [channel, group] : channelGroups_)
            group->SetIsVisible("FX", isVisible);
    
    if(groupName == "Pan")
        for(auto [channel, group] : channelGroups_)
            group->SetIsVisible("Pan", isVisible);
    
    else if(groupName == "Send")
        for(auto [channel, group] : channelGroups_)
            group->SetIsVisible("Send", isVisible);
    
    else if(groupName == "Channel" && channelGroups_.count(channelNumber) > 0)
        channelGroups_[channelNumber]->SetIsVisible(isVisible);
}

void EuCon_ControlSurface::HandleExternalInput()
{
    if(! workQueue_.empty())
    {
        mutex_.Enter();
        list<MarshalledFunctionCall*> localWorkQueue = workQueue_;
        workQueue_.clear();
        mutex_.Leave();
        
        while(! localWorkQueue.empty())
        {
            MarshalledFunctionCall *pCall = localWorkQueue.back();
            localWorkQueue.pop_back();
            pCall->Execute();
            delete pCall;
        }
    }
}

void EuCon_ControlSurface::HandleEuConMessage(string address, double value)
{
    if(address == "PostMessage" && g_hwnd != nullptr)
        PostMessage(g_hwnd, WM_COMMAND, (int)value, 0);
    else if(address == "LayoutChanged")
        DAW::MarkProjectDirty(nullptr);
    else if(CSIMessageGeneratorsByMessage_.count(address) > 0)
        CSIMessageGeneratorsByMessage_[address]->ProcessMessage(address, value);
        
    if(TheManager->GetSurfaceInDisplay())
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "IN <- %s %s  %f  \n", name_.c_str(), address.c_str(), value);
        DAW::ShowConsoleMsg(buffer);
    }
}

void EuCon_ControlSurface::HandleEuConMessage(string address, string value)
{
    // GAW TBD
}

void EuCon_ControlSurface::UpdateTimeDisplay()
{
    double playPosition = (GetPlayState() & 1 ) ? GetPlayPosition() : GetCursorPosition();
    
    if(previousPP != playPosition) // GAW :) Yeah I know shouldn't compare FP values, but the worst you get is an extra upadate or two, meh.
    {
        previousPP = playPosition;

        int *timeModePtr = TheManager->GetTimeMode2Ptr(); // transport
        
        int timeMode = 0;
        
        if (timeModePtr && (*timeModePtr) >= 0)
            timeMode = *timeModePtr;
        else
        {
            timeModePtr = TheManager->GetTimeModePtr(); // ruler
            
            if (timeModePtr)
                timeMode = *timeModePtr;
        }
        
        char samplesBuf[64];
        memset(samplesBuf, 0, sizeof(samplesBuf));
        char measuresBuf[64];
        memset(measuresBuf, 0, sizeof(measuresBuf));
        char chronoBuf[64];
        memset(chronoBuf, 0, sizeof(chronoBuf));

        if(timeMode == 4)  // Samples
        {
            format_timestr_pos(playPosition, samplesBuf, sizeof(samplesBuf), timeMode);
        }
        
        if(timeMode == 1 || timeMode == 2)  // Bars/Beats/Ticks
        {
            int num_measures = 0;
            double beats = TimeMap2_timeToBeats(NULL, playPosition, &num_measures, NULL, NULL, NULL) + 0.000000000001;
            double nbeats = floor(beats);
            beats -= nbeats;
            format_timestr_pos(playPosition, measuresBuf, sizeof(measuresBuf), 2);
        }
        
        if(timeMode == 0 || timeMode == 1 || timeMode == 3  ||  timeMode == 5)  // Hours/Minutes/Seconds/Frames
        {
            double *timeOffsetPtr = TheManager->GetTimeOffsPtr();
            if (timeOffsetPtr)
                playPosition += (*timeOffsetPtr);
            format_timestr_pos(playPosition, chronoBuf, sizeof(chronoBuf), timeMode == 1 ? 0 : timeMode);
        }
        
        switch(timeMode)
        {
            case 0: // Hours/Minutes/Seconds
            case 3: // Seconds
            case 5: // Hours/Minutes/Seconds/Frames
                SendEuConMessage("PrimaryTimeDisplay", chronoBuf);
                SendEuConMessage("SecondaryTimeDisplay", "");
                break;
                
            case 1:
                SendEuConMessage("PrimaryTimeDisplay", measuresBuf);
                SendEuConMessage("SecondaryTimeDisplay", chronoBuf);
                break;
                
            case 2:
                SendEuConMessage("PrimaryTimeDisplay", measuresBuf);
                SendEuConMessage("SecondaryTimeDisplay", "");
                break;
                
            case 4:
                SendEuConMessage("PrimaryTimeDisplay", samplesBuf);
                SendEuConMessage("SecondaryTimeDisplay", "");
                break;
        }
    }
}
