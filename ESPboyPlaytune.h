/*
  Derived from:
  ArduboyPlaytune:
  https://github.com/ESPboy/ArduboyPlaytune
  Playtune: An Arduino tune player library
  https://github.com/LenShustek/arduino-playtune

  Modified to work well with the ESPboy game system
  https://www.ESPboy.com/

  The MIT License (MIT), see the referenced libraries
*/

#ifndef ESPBOY_PLAYTUNE_H
#define ESPBOY_PLAYTUNE_H

#include <ESP8266WiFi.h>
#include <sigma_delta.h>

#define EPT_CHANNELS	    8       //max number of sound channels, last channel also used for tone

#define EPT_SOUNDPIN      D3      //sound pin
#define EPT_SAMPLE_RATE   64000   //higher rate for better sound quality, but increases CPU load

#define EPT_OP_PLAYNOTE   0x90  	//play a note: low nibble is generator #, note is next byte
#define EPT_OP_STOPNOTE   0x80  	//stop a note: low nibble is generator #
#define EPT_OP_RESTART    0xe0  	//restart the score from the beginning
#define EPT_OP_STOP       0xf0  	//stop playing

enum {
  EPT_SYNTH_SQUARE = 0,
  EPT_SYNTH_PIN,
};


class ESPboyPlaytune
{
  public:

    ESPboyPlaytune(boolean (*outEn)());

    void initChannel(byte pin);

    void playScore(const byte *score);

    void stopScore();

    void closeChannels();

    boolean playing();

    void tone(unsigned int frequency, unsigned long duration);

    void toneMutesScore(boolean mute);

    void setSynth(byte type, int param);

    void genISR();

  private:

    void playNote(byte chan, byte note);

    void stopNote(byte chan);

    void stepSong();

    boolean (*outputEnabled)();

    uint16_t songWait;
    boolean songPlaying;
    const uint8_t* songData;
    const uint8_t* songPtr;
    uint8_t songRepeat;

    uint32_t genAcc[EPT_CHANNELS];
    uint32_t genAdd[EPT_CHANNELS];
    uint32_t genDur[EPT_CHANNELS];
    uint16_t genOut;
    uint16_t genType;
    uint16_t genWidth;
};

#endif
