#include "daisy_seed.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

// ------------------------------------------------------------
// structure d'un accord
// ------------------------------------------------------------
typedef struct
{
    float notes[3];
    int size;
} Chord;

// ------------------------------------------------------------
// horloges
// ------------------------------------------------------------
uint32_t note_counter  = 0;
uint32_t chord_counter = 0;
uint32_t drum_counter  = 0;

uint32_t samples_per_note  = 0;
uint32_t samples_per_chord = 0;
uint32_t samples_per_step  = 0;

// ------------------------------------------------------------
// hardware
// ------------------------------------------------------------
DaisySeed hw;

// ------------------------------------------------------------
// effets + sidechain
// ------------------------------------------------------------

// delay mélodie
DelayLine<float, 48000> delay_line;
float delay_feedback = 0.35f;
float delay_mix      = 0.25f;

// filtre svf mélodie
Svf melody_filter;

// lfo cutoff mélodie
Oscillator lfo;

// lfo volume accords
Oscillator chord_lfo;
float chord_lfo_depth = 0.4f;

// filtre global
OnePole lp_filter;

// sidechain
float sidechain_amount = 0.6f;
float sidechain_env    = 0.0f;

// ------------------------------------------------------------
// oscillateurs & enveloppes
// ------------------------------------------------------------

// mélodie
Oscillator osc;
AdEnv env;

// accords
Oscillator chord_osc1, chord_osc2, chord_osc3;
AdEnv chord_env;

// drums : kick
Oscillator kick_osc;
AdEnv kick_env;

// drums : hihat
WhiteNoise noise;
Svf hihat_filter;
AdEnv hihat_env;

// ------------------------------------------------------------
// progression d'accords
// ------------------------------------------------------------
Chord chords[] =
{
    {{220.0f, 261.63f, 329.63f}, 3},
    {{261.63f, 329.63f, 392.0f}, 3},
    {{196.0f, 246.94f, 293.66f}, 3}
};

const int chord_count = 3;
int current_chord = 0;

// ------------------------------------------------------------
// audio callback
// ------------------------------------------------------------
void AudioCallback(AudioHandle::InterleavingInputBuffer in,
                   AudioHandle::InterleavingOutputBuffer out,
                   size_t size)
{
    float melody_volume = 0.05f;
    float chord_volume  = 0.045f;
    float drum_volume   = 0.6f;

    for(size_t i = 0; i < size; i += 2)
    {
        // ================== mélodie ==================
        float melody = osc.Process() * env.Process();

        float cutoff = 800.0f + lfo.Process() * 600.0f;
        melody_filter.SetFreq(cutoff);
        melody_filter.Process(melody);
        melody = melody_filter.Low();

        float delayed = delay_line.Read();
        delay_line.Write(melody + delayed * delay_feedback);
        melody = melody * (1.0f - delay_mix) + delayed * delay_mix;
        melody *= melody_volume;

        // ================== sidechain ==================
        float melody_level = fabsf(melody);
        float attack  = 0.01f;
        float release = 0.0005f;

        if(melody_level > sidechain_env)
            sidechain_env += attack * (melody_level - sidechain_env);
        else
            sidechain_env += release * (melody_level - sidechain_env);

        float sidechain_gain = 1.0f - sidechain_env * sidechain_amount;
        if(sidechain_gain < 0.0f)
            sidechain_gain = 0.0f;

        // ================== accords ==================
        float chord_sig =
            chord_osc1.Process() +
            chord_osc2.Process() +
            chord_osc3.Process();

        chord_sig *= chord_env.Process();

        float lfo_val = (chord_lfo.Process() + 1.0f) * 0.5f;
        float modulated_chord_volume =
            chord_volume * (1.0f - chord_lfo_depth + lfo_val * chord_lfo_depth);

        chord_sig *= modulated_chord_volume * sidechain_gain;

        // ================== drums ==================
        float drums = 0.0f;

        // kick
        float kick_pitch = 50.0f + kick_env.Process() * 90.0f;
        kick_osc.SetFreq(kick_pitch);
        drums += kick_osc.Process() * kick_env.Process();

        // hihat
        float hh = noise.Process();
        hihat_filter.Process(hh);
        hh = hihat_filter.High();
        drums += hh * hihat_env.Process() * 0.3f;

        drums *= drum_volume;

        // ================== mix final ==================
        float sig = melody + chord_sig + drums;
        sig = lp_filter.Process(sig);

        out[i]     = sig;
        out[i + 1] = sig;

        // ================== horloge mélodie ==================
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

        // ================== horloge accords ==================
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

        // ================== horloge drums ==================
        drum_counter++;
        
        // hihat sur contretemps (mi-step)
        if(drum_counter == samples_per_step / 2)
        {
            hihat_env.Trigger();
        }

        // kick sur chaque temps
        if(drum_counter >= samples_per_step)
        {
            drum_counter = 0;

            // kick à chaque temps
            kick_env.Trigger();
        }
    }
}

// ------------------------------------------------------------
// main
// ------------------------------------------------------------
int main(void)
{
    hw.Configure();
    hw.Init();

    float sr = hw.AudioSampleRate();
    samples_per_note  = sr * 0.25f;
    samples_per_chord = sr * 4.0f;
    samples_per_step  = sr * 0.5f; // ~120 bpm

    // ================== init mélodie ==================
    osc.Init(sr);
    osc.SetWaveform(Oscillator::WAVE_SAW);

    env.Init(sr);
    env.SetTime(ADENV_SEG_ATTACK, 0.5f);
    env.SetTime(ADENV_SEG_DECAY, 3.0f);

    // ================== init accords ==================
    chord_osc1.Init(sr); chord_osc1.SetWaveform(Oscillator::WAVE_SAW);
    chord_osc2.Init(sr); chord_osc2.SetWaveform(Oscillator::WAVE_SAW);
    chord_osc3.Init(sr); chord_osc3.SetWaveform(Oscillator::WAVE_SAW);

    chord_env.Init(sr);
    chord_env.SetTime(ADENV_SEG_ATTACK, 2.0f);
    chord_env.SetTime(ADENV_SEG_DECAY, 6.0f);

    // ================== init drums ==================
    kick_osc.Init(sr);
    kick_osc.SetWaveform(Oscillator::WAVE_SIN);

    kick_env.Init(sr);
    kick_env.SetTime(ADENV_SEG_ATTACK, 0.001f);
    kick_env.SetTime(ADENV_SEG_DECAY, 0.25f);

    noise.Init();

    hihat_filter.Init(sr);
    hihat_filter.SetFreq(8000.0f);
    hihat_filter.SetRes(0.7f);

    hihat_env.Init(sr);
    hihat_env.SetTime(ADENV_SEG_ATTACK, 0.001f);
    hihat_env.SetTime(ADENV_SEG_DECAY, 0.08f);

    // ================== init effets ==================
    delay_line.Init();
    delay_line.SetDelay(sr * 0.35f);

    melody_filter.Init(sr);
    melody_filter.SetRes(0.6f);

    lfo.Init(sr);
    lfo.SetWaveform(Oscillator::WAVE_SIN);
    lfo.SetFreq(0.15f);

    chord_lfo.Init(sr);
    chord_lfo.SetWaveform(Oscillator::WAVE_SIN);
    chord_lfo.SetFreq(0.03f);

    lp_filter.Init();
    lp_filter.SetFilterMode(OnePole::FILTER_MODE_LOW_PASS);
    lp_filter.SetFrequency(400.0f / sr);

    hw.StartAudio(AudioCallback);

    while(1)
    {
        System::Delay(1000);
    }
}