/*
 * korinaudiokit.h -- the AudioKit functions MuseScore uses on Korin OS, resolved once from audiokit.so by rank
 * (src/kits/audiokit/audiokit.abi). Header only : the audio driver (korinaudiodriver.cpp) and the synthesizer
 * (fluidsynth.cpp under __armpi__ : AudioKit's GM synth and the system SoundFont, docs/etude/MUSESCORE.md).
 *
 * Copyright (c) 2026 Stéphane Wegener. MIT licence.
 */
#pragma once

#include <armpi/kit.h>

namespace muse::audio::korin {
struct AudioKit {
    // (ranks : the order of kit_fn in audiokit.abi, which only grows)
    enum Rank {
        SF_DEFAULT = 70, SF_PRESETS = 72, SF_PRESET = 73,
        SYNTH_NEW = 74, SYNTH_FREE = 75, SYNTH_MIDI = 76, SYNTH_RENDER_F32 = 78, SYNTH_RESET = 79,
    };
    enum { MIN_VERSION = 80 };	// (ak_synth_voices : the synth with the system SoundFont)
    enum { AKS_NOFX = 2, AKSP_BANK = 0, AKSP_PROGRAM = 4, AKSP_NAME = 8, AKSP_SIZE = 32 };

    long (*sf_default)() = nullptr;
    long (*sf_presets)(long sf) = nullptr;
    long (*sf_preset)(long sf, long i, void* out) = nullptr;
    long (*synth_new)(long sf, long flags) = nullptr;
    long (*synth_free)(long s) = nullptr;
    long (*synth_midi)(long s, long ch, long cmd, long d1, long d2) = nullptr;
    long (*synth_render_f32)(long s, float* l, float* r, long frames) = nullptr;
    long (*synth_reset)(long s) = nullptr;

    bool ok() const { return synth_new && synth_midi && synth_render_f32 && sf_default; }

    static const AudioKit& get()
    {
        static const AudioKit ak = load();
        return ak;
    }

private:
    template<typename F>
    static void fn(void* k, int rank, F& f) { f = reinterpret_cast<F>(armpi_kit_fn(k, rank)); }

    static AudioKit load()
    {
        AudioKit a;
        void* k = armpi_kit_open("audiokit", MIN_VERSION);
        if (!k) {
            return a;
        }
        fn(k, SF_DEFAULT, a.sf_default);
        fn(k, SF_PRESETS, a.sf_presets);
        fn(k, SF_PRESET, a.sf_preset);
        fn(k, SYNTH_NEW, a.synth_new);
        fn(k, SYNTH_FREE, a.synth_free);
        fn(k, SYNTH_MIDI, a.synth_midi);
        fn(k, SYNTH_RENDER_F32, a.synth_render_f32);
        fn(k, SYNTH_RESET, a.synth_reset);
        return a;
    }
};
}
