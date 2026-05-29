# M5StickS3 Gesture & MIDI Dashboard

M5StickS3（左右2台）からWi-Fi（UDP/OSC）経由で送信されるジェスチャーデータとセンサーデータをリアルタイムにデバッグし、MIDI変換して外部機器（LiveTrak L6max等）へ送信するスマホ完結型の「単一HTMLWebアプリ」です。

サイバーパンク調の極めて美麗なダーク・ガラスモーフィズムデザインと、滑らかなThree.jsの3D慣性回転、Web MIDI APIでのスマートな音色制御、そして実機がなくても即座にテスト可能な「スタンドアロン・デバッグ・エミュレーター」を統合しています。

---

## 🌟 主な新機能：スマホ上での「MIDIノート変更＆保存」

左右の各手カードにある **「Left / Right MIDI Notes Map」** タップして展開すると、各ジェスチャー（ATTACK, THRUST, SHAKE, SLASH, TWIST, U-SHAPE）に割り当てるMIDIノート番号（0〜127）をスマホ画面上でいつでも自由に入力・変更できます。

- **LocalStorage自動保存**:
  数値を書き換えると即座にスマホブラウザ内の `LocalStorage` に自動保存されます。アプリを閉じたり、画面をリフレッシュしたり、翌日に再び開いても、変更したノート番号設定がそのままロードされます。
- **Web MIDI APIへの瞬時反映**:
  ジェスチャー検出時、現在設定されている最新のノート番号でMIDI Note ON/OFFが瞬時に送信されます。

---

## 🚀 起動手順（スマホ単体）

1. **ファイルをスマホに送る**:
   - `index.html` をAirDrop、メール、Googleドライブ等を用いてスマホ（iPhoneやAndroid）に送信し、スマホ内の「ファイル」アプリなどに保存します。
2. **ブラウザで開く**:
   - スマホの「ファイル」アプリから `index.html` をタップして開きます（自動的にSafariまたはChromeなどのブラウザで起動します）。
3. **音の有効化**:
   - 画面が開いたら、まず画面のどこかを1回タップします（スマホブラウザのセキュリティ制限を解除し、音が出るようにするため）。
4. **エミュレーターで確認**:
   - 画面最下部の「**Standalone Debug Emulator**」のボタンをタップ、またはスライダーをドラッグすると、実機がなくても3Dキューブが滑らかに回り、黄色いフラッシュが発生し、MIDI出力テストが行えます。

---

## 🎹 MIDI 仕様

### 1. ジェスチャー連動（自動 Note Off 制御）
ジェスチャー検知時に瞬時に Note ON (ベロシティ127) を送信後、100ms後に自動で Note OFF を送信する安全設計です。音が鳴り止まなくなるのを防ぎます。

- **初期設定 (右手 - ネオンレッド)**:
  - `ATTACK` ➔ Note 60 (C4 / ド)
  - `THRUST` ➔ Note 62 (D4 / レ)
  - `SHAKE` ➔ Note 65 (F4 / ファ)
  - `SLASH` ➔ Note 67 (G4 / ソ)
  - `TWIST` ➔ Note 69 (A4 / ラ)
  - `U-SHAPE` ➔ Note 71 (B4 / シ)
- **初期設定 (左手 - シアンブルー)**:
  - `ATTACK` ➔ Note 64 (E4 / ミ)
  - `THRUST` ➔ Note 66 (F#4 / ファ#)
  - `SHAKE` ➔ Note 68 (G#4 / ソ#)
  - `SLASH` ➔ Note 70 (A#4 / アシッド)
  - `TWIST` ➔ Note 72 (C5 / ド)
  - `U-SHAPE` ➔ Note 74 (D5 / レ)

### 2. 傾きデータの MIDI CC ストリーミング
加速度センサーから計算したロール（Roll）とピッチ（Pitch）を `0〜127` の範囲に変換し、リアルタイムでコントロールチェンジ（CC）を送信します（トグルで有効化可能）。

- **左手 (LEFT HAND)**: Roll: `CC 10` (Pan) / Pitch: `CC 11` (Expression)
- **右手 (RIGHT HAND)**: Roll: `CC 74` (Brightness) / Pitch: `CC 71` (Resonance)

---

## 📡 M5StickS3（実機）との接続方法

1. 同一Wi-Fiネットワーク上のPCで、OSC-WebSocket中継サーバー（以前の `server.js` 等）を起動します。
2. スマホの画面の「**WebSocket OSC Bridge IP**」入力欄に、**[PCのIPアドレス]:8080**（例: `192.168.11.3:8080` ）を入力します。
3. **[Connect]** ボタンをタップすると接続され、実機の動きとリアルタイムに同期します。

---

## 🛠️ 技術スタック
- **Front-end**: HTML5, Vanilla CSS3 (Neon Glassmorphic UI)
- **3D Graphics**: Three.js (r128)
- **Web API**: Web MIDI API, Web Audio API, WebSockets
- **Aesthetics**: Outfit Font, Noto Sans JP Font, FontAwesome
