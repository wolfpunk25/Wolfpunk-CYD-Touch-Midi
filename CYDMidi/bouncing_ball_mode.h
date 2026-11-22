#ifndef BOUNCING_BALL_MODE_H
#define BOUNCING_BALL_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "midi_utils.h"

// Pong-style Ambient MIDI mode variables
struct Ball {
  float x, y;
  float vx, vy;
  uint16_t color;
  int size;
  bool active;
  float energy;   // 1.0 when dropped, fades towards 0: scales velocity, brightness and size
};

#define MAX_BALLS 16
Ball balls[MAX_BALLS];
int numActiveBalls = 1;   // balls currently alive

// Decay: how quickly a ball's energy fades (per second); a ball that fades out is removed
const char *DECAY_NAMES[] = {"OFF", "LONG", "MED", "FAST"};
const float DECAY_RATES[] = {0.0f, 0.03f, 0.08f, 0.22f};
const int NUM_DECAYS = 4;
int ballDecay = 2;                 // default: MED (~12 s)
const float BALL_MIN_ENERGY = 0.08f;
const float BALL_HIT_LOSS = 0.97f; // each wall hit costs a little energy

static uint16_t dimColor(uint16_t c, float k) {
  int r = ((c >> 11) & 0x1F) * k, g = ((c >> 5) & 0x3F) * k, b = (c & 0x1F) * k;
  return (r << 11) | (g << 5) | b;
}

// Simple wall system - notes triggered by wall hits
struct Wall {
  int x, y, w, h;
  int note;
  String noteName;
  uint16_t color;
  bool active;
  unsigned long activeTime;
  int side; // 0=top, 1=right, 2=bottom, 3=left
};

#define NUM_WALLS 24  // 8 top + 8 bottom + 4 left + 4 right
Wall walls[NUM_WALLS];
int ballScale = 0;  // Scale selection
int ballKey = 0;    // Key selection
int ballOctave = 4;

// Function declarations
void initializeBouncingBallMode();
void drawBouncingBallMode();
void handleBouncingBallMode();
void initializeBalls();
void spawnBall(float x, float y, bool useTouchVelocity);
void initializeWalls();
void updateBouncingBall();
void updateBalls();
void drawBalls();
void drawWalls();
void checkWallCollisions();

// Implementations
void initializeBouncingBallMode() {
  ballScale = 0;
  ballKey = 0;
  ballOctave = 4;
  numActiveBalls = 0;
  ballDecay = 2;
  initializeBalls();
  initializeWalls();
  spawnBall(random(80, 240), random(80, 150), false);
}

void drawBouncingBallMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("ZEN", "Ambient Bouncing");
  
  // Controls
  drawRoundButton(5, 200, 40, 25, "ADD", THEME_SUCCESS);
  drawRoundButton(50, 200, 40, 25, "CLR", THEME_WARNING);
  drawRoundButton(95, 200, 40, 25, "SCL", THEME_ACCENT);
  drawRoundButton(140, 200, 40, 25, "KEY-", THEME_SECONDARY);
  drawRoundButton(185, 200, 40, 25, "KEY+", THEME_SECONDARY);
  drawRoundButton(230, 200, 40, 25, "OCT", THEME_PRIMARY);
  drawRoundButton(275, 200, 40, 25, "DEC", THEME_ERROR);
  
  // Status display
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  String keyName = getNoteNameFromMIDI(ballKey);
  tft.drawString(keyName + " " + scales[ballScale].name, 10, 180, 1);
  tft.drawString("Oct:" + String(ballOctave), 130, 180, 1);
  tft.drawString("Decay:" + String(DECAY_NAMES[ballDecay]), 185, 180, 1);
  tft.drawString("Balls:" + String(numActiveBalls) + "/" + String(MAX_BALLS) + " ", 255, 180, 1);
  
  drawWalls();
  drawBalls();
}

void initializeBalls() {
  for (int i = 0; i < MAX_BALLS; i++) balls[i].active = false;
  numActiveBalls = 0;
}

// Drop a new ball into a free slot; existing balls are left alone
void spawnBall(float x, float y, bool useTouchVelocity) {
  for (int i = 0; i < MAX_BALLS; i++) {
    if (balls[i].active) continue;
    balls[i].x = constrain(x, 60, 265);
    balls[i].y = constrain(y, 70, 170);
    balls[i].vx = random(-15, 15) / 10.0; // -1.5 to 1.5
    balls[i].vy = random(-15, 15) / 10.0;
    if (abs(balls[i].vx) < 0.5) balls[i].vx = (balls[i].vx >= 0) ? 0.8 : -0.8;
    if (abs(balls[i].vy) < 0.5) balls[i].vy = (balls[i].vy >= 0) ? 0.8 : -0.8;
    if (useTouchVelocity) balls[i].vy = fabs(balls[i].vy);   // dropped balls fall first
    balls[i].color = random(0x2000, 0x8FFF) | 0x0821;
    balls[i].size = random(4, 7);
    balls[i].energy = 1.0f;
    balls[i].active = true;
    numActiveBalls++;
    return;
  }
}

void initializeWalls() {
  int wallIndex = 0;
  
  // Top wall - 8 segments
  for (int i = 0; i < 8; i++) {
    walls[wallIndex].x = 50 + i * 28;
    walls[wallIndex].y = 60;
    walls[wallIndex].w = 28;
    walls[wallIndex].h = 3;
    walls[wallIndex].note = getNoteInScale(ballScale, i, ballOctave) + ballKey;
    walls[wallIndex].noteName = getNoteNameFromMIDI(walls[wallIndex].note);
    walls[wallIndex].color = THEME_PRIMARY;
    walls[wallIndex].active = false;
    walls[wallIndex].side = 0;
    wallIndex++;
  }
  
  // Right wall - 4 segments
  for (int i = 0; i < 4; i++) {
    walls[wallIndex].x = 272;
    walls[wallIndex].y = 63 + i * 28;
    walls[wallIndex].w = 3;
    walls[wallIndex].h = 28;
    walls[wallIndex].note = getNoteInScale(ballScale, i, ballOctave + 1) + ballKey;
    walls[wallIndex].noteName = getNoteNameFromMIDI(walls[wallIndex].note);
    walls[wallIndex].color = THEME_SECONDARY;
    walls[wallIndex].active = false;
    walls[wallIndex].side = 1;
    wallIndex++;
  }
  
  // Bottom wall - 8 segments
  for (int i = 0; i < 8; i++) {
    walls[wallIndex].x = 50 + i * 28;
    walls[wallIndex].y = 177;
    walls[wallIndex].w = 28;
    walls[wallIndex].h = 3;
    walls[wallIndex].note = getNoteInScale(ballScale, 7 - i, ballOctave) + ballKey;
    walls[wallIndex].noteName = getNoteNameFromMIDI(walls[wallIndex].note);
    walls[wallIndex].color = THEME_ACCENT;
    walls[wallIndex].active = false;
    walls[wallIndex].side = 2;
    wallIndex++;
  }
  
  // Left wall - 4 segments
  for (int i = 0; i < 4; i++) {
    walls[wallIndex].x = 50;
    walls[wallIndex].y = 63 + i * 28;
    walls[wallIndex].w = 3;
    walls[wallIndex].h = 28;
    walls[wallIndex].note = getNoteInScale(ballScale, 3 - i, ballOctave + 1) + ballKey;
    walls[wallIndex].noteName = getNoteNameFromMIDI(walls[wallIndex].note);
    walls[wallIndex].color = THEME_WARNING;
    walls[wallIndex].active = false;
    walls[wallIndex].side = 3;
    wallIndex++;
  }
}

void handleBouncingBallMode() {
  // Back button
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    exitToMenu();
    return;
  }
  
  if (touch.justPressed) {
    // Tap the play area to drop a ball right there
    if (touch.x > 56 && touch.x < 268 && touch.y > 66 && touch.y < 174) {
      spawnBall(touch.x, touch.y, true);
      drawBouncingBallMode();
      return;
    }

    // Add ball button
    if (isButtonPressed(5, 200, 40, 25)) {
      spawnBall(random(80, 240), random(80, 150), false);
      drawBouncingBallMode();
      return;
    }
    
    // Clear all balls
    if (isButtonPressed(50, 200, 40, 25)) {
      initializeBalls();
      drawBouncingBallMode();
      return;
    }
    
    // Scale button
    if (isButtonPressed(95, 200, 40, 25)) {
      ballScale = (ballScale + 1) % NUM_SCALES;
      initializeWalls();
      drawBouncingBallMode();
      return;
    }
    
    // Key controls
    if (isButtonPressed(140, 200, 40, 25)) {
      ballKey = (ballKey - 1 + 12) % 12;
      initializeWalls();
      drawBouncingBallMode();
      return;
    }
    
    if (isButtonPressed(185, 200, 40, 25)) {
      ballKey = (ballKey + 1) % 12;
      initializeWalls();
      drawBouncingBallMode();
      return;
    }
    
    // Octave button
    if (isButtonPressed(230, 200, 40, 25)) {
      ballOctave = (ballOctave == 7) ? 2 : ballOctave + 1;
      initializeWalls();
      drawBouncingBallMode();
      return;
    }

    // Decay control
    if (isButtonPressed(275, 200, 40, 25)) {
      ballDecay = (ballDecay + 1) % NUM_DECAYS;
      drawBouncingBallMode();
      return;
    }
  }
  
  // Update physics and display
  updateBouncingBall();
}

void updateBouncingBall() {
  // Smooth 60 FPS animation
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 16) {
    // Clear entire play area to prevent flickering
    tft.fillRect(53, 63, 219, 114, THEME_BG);
    
    updateBalls();
    checkWallCollisions();
    
    // Draw walls
    drawWalls();
    
    // Draw balls
    drawBalls();
    
    lastUpdate = millis();
  }
}

void updateBalls() {
  bool removed = false;
  for (int i = 0; i < MAX_BALLS; i++) {
    if (!balls[i].active) continue;

    // Decay: energy fades with time; a ball that has faded out is removed
    if (DECAY_RATES[ballDecay] > 0) {
      balls[i].energy *= (1.0f - DECAY_RATES[ballDecay] * 0.016f);
      if (balls[i].energy < BALL_MIN_ENERGY) {
        balls[i].active = false;
        numActiveBalls--;
        removed = true;
        continue;
      }
    }
    
    // Update position
    balls[i].x += balls[i].vx;
    balls[i].y += balls[i].vy;
    
    // Bounce off walls with proper collision detection
    if (balls[i].x - balls[i].size <= 53) {
      balls[i].vx = abs(balls[i].vx);
      balls[i].x = 53 + balls[i].size;
    }
    if (balls[i].x + balls[i].size >= 272) {
      balls[i].vx = -abs(balls[i].vx);
      balls[i].x = 272 - balls[i].size;
    }
    if (balls[i].y - balls[i].size <= 63) {
      balls[i].vy = abs(balls[i].vy);
      balls[i].y = 63 + balls[i].size;
    }
    if (balls[i].y + balls[i].size >= 177) {
      balls[i].vy = -abs(balls[i].vy);
      balls[i].y = 177 - balls[i].size;
    }
  }
  if (removed) {
    tft.fillRect(255, 178, 65, 12, THEME_BG);
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawString("Balls:" + String(numActiveBalls) + "/" + String(MAX_BALLS) + " ", 255, 180, 1);
  }
}

void drawBalls() {
  for (int i = 0; i < MAX_BALLS; i++) {
    if (!balls[i].active) continue;
    float e = balls[i].energy;
    int radius = max(2, (int)(balls[i].size * (0.55f + 0.45f * e)));
    tft.fillCircle(balls[i].x, balls[i].y, radius, dimColor(balls[i].color, 0.25f + 0.75f * e));
    tft.drawCircle(balls[i].x, balls[i].y, radius, dimColor(THEME_TEXT, 0.25f + 0.75f * e));
  }
}

void drawWalls() {
  for (int i = 0; i < NUM_WALLS; i++) {
    uint16_t color = walls[i].color;
    
    // Bright flash when active
    if (walls[i].active) {
      unsigned long elapsed = millis() - walls[i].activeTime;
      if (elapsed < 200) {
        color = THEME_TEXT; // Bright white flash
      } else {
        walls[i].active = false;
      }
    }
    
    // Draw wall
    tft.fillRect(walls[i].x, walls[i].y, walls[i].w, walls[i].h, color);
    
    // Add note name for longer walls
    if (walls[i].w > walls[i].h && walls[i].w > 50) {
      tft.setTextColor(THEME_BG, color);
      tft.drawCentreString(walls[i].noteName, 
                          walls[i].x + walls[i].w/2, 
                          walls[i].y - 2, 1);
    }
  }
}


void checkWallCollisions() {
  for (int b = 0; b < MAX_BALLS; b++) {
    if (!balls[b].active) continue;
    
    static float lastX[MAX_BALLS], lastY[MAX_BALLS];
    static bool initialized = false;
    
    if (!initialized) {
      for (int i = 0; i < MAX_BALLS; i++) {
        lastX[i] = balls[i].x;
        lastY[i] = balls[i].y;
      }
      initialized = true;
    }
    
    // Check collision with each wall segment
    for (int w = 0; w < NUM_WALLS; w++) {
      if (walls[w].active) continue; // Skip if wall is already active
      
      bool collision = false;
      
      // Check collision based on ball position and wall bounds
      if (walls[w].side == 0) { // Top walls
        if (balls[b].y - balls[b].size <= walls[w].y + walls[w].h &&
            balls[b].x >= walls[w].x && balls[b].x <= walls[w].x + walls[w].w &&
            lastY[b] > balls[b].y) {
          collision = true;
        }
      }
      else if (walls[w].side == 1) { // Right walls
        if (balls[b].x + balls[b].size >= walls[w].x &&
            balls[b].y >= walls[w].y && balls[b].y <= walls[w].y + walls[w].h &&
            lastX[b] < balls[b].x) {
          collision = true;
        }
      }
      else if (walls[w].side == 2) { // Bottom walls
        if (balls[b].y + balls[b].size >= walls[w].y &&
            balls[b].x >= walls[w].x && balls[b].x <= walls[w].x + walls[w].w &&
            lastY[b] < balls[b].y) {
          collision = true;
        }
      }
      else if (walls[w].side == 3) { // Left walls
        if (balls[b].x - balls[b].size <= walls[w].x + walls[w].w &&
            balls[b].y >= walls[w].y && balls[b].y <= walls[w].y + walls[w].h &&
            lastX[b] > balls[b].x) {
          collision = true;
        }
      }
      
      if (collision) {
        if (deviceConnected) {
          // Louder when the ball is fresh, softer as it decays
          int velocity = constrain((int)(20 + 90 * balls[b].energy) + (int)random(-8, 8), 12, 120);
          sendMIDI(0x90, walls[w].note, velocity);
          sendMIDI(0x80, walls[w].note, 0);
        }
        
        balls[b].energy *= BALL_HIT_LOSS;
        walls[w].active = true;
        walls[w].activeTime = millis();
        
        Serial.printf("Wall segment hit: %s\n", walls[w].noteName.c_str());
        break; // Only trigger one wall per ball per frame
      }
    }
    
    // Update last positions
    lastX[b] = balls[b].x;
    lastY[b] = balls[b].y;
  }
}

#endif