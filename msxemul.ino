/*
 * TTGO VGA32 MSX Emulator
 * Based on fMSX by Marat Fayzullin and FabGL by Fabrizio Di Vittorio.
 */

#include "fabgl.h"
#include "MSX.h"
#include <SPI.h>
#include <SD.h>

// FabGL 전역 객체
fabgl::VGAController VGAController;
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
  extern void TestPalette() ;

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





void setup() {
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
VGAController.begin(

  );
  VGAController.setResolution(VGA_512x384_60Hz);
  Serial.println("VGA Controller initialized");

 // 팔레트 테스트 (선택사항)
  TestPalette();

  Canvas = new fabgl::Canvas(&VGAController);
  Canvas->setBrushColor(Color::Black);
  Canvas->clear();
  Canvas->setPenColor(Color::White);
  Canvas->drawText(10, 10, "MSX Emulator Starting...");




  // SD 카드 마운트
  Serial.println("Mounting SD card...");
  SPI.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);

  if (!SD.begin(SD_CS)) {
    Serial.println("ERROR: SD Card Mount Failed!");
    Canvas->setPenColor(Color::Red);
    Canvas->drawText(10, 30, "SD Card Mount Failed!");
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

  // 사운드 시스템 초기화 (InitMachine() 이후에)
 
Serial.println("Initializing sound system...");
if(!SetAudio(22050, 127)) {
  Serial.println("WARNING: Sound initialization failed!");
  // 실패해도 계속 진행
} else {
  Serial.println("Sound system initialized");
}


  // UI 및 초기화
  Canvas->clear();
  //Canvas->setPenColor(Color::Green);
  Canvas->drawText(10, 10, "MSX Emulator Ready");
  
  delay(1000);
  runFileBrowser();
}

void loop() {
  if (emuRunning) {
    Serial.println("Starting MSX emulation...");
    Canvas->clear(); // 게임 시작 전 화면 클리어

    // StartMSX 호출
    int result = StartMSX(Mode, RAMPages, VRAMPages);
    Serial.printf("StartMSX returned: %d\n", result);

    // 에뮬레이터가 종료되면 다시 파일 브라우저로
    emuRunning = false;
    
    // 해상도가 변경되었을 수 있으므로 재설정
    VGAController.setResolution(VGA_512x384_60Hz);
    //Canvas->setBrushColor(Color::Black);
    Canvas->clear();
    
    // 머신 상태 재설정 (필요시)
    TrashMachine();
    InitMachine();
    
    runFileBrowser();
  }
  delay(10);
}

// 파일 브라우저 개선: "Start MSX BASIC" 메뉴 추가 및 깜빡임 제거
void runFileBrowser() {
  File root = SD.open("/");
  if(!root) {
    //Canvas->setPenColor(Color::Red);
    Canvas->drawText(10, 50, "Failed to open root");
    return;
  }

  int selected = 0;
  int totalFiles = 0;
  String files[50];

  Serial.println("\nScanning for ROM files:");
  File file = root.openNextFile();
  while(file && totalFiles < 50) {
    String fname = String(file.name());
    if(fname.startsWith("/")) fname = fname.substring(1);

    if(fname.endsWith(".ROM") || fname.endsWith(".rom")) {
      files[totalFiles] = fname;
      totalFiles++;
    }
    file = root.openNextFile();
  }
  root.close();

  // 파일이 없어도 BASIC 실행은 가능하므로 에러 처리 완화
  if(totalFiles == 0) {
    //Canvas->setPenColor(Color::Yellow);
    Canvas->drawText(10, 70, "No ROM files found.");
  }

  bool selecting = true;
  bool redraw = true;
  fabgl::Keyboard *kb = PS2Controller.keyboard();

  // 메뉴 항목 수: BASIC 옵션(1개) + 파일 개수
  int totalItems = 1 + totalFiles;

  while(selecting) {
    if (redraw) {
      Canvas->setBrushColor(Color::Black);
      Canvas->clear();

      Canvas->setPenColor(Color::White);
      Canvas->drawText(10, 10, "Select Option (Up/Down/Enter):");

      // 0번 항목: MSX BASIC 실행
      if(selected == 0) {
        Canvas->setPenColor(Color::Yellow);
        Canvas->drawText(10, 30, ">");
      } else {
        Canvas->setPenColor(Color::White);
      }
      Canvas->drawText(25, 30, "[ Run MSX BASIC ]");

      // 1번~N번 항목: 파일 리스트
      for(int i = 0; i < totalFiles && i < 20; i++) {
        int menuIndex = i + 1;
        int yPos = 30 + (menuIndex * 12);

        if(menuIndex == selected) {
          Canvas->setPenColor(Color::Yellow);
          Canvas->drawText(10, yPos, ">");
        } else {
          Canvas->setPenColor(Color::White);
        }
        Canvas->drawText(25, yPos, files[i].c_str());
      }
      redraw = false;
    }

    if(kb->virtualKeyAvailable()) {
      fabgl::VirtualKey key = kb->getNextVirtualKey();

      if(key == fabgl::VK_UP && selected > 0) {
        selected--;
        redraw = true;
      }
      if(key == fabgl::VK_DOWN && selected < totalItems - 1) {
        selected++;
        redraw = true;
      }
      if(key == fabgl::VK_RETURN) {
        // 선택 처리
        if (selected == 0) {
          // BASIC 실행 (ROM 없음)
          Serial.println("Starting MSX BASIC...");
          ROMName[0] = 0; // NULL
          ROMName[1] = 0; // NULL
          
          Canvas->clear();
          //Canvas->setPenColor(Color::Green);
          Canvas->drawText(10, 10, "Booting MSX BASIC...");
        } else {
          // 파일 실행 (인덱스는 selected - 1)
          int fileIdx = selected - 1;
          Serial.printf("Loading ROM: %s\n", files[fileIdx].c_str());

          static char fileNameBuffer[64];
          String path = "/" + files[fileIdx];
          strncpy(fileNameBuffer, path.c_str(), sizeof(fileNameBuffer)-1);
          fileNameBuffer[sizeof(fileNameBuffer)-1] = '\0';

          ROMName[0] = fileNameBuffer;
          ROMName[1] = 0;

          Canvas->clear();
          //Canvas->setPenColor(Color::Green);
          Canvas->drawText(10, 10, "Loading ROM...");
          Canvas->drawText(10, 30, files[fileIdx].c_str());
        }

        delay(500);
        emuRunning = true;
        selecting = false;
      }
    }
    delay(20);
  }
}