/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Korin OS : le pilote audio de MuseScore sur AudioKit (tools/musescore, docs/etude/MUSESCORE.md).
 */
#pragma once

#include <atomic>
#include <thread>
#include <vector>

#include "async/asyncable.h"
#include "iaudiodriver.h"

namespace muse::audio {
class KorinAudioDriver : public IAudioDriver, public async::Asyncable
{
public:
    KorinAudioDriver();
    ~KorinAudioDriver() override;

    void init() override;
    std::string name() const override;
    AudioDeviceID defaultDevice() const override;
    bool open(const Spec& spec, Spec* activeSpec) override;
    void close() override;
    bool isOpened() const override;
    const Spec& activeSpec() const override;
    async::Channel<Spec> activeSpecChanged() const override;
    AudioDeviceList availableOutputDevices() const override;
    async::Notification availableOutputDevicesChanged() const override;
    std::vector<samples_t> availableOutputDeviceBufferSizes() const override;
    std::vector<sample_rate_t> availableOutputDeviceSampleRates() const override;

private:
    void run();

    Spec m_spec;
    long m_stream = -1;
    std::thread m_thread;
    std::atomic<bool> m_quit { false };
    std::vector<float> m_in;
    std::vector<int16_t> m_out;
    async::Channel<Spec> m_activeSpecChanged;
    async::Notification m_availableOutputDevicesChanged;
};
}
