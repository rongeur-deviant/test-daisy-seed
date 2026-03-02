#include "daisy_seed.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

/*
===============================================================================
projet : générateur musical autonome avec daisy seed
-------------------------------------------------------------------------------
ce script génère en temps réel :

- une mélodie aléatoire basée sur une gamme
- des accords évolutifs
- une section rythmique (kick / snare / hihat)
- des effets aléatoires (tremolo, detune, delay)
- un filtre master contrôlé par potentiomètre

le système fonctionne en audio temps réel via un callback.
===============================================================================
*/


// ==============================
// initialisation du hardware
// ==============================

DaisySeed hw;              // carte daisy seed (accès adc, audio, gpio...)
Switch scene_button;       // bouton de changement de scène musicale
bool last_pressed = false; // mémorisation de l'état précédent du bouton


// ==============================
// variables de tempo / horloges
// ==============================

/*
le système ne possède pas de "timer musical" classique.
on utilise donc des compteurs en nombre d'échantillons audio.

comme l'audio est traité sample par sample,
on peut déclencher des événements musicaux
en comptant le nombre d'échantillons écoulés.
*/

uint32_t note_counter = 0;     // déclenchement des notes
uint32_t chord_counter = 0;    // déclenchement des accords
uint32_t drum_counter = 0;     // séquence batterie
uint32_t bpm_counter = 0;      // mise à jour lente du bpm

uint32_t samples_per_note = 0;
uint32_t samples_per_chord = 0;
uint32_t samples_per_step = 0;

float bpm_smooth = 110.0f; // bpm lissé pour éviter les changements brusques


// ==============================
// contrôle énergie globale
// ==============================

/*
energy agit comme un "volume musical intelligent".
elle réduit la dynamique globale pour éviter la saturation.
*/

float energy = 1.0f;


// ==============================
// delay randomisé
// ==============================

/*
delay_line est un buffer circulaire.
on peut lire un ancien échantillon (delay)
et réécrire dedans avec un feedback.
*/

bool delay_enabled = false;
float delay_feedback = 0.35f;
float delay_mix = 0.25f;

DelayLine<float, 48000> delay_line; // 48000 samples max ≈ 1 seconde à 48khz


// ==============================
// filtre global (pot 2)
// ==============================

/*
svf = state variable filter

on utilise la sortie passe-bas pour contrôler la brillance globale.
le potentiomètre 2 agit sur la fréquence de coupure.
*/

Svf master_filter;


// ==============================
// gestion gamme musicale
// ==============================

/*
les gammes sont définies en demi-tons.
ex : 0 = fondamentale, 7 = quinte, etc.
*/

float root_freq = 220.0f; // fondamentale
int* current_scale;       // pointeur vers la gamme active
int scale_size;           // taille de la gamme

int scale_major[]  = {0,2,4,5,7,9,11};
int scale_minor[]  = {0,2,3,5,7,8,10};
int scale_dorian[] = {0,2,3,5,7,9,10};
int scale_penta[]  = {0,3,5,7,10};


// ==============================
// accords
// ==============================

/*
4 accords sont générés.
chaque accord contient 3 notes (triade).
*/

float chord_notes[4][3];
float chord_freq1, chord_freq2, chord_freq3;
int current_chord = 0;


// ==============================
// patterns batterie
// ==============================

/*
chaque pattern contient 4 pas (4/4 simplifié).
true = déclenchement
*/

bool kick_pattern[4];
bool snare_pattern[4];
bool hihat_pattern[4];


// ==============================
// effets mélodiques
// ==============================

/*
fx_lfo = oscillateur basse fréquence utilisé pour le tremolo.
*/

int melodic_fx_mode = 0;
Oscillator fx_lfo;

float tremolo_depth = 0.0f;
float detune_amount = 0.0f;


// ==============================
// oscillateurs mélodie
// ==============================

Oscillator osc;   // oscillateur principal
AdEnv env;        // enveloppe attaque/déclin


// ==============================
// oscillateurs accords
// ==============================

Oscillator chord_osc1, chord_osc2, chord_osc3;
AdEnv chord_env;


// ==============================
// section batterie
// ==============================

Oscillator kick_osc;
AdEnv kick_env;

WhiteNoise noise;      // bruit blanc pour snare & hihat
Svf hihat_filter;
AdEnv hihat_env;
Svf snare_filter;
AdEnv snare_env;


// ==============================
// conversion demi-ton --> fréquence
// ==============================

float SemiToFreq(float root, int semi)
{
    // formule tempérée : f = root * 2^(semi/12)
    return root * powf(2.0f, semi / 12.0f);
}


// ==============================
// génération des accords
// ==============================

void GenerateChords()
{
    /*
    pour chaque accord :
    on choisit une note aléatoire dans la gamme
    puis on construit une triade (note + tierce + quinte)
    */

    for(int i = 0; i < 4; i++)
    {
        int d = rand() % scale_size;

        chord_notes[i][0] = SemiToFreq(root_freq, current_scale[d]);
        chord_notes[i][1] = SemiToFreq(root_freq, current_scale[(d+2)%scale_size]);
        chord_notes[i][2] = SemiToFreq(root_freq, current_scale[(d+4)%scale_size]);
    }
}


// ==============================
// génération pattern batterie
// ==============================

void GenerateDrumPattern()
{
    /*
    génération semi-aléatoire avec probabilités fixes.
    */

    for(int i=0;i<4;i++)
    {
        kick_pattern[i]  = (rand()%100 < 60);
        snare_pattern[i] = (rand()%100 < 50);
        hihat_pattern[i] = (rand()%100 < 70);
    }

    // structure minimale stable
    kick_pattern[0] = true;
    snare_pattern[2] = true;
}


// ==============================
// randomisation complète d'une scène
// ==============================

void RandomizeScene()
{
    /*
    change :
    - fondamentale
    - gamme
    - accords
    - pattern batterie
    - effets
    */

    float roots[] = {110,130.81f,146.83f,174.61f,196};
    root_freq = roots[rand()%5];

    int sc = rand()%4;
    if(sc==0){ current_scale=scale_major; scale_size=7; }
    if(sc==1){ current_scale=scale_minor; scale_size=7; }
    if(sc==2){ current_scale=scale_dorian; scale_size=7; }
    if(sc==3){ current_scale=scale_penta; scale_size=5; }

    GenerateChords();
    GenerateDrumPattern();

    melodic_fx_mode = rand()%2;

    tremolo_depth = 0.2f + (rand()%30)/100.0f;

    delay_enabled = (rand()%2);
}


// ==============================
// callback audio (coeur du système)
// ==============================

void AudioCallback(AudioHandle::InterleavingInputBuffer in,
                   AudioHandle::InterleavingOutputBuffer out,
                   size_t size)
{
    float sr = hw.AudioSampleRate();

    /*
    cette fonction est appelée en boucle par le hardware audio.
    elle doit être ultra rapide.
    */

    for(size_t i=0;i<size;i+=2)
    {
        // gestion bouton scène
        scene_button.Debounce();
        bool pressed = scene_button.Pressed();
        if(pressed && !last_pressed)
            RandomizeScene();
        last_pressed = pressed;

        // lecture des potentiomètres
        float pot_bpm    = hw.adc.GetFloat(0);
        float pot_filter = hw.adc.GetFloat(1);
        float pot_energy = hw.adc.GetFloat(2);

        // calcul bpm
        float target = 40 + pot_bpm*75;
        bpm_smooth = 0.95f*bpm_smooth + 0.05f*target;

        energy = 0.1f + pot_energy * 0.5f;

        float seconds = 60.0f/bpm_smooth;

        samples_per_note  = sr*seconds*0.5f;
        samples_per_chord = sr*seconds*4.0f;

        master_filter.SetFreq(200.0f + pot_filter*6000.0f);
        master_filter.SetRes(0.7f);

        // -------- génération mélodie --------
        float melody = osc.Process()*env.Process()*0.5f;

        // -------- génération accords --------
        float chord_sig =
            chord_osc1.Process()+
            chord_osc2.Process()+
            chord_osc3.Process();

        chord_sig *= chord_env.Process()*0.4f;

        float melodic = melody + chord_sig;

        // -------- effet tremolo --------
        if(melodic_fx_mode==0)
        {
            float l = (fx_lfo.Process()+1)*0.5f;
            melodic *= (1.0f - tremolo_depth + l*tremolo_depth);
        }

        // -------- effet delay --------
        if(delay_enabled)
        {
            float delayed = delay_line.Read();
            delay_line.Write(melodic + delayed*delay_feedback);
            melodic = melodic*(1.0f-delay_mix) + delayed*delay_mix;
        }

        // -------- batterie --------
        float drums = 0.0f;

        kick_osc.SetFreq(50 + kick_env.Process()*90);
        drums += kick_osc.Process()*kick_env.Process();

        snare_filter.Process(noise.Process());
        drums += snare_filter.Band()*snare_env.Process()*0.6f;

        hihat_filter.Process(noise.Process());
        drums += hihat_filter.High()*hihat_env.Process()*0.3f;

        float sig = melodic + drums;

        // -------- filtre master --------
        master_filter.Process(sig);
        sig = master_filter.Low();

        // -------- saturation douce --------
        sig *= energy;
        sig = tanhf(sig*1.5f);

        out[i]=sig;
        out[i+1]=sig;

        // -------- déclenchement notes --------
        note_counter++;
        if(note_counter>=samples_per_note)
        {
            note_counter=0;
            osc.SetFreq(SemiToFreq(root_freq,current_scale[rand()%scale_size])*2);
            env.Trigger();
        }
    }
}


// ==============================
// main
// ==============================

int main(void)
{
    hw.Configure();
    hw.Init();

    float sr = hw.AudioSampleRate();

    scene_button.Init(hw.GetPin(28),1000);

    AdcChannelConfig adc[3];
    adc[0].InitSingle(hw.GetPin(15));
    adc[1].InitSingle(hw.GetPin(16));
    adc[2].InitSingle(hw.GetPin(17));

    hw.adc.Init(adc,3);
    hw.adc.Start();

    osc.Init(sr);
    env.Init(sr);

    chord_osc1.Init(sr);
    chord_osc2.Init(sr);
    chord_osc3.Init(sr);
    chord_env.Init(sr);

    kick_osc.Init(sr);
    kick_env.Init(sr);

    snare_env.Init(sr);
    noise.Init();

    hihat_filter.Init(sr);
    hihat_env.Init(sr);

    delay_line.Init();
    delay_line.SetDelay(sr*0.35f);

    master_filter.Init(sr);

    fx_lfo.Init(sr);
    fx_lfo.SetWaveform(Oscillator::WAVE_SIN);

    RandomizeScene(); // initialise une première scène

    hw.StartAudio(AudioCallback);

    while(1)
        System::Delay(1);
}