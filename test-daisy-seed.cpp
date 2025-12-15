#include "daisy_seed.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

// ------------------------------------------------------------
// STRUCTURE D'UN ACCORD
// ------------------------------------------------------------
typedef struct
{
    float notes[3];
    int size;
} Chord;

// ------------------------------------------------------------
// HORLOGES
// ------------------------------------------------------------
uint32_t note_counter  = 0;
uint32_t chord_counter = 0;

uint32_t samples_per_note  = 0;
uint32_t samples_per_chord = 0;

// ------------------------------------------------------------
// HARDWARE
// ------------------------------------------------------------
DaisySeed hw;

// ------------------------------------------------------------
// EFFETS
// ------------------------------------------------------------

// Delay mélodie
DelayLine<float, 48000> delay_line;
float delay_feedback = 0.35f;
float delay_mix      = 0.25f;

// Filtre SVF mélodie
Svf melody_filter;

// LFO pour cutoff
Oscillator lfo;

// Filtre global
OnePole lp_filter;

// ------------------------------------------------------------
// OSCILLATEURS & ENVELOPPES
// ------------------------------------------------------------

// Mélodie
Oscillator osc;
AdEnv env;

// Accords
Oscillator chord_osc1, chord_osc2, chord_osc3;
AdEnv chord_env;

// ------------------------------------------------------------
// PROGRESSION D'ACCORDS
// ------------------------------------------------------------
Chord chords[] =
{
    {{220.0f, 261.63f, 329.63f}, 3}, // A minor
    {{261.63f, 329.63f, 392.0f}, 3}, // C major
    {{196.0f, 246.94f, 293.66f}, 3}  // G major-ish
};

const int chord_count = 3;
int current_chord = 0;

// ------------------------------------------------------------
// AUDIO CALLBACK
// ------------------------------------------------------------
void AudioCallback(AudioHandle::InterleavingInputBuffer in,
                   AudioHandle::InterleavingOutputBuffer out,
                   size_t size)
{
    float melody_volume = 0.05f;
    float chord_volume  = 0.045f;

    for(size_t i = 0; i < size; i += 2)
    {
        // ================== MÉLODIE ==================
        float melody = osc.Process() * env.Process();

        // LFO -> cutoff
        float cutoff = 800.0f + lfo.Process() * 600.0f;
        melody_filter.SetFreq(cutoff);

        melody_filter.Process(melody);
        melody = melody_filter.Low();

        // delay
        float delayed = delay_line.Read();
        delay_line.Write(melody + delayed * delay_feedback);

        melody = melody * (1.0f - delay_mix) + delayed * delay_mix;
        melody *= melody_volume;

        // ================== ACCORDS ==================
        float chord_sig =
            chord_osc1.Process() +
            chord_osc2.Process() +
            chord_osc3.Process();

        chord_sig *= chord_env.Process() * chord_volume;

        // ================== MIX FINAL ==================
        float sig = melody + chord_sig;
        sig = lp_filter.Process(sig);

        out[i]     = sig;
        out[i + 1] = sig;

        // ================== HORLOGE MÉLODIE ==================
        note_counter++;
        if(note_counter >= samples_per_note)
        {
            note_counter = 0;
            if(rand() % 100 < 70)
            {
                Chord &c = chords[current_chord];
                osc.SetFreq(c.notes[rand() % c.size] * 2.0f);
                env.Trigger();
            }
        }

        // ================== HORLOGE ACCORDS ==================
        chord_counter++;
        if(chord_counter >= samples_per_chord)
        {
            chord_counter = 0;
            current_chord = rand() % chord_count;

            chord_osc1.SetFreq(chords[current_chord].notes[0]);
            chord_osc2.SetFreq(chords[current_chord].notes[1] * 1.005f);
            chord_osc3.SetFreq(chords[current_chord].notes[2] * 0.995f);

            chord_env.Trigger();
        }
    }
}

// ------------------------------------------------------------
// MAIN
// ------------------------------------------------------------
int main(void)
{
    hw.Configure();
    hw.Init();

    float sr = hw.AudioSampleRate();
    samples_per_note  = sr * 0.25f;
    samples_per_chord = sr * 4.0f;

    // ================== INIT MÉLODIE ==================
    osc.Init(sr);
    osc.SetWaveform(Oscillator::WAVE_SAW);
    osc.SetAmp(1.0f);

    env.Init(sr);
    env.SetTime(ADENV_SEG_ATTACK, 0.5f);
    env.SetTime(ADENV_SEG_DECAY, 3.0f);

    // ================== INIT ACCORDS ==================
    chord_osc1.Init(sr); chord_osc1.SetWaveform(Oscillator::WAVE_SAW);
    chord_osc2.Init(sr); chord_osc2.SetWaveform(Oscillator::WAVE_SAW);
    chord_osc3.Init(sr); chord_osc3.SetWaveform(Oscillator::WAVE_SAW);

    chord_env.Init(sr);
    chord_env.SetTime(ADENV_SEG_ATTACK, 2.0f);
    chord_env.SetTime(ADENV_SEG_DECAY, 6.0f);

    // ================== INIT EFFETS ==================
    delay_line.Init();
    delay_line.SetDelay(sr * 0.35f);

    melody_filter.Init(sr);
    melody_filter.SetRes(0.6f);

    lfo.Init(sr);
    lfo.SetWaveform(Oscillator::WAVE_SIN);
    lfo.SetFreq(0.15f);

    lp_filter.Init();
    lp_filter.SetFilterMode(OnePole::FILTER_MODE_LOW_PASS);
    lp_filter.SetFrequency(400.0f / sr);

    hw.StartAudio(AudioCallback);

    while(1)
    {
        System::Delay(1000);
    }
}