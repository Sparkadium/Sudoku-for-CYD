/*
 * ============================================================================
 *  SUDOKU - Minimal Sudoku Game for ESP32-2432S028R (Cheap Yellow Display)
 * ============================================================================
 *  TFT_eSPI User_Setup.h:  ILI9341_2_DRIVER, MOSI=13 SCLK=14 CS=15
 *                           DC=2 RST=12 BL=21 SPI_FREQ=55000000
 *  Board: ESP32 Dev Module
 *  Touch: XPT2046 on HSPI (CLK=25, MISO=39, MOSI=32, CS=33, IRQ=36)
 */
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SD.h>


// ── Hardware pins ──────────────────────────────────────────────────────────
#define TOUCH_CS  33
#define TOUCH_IRQ 36
#define BL_PIN    21

TFT_eSPI tft = TFT_eSPI();
SPIClass hsp(HSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

// ── Screen geometry (portrait 240×320) ─────────────────────────────────────
#define SW 240
#define SH 320

// Grid: 9×9 cells, 26px each = 234px, centered with 3px offset
#define CELL   26
#define GRID_X 3
#define GRID_Y 3
#define GRID_W (CELL * 9)  // 234

// Number picker: row of 9 buttons below the grid
#define PICK_Y (GRID_Y + GRID_W + 4)   // ~241
#define PICK_H 28

// Action buttons below number picker
#define BTN_Y  (PICK_Y + PICK_H + 4)   // ~273
#define BTN_H  40

// ── Colors ─────────────────────────────────────────────────────────────────
#define C_BG       0x1082  // Dark background
#define C_GRID     0x4A69  // Grid lines
#define C_THICK    0x9CF3  // Thick grid lines (3×3 borders)
#define C_CELL     0x18E3  // Cell background
#define C_SEL      0x2A4A  // Selected cell highlight
#define C_GIVEN    0xFFFF  // Given/clue numbers (white)
#define C_PLAYED   0x07FF  // Player-entered numbers (cyan)
#define C_ERROR    0xF800  // Error highlight (red)
#define C_PICKER   0x3186  // Number picker background
#define C_PICKSEL  0x07FF  // Selected number in picker
#define C_PICKTXT  0xFFFF  // Picker text
#define C_BTN      0x4A69  // Button background
#define C_BTNTXT   0xFFFF  // Button text
#define C_WIN      0x07E0  // Win text (green)
#define C_SAME     0x1B4D  // Highlight cells with same number

// ── Touch ──────────────────────────────────────────────────────────────────
#define TDEB 120
static unsigned long lastTouch = 0;

// ── Game state ─────────────────────────────────────────────────────────────
static uint8_t puzzle[9][9];   // Current board (0 = empty)
static uint8_t solution[9][9]; // Full solution
static bool    given[9][9];    // true = clue cell (immutable)
static int8_t  selR = -1, selC = -1;  // Selected cell (-1 = none)
static uint8_t selNum = 0;            // Selected number in picker (0 = none/erase)
static bool    won = false;
static uint8_t difficulty = 35;        // Number of cells to remove (higher = harder)

// ── Screens ────────────────────────────────────────────────────────────────
enum Screen { SCR_MENU, SCR_GAME };
static Screen screen = SCR_MENU;

// ── Random seed ────────────────────────────────────────────────────────────
// Uses analogRead noise for seeding
void seedRandom() {
  uint32_t seed = 0;
  for (int i = 0; i < 32; i++) {
    seed = (seed << 1) | (analogRead(34) & 1);
    delayMicroseconds(50);
  }
  randomSeed(seed);
}

// ══════════════════════════════════════════════════════════════════════════
//  SUDOKU GENERATOR
// ══════════════════════════════════════════════════════════════════════════

// Check if placing val at (r,c) is valid in the given board
bool isValid(uint8_t board[9][9], uint8_t r, uint8_t c, uint8_t val) {
  for (uint8_t i = 0; i < 9; i++) {
    if (board[r][i] == val) return false;
    if (board[i][c] == val) return false;
  }
  uint8_t br = (r / 3) * 3, bc = (c / 3) * 3;
  for (uint8_t i = 0; i < 3; i++)
    for (uint8_t j = 0; j < 3; j++)
      if (board[br + i][bc + j] == val) return false;
  return true;
}

// Fill the board using backtracking with randomized digit order
bool fillBoard(uint8_t board[9][9], uint8_t pos) {
  if (pos == 81) return true;
  uint8_t r = pos / 9, c = pos % 9;
  // Shuffle digits 1-9
  uint8_t digits[9] = {1,2,3,4,5,6,7,8,9};
  for (int i = 8; i > 0; i--) {
    int j = random(0, i + 1);
    uint8_t t = digits[i]; digits[i] = digits[j]; digits[j] = t;
  }
  for (uint8_t d = 0; d < 9; d++) {
    if (isValid(board, r, c, digits[d])) {
      board[r][c] = digits[d];
      if (fillBoard(board, pos + 1)) return true;
      board[r][c] = 0;
    }
  }
  return false;
}

// Generate a new puzzle
void generatePuzzle(uint8_t removals) {
  memset(solution, 0, sizeof(solution));
  fillBoard(solution, 0);

  // Copy solution to puzzle, then remove cells
  memcpy(puzzle, solution, sizeof(solution));
  memset(given, true, sizeof(given));

  // Randomly remove cells
  uint8_t removed = 0;
  uint16_t attempts = 0;
  while (removed < removals && attempts < 300) {
    uint8_t r = random(0, 9);
    uint8_t c = random(0, 9);
    if (puzzle[r][c] != 0) {
      puzzle[r][c] = 0;
      given[r][c] = false;
      removed++;
    }
    attempts++;
  }

  selR = -1; selC = -1; selNum = 0; won = false;
}

// Check if the board is fully and correctly solved
bool checkWin() {
  for (uint8_t r = 0; r < 9; r++)
    for (uint8_t c = 0; c < 9; c++)
      if (puzzle[r][c] != solution[r][c]) return false;
  return true;
}

// ══════════════════════════════════════════════════════════════════════════
//  DRAWING
// ══════════════════════════════════════════════════════════════════════════

void drawCenteredText(const char* txt, int16_t x, int16_t y, int16_t w,
                      uint8_t sz, uint16_t fg, uint16_t bg) {
  int16_t tw = strlen(txt) * 6 * sz;
  tft.setTextSize(sz);
  tft.setTextColor(fg, bg);
  tft.setCursor(x + (w - tw) / 2, y);
  tft.print(txt);
}

void drawGrid() {
  // Cell backgrounds
  for (uint8_t r = 0; r < 9; r++) {
    for (uint8_t c = 0; c < 9; c++) {
      int16_t x = GRID_X + c * CELL;
      int16_t y = GRID_Y + r * CELL;

      // Determine cell color
      uint16_t bg = C_CELL;
      bool isSelected = (r == selR && c == selC);

      // Highlight same-number cells
      if (selR >= 0 && selC >= 0 && puzzle[selR][selC] != 0 &&
          puzzle[r][c] == puzzle[selR][selC] && !isSelected) {
        bg = C_SAME;
      }

      // Highlight same row/col/box as selection
      if (selR >= 0 && selC >= 0 && !isSelected) {
        if (r == selR || c == selC ||
            (r/3 == selR/3 && c/3 == selC/3)) {
          if (bg == C_CELL) bg = 0x1924; // Subtle row/col highlight
        }
      }

      if (isSelected) bg = C_SEL;

      // Error check: player-entered number that conflicts
      bool isError = false;
      if (!given[r][c] && puzzle[r][c] != 0 && puzzle[r][c] != solution[r][c]) {
        isError = true;
      }

      tft.fillRect(x + 1, y + 1, CELL - 1, CELL - 1, bg);

      // Draw number
      if (puzzle[r][c] != 0) {
        char num[2] = { (char)('0' + puzzle[r][c]), 0 };
        uint16_t fg = given[r][c] ? C_GIVEN : (isError ? C_ERROR : C_PLAYED);
        drawCenteredText(num, x, y + 6, CELL, 2, fg, bg);
      }
    }
  }

  // Grid lines - thin lines for all cells
  for (uint8_t i = 0; i <= 9; i++) {
    int16_t x = GRID_X + i * CELL;
    int16_t y = GRID_Y + i * CELL;
    uint16_t color = (i % 3 == 0) ? C_THICK : C_GRID;
    uint8_t  thick = (i % 3 == 0) ? 2 : 1;
    // Vertical
    tft.fillRect(x, GRID_Y, thick, GRID_W + 1, color);
    // Horizontal
    tft.fillRect(GRID_X, y, GRID_W + 1, thick, color);
  }
}

void drawNumberPicker() {
  for (uint8_t i = 0; i < 9; i++) {
    int16_t x = GRID_X + i * CELL;
    uint16_t bg = (selNum == i + 1) ? C_PICKSEL : C_PICKER;
    uint16_t fg = (selNum == i + 1) ? 0x0000 : C_PICKTXT;
    tft.fillRoundRect(x, PICK_Y, CELL, PICK_H, 3, bg);
    char num[2] = { (char)('1' + i), 0 };
    drawCenteredText(num, x, PICK_Y + 6, CELL, 2, fg, bg);
  }
}

void drawButtons() {
  // Erase button
  tft.fillRoundRect(GRID_X, BTN_Y, 74, BTN_H, 5, C_BTN);
  drawCenteredText("ERASE", GRID_X, BTN_Y + 12, 74, 2, C_BTNTXT, C_BTN);

  // New Game button
  tft.fillRoundRect(GRID_X + 80, BTN_Y, 74, BTN_H, 5, C_BTN);
  drawCenteredText("NEW", GRID_X + 80, BTN_Y + 12, 74, 2, C_BTNTXT, C_BTN);

  // Hint button
  tft.fillRoundRect(GRID_X + 160, BTN_Y, 74, BTN_H, 5, 0x4208);
  drawCenteredText("HINT", GRID_X + 160, BTN_Y + 12, 74, 2, 0xFEA0, 0x4208);
}

void drawGameScreen() {
  tft.fillScreen(C_BG);
  drawGrid();
  drawNumberPicker();
  drawButtons();

  if (won) {
    tft.fillRoundRect(20, 130, 200, 50, 8, 0x0000);
    tft.drawRoundRect(20, 130, 200, 50, 8, C_WIN);
    drawCenteredText("SOLVED!", 20, 142, 200, 3, C_WIN, 0x0000);
  }
}

void drawMenuScreen() {
  tft.fillScreen(C_BG);

  drawCenteredText("SUDOKU", 0, 40, SW, 4, C_GIVEN, C_BG);

  // Decorative grid icon
  int16_t gx = 75, gy = 90;
  for (int i = 0; i <= 3; i++) {
    tft.drawFastHLine(gx, gy + i * 30, 90, C_THICK);
    tft.drawFastVLine(gx + i * 30, gy, 90, C_THICK);
  }
  // A few sample numbers
  tft.setTextSize(2); tft.setTextColor(C_PLAYED, C_BG);
  tft.setCursor(gx + 9, gy + 7);  tft.print("5");
  tft.setCursor(gx + 39, gy + 37); tft.print("3");
  tft.setCursor(gx + 69, gy + 67); tft.print("7");
  tft.setTextColor(C_GIVEN, C_BG);
  tft.setCursor(gx + 39, gy + 7);  tft.print("8");
  tft.setCursor(gx + 9, gy + 67);  tft.print("1");

  // Difficulty buttons
  int16_t by = 200;
  tft.fillRoundRect(30, by, 180, 32, 6, 0x0320);
  drawCenteredText("EASY", 30, by + 8, 180, 2, C_BTNTXT, 0x0320);

  tft.fillRoundRect(30, by + 40, 180, 32, 6, 0x7BE0);
  drawCenteredText("MEDIUM", 30, by + 48, 180, 2, 0x0000, 0x7BE0);

  tft.fillRoundRect(30, by + 80, 180, 32, 6, 0xF800);
  drawCenteredText("HARD", 30, by + 88, 180, 2, C_BTNTXT, 0xF800);
}

// ══════════════════════════════════════════════════════════════════════════
//  TOUCH HANDLING
// ══════════════════════════════════════════════════════════════════════════

void handleGameTouch(int16_t tx, int16_t ty) {
  // Grid tap — select a cell
  if (tx >= GRID_X && tx < GRID_X + GRID_W &&
      ty >= GRID_Y && ty < GRID_Y + GRID_W) {
    uint8_t c = (tx - GRID_X) / CELL;
    uint8_t r = (ty - GRID_Y) / CELL;
    if (c < 9 && r < 9) {
      if (selR == r && selC == c) {
        selR = -1; selC = -1; // Deselect on re-tap
      } else {
        selR = r; selC = c;
      }
      // If a number is selected in picker and cell is not given, place it
      if (selR >= 0 && selC >= 0 && !given[selR][selC] && selNum > 0 && !won) {
        puzzle[selR][selC] = selNum;
        if (checkWin()) won = true;
      }
      drawGrid();
      drawNumberPicker();
      if (won) {
        tft.fillRoundRect(20, 130, 200, 50, 8, 0x0000);
        tft.drawRoundRect(20, 130, 200, 50, 8, C_WIN);
        drawCenteredText("SOLVED!", 20, 142, 200, 3, C_WIN, 0x0000);
      }
    }
    return;
  }

  // Number picker tap
  if (ty >= PICK_Y && ty < PICK_Y + PICK_H &&
      tx >= GRID_X && tx < GRID_X + GRID_W) {
    uint8_t n = (tx - GRID_X) / CELL;
    if (n < 9) {
      uint8_t newNum = n + 1;
      if (selNum == newNum) {
        selNum = 0; // Deselect
      } else {
        selNum = newNum;
      }
      // If a cell is already selected, place the number
      if (selR >= 0 && selC >= 0 && !given[selR][selC] && selNum > 0 && !won) {
        puzzle[selR][selC] = selNum;
        if (checkWin()) won = true;
      }
      drawGrid();
      drawNumberPicker();
      if (won) {
        tft.fillRoundRect(20, 130, 200, 50, 8, 0x0000);
        tft.drawRoundRect(20, 130, 200, 50, 8, C_WIN);
        drawCenteredText("SOLVED!", 20, 142, 200, 3, C_WIN, 0x0000);
      }
    }
    return;
  }

  // Button taps
  if (ty >= BTN_Y && ty < BTN_Y + BTN_H) {
    // ERASE
    if (tx >= GRID_X && tx < GRID_X + 74) {
      if (selR >= 0 && selC >= 0 && !given[selR][selC] && !won) {
        puzzle[selR][selC] = 0;
        selNum = 0;
        drawGrid();
        drawNumberPicker();
      }
      return;
    }
    // NEW GAME
    if (tx >= GRID_X + 80 && tx < GRID_X + 154) {
      screen = SCR_MENU;
      drawMenuScreen();
      return;
    }
    // HINT
    if (tx >= GRID_X + 160 && tx < GRID_X + 234) {
      if (!won) {
        // Find a random empty cell and reveal it
        uint8_t empties[81]; uint8_t ne = 0;
        for (uint8_t r = 0; r < 9; r++)
          for (uint8_t c = 0; c < 9; c++)
            if (puzzle[r][c] == 0)
              empties[ne++] = r * 9 + c;
        if (ne > 0) {
          uint8_t pick = empties[random(0, ne)];
          uint8_t r = pick / 9, c = pick % 9;
          puzzle[r][c] = solution[r][c];
          given[r][c] = true;  // Mark as given so it can't be erased
          selR = r; selC = c;
          if (checkWin()) won = true;
          drawGrid();
          if (won) {
            tft.fillRoundRect(20, 130, 200, 50, 8, 0x0000);
            tft.drawRoundRect(20, 130, 200, 50, 8, C_WIN);
            drawCenteredText("SOLVED!", 20, 142, 200, 3, C_WIN, 0x0000);
          }
        }
      }
      return;
    }
  }
}

void handleMenuTouch(int16_t tx, int16_t ty) {
  int16_t by = 200;
  if (tx >= 30 && tx < 210) {
    if (ty >= by && ty < by + 32) {
      difficulty = 30;  // Easy
    } else if (ty >= by + 40 && ty < by + 72) {
      difficulty = 42;  // Medium
    } else if (ty >= by + 80 && ty < by + 112) {
      difficulty = 52;  // Hard
    } else {
      return;
    }
    tft.fillScreen(C_BG);
    drawCenteredText("Generating...", 0, 140, SW, 2, C_GIVEN, C_BG);
    generatePuzzle(difficulty);
    screen = SCR_GAME;
    drawGameScreen();
  }
}

// ══════════════════════════════════════════════════════════════════════════
//  SETUP & LOOP
// ══════════════════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  pinMode(BL_PIN, OUTPUT);
  digitalWrite(BL_PIN, HIGH);

  tft.init();
  tft.setRotation(0);  // Portrait 240×320
  tft.fillScreen(C_BG);
  tft.setTextWrap(false);

  // Touch on HSPI (same config as reference project)
  hsp.begin(25, 39, 32, TOUCH_CS);
  ts.begin(hsp);
  ts.setRotation(0);

  seedRandom();

  screen = SCR_MENU;
  drawMenuScreen();
}

void loop() {
  unsigned long now = millis();

  // Read touch with debounce
  if (ts.touched() && (now - lastTouch) > TDEB) {
    TS_Point p = ts.getPoint();
    lastTouch = now;

    // Map raw touch coordinates to screen pixels
    int16_t tx = map(p.x, 200, 3800, 0, SW);
    int16_t ty = map(p.y, 200, 3800, 0, SH);
    tx = constrain(tx, 0, SW - 1);
    ty = constrain(ty, 0, SH - 1);

    switch (screen) {
      case SCR_MENU: handleMenuTouch(tx, ty); break;
      case SCR_GAME: handleGameTouch(tx, ty); break;
    }
  }
}
