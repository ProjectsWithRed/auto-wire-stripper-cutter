#include <Stepper.h>
#include <Encoder.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#define motorInterfaceType 1
#define i2c_Address 0x3c


const int LINMOT_STEPPERS_STEP_PIN = 19;  // LINMOT: Linear motion
const int LINMOT_STEPPERS_DIR_PIN = 18;

const int EXTRUDER_STEPPER_STEP_PIN = 16;  // The stepper that moves the wire in the extruder.
const int EXTRUDER_STEPPER_DIR_PIN = 17;

const int ENCODER_DT_PIN = 0;
const int ENCODER_CLK_PIN = 23;
const int ENCODER_BTN_PIN = 4;

// For calibration only. The two buttons are used to move the top blade up and down manually.
// Once calibrated, you can remove the buttons from the circuit.
const int BTN1_PIN = 27;
const int BTN2_PIN = 26;

const int LINMOT_STEPPERS_STEPS = 1;  // Steppers step(s) movement at a time.
const int EXTRUDER_STEPPER_STEPS = 1;

const int LINMOT_STEPPERS_SPEED = 2000;
const int EXTRUDER_STEPPER_SPEED = 2000;


const int SCREEN_WIDTH = 128;  // OLED display width, in pixels
const int SCREEN_HEIGHT = 64;  // OLED display height, in pixels
const int TEXT_SIZE = 2;
const int TEXT_OFFSET = 3;

// Values for drawing the wire at the top of the OLED screen.
const int WIRE_STRAND_LENGTH = 30;
const int WIRE_STRAND_Y_POS = 7;
const int WIRE_INSULATION_WIDTH = SCREEN_WIDTH - (WIRE_STRAND_LENGTH * 2);
const int WIRE_INSULATION_HEIGHT = 14;

// These are just references to the corresponding component index in the comps array;
const int STRIPPING_LENGTH1_INDEX = 0;
const int WIRE_LENGTH_INDEX = 1;
const int STRIPPING_LENGTH2_INDEX = 2;
const int QUANTITY_INDEX = 3;
const int STRIPPING_DEPTH_INDEX = 4;
const int START_BTN_INDEX = 5;

const int CUTTING_STEPS = 17750;  // Steps to move blade to fully cut the wire.
const int STRIPPING_MULTIPLIER = 300;  // The chosen stripping depth value on the screen is multiplied by this value.
const int WIRE_MOVEMENT_MULTI = 414;  // How much to move wire per unit on OLED, turn on CALIBRATION_MODE to find this value.
const int DELAY_BETWEEN_CUTS = 100;  // Delay in ms between each cut in the quantity.

// To calibrate the wire movement distance. Use the first cell to enter the distance in mm to move the wire.
// Then adjust WIRE_MOVEMENT_MULTI to get the correct wire length.
const boolean CALIBRATION_MODE = true;


Stepper linMotSteppers(200, LINMOT_STEPPERS_DIR_PIN, LINMOT_STEPPERS_STEP_PIN);
Stepper extruderStepper(200, EXTRUDER_STEPPER_DIR_PIN, EXTRUDER_STEPPER_STEP_PIN);

Encoder encoder(ENCODER_DT_PIN, ENCODER_CLK_PIN);

Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);


int linMotSteppersCurrStep = 0;  // Current position/step of the stepper motor.

// A component is a cell with a value on the OLED display.
struct Component {
    int x, y;  // Position
    int w, h;  // Size
    int value;  // Current value of the cell.
    boolean highlighted;  // Whether the cell is the currently highlighted cell.
    boolean selected;  // Whether the cell is currently the selected one, where its value will be controlled by the encoder.
    boolean btn;  // If it is a button or not.
};


Component comps[] = {{0, 20, 40, 20, 0, 0, 0, 0},
                     {44, 20, 40, 20, 0, 0, 0, 0},
                     {88, 20, 40, 20, 0, 0, 0, 0},
                     {0, 44, 40, 20, 0, 0, 0, 0},
                     {44, 44, 40, 20, 0, 0, 0, 0},
                     {88, 44, 40, 20, 0, 0, 0, 1}};

int numOfComps = sizeof(comps) / sizeof(comps[0]);


int encoderPos;  // Current position/value of the encoder.
int encoderLastPos;  // For OLED drawing.
int encoderLastPosMain;  // For main loop.

boolean encBtnState = false;
boolean encBtnPrevState = false;  // For OLED drawing.
boolean encBtnPrevStateMain = false;  // For main loop.



void setup() {
    linMotSteppers.setSpeed(LINMOT_STEPPERS_SPEED);
    extruderStepper.setSpeed(EXTRUDER_STEPPER_SPEED);

    pinMode(ENCODER_BTN_PIN, INPUT_PULLUP);

    pinMode(BTN1_PIN, INPUT_PULLUP);
    pinMode(BTN2_PIN, INPUT_PULLUP);

    display.begin(i2c_Address, true);
}


void loop() {
    int encPos = getEncoderPos();
    boolean encBtnState = digitalRead(ENCODER_BTN_PIN);

    // Only update OLED screen if encoder is moved or pressed.
    if((encPos != encoderLastPosMain) || (encBtnState != encBtnPrevStateMain)) {
        handleOLEDDisplay();
    }
    
    if(comps[START_BTN_INDEX].selected) {
        runAutoCuttingStripping();
    }

    encoderLastPosMain = encPos;
    encBtnPrevStateMain = encBtnState;


    if(!digitalRead(BTN1_PIN)) {
        moveBlade(1);
    }
    if(!digitalRead(BTN2_PIN)) {
        moveBlade(-1);
    }    
}


void handleOLEDDisplay() {
    display.clearDisplay();

    drawWire();

    boolean btnState = digitalRead(ENCODER_BTN_PIN);

    // Handling whether encoder is changing cell or value of the cell.
    if(!btnState && (btnState != encBtnPrevState)) {
        encBtnState = !encBtnState;

        if(encBtnState) {
            encoderLastPos = encoderPos;
        } else {
            encoder.write(encoderLastPos * 4);
        }
    }
    
    encBtnPrevState = btnState;

    if (!encBtnState) {
        encoderPos = getEncoderPos();
    }

    handleAllComponents();
    
    display.display();
}


void drawWire() {
    display.drawLine(0, WIRE_STRAND_Y_POS, WIRE_STRAND_LENGTH, WIRE_STRAND_Y_POS, SH110X_WHITE);
    display.fillRect(WIRE_STRAND_LENGTH, 0, WIRE_INSULATION_WIDTH, WIRE_INSULATION_HEIGHT, SH110X_WHITE);
    display.drawLine(WIRE_STRAND_LENGTH+WIRE_INSULATION_WIDTH, WIRE_STRAND_Y_POS, SCREEN_WIDTH, WIRE_STRAND_Y_POS, SH110X_WHITE);
}


void handleAllComponents() {
    for(int i = 0; i < numOfComps; i++) {
        Component &comp = comps[i];
        
        if(encoderPos == i) {
            comp.highlighted = true;
            
            if(encBtnState) {
                if(!comp.selected && !comp.btn) {
                    encoder.write(comp.value * 4);
                }
                
                comp.selected = true;
                
                int newEncPos = getEncoderPos();
                comp.value = newEncPos;
                
            } else {
                comp.selected = false;
            }
            
        } else {
            comp.highlighted = false;
            comp.selected = false;
        }
        
        drawComponent(comp);
    }
}


void drawComponent(Component comp) {
    if(comp.highlighted) {
        display.setTextColor(SH110X_BLACK, SH110X_WHITE);
        display.fillRect(comp.x, comp.y, comp.w, comp.h, SH110X_WHITE);
        
        if (comp.selected) {
            display.drawRect(comp.x-1, comp.y-1, comp.w+2, comp.h+2, SH110X_WHITE);
        }
        
    } else {
        display.setTextColor(SH110X_WHITE, SH110X_BLACK);
        display.drawRect(comp.x, comp.y, comp.w, comp.h, SH110X_WHITE);
    }

    if(comp.btn) {
        display.setTextSize(1);
        drawText("Start", comp.x + TEXT_OFFSET, comp.y + TEXT_OFFSET);
    } else {
        display.setTextSize(TEXT_SIZE);
        drawText(String(comp.value), comp.x + TEXT_OFFSET, comp.y + TEXT_OFFSET);
    }
}


void drawText(String text, int x, int y) {
    display.setCursor(x, y);
    display.println(text);
}


void runAutoCuttingStripping() {
    if(CALIBRATION_MODE) {
        moveWire(comps[STRIPPING_LENGTH1_INDEX].value);
        cut();
    } else {
        cut();
        delay(DELAY_BETWEEN_CUTS);

        for(int i = 0; i < comps[QUANTITY_INDEX].value; i++) {
            moveWire(comps[STRIPPING_LENGTH1_INDEX].value);
            strip();
            moveWire(comps[WIRE_LENGTH_INDEX].value);
            strip();
            moveWire(comps[STRIPPING_LENGTH2_INDEX].value);
            cut();
            delay(DELAY_BETWEEN_CUTS);
        }   
    }

    comps[START_BTN_INDEX].selected = false;
    encBtnState = false;
}


void cut() {
    moveBlade(-CUTTING_STEPS);
    moveBlade(CUTTING_STEPS);
}


void strip() {
    moveBlade(-(comps[STRIPPING_DEPTH_INDEX].value * STRIPPING_MULTIPLIER));
    moveBlade(comps[STRIPPING_DEPTH_INDEX].value * STRIPPING_MULTIPLIER);
}


void moveBlade(int steps) {
    linMotSteppers.step(steps);
    linMotSteppersCurrStep += steps;
}


void moveWire(int steps) {
    extruderStepper.step(steps * WIRE_MOVEMENT_MULTI);
}


int getEncoderPos() {
    int encPos = encoder.read() / 4;
    return encPos;
}
