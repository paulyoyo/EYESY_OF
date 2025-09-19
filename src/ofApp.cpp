/*
 * Copyright (c) 2020 Owen Osborn, Critter & Gutiari, Inc.
 *
 * BSD Simplified License.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.
 *
 */
#include "ofApp.h"
#include "ofxMidiClock.h"

//--------------------------------------------------------------
ofApp::ofApp() {
  midiClock = nullptr;
  persistEnabled = false;
  persistFirstRender = true;
  osdEnabled = false;
  audioLevel = 0.0f;
  clockMessageCount = 0;
  calculatedBPM = 120.0f;
}

//--------------------------------------------------------------
ofApp::~ofApp() {
  if (midiClock) {
    delete midiClock;
    midiClock = nullptr;
  }
}

//--------------------------------------------------------------
void ofApp::setup() {

  // listen on the given port
  // cout << "listening for osc messages on port " << PORT << "\n";
  receiver.setup(PORT);

  ofSetVerticalSync(true);
  ofSetFrameRate(60);
  ofSetLogLevel("ofxLua", OF_LOG_VERBOSE);

  ofHideCursor();

  ofSetBackgroundColor(0, 0, 0);

  // setup audio
  soundStream.printDeviceList();

  int bufferSize = 256;

  left.assign(bufferSize, 0.0);
  right.assign(bufferSize, 0.0);

  bufferCounter = 0;

  ofSoundStreamSettings settings;

  // device by name
  auto devices = soundStream.getMatchingDevices("default");
  if (!devices.empty()) {
    settings.setInDevice(devices[0]);
  }

  settings.setInListener(this);
  settings.sampleRate = 11025;
  settings.numOutputChannels = 0;
  settings.numInputChannels = 2;
  settings.bufferSize = bufferSize;
  soundStream.setup(settings);

  // some path, may be absolute or relative to bin/data
  string path = "/sdcard/Modes/oFLua";
  ofDirectory dir(path);
  dir.listDir();

  // go through and print out all the paths
  for (int i = 0; i < dir.size(); i++) {
    // ofLogNotice(dir.getPath(i) + "/main.lua");
    scripts.push_back(dir.getPath(i) + "/main.lua");
  }

  // scripts to run
  currentScript = 0;

  // init the lua state
  lua.init(true); // true because we want to stop on an error

  // listen to error events
  lua.addListener(this);

  // setup MIDI BEFORE loading scripts
  setupMidi();
  
  // Initialize MIDI Clock
  midiClock = new ofxMidiClock();

  // Initialize persist graphics functionality
  persistEnabled = false;
  persistFirstRender = true;
  persistFbo.allocate(ofGetWidth(), ofGetHeight());

  // MIDI globals are now initialized in the eyesy.lua module

  // run a script
  // true = change working directory to the script's parent dir
  // so lua will find scripts with relative paths via require
  // note: changing dir does *not* affect the OF data path
  lua.doScript(scripts[currentScript], true);

  // call the script's setup() function
  lua.scriptSetup();

  // clear main screen
  ofClear(0, 0, 0);
}

//--------------------------------------------------------------
void ofApp::update() {

  // check for waiting messages
  while (receiver.hasWaitingMessages()) {
    // get the next message
    ofxOscMessage m;
    receiver.getNextMessage(m);
    // cout << "new message on port " << PORT << m.getAddress() << "\n";
    if (m.getAddress() == "/key") {
      if (m.getArgAsInt32(0) == 4 && m.getArgAsInt32(1) > 0) {
        prevScript();
      }
      if (m.getArgAsInt32(0) == 5 && m.getArgAsInt32(1) > 0) {
        nextScript();
      }
      if (m.getArgAsInt32(0) == 9 && m.getArgAsInt32(1) > 0) {
        img.grabScreen(0, 0, ofGetWidth(), ofGetHeight());
        string fileName =
            "snapshot_" + ofToString(10000 + snapCounter) + ".png";
        img.save("/sdcard/Grabs/" + fileName);
        snapCounter++;
      }
      if (m.getArgAsInt32(0) == 10 && m.getArgAsInt32(1) > 0) {
        lua.setBool("trig", true);
      }
      if (m.getArgAsInt32(0) == 3 && m.getArgAsInt32(1) > 0) {
        persistEnabled = !persistEnabled;
        persistFirstRender = true;
      }
      if (m.getArgAsInt32(0) == 1 && m.getArgAsInt32(1) > 0) {
        osdEnabled = !osdEnabled;
      }
    }
    if (m.getAddress() == "/knobs") {
      lua.setNumber("knob1", (float)m.getArgAsInt32(0) / 1023);
      lua.setNumber("knob2", (float)m.getArgAsInt32(1) / 1023);
      lua.setNumber("knob3", (float)m.getArgAsInt32(2) / 1023);
      lua.setNumber("knob4", (float)m.getArgAsInt32(3) / 1023);
      lua.setNumber("knob5", (float)m.getArgAsInt32(4) / 1023);
    }
    if (m.getAddress() == "/reload") {
      reloadScript();
    }
    
    // Handle MIDI note messages from Pure Data
    if (m.getAddress() == "/midinote") {
      int pitch = m.getArgAsInt32(0);
      int velocity = m.getArgAsInt32(1);
      
      // Create MIDI message data array: {status, channel, pitch, velocity, control, value, portNum, portName}
      vector<lua_Number> midiData(8);
      midiData[0] = velocity > 0 ? 144 : 128;  // Note On (0x90) or Note Off (0x80)
      midiData[1] = 0;                         // Channel (will be set by Pure Data filtering)
      midiData[2] = pitch;                     // Pitch
      midiData[3] = velocity;                  // Velocity
      midiData[4] = 0;                         // Control (not used for notes)
      midiData[5] = 0;                         // Value (not used for notes)
      midiData[6] = 129;                       // Port number (Pure Data port)
      midiData[7] = 0;                         // Port name (string index)
      
      // Add to message queue
      midiMessages.push_back(midiData);
      
      // Update OSD display
      if (osdEnabled) {
        string noteStr = "Note: " + ofToString(pitch) + " Ch:" + ofToString((int)midiData[1] + 1) + " Vel:" + ofToString(velocity);
        recentMidiNotes.push_back(noteStr);
        if (recentMidiNotes.size() > 5) {
          recentMidiNotes.erase(recentMidiNotes.begin());
        }
      }
      
      // Keep only recent messages to avoid memory buildup
      if (midiMessages.size() > 100) {
        midiMessages.erase(midiMessages.begin());
      }
    }
    
    // Handle MIDI control change messages from Pure Data
    if (m.getAddress() == "/midicc") {
      int control = m.getArgAsInt32(0);
      int value = m.getArgAsInt32(1);
      
      // Create MIDI message data array: {status, channel, pitch, velocity, control, value, portNum, portName}
      vector<lua_Number> midiData(8);
      midiData[0] = 176;                       // Control Change (0xB0)
      midiData[1] = 0;                         // Channel (will be set by Pure Data filtering)
      midiData[2] = 0;                         // Pitch (not used for CC)
      midiData[3] = 0;                         // Velocity (not used for CC)
      midiData[4] = control;                   // Control number
      midiData[5] = value;                     // Control value
      midiData[6] = 129;                       // Port number (Pure Data port)
      midiData[7] = 0;                         // Port name (string index)
      
      // Add to message queue
      midiMessages.push_back(midiData);
      
      // Debug output (commented out for production)
      // ofLogNotice("OSC-MIDI") << "Received MIDI CC: control=" << control << " value=" << value;
      
      // Keep only recent messages to avoid memory buildup
      if (midiMessages.size() > 100) {
        midiMessages.erase(midiMessages.begin());
      }
    }
  }

  // Process MIDI messages and send to Lua
  if (!midiMessages.empty()) {
    // Convert first available MIDI message to Lua table
    vector<lua_Number> midiMsg = midiMessages[0];
    lua.setNumberVector("midi_data", midiMsg);
    lua.setBool("midi_available", true);


    // Remove processed message
    midiMessages.erase(midiMessages.begin());
  } else {
    lua.setBool("midi_available", false);
  }

  // Set midi_enabled status based on whether MIDI input is connected
  lua.setBool("midi_enabled", midiIn.isOpen());
  
  // Update MIDI clock globals for Lua scripts
  // Note: ofxMidi version uses different API, calculate beat/bar from beats
  unsigned int totalBeats = midiClock->getBeats();
  unsigned int currentBeat = (totalBeats % 4) + 1;  // 1-4 for 4/4 time
  unsigned int currentBar = (totalBeats / 4) + 1;
  
  // Update calculated BPM from MIDI clock
  calculatedBPM = midiClock->getBpm();
  
  // Debug: Log clock state every 2 seconds
  static int frameCounter = 0;
  if (++frameCounter % 120 == 0) {  // Every 2 seconds at 60fps
    // ofLogNotice("MIDI CLOCK") << "totalBeats=" << totalBeats << " BPM=" << calculatedBPM;
  }
  
  
  // Detect new beat and bar by comparing with previous
  static unsigned int lastBeat = 0;
  static unsigned int lastBar = 0;
  bool newBeat = (totalBeats != lastBeat);
  bool newBar = (currentBar != lastBar);
  lastBeat = totalBeats;
  lastBar = currentBar;
  
  lua.setNumber("midi_beat", currentBeat);
  lua.setNumber("midi_bar", currentBar);
  lua.setNumber("midi_tick", totalBeats * 6);  // 6 ticks per beat in MIDI clock
  lua.setBool("midi_new_beat", newBeat);
  lua.setNumber("midi_time_numerator", 4);     // Default to 4/4
  lua.setNumber("midi_time_denominator", 4);
  lua.setNumber("midi_bpm", midiClock->getBpm());
  
  // Set trigger flags
  lua.setBool("midi_beat_trigger", newBeat);
  lua.setBool("midi_bar_trigger", newBar);
  // Better transport detection: check if we're receiving regular clock updates
  static float lastClockTime = 0;
  static bool transportRunning = false;
  float currentTime = ofGetElapsedTimef();
  
  if (newBeat) {
    lastClockTime = currentTime;
    transportRunning = true;
  } else if (currentTime - lastClockTime > 2.0f) {  // No beat for 2 seconds = stopped
    transportRunning = false;
  }
  
  lua.setBool("midi_transport_playing", transportRunning);

  // Update persist state for Lua scripts
  lua.setBool("persist", persistEnabled);

  // call the script's update() function
  lua.scriptUpdate();
}

//--------------------------------------------------------------
void ofApp::draw() {

  lua.setNumberVector("inL", left);
  lua.setNumberVector("inR", right);

  // Begin persist graphics rendering if enabled
  if (persistEnabled) {
    persistFbo.begin();
    // Clear any remaining artifacts from GPU
    if (persistFirstRender) {
      persistFirstRender = false;
      ofClear(255, 255, 255, 0);
    }
  }

  lua.scriptDraw();

  // End persist graphics rendering and draw the persisted content
  if (persistEnabled) {
    persistFbo.end();
    persistFbo.draw(0, 0);
  }

  // Draw OSD if enabled
  if (osdEnabled) {
    ofPushStyle();
    ofSetColor(255, 255, 255, 200);

    // Draw semi-transparent background with increased margins
    ofSetColor(0, 0, 0, 120);
    ofDrawRectangle(25, 25, 450, 160);

    ofSetColor(255, 255, 255);
    int yPos = 45;

    // Draw EYESY title
    ofSetColor(255, 255, 255);
    ofDrawBitmapString("EYESY", 35, yPos);
    yPos += 25;
    
    // Display script title
    string scriptTitle = "Unknown Script";
    if (lua.isString("modeTitle")) {
      scriptTitle = lua.getString("modeTitle");
    } else {
      // Fallback: extract filename from current script path
      if (currentScript < scripts.size()) {
        string scriptPath = scripts[currentScript];
        size_t lastSlash = scriptPath.find_last_of("/");
        if (lastSlash != string::npos) {
          string filename = scriptPath.substr(lastSlash + 1);
          size_t lastDot = filename.find_last_of(".");
          if (lastDot != string::npos) {
            scriptTitle = filename.substr(0, lastDot);
          } else {
            scriptTitle = filename;
          }
        }
      }
    }
    ofDrawBitmapString("Script: " + scriptTitle, 35, yPos);
    yPos += 15;

    // Break line below script name
    yPos += 10;

    // Draw horizontal audio meter (moved up)
    yPos += 5;
    float meterWidth = 350;
    float meterHeight = 15;
    float audioMeter = audioLevel * 4.0f; // Scale to reach 100% at full volume

    // Meter background
    ofSetColor(60, 60, 60);
    ofDrawRectangle(35, yPos, meterWidth, meterHeight);

    // Meter level with color zones
    float meterLevel = ofClamp(audioMeter, 0.0f, 1.5f); // Allow up to 150% for red zone
    float displayWidth = meterWidth * (meterLevel / 1.5f); // Scale to fit meter width

    // Determine color based on level
    if (audioMeter < 0.6f) {
        // Green zone: 0-59%
        ofSetColor(0, 255, 0);
    } else if (audioMeter < 1.0f) {
        // Blue zone: 60-99%
        ofSetColor(0, 150, 255);
    } else {
        // Red zone: 100%+
        ofSetColor(255, 0, 0);
    }

    ofDrawRectangle(35, yPos, displayWidth, meterHeight);

    // Beat indicator square (every 4th beat)
    if (midiClock && (midiClock->getBeats() % 4 == 0)) {
        ofSetColor(255, 255, 255);
        ofDrawRectangle(35 + meterWidth + 10, yPos, meterHeight, meterHeight);
    }

    yPos += 25;

    // Break line below audio meter + beat
    yPos += 10;

    // Display MIDI clock info (moved below audio meter)
    string clockInfo = "BPM: " + ofToString(calculatedBPM, 1);
    ofDrawBitmapString(clockInfo, 35, yPos);
    yPos += 15;

    // Display recent MIDI notes (moved to bottom)
    for (const string& note : recentMidiNotes) {
      ofDrawBitmapString(note, 35, yPos);
      yPos += 15;
    }
    
    ofPopStyle();
  }

  // clear flags
  lua.setBool("trig", false);
  
  // Clear MIDI clock trigger flags (they should only last one frame)
  lua.setBool("midi_beat_trigger", false);
  lua.setBool("midi_bar_trigger", false);
}

//--------------------------------------------------------------
void ofApp::audioIn(ofSoundBuffer &input) {

  for (size_t i = 0; i < input.getNumFrames(); i++) {
    left[i] = input[i * 2] * 0.5;
    right[i] = input[i * 2 + 1] * 0.5;
  }
  
  // Calculate audio level for OSD
  float sum = 0.0f;
  for (size_t i = 0; i < input.getNumFrames(); i++) {
    float sample = (left[i] + right[i]) * 0.5f;
    sum += sample * sample;
  }
  audioLevel = sqrt(sum / input.getNumFrames());

  bufferCounter++;
}

//--------------------------------------------------------------
void ofApp::exit() {
  // call the script's exit() function
  lua.scriptExit();

  // clear the lua state
  lua.clear();
  
  // MIDI clock cleanup handled in destructor
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key) {

  switch (key) {

  case 'r':
    reloadScript();
    break;

  case OF_KEY_LEFT:
    prevScript();
    break;

  case OF_KEY_RIGHT:
    nextScript();
    break;

  case ' ':
    lua.doString(
        "print(\"this is a lua string saying you hit the space bar!\")");
    break;
  }

  lua.scriptKeyPressed(key);
}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y) { lua.scriptMouseMoved(x, y); }

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button) {
  lua.scriptMouseDragged(x, y, button);
}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button) {
  lua.scriptMousePressed(x, y, button);
}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button) {
  lua.scriptMouseReleased(x, y, button);
}

//--------------------------------------------------------------
void ofApp::errorReceived(std::string &msg) {
  // ofLogNotice() << "got a script error: " << msg;
}

//--------------------------------------------------------------
void ofApp::reloadScript() {
  // exit, reinit the lua state, and reload the current script
  lua.scriptExit();

  // init OF
  ofSetupScreen();
  ofSetupGraphicDefaults();
  ofSetBackgroundColor(0, 0, 0);

  // load new
  lua.init();
  
  // MIDI globals are reinitialized automatically when eyesy.lua is required
  
  // Reset persist graphics on script reload for clean start
  if (persistEnabled) {
    persistFirstRender = true;
  }
  
  lua.doScript(scripts[currentScript], true);
  lua.scriptSetup();
}

void ofApp::nextScript() {
  currentScript++;
  if (currentScript > scripts.size() - 1) {
    currentScript = 0;
  }
  reloadScript();
}

void ofApp::prevScript() {
  if (currentScript == 0) {
    currentScript = scripts.size() - 1;
  } else {
    currentScript--;
  }
  reloadScript();
}

//--------------------------------------------------------------
// MIDI Implementation using ofxMidi
//--------------------------------------------------------------

void ofApp::setupMidi() {
  midiMessages.clear();

  // Print available MIDI input ports
  // ofLogNotice("MIDI SETUP") << "Available MIDI input ports:";
  // midiIn.listInPorts();
  // ofLogNotice("MIDI SETUP") << "Total ports: " << midiIn.getNumInPorts();

  // Connect to ttymidi port (port 1) instead of invalid port 14
  if (midiIn.getNumInPorts() > 1) {
    midiIn.openPort(1);  // Use ttymidi port
    
    // Production MIDI filtering: Allow timing messages (for clock) but ignore sysex and sense
    // Parameters: ignoreTypes(sysex, timing, sense)
    midiIn.ignoreTypes(true, false, true);  // Ignore sysex and active sensing, allow timing for clock
    
    midiIn.addListener(this);
    // ofLogNotice("MIDI") << "Connected to MIDI port 1: " << midiIn.getName();
  } else {
    // ofLogWarning("MIDI") << "No MIDI input ports available";
  }
}

void ofApp::newMidiMessage(ofxMidiMessage &msg) {
  // DEBUG: Log ALL incoming MIDI messages with raw bytes
  string bytesStr = "";
  for (int i = 0; i < msg.bytes.size(); i++) {
    bytesStr += ofToString((int)msg.bytes[i]) + " ";
  }
  // ofLogNotice("MIDI RAW") << "Status: " << msg.status << " Channel: " << msg.channel 
                          // << " Bytes: [" << bytesStr << "] Size: " << msg.bytes.size();
  
  // Pass message to MIDI clock using built-in ofxMidi API
  bool clockHandled = midiClock->update(msg.bytes);
  // ofLogNotice("MIDI CLOCK UPDATE") << "Clock handled: " << clockHandled;
  
  // Count and handle timing messages
  static float lastClockTime = 0;
  bool isTimingMessage = false;
  
  if (msg.status == MIDI_TIME_CLOCK || 
      msg.status == MIDI_START || 
      msg.status == MIDI_STOP || 
      msg.status == MIDI_CONTINUE ||
      msg.status == MIDI_SONG_POS_POINTER) {
    isTimingMessage = true;
    // ofLogNotice("MIDI TIMING") << "Timing message: status=" << msg.status;
    
    // Count clock messages and calculate BPM
    if (msg.status == MIDI_TIME_CLOCK) {
      clockMessageCount++;
      float currentTime = ofGetElapsedTimef();
      
      // Calculate BPM every 24 clock messages (1 beat)
      if (clockMessageCount % 24 == 0 && lastClockTime > 0) {
        float timeDiff = currentTime - lastClockTime;
        if (timeDiff > 0) {
          calculatedBPM = 60.0f / timeDiff; // BPM calculation
        }
        lastClockTime = currentTime;
      } else if (lastClockTime == 0) {
        lastClockTime = currentTime;
      }
    }
    
    // DON'T return here - let timing messages also go to Lua
  }
  
  // Convert ofxMidiMessage to the expected Lua format for non-timing messages
  vector<lua_Number> midiData(8,
                              0); // Format: {status, channel, pitch, velocity,
                                  // control, value, portNum, portName}

  midiData[0] = msg.status;   // status
  midiData[1] = msg.channel;  // channel (already 1-16 in ofxMidi)
  midiData[2] = msg.pitch;    // pitch/note
  midiData[3] = msg.velocity; // velocity
  midiData[4] = msg.control;  // control number
  midiData[5] = msg.value;    // control value
  midiData[6] = msg.portNum;  // port number
  midiData[7] = 0;            // portName (string index, 0 for now)

  // Add to message queue
  midiMessages.push_back(midiData);

  // Basic debug output (remove or comment out for production)
  // ofLogNotice("MIDI") << "Received MIDI message: "
  //                     << msg.getStatusString(msg.status)
  //                     << " status:" << msg.status
  //                     << " ch:" << msg.channel << " pitch:" << msg.pitch
  //                     << " vel:" << msg.velocity << " ctrl:" << msg.control
  //                     << " val:" << msg.value;

  // Keep only recent messages to avoid memory buildup
  if (midiMessages.size() > 100) {
    midiMessages.erase(midiMessages.begin());
  }
}


