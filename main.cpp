// OS RACER - Multi-threaded Kernel Edition (Final Complete Version)
// C++11 - Visual Studio / MSVC
//
// Level 1: Retro Digital Grid (Distinctive wireframe landscape).
// Level 2: Cyber City (Standard).
// Level 3: Pure Deep Space (Stars ONLY. No nebula, no mountains, no ground).

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <windows.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdio> // For swprintf_s
#include <locale>
#include <mmsystem.h>
#include <stdio.h>
#pragma comment(lib, "winmm.lib")
using namespace std;

// Audio file paths (place these files in the "audio" folder)
const wchar_t* AUDIO_FOLDER = L"audio\\";
const wchar_t* ENGINE_IDLE_FILE = L"engine_idle.mp3";  // Engine idle/running sound (when moving)
const wchar_t* ENGINE_ACCEL_FILE = L"engine_accel.mp3"; // Engine acceleration sound (when accelerating)
const wchar_t* BRAKE_SOUND_FILE = L"brake.mp3";     // Brake sound file (when steering)
const wchar_t* CRASH_SOUND_FILE = L"crash.mp3";     // Crash sound file
const wchar_t* GAMEOVER_SOUND_FILE = L"gameover.mp3"; // Game Over sound file
const wchar_t* WIN_SOUND_FILE = L"victory.mp3";     // Victory sound file
const wchar_t* BGM_FILE = L"bgm.mp3";               // Background music file

// Helper function to get full audio file path
wstring GetAudioPath(const wchar_t* filename) {
    wstring path = AUDIO_FOLDER;
    path += filename;
    return path;
}

// ================================================================
// OS RACER — Kernel Physics Parameter Table (KPT)
// ================================================================
const int nScreenWidth = 120; // Console width (columns)
const int nScreenHeight = 30; // Console height (rows)
const int FRAME_RATE = 60; // Render thread FPS
const float PHYSICS_HZ = 240.0f; // Physics thread frequency
const float DELTA_T = 1.0f / PHYSICS_HZ; // Physics fixed-step (s)

// Road / Track Parameters
const float ROAD_WIDTH_LIMIT = 1.0f; // Max absolute lane offset
const float CAMERA_LAG_DISTANCE = 3.0f; // Third-person: camera looks back 3 units
const float PLAYER_HALF_WIDTH = 0.18f; // Half of car width in normalized road coordinates

// Vehicle Dynamics Parameters
const float MAX_SPEED = 50.0f; // Top speed
const float ACCELERATION = 17.0f; // Acceleration units/sec
const float DECELERATION = 20.0f; // Deceleration units/sec
const float FRICTION = 0.9999f; // Speed friction per physics tick

// Steering / Lateral Control
const float LATERAL_FACTOR = 0.0004f;
const float STEER_COMPENSATION = 0.002f;

// Heading Angle & Drift
const float HEADING_TURN_SPEED = 0.4f;
const float HEADING_DRIFT_FACTOR = 0.004f;

// Rendering / Visual characters
const wchar_t CHAR_FULL = L'█';
const wchar_t CHAR_DARK = L'▓';
const wchar_t CHAR_MED = L'▒';
const wchar_t CHAR_LIGHT = L'░';
const wchar_t CHAR_EMPTY = L' ';

// Game state
enum GameState { BOOT_MENU = 0, MAP_SELECT, KERNEL_RUNNING, GAME_WIN, GAME_OVER, SYSTEM_HALT };
std::atomic<GameState> currentState(BOOT_MENU);

// Global map ID for rendering context
int g_currentMapId = 1;

// -------------------------- Player State -------------------------
struct PlayerPCB {
    float fX_Register = 0.0f; // Lateral position on road (-1 ~ +1)
    float fSpeed = 0.0f; // Forward speed
    float fDistance = 0.0f; // Forward distance along track
    float fCurvature = 0.0f; // Current track curvature
    float fPlayerCurvature = 0.0f; // Accumulated curvature for background
    float fHeadingAngle = 0.0f; // Visual steering angle
    bool bCrashed = false;
    int nSteerState = 0; // -1, 0, +1
    void Reset() { *this = PlayerPCB(); }
} player;

std::mutex g_player_mutex;

// --------------------------- Track Data --------------------------
struct Obstacle {
    float fSegDistance; // Distance inside the segment
    float fOffsetX; // Lateral offset from center (-ROAD_WIDTH_LIMIT to ROAD_WIDTH_LIMIT)
    float fWidth; // Obstacle width in normalized road coordinates
};

struct TrackSegment {
    float fCurvature; // Curvature factor
    float fDistance; // Segment length
    vector<Obstacle> vecObstacles; // Obstacles inside this segment
};

vector<TrackSegment> vecTrack;
float fTotalTrackLength = 0.0f;
vector<pair<float, float>> vecMapPointsCurrent;
vector<pair<float, float>> vecMapPreview[3];

// --------------------------- Console -----------------------------
HANDLE hConsole = NULL;
vector<wchar_t> screenBuffer(nScreenWidth * nScreenHeight, CHAR_EMPTY);
vector<WORD> colorBuffer(nScreenWidth * nScreenHeight, 0x07);
std::mutex g_screen_mutex;

// -------------------------- Thread Control -----------------------
std::atomic<bool> running(true);

// ------------------------- Input Atomics -------------------------
std::atomic<int> input_steer(0); // -1, 0, +1
std::atomic<bool> input_accel(false);
std::atomic<bool> input_brake(false);
std::atomic<bool> input_escape(false);

// Edge-detect input (one-shot)
std::atomic<bool> input_space_edge(false);
std::atomic<bool> input_up_edge(false);
std::atomic<bool> input_down_edge(false);
std::atomic<bool> input_1_edge(false);
std::atomic<bool> input_2_edge(false);
std::atomic<bool> input_3_edge(false);

// ----------------- Obstacle Warning (shared flags) ----------------
std::atomic<bool> warnObstacle(false);
std::atomic<float> warnObstacleDist(0.0f);
std::atomic<float> warnObstacleOffsetX(0.0f);

// ----------------- Sound System -----------------
std::atomic<bool> sound_crash(false);
std::atomic<bool> sound_gameover(false);
std::atomic<bool> sound_win(false);
std::atomic<bool> sound_brake(false);
std::atomic<float> lastSpeed(0.0f);
std::atomic<bool> sound_accel_active(false);
std::atomic<float> engineRPM(0.0f); // Engine RPM for realistic sound
std::atomic<bool> bgm_playing(false);
std::atomic<bool> engine_idle_playing(false);
std::atomic<bool> engine_accel_playing(false);
std::atomic<bool> brake_sound_playing(false);

// =================================================================
// Sound System
// =================================================================
void PlayBeepSound(int frequency, int duration) {
    Beep(frequency, duration);
}

// Play audio file using MCI (supports WAV, MP3, etc.)
bool PlayAudioFile(const wchar_t* filename, bool loop = false) {
    // Get full path with audio folder
    wstring fullPath = GetAudioPath(filename);
    
    // First, ensure any existing instance is properly closed
    wchar_t closeCmd[128];
    swprintf_s(closeCmd, L"close audio_file");
    mciSendStringW(closeCmd, NULL, 0, NULL); // Ignore errors if not open
    Sleep(30); // Give system more time to release resources
    
    wchar_t cmd[512];
    MCIERROR err = 0;
    
    // Check file extension to determine format
    wstring filenameStr(filename);
    size_t dotPos = filenameStr.find_last_of(L".");
    bool isMP3 = false;
    if (dotPos != wstring::npos) {
        wstring ext = filenameStr.substr(dotPos + 1);
        for (auto& c : ext) c = towlower(c);
        isMP3 = (ext == L"mp3");
    }
    
    // Try MP3 first if extension suggests MP3, otherwise try WAV first
    if (isMP3) {
        swprintf_s(cmd, L"open \"%s\" type mpegvideo alias audio_file", fullPath.c_str());
        err = mciSendStringW(cmd, NULL, 0, NULL);
        if (err != 0) {
            // Try as WAV if MP3 fails
            swprintf_s(cmd, L"open \"%s\" type waveaudio alias audio_file", fullPath.c_str());
            err = mciSendStringW(cmd, NULL, 0, NULL);
        }
    } else {
        // Try WAV first
        swprintf_s(cmd, L"open \"%s\" type waveaudio alias audio_file", fullPath.c_str());
        err = mciSendStringW(cmd, NULL, 0, NULL);
        if (err != 0) {
            // Try as MP3 if WAV fails
            swprintf_s(cmd, L"open \"%s\" type mpegvideo alias audio_file", fullPath.c_str());
            err = mciSendStringW(cmd, NULL, 0, NULL);
        }
    }
    
    if (err != 0) {
        return false; // File not found or format not supported
    }
    
    wchar_t playCmd[128];
    if (loop) {
        swprintf_s(playCmd, L"play audio_file repeat");
    } else {
        swprintf_s(playCmd, L"play audio_file");
    }
    
    MCIERROR playErr = mciSendStringW(playCmd, NULL, 0, NULL);
    if (playErr != 0) {
        // If play fails, close the device to avoid resource leak
        swprintf_s(cmd, L"close audio_file");
        mciSendStringW(cmd, NULL, 0, NULL);
        return false;
    }
    
    return true;
}

// Stop audio file
void StopAudioFile() {
    mciSendStringW(L"stop audio_file", NULL, 0, NULL);
    mciSendStringW(L"close audio_file", NULL, 0, NULL);
}

// Play audio file with custom alias (for multiple simultaneous sounds)
bool PlayAudioFileWithAlias(const wchar_t* filename, const wchar_t* alias, bool loop = false) {
    // Get full path with audio folder
    wstring fullPath = GetAudioPath(filename);
    
    // First, ensure any existing instance is properly closed to avoid conflicts
    wchar_t cmd[512];
    
    // Check if device is already open and stop/close it properly
    wchar_t statusCmd[128];
    wchar_t status[128];
    swprintf_s(statusCmd, L"status %s mode", alias);
    MCIERROR statusErr = mciSendStringW(statusCmd, status, 128, NULL);
    
    if (statusErr == 0) {
        // Device exists, stop and close it first
        swprintf_s(cmd, L"stop %s", alias);
        mciSendStringW(cmd, NULL, 0, NULL);
        swprintf_s(cmd, L"close %s", alias);
        mciSendStringW(cmd, NULL, 0, NULL);
        Sleep(20); // Give system time to release resources
    }
    
    // Try MP3 first
    swprintf_s(cmd, L"open \"%s\" type mpegvideo alias %s", fullPath.c_str(), alias);
    MCIERROR err = mciSendStringW(cmd, NULL, 0, NULL);
    
    if (err != 0) {
        // Try WAV format if MP3 fails
        swprintf_s(cmd, L"open \"%s\" type waveaudio alias %s", fullPath.c_str(), alias);
        err = mciSendStringW(cmd, NULL, 0, NULL);
        if (err != 0) {
            return false; // File not found or format not supported
        }
    }
    
    // Play the file
    wchar_t playCmd[128];
    if (loop) {
        swprintf_s(playCmd, L"play %s repeat", alias);
    } else {
        swprintf_s(playCmd, L"play %s", alias);
    }
    
    MCIERROR playErr = mciSendStringW(playCmd, NULL, 0, NULL);
    if (playErr != 0) {
        // If play fails, close the device to avoid resource leak
        swprintf_s(cmd, L"close %s", alias);
        mciSendStringW(cmd, NULL, 0, NULL);
        return false;
    }
    
    return true;
}

// Stop audio file with custom alias
void StopAudioFileWithAlias(const wchar_t* alias) {
    wchar_t cmd[128];
    wchar_t statusCmd[128];
    wchar_t status[128];
    
    // Check if device exists before trying to stop/close
    swprintf_s(statusCmd, L"status %s mode", alias);
    MCIERROR statusErr = mciSendStringW(statusCmd, status, 128, NULL);
    
    if (statusErr == 0) {
        // Device exists, stop it first
        swprintf_s(cmd, L"stop %s", alias);
        mciSendStringW(cmd, NULL, 0, NULL);
        
        // Then close it
        swprintf_s(cmd, L"close %s", alias);
        MCIERROR closeErr = mciSendStringW(cmd, NULL, 0, NULL);
        
        // If close fails, try again after a short delay
        if (closeErr != 0) {
            Sleep(10);
            swprintf_s(cmd, L"close %s", alias);
            mciSendStringW(cmd, NULL, 0, NULL);
        }
    }
}

// Fade out audio file with custom alias (gradual volume decrease)
void FadeOutAudioFileWithAlias(const wchar_t* alias, float fadeoutProgress) {
    // fadeoutProgress: 0.0 = full volume, 1.0 = silent
    int volume = (int)(1000 * (1.0f - fadeoutProgress)); // MCI volume: 0-1000
    volume = max(0, min(1000, volume));
    
    wchar_t cmd[128];
    swprintf_s(cmd, L"setaudio %s volume to %d", alias, volume);
    mciSendStringW(cmd, NULL, 0, NULL);
    
    // If fully faded out, stop the sound
    if (fadeoutProgress >= 1.0f) {
        StopAudioFileWithAlias(alias);
    }
}

void SoundThreadProc() {
    float lastEngineRPM = 0.0f;
    auto lastEngineTime = chrono::high_resolution_clock::now();
    bool isAccelerating = false;
    bool wasAccelerating = false; // Track previous state for edge detection
    bool bgmStarted = false;
    
    while (running.load()) {
        GameState state = currentState.load();
        
        // Background music - start only when entering the map (KERNEL_RUNNING)
        if (state == KERNEL_RUNNING) {
            if (!bgmStarted && !bgm_playing.load()) {
                // Try to play background music with loop
                if (PlayAudioFile(BGM_FILE, true)) {
                    bgm_playing.store(true);
                    bgmStarted = true;
                }
            } else if (bgm_playing.load()) {
                // Ensure BGM keeps looping - check if it's still playing
                // If it stopped, restart it
                wchar_t statusCmd[128];
                wchar_t status[128];
                swprintf_s(statusCmd, L"status audio_file mode");
                MCIERROR err = mciSendStringW(statusCmd, status, 128, NULL);
                if (err == 0) {
                    // Check if stopped (should be "playing" or "stopped")
                    wstring statusStr(status);
                    if (statusStr.find(L"stopped") != wstring::npos || statusStr.find(L"not ready") != wstring::npos) {
                        // Restart BGM with loop
                        PlayAudioFile(BGM_FILE, true);
                    }
                }
            }
        } else {
            // Stop BGM when not in game (menu, game over, win, etc.)
            if (bgm_playing.load()) {
                StopAudioFile();
                bgm_playing.store(false);
                bgmStarted = false;
            }
        }
        
        if (state == KERNEL_RUNNING) {
            float currentSpeed = 0.0f;
            bool accelPressed = false;
            {
                std::lock_guard<std::mutex> lk(g_player_mutex);
                currentSpeed = player.fSpeed;
            }
            accelPressed = input_accel.load();
            int steerInput = input_steer.load();
            
            // Engine sound plays when moving (speed > 0)
            // RPM is based on speed, higher when accelerating
            isAccelerating = accelPressed && currentSpeed < MAX_SPEED;
            
            // Brake sound handling - stop immediately when key released
            static int lastSteerInput = 0;
            if (steerInput != 0) {
                // Just started steering - trigger brake sound
                if (lastSteerInput == 0 && currentSpeed > 0.1f) {
                    sound_brake.store(true);
                }
                lastSteerInput = steerInput;
            } else {
                // Stop brake sound immediately when steering stops (steerInput == 0)
                // Always check and stop if playing when steering is released
                if (brake_sound_playing.load()) {
                    // Force stop and close the audio file
                    wchar_t cmd[128];
                    swprintf_s(cmd, L"stop brake_sound");
                    mciSendStringW(cmd, NULL, 0, NULL);
                    swprintf_s(cmd, L"close brake_sound");
                    mciSendStringW(cmd, NULL, 0, NULL);
                    brake_sound_playing.store(false);
                }
                lastSteerInput = 0; // Reset to 0
            }
            
            // Calculate engine RPM based on speed
            // Base RPM from speed (idle at 800 RPM when moving, up to 4000 RPM at max speed)
            float targetRPM = 0.0f;
            if (currentSpeed > 0.1f) {
                // Base RPM from speed: 800 RPM (idle) to 3500 RPM (max speed)
                targetRPM = 800.0f + (currentSpeed / MAX_SPEED) * 2700.0f;
                
                // Add extra RPM when accelerating
                if (isAccelerating) {
                    targetRPM += 500.0f; // Higher RPM when accelerating (up to 4000 RPM)
                }
            } else {
                // No RPM when stopped
                targetRPM = 0.0f;
            }
            
            // Smooth RPM transition
            float rpmChangeRate = (currentSpeed > 0.1f) ? 0.3f : 0.5f; // Faster decay when stopping
            float currentRPM = lastEngineRPM + (targetRPM - lastEngineRPM) * rpmChangeRate;
            if (currentRPM < 50.0f) currentRPM = 0.0f; // Stop completely when low
            lastEngineRPM = currentRPM;
            engineRPM.store(currentRPM);
            
            // Play engine sounds when moving (speed > 0)
            if (currentSpeed > 0.1f) {
                // Play idle engine sound (always when moving) - ensure it keeps playing
                if (!engine_idle_playing.load()) {
                    if (!PlayAudioFileWithAlias(ENGINE_IDLE_FILE, L"engine_idle", true)) {
                        // Fallback to beep-based engine sound
                        engine_idle_playing.store(true);
                    } else {
                        engine_idle_playing.store(true);
                        // Set full volume for idle sound
                        wchar_t cmd[128];
                        swprintf_s(cmd, L"setaudio engine_idle volume to 1000");
                        mciSendStringW(cmd, NULL, 0, NULL);
                    }
                } else {
                    // Ensure idle sound keeps playing and is at full volume (prevent fadeout)
                    wchar_t cmd[128];
                    swprintf_s(cmd, L"setaudio engine_idle volume to 1000");
                    mciSendStringW(cmd, NULL, 0, NULL);
                }
                
                // Play acceleration sound when accelerating
                // Restart acceleration sound each time acceleration key is pressed (edge detection)
                if (isAccelerating) {
                    // If just started accelerating (edge detection), restart the sound
                    if (!wasAccelerating) {
                        // Stop and restart acceleration sound to reset playback
                        // Always close existing first to avoid conflicts
                        if (engine_accel_playing.load()) {
                            // Use proper stop function to ensure cleanup
                            StopAudioFileWithAlias(L"engine_accel");
                            engine_accel_playing.store(false);
                            Sleep(30); // Longer delay to ensure system releases resources
                        }
                        // Start playing acceleration sound (will overlay with idle sound)
                        if (PlayAudioFileWithAlias(ENGINE_ACCEL_FILE, L"engine_accel", true)) {
                            engine_accel_playing.store(true);
                        } else {
                            // File not found or resource error, reset state
                            engine_accel_playing.store(false);
                        }
                    }
                    // If already accelerating, verify it's still playing
                    else if (engine_accel_playing.load()) {
                        // Check if sound is actually playing, restart if needed
                        wchar_t statusCmd[128];
                        wchar_t status[128];
                        swprintf_s(statusCmd, L"status engine_accel mode");
                        MCIERROR err = mciSendStringW(statusCmd, status, 128, NULL);
                        if (err != 0) {
                            // Device not found or error, restart it
                            if (PlayAudioFileWithAlias(ENGINE_ACCEL_FILE, L"engine_accel", true)) {
                                engine_accel_playing.store(true);
                            } else {
                                engine_accel_playing.store(false);
                            }
                        } else {
                            wstring statusStr(status);
                            if (statusStr.find(L"stopped") != wstring::npos) {
                                // Restart if stopped
                                wchar_t playCmd[128];
                                swprintf_s(playCmd, L"play engine_accel repeat");
                                mciSendStringW(playCmd, NULL, 0, NULL);
                            }
                        }
                    }
                } else {
                    // Stop acceleration sound immediately when not accelerating (key released)
                    if (engine_accel_playing.load()) {
                        // Use proper stop function
                        StopAudioFileWithAlias(L"engine_accel");
                        engine_accel_playing.store(false);
                    }
                }
                
                // Update previous state for next frame
                wasAccelerating = isAccelerating;
                
                // If using beep fallback, play beep sound based on RPM
                if (engine_idle_playing.load() || engine_accel_playing.load()) {
                    auto now = chrono::high_resolution_clock::now();
                    auto elapsed = chrono::duration_cast<chrono::milliseconds>(now - lastEngineTime).count();
                    
                    if (currentRPM > 100.0f && elapsed > 80) {
                        lastEngineTime = now;
                        
                        // Base frequency from RPM
                        int baseFreq = (int)(currentRPM / 120.0f);
                        baseFreq = max(80, min(baseFreq, 600));
                        
                        // Play engine sound - louder/higher when accelerating
                        std::thread([baseFreq, currentRPM, isAccelerating]() {
                            int duration = isAccelerating ? 70 : 50; // Longer when accelerating
                            Beep(baseFreq, duration);
                            Sleep(5);
                            Beep(baseFreq * 2, isAccelerating ? 50 : 35);
                            if (currentRPM > 2000) {
                                Sleep(5);
                                Beep((int)(baseFreq * 1.5f), isAccelerating ? 40 : 25);
                            }
                        }).detach();
                    }
                }
            } else {
                // Stop all engine sounds when stopped (speed = 0)
                if (engine_idle_playing.load()) {
                    StopAudioFileWithAlias(L"engine_idle");
                    engine_idle_playing.store(false);
                }
                if (engine_accel_playing.load()) {
                    StopAudioFileWithAlias(L"engine_accel");
                    engine_accel_playing.store(false);
                }
            }
            
            lastSpeed.store(currentSpeed);
        } else {
            // Stop all engine sounds when not in game
            if (engine_idle_playing.load()) {
                StopAudioFileWithAlias(L"engine_idle");
                engine_idle_playing.store(false);
            }
            if (engine_accel_playing.load()) {
                StopAudioFileWithAlias(L"engine_accel");
                engine_accel_playing.store(false);
            }
            if (brake_sound_playing.load()) {
                // Force stop and close the audio file
                wchar_t cmd[128];
                swprintf_s(cmd, L"stop brake_sound");
                mciSendStringW(cmd, NULL, 0, NULL);
                swprintf_s(cmd, L"close brake_sound");
                mciSendStringW(cmd, NULL, 0, NULL);
                brake_sound_playing.store(false);
            }
        }
        
        // Brake sound - try file first, fallback to beep
        if (sound_brake.exchange(false)) {
            // Stop any existing brake sound first (synchronously)
            if (brake_sound_playing.load()) {
                wchar_t cmd[128];
                swprintf_s(cmd, L"stop brake_sound");
                mciSendStringW(cmd, NULL, 0, NULL);
                swprintf_s(cmd, L"close brake_sound");
                mciSendStringW(cmd, NULL, 0, NULL);
                brake_sound_playing.store(false);
            }
            // Play brake sound without loop (play once)
            if (!PlayAudioFileWithAlias(BRAKE_SOUND_FILE, L"brake_sound", false)) {
                // Fallback to beep - short brake sound
                Beep(300, 50);
                Sleep(10);
                Beep(250, 40);
                brake_sound_playing.store(false);
            } else {
                brake_sound_playing.store(true);
            }
        }
        
        // Crash sound - try file first, fallback to beep
        if (sound_crash.exchange(false)) {
            std::thread([]() {
                if (!PlayAudioFile(CRASH_SOUND_FILE, false)) {
                    // Fallback to beep
                    Beep(150, 200);
                    Sleep(50);
                    Beep(100, 300);
                }
            }).detach();
        }
        
        // Game Over sound - try file first, fallback to beep
        if (sound_gameover.exchange(false)) {
            std::thread([]() {
                // Stop background music when game over
                if (bgm_playing.load()) {
                    StopAudioFile();
                    bgm_playing.store(false);
                }
                
                // Play game over sound
                if (!PlayAudioFile(GAMEOVER_SOUND_FILE, false)) {
                    // Fallback to beep - dramatic game over sound
                    Beep(200, 300);
                    Sleep(100);
                    Beep(150, 400);
                    Sleep(100);
                    Beep(100, 500);
                }
            }).detach();
        }
        
        // Victory sound - try file first, fallback to beep
        if (sound_win.exchange(false)) {
            // Stop BGM when victory
            if (bgm_playing.load()) {
                StopAudioFile();
                bgm_playing.store(false);
                bgmStarted = false;
            }
            // Stop all engine sounds
            if (engine_idle_playing.load()) {
                StopAudioFileWithAlias(L"engine_idle");
                engine_idle_playing.store(false);
            }
            if (engine_accel_playing.load()) {
                StopAudioFileWithAlias(L"engine_accel");
                engine_accel_playing.store(false);
            }
            // Give system time to stop all sounds
            Sleep(50);
            
            // Play victory sound (synchronously to ensure it plays)
            // Try MP3 first, then WAV
            bool played = false;
            if (!PlayAudioFile(WIN_SOUND_FILE, false)) {
                // Try WAV version if MP3 fails
                const wchar_t* winWav = L"victory.wav";
                if (!PlayAudioFile(winWav, false)) {
                    // Fallback to beep fanfare
                    Beep(523, 200);
                    Sleep(50);
                    Beep(659, 200);
                    Sleep(50);
                    Beep(784, 200);
                    Sleep(50);
                    Beep(1047, 400);
                } else {
                    played = true;
                }
            } else {
                played = true;
            }
        }
        
        Sleep(50); // Check every 50ms
    }
    
    // Cleanup on exit
    if (bgm_playing.load()) {
        StopAudioFile();
    }
    if (engine_idle_playing.load()) {
        StopAudioFileWithAlias(L"engine_idle");
    }
    if (engine_accel_playing.load()) {
        StopAudioFileWithAlias(L"engine_accel");
    }
}

// =================================================================
// Utility Draw Functions
// =================================================================
void KernelDrawBox(wchar_t* s, int x, int y, int w, int h) {
    for (int i = 0; i < h; ++i) for (int j = 0; j < w; ++j) {
        int px = x + j, py = y + i;
        if (px < 0 || px >= nScreenWidth || py < 0 || py >= nScreenHeight) continue;
        wchar_t c = (i == 0 || i == h - 1) ? L'═' : (j == 0 || j == w - 1) ? L'║' : L' ';
        if (i == 0 && j == 0) c = L'╔';
        if (i == 0 && j == w - 1) c = L'╗';
        if (i == h - 1 && j == 0) c = L'╚';
        if (i == h - 1 && j == w - 1) c = L'╝';
        s[py * nScreenWidth + px] = c;
    }
}

void KernelDrawString(wchar_t* s, int x, int y, const wstring& t) {
    for (size_t i = 0; i < t.size(); ++i) {
        int px = x + (int)i;
        if (px < 0 || px >= nScreenWidth) continue;
        if (y >= 0 && y < nScreenHeight) s[y * nScreenWidth + px] = t[i];
    }
}

// =================================================================
// Map Generation
// =================================================================
void GenerateMapPoints(const vector<TrackSegment>& track, vector<pair<float, float>>& points) {
    points.clear();
    float x = 0.0f, y = 0.0f, angle = 0.0f;
    const float STEP = 1.0f;
    for (auto& seg : track) {
        for (float d = 0.0f; d < seg.fDistance; d += STEP) {
            angle += seg.fCurvature * STEP * 0.01f;
            x += sinf(angle) * STEP;
            y += cosf(angle) * STEP;
            points.emplace_back(x, y);
        }
    }
}

void BuildTrackData(int id, vector<TrackSegment>& t) {
    t.clear();
    if (id == 1) { // ADVANCED S-CURVE (No Obstacles)
        t.push_back({ 0.0f, 80 });
        t.push_back({ 0.6f, 250 });
        t.push_back({-0.6f, 250 });
        t.push_back({ 0.0f, 100 });
        t.push_back({ 0.8f, 200 });
        t.push_back({ 0.0f, 120 });
    }
    else if (id == 2) { // EXPERT SLALOM (Some Obstacles)
        t.push_back({ 0.0f, 100 });
        t.push_back({ 0.7f, 150 });
        t.push_back({-0.5f, 150 });
        t.push_back({ 0.9f, 200 });
        t.push_back({ 0.0f, 100 });
        t.push_back({-0.8f, 180 });
        t.push_back({ 0.6f, 200 });
        t.push_back({ 0.0f, 150 });
        t.push_back({ 1.0f, 120 });
        t.push_back({ 0.0f, 100 });
        // Obstacles
        t[4].vecObstacles.push_back({ 20.0f, 0.4f, 0.3f });
        t[4].vecObstacles.push_back({ 70.0f, -0.5f, 0.4f });
        t[5].vecObstacles.push_back({ 80.0f, 0.6f, 0.3f });
    }
    else if (id == 3) { // EXTREME CIRCULAR (More Obstacles)
        t.push_back({ 0.0f, 100 });
        t.push_back({ 0.8f, 200 });
        t.push_back({-0.6f, 150 });
        t.push_back({ 0.6f, 180 });
        t.push_back({-0.8f, 200 });
        t.push_back({ 0.5f, 150 });
        t.push_back({-0.5f, 150 });
        t.push_back({ 0.8f, 200 });
        t.push_back({-0.8f, 200 });
        t.push_back({ 0.0f, 100 });
        // Obstacles
        t[1].vecObstacles.push_back({ 50.0f, -0.6f, 0.3f });
        t[1].vecObstacles.push_back({150.0f, 0.6f, 0.3f });
        t[3].vecObstacles.push_back({ 40.0f, 0.0f, 0.5f });
        t[5].vecObstacles.push_back({100.0f, -0.4f, 0.4f });
        t[7].vecObstacles.push_back({ 70.0f, 0.3f, 0.2f });
    }
}

void InitMaps() {
    vector<TrackSegment> tmp;
    for (int i = 0; i < 3; i++) {
        BuildTrackData(i + 1, tmp);
        GenerateMapPoints(tmp, vecMapPreview[i]);
    }
}

void LoadMap(int id) {
    g_currentMapId = id;
    BuildTrackData(id, vecTrack);
    GenerateMapPoints(vecTrack, vecMapPointsCurrent);
    fTotalTrackLength = 0.0f;
    for (auto& s : vecTrack) fTotalTrackLength += s.fDistance;
}

// =================================================================
// Mini-map Rendering
// =================================================================
void DrawTrackView(wchar_t* s, int x, int y, int w, int h,
                   const vector<pair<float, float>>& p,
                   bool showPlayer) {
    KernelDrawBox(s, x, y, w, h);
    KernelDrawString(s, x + 1, y + 1, L"TRACK MAP");
    if (p.empty()) return;

    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (auto& pt : p) {
        minX = min(minX, pt.first); maxX = max(maxX, pt.first);
        minY = min(minY, pt.second); maxY = max(maxY, pt.second);
    }
    float rx = (maxX - minX) != 0.0f ? (maxX - minX) : 1.0f;
    float ry = (maxY - minY) != 0.0f ? (maxY - minY) : 1.0f;
    float sx = (float)(w - 4) / rx;
    float sy = (float)(h - 4) / ry;

    for (auto& pt : p) {
        int px = x + 2 + (int)((pt.first - minX) * sx);
        int py = y + h - 2 - (int)((pt.second - minY) * sy);
        if (px >= x + 1 && px < x + w - 1 && py >= y + 1 && py < y + h - 1)
            s[py * nScreenWidth + px] = CHAR_FULL;
    }

    if (showPlayer) {
        std::lock_guard<std::mutex> lk(g_player_mutex);
        if (player.fDistance <= fTotalTrackLength && !p.empty()) {
            int idx = (int)((player.fDistance / (fTotalTrackLength > 0 ? fTotalTrackLength : 1.0f)) * (int)p.size());
            idx = min(idx, (int)p.size() - 1);
            auto pos = p[idx];
            int px = x + 2 + (int)((pos.first - minX) * sx);
            int py = y + h - 2 - (int)((pos.second - minY) * sy);
            if (px >= x && px < x + w && py >= y && py < y + h)
                s[py * nScreenWidth + px] = L'★';
        }
    }

    // Draw obstacles as 'X'
    if (!p.empty()) {
        float acc = 0.0f;
        for (size_t si = 0; si < vecTrack.size(); ++si) {
            const auto& seg = vecTrack[si];
            for (const auto& obs : seg.vecObstacles) {
                float globalObsDist = acc + obs.fSegDistance;
                if (globalObsDist >= 0 && globalObsDist <= fTotalTrackLength) {
                    int idx = (int)((globalObsDist / (fTotalTrackLength > 0 ? fTotalTrackLength : 1.0f)) * (int)p.size());
                    idx = max(0, min((int)p.size() - 1, idx));
                    auto opos = p[idx];
                    int px = x + 2 + (int)((opos.first - minX) * sx);
                    int py = y + h - 2 - (int)((opos.second - minY) * sy);
                    if (px >= x + 1 && px < x + w - 1 && py >= y + 1 && py < y + h - 1)
                        s[py * nScreenWidth + px] = L'╳';
                }
            }
            acc += seg.fDistance;
        }
    }
}

// =================================================================
// Boundary & Collision
// =================================================================
void EnforceBoundaryProtection() {
    std::lock_guard<std::mutex> lk(g_player_mutex);
    float playerLeft = player.fX_Register - PLAYER_HALF_WIDTH;
    float playerRight = player.fX_Register + PLAYER_HALF_WIDTH;
    if (playerLeft <= -ROAD_WIDTH_LIMIT || playerRight >= ROAD_WIDTH_LIMIT) {
        if (!player.bCrashed) {
            player.bCrashed = true;
            player.fSpeed = 0.0f;
            currentState = GAME_OVER;
            sound_crash.store(true);
            sound_gameover.store(true);
        }
    }
}

void CheckObstacleCollision() {
    std::lock_guard<std::mutex> lk(g_player_mutex);
    if (player.bCrashed || currentState.load() != KERNEL_RUNNING) return;

    float pos = player.fDistance;
    int section = 0;
    float fSegStartDistance = 0.0f;
    while (section < (int)vecTrack.size() && pos >= vecTrack[section].fDistance) {
        pos -= vecTrack[section].fDistance;
        fSegStartDistance += vecTrack[section].fDistance;
        section++;
    }
    if (section < (int)vecTrack.size()) {
        const TrackSegment& currentSeg = vecTrack[section];
        float fDistInSeg = pos;
        for (const auto& obs : currentSeg.vecObstacles) {
            if (fDistInSeg >= obs.fSegDistance - 0.5f && fDistInSeg <= obs.fSegDistance + 0.5f) {
                float fPlayerLeft = player.fX_Register - PLAYER_HALF_WIDTH;
                float fPlayerRight = player.fX_Register + PLAYER_HALF_WIDTH;
                float fObsLeft = obs.fOffsetX - obs.fWidth / 2.0f;
                float fObsRight = obs.fOffsetX + obs.fWidth / 2.0f;
                if (max(fPlayerLeft, fObsLeft) < min(fPlayerRight, fObsRight)) {
                    player.bCrashed = true;
                    player.fSpeed = 0.0f;
                    currentState = GAME_OVER;
                    sound_crash.store(true);
                    sound_gameover.store(true);
                    return;
                }
            }
        }
    }
}

// =================================================================
// Input Thread
// =================================================================
void InputThreadProc() {
    bool lastSpace = false, lastUp = false, lastDown = false;
    bool last1 = false, last2 = false, last3 = false;
    while (running.load()) {
        int steer = 0;
        if ((GetAsyncKeyState('A') & 0x8000) || (GetAsyncKeyState(VK_LEFT) & 0x8000)) steer = -1;
        if ((GetAsyncKeyState('D') & 0x8000) || (GetAsyncKeyState(VK_RIGHT) & 0x8000)) steer = 1;
        input_steer.store(steer);
        input_accel.store((GetAsyncKeyState('W') & 0x8000) || (GetAsyncKeyState(VK_UP) & 0x8000));
        input_brake.store((GetAsyncKeyState('S') & 0x8000) || (GetAsyncKeyState(VK_DOWN) & 0x8000));

        bool nowSpace = (GetAsyncKeyState(VK_SPACE) & 0x8000);
        if (nowSpace && !lastSpace) input_space_edge.store(true);
        lastSpace = nowSpace;

        bool nowUp = (GetAsyncKeyState(VK_UP) & 0x8000);
        if (nowUp && !lastUp) input_up_edge.store(true);
        lastUp = nowUp;

        bool nowDown = (GetAsyncKeyState(VK_DOWN) & 0x8000);
        if (nowDown && !lastDown) input_down_edge.store(true);
        lastDown = nowDown;

        bool now1 = (GetAsyncKeyState('1') & 0x8000);
        if (now1 && !last1) input_1_edge.store(true); last1 = now1;
        bool now2 = (GetAsyncKeyState('2') & 0x8000);
        if (now2 && !last2) input_2_edge.store(true); last2 = now2;
        bool now3 = (GetAsyncKeyState('3') & 0x8000);
        if (now3 && !last3) input_3_edge.store(true); last3 = now3;

        if (GetAsyncKeyState(VK_ESCAPE) & 1) input_escape.store(true);

        Sleep(1);
    }
}

// =================================================================
// Physics Thread
// =================================================================
void PhysicsThreadProc() {
    using clock = chrono::high_resolution_clock;
    const double dt = DELTA_T;
    auto last = clock::now();
    double accumulator = 0.0;

    while (running.load()) {
        auto now = clock::now();
        accumulator += chrono::duration<double>(now - last).count();
        last = now;

        while (accumulator >= dt) {
            {
                std::lock_guard<std::mutex> lk(g_player_mutex);
                if (currentState.load() == KERNEL_RUNNING) {
                    int steer = input_steer.load();
                    player.nSteerState = steer;

                    if (!player.bCrashed) {
                        if (input_accel.load()) player.fSpeed += ACCELERATION * (float)dt;
                        else player.fSpeed *= FRICTION;
                        if (input_brake.load()) player.fSpeed -= DECELERATION * (float)dt;
                    } else {
                        player.fSpeed = 0.0f;
                    }

                    player.fSpeed = max(-15.0f, min(MAX_SPEED, player.fSpeed));
                    player.fDistance += player.fSpeed * (float)dt;

                    if (player.fDistance >= fTotalTrackLength) {
                        player.fDistance = fTotalTrackLength;
                        currentState = GAME_WIN;
                        sound_win.store(true);
                    }

                    float pos = player.fDistance;
                    float targetCurv = 0.0f;
                    int section = 0;
                    while (section < (int)vecTrack.size() && pos >= vecTrack[section].fDistance) {
                        pos -= vecTrack[section].fDistance;
                        section++;
                    }
                    if (section < (int)vecTrack.size())
                        targetCurv = vecTrack[section].fCurvature;

                    player.fCurvature += (targetCurv - player.fCurvature) * (float)dt * 3.0f;
                    player.fPlayerCurvature += player.fCurvature * (float)dt * player.fSpeed * 0.01f;

                    float steerInput = (float)player.nSteerState * 0.5f;
                    float fInertiaSlide = -player.fCurvature * player.fSpeed * LATERAL_FACTOR;
                    float compensation = steerInput * STEER_COMPENSATION;
                    float headingDrift = player.fHeadingAngle * player.fSpeed * HEADING_DRIFT_FACTOR;
                    float fNetForce = (fInertiaSlide + compensation + headingDrift) * 40.0f;
                    player.fX_Register += fNetForce * (float)dt;

                    if (player.nSteerState == -1) player.fHeadingAngle -= HEADING_TURN_SPEED * (float)dt;
                    else if (player.nSteerState == 1) player.fHeadingAngle += HEADING_TURN_SPEED * (float)dt;
                    else player.fHeadingAngle *= 0.95f;
                }
            }

            EnforceBoundaryProtection();
            CheckObstacleCollision();

            // Obstacle warning
            if (currentState.load() == KERNEL_RUNNING) {
                float playerDist = player.fDistance;
                const float WARNING_RANGE = 50.0f;
                bool found = false;
                float foundDistDelta = 0.0f, foundOffsetX = 0.0f;
                float acc = 0.0f;
                for (size_t si = 0; si < vecTrack.size() && !found; ++si) {
                    const auto& seg = vecTrack[si];
                    for (const auto& obs : seg.vecObstacles) {
                        float globalObsDist = acc + obs.fSegDistance;
                        if (globalObsDist > playerDist) {
                            float delta = globalObsDist - playerDist;
                            if (delta <= WARNING_RANGE) {
                                found = true;
                                foundDistDelta = delta;
                                foundOffsetX = obs.fOffsetX;
                                break;
                            }
                        }
                    }
                    acc += seg.fDistance;
                }
                if (found) {
                    warnObstacle.store(true);
                    warnObstacleDist.store(foundDistDelta);
                    warnObstacleOffsetX.store(foundOffsetX);
                } else {
                    warnObstacle.store(false);
                }
            } else {
                warnObstacle.store(false);
            }

            accumulator -= dt;
        }
        Sleep(0);
    }
}

// =================================================================
// Render Thread
// =================================================================
void RenderThreadProc() {
    using clock = chrono::high_resolution_clock;
    const double frameMs = 1000.0 / FRAME_RATE;

    int nSelectedMap = 1;
    wstring maps[3] = {
        L"1. No Obstacles",
        L"2. Obstacles",
        L"3. More Obstacles"
    };
    wstring desc[6] = {
        L"LEVEL 1", L"General rural roads",
        L"LEVEL 2", L"City roads",
        L"LEVEL 3", L"Cyber ​​Road"
    };

    auto last = clock::now();
    static float fCameraCurvature = 0.0f;
    static float fCameraPlayerCurvature = 0.0f;
    static double fTotalTime = 0.0;

    while (running.load()) {
        auto start = clock::now();
        chrono::duration<double, milli> elapsed = start - last;
        last = start;
        double frameDeltaTime = elapsed.count() / 1000.0;
        if (currentState.load() == KERNEL_RUNNING) fTotalTime += frameDeltaTime;

        vector<wchar_t> localBuf(nScreenWidth * nScreenHeight, CHAR_EMPTY);
        vector<WORD> localColor(nScreenWidth * nScreenHeight, 0x07);

        GameState st = currentState.load();
        int horizonY = nScreenHeight / 2;

        // BOOT MENU
        if (st == BOOT_MENU) {
            KernelDrawBox(localBuf.data(), 35, 14, 50, 12);
            KernelDrawString(localBuf.data(), 50, 18, L"OS RACER : KERNEL vX.Y (MT)");
            KernelDrawString(localBuf.data(), 48, 22, L"[ PRESS SPACE TO START ]");
            if (input_space_edge.exchange(false)) currentState = MAP_SELECT;
            if (input_escape.exchange(false)) currentState = SYSTEM_HALT;
        }
        // MAP SELECT
        else if (st == MAP_SELECT) {
            KernelDrawBox(localBuf.data(), 15, 8, 40, 14);
            KernelDrawString(localBuf.data(), 26, 10, L"SELECT TRACK");
            for (int i = 0; i < 3; i++) {
                wstring txt = (nSelectedMap == i + 1) ? L"▶ " + maps[i] : L"  " + maps[i];
                KernelDrawString(localBuf.data(), 18, 13 + i * 2, txt);
            }
            KernelDrawBox(localBuf.data(), 15, 23, 40, 7);
            KernelDrawString(localBuf.data(), 17, 24, L"DESCRIPTION:");
            KernelDrawString(localBuf.data(), 17, 25, desc[(nSelectedMap - 1) * 2]);
            KernelDrawString(localBuf.data(), 17, 26, desc[(nSelectedMap - 1) * 2 + 1]);
            KernelDrawString(localBuf.data(), 20, 28, L"[↑↓] Select [SPACE] Start");
            DrawTrackView(localBuf.data(), 65, 8, 40, 22, vecMapPreview[nSelectedMap - 1], false);

            if (input_up_edge.exchange(false)) nSelectedMap = max(1, nSelectedMap - 1);
            if (input_down_edge.exchange(false)) nSelectedMap = min(3, nSelectedMap + 1);
            if (input_1_edge.exchange(false)) nSelectedMap = 1;
            if (input_2_edge.exchange(false)) nSelectedMap = 2;
            if (input_3_edge.exchange(false)) nSelectedMap = 3;
            if (input_space_edge.exchange(false)) {
                LoadMap(nSelectedMap);
                { std::lock_guard<std::mutex> lk(g_player_mutex); player.Reset(); }
                fCameraCurvature = fCameraPlayerCurvature = 0.0f;
                fTotalTime = 0.0;
                currentState = KERNEL_RUNNING;
            }
            if (input_escape.exchange(false)) currentState = BOOT_MENU;
        }
        // RACING
        else if (st == KERNEL_RUNNING || st == GAME_WIN || st == GAME_OVER) {
            float pX = 0.0f, pSpeed = 0.0f, pDist = 0.0f, pCurv = 0.0f;
            bool pCrashed = false;
            {
                std::lock_guard<std::mutex> lk(g_player_mutex);
                pX = player.fX_Register; pSpeed = player.fSpeed; pDist = player.fDistance;
                pCrashed = player.bCrashed; pCurv = player.fCurvature;
            }

            float fCameraDistance = max(0.0f, pDist - CAMERA_LAG_DISTANCE);
            float camTargetCurv = 0.0f;
            float camPos = fCameraDistance;
            int camSection = 0;
            if (fCameraDistance < fTotalTrackLength) {
                while (camSection < (int)vecTrack.size() && camPos >= vecTrack[camSection].fDistance) {
                    camPos -= vecTrack[camSection].fDistance;
                    camSection++;
                }
                if (camSection < (int)vecTrack.size())
                    camTargetCurv = vecTrack[camSection].fCurvature;
            }

            fCameraCurvature += (camTargetCurv - fCameraCurvature) * (float)frameDeltaTime * 3.0f;
            fCameraPlayerCurvature += fCameraCurvature * (float)frameDeltaTime * pSpeed * 0.01f;

            float fBgOffset = fCameraPlayerCurvature * 200.0f - pX * 30.0f;

            // ==================== BACKGROUND ====================
            // Speed-based background motion blur effect
            float speedBlurFactor = 1.0f + (pSpeed / MAX_SPEED) * 0.5f; // 1.0x to 1.5x
            
            for (int y = 0; y < horizonY; ++y) {
                for (int x = 0; x < nScreenWidth; ++x) {
                    int idx = y * nScreenWidth + x;
                    wchar_t& pixelChar = localBuf[idx];

                    if (g_currentMapId == 1) { // Retro Grid (多層次山脈)

                        float f2 = (x + fBgOffset * 0.1f * speedBlurFactor) * 0.07f;
                        int h2 = (int)(fabs(sinf(f2)) * 8.0f + 3.0f);
                        if (y >= horizonY - h2) {
                            localBuf[idx] = CHAR_MED; // 使用中等字符
                            localColor[idx] = FOREGROUND_GREEN  | FOREGROUND_GREEN | 0; 
                        }

                        float f1 = (x + fBgOffset * 0.2f * speedBlurFactor) * 0.08f;
                        int h1 = (int)(fabs(sinf(f1)) * 10.0f + 4.0f);
                        if (y >= horizonY - h1) {
                            pixelChar = ((x + y) % 2 == 0) ? CHAR_LIGHT : CHAR_MED; 
                            localColor[idx] = FOREGROUND_GREEN | FOREGROUND_INTENSITY; // 亮青綠色
                        }
                        
                    }
                    else if (g_currentMapId == 2) { // Cyber City
                        float cityOffset = x + fBgOffset * speedBlurFactor;
                        int bIndex = (int)(cityOffset / 6.0f);
                        float rHeight = fabs(sinf(bIndex * 132.5f) + sinf(bIndex * 45.1f) * 0.5f);
                        int h = (int)(rHeight * 8.0f + 4.0f);
                        int bIndex2 = (int)((cityOffset + 100.0f) / 4.0f);
                        float rHeight2 = fabs(sinf(bIndex2 * 99.3f));
                        int h2 = (int)(rHeight2 * 6.0f + 2.0f);

                        if (y >= horizonY - h) {
                            if (rHeight > 0.4f && x % 3 != 0 && y % 3 != 0 && y > horizonY - h + 3)
                                pixelChar = CHAR_LIGHT; // windows
                            else
                                pixelChar = CHAR_FULL;
                            if (pixelChar == CHAR_FULL) {
                                localColor[idx] = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY; // 粉紅建築
                            }
                        }
                        else if (y >= horizonY - h2) {
                            pixelChar = CHAR_MED;
                        }
                    }
                    else if (g_currentMapId == 3) { // Pure Space
                        int starX = (int)(x + fBgOffset * 0.1f * speedBlurFactor);
                        int noise = (starX ^ (y * 57)) * 1664525;
                        if ((noise & 0xFF) > 253) pixelChar = L'★';
                        else if ((noise & 0xFF) > 245) pixelChar = L'.';
                    }
                }
            }

            // ==================== ROAD ====================
            wchar_t roadMainChar = CHAR_DARK;
            wchar_t roadStripeChar = CHAR_FULL;
            wchar_t groundChar = CHAR_LIGHT;

            if (g_currentMapId == 1) {
                roadMainChar = CHAR_MED;    // 可見的中亮道路
                roadStripeChar = CHAR_FULL;
                groundChar = CHAR_DARK;
            }
            else if (g_currentMapId == 2) {
                roadMainChar = CHAR_DARK;   // 深色瀝青
                roadStripeChar = CHAR_FULL;
                groundChar = CHAR_LIGHT;
            }
            else if (g_currentMapId == 3) {
                roadMainChar = CHAR_MED;
                roadStripeChar = CHAR_FULL;
                groundChar = CHAR_EMPTY;
            }

            for (int y = 0; y < nScreenHeight / 2; y++) {
                float pers = (float)y / (nScreenHeight / 2);
                float mid = 0.5f + fCameraCurvature * powf(1.0f - pers, 3.0f) - pX * 0.5f;
                float roadW = 0.1f + pers * 0.9f;
                float clipW = roadW * 0.12f;
                roadW *= 0.5f;
                int row = horizonY + y;
                float fDistToHorizon = (1.0f / (pers + 0.01f)) * 5.0f;
                float fWorldDist = fCameraDistance + fDistToHorizon;
                bool bDrawFinishLine = (fWorldDist >= fTotalTrackLength - 3.0f && fWorldDist <= fTotalTrackLength + 5.0f);
                
                // Speed-based stripe animation (faster speed = faster moving stripes)
                float speedFactor = 1.0f + (pSpeed / MAX_SPEED) * 2.0f; // 1x to 3x speed
                float stripeOffset = pDist * 0.2f * speedFactor;

                for (int x = 0; x < nScreenWidth; x++) {
                    float wx = (float)x / nScreenWidth;
                    int nPixel = row * nScreenWidth + x;
                    int stripe = (int)(25 * powf(1.0f - pers, 2.5f) + stripeOffset) % 2;

                    if (wx >= mid - roadW && wx <= mid + roadW) {
                        if (bDrawFinishLine && pDist < fTotalTrackLength) {
                            bool check = ((int)(wx * 40) + (int)(y)) % 2 == 0;
                            localBuf[nPixel] = check ? CHAR_FULL : CHAR_EMPTY;
                        } else {
                            localBuf[nPixel] = roadMainChar;
                            if (fabs(wx - mid) < 0.005f && stripe)
                                localBuf[nPixel] = roadStripeChar;
                        }
                    }
                    else if (wx >= mid - roadW - clipW && wx <= mid + roadW + clipW) {
                        localBuf[nPixel] = (g_currentMapId == 1) ? CHAR_FULL : (stripe ? roadStripeChar : roadMainChar);
                        if (g_currentMapId == 2) {
                            localBuf[nPixel] = CHAR_FULL; 
                            bool isRed = ((int)(fWorldDist / 5.0f)) % 2 == 0;
                            if (isRed) {
                                localColor[nPixel] = FOREGROUND_RED | FOREGROUND_INTENSITY;
                            } else {
                                localColor[nPixel] = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
                            }
                        }
                    }
                    else {
                        localBuf[nPixel] = groundChar;
                    }

                    // Level 3 彩虹道路邊緣
                    if (g_currentMapId == 3 && row >= horizonY) {
                        if ((wx >= mid - roadW - clipW && wx <= mid - roadW) ||
                            (wx >= mid + roadW && wx <= mid + roadW + clipW)) {
                            int idx = ((int)pDist + x) % 7;
                            WORD rainbow[7] = {
                                FOREGROUND_RED | FOREGROUND_INTENSITY,
                                FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY,
                                FOREGROUND_GREEN | FOREGROUND_INTENSITY,
                                FOREGROUND_GREEN,
                                FOREGROUND_BLUE | FOREGROUND_INTENSITY,
                                FOREGROUND_RED | FOREGROUND_BLUE,
                                FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY
                            };
                            localColor[nPixel] = rainbow[idx];
                        }
                    }
                }

                // Obstacles (holes)
                if (camSection < (int)vecTrack.size()) {
                    const TrackSegment& seg = vecTrack[camSection];
                    float fDistInCamSeg = camPos + fDistToHorizon;
                    for (const auto& obs : seg.vecObstacles) {
                        if (fDistInCamSeg >= obs.fSegDistance && fDistInCamSeg < obs.fSegDistance + 10.0f) {
                            float fObstacleX = mid + obs.fOffsetX * roadW * 2.0f;
                            int nObsCenter = (int)(fObstacleX * nScreenWidth);
                            int nObsPixelWidth = (int)(obs.fWidth * roadW * nScreenWidth * 2.0f);
                            int nObsStart = nObsCenter - nObsPixelWidth / 2;
                            int nObsEnd = nObsCenter + nObsPixelWidth / 2;
                            for (int x = max(0, nObsStart); x < min(nScreenWidth, nObsEnd); ++x) {
                                localBuf[row * nScreenWidth + x] = L' ';
                            }
                        }
                    }
                }
            }

            // Player Car
            const int CAR_RENDER_ROW_Y = 28;
            int Y_INDEX = CAR_RENDER_ROW_Y - nScreenHeight / 2;
            float pers_car = (float)Y_INDEX / (nScreenHeight / 2);
            float mid_car = 0.5f + fCameraCurvature * powf(1.0f - pers_car, 3.0f) - pX * 0.5f;
            float car_x_norm = mid_car + pX * 0.5f;
            int car_x_center = (int)(car_x_norm * nScreenWidth);
            int nSteer = player.nSteerState;

            vector<wstring> carSprite;
            if (nSteer == 0) {          // straight
                carSprite = {
                    L"   ||####||   ",
                    L"      ##      ",
                    L"     ####     ",
                    L"|||########|||",
                    L"|||  ####  |||"
                };
            }
            else if (nSteer > 0) {      // turning right
                carSprite = {
                    L"      //####//",
                    L"        ##    ",
                    L"      ####    ",
                    L"/// ########//",
                    L"///   #### ///"
                };
            }
            else {                      // turning left
                carSprite = {
                    L"\\\\####\\\\      ",
                    L"    ##        ",
                    L"    ####      ",
                    L"\\\\######## \\\\\\",
                    L"\\\\\\ ####   \\\\\\"
                };
            }


            int sprite_height = (int)carSprite.size();
            int sprite_width = 14;
            for (int i = 0; i < sprite_height; ++i) {
                int draw_y = CAR_RENDER_ROW_Y - (sprite_height - 1) + i;
                if (draw_y < 0 || draw_y >= nScreenHeight) continue;
                int draw_x_start = car_x_center - (sprite_width / 2);
                for (int cx = 0; cx < sprite_width && cx < (int)carSprite[i].size(); ++cx) {
                    int target_x = draw_x_start + cx;
                    if (target_x < 0 || target_x >= nScreenWidth) continue;
                    wchar_t ch = carSprite[i][cx];
                    if (ch != L' ') localBuf[draw_y * nScreenWidth + target_x] = ch;
                }
            }

            // HUD
            KernelDrawBox(localBuf.data(), 1, 1, 30, 11);
            KernelDrawString(localBuf.data(), 3, 2, L"SYSTEM MONITOR");
            wchar_t buf[80];
            swprintf_s(buf, L"DIST : %.0f / %.0f", pDist, fTotalTrackLength);
            KernelDrawString(localBuf.data(), 3, 4, buf);
            swprintf_s(buf, L"TIME : %.2f sec", fTotalTime);
            KernelDrawString(localBuf.data(), 3, 6, buf);
            
            // Speed with visual indicator
            int speedPercent = (int)((pSpeed / MAX_SPEED) * 100.0f);
            swprintf_s(buf, L"SPEED: %3d km/h", (int)pSpeed);
            KernelDrawString(localBuf.data(), 3, 8, buf);
            
            // Speed bar
            int barWidth = 24;
            int filledWidth = (int)((pSpeed / MAX_SPEED) * barWidth);
            wstring speedBar = L"[";
            for (int i = 0; i < barWidth; i++) {
                if (i < filledWidth) {
                    // Color based on speed: green -> yellow -> red
                    if (i < barWidth / 3) speedBar += L"█";
                    else if (i < barWidth * 2 / 3) speedBar += L"▓";
                    else speedBar += L"▒";
                } else {
                    speedBar += L" ";
                }
            }
            speedBar += L"]";
            KernelDrawString(localBuf.data(), 3, 9, speedBar);
            
            // Speed effect indicator
            if (pSpeed > MAX_SPEED * 0.7f) {
                KernelDrawString(localBuf.data(), 3, 10, L">>> HIGH SPEED <<<");
            }

            if (warnObstacle.load()) {
                swprintf_s(buf, L"OBST: %.0f m", warnObstacleDist.load());
                KernelDrawString(localBuf.data(), 3, 5, buf);
            }

			DrawTrackView(localBuf.data(), nScreenWidth - 33, 1, 31, 15, vecMapPointsCurrent, true);

            // ==================== [START] 設置儀表板和地圖背景為白色 ====================
            const WORD WHITE_BACKGROUND = BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE;
            
            // 設置儀表板背景
            int hudX = 1, hudY = 1, hudW = 29, hudH = 10;
            for (int y = hudY; y <= hudY + hudH; ++y) {
                if (y >= nScreenHeight) break;
                for (int x = hudX; x <= hudX + hudW; ++x) {
                    if (x >= nScreenWidth) break;
                    // 保留前景文字顏色，只修改背景為白色 (0x0F 是白字黑底，需要修改)
                    localColor[y * nScreenWidth + x] = (localColor[y * nScreenWidth + x] & 0xF0) | WHITE_BACKGROUND;
                }
            }
            
            // 設置地圖鳥瞰圖背景
            int mapX = nScreenWidth - 33, mapY = 1, mapW = 31, mapH = 14;
            for (int y = mapY; y <= mapY + mapH; ++y) {
                if (y >= nScreenHeight) break;
                for (int x = mapX; x <= mapX + mapW; ++x) {
                    if (x >= nScreenWidth) break;
                    // 保留前景文字顏色，只修改背景為白色
                    localColor[y * nScreenWidth + x] = (localColor[y * nScreenWidth + x] & 0xF0) | WHITE_BACKGROUND;
                }
            }

            if (st == GAME_OVER) {
                // Enhanced GAME OVER screen
                KernelDrawBox(localBuf.data(), 35, 10, 50, 16);
                KernelDrawString(localBuf.data(), 52, 12, L"╔══════════════════╗");
                KernelDrawString(localBuf.data(), 52, 13, L"║                  ║");
                KernelDrawString(localBuf.data(), 52, 14, L"║   GAME  OVER     ║");
                KernelDrawString(localBuf.data(), 52, 15, L"║                  ║");
                KernelDrawString(localBuf.data(), 52, 16, L"╚══════════════════╝");
                
                KernelDrawString(localBuf.data(), 48, 18, L"!! CRASHED !!");
                KernelDrawString(localBuf.data(), 45, 20, L"Final Distance: ");
                swprintf_s(buf, L"%.0f / %.0f", pDist, fTotalTrackLength);
                KernelDrawString(localBuf.data(), 60, 20, buf);
                KernelDrawString(localBuf.data(), 45, 21, L"Time: ");
                swprintf_s(buf, L"%.2f sec", fTotalTime);
                KernelDrawString(localBuf.data(), 51, 21, buf);
                KernelDrawString(localBuf.data(), 45, 22, L"Final Speed: ");
                swprintf_s(buf, L"%d km/h", (int)pSpeed);
                KernelDrawString(localBuf.data(), 58, 22, buf);
                
                KernelDrawString(localBuf.data(), 46, 24, L"[SPACE] Return to Menu");
                KernelDrawString(localBuf.data(), 46, 25, L"[ESC] Exit Game");
                
                // Red tint for crash effect
                for (int y = 10; y < 26; y++) {
                    for (int x = 35; x < 85; x++) {
                        if (x < nScreenWidth && y < nScreenHeight) {
                            int idx = y * nScreenWidth + x;
                            if (localBuf[idx] != CHAR_EMPTY) {
                                localColor[idx] = (localColor[idx] & 0xF0) | FOREGROUND_RED | FOREGROUND_INTENSITY;
                            }
                        }
                    }
                }
                
                if (input_space_edge.exchange(false)) {
                    { std::lock_guard<std::mutex> lk(g_player_mutex); player.Reset(); }
                    currentState = MAP_SELECT;
                }
                if (input_escape.exchange(false)) currentState = SYSTEM_HALT;
            }
            if (st == GAME_WIN) {
                // Enhanced VICTORY screen with animation
                static float victoryAnimTime = 0.0f;
                victoryAnimTime += (float)frameDeltaTime;
                
                // Animated border effect
                int animOffset = (int)(sinf(victoryAnimTime * 2.0f) * 2.0f);
                
                // Large victory box
                KernelDrawBox(localBuf.data(), 30, 5, 60, 20);
                
                // Decorative top border
                KernelDrawString(localBuf.data(), 35, 6, L"╔═══════════════════════════════════════════╗");
                KernelDrawString(localBuf.data(), 35, 7, L"║                                           ║");
                
                // Large VICTORY text with animation (simplified to fit screen)
                int titleY = 8;
                KernelDrawString(localBuf.data(), 42 + animOffset, titleY,     L"╔╗  ╦ ╦╔═╗╔═╗╔╦╗╦ ╦╔═╗╦");
                KernelDrawString(localBuf.data(), 42 + animOffset, titleY + 1, L"╠╩╗ ║║║╠═╣║   ║ ╠═╣║ ╦║");
                KernelDrawString(localBuf.data(), 42 + animOffset, titleY + 2, L"╚═╝ ╚╩╝╩ ╩╚═╝ ╩ ╩ ╩╚═╝╩");
                KernelDrawString(localBuf.data(), 48 + animOffset, titleY + 4, L"★ ★ ★ ★ ★");
                
                // Separator
                KernelDrawString(localBuf.data(), 35, 14, L"║                                           ║");
                KernelDrawString(localBuf.data(), 35, 15, L"╠═══════════════════════════════════════════╣");
                KernelDrawString(localBuf.data(), 35, 16, L"║                                           ║");
                
                // Statistics section
                int statY = 17;
                KernelDrawString(localBuf.data(), 37, statY, L"╔═══════════════════════════════════════╗");
                KernelDrawString(localBuf.data(), 37, statY + 1, L"║  RACE STATISTICS                     ║");
                KernelDrawString(localBuf.data(), 37, statY + 2, L"╠═══════════════════════════════════════╣");
                
                // Calculate statistics
                float avgSpeed = fTotalTime > 0.0f ? (fTotalTrackLength / fTotalTime) : 0.0f;
                
                // Display stats
                KernelDrawString(localBuf.data(), 39, statY + 3, L"║  Total Distance: ");
                swprintf_s(buf, L"%.0f units", fTotalTrackLength);
                KernelDrawString(localBuf.data(), 58, statY + 3, buf);
                KernelDrawString(localBuf.data(), 72, statY + 3, L"║");
                
                KernelDrawString(localBuf.data(), 39, statY + 4, L"║  Completion Time: ");
                swprintf_s(buf, L"%.2f sec", fTotalTime);
                KernelDrawString(localBuf.data(), 59, statY + 4, buf);
                KernelDrawString(localBuf.data(), 72, statY + 4, L"║");
                
                KernelDrawString(localBuf.data(), 39, statY + 5, L"║  Average Speed: ");
                swprintf_s(buf, L"%.1f km/h", avgSpeed);
                KernelDrawString(localBuf.data(), 57, statY + 5, buf);
                KernelDrawString(localBuf.data(), 72, statY + 5, L"║");
                
                // Performance rating
                wstring rating;
                if (fTotalTime < fTotalTrackLength / 30.0f) {
                    rating = L"EXCELLENT!";
                } else if (fTotalTime < fTotalTrackLength / 25.0f) {
                    rating = L"GREAT!";
                } else if (fTotalTime < fTotalTrackLength / 20.0f) {
                    rating = L"GOOD!";
                } else {
                    rating = L"COMPLETED!";
                }
                
                KernelDrawString(localBuf.data(), 39, statY + 6, L"║  Performance: ");
                KernelDrawString(localBuf.data(), 56, statY + 6, rating);
                KernelDrawString(localBuf.data(), 72, statY + 6, L"║");
                
                KernelDrawString(localBuf.data(), 37, statY + 7, L"╚═══════════════════════════════════════╝");
                
                // Instructions
                KernelDrawString(localBuf.data(), 35, 22, L"║                                       ║");
                KernelDrawString(localBuf.data(), 42, 23, L"[SPACE] Return  [ESC] Exit");
                KernelDrawString(localBuf.data(), 35, 24, L"╚═══════════════════════════════════════╝");
                
                // Animated color effect - rainbow/gold shimmer (within bounds)
                int boxX = 30, boxY = 5, boxW = 60, boxH = 20;
                for (int y = boxY; y < boxY + boxH && y < nScreenHeight; y++) {
                    for (int x = boxX; x < boxX + boxW && x < nScreenWidth; x++) {
                        int idx = y * nScreenWidth + x;
                        if (localBuf[idx] != CHAR_EMPTY) {
                            // Create shimmer effect
                            int colorPhase = (int)(victoryAnimTime * 3.0f + x + y) % 6;
                            WORD colors[] = {
                                FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY, // Gold
                                FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY, // White
                                FOREGROUND_GREEN | FOREGROUND_INTENSITY, // Green
                                FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY, // Gold
                                FOREGROUND_RED | FOREGROUND_INTENSITY, // Red
                                FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY // Gold
                            };
                            localColor[idx] = (localColor[idx] & 0xF0) | colors[colorPhase];
                        }
                    }
                }
                
                if (input_space_edge.exchange(false)) {
                    { std::lock_guard<std::mutex> lk(g_player_mutex); player.Reset(); }
                    victoryAnimTime = 0.0f;
                    currentState = MAP_SELECT;
                }
                if (input_escape.exchange(false)) currentState = SYSTEM_HALT;
            }
        }
        else if (st == SYSTEM_HALT) {
            running = false;
        }

        // Write to console
        {
            std::lock_guard<std::mutex> lk(g_screen_mutex);
            DWORD dw;
            WriteConsoleOutputCharacterW(hConsole, localBuf.data(), nScreenWidth * nScreenHeight, {0,0}, &dw);
            WriteConsoleOutputAttribute(hConsole, localColor.data(), nScreenWidth * nScreenHeight, {0,0}, &dw);
        }

        auto end = clock::now();
        chrono::duration<double, milli> renderElapsed = end - start;
        double ms = renderElapsed.count();
        if (ms < frameMs) Sleep((DWORD)(frameMs - ms));
    }
}

// =================================================================
// Main
// =================================================================
int main() {
    std::locale::global(std::locale(""));
    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    
    // Lock console window size to prevent resizing
    COORD size = { (short)nScreenWidth, (short)nScreenHeight };
    SetConsoleScreenBufferSize(hConsole, size);
    
    // Set window size to match buffer size
    SMALL_RECT rect = { 0, 0, (short)(nScreenWidth - 1), (short)(nScreenHeight - 1) };
    SetConsoleWindowInfo(hConsole, TRUE, &rect);
    
    // Disable window resizing
    HWND hwnd = GetConsoleWindow();
    if (hwnd != NULL) {
        LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
        SetWindowLongPtr(hwnd, GWL_STYLE, style);
    }
    
    CONSOLE_CURSOR_INFO ci{1, false};
    SetConsoleCursorInfo(hConsole, &ci);

    InitMaps();
    currentState = BOOT_MENU;

    thread tInput(InputThreadProc);
    thread tPhysics(PhysicsThreadProc);
    thread tRender(RenderThreadProc);
    thread tSound(SoundThreadProc);

    tInput.join();
    tPhysics.join();
    tRender.join();
    tSound.join();

    return 0;
}
