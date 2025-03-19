#include <MIDI.h>
#include <EEPROM.h>

MIDI_CREATE_DEFAULT_INSTANCE();
#define VERSION 0xdeafbea4
#define MIDI_CHANNEL 1

typedef enum {
  BUTTON_CLEAN = 0,
  BUTTON_CRUNCH,
  BUTTON_OD1,
  BUTTON_OD2,
  BUTTON_MASTER,
  BUTTON_FXLOOP
} ButtonType;

typedef enum {
  CHANNEL_CLEAN = 0,
  CHANNEL_CRUNCH,
  CHANNEL_OD1,
  CHANNEL_OD2,
} ChannelType;

typedef enum {
  LED_CLEAN = 0,
  LED_CRUNCH,
  LED_OD1,
  LED_OD2,
  LED_MASTER1,
  LED_MASTER2,
  LED_FXLOOP
} LedType;

struct settings_t {
  settings_t()
    : version(VERSION), channel(MIDI_CHANNEL){};
  settings_t(uint8_t channel)
    : version(VERSION), channel(channel){};
  uint32_t version;
  uint8_t channel;
  uint8_t reserved1;
  uint16_t reserved2;
  uint32_t reserved3;
};

struct button_t {
  button_t(ButtonType type, uint8_t pin, uint32_t millis, bool state)
    : type(type), pin(pin), lastMillis(millis), lastState(state){};
  ButtonType type;
  uint8_t pin;
  uint32_t lastMillis;
  bool lastState;
};

struct channel_t {
  channel_t(ChannelType type, uint8_t midival, bool selected, bool fxloop, bool master)
    : type(type), midiValue(midival), selected(selected), fxloop(fxloop), master(master){};
  ChannelType type;
  uint8_t midiValue;
  bool selected;
  bool fxloop;
  bool master;  //true for master 1, false for master 2
};

struct led_t {
  led_t(LedType type, uint8_t pin)
    : type(type), pin(pin){};
  LedType type;
  uint8_t pin;
};

// globals
static struct settings_t settings = settings_t();
static struct channel_t *currentchannel = nullptr;

//store the amp channel data at an offset in eeprom
//this offset must be larger than settings_t size in bytes
#define SETTINGS_MEM_OFFSET 0
#define CHANNELS_MEM_OFFSET sizeof(struct settings_t)


static struct button_t buttons[] = {
  button_t(BUTTON_CLEAN, 2, 0, HIGH),   // D2
  button_t(BUTTON_CRUNCH, 3, 0, HIGH),  // D3
  button_t(BUTTON_OD1, 4, 0, HIGH),     // D4
  button_t(BUTTON_OD2, 5, 0, HIGH),     // D5
  button_t(BUTTON_MASTER, 6, 0, HIGH),  // D6
  button_t(BUTTON_FXLOOP, 7, 0, HIGH),  // D7
};

static struct channel_t channels[] = {
  channel_t(CHANNEL_CLEAN, 0, true, false, true),  // Selected by default if there is no data in the EEPROM
  channel_t(CHANNEL_CRUNCH, 1, false, false, true),
  channel_t(CHANNEL_OD1, 2, false, false, true),
  channel_t(CHANNEL_OD2, 3, false, false, true),
};

static struct led_t leds[] = {
  led_t(LED_CLEAN, A0),
  led_t(LED_CRUNCH, A1),
  led_t(LED_OD1, A2),
  led_t(LED_OD2, A3),
  led_t(LED_FXLOOP, A4),
  led_t(LED_MASTER1, A5),
  led_t(LED_MASTER2, 8),  // D8, since nano only allows A0-A5 as digital outputs
};

void sendChannel(struct channel_t *channel)
{
  MIDI.sendProgramChange(channel->midiValue, settings.channel);
}

void sendMaster(bool on)
{
  MIDI.sendControlChange(14, (on) ? 0 : 1, settings.channel);
}

void sendFxLoop(bool on)
{
  MIDI.sendControlChange(13, (on) ? 1 : 0, settings.channel);
}

void syncAmp()
{
  delay(1000);
  for (const struct channel_t &channel : channels) {
    sendChannel(&channel);
    updateLedsForChannel(&channel);
    delay(300);
    // set master 1
    sendMaster(true);
    updateLedsForChannel(&channel);
    delay(300);
    // set fx loop off
    sendFxLoop(false);
    updateLedsForChannel(&channel);
    delay(300);
  }
}

void blinknumber(int num, int millis)
{
  digitalWrite(LED_BUILTIN, LOW);
  for (int i = 0; i < num; i++) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(millis);
    digitalWrite(LED_BUILTIN, LOW);
    delay(millis);
  }
  delay(1000);
}

void blinkAllLeds(unsigned long blinkmillis)
{
  for (const struct led_t &led : leds) {
    setLed(&led, true);
  }
  delay(blinkmillis);
  for (const struct led_t &led : leds) {
    setLed(&led, false);
  }
}

void ledTest()
{
  for (const struct led_t &led : leds) {
    digitalWrite(led.pin, HIGH);
    delay(500);
    digitalWrite(led.pin, LOW);
  }
}

void midiTest()
{
  uint8_t val = HIGH;
  pinMode(leds[3].pin, OUTPUT);
  uint8_t channel = 1;
  while(true) {
    MIDI.sendControlChange(/*controlnumber=*/14, /*value=*/50, /*channel=*/1);
    MIDI.sendProgramChange(/*programnum=*/12, /*channel=*/1);
    if (channel == 16) { channel = 1; }
    digitalWrite(leds[0].pin, val);
    val = (val == HIGH) ? LOW : HIGH;
    delay(1000);
  }
}

void setup()
{
  for (const struct button_t &button : buttons) {
    pinMode(button.pin, INPUT_PULLUP);
  }

  for (const struct led_t &led : leds) {
    pinMode(led.pin, OUTPUT);
    digitalWrite(led.pin, LOW);
  }

  MIDI.begin(MIDI_CHANNEL);

  // holding master button on power on initializes reset
  if (isButtonPressedWithoutBounce(&buttons[BUTTON_MASTER])) {
    clearEeprom();
    blinkAllLeds(700);
    syncAmp();
    blinkAllLeds(700);
    // select CHANNEL_CLEAN
    currentchannel = &channels[0];
    saveToEeprom();
  }

  // read settings from eeprom into settings and channels
  loadFromEeprom();

  // Load the channels
  for (const struct channel_t &channel : channels) {
    if (channel.selected) {
      currentchannel = &channel;
    }
  }
  // if no channel is selected, something went wrong
  // user should reset the eeprom and try again
  // This should never happen since the default channel initialization selects CHANNEL_CLEAN.
  if (currentchannel == nullptr) {
    while (true) {
      blinkAllLeds(500);
      delay(500);
    }
  }
  sendChannel(currentchannel);
  updateLedsForChannel(currentchannel);
}

bool isButtonPressedWithoutBounce(struct button_t *button)
{
  return (digitalRead(button->pin) == LOW) ? true : false;
}

bool isButtonPressedWithBounce(struct button_t *button)
{
  // Debounce logic from https://forum.arduino.cc/t/5-button-marshall-dsl40cr-footswtich-controller/531856/2
  // Read and debounce button, return true on falling edge
  const static unsigned long debounceTime = 30;
  const static int8_t rising = HIGH - LOW;
  const static int8_t falling = LOW - HIGH;
  bool pressed = false;
  // read the button's current state
  bool state = digitalRead(button->pin);
  // calculate the state change since last time
  int8_t stateChange = state - button->lastState;

  //  if (state == LOW && button->lastMillis == 0) {
  //    // button first pressed, start debounce period
  //    button->lastMillis = millis();
  //  } else if (state == LOW && button->lastMillis != 0) {
  //    // in debounce period. See if button has been low longer than the debounce period
  //    if (millis() > (button->lastMillis + debounceTime)) {
  //      pressed = true;
  //    }
  //  } else if (state == HIGH) {
  //    if (millis() > (button->lastMillis + debounceTime)) {
  //      // state is high, and we're past the debounce period
  //      // button was released
  //      button->lastMillis = 0;
  //    } else {
  //      // bounce during the debounce period .. do nothing
  //    }
  //  }
  //  return pressed;

  //  // If the button is pressed (went from high to low)
  //  if (stateChange == falling) {
  //    // check if the time since the last bounce is higher than the threshold
  //    if (millis() - button->lastMillis > debounceTime) {
  //      pressed = true; // the button is pressed
  //    }
  //  } else if (stateChange == rising) {
  //    // if the button is released or bounces
  //    // remember when this happened
  //    button->lastMillis = millis();
  //  }
  //  // remember the current state
  //  button->lastState = state;
  //  // return true if the button was pressed and didn't bounce
  //  return pressed;

  //  button states
  //  -------------
  //  button->state = high;
  //  button->millis = 0;
  //
  //  button->state = low;
  //  button->millis = X;
  //
  //  button->state = low/high;
  //  button->millis = X + variable;
  //
  //  button->state = low;
  //  button->millis > debounce;
  //
  //  button->state = high;
  //  button->millis > debounce; // reset to button->millis = 0 (back to beginning state)

  if (state != button->lastState) {
    // button pushed or bouncing, reset the counter each time it happens and save
    // the last state
    button->lastMillis = millis();
    button->lastState = state;
  } else if (state == LOW && (millis() > (button->lastMillis + debounceTime))) {
    if (button->lastMillis != 0) {
      pressed = true;
    }
    // only register the first time it's out of the debounce period
    button->lastMillis = 0;
  } else if (state == HIGH && (millis() > (button->lastMillis + debounceTime))) {
    // button sitting idle high, reset the counter
    button->lastMillis = 0;
  }
  return pressed;
}

button_t *getPressedButton()
{
  for (const struct button_t &button : buttons) {
    if (isButtonPressedWithBounce(&button)) {
      return &button;
    }
  }
  return nullptr;
}

void setLed(struct led_t *led, bool on)
{
  digitalWrite(led->pin, (on) ? HIGH : LOW);
}

void updateLedsForChannel(struct channel_t *channel)
{
  for (const struct led_t &led : leds) {
    // hacky
    switch (led.type) {
      case LED_CLEAN:
        setLed(&led, channel->type == CHANNEL_CLEAN);
        break;
      case LED_CRUNCH:
        setLed(&led, channel->type == CHANNEL_CRUNCH);
        break;
      case LED_OD1:
        setLed(&led, channel->type == CHANNEL_OD1);
        break;
      case LED_OD2:
        setLed(&led, channel->type == CHANNEL_OD2);
        break;
      case LED_FXLOOP:
        setLed(&led, channel->fxloop);
        break;
      case LED_MASTER1:
        setLed(&led, channel->master);
        break;
      case LED_MASTER2:
        setLed(&led, !channel->master);
        break;
      default:
        setLed(&led, false);
    }
  }
}

bool loadSettingsFromEeprom()
{
  struct settings_t eeprom_data;
  eeprom_read_block(&eeprom_data, (void *)SETTINGS_MEM_OFFSET, sizeof(settings));

  if (eeprom_data.version != VERSION) {
    return false;
  }
  settings = eeprom_data;
  return true;
}

void loadChannelsFromEeprom()
{
  for (int i = 0; i < sizeof(channels) / sizeof(channel_t); i++) {
    eeprom_read_block(&channels[i], (void *)(CHANNELS_MEM_OFFSET + (sizeof(struct channel_t) * i)), sizeof(struct channel_t));
  }
}

void loadFromEeprom()
{
  // read settings from EEPROM
  if (loadSettingsFromEeprom()) {
    // read channels from EEPROM
    loadChannelsFromEeprom();
  }
}

void saveSettingsToEeprom()
{
  eeprom_write_block(&settings, (void *)SETTINGS_MEM_OFFSET, sizeof(struct settings_t));
}

void saveChannelsToEeprom()
{
  for (int i = 0; i < sizeof(channels) / sizeof(channel_t); i++) {
    eeprom_write_block(&channels[i], (void *)(CHANNELS_MEM_OFFSET + (sizeof(struct channel_t) * i)), sizeof(struct channel_t));
  }
}

void clearEeprom()
{
  bool ledstate = false;
  for (int i = 0; i < EEPROM.length(); i++) {
    EEPROM.write(i, 0);
    if (i % 4 == 0) {
      ledstate = !ledstate;
      setLed(&leds[0], ledstate);
    }
  }
}

void saveToEeprom()
{
  // write settings to EEPROM
  saveSettingsToEeprom();

  // write channels to EEPROM
  saveChannelsToEeprom();
}

void selectChannel(ChannelType type)
{
  // same channel selected, don't do anything
  if (type == currentchannel->type) {
    return;
  }
  for (struct channel_t &channel : channels) {
    if (channel.type == type) {
      channel.selected = true;
      currentchannel->selected = false;
      currentchannel = &channel;
    }
  }
  updateLedsForChannel(currentchannel);
  sendChannel(currentchannel);
  saveChannelsToEeprom();
}

void selectMaster()
{
  currentchannel->master = !currentchannel->master;
  sendMaster(currentchannel->master);
  updateLedsForChannel(currentchannel);
  saveToEeprom();
}

void toggleFxLoop()
{
  currentchannel->fxloop = !currentchannel->fxloop;
  sendFxLoop(currentchannel->fxloop);
  updateLedsForChannel(currentchannel);
  saveToEeprom();
}

void handleButtonPress(struct button_t *button)
{
  switch (button->type) {
    case BUTTON_CLEAN:
      selectChannel(CHANNEL_CLEAN);
      break;
    case BUTTON_CRUNCH:
      selectChannel(CHANNEL_CRUNCH);
      break;
    case BUTTON_OD1:
      selectChannel(CHANNEL_OD1);
      break;
    case BUTTON_OD2:
      selectChannel(CHANNEL_OD2);
      break;
    case BUTTON_MASTER:
      selectMaster();
      break;
    case BUTTON_FXLOOP:
      toggleFxLoop();
      break;
    default:
      break;
  }
}

void loop()
{
  struct button_t *button = getPressedButton();
  if (button != nullptr) {
    handleButtonPress(button);
  }
  delay(5);
}