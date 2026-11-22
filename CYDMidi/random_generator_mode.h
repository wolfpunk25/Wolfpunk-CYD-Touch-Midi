#ifndef RANDOM_GENERATOR_MODE_H
#define RANDOM_GENERATOR_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "midi_utils.h"

// Random Generator mode variables
struct RandomGen {
  int rootNote = 60; // C4
  int scaleType = 0; // Major
  int minOctave = 3;
  int maxOctave = 6;
  int probability = 50; // 0-100%
  int bpm = 120; // BPM instead of interval
  int subdivision = 4; // 4=quarter, 8=eighth, 16=sixteenth
  bool isPlaying = false;
  unsigned long lastNoteTime = 0;
  unsigned long nextNoteTime = 0;
  int currentNote = -1;
  unsigned long noteInterval = 500; // Calculated from BPM
  bool walkMode = false;            // false = jump anywhere, true = step to neighbouring scale notes
  int gate = 80;                    // note length as % of the step (10-100)
  int walkPos = 0;                  // current position in scale steps (octave * notes + degree)
  unsigned long noteOffTime = 0;
};

RandomGen randomGen;

// Function declarations
void initializeRandomGeneratorMode();
void drawRandomGeneratorMode();
void handleRandomGeneratorMode();
void drawRandomGenControls();
void drawRandomNowLine();
void stopRandomNote();
void resetWalkPos();
void updateRandomGenerator();
void playRandomNote();
void calculateNoteInterval();

// Implementations
void initializeRandomGeneratorMode() {
  randomGen.rootNote = 60;
  randomGen.scaleType = 0;
  randomGen.minOctave = 3;
  randomGen.maxOctave = 6;
  randomGen.probability = 50;
  randomGen.bpm = 120;
  randomGen.subdivision = 4;
  randomGen.isPlaying = false;
  randomGen.currentNote = -1;
  randomGen.walkMode = false;
  randomGen.gate = 80;
  calculateNoteInterval();
  resetWalkPos();
  randomGen.nextNoteTime = millis() + randomGen.noteInterval;
}

void drawRandomGeneratorMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("RANDOM", "Random Music");

  drawRandomGenControls();
}

void drawRandomGenControls() {
  // Clear old values so shorter text (100 -> 95) leaves no stray digits
  tft.fillRect(0, 50, 320, 145, THEME_BG);

  int y = 55;
  int spacing = 22;

  // Play/Stop and Root note on same line
  drawRoundButton(10, y, 60, 25, randomGen.isPlaying ? "STOP" : "PLAY",
                 randomGen.isPlaying ? THEME_ERROR : THEME_SUCCESS);

  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString("Key:", 80, y + 6, 1);
  String rootName = getNoteNameFromMIDI(randomGen.rootNote);
  drawRoundButton(110, y, 35, 25, rootName, THEME_PRIMARY);
  drawRoundButton(150, y, 25, 25, "+", THEME_SECONDARY);
  drawRoundButton(180, y, 25, 25, "-", THEME_SECONDARY);

  // Scale selector
  drawRoundButton(220, y, 80, 25, scales[randomGen.scaleType].name, THEME_ACCENT);

  y += spacing + 5;

  // Octave range
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString("Oct:", 10, y + 6, 1);
  tft.drawString(String(randomGen.minOctave) + "-" + String(randomGen.maxOctave), 35, y + 6, 1);
  drawRoundButton(70, y, 35, 25, "MIN-", THEME_SECONDARY);
  drawRoundButton(110, y, 35, 25, "MIN+", THEME_SECONDARY);
  drawRoundButton(150, y, 35, 25, "MAX-", THEME_SECONDARY);
  drawRoundButton(190, y, 35, 25, "MAX+", THEME_SECONDARY);

  y += spacing + 5;

  // Probability with visual bar
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString("Chance:", 10, y + 6, 1);
  tft.drawString(String(randomGen.probability) + "%", 55, y + 6, 1);
  drawRoundButton(85, y, 25, 25, "-", THEME_SECONDARY);
  drawRoundButton(115, y, 25, 25, "+", THEME_SECONDARY);

  int barW = 80;
  int barX = 145;
  tft.drawRect(barX, y + 8, barW, 10, THEME_TEXT_DIM);
  int fillW = (barW * randomGen.probability) / 100;
  if (fillW > 0) {
    tft.fillRect(barX + 1, y + 9, fillW, 8, THEME_PRIMARY);
  }

  y += spacing + 5;

  // BPM and subdivision controls
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString("BPM:", 10, y + 6, 1);
  tft.drawString(String(randomGen.bpm), 40, y + 6, 1);
  drawRoundButton(65, y, 25, 25, "-", THEME_SECONDARY);
  drawRoundButton(95, y, 25, 25, "+", THEME_SECONDARY);

  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString("Beat:", 125, y + 6, 1);
  String subdivText;
  if (randomGen.subdivision == 4) subdivText = "1/4";
  else if (randomGen.subdivision == 8) subdivText = "1/8";
  else if (randomGen.subdivision == 16) subdivText = "1/16";
  tft.drawString(subdivText, 160, y + 6, 1);
  drawRoundButton(190, y, 25, 25, "<", THEME_SECONDARY);
  drawRoundButton(220, y, 25, 25, ">", THEME_SECONDARY);

  y += spacing + 5;

  // Mode and gate length
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString("Mode:", 10, y + 6, 1);
  drawRoundButton(50, y, 55, 25, randomGen.walkMode ? "WALK" : "RANDOM", THEME_ACCENT);
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString("Gate:", 120, y + 6, 1);
  tft.drawString(String(randomGen.gate) + "%", 155, y + 6, 1);
  drawRoundButton(190, y, 25, 25, "-", THEME_SECONDARY);
  drawRoundButton(220, y, 25, 25, "+", THEME_SECONDARY);

  drawRandomNowLine();
}

// Only this small area changes when a note plays or stops
void drawRandomNowLine() {
  tft.fillRect(0, 198, 320, 28, THEME_BG);
  if (randomGen.currentNote != -1) {
    tft.setTextColor(THEME_PRIMARY, THEME_BG);
    tft.drawString("Now: ", 10, 204, 1);
    tft.setTextColor(THEME_ACCENT, THEME_BG);
    tft.drawString(getNoteNameFromMIDI(randomGen.currentNote), 45, 200, 2);
  }
}

void stopRandomNote() {
  if (randomGen.currentNote != -1) {
    sendMIDI(0x80, randomGen.currentNote, 0);
    randomGen.currentNote = -1;
    drawRandomNowLine();
  }
}

void resetWalkPos() {
  Scale& scale = scales[randomGen.scaleType];
  int lowPos = randomGen.minOctave * scale.numNotes;
  int highPos = (randomGen.maxOctave + 1) * scale.numNotes - 1;
  randomGen.walkPos = (lowPos + highPos) / 2;
}

void handleRandomGeneratorMode() {
  // Back button
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    stopRandomNote();
    randomGen.isPlaying = false;
    exitToMenu();
    return;
  }

  if (touch.justPressed) {
    int y = 55;
    int spacing = 22;

    // Play/Stop and Root note controls
    if (isButtonPressed(10, y, 60, 25)) {
      randomGen.isPlaying = !randomGen.isPlaying;
      if (randomGen.isPlaying) {
        randomGen.nextNoteTime = millis() + randomGen.noteInterval;
        resetWalkPos();
      } else {
        stopRandomNote();
      }
      drawRandomGenControls();
      return;
    }

    if (isButtonPressed(150, y, 25, 25)) {
      randomGen.rootNote = min(127, randomGen.rootNote + 1);
      drawRandomGenControls();
      return;
    }
    if (isButtonPressed(180, y, 25, 25)) {
      randomGen.rootNote = max(0, randomGen.rootNote - 1);
      drawRandomGenControls();
      return;
    }

    // Scale selector
    if (isButtonPressed(220, y, 80, 25)) {
      randomGen.scaleType = (randomGen.scaleType + 1) % NUM_SCALES;
      resetWalkPos();
      drawRandomGenControls();
      return;
    }

    y += spacing + 5;

    // Octave controls
    if (isButtonPressed(70, y, 35, 25)) {
      randomGen.minOctave = max(1, randomGen.minOctave - 1);
      if (randomGen.minOctave >= randomGen.maxOctave) {
        randomGen.maxOctave = randomGen.minOctave + 1;
      }
      resetWalkPos();
      drawRandomGenControls();
      return;
    }
    if (isButtonPressed(110, y, 35, 25)) {
      randomGen.minOctave = min(8, randomGen.minOctave + 1);
      if (randomGen.minOctave >= randomGen.maxOctave) {
        randomGen.maxOctave = randomGen.minOctave + 1;
      }
      resetWalkPos();
      drawRandomGenControls();
      return;
    }
    if (isButtonPressed(150, y, 35, 25)) {
      randomGen.maxOctave = max(randomGen.minOctave + 1, randomGen.maxOctave - 1);
      resetWalkPos();
      drawRandomGenControls();
      return;
    }
    if (isButtonPressed(190, y, 35, 25)) {
      randomGen.maxOctave = min(9, randomGen.maxOctave + 1);
      resetWalkPos();
      drawRandomGenControls();
      return;
    }

    y += spacing + 5;

    // Probability controls
    if (isButtonPressed(85, y, 25, 25)) {
      randomGen.probability = max(0, randomGen.probability - 5);
      drawRandomGenControls();
      return;
    }
    if (isButtonPressed(115, y, 25, 25)) {
      randomGen.probability = min(100, randomGen.probability + 5);
      drawRandomGenControls();
      return;
    }

    y += spacing + 5;

    // BPM controls
    if (isButtonPressed(65, y, 25, 25)) {
      randomGen.bpm = max(60, randomGen.bpm - 5);
      calculateNoteInterval();
      drawRandomGenControls();
      return;
    }
    if (isButtonPressed(95, y, 25, 25)) {
      randomGen.bpm = min(200, randomGen.bpm + 5);
      calculateNoteInterval();
      drawRandomGenControls();
      return;
    }

    // Subdivision controls
    if (isButtonPressed(190, y, 25, 25)) {
      if (randomGen.subdivision == 16) randomGen.subdivision = 8;
      else if (randomGen.subdivision == 8) randomGen.subdivision = 4;
      calculateNoteInterval();
      drawRandomGenControls();
      return;
    }
    if (isButtonPressed(220, y, 25, 25)) {
      if (randomGen.subdivision == 4) randomGen.subdivision = 8;
      else if (randomGen.subdivision == 8) randomGen.subdivision = 16;
      calculateNoteInterval();
      drawRandomGenControls();
      return;
    }

    y += spacing + 5;

    // Mode and gate controls
    if (isButtonPressed(50, y, 55, 25)) {
      randomGen.walkMode = !randomGen.walkMode;
      resetWalkPos();
      drawRandomGenControls();
      return;
    }
    if (isButtonPressed(190, y, 25, 25)) {
      randomGen.gate = max(10, randomGen.gate - 10);
      drawRandomGenControls();
      return;
    }
    if (isButtonPressed(220, y, 25, 25)) {
      randomGen.gate = min(100, randomGen.gate + 10);
      drawRandomGenControls();
      return;
    }
  }

  // Update random generator
  updateRandomGenerator();
}

void updateRandomGenerator() {
  if (!randomGen.isPlaying) return;

  unsigned long now = millis();

  // Wait quietly while Bluetooth is down, then restart on the beat instead of bursting
  if (!deviceConnected) {
    stopRandomNote();
    randomGen.nextNoteTime = now + randomGen.noteInterval;
    return;
  }

  // End the note once its gate time has passed
  if (randomGen.currentNote != -1 && now >= randomGen.noteOffTime) {
    stopRandomNote();
  }

  if (now >= randomGen.nextNoteTime) {
    // Keep a fixed grid: add to the scheduled time rather than "now" so redraws cannot make the tempo drift
    randomGen.nextNoteTime += randomGen.noteInterval;
    if (now > randomGen.nextNoteTime) randomGen.nextNoteTime = now + randomGen.noteInterval; // fell far behind: resync
    playRandomNote();
  }
}

void playRandomNote() {
  stopRandomNote();

  if (random(100) >= randomGen.probability) return;

  Scale& scale = scales[randomGen.scaleType];
  int n = scale.numNotes;
  int octave, degree;

  if (randomGen.walkMode) {
    // Step up or down by up to two scale degrees, bouncing off the ends of the octave range
    int lowPos = randomGen.minOctave * n;
    int highPos = (randomGen.maxOctave + 1) * n - 1;
    int step = random(-2, 3);
    int pos = randomGen.walkPos + step;
    if (pos < lowPos) pos = lowPos + (lowPos - pos);
    if (pos > highPos) pos = highPos - (pos - highPos);
    pos = constrain(pos, lowPos, highPos);
    randomGen.walkPos = pos;
    octave = pos / n;
    degree = pos % n;
  } else {
    degree = random(n);
    octave = random(randomGen.minOctave, randomGen.maxOctave + 1);
  }

  int note = randomGen.rootNote % 12 + scale.intervals[degree] + (octave * 12);
  if (note < 0 || note > 127) return;

  sendMIDI(0x90, note, 100);
  randomGen.currentNote = note;
  randomGen.noteOffTime = millis() + (randomGen.noteInterval * randomGen.gate) / 100;
  drawRandomNowLine();
}

void calculateNoteInterval() {
  // Calculate note interval from BPM and subdivision
  float beatsPerSecond = randomGen.bpm / 60.0;
  float notesPerSecond = beatsPerSecond * (randomGen.subdivision / 4.0);
  randomGen.noteInterval = 1000.0 / notesPerSecond;
}

#endif
