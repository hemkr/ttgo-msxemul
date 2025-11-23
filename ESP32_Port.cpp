#include "fabgl.h"
#include <SPI.h>
#include <SD.h>
#include <Arduino.h>

#undef word

extern "C" {
  #ifndef _ARDUINO_H_
  #define _ARDUINO_H_
  #endif
  #include "MSX.h"
}
#include "config.h"

extern fabgl::VGA16Controller VGAController;
extern fabgl::PS2Controller PS2Controller;
extern fabgl::Canvas *Canvas;

// 화면 버퍼
fabgl::Bitmap *msxScreen = nullptr;
uint8_t *XBuf = NULL;


//extern "C" void PlayAllSound(int uSec);

// VGA 타입 체크
static fabgl::VGA16Controller* vga16 = nullptr;

// MSX 컬러 테이블 (BGR 하드웨어 보정: R과 B를 교체하여 정의)
// MSX 컬러 테이블 (올바른 RGB 값)
fabgl::RGB888 msxColor(uint8_t c) {
  static const fabgl::RGB888 table[16] = {
    {0,0,0},         // 0: Black
    {0,0,0},         // 1: Black
    {73,184,62},     // 2: Green (B,G,R 순서로 바꿈)
    {125,208,116},   // 3: Light Green
    {224,85,89},     // 4: Blue (실제로는 Red처럼 보이면 순서 문제)
    {241,118,128},   // 5: Light Blue
    {81,94,185},     // 6: Red (실제로는 Blue처럼 보이면 순서 문제)
    {239,219,101},   // 7: Cyan
    {89,101,219},    // 8: Red
    {125,137,255},   // 9: Light Red
    {94,195,204},    // 10: Yellow
    {135,208,222},   // 11: Light Yellow
    {65,162,58},     // 12: Dark Green
    {181,102,183},   // 13: Magenta
    {204,204,204},   // 14: Gray
    {255,255,255}    // 15: White
  };
  return table[c & 0x0F];
}


// 키보드 매핑 매크로
#define PRESS(r, b) KeyState[r] &= ~(b)
#define RELEASE(r, b) KeyState[r] |= (b)

// FabGL 키를 MSX 키 매트릭스로 변환
// FabGL 키를 MSX 키 매트릭스로 변환 (수정 버전)
// MSX 키보드 매트릭스: http://map.grauw.nl/resources/msx_io_ports.php#ppi
void ProcessKey(fabgl::VirtualKey vk, bool down) {
  if (down) {
    switch(vk) {
      // Row 0: 0-7
      case fabgl::VK_0: PRESS(0, 0x01); break;
      case fabgl::VK_1: PRESS(0, 0x02); break;
      case fabgl::VK_2: PRESS(0, 0x04); break;
      case fabgl::VK_3: PRESS(0, 0x08); break;
      case fabgl::VK_4: PRESS(0, 0x10); break;
      case fabgl::VK_5: PRESS(0, 0x20); break;
      case fabgl::VK_6: PRESS(0, 0x40); break;
      case fabgl::VK_7: PRESS(0, 0x80); break;
      
      // Row 1: 8, 9, -, =, \, [, ], ;
      case fabgl::VK_8: PRESS(1, 0x01); break;
      case fabgl::VK_9: PRESS(1, 0x02); break;
      case fabgl::VK_MINUS: PRESS(1, 0x04); break;
      case fabgl::VK_EQUALS: PRESS(1, 0x08); break;
      case fabgl::VK_BACKSLASH: PRESS(1, 0x10); break;
      case fabgl::VK_LEFTBRACKET: PRESS(1, 0x20); break;
      case fabgl::VK_RIGHTBRACKET: PRESS(1, 0x40); break;
      case fabgl::VK_SEMICOLON: PRESS(1, 0x80); break;

      // Row 2: ', `, ,, ., /, Dead, A, B
      case fabgl::VK_QUOTE: PRESS(2, 0x01); break;
      case fabgl::VK_GRAVEACCENT: PRESS(2, 0x02); break;
      case fabgl::VK_COMMA: PRESS(2, 0x04); break;
      case fabgl::VK_PERIOD: PRESS(2, 0x08); break;
      case fabgl::VK_SLASH: PRESS(2, 0x10); break;
      // Dead key는 스킵
      case fabgl::VK_A: PRESS(2, 0x40); break;
      case fabgl::VK_B: PRESS(2, 0x80); break;

      // Row 3: C, D, E, F, G, H, I, J
      case fabgl::VK_C: PRESS(3, 0x01); break;
      case fabgl::VK_D: PRESS(3, 0x02); break;
      case fabgl::VK_E: PRESS(3, 0x04); break;
      case fabgl::VK_F: PRESS(3, 0x08); break;
      case fabgl::VK_G: PRESS(3, 0x10); break;
      case fabgl::VK_H: PRESS(3, 0x20); break;
      case fabgl::VK_I: PRESS(3, 0x40); break;
      case fabgl::VK_J: PRESS(3, 0x80); break;

      // Row 4: K, L, M, N, O, P, Q, R
      case fabgl::VK_K: PRESS(4, 0x01); break;
      case fabgl::VK_L: PRESS(4, 0x02); break;
      case fabgl::VK_M: PRESS(4, 0x04); break;
      case fabgl::VK_N: PRESS(4, 0x08); break;
      case fabgl::VK_O: PRESS(4, 0x10); break;
      case fabgl::VK_P: PRESS(4, 0x20); break;
      case fabgl::VK_Q: PRESS(4, 0x40); break;
      case fabgl::VK_R: PRESS(4, 0x80); break;

      // Row 5: S, T, U, V, W, X, Y, Z
      case fabgl::VK_S: PRESS(5, 0x01); break;
      case fabgl::VK_T: PRESS(5, 0x02); break;
      case fabgl::VK_U: PRESS(5, 0x04); break;
      case fabgl::VK_V: PRESS(5, 0x08); break;
      case fabgl::VK_W: PRESS(5, 0x10); break;
      case fabgl::VK_X: PRESS(5, 0x20); break;
      case fabgl::VK_Y: PRESS(5, 0x40); break;
      case fabgl::VK_Z: PRESS(5, 0x80); break;

      // Row 6: SHIFT, CTRL, GRAPH, CAPS, CODE, F1, F2, F3
      case fabgl::VK_LSHIFT: 
      case fabgl::VK_RSHIFT: PRESS(6, 0x01); break;
      case fabgl::VK_LCTRL: 
      case fabgl::VK_RCTRL: PRESS(6, 0x02); break;
      case fabgl::VK_LALT: PRESS(6, 0x04); break; // GRAPH key
      case fabgl::VK_CAPSLOCK: PRESS(6, 0x08); break;
      case fabgl::VK_RALT: PRESS(6, 0x10); break; // CODE key
      case fabgl::VK_F1: PRESS(6, 0x20); break;
      case fabgl::VK_F2: PRESS(6, 0x40); break;
      case fabgl::VK_F3: PRESS(6, 0x80); break;

      // Row 7: F4, F5, ESC, TAB, STOP, BS, SELECT, RETURN
      case fabgl::VK_F4: PRESS(7, 0x01); break;
      case fabgl::VK_F5: PRESS(7, 0x02); break;
      case fabgl::VK_ESCAPE: PRESS(7, 0x04); break;
      case fabgl::VK_TAB: PRESS(7, 0x08); break;
      case fabgl::VK_PAUSE: PRESS(7, 0x10); break; // STOP key
      case fabgl::VK_BACKSPACE: PRESS(7, 0x20); break;
      case fabgl::VK_HOME: PRESS(7, 0x40); break; // SELECT key
      case fabgl::VK_RETURN: PRESS(7, 0x80); break;

      // Row 8: SPACE, HOME(CLS), INS, DEL, LEFT, UP, DOWN, RIGHT
      case fabgl::VK_SPACE: PRESS(8, 0x01); break;
      case fabgl::VK_END: PRESS(8, 0x02); break; // HOME(CLS) key
      case fabgl::VK_INSERT: PRESS(8, 0x04); break;
      case fabgl::VK_DELETE: PRESS(8, 0x08); break;
      case fabgl::VK_LEFT: PRESS(8, 0x10); break;
      case fabgl::VK_UP: PRESS(8, 0x20); break;
      case fabgl::VK_DOWN: PRESS(8, 0x40); break;
      case fabgl::VK_RIGHT: PRESS(8, 0x80); break;

      // 텐키 숫자 매핑
      case fabgl::VK_KP_0: PRESS(0, 0x01); break; // 0
      case fabgl::VK_KP_1: PRESS(0, 0x02); break; // 1
      case fabgl::VK_KP_2: PRESS(0, 0x04); break; // 2
      case fabgl::VK_KP_3: PRESS(0, 0x08); break; // 3
      case fabgl::VK_KP_4: PRESS(0, 0x10); break; // 4
      case fabgl::VK_KP_5: PRESS(0, 0x20); break; // 5
      case fabgl::VK_KP_6: PRESS(0, 0x40); break; // 6
      case fabgl::VK_KP_7: PRESS(0, 0x80); break; // 7
      case fabgl::VK_KP_8: PRESS(1, 0x01); break; // 8
      case fabgl::VK_KP_9: PRESS(1, 0x02); break; // 9

      // 텐키 기호 매핑
      case fabgl::VK_KP_MULTIPLY: PRESS(2, 0x80); break; // * -> :
      case fabgl::VK_KP_PLUS: PRESS(1, 0x80); break;     // + -> ;
      case fabgl::VK_KP_MINUS: PRESS(1, 0x04); break;    // -
      case fabgl::VK_KP_DIVIDE: PRESS(2, 0x10); break;   // /
      case fabgl::VK_KP_PERIOD: PRESS(2, 0x08); break;   // .

      // 종료 키
      case fabgl::VK_F12: ExitNow = 1; break;
      
      default: break;
    }
  } else {
    // Key Up Event
    switch(vk) {
      // Row 0
      case fabgl::VK_0: RELEASE(0, 0x01); break;
      case fabgl::VK_1: RELEASE(0, 0x02); break;
      case fabgl::VK_2: RELEASE(0, 0x04); break;
      case fabgl::VK_3: RELEASE(0, 0x08); break;
      case fabgl::VK_4: RELEASE(0, 0x10); break;
      case fabgl::VK_5: RELEASE(0, 0x20); break;
      case fabgl::VK_6: RELEASE(0, 0x40); break;
      case fabgl::VK_7: RELEASE(0, 0x80); break;
      
      // Row 1
      case fabgl::VK_8: RELEASE(1, 0x01); break;
      case fabgl::VK_9: RELEASE(1, 0x02); break;
      case fabgl::VK_MINUS: RELEASE(1, 0x04); break;
      case fabgl::VK_EQUALS: RELEASE(1, 0x08); break;
      case fabgl::VK_BACKSLASH: RELEASE(1, 0x10); break;
      case fabgl::VK_LEFTBRACKET: RELEASE(1, 0x20); break;
      case fabgl::VK_RIGHTBRACKET: RELEASE(1, 0x40); break;
      case fabgl::VK_SEMICOLON: RELEASE(1, 0x80); break;

      // Row 2
      case fabgl::VK_QUOTE: RELEASE(2, 0x01); break;
      case fabgl::VK_GRAVEACCENT: RELEASE(2, 0x02); break;
      case fabgl::VK_COMMA: RELEASE(2, 0x04); break;
      case fabgl::VK_PERIOD: RELEASE(2, 0x08); break;
      case fabgl::VK_SLASH: RELEASE(2, 0x10); break;
      case fabgl::VK_A: RELEASE(2, 0x40); break;
      case fabgl::VK_B: RELEASE(2, 0x80); break;

      // Row 3
      case fabgl::VK_C: RELEASE(3, 0x01); break;
      case fabgl::VK_D: RELEASE(3, 0x02); break;
      case fabgl::VK_E: RELEASE(3, 0x04); break;
      case fabgl::VK_F: RELEASE(3, 0x08); break;
      case fabgl::VK_G: RELEASE(3, 0x10); break;
      case fabgl::VK_H: RELEASE(3, 0x20); break;
      case fabgl::VK_I: RELEASE(3, 0x40); break;
      case fabgl::VK_J: RELEASE(3, 0x80); break;

      // Row 4
      case fabgl::VK_K: RELEASE(4, 0x01); break;
      case fabgl::VK_L: RELEASE(4, 0x02); break;
      case fabgl::VK_M: RELEASE(4, 0x04); break;
      case fabgl::VK_N: RELEASE(4, 0x08); break;
      case fabgl::VK_O: RELEASE(4, 0x10); break;
      case fabgl::VK_P: RELEASE(4, 0x20); break;
      case fabgl::VK_Q: RELEASE(4, 0x40); break;
      case fabgl::VK_R: RELEASE(4, 0x80); break;

      // Row 5
      case fabgl::VK_S: RELEASE(5, 0x01); break;
      case fabgl::VK_T: RELEASE(5, 0x02); break;
      case fabgl::VK_U: RELEASE(5, 0x04); break;
      case fabgl::VK_V: RELEASE(5, 0x08); break;
      case fabgl::VK_W: RELEASE(5, 0x10); break;
      case fabgl::VK_X: RELEASE(5, 0x20); break;
      case fabgl::VK_Y: RELEASE(5, 0x40); break;
      case fabgl::VK_Z: RELEASE(5, 0x80); break;

      // Row 6
      case fabgl::VK_LSHIFT: 
      case fabgl::VK_RSHIFT: RELEASE(6, 0x01); break;
      case fabgl::VK_LCTRL: 
      case fabgl::VK_RCTRL: RELEASE(6, 0x02); break;
      case fabgl::VK_LALT: RELEASE(6, 0x04); break;
      case fabgl::VK_CAPSLOCK: RELEASE(6, 0x08); break;
      case fabgl::VK_RALT: RELEASE(6, 0x10); break;
      case fabgl::VK_F1: RELEASE(6, 0x20); break;
      case fabgl::VK_F2: RELEASE(6, 0x40); break;
      case fabgl::VK_F3: RELEASE(6, 0x80); break;

      // Row 7
      case fabgl::VK_F4: RELEASE(7, 0x01); break;
      case fabgl::VK_F5: RELEASE(7, 0x02); break;
      case fabgl::VK_ESCAPE: RELEASE(7, 0x04); break;
      case fabgl::VK_TAB: RELEASE(7, 0x08); break;
      case fabgl::VK_PAUSE: RELEASE(7, 0x10); break;
      case fabgl::VK_BACKSPACE: RELEASE(7, 0x20); break;
      case fabgl::VK_HOME: RELEASE(7, 0x40); break;
      case fabgl::VK_RETURN: RELEASE(7, 0x80); break;

      // Row 8
      case fabgl::VK_SPACE: RELEASE(8, 0x01); break;
      case fabgl::VK_END: RELEASE(8, 0x02); break;
      case fabgl::VK_INSERT: RELEASE(8, 0x04); break;
      case fabgl::VK_DELETE: RELEASE(8, 0x08); break;
      case fabgl::VK_LEFT: RELEASE(8, 0x10); break;
      case fabgl::VK_UP: RELEASE(8, 0x20); break;
      case fabgl::VK_DOWN: RELEASE(8, 0x40); break;
      case fabgl::VK_RIGHT: RELEASE(8, 0x80); break;

      // 텐키
      case fabgl::VK_KP_0: RELEASE(0, 0x01); break;
      case fabgl::VK_KP_1: RELEASE(0, 0x02); break;
      case fabgl::VK_KP_2: RELEASE(0, 0x04); break;
      case fabgl::VK_KP_3: RELEASE(0, 0x08); break;
      case fabgl::VK_KP_4: RELEASE(0, 0x10); break;
      case fabgl::VK_KP_5: RELEASE(0, 0x20); break;
      case fabgl::VK_KP_6: RELEASE(0, 0x40); break;
      case fabgl::VK_KP_7: RELEASE(0, 0x80); break;
      case fabgl::VK_KP_8: RELEASE(1, 0x01); break;
      case fabgl::VK_KP_9: RELEASE(1, 0x02); break;
      case fabgl::VK_KP_MULTIPLY: RELEASE(2, 0x80); break;
      case fabgl::VK_KP_PLUS: RELEASE(1, 0x80); break;
      case fabgl::VK_KP_MINUS: RELEASE(1, 0x04); break;
      case fabgl::VK_KP_DIVIDE: RELEASE(2, 0x10); break;
      case fabgl::VK_KP_PERIOD: RELEASE(2, 0x08); break;

      default: break;
    }
  }
}

// 키보드 업데이트 함수
void UpdateMSXKeyboard() {
  fabgl::Keyboard *kb = PS2Controller.keyboard();
  if(!kb) return;

  while(kb->virtualKeyAvailable()) {
    bool down;
    fabgl::VirtualKey vk = kb->getNextVirtualKey(&down);
    ProcessKey(vk, down);
  }
}









// 팔레트 테스트 함수 (setup에서 호출)
void TestPalette() {
  Serial.println("\n=== MSX Color Palette Test ===");
  for (int i = 0; i < 16; i++) {
    fabgl::RGB888 color = msxColor(i);
    Serial.print("Color ");
    Serial.print(i);
    Serial.print(": R=");
    Serial.print(color.R);
    Serial.print(" G=");
    Serial.print(color.G);
    Serial.print(" B=");
    Serial.println(color.B);
    
    // 실제로 설정되어 있는지 확인
    if(vga16) {
      vga16->setPaletteItem(i, color);
    }
  }
  Serial.println("Palette test completed.");
}



//=============================================================================
// fMSX Driver Implementation
//=============================================================================
int InitMachine(void) {
  Serial.println("InitMachine: Starting...");

  // VGA16 컨트롤러 확인 및 팔레트 설정
  vga16 = &VGAController;
  if(vga16) {
    Serial.println("=== Setting Initial Palette in InitMachine ===");
    for(int i = 0; i < 16; i++) {
      fabgl::RGB888 color = msxColor(i);
      vga16->setPaletteItem(i, color);
      Serial.printf("InitMachine: Palette[%d] = R=%d G=%d B=%d\n", 
                    i, color.R, color.G, color.B);
    }
    Serial.println("Initial palette set in InitMachine");
  } else {
    Serial.println("ERROR: VGA16Controller not available!");
    return 0;
  }

  size_t bufSize = MSX_WIDTH * MSX_HEIGHT;
  XBuf = (uint8_t*)ps_malloc(bufSize);
  if(!XBuf) {
    Serial.println("Failed to allocate XBuf");
    return 0;
  }
  memset(XBuf, 0, bufSize);
  Serial.printf("XBuf allocated: %d bytes at %p\n", bufSize, XBuf);

  // Native 포맷으로 비트맵 생성
  msxScreen = new fabgl::Bitmap(MSX_WIDTH, MSX_HEIGHT, XBuf, 
                                 fabgl::PixelFormat::Native, false);
  if(!msxScreen) {
    Serial.println("Failed to create msxScreen");
    free(XBuf);
    XBuf = NULL;
    return 0;
  }
  
  Serial.println("InitMachine completed successfully");
  return 1;
}

void TrashMachine(void) {
  if(msxScreen) { delete msxScreen; msxScreen = nullptr; }
  if(XBuf) { free(XBuf); XBuf = NULL; }
}

// SetColor: VGA16Controller용

void SetColor(byte N, byte R, byte G, byte B) {
  if(N >= 16) return;
  
  if(!vga16) {
    vga16 = &VGAController;
    Serial.println("SetColor: Initializing vga16 pointer");
  }
  
  if(vga16) {
    fabgl::RGB888 color;
    color.R = R;
    color.G = G;
    color.B = B;
    vga16->setPaletteItem(N, color);
    
    // 중요한 색상 변경만 로그
    if(N <= 15) {
      Serial.printf("SetColor[%d]: R=%d G=%d B=%d\n", N, R, G, B);
    }
  } else {
    Serial.println("ERROR: SetColor called but vga16 is NULL!");
  }
}




void RefreshScreen(void) {
  if(Canvas && msxScreen) {
    Canvas->drawBitmap(VGA_OFFSET_X, VGA_OFFSET_Y, msxScreen);
  }
  // UpdatePSG는 이미 PlayAllSound에서 호출되므로 여기서는 불필요
}

// 스프라이트 그리기 함수
void DrawSprites(byte Y, byte *LineBuffer) {
  if (SpritesOFF) return;

  int Size = (VDP[1] & 2) ? 16 : 8;
  int Mag  = (VDP[1] & 1); 

  for (int i = 0; i < 32; i++) {
    byte *Attr = SprTab + i * 4;
    int Sy = Attr[0];
    if (Sy == 208) break; 

    if (Sy > 240) Sy -= 256;
    Sy++;

    int Diff = Y - Sy;
    if (Mag) Diff >>= 1;

    if (Diff >= 0 && Diff < Size) {
      int Sx = Attr[1];
      int PatIdx = Attr[2];
      byte Color = Attr[3] & 0x0F;

      if (Attr[3] & 0x80) Sx -= 32;
      if (Color == 0) continue;
      if (Size == 16) PatIdx &= 0xFC;

      byte PatLine = SprGen[PatIdx * 8 + Diff];

      for (int p = 0; p < 8; p++) {
        int DrawX = Sx + (Mag ? p * 2 : p);
        if ((PatLine & 0x80) && (DrawX >= 0) && (DrawX < MSX_WIDTH)) {
          LineBuffer[DrawX] = Color;
          if (Mag && (DrawX + 1 < MSX_WIDTH)) LineBuffer[DrawX + 1] = Color;
        }
        PatLine <<= 1;
      }

      if (Size == 16) {
        PatLine = SprGen[PatIdx * 8 + Diff + 16];
        for (int p = 0; p < 8; p++) {
          int DrawX = Sx + (Mag ? (p + 8) * 2 : (p + 8));
           if ((PatLine & 0x80) && (DrawX >= 0) && (DrawX < MSX_WIDTH)) {
            LineBuffer[DrawX] = Color;
            if (Mag && (DrawX + 1 < MSX_WIDTH)) LineBuffer[DrawX + 1] = Color;
          }
          PatLine <<= 1;
        }
      }
    }
  }
}

// SCREEN 0 (Text 40x24) 구현 - BASIC 모드용
void RefreshLine0(byte Y) {
  if(!XBuf || Y >= MSX_HEIGHT) return;
  
  byte *P = XBuf + Y * MSX_WIDTH;
  
  // Text Color & Background Color (VDP Reg 7)
  byte BG = VDP[7] & 0x0F;
  byte FG = VDP[7] >> 4;
  
  // 정상값 확인: BG=4 (파란색), FG=15 (흰색)
  // 만약 VDP[7]이 이상하면 강제 수정
  if (BG != 4 || FG != 15) {
    BG = 4;   // Dark Blue
    FG = 15;  // White
  }

  // Screen 0은 폭이 240픽셀이므로 좌우 8픽셀씩 여백이 생김
  memset(P, BG, 8); // 좌측 여백
  P += 8;

  int Row = (Y >> 3) * 40; // 40 chars per line
  int Line = Y & 7;

  for (int X = 0; X < 40; X++) {
    int CharCode = ChrTab[Row + X];
    byte Pattern = ChrGen[CharCode * 8 + Line];

    // 텍스트 모드는 6픽셀 너비 (상위 6비트 사용)
    for (int i = 0; i < 6; i++) {
      *P++ = (Pattern & 0x80) ? FG : BG;
      Pattern <<= 1;
    }
  }

  memset(P, BG, 8); // 우측 여백
}

// SCREEN 1 (Text 32x24) 구현
void RefreshLine1(byte Y) {
  if(!XBuf || Y >= MSX_HEIGHT) return;
  
  byte *P = XBuf + Y * MSX_WIDTH;
  int Row = (Y >> 3) * 32;
  int Line = Y & 7;
  byte BackdropColor = VDP[7] & 0x0F;
  
  // 디버깅
  static bool colorDebug = false;
  if (!colorDebug && Y == 8) {
    Serial.printf("RefreshLine1 Y=%d: Row=%d, BackdropColor=%d\n", Y, Row, BackdropColor);
    
    // 첫 5개 문자 정보 출력
    for(int i = 0; i < 5; i++) {
      int CharCode = ChrTab[Row + i];
      byte Color = ColTab[CharCode >> 3];
      Serial.printf("  Char[%d]: Code=%d(0x%02X), ColorTable=0x%02X, FG=%d, BG=%d\n", 
                    i, CharCode, CharCode, Color, Color >> 4, Color & 0x0F);
    }
    colorDebug = true;
  }

  for (int X = 0; X < 32; X++) {
    int CharCode = ChrTab[Row + X];
    byte Pattern = ChrGen[CharCode * 8 + Line];
    byte Color = ColTab[CharCode >> 3];  // 8문자당 1바이트

    byte FG = Color >> 4;
    byte BG = Color & 0x0F;
    
    // 투명색(0) 처리
    if(FG == 0) FG = BackdropColor;
    if(BG == 0) BG = BackdropColor;

    // 8픽셀 그리기
    for(int i = 0; i < 8; i++) {
      *P++ = (Pattern & 0x80) ? FG : BG;
      Pattern <<= 1;
    }
  }
  
  DrawSprites(Y, XBuf + Y * MSX_WIDTH);
}

// SCREEN 2 (Graphic 2) 구현 - 게임용
void RefreshLine2(byte Y) {
  if(!XBuf || Y >= MSX_HEIGHT) return;
  
  byte *P = XBuf + Y * MSX_WIDTH;
  
  int Zone = Y / 64; 
  int Row  = (Y >> 3) * 32; 
  int Line = Y & 7;         
  byte BackdropColor = VDP[7] & 0x0F;
  
  // VDP[7] 디버깅
  static bool debug2Printed = false;
  if (!debug2Printed && Y == 0) {
    Serial.print("SCREEN 2 - VDP[7] = 0x");
    Serial.print(VDP[7], HEX);
    Serial.print(" Backdrop=");
    Serial.println(BackdropColor);
    debug2Printed = true;
  }

  for (int X = 0; X < 32; X++) {
    int CharCode = ChrTab[Row + X] + (Zone * 256);
    byte Pattern = ChrGen[CharCode * 8 + Line];
    byte Color   = ColTab[CharCode * 8 + Line];
    
    byte FG = Color >> 4;   
    byte BG = Color & 0x0F; 

    if(FG == 0) FG = BackdropColor;
    if(BG == 0) BG = BackdropColor;

    for(int i=0; i<8; i++) {
      *P++ = (Pattern & 0x80) ? FG : BG;
      Pattern <<= 1;
    }
  }
  DrawSprites(Y, XBuf + Y * MSX_WIDTH);
}
// 미구현 모드 안전장치 (화면 깨짐 방지)
void ClearLine(byte Y) {
  if(!XBuf || Y >= MSX_HEIGHT) return;
  byte *P = XBuf + Y * MSX_WIDTH;
  byte BG = VDP[7] & 0x0F;
  memset(P, BG, MSX_WIDTH);
}

void RefreshLineTx80(byte Y) { ClearLine(Y); }
void RefreshLine3(byte Y) { ClearLine(Y); }
void RefreshLine4(byte Y) { ClearLine(Y); }
void RefreshLine5(byte Y) { ClearLine(Y); }
void RefreshLine6(byte Y) { ClearLine(Y); }
void RefreshLine7(byte Y) { ClearLine(Y); }
void RefreshLine8(byte Y) { ClearLine(Y); }
void RefreshLine10(byte Y) { ClearLine(Y); }
void RefreshLine12(byte Y) { ClearLine(Y); }




void Keyboard(void) {
  UpdateMSXKeyboard();
}

unsigned int Joystick(void) {
unsigned int status = 0xFFFF; 
  fabgl::Keyboard *kb = PS2Controller.keyboard();
  if (!kb) return status;

  // 조이스틱 매핑은 유지하되 키보드 매핑과 충돌하지 않게 주의
  if (kb->isVKDown(fabgl::VK_UP))    status &= ~0x0001; 
  if (kb->isVKDown(fabgl::VK_DOWN))  status &= ~0x0002; 
  if (kb->isVKDown(fabgl::VK_LEFT))  status &= ~0x0004; 
  if (kb->isVKDown(fabgl::VK_RIGHT)) status &= ~0x0008; 
  
  // 조이스틱 버튼 매핑 (기존 Space/M 대신 별도 키 할당 고려 가능)
  // 여기서는 Space를 그대로 두지만, 키보드 타이핑 시 Space와 중복될 수 있음
  // 게임 플레이 시에는 문제 없음
  if (kb->isVKDown(fabgl::VK_SPACE)) status &= ~0x0010; // Button A
  if (kb->isVKDown(fabgl::VK_M))     status &= ~0x0020; // Button B

  return status;
}

unsigned int Mouse(byte N) { return 0; }

void PutImage(void) {
  RefreshScreen();
}

// SD 카드 파일 I/O
extern "C" {
  void* arduino_fopen(const char* filename, const char* mode) {
    if(!filename) return NULL;
    String path = String(filename);
    if(!path.startsWith("/")) path = "/" + path;
    
    File tempFile = SD.open(path, FILE_READ);
    if(!tempFile) return NULL;
    File* fptr = new File(tempFile);
    return (void*)fptr;
  }
  int arduino_fclose(void* stream) {
    if(!stream) return 0;
    File* f = (File*)stream;
    f->close();
    delete f;
    return 0;
  }
  size_t arduino_fread(void* ptr, size_t size, size_t nmemb, void* stream) {
    if(!stream || !ptr || size == 0 || nmemb == 0) return 0;
    File* f = (File*)stream;
    if(!f->available()) return 0;
    size_t bytesRead = f->read((uint8_t*)ptr, size * nmemb);
    return bytesRead / size;
  }
  size_t arduino_fwrite(const void* ptr, size_t size, size_t nmemb, void* stream) {
    if(!stream || !ptr || size == 0 || nmemb == 0) return 0;
    File* f = (File*)stream;
    size_t bytesWritten = f->write((const uint8_t*)ptr, size * nmemb);
    return bytesWritten / size;
  }
  int arduino_fseek(void* stream, long offset, int whence) {
    if(!stream) return -1;
    File* f = (File*)stream;
    if(whence == SEEK_SET) f->seek(offset);
    else if(whence == SEEK_END) f->seek(f->size() + offset);
    else if(whence == SEEK_CUR) f->seek(f->position() + offset);
    return 0;
  }
  long arduino_ftell(void* stream) {
    if(!stream) return -1;
    File* f = (File*)stream;
    return f->position();
  }
  void arduino_rewind(void* stream) {
    if(!stream) return;
    File* f = (File*)stream;
    f->seek(0);
  }
  int arduino_fgetc(void* stream) {
    if(!stream) return -1;
    File* f = (File*)stream;
    if(!f->available()) return -1;
    return f->read();
  }
  int arduino_feof(void* stream) {
    if(!stream) return 1;
    File* f = (File*)stream;
    return !f->available();
  }
}