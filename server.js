const dgram = require('dgram');
const WebSocket = require('ws');
const os = require('os');

// 設定
const UDP_PORT = 8000;
const WS_PORT = 8080;

// WebSocketサーバーの起動
const wss = new WebSocket.Server({ port: WS_PORT });
console.log(`[WebSocket] Server running on ws://localhost:${WS_PORT}`);

// 自身のローカルIPアドレスを表示（ユーザーが設定しやすいように）
const networkInterfaces = os.networkInterfaces();
console.log('\n--- 💡 ネットワーク設定用IPアドレス一覧 ---');
for (const interfaceName in networkInterfaces) {
  for (const net of networkInterfaces[interfaceName]) {
    if (net.family === 'IPv4' && !net.internal) {
      console.log(`  PCのIPアドレス: ${net.address}`);
    }
  }
}
console.log('-------------------------------------------\n');

// 接続中のクライアント一覧
const clients = new Set();

wss.on('connection', (ws) => {
  clients.add(ws);
  console.log(`[WebSocket] スマホ/ブラウザが接続されました。 (接続数: ${clients.size})`);
  
  ws.on('close', () => {
    clients.delete(ws);
    console.log(`[WebSocket] 切断されました。 (接続数: ${clients.size})`);
  });
});

// ブロードキャスト関数
function broadcast(jsonData) {
  const payload = JSON.stringify(jsonData);
  for (const client of clients) {
    if (client.readyState === WebSocket.OPEN) {
      client.send(payload);
    }
  }
}

// OSCバイナリメッセージの解析器 (最小構成で実装)
function parseOSC(msg) {
  try {
    let offset = 0;
    
    // 1. アドレスの抽出 (ヌル終端の文字列、4バイトアライン)
    let address = "";
    while (offset < msg.length) {
      const char = msg.readUInt8(offset++);
      if (char === 0) break;
      address += String.fromCharCode(char);
    }
    offset = Math.ceil(offset / 4) * 4;
    
    if (offset >= msg.length) return null;
    
    // 2. タイプタグの抽出 (',' から始まる文字列、4バイトアライン)
    if (msg.readUInt8(offset) !== 44) { // ',' (ASCII 44)
      return null;
    }
    offset++; // ','をスキップ
    
    let types = "";
    while (offset < msg.length) {
      const char = msg.readUInt8(offset++);
      if (char === 0) break;
      types += String.fromCharCode(char);
    }
    offset = Math.ceil(offset / 4) * 4;
    
    // 3. 引数の抽出
    const args = [];
    for (let i = 0; i < types.length; i++) {
      const type = types[i];
      if (type === 'f') { // Float (32-bit big endian float)
        if (offset + 4 > msg.length) break;
        args.push(msg.readFloatBE(offset));
        offset += 4;
      } else if (type === 's') { // String (null-terminated string, 4-byte aligned)
        let str = "";
        while (offset < msg.length) {
          const char = msg.readUInt8(offset++);
          if (char === 0) break;
          str += String.fromCharCode(char);
        }
        offset = Math.ceil(offset / 4) * 4;
        args.push(str);
      } else if (type === 'i') { // Integer (32-bit int)
        if (offset + 4 > msg.length) break;
        args.push(msg.readInt32BE(offset));
        offset += 4;
      }
    }
    
    return { address, args };
  } catch (err) {
    console.error('[OSC Parser Error]', err);
    return null;
  }
}

// UDPソケットの作成 (OSC受信)
const udpServer = dgram.createSocket('udp4');

udpServer.on('message', (msg, rinfo) => {
  const oscData = parseOSC(msg);
  if (oscData) {
    // センサーデータは量が多いのでジェスチャー受信時のみコンソールに表示
    if (oscData.address.endsWith('/gesture')) {
      console.log(`[OSC Gesture] アドレス: ${oscData.address}, データ: ${JSON.stringify(oscData.args)}`);
    }
    
    // クライアント(ブラウザ)へブロードキャスト
    broadcast(oscData);
  }
});

udpServer.on('listening', () => {
  const address = udpServer.address();
  console.log(`[UDP/OSC] サーバーが ${address.address}:${address.port} で起動しました。`);
  console.log('M5StickS3の outIp に上記「PCのIPアドレス」を設定してください。\n');
});

udpServer.bind(UDP_PORT);
