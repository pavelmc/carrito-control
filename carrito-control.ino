/*
 * Pin mapping
 *
 * *** NRF24L01 Radio ***
 * Radio    Arduino
 * CE    -> 9
 * CSN   -> 10 (Hardware SPI SS)
 * MOSI  -> 11 (Hardware SPI MOSI)
 * MISO  -> 12 (Hardware SPI MISO)
 * SCK   -> 13 (Hardware SPI SCK)
 * IRQ   -> No connection
 * VCC   -> No more than 3.6 volts
 * GND   -> GND
 *
 * *** Buttons ***
 * 2 - FWD
 * 5 - REV
 * 3 - LEFT
 * 4 - RIGHT
 *
 * ** Other buttons as analog ones ***
 * A4 - 10 buttons anlog muxed
 *
 * ** OPTION Buttons ***
 *
 *
 */

/***** NRF24L01 *****/
#include <SPI.h>
#include <NRFLite.h>

// Our radio's id
const static uint8_t RADIO_ID = 12;
// The transmitter will send to this id.
const static uint8_t DESTINATION_RADIO_ID = 18;
const static uint8_t PIN_RADIO_CE = 9;
const static uint8_t PIN_RADIO_CSN = 10;

/*************************************************************************
 * Radio data structure explained:
 *
 * We will pass just relative values, aka, increments
 *
 * -> Speed: (int16)
 *      motor speed increments, every tap increment it in a fixed val
 *      the same for decrements
 * -> Speed hold (bool)
 *      true if we are holding the actual speed, false if we must set
 *      the motor free (this means that we keep the FWD/REV key pressed)
 * -> Turn: (int16)
 *      Turn direction increments, every tap increment it in a fixed val
 *      the same for decrements
 * -> Turn hold (bool)
 *      True if we are holding any direction key.
 * -> Options; (unsigned long)
 *      really 4 bytes, first one is bit mapped to options as
 *      lights, siren, etc... the others will be worked out
*************************************************************************/
// Any packet up to 32 bytes can be sent.
struct RadioPacket {
    unsigned long serial = 0;
    int speed = 0;
    bool speed_hold = 0;
    int turn = 0;
    bool turn_hold = 0;
    bool stop = 0;
    bool lookAhead = 0;
    unsigned long options = 0;
};

// instantiate
NRFLite _radio;
RadioPacket _radioData;

// https://github.com/thomasfredericks/Bounce2
#include <Bounce2.h>

#define FWD     5   // working
#define REV     2   // working
#define LEFT    3   // working
#define RIGHT   4   // working

#define DEBOUNCE_INTERVAL 25  // ms
#define HELD_TIME 1000 // ms

Bounce B_fwd = Bounce();
Bounce B_rev = Bounce();
Bounce B_left = Bounce();
Bounce B_right = Bounce();

#include <BMux.h>
#define ANALOG_PIN A4
#define BUTTONS_COUNT 10
BMux abm;

// Adding the buttons the the instance
Button Bfront = Button(600, &lookAhead);
Button Bstop = Button(540, &stopMotion);


// Serial Debug
#define SDEBUG      0

/************************************************************************/

// constants
#define SPEEDINC         (5)      // increments of the speed
#define TURNINC          (6)      // increments of the turn angle

// Variables
int speed = 0;          // speed value to send
int turn = 0;           // turn value to send
bool needTC = 0;        // flag to sign we need to TX


//~ #define SERIAL_INPUT true


/************************************************************************/

// check for buttons state
void checkButtState() {
    B_fwd.update();
    B_rev.update();
    B_left.update();
    B_right.update();
}


// check FWD actions
void checkFWD() {
    // check HELD
    if (B_fwd.held() > HELD_TIME) {
        // no acceleration
        _radioData.speed = 0;
        // but we are held
        _radioData.speed_hold = 1;
        // reset held count
        B_fwd.durationOfPreviousState = 0;
    }

    // check click
    if (B_fwd.fell()) {
        // just a click
        _radioData.speed = SPEEDINC;
        // in any case we sign for a held off
        _radioData.speed_hold = 0;
    }

    // button was released for more than some time
    if (B_fwd.rose()) {
        // no action
        _radioData.speed = 0;
        // in any case we sign for a held off
        _radioData.speed_hold = 0;
    }
}


// check REV actions
void checkREV() {
    // check HELD
    if (B_rev.held()) {
        // no acceleration
        _radioData.speed = 0;
        // but we are held
        _radioData.speed_hold = 1;
        // reset held count
        B_rev.durationOfPreviousState = 0;
    }

    // check click
    if (B_rev.fell()) {
        // just a click
        _radioData.speed = int(0) - SPEEDINC;
        // in any case we sign for a held off
        _radioData.speed_hold = 0;
    }

    // button was released for more than some time
    if (B_rev.rose()) {
        // no action
        _radioData.speed = 0;
        // in any case we sign for a held off
        _radioData.speed_hold = 0;
    }
}


// check RIGHT actions
void checkRIGHT() {
    // check HELD
    if (B_right.held() > HELD_TIME) {
        // no acceleration
        _radioData.turn = 0;
        // but we are held
        _radioData.turn_hold = 1;
        // reset held count
        B_right.durationOfPreviousState = 0;
    }

    // check click
    if (B_right.fell()) {
        // just a click
        _radioData.turn = TURNINC;
        // in any case we sign for a held off
        _radioData.turn_hold = 0;
    }

    // button was released for more than some time
    if (B_right.rose()) {
        // no action
        _radioData.turn = 0;
        // in any case we sign for a held off
        _radioData.turn_hold = 0;
    }
}


// check LEFT actions
void checkLEFT() {
    // check HELD
    if (B_left.held() > HELD_TIME) {
        // no acceleration
        _radioData.turn = 0;
        // but we are held
        _radioData.turn_hold = 1;
        // reset held count
        B_left.durationOfPreviousState = 0;
    }

    // check click
    if (B_left.fell()) {
        // just a click
        _radioData.turn = int(0) - TURNINC;
        // in any case we sign for a held off
        _radioData.turn_hold = 0;
    }

    // button was released for more than some time
    if (B_left.rose()) {
        // no action
        _radioData.turn = 0;
        // in any case we sign for a held off
        _radioData.turn_hold = 0;
    }
}


// tx buffer changed
bool txBufferChanged() {
    // speed
    if (_radioData.speed != 0) {
        #ifdef SDEBUG
        Serial.println("!speed");
        #endif
        return true;
    }

    // speed hold
    if (_radioData.speed_hold != 0) {
        #ifdef SDEBUG
        Serial.println("!speed_hold");
        #endif
        return true;
    }

    // turn
    if (_radioData.turn != 0) {
        #ifdef SDEBUG
        Serial.println("!turn");
        #endif
        return true;
    }

    // turn hold
    if (_radioData.turn_hold != 0) {
        #ifdef SDEBUG
        Serial.println("!turn_hold");
        #endif
        return true;
    }

    // options
    if (_radioData.options != 0) {
        #ifdef SDEBUG
        Serial.println("!options");
        #endif
        return true;
    }

    // if you reached this point return false
    return false;
}


// check for changes
void checkChanges() {
    if (txBufferChanged()) {
        // TX the data
        if (txCommand()) {
            #ifdef SDEBUG
            Serial.println("GOT ACK ");
            #endif
        } else {
            #ifdef SDEBUG
            Serial.println("Fail !!!");
            #endif
        }
    }
}


// transmit
bool txCommand() {
    // DEBUG
    #ifdef SDEBUG
    Serial.print("TX DONE... ");
    #endif

    // set serial
    _radioData.serial = millis();

    // Note how '&' must be placed in front of the variable name.
    return _radio.send(DESTINATION_RADIO_ID, &_radioData, sizeof(_radioData));
}


// Move direction until weare facing forward
void lookAhead() {
    // serial debug
    #ifdef SDEBUG
    Serial.println("LookAhead");
    #endif

    // isntruct it to look ahead
    _radioData.lookAhead = 1;

    // transmit it ride away
    bool t = txCommand();

    // check if acknowledge
    if (t) { // success, it was received
        // set it back to zero
        _radioData.lookAhead = 0;
        // transmit it ride away
        bool t = txCommand();
    }
}


// Stop all motion
void stopMotion() {
    // serial debug
    #ifdef SDEBUG
    Serial.println("Stop Motor");
    #endif

    // stop motion
    _radioData.stop = 1;

    // transmit it ride away
    bool t = txCommand();

    // check if acknowledge
    if (t) { // success, it was received
        // set it back to zero
        _radioData.stop = 0;
        // transmit it ride away
        bool t = txCommand();
    }
}

/************************************************************************/


void setup () {
    // serial console
    #ifdef SDEBUG
    Serial.begin(9600);
    #endif

    // init the NRF radio
    /*****
     * By default, 'init' configures the radio to use a 2MBPS bitrate
     * on channel 100 (channels 0-125 are valid).
     *
     * Both the RX and TX radios must have the same bitrate and
     * channel to communicate with each other.
     *
     * You can assign a different bitrate and channel as shown
     * below.
     *
     * _radio.init(RADIO_ID, PIN_RADIO_CE, PIN_RADIO_CSN,
     *             NRFLite::BITRATE250KBPS, 0)
     * _radio.init(RADIO_ID, PIN_RADIO_CE, PIN_RADIO_CSN,
     *             NRFLite::BITRATE1MBPS, 75)
     * _radio.init(RADIO_ID, PIN_RADIO_CE, PIN_RADIO_CSN,
     *             NRFLite::BITRATE2MBPS, 100)
     *******/
    if (!_radio.init(RADIO_ID, PIN_RADIO_CE, PIN_RADIO_CSN, NRFLite::BITRATE250KBPS, 52)) {
        #ifdef SDEBUG
        Serial.println("Cannot communicate with radio");
        #endif
        while (1); // Wait here forever.
    }

    // Radio initialized ok
    #ifdef SDEBUG
    Serial.println("Radio init done.");
    #endif

    // setup debounce instances for the buttons
    B_fwd.attach(FWD, INPUT_PULLUP);
    B_rev.attach(REV, INPUT_PULLUP);
    B_left.attach(LEFT, INPUT_PULLUP);
    B_right.attach(RIGHT, INPUT_PULLUP);

    // set debounce intervals
    B_fwd.interval(DEBOUNCE_INTERVAL);
    B_rev.interval(DEBOUNCE_INTERVAL);
    B_left.interval(DEBOUNCE_INTERVAL);
    B_right.interval(DEBOUNCE_INTERVAL);

    // assign the function to the buttons
    abm.init(ANALOG_PIN, 5, 20);
    abm.add(Bfront);
    abm.add(Bstop);
}


void loop () {
    // Update the Bounce instances
    checkButtState();

    // check for changes
    checkChanges();

}
