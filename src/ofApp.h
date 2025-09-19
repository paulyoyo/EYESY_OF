/*
 * Copyright (c) 2020 Owen Osborn, Critter & Gutiari, Inc.
 *
 * BSD Simplified License.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.
 *
 */
#pragma once

#include "ofMain.h"
#include "ofxLua.h"
#include "ofxOsc.h"
#include "ofxMidi.h"

// Forward declaration
class ofxMidiClock;

#define PORT 4000
#define MIDI_BUFFER_SIZE 256

class ofApp : public ofBaseApp, ofxLuaListener, ofxMidiListener {

    public:
        ofApp();
        ~ofApp();

        // main
        void setup();
        void update();
        void draw();
        void exit();
        
        // input
        void keyPressed(int key);
        void mouseMoved(int x, int y);
        void mouseDragged(int x, int y, int button);
        void mousePressed(int x, int y, int button);
        void mouseReleased(int x, int y, int button);
        
        // ofxLua error callback
        void errorReceived(std::string& msg);
        
        // script control
        void reloadScript();
        void nextScript();
        void prevScript();
    
        ofxLua lua;
        vector<string> scripts;
        size_t currentScript;

        // osc control
        ofxOscReceiver receiver;

        // audio stuff
        void audioIn(ofSoundBuffer & input);
    
        vector <lua_Number> left;
        vector <lua_Number> right;

        int     bufferCounter;
        int     drawCounter;
    
        float smoothedVol;
        float scaledVol;
        
        ofSoundStream soundStream;

        int                 snapCounter;
        string              snapString;
        ofImage             img;
        
        // Persist graphics functionality
        bool                persistEnabled;
        bool                persistFirstRender;
        ofFbo               persistFbo;

        // MIDI functionality
        ofxMidiIn           midiIn;
        vector<vector<lua_Number>> midiMessages;
        void                newMidiMessage(ofxMidiMessage& eventArgs);
        void                setupMidi();
        
        // MIDI Clock functionality  
        ofxMidiClock*       midiClock;
        
        // OSD functionality
        bool                osdEnabled;
        vector<string>      recentMidiNotes;
        float               audioLevel;
        int                 clockMessageCount;
        float               calculatedBPM;
};
