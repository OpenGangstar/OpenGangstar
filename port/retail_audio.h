#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

class FileSystemBase;
struct CityAudioCue;

std::string retailSoundPath(unsigned id);
std::vector<std::string> readRetailSoundIndex(FileSystemBase& assets);

class RetailAudio {
public:
    explicit RetailAudio(FileSystemBase& assets);
    ~RetailAudio();

    void play(unsigned id, bool loop, float gain = 1, float pitch = 1);
    void setBusVolumes(float effects, float music);
    void playPriority(unsigned id, bool loop, float gain = 1);
    void playScriptCue(const CityAudioCue& cue);
    void updateV6Engine(bool running, int rpm, float load);
    float mixGain(unsigned id) const;
    void setPitch(unsigned id, float pitch);
    void setGain(unsigned id, float gain);
    void stop(unsigned id);
    void stopAll();
    void update();
    bool playing(unsigned id) const;

private:
    struct Voice;
    void startVoice(unsigned id, bool loop, float gain, float pitch, bool priority);
    bool priorityActive() const;
    float mixGain(unsigned id, const Voice& voice, bool cutscene) const;
    FileSystemBase& assets;
    std::vector<std::string> soundNames;
    float effectsVolume = 0.8f;
    float musicVolume = 0.8f;
    bool v6Running = false;
    float v6FilteredRpm = 0;
    int v6LoadFadeStep = 0;
    std::map<unsigned, std::unique_ptr<Voice>> voices;
};
