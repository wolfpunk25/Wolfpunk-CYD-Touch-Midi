#ifndef PATCH_MODE_H
#define PATCH_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "midi_utils.h"
#include "patch_names.h"   // generated from the Pi: tools/gen_patch_names.py

// PATCH mode: lists every patch on the Pi synth and selects one with a
// MIDI Program Change (program = patch id number - 1).

#define PATCH_COLS 2
#define PATCH_ROWS 5
#define PATCH_PER_PAGE (PATCH_COLS * PATCH_ROWS)
#define PATCH_LIST_Y 52
#define PATCH_CELL_W 152
#define PATCH_CELL_H 28

int patchPage = 0;
int patchSelected = -1;   // index into PATCH_LIST of the last patch chosen

void initializePatchMode() {
  patchPage = 0;
  patchSelected = -1;
}

static int patchPageCount() {
  return (PATCH_COUNT + PATCH_PER_PAGE - 1) / PATCH_PER_PAGE;
}

static void patchCellPos(int slot, int &x, int &y) {
  x = 5 + (slot % PATCH_COLS) * (PATCH_CELL_W + 6);
  y = PATCH_LIST_Y + (slot / PATCH_COLS) * (PATCH_CELL_H + 2);
}

static void drawPatchCell(int slot) {
  int index = patchPage * PATCH_PER_PAGE + slot;
  int x, y;
  patchCellPos(slot, x, y);
  if (index >= PATCH_COUNT) {
    tft.fillRect(x, y, PATCH_CELL_W, PATCH_CELL_H, THEME_BG);
    return;
  }
  bool selected = (index == patchSelected);
  uint16_t fill = selected ? THEME_ACCENT : THEME_BG;
  uint16_t text = selected ? THEME_BG : THEME_TEXT;
  tft.fillRoundRect(x, y, PATCH_CELL_W, PATCH_CELL_H, 6, fill);
  tft.drawRoundRect(x, y, PATCH_CELL_W, PATCH_CELL_H, 6, THEME_ACCENT);

  String name = PATCH_LIST[index].name;
  while (name.length() > 1 && tft.textWidth(name, 2) > PATCH_CELL_W - 12) {
    name.remove(name.length() - 1);
  }
  tft.setTextColor(text, fill);
  tft.drawString(name, x + 6, y + 6, 2);
}

static void drawPatchFooter() {
  tft.fillRect(0, 200, 320, 40, THEME_BG);
  drawRoundButton(10, 205, 70, 28, "PREV", THEME_SECONDARY);
  drawRoundButton(240, 205, 70, 28, "NEXT", THEME_SECONDARY);
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawCentreString(String(patchPage + 1) + " / " + String(patchPageCount()), 160, 212, 2);
}

void drawPatchMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("PATCHES", "Tap to load on the synth");
  for (int slot = 0; slot < PATCH_PER_PAGE; slot++) drawPatchCell(slot);
  drawPatchFooter();
}

void handlePatchMode() {
  if (!touch.justPressed) return;

  if (isButtonPressed(10, 10, 50, 25)) {
    exitToMenu();
    return;
  }

  int pages = patchPageCount();
  if (isButtonPressed(10, 205, 70, 28)) {
    patchPage = (patchPage + pages - 1) % pages;
    drawPatchMode();
    return;
  }
  if (isButtonPressed(240, 205, 70, 28)) {
    patchPage = (patchPage + 1) % pages;
    drawPatchMode();
    return;
  }

  for (int slot = 0; slot < PATCH_PER_PAGE; slot++) {
    int index = patchPage * PATCH_PER_PAGE + slot;
    if (index >= PATCH_COUNT) break;
    int x, y;
    patchCellPos(slot, x, y);
    if (isButtonPressed(x, y, PATCH_CELL_W, PATCH_CELL_H)) {
      int previous = patchSelected;
      patchSelected = index;
      sendProgramChange(PATCH_LIST[index].program);
      if (previous >= patchPage * PATCH_PER_PAGE &&
          previous < (patchPage + 1) * PATCH_PER_PAGE) {
        drawPatchCell(previous - patchPage * PATCH_PER_PAGE);
      }
      drawPatchCell(slot);
      return;
    }
  }
}

#endif
