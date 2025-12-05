/*
 * TTGO VGA32 MSX Emulator (Improved File Browser with Scrolling)
 * Based on fMSX by Marat Fayzullin and FabGL by Fabrizio Di Vittorio.
 */

#include "fabgl.h"
#include "MSX.h"
#include <SPI.h>
#include <SD.h>
#include <vector> // 파일 목록 관리를 위해 vector 사용 (필요시)

// [중요 수정] VGAController 대신 VGA16Controller를 사용해야 팔레트 제어가 가능합니다.
// ESP32_Port.cpp의 extern 선언과 타입을 일치시킵니다.
fabgl::VGA16Controller VGAController;
fabgl::PS2Controller PS2Controller;
fabgl::Canvas        *Canvas;

#include "esp32-hal-psram.h"
extern "C" {
  #include "esp_spiram.h"
}

#ifndef LSB_FIRST
#define LSB_FIRST
#endif

// fMSX 전역 변수 연결
extern "C" {
  extern int Mode;
  extern int RAMPages;
  extern int VRAMPages;
  extern const char *ROMName[MAXCARTS];
  extern const char *ProgDir;
  extern byte Verbose;
  extern  int SetAudio(int, int);
}

// ESP32-MSX 브릿지
#include "ESP32_MSX.h"

// PSLReg 변수 정의
byte PSLReg = 0;

// 에뮬레이터 상태
bool emuRunning = false;

// SD 카드 설정
#define SD_MISO 2
#define SD_MOSI 12
#define SD_CLK  14
#define SD_CS   13

// 파일 브라우저 설정
const int MAX_FILES = 200;      // 최대 파일 로드 개수 증가
const int ITEMS_PER_PAGE = 25;  // 한 화면에 보여줄 항목 수

void setup() {
  setCpuFrequencyMhz(240);
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n=== MSX Emulator Starting ===");

  // PSRAM 초기화 확인
  if (psramInit()) {
    Serial.printf("PSRAM initialized: %d bytes\n", ESP.getPsramSize());
    Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
  } else {
    Serial.println("ERROR: PSRAM initialization FAILED!");
    while(1) { delay(1000); }
  }

  // 입력 장치 초기화
  PS2Controller.begin(PS2Preset::KeyboardPort0_MousePort1);
  Serial.println("PS2 Controller initialized");

  // 비디오 초기화
  // VGA16Controller를 사용하므로 begin() 호출
  VGAController.begin();
  VGAController.setResolution(VGA_512x384_60Hz);
  Serial.println("VGA16 Controller initialized");

  Canvas = new fabgl::Canvas(&VGAController);
  Canvas->setBrushColor(Color::Black);
  Canvas->clear();
  Canvas->setPenColor(Color::White);
  Canvas->drawText(10, 10, "MSX Emulator Starting...");
  Canvas->waitCompletion(); // 화면 출력이 완료될 때까지 대기 (로딩 개선)

  // SD 카드 마운트
  Serial.println("Mounting SD card...");
  SPI.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);

  if (!SD.begin(SD_CS)) {
    Serial.println("ERROR: SD Card Mount Failed!");
    Canvas->setPenColor(Color::Red);
    Canvas->drawText(10, 30, "SD Card Mount Failed!");
    Canvas->waitCompletion(); // 화면 출력이 완료될 때까지 대기
    while(1) { delay(1000); }
  }

  Serial.println("SD card mounted successfully");

  // fMSX 초기 설정
  Mode = MSX_MSX1 | MSX_NTSC | MSX_GUESSA | MSX_GUESSB;
  RAMPages = 4;   
  VRAMPages = 2;  
  Verbose = 1;
  ProgDir = "/"; 

  // 메모리 시스템 초기화
  if(!InitMemorySystem()) {
    Serial.println("ERROR: InitMemorySystem() failed!");
    while(1) { delay(1000); }
  }

  // 머신 초기화
  if(!InitMachine()) {
    Serial.println("ERROR: InitMachine() failed!");
    while(1) { delay(1000); }
  }

  // 사운드 시스템 초기화
  Serial.println("Initializing sound system...");
  if(!SetAudio(22050, 127)) {
    Serial.println("WARNING: Sound initialization failed!");
  } else {
    Serial.println("Sound system initialized");
  }

  // UI 및 초기화
  Canvas->clear();
  Canvas->drawText(10, 10, "MSX Emulator Ready");
  Canvas->waitCompletion(); // 화면 출력이 완료될 때까지 대기
  
  delay(1000);
  runFileBrowser();
}

void loop() {
  if (emuRunning) {
    Serial.println("Starting MSX emulation...");
    Canvas->clear(); 

    // StartMSX 호출
    int result = StartMSX(Mode, RAMPages, VRAMPages);
    Serial.printf("StartMSX returned: %d\n", result);

    emuRunning = false;
    
    // 해상도 및 상태 재설정
    VGAController.setResolution(VGA_512x384_60Hz);
    Canvas->clear();
    
    TrashMachine();
    InitMachine();
    
    runFileBrowser();
  }
  delay(10);
}

// 개선된 파일 브라우저 (스크롤 기능 + 키 반복 방지)
void runFileBrowser() {
  File root = SD.open("/");
  if(!root) {
    Canvas->drawText(10, 50, "Failed to open root");
    return;
  }

  // 파일 목록 로딩
  int totalFiles = 0;
  String files[MAX_FILES];

  File file = root.openNextFile();
  while(file && totalFiles < MAX_FILES) {
    String fname = String(file.name());
    if(fname.startsWith("/")) fname = fname.substring(1);

    // ROM 파일만 필터링
    if(fname.endsWith(".ROM") || fname.endsWith(".rom") || fname.endsWith(".mx1") || fname.endsWith(".MX1")) {
      files[totalFiles] = fname;
      totalFiles++;
    }
    file = root.openNextFile();
  }
  root.close();

  if(totalFiles == 0) {
    Canvas->drawText(10, 70, "No ROM files found.");
  }

  bool selecting = true;
  bool redraw = true;
  fabgl::Keyboard *kb = PS2Controller.keyboard();
  
  // 전체 메뉴 항목 수 = 파일 수 + 1 (MSX BASIC 실행 메뉴)
  int totalItems = 1 + totalFiles;
  
  int selected = 0;       // 현재 선택된 커서 위치 (전체 목록 기준)
  int topItem = 0;        // 화면 맨 위에 표시될 항목의 인덱스 (스크롤 오프셋)

  // 키 반복 방지를 위한 변수
  unsigned long lastKeyTime = 0;
  const unsigned long KEY_DELAY = 150; // 키 입력 간격 (밀리초)
  fabgl::VirtualKey lastKey = fabgl::VK_NONE;

  while(selecting) {
    if (redraw) {
      Canvas->setBrushColor(Color::Black);
      Canvas->clear();

      // 헤더 출력
      Canvas->setPenColor(Color::Cyan);
      Canvas->drawText(10, 10, "Select ROM (Up/Down/Enter):");
      
      Canvas->setPenColor(Color::BrightMagenta);
      Canvas->drawLine(10, 25, 500, 25);

      // 리스트 출력 루프 (스크롤 적용)
      for(int i = 0; i < ITEMS_PER_PAGE; i++) {
        int itemIndex = topItem + i;
        
        if (itemIndex >= totalItems) break;

        int yPos = 35 + (i * 14);

        // 커서 표시
        if(itemIndex == selected) {
          Canvas->setPenColor(Color::Yellow);
          Canvas->drawText(5, yPos, ">");
          Canvas->setPenColor(Color::White);
        } else {
          Canvas->setPenColor(Color::BrightMagenta); 
        }

        // 메뉴 내용 출력
        if (itemIndex == 0) {
          Canvas->drawText(20, yPos, "[ Run MSX BASIC ]");
        } else {
          Canvas->drawText(20, yPos, files[itemIndex - 1].c_str());
        }
      }
      
      // 페이지 정보 표시
      Canvas->setPenColor(Color::BrightMagenta);
      char pageInfo[32];
      sprintf(pageInfo, "Item %d / %d", selected + 1, totalItems);
      Canvas->drawText(400, 10, pageInfo);
      
      redraw = false;
    }

    if(kb->virtualKeyAvailable()) {
      fabgl::VirtualKey key = kb->getNextVirtualKey();
      unsigned long currentTime = millis();

      // 키 반복 방지: 같은 키가 너무 빨리 반복되지 않도록 체크
      if (key != lastKey || (currentTime - lastKeyTime) >= KEY_DELAY) {
        lastKey = key;
        lastKeyTime = currentTime;

        if(key == fabgl::VK_UP) {
          if (selected > 0) {
            selected--;
            if (selected < topItem) {
              topItem = selected;
            }
            redraw = true;
          }
        }
        
        if(key == fabgl::VK_DOWN) {
          if (selected < totalItems - 1) {
            selected++;
            if (selected >= topItem + ITEMS_PER_PAGE) {
              topItem = selected - ITEMS_PER_PAGE + 1;
            }
            redraw = true;
          }
        }
        
        if(key == fabgl::VK_RETURN) {
          if (selected == 0) {
            Serial.println("Starting MSX BASIC...");
            ROMName[0] = 0; 
            ROMName[1] = 0; 
            Canvas->clear();
            Canvas->drawText(10, 10, "Booting MSX BASIC...");
          } else {
            int fileIdx = selected - 1;
            Serial.printf("Loading ROM: %s\n", files[fileIdx].c_str());
            static char fileNameBuffer[64];
            String path = "/" + files[fileIdx];
            strncpy(fileNameBuffer, path.c_str(), sizeof(fileNameBuffer)-1);
            fileNameBuffer[sizeof(fileNameBuffer)-1] = '\0';
            ROMName[0] = fileNameBuffer;
            ROMName[1] = 0;
            Canvas->clear();
            Canvas->drawText(10, 10, "Loading ROM...");
            Canvas->drawText(10, 30, files[fileIdx].c_str());
          }
          delay(500);
          emuRunning = true;
          selecting = false;
        }
        
        // Page Up/Down 기능
        if (key == fabgl::VK_PAGEUP) {
          selected -= ITEMS_PER_PAGE;
          if (selected < 0) selected = 0;
          if (selected < topItem) topItem = selected;
          redraw = true;
        }
        if (key == fabgl::VK_PAGEDOWN) {
          selected += ITEMS_PER_PAGE;
          if (selected >= totalItems) selected = totalItems - 1;
          if (selected >= topItem + ITEMS_PER_PAGE) topItem = selected - ITEMS_PER_PAGE + 1;
          redraw = true;
        }
      }
    }
    delay(10);
  }
}