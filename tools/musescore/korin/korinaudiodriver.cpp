/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Korin OS : le pilote audio de MuseScore sur AudioKit (tools/musescore, docs/etude/MUSESCORE.md ; modèle : le
 * pilote de Godot, tools/godot/platform/korin/audio_driver_korin.cpp). Un flux d'AudioKit en 16 bits entrelacés ;
 * un fil à nous appelle le rappel de MuseScore (float entrelacés), convertit, et écrit quand AudioKit a la place
 * (AudioKit ne réveille personne : par petits sommeils).
 */
#include "korinaudiodriver.h"

#include <chrono>
#include <cmath>

#include "translation.h"
#include "log.h"
#include "runtime.h"

extern "C" {
#include <armpi/kit.h>
}

using namespace muse;
using namespace muse::audio;

namespace {
/* la table d'AudioKit (src/kits/audiokit/audiokit.abi) */
enum { AK_OPEN = 0, AK_WRITE = 1, AK_SPACE = 3, AK_CLOSE = 7, AK_LATENCY = 10, AK_MIN = 11 };
long (*ak_open)(long rate, long channels, long bits, long output) = nullptr;
long (*ak_write)(long stream, const void* buf, long bytes) = nullptr;
long (*ak_space)(long stream) = nullptr;
long (*ak_close)(long stream) = nullptr;
long (*ak_latency)(long stream, long bytes) = nullptr;

bool loadAudioKit()
{
    if (ak_open) {
        return true;
    }
    void* k = armpi_kit_open("audiokit", AK_MIN);
    if (!k) {
        return false;
    }
    ak_open = reinterpret_cast<long (*)(long, long, long, long)>(armpi_kit_fn(k, AK_OPEN));
    ak_write = reinterpret_cast<long (*)(long, const void*, long)>(armpi_kit_fn(k, AK_WRITE));
    ak_space = reinterpret_cast<long (*)(long)>(armpi_kit_fn(k, AK_SPACE));
    ak_close = reinterpret_cast<long (*)(long)>(armpi_kit_fn(k, AK_CLOSE));
    ak_latency = reinterpret_cast<long (*)(long, long)>(armpi_kit_fn(k, AK_LATENCY));
    return ak_open && ak_write && ak_space && ak_close;
}
}

KorinAudioDriver::KorinAudioDriver() = default;

KorinAudioDriver::~KorinAudioDriver()
{
    close();
}

void KorinAudioDriver::init()
{
}

std::string KorinAudioDriver::name() const
{
    return "AudioKit";
}

AudioDeviceID KorinAudioDriver::defaultDevice() const
{
    return DEFAULT_DEVICE_ID;
}

bool KorinAudioDriver::open(const Spec& spec, Spec* activeSpec)
{
    if (isOpened()) {
        close();
    }
    if (!loadAudioKit()) {
        LOGE() << "AudioKit absent";
        return false;
    }
    m_spec = spec;
    if (m_spec.output.audioChannelCount != 2) {
        m_spec.output.audioChannelCount = 2;	// (AudioKit : stéréo)
    }
    if (m_spec.output.sampleRate != 44100 && m_spec.output.sampleRate != 48000) {
        m_spec.output.sampleRate = 44100;
    }
    m_stream = ak_open(long(m_spec.output.sampleRate), 2, 16, 0);
    if (m_stream < 0) {
        LOGE() << "AudioKit : pas de flux (service sound) : " << m_stream;
        m_stream = -1;
        return false;
    }
    const size_t n = size_t(m_spec.output.samplesPerChannel) * 2;
    m_in.assign(n, 0.0f);
    m_out.assign(n, 0);
    if (ak_latency) {
        ak_latency(m_stream, long(n * sizeof(int16_t) * 2));	// (deux tampons d'avance)
    }
    m_quit = false;
    m_thread = std::thread([this]() { run(); });

    m_activeSpecChanged.send(m_spec);
    if (activeSpec) {
        *activeSpec = m_spec;
    }
    LOGI() << "AudioKit : " << m_spec.output.sampleRate << " Hz, " << m_spec.output.samplesPerChannel << " trames";
    return true;
}

void KorinAudioDriver::run()
{
    runtime::setThreadName("audio_driver");
    const long bytes = long(m_out.size() * sizeof(int16_t));
    while (!m_quit) {
        m_spec.callback(reinterpret_cast<uint8_t*>(m_in.data()), int(m_in.size() * sizeof(float)));
        for (size_t i = 0; i < m_in.size(); ++i) {
            float v = m_in[i];
            v = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
            m_out[i] = int16_t(std::lrint(v * 32767.0f));
        }
        // ak_write prend ce qui tient (avance et anneau du service sound) : le tampon part en morceaux, sans
        // attendre que toute sa taille soit libre d'un coup (ce qui pouvait ne jamais arriver)
        const uint8_t* p = reinterpret_cast<const uint8_t*>(m_out.data());
        long done = 0;
        int idle = 0;
        while (!m_quit && done < bytes) {
            const long r = ak_write(m_stream, p + done, bytes - done);
            if (r < 0) {
                LOGE() << "AudioKit : ak_write " << r;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            done += r;
            if (r == 0) {
                if (++idle == 2500) {	// (5 s sans place : le service sound ne joue plus ce flux)
                    LOGW() << "AudioKit : rien n'est pris depuis 5 s (ak_space " << ak_space(m_stream) << ")";
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            } else {
                idle = 0;
            }
        }
    }
}

void KorinAudioDriver::close()
{
    m_quit = true;
    if (m_thread.joinable()) {
        m_thread.join();
    }
    if (m_stream >= 0) {
        ak_close(m_stream);
        m_stream = -1;
    }
}

bool KorinAudioDriver::isOpened() const
{
    return m_stream >= 0;
}

const KorinAudioDriver::Spec& KorinAudioDriver::activeSpec() const
{
    return m_spec;
}

async::Channel<IAudioDriver::Spec> KorinAudioDriver::activeSpecChanged() const
{
    return m_activeSpecChanged;
}

AudioDeviceList KorinAudioDriver::availableOutputDevices() const
{
    AudioDeviceList devices;
    devices.push_back({ DEFAULT_DEVICE_ID, muse::trc("audio", "System default") });
    return devices;
}

async::Notification KorinAudioDriver::availableOutputDevicesChanged() const
{
    return m_availableOutputDevicesChanged;
}

std::vector<samples_t> KorinAudioDriver::availableOutputDeviceBufferSizes() const
{
    return { 256, 512, 1024, 2048, 4096 };
}

std::vector<sample_rate_t> KorinAudioDriver::availableOutputDeviceSampleRates() const
{
    return { 44100, 48000 };
}
