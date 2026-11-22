#ifndef STRUM_MODE_H
#define STRUM_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "midi_utils.h"

// STRUM: vertical "strings" tuned to a scale. Drag a finger across them to pluck;
// the speed of the swipe sets the velocity.
#define STRUM_STRINGS 11
#define STRUM_SPACING 28
#define STRUM_FIRST_X 20
#define STRUM_TOP 52
#define STRUM_BOTTOM 192

int strumScale = 0;
int strumKey = 0;      // C=0
int strumOctave = 4;
int strumRing = 1;     // 0=short, 1=medium, 2=long
const unsigned long STRUM_RING_MS[3] = {150, 450, 1200};
const char *STRUM_RING_NAMES[3] = {"SHORT", "MED", "LONG"};

int strumNote[STRUM_STRINGS];
unsigned long strumOffTime[STRUM_STRINGS];   // 0 = not sounding
unsigned long strumFlashUntil[STRUM_STRINGS]; // 0 = drawn normally
int strumPrevX = -1;
unsigned long strumPrevT = 0;

void initializeStrumMode();
void drawStrumMode();
void handleStrumMode();
void drawStrumString(int i, bool lit);
void calculateStrumNotes();
void pluckString(int i, int velocity);
void stopStrumNotes();

void initializeStrumMode() {
  strumScale = 0;
  strumKey = 0;
  strumOctave = 4;
  strumRing = 1;
  strumPrevX = -1;
  for (int i = 0; i < STRUM_STRINGS; i++) {
    strumOffTime[i] = 0;
    strumFlashUntil[i] = 0;
  }
  calculateStrumNotes();
}

void calculateStrumNotes() {
  for (int i = 0; i < STRUM_STRINGS; i++) {
    strumNote[i] = constrain(getNoteInScale(strumScale, i, strumOctave) + strumKey, 0, 127);
  }
}

void drawStrumString(int i, bool lit) {
  int x = STRUM_FIRST_X + i * STRUM_SPACING;
  bool isRoot = ((strumNote[i] - strumKey) % 12 == 0);
  uint16_t color = lit ? THEME_PRIMARY : (isRoot ? THEME_ACCENT : 0x8410);
  tft.fillRect(x - 3, STRUM_TOP, 7, STRUM_BOTTOM - STRUM_TOP, THEME_BG);
  if (lit) {
    tft.fillRect(x - 1, STRUM_TOP, 3, STRUM_BOTTOM - STRUM_TOP, color);
  } else {
    tft.drawFastVLine(x, STRUM_TOP, STRUM_BOTTOM - STRUM_TOP, color);
  }
  // Note name along the bottom of each string
  tft.fillRect(x - 13, STRUM_BOTTOM + 1, 27, 9, THEME_BG);
  tft.setTextColor(isRoot ? THEME_ACCENT : THEME_TEXT_DIM, THEME_BG);
  String name = getNoteNameFromMIDI(strumNote[i]);
  name.remove(name.length() - 1); // drop octave digit to save space
  tft.drawCentreString(name, x, STRUM_BOTTOM + 2, 1);
}

void drawStrumMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("STRUM", scales[strumScale].name + " in " + getNoteNameFromMIDI(strumKey));
  for (int i = 0; i < STRUM_STRINGS; i++) drawStrumString(i, false);

  drawRoundButton(5, 207, 55, 25, "SCALE", THEME_ACCENT);
  drawRoundButton(65, 207, 40, 25, "KEY-", THEME_WARNING);
  drawRoundButton(110, 207, 40, 25, "KEY+", THEME_WARNING);
  drawRoundButton(155, 207, 40, 25, "OCT-", THEME_SECONDARY);
  drawRoundButton(200, 207, 40, 25, "OCT+", THEME_SECONDARY);
  drawRoundButton(245, 207, 70, 25, STRUM_RING_NAMES[strumRing], THEME_SUCCESS);
}

void pluckString(int i, int velocity) {
  if (strumOffTime[i] != 0) sendMIDI(0x80, strumNote[i], 0); // retrigger cleanly
  sendMIDI(0x90, strumNote[i], constrain(velocity, 1, 127));
  unsigned long now = millis();
  strumOffTime[i] = now + STRUM_RING_MS[strumRing];
  strumFlashUntil[i] = now + 120;
  drawStrumString(i, true);
}

void stopStrumNotes() {
  for (int i = 0; i < STRUM_STRINGS; i++) {
    if (strumOffTime[i] != 0) {
      sendMIDI(0x80, strumNote[i], 0);
      strumOffTime[i] = 0;
    }
    strumFlashUntil[i] = 0;
  }
  strumPrevX = -1;
}

void handleStrumMode() {
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    stopStrumNotes();
    exitToMenu();
    return;
  }

  if (touch.justPressed) {
    bool changed = true;
    if (isButtonPressed(5, 207, 55, 25)) strumScale = (strumScale + 1) % NUM_SCALES;
    else if (isButtonPressed(65, 207, 40, 25)) strumKey = (strumKey + 11) % 12;
    else if (isButtonPressed(110, 207, 40, 25)) strumKey = (strumKey + 1) % 12;
    else if (isButtonPressed(155, 207, 40, 25)) strumOctave = max(1, strumOctave - 1);
    else if (isButtonPressed(200, 207, 40, 25)) strumOctave = min(7, strumOctave + 1);
    else if (isButtonPressed(245, 207, 70, 25)) strumRing = (strumRing + 1) % 3;
    else changed = false;
    if (changed) {
      stopStrumNotes();
      calculateStrumNotes();
      drawStrumMode();
      return;
    }
  }

  unsigned long now = millis();

  // Pluck any string the finger crossed since the last update
  bool inArea = touch.isPressed && touch.y >= STRUM_TOP && touch.y <= STRUM_BOTTOM + 10;
  if (inArea) {
    if (strumPrevX >= 0 && touch.x != strumPrevX) {
      unsigned long dt = max(1UL, now - strumPrevT);
      float speed = fabs((float)(touch.x - strumPrevX)) / dt; // pixels per ms
      int velocity = constrain((int)(30 + speed * 40), 30, 127);
      for (int i = 0; i < STRUM_STRINGS; i++) {
        int sx = STRUM_FIRST_X + i * STRUM_SPACING;
        bool crossed = (strumPrevX < sx && touch.x >= sx) || (strumPrevX > sx && touch.x <= sx);
        if (crossed) pluckString(i, velocity);
      }
    }
    strumPrevX = touch.x;
    strumPrevT = now;
  } else {
    strumPrevX = -1;
  }

  // End of ring and end of the string's flash
  for (int i = 0; i < STRUM_STRINGS; i++) {
    if (strumFlashUntil[i] != 0 && now >= strumFlashUntil[i]) {
      strumFlashUntil[i] = 0;
      drawStrumString(i, false);
    }
    if (strumOffTime[i] != 0 && now >= strumOffTime[i]) {
      sendMIDI(0x80, strumNote[i], 0);
      strumOffTime[i] = 0;
    }
  }
}

#endif
