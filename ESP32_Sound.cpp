#include <Arduino.h>
#include "driver/dac.h"

extern "C" {
  #include "MSX.h"
  #include "Sound.h"
  #include "AY8910.h"
  extern AY8910 PSG;
}

// 타이머 및 오디오 설정
static hw_timer_t *sampleTimer = nullptr;
static volatile int sampleRate = 22050;
static volatile int globalVolume = 64;
static volatile bool audioRunning = false;

// PSG 캐시 (ISR용)
static volatile uint16_t isr_freqReg[3] = {0,0,0};
static volatile uint8_t  isr_vol[3]     = {0,0,0};
static volatile uint8_t  isr_mixerReg   = 0xFF;

// Phase accumulator (32비트 충분히 사용)
static volatile uint32_t phase_acc[3] = {0,0,0};
static volatile uint32_t phase_inc[3] = {0,0,0};

// 노이즈 생성
static volatile uint32_t noise_lfsr = 0x1234;
static volatile uint32_t noise_counter = 0;
static volatile uint32_t noise_period = 100;
static volatile uint8_t  noise_out = 0;
static volatile uint8_t  noise_vol = 0;

// 주파수를 phase increment로 변환
static inline uint32_t freq_to_inc(float freq) {
    if (freq <= 0.0f || sampleRate <= 0) return 0;
    // 더 정확한 계산: (freq / sampleRate) * 2^32
    double ratio = (double)freq / (double)sampleRate;
    return (uint32_t)(ratio * 4294967296.0); // 2^32
}

// ISR: 샘플 생성 및 DAC 출력
void IRAM_ATTR onSampleTimer() {
    if (!audioRunning) {
        dac_output_voltage(DAC_CHANNEL_1, 128);
        return;
    }

    int32_t mix = 0;
    int activeChannels = 0;

    // 3개 톤 채널
    for (int ch = 0; ch < 3; ch++) {
        if (phase_inc[ch] > 0 && isr_vol[ch] > 0) {
            activeChannels++;
            
            // Phase 누적
            phase_acc[ch] += phase_inc[ch];
            
            // MSB로 사각파 생성 (50% duty cycle)
            int8_t output = (phase_acc[ch] & 0x80000000) ? 1 : -1;
            
            // 볼륨 적용 (0-15를 0-127로 스케일)
            int32_t amplitude = ((int32_t)isr_vol[ch] * 127) / 15;
            mix += output * amplitude;
        }
    }

    // 노이즈 채널
    if (noise_vol > 0 && noise_period > 0) {
        activeChannels++;
        noise_counter++;
        if (noise_counter >= noise_period) {
            noise_counter = 0;
            // 17-bit LFSR (더 나은 노이즈 품질)
            uint32_t bit = ((noise_lfsr >> 0) ^ (noise_lfsr >> 3)) & 1;
            noise_lfsr = (noise_lfsr >> 1) | (bit << 16);
            noise_out = (noise_lfsr & 1) ? 1 : -1;
        }
        int32_t nvol = ((int32_t)noise_vol * 127) / 15;
        mix += noise_out * nvol;
    }

    // 믹싱 (채널 수로 나누어 정규화)
    if (activeChannels > 0) {
        mix = mix / activeChannels;
    }

    // 마스터 볼륨 적용
    mix = (mix * globalVolume) / 127;

    // DAC 출력 범위로 변환 (0-255)
    int dac = 128 + (mix / 2); // -127~127 -> 0~255
    
    // 클램핑
    if (dac < 0) dac = 0;
    if (dac > 255) dac = 255;
    
    dac_output_voltage(DAC_CHANNEL_1, (uint8_t)dac);
}

// PSG 레지스터 업데이트
extern "C" void UpdatePSG() {
    static bool hadSound = false;
    
    // 인터럽트 비활성화 (critical section)
    noInterrupts();
    
    // 믹서 레지스터
    isr_mixerReg = PSG.R[7];
    
    bool hasSound = false;
    
    // 톤 채널 업데이트
    for (int i = 0; i < 3; i++) {
        uint16_t freqReg = ((PSG.R[i*2+1] & 0x0F) << 8) | PSG.R[i*2];
        bool toneEnabled = !(isr_mixerReg & (1 << i));
        uint8_t vol = PSG.R[8 + i] & 0x0F;
        
        if (toneEnabled && freqReg > 0 && vol > 0) {
            hasSound = true;
            isr_vol[i] = vol;
            
            // PSG 주파수 계산: Clock / (16 * period)
            float freq = 3579545.0f / (16.0f * (float)freqReg);
            
            // 주파수 범위 체크 (20Hz ~ 10kHz)
            if (freq < 20.0f) freq = 20.0f;
            if (freq > 10000.0f) freq = 10000.0f;
            
            phase_inc[i] = freq_to_inc(freq);
            
            if (!hadSound) {
                Serial.printf("Sound: Ch%d Freq=%.1fHz Vol=%d\n", i, freq, vol);
            }
        } else {
            isr_vol[i] = 0;
            phase_inc[i] = 0;
        }
    }
    
    // 노이즈 채널
    uint8_t noiseReg = PSG.R[6] & 0x1F;
    bool noiseEnabled = false;
    for (int i = 0; i < 3; i++) {
        if (!(isr_mixerReg & (0x08 << i))) {
            noiseEnabled = true;
            break;
        }
    }
    
    if (noiseEnabled && noiseReg > 0) {
        hasSound = true;
        float noiseFreq = 3579545.0f / (16.0f * (float)noiseReg);
        
        // 노이즈 주파수 범위 제한
        if (noiseFreq < 50.0f) noiseFreq = 50.0f;
        if (noiseFreq > 10000.0f) noiseFreq = 10000.0f;
        
        noise_period = (uint32_t)((float)sampleRate / noiseFreq);
        if (noise_period < 1) noise_period = 1;
        
        noise_vol = PSG.R[8] & 0x0F;
    } else {
        noise_vol = 0;
    }
    
    hadSound = hasSound;
    
    // 인터럽트 재활성화
    interrupts();
}

// 타이머 시작
static void startTimer(int rate) {
    Serial.println("Starting audio timer...");
    
    if (sampleTimer) {
        timerAlarmDisable(sampleTimer);
        timerDetachInterrupt(sampleTimer);
        timerEnd(sampleTimer);
        sampleTimer = nullptr;
    }
    
    // 타이머 0, 분주비 80 (1MHz = 1us per tick)
    sampleTimer = timerBegin(0, 80, true);
    if (!sampleTimer) {
        Serial.println("ERROR: Timer init failed!");
        return;
    }
    
    // ISR 연결
    timerAttachInterrupt(sampleTimer, &onSampleTimer, true);
    
    // 주기 설정
    uint64_t period_us = 1000000ULL / rate;
    timerAlarmWrite(sampleTimer, period_us, true);
    timerAlarmEnable(sampleTimer);
    
    Serial.printf("Timer: %llu us period (%d Hz)\n", period_us, rate);
}

// 오디오 초기화
extern "C" int SetAudio(int Rate, int Volume) {
    Serial.println("\n=== Audio Init ===");
    
    if (Rate <= 0) return 0;

    // DAC 초기화
    dac_output_enable(DAC_CHANNEL_1);
    dac_output_voltage(DAC_CHANNEL_1, 128);

    sampleRate = Rate;
    globalVolume = Volume;
    audioRunning = true;

    startTimer(Rate);
    
    Serial.printf("Audio ready: %d Hz, Vol=%d\n", Rate, Volume);
    return 1;
}

// PlayAllSound
extern "C" void PlayAllSound(int uSec) {
    Loop8910(&PSG, uSec);
    UpdatePSG();
}

// 정지
extern "C" void StopAudio() {
    audioRunning = false;
    if (sampleTimer) {
        timerAlarmDisable(sampleTimer);
        timerDetachInterrupt(sampleTimer);
        timerEnd(sampleTimer);
        sampleTimer = nullptr;
    }
    dac_output_disable(DAC_CHANNEL_1);
}