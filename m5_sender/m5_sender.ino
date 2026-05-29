/**
 * @file m5_sender.ino
 * @brief Antigravity KP2 Hybrid Master - Upgraded Gestures & Physics
 * @details M5StickS3のオンボード上で超低遅延ジェスチャー判定を行います。
 *          円運動(O-Gest)の検出軸を X (Pitch) と Z (Yaw) に変更し、
 *          物理アタック時のジェスチャークロスオーバー（誤検知干渉）防止保護を搭載。
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <OSCMessage.h>
#include <M5Unified.h>

// =================================================================
// 1. Wi-Fi & 送信先設定
// =================================================================
const char* ssid     = "Buffalo-G-A52A";   
const char* password = "password1234";     
const char* outIp    = "192.168.11.10";    // スマホまたはPCのブリッジIP
const int outPort    = 8000;               // 受信ポート

// 右手用は "Right"、左手用の場合は "Left"
const String deviceSide = "Right"; 

// =================================================================
// 2. 通信オブジェクト & 内部状態
// =================================================================
WiFiUDP Udp;
IPAddress targetIp;

unsigned long lastSensorTime = 0;
const unsigned long SENSOR_PERIOD_MS = 15; // 15ms周期

// OSC送信用アドレス
String oscTeleAddress;    
String oscBtnAddress;     
String oscAtkAddress;     
String oscGestAddress;    

bool lastBtnAState = false;

// ジェスチャ用クールダウンタイマー
unsigned long lastAttackTime = 0;
unsigned long lastThrustTime = 0;
const unsigned long GESTURE_COOLDOWN_MS = 350; 

// 円運動 (Oジェスチャー) 判定バッファ
float lastAngle = 0;
float accumulatedAngle = 0;
unsigned long circleTimer = 0;

// =================================================================
// SETUP: 初期化
// =================================================================
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  Serial.begin(115200);
  delay(500);
  Serial.println("\n--- M5StickS3 OSC Hybrid Sender v2.1 ---");

  // LCD初期表示
  M5.Lcd.init();
  M5.Lcd.setRotation(1); 
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextDatum(MC_DATUM);
  M5.Lcd.setTextSize(1.5);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.drawString("WiFi Connecting...", 120, 68);

  // アドレス構築
  oscTeleAddress = "/m5/" + deviceSide;
  oscBtnAddress  = "/m5/" + deviceSide + "/btnA";
  oscAtkAddress  = "/m5/" + deviceSide + "/attack";
  oscGestAddress = "/m5/" + deviceSide + "/gesture";

  // Wi-Fi 接続
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 15) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected!");
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextColor(GREEN);
    M5.Lcd.drawString("WiFi CONNECTED", 120, 35);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setTextSize(1.2);
    M5.Lcd.drawString(WiFi.localIP().toString(), 120, 70);
    M5.Lcd.drawString("Side: " + deviceSide + " (Hybrid v2.1)", 120, 100);
  } else {
    Serial.println("\n[WiFi] Offline Mode.");
    M5.Lcd.fillScreen(RED);
    M5.Lcd.drawString("WiFi Offline!", 120, 68);
  }

  Udp.begin(8888);
  targetIp.fromString(outIp);

  if (!M5.Imu.isEnabled()) {
    Serial.println("[IMU] Error: Not Found!");
    M5.Lcd.fillScreen(RED);
    M5.Lcd.drawString("IMU ERROR", 120, 68);
    while (1) { delay(100); }
  }

  Serial.println("[System] Setup Complete.");
  delay(1000);
  drawMainUI();
}

// =================================================================
// LOOP: メインループ
// =================================================================
void loop() {
  M5.update();
  unsigned long now = millis();

  // -------------------------------------------------------------
  // A. 物理ボタンA (BtnA) ➔ 緊急割り込み型ゲートOSC
  // -------------------------------------------------------------
  bool currentBtnAState = M5.BtnA.isPressed();
  if (currentBtnAState != lastBtnAState) {
    lastBtnAState = currentBtnAState;
    sendOSCBtnState(currentBtnAState ? 1 : 0);
  }

  // -------------------------------------------------------------
  // B. 15ms毎の定期テレメトリストリーミング (6-Axis + BtnA)
  // -------------------------------------------------------------
  if (now - lastSensorTime >= SENSOR_PERIOD_MS) {
    lastSensorTime = now;

    float accX = 0, accY = 0, accZ = 0;
    float gyroX = 0, gyroY = 0, gyroZ = 0;

    M5.Imu.getAccel(&accX, &accY, &accZ);
    M5.Imu.getGyro(&gyroX, &gyroY, &gyroZ);

    // 7番目の引数としてボタン状態を付与
    OSCMessage teleMsg(oscTeleAddress.c_str());
    teleMsg.add(accX);
    teleMsg.add(accY);
    teleMsg.add(accZ);
    teleMsg.add(gyroX);
    teleMsg.add(gyroY);
    teleMsg.add(gyroZ);
    teleMsg.add(currentBtnAState ? 1.0f : 0.0f);

    if (WiFi.status() == WL_CONNECTED) {
      Udp.beginPacket(targetIp, outPort);
      teleMsg.send(Udp);
      Udp.endPacket();
      teleMsg.empty();
    }

    // -------------------------------------------------------------
    // C. 円運動 (Oジェスチャー) オンボードリアルタイム判定
    // -------------------------------------------------------------
    // 手を回す動作は上下(Pitch: X軸回転)と左右(Yaw: Z軸回転)の組み合わせになるため、
    // gyroZ と gyroX の極座標角度変化を積算するのが幾何学的に正解です！
    // ※アタック打撃直後のクールダウン中は円運動の積算を一時的にマスクして誤判定を防ぐ
    if (now - lastAttackTime > GESTURE_COOLDOWN_MS) {
      float currentAngle = atan2(gyroZ, gyroX); 
      float diff = currentAngle - lastAngle;
      if (diff < -PI) diff += 2.0f * PI;
      if (diff > PI)  diff -= 2.0f * PI;

      float gyroMag = sqrt(gyroX * gyroX + gyroZ * gyroZ);
      if (gyroMag > 110.0f) { // しきい値を 110.0 deg/sec に調整
        accumulatedAngle += diff;
        if (circleTimer == 0) circleTimer = now;

        // 約300度 (5.2ラジアン) 回転したか
        if (abs(accumulatedAngle) >= (300.0f * PI / 180.0f)) {
          if (now - circleTimer < 800) { // 800ms以内の滑らかな円
            String dir = (accumulatedAngle > 0.0f) ? "CIRCLE_CW" : "CIRCLE_CCW";
            sendOSCGesture(dir);
            flashScreenFeedback(dir, GREEN);
          }
          accumulatedAngle = 0.0f;
          circleTimer = 0;
        }
      } else {
        accumulatedAngle *= 0.85f; // 自然減衰
        if (abs(accumulatedAngle) < 0.05f) circleTimer = 0;
      }
      lastAngle = currentAngle;
    } else {
      // アタック中はバッファをリセットして干渉を防ぐ
      accumulatedAngle = 0.0f;
      circleTimer = 0;
    }
  }

  // -------------------------------------------------------------
  // D. 物理アタック判定 ➔ 重力参照型 18全方位分割ドラムアタック
  // -------------------------------------------------------------
  if (now - lastAttackTime > GESTURE_COOLDOWN_MS) {
    float accX = 0, accY = 0, accZ = 0;
    M5.Imu.getAccel(&accX, &accY, &accZ);

    float totalAcc = sqrt(accX * accX + accY * accY + accZ * accZ);

    // 3.0G 以上の急激な負の衝撃ピーク
    if (totalAcc > 3.0f) {
      lastAttackTime = now; // クールダウン開始

      // 1. その瞬間の重力ベクトルを規格化
      float g_len = totalAcc;
      float gx = accX / g_len;
      float gy = accY / g_len;
      float gz = accZ / g_len;

      // 2. 手首の傾斜角度 (Phi)
      float phi = acos(-gz) * (180.0f / PI);

      int finalSector = 0;
      int noteNumber = 60; 

      if (phi >= 75.0f) {
        // ほぼ垂直に立てた状態 ➔ 真上(Zenith) または 真下(Nadir)
        if (gy > 0) {
          finalSector = 16; 
          noteNumber = 76;
        } else {
          finalSector = 17; 
          noteNumber = 77;
        }
      } else {
        // 3. 手首のひねり（回転 Theta）を算出 (0 ~ 360度)
        float theta = atan2(gy, gx) * (180.0f / PI);
        if (theta < 0) theta += 360.0f; 

        // 45度刻みで 8分割
        int yawIndex = (int)((theta + 22.5f) / 45.0f) % 8;

        if (phi < 35.0f) {
          // 水平〜浅い傾斜 ➔ 斜め上層 (Sectors 0 ~ 7)
          finalSector = yawIndex;
          noteNumber = 60 + yawIndex; 
        } else {
          // 深い傾斜 ➔ 斜め下層 (Sectors 8 ~ 15)
          finalSector = 8 + yawIndex;
          noteNumber = 68 + yawIndex; 
        }
      }

      int velocity = (int)(totalAcc * 14.0f);
      if (velocity > 127) velocity = 127;
      if (velocity < 40)  velocity = 40;

      sendOSCAttack(noteNumber, velocity);
      flashScreenFeedback("HIT: " + String(finalSector), YELLOW);
      
      M5.Speaker.tone(880 + (finalSector * 45), 50);
    }
  }

  // -------------------------------------------------------------
  // E. 突き (THRUST) 判定 ➔ キャリブレーション信号送信
  // -------------------------------------------------------------
  // ※アタックドラムの直後は誤リセットを防ぐためマスク
  if (now - lastThrustTime > GESTURE_COOLDOWN_MS && now - lastAttackTime > GESTURE_COOLDOWN_MS) {
    float accX = 0, accY = 0, accZ = 0;
    M5.Imu.getAccel(&accX, &accY, &accZ);

    // 前方への急激な水平突き
    if (abs(accY) > 2.3f || abs(accX) > 2.3f) {
      lastThrustTime = now;
      sendOSCGesture("THRUST");
      flashScreenFeedback("CALIBRATE", CYAN);
      M5.Speaker.tone(1500, 100);
    }
  }

  // -------------------------------------------------------------
  // F. 物理ボタンB (BtnB) ➔ Xジェスチャーの模擬トリガー
  // -------------------------------------------------------------
  if (M5.BtnB.wasPressed()) {
    sendOSCGesture("GESTURE_X");
    flashScreenFeedback("GESTURE X", MAGENTA);
    M5.Speaker.tone(600, 120);
  }

  checkScreenRestore(now);
  delay(1);
}

// =================================================================
// SUB FUNCTIONS: OSC送信 ＆ 表示制御
// =================================================================

void sendOSCBtnState(int isPressed) {
  Serial.printf("[OSC] BtnA State: %d\n", isPressed);
  OSCMessage msg(oscBtnAddress.c_str());
  msg.add(isPressed);

  if (WiFi.status() == WL_CONNECTED) {
    Udp.beginPacket(targetIp, outPort);
    msg.send(Udp);
    Udp.endPacket();
  }
}

void sendOSCAttack(int noteNum, int velocity) {
  Serial.printf("[OSC] Attack Drum Hit -> Note: %d, Vel: %d\n", noteNum, velocity);
  OSCMessage msg(oscAtkAddress.c_str());
  msg.add(noteNum);
  msg.add(velocity);

  if (WiFi.status() == WL_CONNECTED) {
    Udp.beginPacket(targetIp, outPort);
    msg.send(Udp);
    Udp.endPacket();
  }
}

void sendOSCGesture(String gestureName) {
  Serial.printf("[OSC] Gesture Send: %s\n", gestureName.c_str());
  OSCMessage msg(oscGestAddress.c_str());
  msg.add(gestureName.c_str());

  if (WiFi.status() == WL_CONNECTED) {
    Udp.beginPacket(targetIp, outPort);
    msg.send(Udp);
    Udp.endPacket();
  }
}

void drawMainUI() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.drawFastHLine(0, 24, 240, DARKGREY);
  
  M5.Lcd.setTextDatum(TC_DATUM);
  M5.Lcd.setTextSize(1.2);
  M5.Lcd.setTextColor(deviceSide == "Right" ? RED : CYAN);
  M5.Lcd.drawString(deviceSide + " STICK - HYBRID MASTER", 120, 6);

  M5.Lcd.setTextDatum(MC_DATUM);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(1.4);
  M5.Lcd.drawString("STREAMING ACTIVE", 120, 55);

  M5.Lcd.setTextSize(0.9);
  M5.Lcd.setTextColor(LIGHTGREY);
  M5.Lcd.drawString("Robust Gestures Enabled", 120, 85);

  M5.Lcd.setTextDatum(BC_DATUM);
  M5.Lcd.setTextSize(0.85);
  M5.Lcd.setTextColor(GRAY);
  M5.Lcd.drawString("BtnA: Touch | BtnB: X-Gest", 120, 126);
}

unsigned long flashScreenEndTime = 0;
bool isFlashing = false;

void flashScreenFeedback(String text, uint32_t color) {
  isFlashing = true;
  flashScreenEndTime = millis() + 180; 

  M5.Lcd.fillScreen(color);
  M5.Lcd.setTextColor(color == YELLOW ? BLACK : WHITE);
  M5.Lcd.setTextDatum(MC_DATUM);
  M5.Lcd.setTextSize(2.2);
  M5.Lcd.drawString(text, 120, 68);
}

void checkScreenRestore(unsigned long now) {
  if (isFlashing && now >= flashScreenEndTime) {
    isFlashing = false;
    drawMainUI();
  }
}
