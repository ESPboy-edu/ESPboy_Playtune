#include "ESPboyPlaytune.h"

static const uint16_t midi_word_note_frequencies[128] PROGMEM = {
  16, 17, 18, 19, 21, 22, 23, 24, 26, 28, 29, 31,
  33, 35, 37, 39, 41, 44, 46, 49, 52, 55, 58, 62, 65,
  69, 73, 78, 82, 87, 92, 98, 104, 110, 117, 123, 131,
  139, 147, 156, 165, 175, 185, 196, 208, 220, 233, 247, 262,
  277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494, 523,
  554, 587, 622, 659, 698, 740, 784, 831, 880, 932, 988, 1047,
  1109, 1175, 1245, 1319, 1397, 1480, 1568, 1661, 1760, 1865, 1976, 2093,
  2217, 2349, 2489, 2637, 2794, 2960, 3136, 3322, 3520, 3729, 3951, 4186,
  4435, 4699, 4978, 5274, 5588, 5920, 6272, 6645, 7040, 7459, 7902, 8372,
  8870, 9397, 9956, 10548, 11175, 11840, 12544, 13290, 14080, 14917,
  15804, 16744, 17740, 18795, 19912, 21096, 22351, 23680, 25088
};

ESPboyPlaytune* ept_instance = NULL; //hacky way to attach a class member to interrupt

void ICACHE_RAM_ATTR eptGenISR()
{
  if (ept_instance != NULL) ept_instance->genISR();
}

ESPboyPlaytune::ESPboyPlaytune(boolean (*outEn)())
{
  outputEnabled = outEn;

  memset(genAcc, 0, sizeof(genAcc));
  memset(genAdd, 0, sizeof(genAdd));
  memset(genDur, 0, sizeof(genDur));

  genType = EPT_SYNTH_SQUARE;
  genOut = 0;
  genWidth = 128;

  songPlaying = false;
  songWait = 0;
}

void ESPboyPlaytune::initChannel(byte pin)
{
  noInterrupts();

  ept_instance = this;

  sigmaDeltaSetup(0, F_CPU / 256);
  sigmaDeltaAttachPin(EPT_SOUNDPIN);
  sigmaDeltaEnable();

  timer1_attachInterrupt(eptGenISR);
  timer1_enable(TIM_DIV1, TIM_EDGE, TIM_LOOP);
  timer1_write(ESP.getCpuFreqMHz() * 1000000 / EPT_SAMPLE_RATE);

  interrupts();
}

void ESPboyPlaytune::playNote(byte chan, byte note)
{
  if (chan >= EPT_CHANNELS || note > 127) return;

  uint32_t freq = pgm_read_word(midi_word_note_frequencies + note);

  genAcc[chan] = 0; //always reset phase
  genAdd[chan] = freq ? 0x100000000 / (EPT_SAMPLE_RATE / freq) : 0;
  genDur[chan] = 0xffffffff;
}

void ESPboyPlaytune::stopNote(byte chan)
{
  if (chan >= EPT_CHANNELS) return;

  genAcc[chan] = 0;
  genAdd[chan] = 0;
}

void ESPboyPlaytune::playScore(const byte *song_data)
{
  songData = song_data;
  songPtr = song_data;
  songRepeat = 0;

  stepSong();

  songPlaying = true;
}

void ESPboyPlaytune::stopScore()
{
  for (uint8_t chn = 0; chn < EPT_CHANNELS - 1; ++chn) stopNote(chn);	//stop all but last channel, which is used for tone()

  songPlaying = false;
}

boolean ESPboyPlaytune::playing()
{
  return songPlaying;
}

void ESPboyPlaytune::stepSong()
{
  while (1)
  {
    uint8_t cmd = pgm_read_byte(songPtr++);
    uint8_t op = cmd & 0xf0;
    uint8_t chan = cmd & 0x0f;

    if (op == EPT_OP_STOPNOTE)
    {
      stopNote(chan);
    }
    else if (op == EPT_OP_PLAYNOTE)
    {
      playNote(chan, pgm_read_byte(songPtr++));
    }
    else if (op < 0x80)
    {
      uint16_t duration = ((unsigned)cmd << 8) | (pgm_read_byte(songPtr++));

      songWait = EPT_SAMPLE_RATE * duration / 1000;

      break;
    }
    else if (op == EPT_OP_RESTART)
    {
      if ((chan == 0) || (songRepeat < chan))
      {
        songPtr = songData;
      }

      ++songRepeat;
    }
    else if (op == EPT_OP_STOP)
    {
      stopScore();
      break;
    }
  }
}

void ESPboyPlaytune::closeChannels()
{
  noInterrupts();

  stopScore();

  timer1_disable();
  sigmaDeltaDisable();

  interrupts();
}

void ESPboyPlaytune::tone(unsigned int freq, unsigned long duration)
{
  int chan = EPT_CHANNELS - 1;

  genAcc[chan] = 0;	//reset phase
  genAdd[chan] = freq ? 0x100000000 / (EPT_SAMPLE_RATE / freq) : 0;;
  genDur[chan] = EPT_SAMPLE_RATE * duration / 1000;
}

void ESPboyPlaytune::toneMutesScore(boolean mute)
{
}

void ESPboyPlaytune::setSynth(byte type, int param)
{
  genType = type;
  genWidth = param;
}

void ESPboyPlaytune::genISR()
{
  switch (genType)
  {
    case EPT_SYNTH_SQUARE:
      {
        sigmaDeltaWrite(0, genOut);

        genOut = 0;

        uint32_t genDuty = genWidth << 24;

        for (int chn = 0; chn < EPT_CHANNELS; ++chn)
        {
          if (genDur[chn])
          {
            if (genDur[chn] < 0xffffffff) --genDur[chn];

            genAcc[chn] += genAdd[chn];

            if (genAcc[chn] > genDuty) genOut += 128 - (EPT_CHANNELS * 8);
          }
        }

        if (genOut > 255) genOut = 255;
      }
      break;

    case EPT_SYNTH_PIN:
      {
        sigmaDeltaWrite(0, genOut ? 128 : 0);

        if (genOut) --genOut;

        for (int chn = 0; chn < EPT_CHANNELS; ++chn)
        {
          if (genDur[chn])
          {
            if (genDur[chn] < 0xffffffff) --genDur[chn];

            uint32_t prev = genAcc[chn];

            genAcc[chn] += genAdd[chn];

            if (genAcc[chn] < prev) genOut = genWidth;
          }
        }
      }
      break;
  }

  if (songPlaying)
  {
    if (songWait) --songWait;

    if (!songWait) stepSong();
  }
}
