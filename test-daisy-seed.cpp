#include "daisy_seed.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

// ------------------------------------------------------------
// STRUCTURE D'UN ACCORD
// ------------------------------------------------------------
typedef struct
{
    float notes[3]; // 3 notes par accord
    int size;
} Chord;

// ------------------------------------------------------------
// HORLOGES
// ------------------------------------------------------------
uint32_t note_counter = 0;
uint32_t chord_counter = 0;

uint32_t samples_per_note  = 0;
uint32_t samples_per_chord = 0;

// ------------------------------------------------------------
// HARDWARE
// ------------------------------------------------------------
DaisySeed hw;

// ------------------------------------------------------------
// OSCILLATEURS & ENVELOPPES
// ------------------------------------------------------------

// mélodie
Oscillator osc;
AdEnv env;

// accords (3 oscillateurs)
Oscillator chord_osc1, chord_osc2, chord_osc3;
AdEnv chord_env;

// filtre passe-bas global (OnePole)
OnePole lp_filter;

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
// CALLBACK AUDIO
// ------------------------------------------------------------
void AudioCallback(AudioHandle::InterleavingInputBuffer in,
                   AudioHandle::InterleavingOutputBuffer out,
                   size_t size)
{
    float melody_volume = 0.03f; // très doux
    float chord_volume  = 0.08f; // pad planant

    for(size_t i = 0; i < size; i += 2)
    {
        // ---- mélodie ----
        float sig = osc.Process() * env.Process() * melody_volume;

        // ---- accords ----
        float chord_sig = chord_osc1.Process() + chord_osc2.Process() + chord_osc3.Process();
        chord_sig *= chord_env.Process() * chord_volume;

        // ---- mix total ----
        sig += chord_sig;

        // ---- filtre passe-bas global ----
        sig = lp_filter.Process(sig);

        // ---- sortie stéréo ----
        out[i] = out[i+1] = sig;

        // ---- horloge mélodie ----
        note_counter++;
        if(note_counter >= samples_per_note)
        {
            note_counter = 0;
            if(rand() % 100 < 70) // probabilité de jouer
            {
                Chord &c = chords[current_chord];
                int idx = rand() % c.size;
                osc.SetFreq(c.notes[idx] * 2.0f); // octave supérieure
                env.Trigger();
            }
        }

        // ---- horloge accord ----
        chord_counter++;
        if(chord_counter >= samples_per_chord)
        {
            chord_counter = 0;
            current_chord = rand() % chord_count;

            chord_osc1.SetFreq(chords[current_chord].notes[0]);
            chord_osc2.SetFreq(chords[current_chord].notes[1] * 1.005f); // léger désaccord
            chord_osc3.SetFreq(chords[current_chord].notes[2] * 0.995f); // léger désaccord
            chord_env.Trigger();
        }
    }
}

// ------------------------------------------------------------
// MAIN
// ------------------------------------------------------------
int main(void)
{
    // ---- initialisation hardware ----
    hw.Configure();
    hw.Init();

    float sr = hw.AudioSampleRate();
    samples_per_note  = sr * 0.25f; // note toutes les 250 ms
    samples_per_chord = sr * 4.0f;  // accord toutes les 4 s

    // ---- mélodie ----
    osc.Init(sr);
    osc.SetWaveform(Oscillator::WAVE_SAW);
    osc.SetAmp(1.0f);

    env.Init(sr);
    env.SetTime(ADENV_SEG_ATTACK, 0.5f); // attaque douce
    env.SetTime(ADENV_SEG_DECAY, 3.0f);  // décroissance longue

    // ---- accords ----
    chord_osc1.Init(sr); chord_osc1.SetWaveform(Oscillator::WAVE_SAW);
    chord_osc2.Init(sr); chord_osc2.SetWaveform(Oscillator::WAVE_SAW);
    chord_osc3.Init(sr); chord_osc3.SetWaveform(Oscillator::WAVE_SAW);

    chord_env.Init(sr);
    chord_env.SetTime(ADENV_SEG_ATTACK, 2.0f); // attaque progressive
    chord_env.SetTime(ADENV_SEG_DECAY, 6.0f);  // tenue longue

    // ---- filtre passe-bas global OnePole ----
    lp_filter.Init();
    lp_filter.SetFilterMode(OnePole::FILTER_MODE_LOW_PASS);

    float fc = 400.0f;
    lp_filter.SetFrequency(fc/sr);

    // ---- démarrer audio ----
    hw.StartAudio(AudioCallback);

    // ---- boucle infinie ----
    while(1)
    {
        System::Delay(1000);
    }
}