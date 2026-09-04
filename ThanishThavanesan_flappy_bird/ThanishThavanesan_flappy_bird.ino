// Thanish Thavanesan
//ESP32 Flappy Bird on OLED screen, Joystick for Menu, and 3-Button Controls
// Controls:
//   Joystick          -> move menu cursor (Easy/Medium/Hard) and tilts up for flaps in-game
//   Joystick click    -> confirm menu selection
//   Flap button       -> alternate way to flap
//   Pause button      -> pause / resume
//   Quit button       -> quit to menu


// Libraries used 
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED Setup 
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Joystick Pins 
#define JOYSTICK_SW 33   // click = confirm
#define JOYSTICK_VRY 35  // Y-axis = menu navigation 

// Buttons 
#define FLAP_BUTTON 26    // flap button, wired to GND (uses internal pull-up)
#define PAUSE_BUTTON 32   // pause / resume
#define QUIT_BUTTON 25    // quit to menu

// 7-Segment Display (via 74HC595) 
#define DATA_PIN 16
#define CLOCK_PIN 17
#define LATCH_PIN 18
#define SEG1_PIN 12
#define SEG2_PIN 27
#define SEG3_PIN 2
#define SEG4_PIN 15
int digitPins[4] = {SEG1_PIN, SEG2_PIN, SEG3_PIN, SEG4_PIN};
// Segment patterns wiring: Q0=dp, Q1=g, Q2=f, Q3=e, Q4=d, Q5=c, Q6=b, Q7=a
const byte digitSegments[10] = {
  0xFC, 0x60, 0xDA, 0xF2, 0x66, 0xB6, 0xBE, 0xE0, 0xFE, 0xF6
};
hw_timer_t *displayTimer = NULL;
volatile int isrDigitIndex = 0;
void IRAM_ATTR onDisplayTimer(); // forward declaration - Arduino's auto-prototyping
                                // can miss IRAM_ATTR functions defined later in the file

// Setting up Game State 
enum GameState { DIFFICULTY_SELECT, PLAYING, PAUSED, GAME_OVER_STATE };
GameState gameState = DIFFICULTY_SELECT;

float pipeSpeed = 2.0;    // pixels per game frame
int gapHeightSetting = 22;
int highScore = 0;

// Menu Navigation 
const char* menuLabels[3] = {"Easy", "Medium", "Hard"};
int menuIndex = 0;
bool joystickNeutral = true; // requires returning to center before next move registers
bool joystickFlapNeutral = true; // requires returning to center before the joystick can flap in-game

// Bird 
float birdY = 32;
float birdVelocity = 0;
const float gravity = 0.5;
const float flapStrength = -3.5;
const int birdX = 20;              //left side of bird
const int birdSize = 3;            //size of the bird, adding with birdx=right size

// Pipes 
const int pipeWidth = 10;
const int numPipes = 3;
const int pipeSpacing = 50;
int pipeX[numPipes];
int pipeGapY[numPipes];
bool pipePassed[numPipes];

// Score 
volatile int score = 0;
bool lastFlapButtonState = HIGH;
bool lastPauseButtonState = HIGH;
bool lastQuitButtonState = HIGH;

void setup() {
  Serial.begin(115200);
  pinMode(JOYSTICK_SW, INPUT_PULLUP); // button reads LOW when pressed
  pinMode(FLAP_BUTTON, INPUT_PULLUP); // dedicated flap button, LOW when pressed
  pinMode(PAUSE_BUTTON, INPUT_PULLUP);
  pinMode(QUIT_BUTTON, INPUT_PULLUP);
  // JOYSTICK_VRY is an ADC input pin, no pinMode needed for analogRead

  pinMode(DATA_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(LATCH_PIN, OUTPUT);
  for (int i = 0; i < 4; i++) pinMode(digitPins[i], OUTPUT);

  Wire.begin();
  Wire.setClock(400000); // faster I^2C = shorter display.display() blocking time,
                          // which was causing the 7-segment multiplexing to not refresh frequently

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {     //0x3C is the OLED's I^2C address
    Serial.println("OLED init failed");
    while (true);
  }

  // Hardware timer: refreshes one 7-segment digit every 2ms in the background,
  // regardless of what the main loop or OLED are doing (fixes the flicker)
  //Calculation: T= 1/1 000 000Hz=0.000001sec= 1 micro sec and 2000 ticks* 1 micro sec=2000 micro sec= 2ms
  displayTimer = timerBegin(1000000); // 1 MHz tick rate
  timerAttachInterrupt(displayTimer, &onDisplayTimer);
  timerAlarm(displayTimer, 2000, true, 0); // 2000 ticks @ 1MHz = 2ms, auto-reload

  gameState = DIFFICULTY_SELECT;
  drawDifficultyScreen();
}

//Adjusts the rate in which pipes come,and the gap from different difficulties
void applyDifficulty(int index) {
  switch (index) {
    case 0: // Easy
      pipeSpeed = 1.5;
      gapHeightSetting = 28;
      break;
    case 1: // Medium
      pipeSpeed = 2.5;
      gapHeightSetting = 22;
      break;
    case 2: // Hard
      pipeSpeed = 3.5;
      gapHeightSetting = 16;
      break;
  }
  resetGame();
}

//Resets the game and places the bird at its initial position
void resetGame() {
  birdY = 32;
  birdVelocity = 0;
  score = 0;
  gameState = PLAYING;
  joystickFlapNeutral = true;

  for (int i = 0; i < numPipes; i++) {
    pipeX[i] = SCREEN_WIDTH + i * pipeSpacing;                         //Screen width=128, pipeSpacing=60
    pipeGapY[i] = random(10, SCREEN_HEIGHT - gapHeightSetting - 10);   //10 is used so it gives the gap some distance from the top
    pipePassed[i] = false;                                             //Initial state, so bird has not passed any pipes
  }
}

void loop() {

  //Makes sure the button is only pressed once during flaps
  bool flapPressed = (digitalRead(FLAP_BUTTON) == LOW);
  bool flapJustPressed = (flapPressed && !lastFlapButtonState);
  lastFlapButtonState = flapPressed;

  bool pausePressed = (digitalRead(PAUSE_BUTTON) == LOW);
  bool pauseJustPressed = (pausePressed && !lastPauseButtonState);
  lastPauseButtonState = pausePressed;

  bool quitPressed = (digitalRead(QUIT_BUTTON) == LOW);
  bool quitJustPressed = (quitPressed && !lastQuitButtonState);
  lastQuitButtonState = quitPressed;

  if (gameState == DIFFICULTY_SELECT) {
    handleMenuNavigation();
    drawDifficultyScreen();
    delay(20);                          //Wait 20ms for OLED to update screen for menu selection
    return;
  }
  //If game is paused create pause screen, and wait until user chooses an option
  if (gameState == PAUSED) {
    if (pauseJustPressed) {
      gameState = PLAYING;
    } else if (quitJustPressed) {
      gameState = DIFFICULTY_SELECT;
    }
    drawPausedScreen();
    delay(20);
    return;
  }

  if (gameState == GAME_OVER_STATE) {
    bool joyClicked = (digitalRead(JOYSTICK_SW) == LOW);  
    if (quitJustPressed || joyClicked) {                 //Click joystick to exit or exit button
      gameState = DIFFICULTY_SELECT;
    }
    drawGame();
    delay(20);
    return;
  }

  // PLAYING state for buttons 
  if (pauseJustPressed) {
    gameState = PAUSED;
    drawPausedScreen();
    delay(20);
    return;
  }
  if (quitJustPressed) {
    gameState = DIFFICULTY_SELECT;
    delay(20);
    return;
  }
  if (flapJustPressed) {
    birdVelocity = flapStrength;
  }

  // Joystick-up also flaps, but requires returning to
  // Center before it can trigger again, so holding it up doesn't spam flaps
  int flapYVal = analogRead(JOYSTICK_VRY);
  if (joystickFlapNeutral) {
    if (flapYVal > 2900) {     //flaps up, since hardware is opposite
      birdVelocity = flapStrength;
      joystickFlapNeutral = false;
    }
  } else {
    if (flapYVal > 1600 && flapYVal < 2500) {       //Center range of joystick
      joystickFlapNeutral = true;
    }
  }

  // Physics
  birdVelocity += gravity;                //Increases downward velocity of bird
  birdY += birdVelocity;                  //Makes the bird move downward

  //Adds barriers to the bottom and top of the screen
  if (birdY < 0) {
    birdY = 0;                      
    birdVelocity = 0;                 //Can go up to the top, but stops upward movement and game continues
  }
  if (birdY > SCREEN_HEIGHT - birdSize) {
    endGame();
  }

  //Move pipes left, and if they have already passed the screen, move them back to the right with different gaps
  for (int i = 0; i < numPipes; i++) {
    pipeX[i] -= pipeSpeed;            //Moves pipes left

    if (pipeX[i] < -pipeWidth) {     //When the whole pipe has disappeared, not just one edge
      pipeX[i] = SCREEN_WIDTH;
      pipeGapY[i] = random(10, SCREEN_HEIGHT - gapHeightSetting - 10);
      pipePassed[i] = false;          //Reset passed score
    }

    bool withinPipeX = (birdX + birdSize > pipeX[i]) && (birdX < pipeX[i] + pipeWidth);   //Checks horizontal overlap with pipe
    bool withinGap = (birdY > pipeGapY[i]) && (birdY + birdSize < pipeGapY[i] + gapHeightSetting); 
    if (withinPipeX && !withinGap) {
      endGame();
    }

    // Give 1 point when right edge of pipe passes the bird (not when the pipe = bird), which avoids
    // missing an exact-pixel match when pipes move by more than 1px per frame
    if (!pipePassed[i] && (pipeX[i] + pipeWidth < birdX)) {
      score++;
      pipePassed[i] = true;
    }
  }

  drawGame();
  delay(30); 
}

void endGame() {
  if (score > highScore) {
    highScore = score;
  }
  gameState = GAME_OVER_STATE;
}

// 7-Segment Display Functions (run from a hardware timer interrupt) 
// Using common cathode, so bits do not get inverted
void IRAM_ATTR sendSegments(byte segData) {            //Tells display which segments to turn on to make a # 
  digitalWrite(LATCH_PIN, LOW); 
  shiftOut(DATA_PIN, CLOCK_PIN, LSBFIRST, segData);
  digitalWrite(LATCH_PIN, HIGH);
}

void IRAM_ATTR showDigitAt(int position, int number) { //Tells display which physical digit position it should turn on 
  sendSegments(digitSegments[number]);
  for (int i = 0; i < 4; i++) {
      digitalWrite(digitPins[i], (i == position) ? LOW : HIGH);
  }
}

// Display refreshes every 2ms from display timer regardless of what the main loop or OLED is doing 
void IRAM_ATTR onDisplayTimer() {
  int num = score;                      //Make a copy so rest of function works with num
  int digit;
  switch (isrDigitIndex) {
    case 0: digit = (num / 1000) % 10; break;
    case 1: digit = (num / 100) % 10; break;
    case 2: digit = (num / 10) % 10; break;
    default: digit = num % 10; break;
  }
  showDigitAt(isrDigitIndex, digit);
  isrDigitIndex = (isrDigitIndex + 1) % 4;
}

// Reads joystick Y-axis and moves the menu cursor once per flick
void handleMenuNavigation() {
  int yVal = analogRead(JOYSTICK_VRY); // ESP32 ADC range: 0-4095, center approximately 2048

  if (joystickNeutral) {
    if (yVal < 1200) { // Pushed up
      menuIndex = (menuIndex + 1) % 3;
      joystickNeutral = false;
    } else if (yVal > 2900) { // Pushed down
      menuIndex = (menuIndex - 1 + 3) % 3;
      joystickNeutral = false;
    }
  } else {
    // Require it to return near center before allowing the next move
    if (yVal > 1600 && yVal < 2500) {
      joystickNeutral = true;
    }
  }

  if (digitalRead(JOYSTICK_SW) == LOW) {     
    applyDifficulty(menuIndex);
  }
}

void drawDifficultyScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(15, 0);
  display.println("FLAPPY BIRD");
 
  //Lists Modes of difficulty with a ">" cursor for whichever is selected
  for (int i = 0; i < 3; i++) {
    display.setCursor(10, 16 + i * 12);
    if (i == menuIndex) {
      display.print("> ");
    } else {
      display.print("  ");
    }
    display.println(menuLabels[i]);
  }
  //Shows high score and prints it to the OLED
  display.setCursor(0, 55);
  display.print("High Score: ");
  display.println(highScore);

  display.display();
}
 //Display Pause screen setup
void drawPausedScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(35, 25);
  display.println("PAUSED");
  display.setCursor(5, 40);
  display.println("Resume/Quit btns");
  display.display();
}
//Display end of game with a summary including highscore, final score, and instructions
void drawGame() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  if (gameState == GAME_OVER_STATE) {
    display.setTextSize(1);
    display.setCursor(20, 15);
    display.println("GAME OVER");
    display.setCursor(20, 28);
    display.print("Score: ");
    display.println(score);
    display.setCursor(15, 40);
    display.print("Best: ");
    display.println(highScore);
    display.setCursor(0, 53);
    display.println("Click/Quit for menu");
    display.display();
    return;
  }

  // Bird's setup in game
  display.fillRect(birdX, (int)birdY, birdSize, birdSize, SSD1306_WHITE);

  // Pipe's setup in game
  for (int i = 0; i < numPipes; i++) {
    display.fillRect(pipeX[i], 0, pipeWidth, pipeGapY[i], SSD1306_WHITE);
    display.fillRect(pipeX[i], pipeGapY[i] + gapHeightSetting, pipeWidth, SCREEN_HEIGHT - (pipeGapY[i] + gapHeightSetting), SSD1306_WHITE);
  }

  display.display();
}
