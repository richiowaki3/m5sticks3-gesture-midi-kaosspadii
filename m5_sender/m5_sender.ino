/**
 * @file m5_sender.ino
 * @brief M5StickS3 OSC Sender for Antigravity Gesture & MIDI System
 * @details M5StickS3の6軸センサーデータ(15ms毎)およびジェスチャー確定データを、
 *          Wi-Fiを経由してOSC (UDP) パケットでスマホ（または中継ブリッジ）へ送信します。
 *          左右の切り替えはスケッチ冒頭の設定変数を書き換えるだけで対応可能です。
 * 
 * Hardware: M5StickS3 (M5Unifiedライブラリにより、StickC / Plus / Plus2等の旧機種にも互換)
 * 
 * 必要なライブラリ (Arduino IDEのライブラリマネージャからインストールしてください):
 *   1. M5Unified (by M5Stack)
 *   2. OSC (by Adrian Freed and Yotam Mann - CNMAT製)
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <OSCMessage.h>
#include <M5Unified.h>

// =================================================================
// 1. Wi-Fi & 送信先 (スマホ / PCブリッジ) 設定
// =================================================================
const char* ssid     = "Buffalo-G-A52A";   // ご利用のWi-FiルーターのSSID
const char* password = "password1234";     // Wi-Fiのパスワード

// 送信先 (スマホのIP、またはPCで中継サーバーを動かしている場合はPCのIP)
const char* outIp    = "192.168.11.10";      // 送信先のIPアドレス (スマホIP)
const int outPort    = 8000;                // 受信ポート (通常 8000)

// =================================================================
// 2. 左右デバイス識別設定
// =================================================================
// 右手用の場合は "Right"、左手用の場合は "Left" に書き換えてください。
const String deviceSide = "Right"; 

// =================================================================
// 3. 通信オブジェクト & 内部状態
// =================================================================
WiFiUDP Udp;
IPAddress targetIp;

// 15ms周期タイマー用
unsigned long lastSensorTime = 0;
const unsigned long SENSOR_PERIOD_MS = 15; 

// OSC送信用のアドレス文字列
String oscSensorAddress;
String oscGestureAddress;

// =================================================================
// SETUP: 初期化処理
// =================================================================
void setup() {
  // M5Unifiedの初期設定
  auto cfg = M5.config();
  M5.begin(cfg);

  // シリアル通信初期化 (デバッグモニター用)
  Serial.begin(115200);
  delay(500);
  Serial.println("\n--- M5StickS3 OSC Sender Initializing ---");

  // LCD画面の初期設定
  M5.Lcd.init();
  M5.Lcd.setRotation(1); // 横向き
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextDatum(MC_DATUM); // 中央基準
  M5.Lcd.setTextSize(1.5);
  M5.Lcd.setTextColor(WHITE);

  M5.Lcd.drawString("WiFi Connecting...", 120, 68);

  // OSC送信用アドレスの構築
  // 例: "/m5/Right" および "/m5/Right/gesture"
  oscSensorAddress = "/m5/" + deviceSide;
  oscGestureAddress = "/m5/" + deviceSide + "/gesture";

  // Wi-Fi接続処理
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected!");
    Serial.print("[WiFi] IP Address: ");
    Serial.println(WiFi.localIP());
    
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextColor(GREEN);
    M5.Lcd.drawString("WiFi OK", 120, 40);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setTextSize(1.2);
    M5.Lcd.drawString(WiFi.localIP().toString(), 120, 70);
    M5.Lcd.drawString("Side: " + deviceSide, 120, 95);
  } else {
    Serial.println("\n[WiFi] Connection Failed. Run in Offline Mode.");
    M5.Lcd.fillScreen(RED);
    M5.Lcd.drawString("WiFi Failed!", 120, 68);
  }

  // UDPオブジェクトの開始
  Udp.begin(8888); // ローカル待ち受け用ポート（ダミー）
  targetIp.fromString(outIp);

  // 内蔵IMU（6軸センサー）の起動チェック
  if (!M5.Imu.isEnabled()) {
    Serial.println("[IMU] Error: IMU not found!");
    M5.Lcd.fillScreen(RED);
    M5.Lcd.drawString("IMU ERROR", 120, 68);
    while (1) { delay(100); }
  }

  Serial.println("[System] OSC Sender Setup Complete.");
  delay(1500);
  
  // メイン画面の描画
  drawMainUI();
}

// =================================================================
// LOOP: メインループ
// =================================================================
void loop() {
  // M5ボタン・各種状態アップデート
  M5.update();

  unsigned long now = millis();

  // -------------------------------------------------------------
  // A. 15ms毎の定期センサーデータ送信 (accX, accY, accZ, gyroX, gyroY, gyroZ)
  // -------------------------------------------------------------
  if (now - lastSensorTime >= SENSOR_PERIOD_MS) {
    lastSensorTime = now;

    float accX = 0, accY = 0, accZ = 0;
    float gyroX = 0, gyroY = 0, gyroZ = 0;

    // センサー値を取得
    M5.Imu.getAccel(&accX, &accY, &accZ);
    M5.Imu.getGyro(&gyroX, &gyroY, &gyroZ);

    // OSCメッセージの構築
    OSCMessage msg(oscSensorAddress.c_str());
    msg.add(accX);
    msg.add(accY);
    msg.add(accZ);
    msg.add(gyroX);
    msg.add(gyroY);
    msg.add(gyroZ);

    // UDP送信
    if (WiFi.status() == WL_CONNECTED) {
      Udp.beginPacket(targetIp, outPort);
      msg.send(Udp);
      Udp.endPacket();
      msg.empty();
    }
  }

  // -------------------------------------------------------------
  // B. ボタン操作によるジェスチャー確定の模擬送信 (テスト・デバッグ用)
  // -------------------------------------------------------------
  // 本体正面の大きなボタン (BtnA) が押されたら、"ATTACK" ジェスチャーを送信
  if (M5.BtnA.wasPressed()) {
    sendOSCGesture("ATTACK");
    flashScreenFeedback("ATTACK", YELLOW);
  }
  
  // 本体側面の小さなボタン (BtnB) が押されたら、"THRUST" ジェスチャーを送信
  if (M5.BtnB.wasPressed()) {
    sendOSCGesture("THRUST");
    flashScreenFeedback("THRUST", CYAN);
  }
  
  // -------------------------------------------------------------
  // C. M5StickS3内蔵の高速ジェスチャー判定 (超低遅延トリガー)
  // -------------------------------------------------------------
  static unsigned long lastGestureTime = 0;
  if (now - lastGestureTime > 350) { // 350msのチャタリング防止クールダウン
    float accX = 0, accY = 0, accZ = 0;
    float gyroX = 0, gyroY = 0, gyroZ = 0;
    M5.Imu.getAccel(&accX, &accY, &accZ);
    M5.Imu.getGyro(&gyroX, &gyroY, &gyroZ);

    float totalAcc = sqrt(accX * accX + accY * accY + accZ * accZ);
    float totalGyro = sqrt(gyroX * gyroX + gyroY * gyroY + gyroZ * gyroZ);

    // A. 強いアタック (瞬間的な3.0G以上の全方向衝撃)
    if (totalAcc > 3.0f) {
      lastGestureTime = now;
      sendOSCGesture("ATTACK");
      flashScreenFeedback("ATTACK", YELLOW);
    }
    // B. 前方への突き (瞬間的な2.0G以上の水平衝撃)
    else if (abs(accY) > 2.0f || abs(accX) > 2.0f) {
      lastGestureTime = now;
      sendOSCGesture("THRUST");
      flashScreenFeedback("THRUST", CYAN);
    }
    // C. 激しいひねり/シェイク (高速な回転)
    else if (totalGyro > 350.0f) {
      lastGestureTime = now;
      sendOSCGesture("SHAKE");
      flashScreenFeedback("SHAKE", GREEN);
    }
  }

  // 画面の復帰処理
  checkScreenRestore(now);

  delay(1); // システムの安定稼働用マイクロウェイト
}

// =================================================================
// SUB FUNCTIONS: 各種補助関数
// =================================================================

/**
  ジェスチャー文字列をOSCで送信する関数
*/
void sendOSCGesture(String gestureName) {
  Serial.printf("[OSC] Sending Gesture: %s (%s)\n", gestureName.c_str(), oscGestureAddress.c_str());

  OSCMessage msg(oscGestureAddress.c_str());
  msg.add(gestureName.c_str());

  if (WiFi.status() == WL_CONNECTED) {
    Udp.beginPacket(targetIp, outPort);
    msg.send(Udp);
    Udp.endPacket();
    msg.empty();
  }

  // ビープ音で確認
  M5.Speaker.tone(1500, 80);
}

/**
  メイン画面の描画
*/
void drawMainUI() {
  M5.Lcd.fillScreen(BLACK);
  
  // 上部外枠ライン
  M5.Lcd.drawFastHLine(0, 24, 240, DARKGREY);
  
  M5.Lcd.setTextDatum(TC_DATUM);
  M5.Lcd.setTextSize(1.2);
  M5.Lcd.setTextColor(deviceSide == "Right" ? RED : CYAN);
  M5.Lcd.drawString(deviceSide + " STICK (OSC SENDER)", 120, 6);

  M5.Lcd.setTextDatum(MC_DATUM);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(1.5);
  M5.Lcd.drawString("STREAMING ACTIVE", 120, 68);

  // 下部説明
  M5.Lcd.setTextDatum(BC_DATUM);
  M5.Lcd.setTextSize(0.9);
  M5.Lcd.setTextColor(LIGHTGREY);
  M5.Lcd.drawString("BtnA: ATTACK  |  BtnB: THRUST", 120, 126);
}

// 画面フラッシュフィードバック制御用変数
unsigned long flashScreenEndTime = 0;
bool isFlashing = false;

/**
  ジェスチャー送信時の画面フラッシュ
*/
void flashScreenFeedback(String text, uint32_t color) {
  isFlashing = true;
  flashScreenEndTime = millis() + 150; // 150msフラッシュ

  M5.Lcd.fillScreen(color);
  M5.Lcd.setTextColor(BLACK);
  M5.Lcd.setTextDatum(MC_DATUM);
  M5.Lcd.setTextSize(3);
  M5.Lcd.drawString(text, 120, 68);
}

/**
  画面の復帰チェック
*/
void checkScreenRestore(unsigned long now) {
  if (isFlashing && now >= flashScreenEndTime) {
    isFlashing = false;
    drawMainUI();
  }
}
