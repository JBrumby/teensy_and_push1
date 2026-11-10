// Hardpush.cpp
//


// use this way:
// ================================================
// USBHost myusb;
// USBHub hub1(myusb);
// MIDIDevice pushMidi(myusb);
// bool pushReady = false;
//
// Hardpush push1;
//
// void setup() {
//     push1.begin(myusb, hub1, pushMidi);
// }
//=================================================


//
// https://pushmod.blogspot.com/

// - make the buttons more sensitive, sniff original sysex
// - more efficient handeling of displaying input notes
// - Make the display functions build on eachother

#include "Hardpush.h"

#include <USBHost_t36.h>

// #include <MIDI.h>
// #include <Bounce2.h>
#include <EEPROM.h>
#include <SD.h>

Hardpush* Hardpush::instance = nullptr;


void Hardpush::onNoteOnUSB(byte ch, byte p, byte v)          { if (instance) instance->handleNoteOnUSB(ch, p, v); }
void Hardpush::onNoteOffUSB(byte ch, byte p, byte v)         { if (instance) instance->handleNoteOffUSB(ch, p, v); }
void Hardpush::onControlChangeUSB(byte ch, byte c, byte v)   { if (instance) instance->handleControlChangeUSB(ch, c, v); }
void Hardpush::onPitchBendUSB(byte ch, int bend)             { if (instance) instance->handlePitchBendUSB(ch, bend); }
void Hardpush::onAfterTouchPolyUSB(byte ch, byte n, byte pr) { if (instance) instance->handleAfterTouchPolyUSB(ch, n, pr); }
void Hardpush::onAfterTouchChannelUSB(byte ch, byte pr)      { if (instance) instance->handleAfterTouchChannelUSB(ch, pr); }
void Hardpush::onNoteOnDIN(byte ch, byte p, byte v)          { if (instance) instance->handleNoteOnDIN(ch, p, v); }
void Hardpush::onNoteOffDIN(byte ch, byte p, byte v)         { if (instance) instance->handleNoteOffDIN(ch, p, v); }

// ---------------------------------------------------------------------------
//  USB host + devices -> in instanz created
// ---------------------------------------------------------------------------
//USBHost     myusb;
//USBHub      hub1(myusb);
//MIDIDevice  pushMidi(myusb);
// bool pushReady = false;

// MIDI_CREATE_INSTANCE(HardwareSerial, Serial1,     midiUSB); to PUsh 1
// MIDI_CREATE_INSTANCE(HardwareSerial, Serial2,    midiDIN);  to Computer

///////////////////////////////////////////////////////////
//
// EEPROM ADRESSES
//
///////////////////////////////////////////////////////////

enum {
  PAGE_EEPROM_ADRESS = 2,
  FEEDBACK_EEPROM_ADRESS = 3,
  PAGE1_CHANNEL_EEPROM_ADRESS,
  PAGE2_CHANNEL_EEPROM_ADRESS,
  PAGE3_CHANNEL_EEPROM_ADRESS,
  PAGE4_CHANNEL_EEPROM_ADRESS,
  PAGE1_COLOR_EEPROM_ADRESS,
  PAGE2_COLOR_EEPROM_ADRESS,
  PAGE3_COLOR_EEPROM_ADRESS,
  PAGE4_COLOR_EEPROM_ADRESS,
  PAGE1_OCTAVE_EEPROM_ADRESS,
  PAGE2_OCTAVE_EEPROM_ADRESS,
  PAGE3_OCTAVE_EEPROM_ADRESS,
  PAGE4_OCTAVE_EEPROM_ADRESS,
  PAGE1_LAYOUT_EEPROM_ADRESS,
  PAGE2_LAYOUT_EEPROM_ADRESS,
  PAGE3_LAYOUT_EEPROM_ADRESS,
  PAGE4_LAYOUT_EEPROM_ADRESS,
  SCALE_PRESET_EEPROM_ADRESS,
  PRESET1_TONIC_EEPROM_ADRESS,
  PRESET2_TONIC_EEPROM_ADRESS,
  PRESET3_TONIC_EEPROM_ADRESS,
  PRESET4_TONIC_EEPROM_ADRESS,
  PRESET1_SCALE_EEPROM_ADRESS,
  PRESET2_SCALE_EEPROM_ADRESS,
  PRESET3_SCALE_EEPROM_ADRESS,
  PRESET4_SCALE_EEPROM_ADRESS,
};

// SET A VALUE IN EEPROM
void Hardpush::eepromSet(byte adress, byte value, bool set_dirty)
{
  if (PUSH_DEBUG) Serial.printf("SD-Card SET-value Address=%u Value=%u \n ", (unsigned)adress, (unsigned)value);
  if (!sd_ready) return;   // früher Ausstieg, wenn SD nicht bereit 
  // EEPROM.update(adress, value);
  config_file.seek(adress);
  config_file.write((uint8_t) value);
  if (set_dirty) sd_is_dirty = true; 
  // config_file.flush(); in loop verlagert
}
// GET A VALUE FROM EEPROM
byte Hardpush::eepromGet(byte adress, byte minValue, byte maxValue, byte defaultValue)
{
  if (PUSH_DEBUG) Serial.printf("SD-Card GET-value Address=%u Default_Value=%u\n", (unsigned)adress, (unsigned)defaultValue);
  
  // return defaultValue;  // NO WRITING. DISABLED
  
  // byte value = EEPROM.read(adress);
  config_file.seek(adress);
  int raw = config_file.read(); // to catch -1 if no data read from file
  byte value = (raw < 0) ? defaultValue : (byte)raw;
  if (PUSH_DEBUG) Serial.printf("SD-Card GET-value Address=%u Read_Value=%u\n", (unsigned)adress, (unsigned)value);
  if(value == defaultValue)
    return value;
  if(value < minValue || value > maxValue)
  {
    value = defaultValue;
    eepromSet(adress, value);
  }
  return value;
}

void Hardpush::loadSettings(File& file) {
  config_file = file;
  sd_ready    = true;

  instrumentPageIndex = eepromGet(PAGE_EEPROM_ADRESS,        0, numberOfPages - 1,       0);
  scalePresetIndex    = eepromGet(SCALE_PRESET_EEPROM_ADRESS,0, numberOfScalePresets - 1,0);

  for (int i = 0; i < numberOfPages; i++) {
    pageChannels[i] = eepromGet(PAGE1_CHANNEL_EEPROM_ADRESS + i, 1, 16,                     i + 1);
    pageColors[i]   = eepromGet(PAGE1_COLOR_EEPROM_ADRESS   + i, 0, numberOfColorSchemes-1,i);
    pageLayouts[i]  = eepromGet(PAGE1_LAYOUT_EEPROM_ADRESS  + i, 0, numberOfLayouts - 1,   0);
    pageOctaves[i]  = eepromGet(PAGE1_OCTAVE_EEPROM_ADRESS  + i, 0, 8,                     3);
  }
  for (int i = 0; i < numberOfScalePresets; i++) {
    scalePresetScales[i] = eepromGet(PRESET1_SCALE_EEPROM_ADRESS + i, 0, numberOfScales - 1, 1);
    scalePresetTonics[i] = eepromGet(PRESET1_TONIC_EEPROM_ADRESS + i, 0, 11,                 0);
  }
}

void Hardpush::saveIfDirty() {
  if (!sd_ready || !sd_is_dirty) return;
  if (!SD.sdfs.card()) return;
  if (SD.sdfs.card()->isBusy()) return;
  config_file.flush();
  sd_is_dirty = false;
}

///////////////////////////////////////////////////////////
//
// COLORS
//
///////////////////////////////////////////////////////////

// RGB LEDS
// See this table http://pushmod.blogspot.se/p/pad-color-table.html

// RED GREEN LEDS
/*0 - Off (Black)
1 - Red Dim
2 - Red Dim Blink
3 - Red Dim Blink Fast
4 - Red
5 - Red Blink
6 - Red Blink Fast
7 - Orange Dim
8 - Orange Dim Blink
9 - Orange Dim Blink Fast
10 - Orange
11 - Orange Blink
12 - Orange Blink Fast
13 - Yellow (Lime) Dim
14 - Yellow Dim Blink
15 - Yellow Dim Blink Fast
16 - Yellow (Lime)
17 - Yellow Blink
18 - Yellow Blink Fast
19 - Green Dim
20 - Green Dim Blink
21 - Green Dim Blink Fast
22 - Green
23 - Green Blink
24 - Green Blink Fast
25 -> 127 - Green*/






///////////////////////////////////////////////
//
// GLOBAL VARIABLES
//
////////////////////////////////////////////////




// BUG HANDELING
static const byte displayBugDelay = 26;  // probably due to the usb host firmware





///////////////////////////////////////////////
//
// CONNECTED DEVICE DETECTION
//
////////////////////////////////////////////////
// Bounce debouncer = Bounce();
// int devicePin = 2;


//////////////////////////////////////////////////
//
// SYSEX DATA
//
/////////////////////////////////////////////////
byte sysexUserModeMessage[] = {240, 71, 127, 21, 98, 0, 1, 0, 247}; // https://pushmod.blogspot.com/ sagt etwas anderes! Dies soll Live-Mode sein
byte sysexPolyPressure[] =    {240, 71, 127, 21, 92, 0, 1, 0, 247}; // Das wäre der Key aftertouch
byte sysexChannelPressure[] = {240, 71, 127, 21, 92, 0, 1, 1, 247};

// in header file 
// byte displayBuffer[4][68];

// byte sysexDisplayBuffer[77]={240, 71, 127, 21, 24, 0, 69, 0, //the outline for a display sysex message
// 32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,
// 32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,
// 32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,
// 32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,
// 247};

byte padSenseSyssexLow[]= {240, 71, 127, 21, 93, 0, 32, 0, 0, 6, 13, 0, 0, 7, 8, 16, 0, 0, 1, 4, 12, 0, 8, 0, 0, 0, 1, 13, 4, 12, 0, 32, 0, 0, 0, 14, 10, 6, 0, 247}; //sniffed midi message for setting pad sensitivity to -9
// Start Sysex : {240, 71, 127, 
// AbletonID   = 21, 
// Befehl Sensitivity: 93, 0, 32, 
// off_treshold: 0, 0, 6, 13, 
// on_treshold: 0, 0, 7, 8, 
// gain: 16, 0, 0, 1, 4, 12, 0, 8, 
// curve1: 0, 0, 0, 1, 13, 4, 12, 0, 
// curve2: 32, 0, 0, 0, 14, 10, 6, 0, 
// Sysex Ende: 247}; //sniffed midi message for setting pad sensitivity to -9




////////////////////////////////////////////////////////////////////////////////
//
//
// MATH AND NOTE FUNCTIONS
//
//
////////////////////////////////////////////////////////////////////////////////

int Hardpush::loopingNumbers(int number, int lastNumber){
  if(number>0 && number<=lastNumber)
    return number;
  else if(number<1)
    return lastNumber;
  else
    return 1;
}

int Hardpush::indexInLoopingPattern(int anyIndex, int patternSize){ // works like modulo to get out of bound indecies, but this one also takes negative values
  int maxPatternIndex = patternSize-1;
  if (anyIndex >= 0) {
    return anyIndex % patternSize;
  }
  else {
    anyIndex = abs(anyIndex)-1; // clearify calculations with "distance", we already know it's negative
    return maxPatternIndex - (anyIndex % patternSize); //find index relative to root in that new octave
  }
}


int Hardpush::loopingPatternOffset(int index, int patternSize){ //works like whole number division, but also works for negative values
  if (index >= 0) {
    return index / patternSize;
  }
  else {
    return ((index+1) / patternSize) - 1; //always -1 or less, never 0
  }
}

int Hardpush::getRelativeScaleNote(int index) {//returns midi value for a buttonIndex, with respect to scale and octave offset
  return loopingPatternOffset(index,scaleSizes[scalePresetScales[scalePresetIndex]]) * 12 + scales[scalePresetScales[scalePresetIndex]][indexInLoopingPattern(index,scaleSizes[scalePresetScales[scalePresetIndex]])]; 
}

byte Hardpush::pushButtonIndexToScaleIndex(byte index){ //index to absolute scale step, taking the layout into account
  return (index/8) * getYlayout(instrumentPageIndex) + (index%8) * getXlayout(instrumentPageIndex);
}

byte Hardpush::pushButtonIndexToAbsoluteNote(byte index){ //from index 0-63 to notes 0-127 depending on scale,root and layout
  return scalePresetTonics[scalePresetIndex] + pageOctaves[instrumentPageIndex]*12 + getRelativeScaleNote(pushButtonIndexToScaleIndex(index));
}

bool Hardpush::legalRange(byte value){ // Checks if a note request is within 0-127
  return (value>=0 && value<128);
}

void Hardpush::killAllPressedNotesDIN(byte channel){ 
  for(int i=0;i<128;i++){
    if(pressedNotes[i]) {
      // midiDIN.sendNoteOff(i,0,channel);
      usbMIDI.sendNoteOff(i, 0, channel);
      // usbMIDI.sendNoteOff(active[i].note, 0, active[i].channel);
      pressedNotes[i] = false;
    }
  }
}


////////////////////////////////////////////////////////////////////////////////
//
//
// DISPLAY STUFF
//
//
////////////////////////////////////////////////////////////////////////////////


void Hardpush::displayButton(byte index, bool triggered){
  byte buttonState;
  byte note = pushButtonIndexToAbsoluteNote(index);
  if(note<0 || note>127){
    mySendNoteOff(index+36);
    return;
  }
  else if(triggered)
    buttonState = 2; // set button color to triggered
  else if(pushButtonIndexToScaleIndex(index)%scaleSizes[scalePresetScales[scalePresetIndex]]==0)
    buttonState = 1; // set button color to root
  else
    buttonState = 0; // set button color to non-root
  mySendNoteOn(index+36,colors[ pageColors[instrumentPageIndex] ][buttonState]);
}

void Hardpush::initPageSelectButtons(){
  // delay(displayBugDelay);
  for(int i=0;i<numberOfPages;i++){
    mySendControlChange(i+106,colors[pageColors[i]][0]);
  }
}

// Change Scales with button presses not knobs
void Hardpush::initScalePresetSelectButtons(){
  //delay(displayBugDelay);
  for(int i=0;i<numberOfScalePresets;i++){
    if(i==scalePresetIndex) // ausgeählter Preset?
      mySendControlChange(i+102,2);
    else // nicht ausgewählter Preset
      mySendControlChange(i+102,1);
  }
}


void Hardpush::initPushSettings(){
  delay(30);
  // midiUSB.sendSysEx(9,sysexUserModeMessage,true);
  ////midiUSB.sendSysEx(9,sysexChannelPressure,true);
  // midiUSB.sendSysEx(40,padSenseSyssexLow,true);
  delay(30);
  pPushMidi->sendSysEx(9, sysexUserModeMessage, true, PUSH_CABLE);
  delay(30);
  pPushMidi->sendSysEx(40, padSenseSyssexLow, true, PUSH_CABLE);
  delay(30);
}


void Hardpush::initGrid(){
  for(int i=0;i<64;i++){
    displayButton(i,false);
  }
}


void Hardpush::initOtherButtons(){
  for(int i=54;i<56;i++){ //octave buttons
    mySendControlChange(i,4);
  }
  initPageSelectButtons(); // lower buttons
  initScalePresetSelectButtons();
  for(int i=20;i<22;i++){ //top row buttons global settings
    mySendControlChange(i,7);
  }
  for(int i=24;i<28;i++){ //top row buttons page page settings
    mySendControlChange(i,7);
  }
}

void Hardpush::initDisplay(){
  // addDisplayTop();
  // byte separationLines[68]={6,6,6,6,6,6,6,6,32,6,6,6,6,6,6,6,6,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,6,6,6,6,6,6,6,6,32,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,32,6,6,6,6,6,6,6,6};
  // for(int i=0;i<68;i++){
  //   displayBuffer[1][i]=separationLines[i];
  // }

  // addButtonLabelAndData(1, String("Mode"), nameOfScales[scalePresetScales[scalePresetIndex]]);
  // addButtonLabelAndData(2, String("Tonic"), nameOfKeys[scalePresetTonics[scalePresetIndex]]);

  // addButtonLabelAndData(5, String("Channel"), pageChannels[instrumentPageIndex]);
  // addButtonLabelAndData(6, String("Color"), nameOfColors[pageColors[instrumentPageIndex]]);
  // addButtonLabelAndData(7, String("Layout"), nameOfLayouts[pageLayouts[instrumentPageIndex]]);
  // addButtonLabelAndData(8, String("Octave"), pageOctaves[instrumentPageIndex]);
  // showDisplayRow(0);
  // showDisplayRow(1);
  // showDisplayRow(2);
  // showDisplayRow(3);

  if (!pPush) return;
  addDisplayTop();

  static const byte separationLines[68] = {6,6,6,6,6,6,6,6,32,6,6,6,6,6,6,6,6,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,6,6,6,6,6,6,6,6,32,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,32,6,6,6,6,6,6,6,6};
  char sepStr[PUSH_DISPLAY_COLS + 1];
  for (byte i = 0; i < PUSH_DISPLAY_COLS; i++) sepStr[i] = (char)separationLines[i];
  sepStr[PUSH_DISPLAY_COLS] = 0;
  pPush->setText(1, 0, sepStr);

  addButtonLabelAndData(1, String("Mode"),    nameOfScales[scalePresetScales[scalePresetIndex]]);
  addButtonLabelAndData(2, String("Tonic"),   nameOfKeys[scalePresetTonics[scalePresetIndex]]);
  addButtonLabelAndData(5, String("Channel"), pageChannels[instrumentPageIndex]);
  addButtonLabelAndData(6, String("Color"),   nameOfColors[pageColors[instrumentPageIndex]]);
  addButtonLabelAndData(7, String("Layout"),  nameOfLayouts[pageLayouts[instrumentPageIndex]]);
  addButtonLabelAndData(8, String("Octave"),  pageOctaves[instrumentPageIndex]);

  pPush->flushDisplay();  

}

void Hardpush::addToDisplay(String text, byte row, byte section, byte position){
  // byte byteBuffer[text.length()+1]; //create a byte array
  // text.getBytes(byteBuffer,text.length()+1); //fill it with the message
  // for(uint i=0;i<text.length();i++){ //move the text over to the displaybuffer
  //   displayBuffer[row][i+17*(section)+position]=byteBuffer[i];
  // }

  // New - use PushDivice
  if (!pPush) return;
  byte col = 17 * section + position;
  char buf[80];
  text.toCharArray(buf, sizeof(buf));
  pPush->setText(row, col, buf);
}

void Hardpush::addDisplayTop(){
  removeDisplayRow(0);
  addToDisplay(String("Scale preset: ")+(scalePresetIndex+1), 0, 0, 0);
  addToDisplay(String("Track preset: ")+(instrumentPageIndex+1), 0, 2, 0);
}

void Hardpush::addToDisplay8Section(String text, byte row, byte section, bool center){
  // byte index=section-1;
  // for(uint i=0;i<8;i++){ //clear section
  //   displayBuffer[row][i+9*index-index/2]=32;
  // }
  // byte byteBuffer[text.length()+1]; //create a byte array
  // text.getBytes(byteBuffer,text.length()+1); //fill it with the message
  // byte centerAlignText = 0; 
  // if (text.length()<7 && center)
  //   centerAlignText = (4-(text.length()+1)/2);
  // for(uint i=0;i<8 && i<text.length();i++){ //move the text over to the displaybuffer
  //   displayBuffer[row][i+9*index-index/2+centerAlignText]=byteBuffer[i];
  // }


 if (!pPush) return;
  byte index    = section - 1;
  byte startCol = 9 * index - index / 2;

  char sectionBuf[9];
  memset(sectionBuf, ' ', 8);
  sectionBuf[8] = 0;

  byte centerAlignText = 0;
  if (text.length() < 7 && center)
    centerAlignText = (4 - (text.length() + 1) / 2);

  byte copyLen = min((byte)text.length(), (byte)(8 - centerAlignText));
  for (byte i = 0; i < copyLen; i++)
    sectionBuf[i + centerAlignText] = text[i];

  pPush->setText(row, startCol, sectionBuf);}

void Hardpush::addKnobLabelAndData(byte knob, String label, String value){
  addToDisplay8Section(label, 0, knob, true);
  addToDisplay8Section(value, 1, knob, true);
}
void Hardpush::addButtonLabelAndData(byte button, String label, String value){
  addToDisplay8Section(label, 2, button, true);
  addToDisplay8Section(value, 3, button, true);
}


void Hardpush::removeDisplaySection(byte row, byte section){
  // for(int i=0;i<17;i++){
  //   displayBuffer[row][i+17*(section)]=32;
  // }
   if (!pPush) return;
  byte startCol = 17 * section;
  pPush->setText(row, startCol, "                 ");  // 17 Leerzeichen
}

void Hardpush::removeDisplayRow(byte row){
  // for(int i=0;i<68;i++){
  //   displayBuffer[row][i]=32;
  // }
  if (!pPush) return;
  char buf[PUSH_DISPLAY_COLS + 1];
  memset(buf, ' ', PUSH_DISPLAY_COLS);
  buf[PUSH_DISPLAY_COLS] = 0;
  pPush->setText(row, 0, buf);
}

void Hardpush::showDisplayRow(byte row){
  // sysexDisplayBuffer[4] = row + 24; //change to correct row
  // for(int i=0;i<68;i++){ // Fill the sysex with most recent data
  //   sysexDisplayBuffer[i+8] = displayBuffer[row][i];
  // }
  // // midiUSB.sendSysEx(77,sysexDisplayBuffer,true); //send it
  // pPushMidi->sendSysEx(77,sysexDisplayBuffer,true,PUSH_CABLE); //send it
  // delay(displayBugDelay);

  if (!pPush) return;
  (void)row;
  pPush->flushDisplay();
}

////////////////////////////////////////////////////////////////
//
// Homemade USB MIDI functions because the USB host can't cope with the library or something
//
///////////////////////////////////////////////////////////////

void Hardpush::MIDImessage(byte command, byte MIDInote, byte MIDIvelocity) {
  pPushMidi->send(command, MIDInote, MIDIvelocity, PUSH_CABLE); // ist PUSH_CABLE hier richtig???
  //Serial1.write(command);//send note on or note off command 
  //Serial1.write(MIDInote);//send most segnificant byte
  //Serial1.write(MIDIvelocity);//send least segnificant
}
void Hardpush::mySendNoteOn(byte note, byte velocity){
  MIDImessage(144, note, velocity);
}
void Hardpush::mySendNoteOff(byte note){
  MIDImessage(128, note, 0);
}

void Hardpush::mySendControlChange(byte number, byte value){
  MIDImessage(176, number, value);
}

void Hardpush::mySendAfterTouch(byte value){
  Serial2.write(0xD0); //ch1
  Serial2.write(value);
}

void Hardpush::killAllNotesUSB(){ 
  if (pPushMidi->idVendor() != 0) {
    for(int i=0;i<128;i++){
      mySendNoteOff(i);
    }
  }
}

////////////////////////////////////////////////////////////////
//
// LAYOUT GETTERS AND SETTERS
//
///////////////////////////////////////////////////////////////

//LAYOUTS

byte Hardpush::getXlayout(byte pageIndex){
  return layouts[pageLayouts[pageIndex]][0];
}

byte Hardpush::getYlayout(byte pageIndex){
  return layouts[pageLayouts[pageIndex]][1];
}

void Hardpush::setLayout(byte layoutIndex, byte pageIndex){
  pageLayouts[pageIndex] = layoutIndex;
}


///////////////////////////////////////////////////////////////////
//
// CALLBACKS USB MIDI
//
///////////////////////////////////////////////////////////////////


void Hardpush::handleNoteOnUSB(byte channel, byte pitch, byte velocity)
{
  #if PUSH_DEBUG
  noteOnCount++;
  #endif
  //if (PUSH_DEBUG) Serial.println(F("callback: handleNoteOnUSB."));
  if (PUSH_DEBUG) Serial.printf("NoteOn  ch=%u note=%u vel=%u cable=%u\n", channel, pitch, velocity, pPushMidi->getCable());
  if(pitch>35 && pitch<100){
    byte note = pushButtonIndexToAbsoluteNote(pitch-36);
    if(legalRange(note)){
      // midiDIN.sendNoteOn(note,velocity,pageChannels[instrumentPageIndex]);
      int scaled = (int)velocity * 12 +5;
      if (scaled > 126) scaled = 126;
      usbMIDI.sendNoteOn(note, (byte)scaled, pageChannels[instrumentPageIndex]);// channel); //pageChannels[instrumentPageIndex]);
      // usbMIDI.sendNoteOff(active[i].note, 0, active[i].channel);
      pressedNotes[note]=true;
    }
    // if(internalFeedback)
    //   handleNoteOnDIN(pageChannels[instrumentPageIndex], note, velocity);
  }
}

void Hardpush::handleNoteOffUSB(byte channel, byte pitch, byte velocity)
{
  
  #if PUSH_DEBUG
  noteOffCount++;
  #endif 

  //if (PUSH_DEBUG) Serial.println(F("callback: handleNoteOffUSB"));
  if(pitch>35 && pitch<100){
    byte note = pushButtonIndexToAbsoluteNote(pitch-36);
    if(legalRange(note)){
      // midiDIN.sendNoteOff(note,0,pageChannels[instrumentPageIndex]);
      usbMIDI.sendNoteOff(note, 0, pageChannels[instrumentPageIndex]); // channel); // pageChannels[instrumentPageIndex]);
      // usbMIDI.sendNoteOn(note, velocity, pageChannels[instrumentPageIndex]);
      pressedNotes[note]=false;
    }
    // if(internalFeedback)
    //   handleNoteOffDIN(pageChannels[instrumentPageIndex], note, velocity);
  }

}

void Hardpush::handleAfterTouchPolyUSB(byte channel, byte note, byte pressure){
  // if (PUSH_DEBUG) Serial.printf("callback handleAfterTouchPolyUSB val=%u .\n", pressure);
  // midiDIN.sendAfterTouch(pressure, pageChannels[instrumentPageIndex]);
  if (note > 35 && note < 100) {
    byte mapped = pushButtonIndexToAbsoluteNote(note-36);
    if (legalRange(mapped))
      usbMIDI.sendAfterTouchPoly((uint8_t)note, pressure, pageChannels[instrumentPageIndex]);
    // usbMIDI.sendAfterTouchPoly((uint8_t)n, val, noteChannel);
  }
}

void Hardpush::handleAfterTouchChannelUSB(byte channel, byte pressure){
  // if (PUSH_DEBUG) Serial.printf("callback handleAfterTouchChannelUSB val=%u .\n", pressure);
  // midiDIN.sendAfterTouch(pressure, pageChannels[instrumentPageIndex]);
  usbMIDI.sendAfterTouch(pressure, pageChannels[instrumentPageIndex]);
  // usbMIDI.sendAfterTouchPoly((uint8_t)note, pressure, pageChannels[instrumentPageIndex]);
  //mySendAfterTouch(pressure);
}

void Hardpush::handleControlChangeUSB(byte channel, byte control, byte value){
  if (PUSH_DEBUG) Serial.printf("callback für CC. handleControlChangeUSB Chan=%lu CC=%lu, Val=%lu\n", channel, control, value);
  
  //SUSTAIN PEDAL
  if(control==64){
    // midiDIN.sendControlChange(64,value,pageChannels[instrumentPageIndex]);
    usbMIDI.sendControlChange(64, value, pageChannels[instrumentPageIndex]);
    // usbMIDI.sendControlChange(c.ccNum, c.value, c.channel);
  }

  // OCTAVE CHANGE BUTTONS
  if(control==54 || control==55){
    //if (PUSH_DEBUG) Serial.printf("callback CC: Octave change Button\n");
    killAllPressedNotesDIN(pageChannels[instrumentPageIndex]); // avoid hanging notes
    if(control==54 && value==127){
      pageOctaves[instrumentPageIndex]=constrain(pageOctaves[instrumentPageIndex]-1,0,8);
    }
    else if(control==55 && value==127){
      pageOctaves[instrumentPageIndex]=constrain(pageOctaves[instrumentPageIndex]+1,0,8);
    }
    eepromSet(PAGE1_OCTAVE_EEPROM_ADRESS+instrumentPageIndex,pageOctaves[instrumentPageIndex]);
    addButtonLabelAndData(8, String("Octave"), pageOctaves[instrumentPageIndex]);
    showDisplayRow(3);
    initGrid();
  }


  // TEMPO KNOB
  if(control==14){
    killAllPressedNotesDIN(pageChannels[instrumentPageIndex]); // avoid hanging notes
    if(control==14 && value<64){
      scalePresetScales[scalePresetIndex]=indexInLoopingPattern(scalePresetScales[scalePresetIndex]+1,numberOfScales);
    }
    else{
      scalePresetScales[scalePresetIndex]=indexInLoopingPattern(scalePresetScales[scalePresetIndex]-1,numberOfScales);
    }
    eepromSet(PRESET1_SCALE_EEPROM_ADRESS+scalePresetIndex,scalePresetScales[scalePresetIndex]);
    addButtonLabelAndData(1, String("Mode"), nameOfScales[scalePresetScales[scalePresetIndex]]);
    showDisplayRow(3);
    initGrid();
  }
  // // SWING KNOB
  // if(control==15){
  //   killAllNotesDIN(); // avoid hanging notes
  //   if(control==15 && value<64){
  //     scalePresetScales[scalePresetIndex]=constrain(scalePresetScales[scalePresetIndex]+1, 0, numberOfScales-1);
  //   }
  //   else{
  //     scalePresetScales[scalePresetIndex]=constrain(scalePresetScales[scalePresetIndex]-1, 0, numberOfScales-1);
  //   }
  //   addToDisplay8Section(nameOfScales[scalePresetScales[scalePresetIndex]],3,2,false);
  //   showDisplayRow(3);
  //   initGrid();
  // }
  // // ENCODER 1
  // if(control==71){
  //   if(value<64){
  //     colors[pageColors[instrumentPageIndex]][0]=constrain(colors[pageColors[instrumentPageIndex]][0]+1,0,127);
  //   }
  //   else{
  //     colors[pageColors[instrumentPageIndex]][0]=constrain(colors[pageColors[instrumentPageIndex]][0]-1,0,127);
  //   }
  //   addKnobLabelAndData(1, String("Color1"), colors[pageColors[instrumentPageIndex]][0]);
  //   showDisplayRow(0);
  //   showDisplayRow(1);
  //   initGrid();
  // }
  // // ENCODER 2
  //  if(control==72){
  //   if(value<64){
  //     colors[pageColors[instrumentPageIndex]][1]=constrain(colors[pageColors[instrumentPageIndex]][1]+1,0,127);
  //   }
  //   else{
  //     colors[pageColors[instrumentPageIndex]][1]=constrain(colors[pageColors[instrumentPageIndex]][1]-1,0,127);
  //   }
  //   addKnobLabelAndData(2, String("Color2"), colors[pageColors[instrumentPageIndex]][1]);
  //   showDisplayRow(0);
  //   showDisplayRow(1);
  //   initGrid();
  // }
  // // ENCODER 3
  // if(control==73){
  //   if(value<64){
  //     colors[pageColors[instrumentPageIndex]][2]=constrain(colors[pageColors[instrumentPageIndex]][2]+1,0,127);
  //   }
  //   else{
  //     colors[pageColors[instrumentPageIndex]][2]=constrain(colors[pageColors[instrumentPageIndex]][2]-1,0,127);
  //   }
  //   addKnobLabelAndData(3, String("Color3"), colors[pageColors[instrumentPageIndex]][2]);
  //   showDisplayRow(0);
  //   showDisplayRow(1);
  // }
 // ENCODER 4
  // if(control==74){
  //   killAllNotesDIN(); // avoid hanging notes
  //   if(value<64){
  //     pageChannels[instrumentPageIndex]=constrain(pageChannels[instrumentPageIndex]+1,1,16);
  //   }
  //  else{
  //     pageChannels[instrumentPageIndex]=constrain(pageChannels[instrumentPageIndex]-1,1,16);
  //   }
  //   addKnobLabelAndData(4, String("Channel"), pageChannels[instrumentPageIndex]);
  //   showDisplayRow(0);
  //   showDisplayRow(1);
  // }

  //  // ENCODER 5
  // if(control==75){
  //   killAllNotesDIN(); // avoid hanging notes
  //   if(value<64){
  //     pageLayouts[instrumentPageIndex]=constrain(pageLayouts[instrumentPageIndex]+1,0,numberOfLayouts-1);
  //   }
  //  else{
  //     pageLayouts[instrumentPageIndex]=constrain(pageLayouts[instrumentPageIndex]-1,0,numberOfLayouts-1);
  //   }
  //   addKnobLabelAndData(5, String("Layout"), nameOfLayouts[pageLayouts[instrumentPageIndex]]);
  //   showDisplayRow(0);
  //   showDisplayRow(1);
  //   initGrid();
  // }

  // // ENCODER 6
  // if(control==76){
  //   if(value<64){
  //     pageColors[instrumentPageIndex]=constrain(pageColors[instrumentPageIndex]+1,0,numberOfColorSchemes-1);
  //   }
  //  else{
  //     pageColors[instrumentPageIndex]=constrain(pageColors[instrumentPageIndex]-1,0,numberOfColorSchemes-1);
  //   }
  //   addKnobLabelAndData(6, String("Color"), nameOfColors[pageColors[instrumentPageIndex]]);
  //   showDisplayRow(0);
  //   showDisplayRow(1);
  //   initGrid();
  //   initOtherButtons();
  // }

  // Top BUTTON ROW - SCALE SETTINGS
  if(control==20 || control==21){
    killAllPressedNotesDIN(pageChannels[instrumentPageIndex]); // avoid hanging notes
    if(control==20 && value==127){ // change scale
      scalePresetScales[scalePresetIndex]=indexInLoopingPattern(scalePresetScales[scalePresetIndex]+1,numberOfScales);
      eepromSet(PRESET1_SCALE_EEPROM_ADRESS+scalePresetIndex,scalePresetScales[scalePresetIndex]);
      addButtonLabelAndData(1, String("Mode"), nameOfScales[scalePresetScales[scalePresetIndex]]);
      showDisplayRow(3);
      initGrid();
    }
    if(control==21 && value==127){ // Change tonic
      scalePresetTonics[scalePresetIndex]=indexInLoopingPattern(scalePresetTonics[scalePresetIndex]+1,12);
      eepromSet(PRESET1_TONIC_EEPROM_ADRESS+scalePresetIndex,scalePresetTonics[scalePresetIndex]);
      addButtonLabelAndData(2, String("Tonic"), nameOfKeys[scalePresetTonics[scalePresetIndex]]);
      showDisplayRow(3);
    }
  }

   // Top BUTTON ROW - PAGE SETTINGS
  if(control==24 || control==25 || control==26 || control==27){
    if(control==24 && value==127){
      killAllPressedNotesDIN(pageChannels[instrumentPageIndex]); // avoid hanging notes
      pageChannels[instrumentPageIndex]=loopingNumbers(pageChannels[instrumentPageIndex]+1,16);
      eepromSet(PAGE1_CHANNEL_EEPROM_ADRESS+instrumentPageIndex,pageChannels[instrumentPageIndex]);
      addButtonLabelAndData(5, String("Channel"), pageChannels[instrumentPageIndex]);
    }
    
    if(control==25 && value==127){
      pageColors[instrumentPageIndex]=indexInLoopingPattern(pageColors[instrumentPageIndex]+1,numberOfColorSchemes);
      eepromSet(PAGE1_COLOR_EEPROM_ADRESS+instrumentPageIndex,pageColors[instrumentPageIndex]);
      addButtonLabelAndData(6, String("Color"), nameOfColors[pageColors[instrumentPageIndex]]);
      initGrid();
      initPageSelectButtons();
    }

    if(control==26 && value==127){
      pageLayouts[instrumentPageIndex]=indexInLoopingPattern(pageLayouts[instrumentPageIndex]+1,numberOfLayouts);
      eepromSet(PAGE1_LAYOUT_EEPROM_ADRESS+instrumentPageIndex,pageLayouts[instrumentPageIndex]);
      addButtonLabelAndData(7, String("Layout"), nameOfLayouts[pageLayouts[instrumentPageIndex]]);
      initGrid();
    }

    if(control==27 && value==127){
      killAllPressedNotesDIN(pageChannels[instrumentPageIndex]); // avoid hanging notes
      pageOctaves[instrumentPageIndex]=indexInLoopingPattern(pageOctaves[instrumentPageIndex]+1,9);
      eepromSet(PAGE1_OCTAVE_EEPROM_ADRESS+instrumentPageIndex,pageOctaves[instrumentPageIndex]);
      addButtonLabelAndData(8, String("Octave"), pageOctaves[instrumentPageIndex]);
      initGrid();
    }
    showDisplayRow(2);
    showDisplayRow(3);
  }

// LOWER BUTTON ROW SCALE PRESETS
if(control==102 || control==103 || control==104 || control==105){
    killAllPressedNotesDIN(pageChannels[instrumentPageIndex]);
    if(value==127){
      scalePresetIndex=control-102;
      eepromSet(SCALE_PRESET_EEPROM_ADRESS,scalePresetIndex, false);
    }
    addDisplayTop();
    addButtonLabelAndData(1, String("Mode"), nameOfScales[scalePresetScales[scalePresetIndex]]);
    addButtonLabelAndData(2, String("Tonic"), nameOfKeys[scalePresetTonics[scalePresetIndex]]);
    initScalePresetSelectButtons();
    showDisplayRow(0);
    showDisplayRow(3);
    initGrid();
  }


    // LOWER BUTTON ROW Track select
  if(control==106 || control==107 || control==108 || control==109){
    killAllPressedNotesDIN(pageChannels[instrumentPageIndex]);
    if(value==127){
      instrumentPageIndex=control-106;
      eepromSet(PAGE_EEPROM_ADRESS,instrumentPageIndex, false);
    }
    addDisplayTop();
    addButtonLabelAndData(5, String("Channel"), pageChannels[instrumentPageIndex]);
    addButtonLabelAndData(6, String("Color"), nameOfColors[pageColors[instrumentPageIndex]]);
    addButtonLabelAndData(7, String("Layout"), nameOfLayouts[pageLayouts[instrumentPageIndex]]);
    addButtonLabelAndData(8, String("Octave"), pageOctaves[instrumentPageIndex]);
    showDisplayRow(0);
    showDisplayRow(3);
    initGrid();
  }
}

void Hardpush::handlePitchBendUSB(byte channel, int bend){
  // midiDIN.sendPitchBend(bend,pageChannels[instrumentPageIndex]);
  usbMIDI.sendPitchBend(bend, pageChannels[instrumentPageIndex]);
  if (PUSH_DEBUG) Serial.printf("Pitchbend val=%d\n", bend);
}

///////////////////////////////////////////////////////////////////
//
//CALLBACKS DIN MIDI / Computer USB
//
///////////////////////////////////////////////////////////////////

// incouming midi messages light up the pad
void Hardpush::handleNoteOnDIN(byte channel, byte pitch, byte velocity){
  if (channel == pageChannels[instrumentPageIndex]) {
    for(int i=0;i<64;i++){
      if((pitch)==pushButtonIndexToAbsoluteNote(i)){
        displayButton(i,true);
      }
    }
  }
}

void Hardpush::handleNoteOffDIN(byte channel, byte pitch, byte velocity){
  if (channel == pageChannels[instrumentPageIndex]) {
    for(int i=0;i<64;i++){
      if((pitch)==pushButtonIndexToAbsoluteNote(i)){
        displayButton(i,false);
      }
    }
  }
}


///////////////////////////////////////////////////////////////////
//
//SETUP
//
///////////////////////////////////////////////////////////////////


void Hardpush::begin(USBHost& usb, MIDIDevice& midi, bool& ready) {

  pMyusb     = &usb;
  pPushMidi  = &midi;
  pPushReady = &ready;

  instance = this; // ← essentiell, sonst sind alle Callbacks stumm!





  // Serial1.begin(31250);
  // Serial2.begin(31250);
  // Serial1.flush();
  // Serial2.flush();
  Serial.begin(115200);
  if (PUSH_DEBUG) Serial.println(F("Setup Teensy 0."));
  // midiUSB.begin(1); //the push is always sending on channel 1
  // midiDIN.begin(MIDI_CHANNEL_OMNI);  //the midi din ports can send on any channel
  pMyusb->begin();

  // midiUSB.turnThruOff();
  // midiDIN.turnThruOff();


    pPushMidi->setHandleNoteOn           (onNoteOnUSB);
    pPushMidi->setHandleNoteOff          (onNoteOffUSB);
    pPushMidi->setHandleControlChange    (onControlChangeUSB);
    pPushMidi->setHandlePitchChange      (onPitchBendUSB);
    pPushMidi->setHandleAfterTouchChannel(onAfterTouchChannelUSB);
    pPushMidi->setHandleAfterTouchPoly   (onAfterTouchPolyUSB);
    usbMIDI.setHandleNoteOn           (onNoteOnDIN);
    usbMIDI.setHandleNoteOff          (onNoteOffDIN);  


  //attach callbacks for Push 1 on USB
  //midiUSB.setHandleNoteOn(handleNoteOnUSB); // Listen for user input on the push
  //midiUSB.setHandleNoteOff(handleNoteOffUSB);
  //midiUSB.setHandleControlChange(handleControlChangeUSB);
  //midiUSB.setHandlePitchBend(handlePitchBendUSB);
  //pushMidi.setHandleNoteOn(handleNoteOnUSB); // Listen for user input on the push
  //pushMidi.setHandleNoteOff(handleNoteOffUSB);
  //pushMidi.setHandleControlChange(handleControlChangeUSB); 
  //pushMidi.setHandlePitchChange(handlePitchBendUSB);
  //midiUSB.setHandleAfterTouchChannel(handleAfterTouchChannelUSB);
  //midiUSB.setHandleAfterTouchPoly(handleAfterTouchPolyUSB);
  //pushMidi.setHandleAfterTouchChannel(handleAfterTouchChannelUSB);
  //pushMidi.setHandleAfterTouchPoly(handleAfterTouchPolyUSB);
  
  // reverse sending from Computer. lights up the pads.
  // attach callbacks for DIN-Midi or Computer USB on Teensy 4.1
  //midiDIN.setHandleNoteOn(handleNoteOnDIN); // listen for note on/off feedback to visualize on the grid
  //midiDIN.setHandleNoteOff(handleNoteOffDIN);
  //usbMIDI.setHandleNoteOn(handleNoteOnDIN); // listen for note on/off feedback to visualize on the grid
  //usbMIDI.setHandleNoteOff(handleNoteOffDIN);

  //init display buffer
  // for(int row=0;row<4;row++){
  //   for(int col=0;col<68;col++){
  //     displayBuffer[row][col]=32;
  //   }
  // }
  if (pPush) pPush->clearText();

  if (PUSH_DEBUG) Serial.println(F("Setup Teensy 4."));

  // Init pressed notes memory
  for(int i=0;i<128;i++){
    pressedNotes[i] = false;
  }
  if (PUSH_DEBUG) Serial.println(F("Setup Teensy 5."));

  ///////// SD ////////////
  if (!SD.begin(chipSelect)) {
    Serial.println("SD-Card: initialization failed!");
    return;
  }
  if (PUSH_DEBUG) Serial.println("SD-Card: initialization done.");

  // Datei einmalig zum Lesen UND Schreiben öffnen (ohne O_APPEND )
  config_file = SD.open(filename, FILE_WRITE_BEGIN); 
  if (!config_file) {
    Serial.println("Datei konnte nicht geöffnet werden!");
    return;
  } else {
    sd_ready = true;
  }

  //Load EEPROM SETTINGS
  instrumentPageIndex = eepromGet(PAGE_EEPROM_ADRESS,0,numberOfPages-1,0);
  scalePresetIndex = eepromGet(SCALE_PRESET_EEPROM_ADRESS,0,numberOfScalePresets-1,0);
  for(int i=0;i<numberOfPages;i++){
    pageChannels[i]=eepromGet(PAGE1_CHANNEL_EEPROM_ADRESS+i,1,16,i+1);
    pageColors[i]=eepromGet(PAGE1_COLOR_EEPROM_ADRESS+i,0,numberOfColorSchemes-1,i);
    pageLayouts[i]=eepromGet(PAGE1_LAYOUT_EEPROM_ADRESS+i,0,numberOfLayouts-1,0);
    pageOctaves[i]=eepromGet(PAGE1_OCTAVE_EEPROM_ADRESS+i,0,8,3);
  }
  if (PUSH_DEBUG) Serial.println(F("Setup Teensy 6."));
  for(int i=0;i<numberOfScalePresets;i++){
    scalePresetScales[i]= eepromGet(PRESET1_SCALE_EEPROM_ADRESS+i,0,numberOfScales-1,1);
    scalePresetTonics[i]=eepromGet(PRESET1_TONIC_EEPROM_ADRESS+i,0,11,0);
  }
  if (PUSH_DEBUG) Serial.println(F("Setup Teensy 7."));
  //I don't understand why I need this shit, but without it I get some horrible note action
  killAllNotesUSB();
  if (PUSH_DEBUG) Serial.println(F("Setup Teensy 8."));
  //make connected devices hot-swappable 
  //pinMode(devicePin, INPUT_PULLUP);
  // debouncer.attach(devicePin);  
  // debouncer.interval(50);

  // //initial check for connected device
  // bool connected = (pushMidi.idVendor() != 0);
  // if (connected && !pushReady) {
  // // if ( debouncer.read() ){
  //   initPushSettings();
  //   initGrid();
  //   initOtherButtons();
  //   initDisplay();
  // }



}

///////////////////////////////////////////////////////////////////
//
//LOOP
//
///////////////////////////////////////////////////////////////////

void Hardpush::loop() {
  // Serial.println(F("Living."));
  pMyusb->Task();
  // while (pushMidi.read(PUSH_CABLE)) { }   // dispatch all queued Push events to the callbacks
  // zazze übergibt keinen Parameter
  while (pPushMidi->read()) {};
  while (usbMIDI.read())  { };   // drain the iPad side

  // midiUSB.read();
  // midiDIN.read();
  // debouncer.update();
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint > 2000) {
    lastPrint = millis();
    //Serial.printf("NoteOn=%lu  NoteOff=%lu\n", noteOnCount, noteOffCount);
}
  
  // if ( debouncer.rose() ){ //If a device was just connected, initialize it
  bool connected = (pPushMidi->idVendor() != 0);
  if (connected && !(*pPushReady)) {
    if (PUSH_DEBUG) Serial.println(F("Push connected."));
    initPushSettings();
    initGrid();
    initOtherButtons();
    initDisplay();
    *pPushReady = true;

  } else if (!connected && *pPushReady) {
    *pPushReady = false;
    if (PUSH_DEBUG) Serial.println(F("Push disconnected."));
  }

  if (sd_ready && sd_is_dirty && SD.sdfs.card() && !SD.sdfs.card()->isBusy()) { // && SD.sdfs.card() prüft ob, sd card ready to read
    // Die SD-Karte ist bereit – jetzt kann geschrieben werden
    config_file.flush();
    sd_is_dirty = false;
    if (PUSH_DEBUG) Serial.println("SD-Card: flush() augerufen.");
  }

}

