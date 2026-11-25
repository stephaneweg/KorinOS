/*
 * korinfluidsynth.inc -- FluidSynth (MuseScore's SoundFont synthesizer class) on Korin OS : AudioKit's GM synth
 * instead of libfluidsynth, with the system SoundFont (/res/soundfonts, ak_sf_default), the one Koton, Arabesque
 * and the drum machine play. Included by fluidsynth.cpp under __armpi__ (tools/musescore/patches/musescore.patch) ;
 * same class, same sequencer (FluidSequencer), only the sound changes. docs/etude/MUSESCORE.md.
 *
 * AudioKit's synth : 16 channels, percussion on channel 10 only (bank 128), 44.1 kHz. MuseScore's channels (one per
 * articulation of the track) are mapped : drums -> 9, the others in turn, skipping 9. Not done : key tuning
 * (microtones), the low-pass filter setting, user SoundFonts (one SoundFont : the system's).
 *
 * Copyright (c) 2026 Stéphane Wegener. MIT licence.
 */

#include <array>
#include <map>
#include <cstring>

#include "audio/common/audioerrors.h"
#include "audio/common/audiotypes.h"
#include "korinaudiokit.h"

#include "log.h"

using namespace muse;
using namespace muse::midi;
using namespace muse::audio;
using namespace muse::audio::synth;
using namespace muse::mpe;

static constexpr int DEFAULT_MIDI_VOLUME = 100;
static constexpr unsigned int KORIN_AUDIO_CHANNELS_COUNT = 2;
static constexpr bool STAFF_TO_MIDIOUT_CHANNEL = true;
static constexpr int AK_DRUM_CHANNEL = 9;
static constexpr int AK_CHANNELS = 16;
static constexpr sample_rate_t AK_RATE = 44100;
// gain du rendu d'AudioKit dans le mélangeur de MuseScore (FluidSynth y avait synth.gain = 4,8) : mesuré sous QEMU
// (25/11) : Wait There (p, mp) sortait à -30 dBFS au gain 1 ; 3 la met vers -20 dBFS, et laisse de la marge à un tutti
static constexpr float AK_GAIN = 3.0f;

struct muse::audio::synth::Fluid {
    long synth = 0;
    std::map<int, int> akChannel;	// (MuseScore's channel -> AudioKit's)
    int nextMelodic = 0;
    std::array<std::array<int, 128>, AK_CHANNELS> cc {};
    std::array<int, AK_CHANNELS> bend {};
    std::vector<float> l, r;

    ~Fluid()
    {
        const auto& ak = korin::AudioKit::get();
        if (synth > 0 && ak.synth_free) {
            ak.synth_free(synth);
        }
    }

    int map(int ch, bool drum = false)
    {
        auto it = akChannel.find(ch);
        if (it != akChannel.end()) {
            return it->second;
        }
        int a = AK_DRUM_CHANNEL;
        if (!drum) {
            if (nextMelodic == AK_DRUM_CHANNEL) {
                ++nextMelodic;
            }
            a = nextMelodic % AK_CHANNELS;
            nextMelodic = (nextMelodic + 1) % AK_CHANNELS;
        }
        akChannel[ch] = a;
        return a;
    }

    void midi(int ch, int cmd, int d1, int d2)
    {
        if (synth > 0) {
            korin::AudioKit::get().synth_midi(synth, ch, cmd, d1, d2);
        }
        if (cmd == 0xB0) {
            cc[ch][d1 & 127] = d2;
        } else if (cmd == 0xE0) {
            bend[ch] = (d1 & 127) | ((d2 & 127) << 7);
        }
    }
};

FluidSynth::FluidSynth(const AudioSourceParams& params, const modularity::ContextPtr& iocCtx)
    : AbstractSynthesizer(params, iocCtx)
{
    m_fluid = std::make_shared<Fluid>();
    m_midiOutPort = midiOutPort();
}

bool FluidSynth::isValid() const
{
    return m_fluid->synth > 0;
}

Ret FluidSynth::init(const OutputSpec& spec)
{
    m_outputSpec = spec;
    if (spec.sampleRate != AK_RATE) {
        LOGW() << "AudioKit : le synthé joue à " << AK_RATE << " Hz, la sortie est à " << spec.sampleRate;
    }
    createFluidInstance();
    m_sequencer.setOnOffStreamFlushed([this]() {
        m_flushSoundRequested = true;
    });
    return m_fluid->synth > 0 ? make_ret(Err::NoError) : make_ret(Err::SynthNotInited);
}

void FluidSynth::createFluidInstance()
{
    const auto& ak = korin::AudioKit::get();
    if (!ak.ok()) {
        LOGE() << "AudioKit : synthé absent (audiokit.so trop ancien ?)";
        return;
    }
    if (!ak.sf_default()) {
        LOGW() << "AudioKit : pas de SoundFont dans /res/soundfonts, banque FM d'AudioKit";
    }
    const long s = ak.synth_new(0, korin::AudioKit::AKS_NOFX);	// (les effets : ceux du mélangeur de MuseScore)
    m_fluid->synth = s > 0 ? s : 0;
    if (s <= 0) {
        LOGE() << "AudioKit : ak_synth_new " << s;
    }
}

void FluidSynth::doFlushSound()
{
    for (const auto& p : m_fluid->akChannel) {
        const int ch = p.second;
        for (int n = 0; n < 128; ++n) {
            m_fluid->midi(ch, 0x80, n, 0);
        }
        setControllerValue(p.first, midi::SUSTAIN_PEDAL_CONTROLLER, 0);
        setControllerValue(p.first, midi::SOSTENUTO_PEDAL_CONTROLLER, 0);
        setPitchBend(p.first, 8192);
    }

    if (m_midiOutPort && m_midiOutPort->isConnected()) {
        int lowerBound = 0;
        int upperBound = 0;
        if (STAFF_TO_MIDIOUT_CHANNEL) {
            int channel = m_sequencer.lastStaff();
            if (channel < 0) {
                return;
            }
            channel = channel % 16;
            lowerBound = channel;
            upperBound = channel + 1;
        } else {
            upperBound = std::min(static_cast<int>(m_sequencer.channels().lastIndex()), 16);
        }
        for (int i = lowerBound; i < upperBound; i++) {
            muse::midi::Event e(muse::midi::Event::Opcode::ControlChange, muse::midi::Event::MessageType::ChannelVoice20);
            e.setChannel(i);
            e.setIndex(123); // CC#123 = All notes off
            m_midiOutPort->sendEvent(e);
        }
    }
}

bool FluidSynth::handleEvent(const midi::Event& event)
{
    const int ch = m_fluid->map(event.channel());
    bool ok = true;
    switch (event.opcode()) {
    case Event::Opcode::NoteOn:
        m_fluid->midi(ch, 0x90, event.note(), event.velocity7());
        break;
    case Event::Opcode::NoteOff:
        m_fluid->midi(ch, 0x80, event.note(), 0);
        break;
    case Event::Opcode::ControlChange:
        if (event.index() == muse::midi::EXPRESSION_CONTROLLER) {
            setExpressionLevel(event.data7());
        } else {
            m_fluid->midi(ch, 0xB0, event.index(), event.data7());
        }
        break;
    case Event::Opcode::ProgramChange:
        m_fluid->midi(ch, 0xC0, event.program(), 0);
        break;
    case Event::Opcode::PitchBend:
        setPitchBend(event.channel(), event.pitchBend14());
        break;
    default:
        ok = false;
        break;
    }

    if (m_midiOutPort) {
        if (STAFF_TO_MIDIOUT_CHANNEL && event.isChannelVoice()) {
            int staff = m_sequencer.lastStaff();
            if (staff >= 0) {
                midi::Event me(event);
                me.setChannel(staff % 16);
                m_midiOutPort->sendEvent(me);
            }
        } else {
            m_midiOutPort->sendEvent(event);
        }
    }
    return ok;
}

void FluidSynth::setOutputSpec(const OutputSpec& spec)
{
    m_outputSpec = spec;
}

Ret FluidSynth::addSoundFonts(const std::vector<io::path_t>& sfonts)
{
    // (rien à charger : le synthé d'AudioKit joue la SoundFont du système, chargée une fois pour toutes les apps)
    for (const io::path_t& sfont : sfonts) {
        m_sfontPaths.insert(sfont);
    }
    return make_ret(Err::NoError);
}

void FluidSynth::setPreset(const std::optional<midi::Program>& preset)
{
    m_preset = preset;
}

std::string FluidSynth::name() const
{
    return "Fluid";
}

AudioSourceType FluidSynth::type() const
{
    return AudioSourceType::Fluid;
}

void FluidSynth::setupSound(const PlaybackSetupData& setupData)
{
    if (m_fluid->synth <= 0) {
        return;
    }

    auto setupChannel = [this](const midi::channel_t channelIdx, const midi::Program& program) {
        const bool drum = program.bank >= 128;
        const int ch = m_fluid->map(channelIdx, drum);
        if (!drum) {
            m_fluid->midi(ch, 0xB0, 0, program.bank & 127);	// (banque : AudioKit retombe sur la 0 si elle manque)
        }
        m_fluid->midi(ch, 0xC0, program.program, 0);
        // étendue de la molette : 24 demi-tons (RPN 0), comme MuseScore le règle sur FluidSynth
        m_fluid->midi(ch, 0xB0, 101, 0);
        m_fluid->midi(ch, 0xB0, 100, 0);
        m_fluid->midi(ch, 0xB0, 6, 24);
        m_fluid->midi(ch, 0xB0, 38, 0);
        m_fluid->midi(ch, 0xB0, 7, DEFAULT_MIDI_VOLUME);
        m_fluid->midi(ch, 0xB0, muse::midi::EXPRESSION_CONTROLLER, m_sequencer.naturalExpressionLevel());
    };

    m_sequencer.channelAdded().onReceive(this, setupChannel, async::Asyncable::Mode::SetReplace);
    m_sequencer.init(setupData, m_preset, setupData.supportsSingleNoteDynamics);

    for (const auto& voice : m_sequencer.channels().data()) {
        for (const auto& pair : voice.second) {
            const ChannelMap::ChannelMapping& channelMapping = pair.second;
            setupChannel(channelMapping.first, channelMapping.second);
        }
    }
}

void FluidSynth::setupEvents(const mpe::PlaybackData& playbackData)
{
    m_sequencer.load(playbackData);
}

const mpe::PlaybackData& FluidSynth::playbackData() const
{
    return m_sequencer.playbackData();
}

void FluidSynth::flushSound()
{
    m_sequencer.flushOffstream();
    m_flushSoundRequested = true;
}

bool FluidSynth::isActive() const
{
    return m_sequencer.isActive();
}

void FluidSynth::setIsActive(const bool isActive)
{
    m_sequencer.setActive(isActive);
    toggleExpressionController();
}

msecs_t FluidSynth::playbackPosition() const
{
    return m_sequencer.playbackPosition();
}

void FluidSynth::setPlaybackPosition(const msecs_t newPosition)
{
    m_sequencer.setPlaybackPosition(newPosition);
    if (isActive()) {
        setExpressionLevel(m_sequencer.currentExpressionLevel());
    }
}

unsigned int FluidSynth::audioChannelsCount() const
{
    return KORIN_AUDIO_CHANNELS_COUNT;
}

samples_t FluidSynth::process(float* buffer, samples_t samplesPerChannel)
{
    IF_ASSERT_FAILED(samplesPerChannel > 0) {
        return 0;
    }

    if (m_flushSoundRequested) {
        doFlushSound();
        m_flushSoundRequested = false;
    }

    const msecs_t nextMsecs = samplesToMsecs(samplesPerChannel, m_outputSpec.sampleRate);
    const FluidSequencer::EventSequenceMap sequences = m_sequencer.movePlaybackForward(nextMsecs);
    samples_t sampleOffset = 0;

    for (auto it = sequences.cbegin(); it != sequences.cend(); ++it) {
        samples_t durationInSamples = samplesPerChannel - sampleOffset;

        auto nextIt = std::next(it);
        if (nextIt != sequences.cend()) {
            msecs_t duration = nextIt->first - it->first;
            durationInSamples = microSecsToSamples(duration, m_outputSpec.sampleRate);
        }

        IF_ASSERT_FAILED(sampleOffset + durationInSamples <= samplesPerChannel) {
            break;
        }

        if (!processSequence(it->second, durationInSamples, buffer + sampleOffset * KORIN_AUDIO_CHANNELS_COUNT)) {
            return 0;
        }

        sampleOffset += durationInSamples;
    }

    return samplesPerChannel;
}

bool FluidSynth::processSequence(const FluidSequencer::EventSequence& sequence, const samples_t samples, float* buffer)
{
    for (const FluidSequencer::EventType& event : sequence) {
        handleEvent(std::get<midi::Event>(event));
    }

    if (samples == 0) {
        return true;
    }
    if (m_fluid->synth <= 0) {
        std::memset(buffer, 0, samples * KORIN_AUDIO_CHANNELS_COUNT * sizeof(float));
        return true;
    }

    // AudioKit rend la gauche et la droite à part ; MuseScore attend des trames entrelacées
    if (m_fluid->l.size() < samples) {
        m_fluid->l.resize(samples);
        m_fluid->r.resize(samples);
    }
    korin::AudioKit::get().synth_render_f32(m_fluid->synth, m_fluid->l.data(), m_fluid->r.data(), long(samples));
    for (samples_t i = 0; i < samples; ++i) {
        buffer[2 * i] = m_fluid->l[i] * AK_GAIN;
        buffer[2 * i + 1] = m_fluid->r[i] * AK_GAIN;
    }
    return true;
}

async::Channel<unsigned int> FluidSynth::audioChannelsCountChanged() const
{
    return m_streamsCountChanged;
}

void FluidSynth::toggleExpressionController()
{
    if (isActive()) {
        setExpressionLevel(m_sequencer.currentExpressionLevel());
    } else {
        setExpressionLevel(m_sequencer.naturalExpressionLevel());
    }
}

int FluidSynth::setExpressionLevel(int level)
{
    for (const auto& p : m_fluid->akChannel) {
        m_fluid->midi(p.second, 0xB0, muse::midi::EXPRESSION_CONTROLLER, level);
    }
    return 0;
}

int FluidSynth::setControllerValue(int channel, int ctrl, int value)
{
    const int ch = m_fluid->map(channel);
    if (m_fluid->cc[ch][ctrl & 127] != value) {
        m_fluid->midi(ch, 0xB0, ctrl, value);
    }
    return 0;
}

int FluidSynth::setPitchBend(int channel, int pitchBend)
{
    const int ch = m_fluid->map(channel);
    if (m_fluid->bend[ch] != pitchBend) {
        m_fluid->midi(ch, 0xE0, pitchBend & 127, (pitchBend >> 7) & 127);
    }
    return 0;
}
