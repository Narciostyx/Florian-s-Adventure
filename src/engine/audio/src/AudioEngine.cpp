#include "fa/audio/AudioEngine.hpp"

#include <cstddef>
#include <utility>

#include <fa/core/Math.hpp>

namespace fa::audio {

AudioEngine::AudioEngine() : m_mixer(kSampleRate, kChannels) {}

AudioEngine::~AudioEngine() {
    shutdown();
}

core::Status AudioEngine::initialize(platform::IAudioDevice& device) {
    shutdown();

    m_mixer.setFormat(kSampleRate, kChannels);
    m_mixer.reset();

    const core::Status status = device.initialize(kSampleRate, kChannels, &AudioEngine::audioCallback, this);
    if (status.isError()) {
        publishStats();
        return status;
    }

    m_device = &device;
    m_initialized.store(true, std::memory_order_release);
    publishStats();
    return core::okStatus();
}

void AudioEngine::shutdown() {
    if (m_device != nullptr) {
        m_device->shutdown();
        m_device = nullptr;
    }
    m_initialized.store(false, std::memory_order_release);

    Command discarded;
    while (m_commands.tryPop(discarded)) {
    }
    m_mixer.reset();
    publishStats();
}

bool AudioEngine::isInitialized() const noexcept {
    return m_initialized.load(std::memory_order_acquire);
}

SoundHandle AudioEngine::play(std::shared_ptr<const AudioClip> clip, const SoundDesc& desc) {
    if (!isInitialized() || !clip || !clip->isValid()) {
        return kInvalidSoundHandle;
    }

    SoundHandle handle = m_nextHandle.fetch_add(1, std::memory_order_relaxed);
    if (handle == kInvalidSoundHandle) {
        handle = m_nextHandle.fetch_add(1, std::memory_order_relaxed);
    }

    Command command;
    command.kind = CommandKind::Play;
    command.handle = handle;
    command.desc = desc;
    command.clip = std::move(clip);
    if (!m_commands.tryPush(std::move(command))) {
        return kInvalidSoundHandle;
    }
    return handle;
}

void AudioEngine::stop(SoundHandle handle, float fadeSeconds) {
    if (!isInitialized() || handle == kInvalidSoundHandle) {
        return;
    }
    Command command;
    command.kind = CommandKind::Stop;
    command.handle = handle;
    command.fadeSeconds = fadeSeconds;
    static_cast<void>(m_commands.tryPush(std::move(command)));
}

void AudioEngine::setBusVolume(AudioBus bus, float volume) {
    if (!isInitialized()) {
        return;
    }
    Command command;
    command.kind = CommandKind::BusVolume;
    command.bus = bus;
    command.value = volume;
    static_cast<void>(m_commands.tryPush(std::move(command)));
}

void AudioEngine::setListener(core::Vec2 position, bool enabled) {
    if (!isInitialized()) {
        return;
    }
    Command command;
    command.kind = CommandKind::Listener;
    command.position = position;
    command.enabled = enabled;
    static_cast<void>(m_commands.tryPush(std::move(command)));
}

void AudioEngine::update() {
    if (!isInitialized() || m_device == nullptr || !m_device->isOpen()) {
        drainCommands();
        publishStats();
    }
}

AudioStats AudioEngine::stats() const noexcept {
    AudioStats result;
    result.activeVoices = m_activeVoices.load(std::memory_order_relaxed);
    result.maxVoices = kMaxVoices;
    result.sampleRate = m_sampleRate.load(std::memory_order_relaxed);
    result.channels = m_channels.load(std::memory_order_relaxed);
    result.played = m_played.load(std::memory_order_relaxed);
    result.stopped = m_stopped.load(std::memory_order_relaxed);
    result.finished = m_finished.load(std::memory_order_relaxed);
    result.dropped = m_dropped.load(std::memory_order_relaxed) + static_cast<std::uint64_t>(m_commands.dropped());
    result.evicted = m_evicted.load(std::memory_order_relaxed);
    result.mixedFrames = m_mixedFrames.load(std::memory_order_relaxed);
    result.masterVolume = m_masterVolume.load(std::memory_order_relaxed);
    return result;
}

void AudioEngine::audioCallback(void* user, std::int16_t* output, int frames) {
    if (user == nullptr || output == nullptr || frames <= 0) {
        return;
    }
    static_cast<AudioEngine*>(user)->render(output, frames);
}

void AudioEngine::render(std::int16_t* output, int frames) {
    drainCommands();
    const std::size_t samples = static_cast<std::size_t>(frames) * static_cast<std::size_t>(m_mixer.channels());
    m_mixer.mix(std::span<std::int16_t>(output, samples));
    publishStats();
}

void AudioEngine::drainCommands() {
    Command command;
    while (m_commands.tryPop(command)) {
        apply(command);
    }
}

void AudioEngine::apply(Command& command) {
    switch (command.kind) {
        case CommandKind::Play:
            static_cast<void>(m_mixer.add(std::move(command.clip), command.desc, command.handle));
            break;
        case CommandKind::Stop:
            static_cast<void>(m_mixer.stop(command.handle, command.fadeSeconds));
            break;
        case CommandKind::BusVolume:
            m_mixer.setBusVolume(command.bus, command.value);
            break;
        case CommandKind::Listener:
            m_mixer.setListener(command.position, command.enabled);
            break;
    }
}

void AudioEngine::publishStats() {
    const AudioStats snapshot = m_mixer.stats();
    m_activeVoices.store(snapshot.activeVoices, std::memory_order_relaxed);
    m_sampleRate.store(snapshot.sampleRate, std::memory_order_relaxed);
    m_channels.store(snapshot.channels, std::memory_order_relaxed);
    m_played.store(snapshot.played, std::memory_order_relaxed);
    m_stopped.store(snapshot.stopped, std::memory_order_relaxed);
    m_finished.store(snapshot.finished, std::memory_order_relaxed);
    m_dropped.store(snapshot.dropped, std::memory_order_relaxed);
    m_evicted.store(snapshot.evicted, std::memory_order_relaxed);
    m_mixedFrames.store(snapshot.mixedFrames, std::memory_order_relaxed);
    m_masterVolume.store(snapshot.masterVolume, std::memory_order_relaxed);
}

}  // namespace fa::audio
