const dgram = require('dgram');
const WebSocket = require('ws');
const os = require('os');
const http = require('http');
const fs = require('fs');
const path = require('path');

// 設定
const PORT = 8080; // HTTPとWebSocketで共用
const UDP_PORT = 8000;

// HTTPサーバーの作成 (index.htmlを配信)
const server = http.createServer((req, res) => {
  // ルートまたはindex.htmlへのリクエストに対してファイルを返す
  if (req.url === '/' || req.url === '/index.html') {
    fs.readFile(path.join(__dirname, 'index.html'), 'utf8', (err, data) => {
      if (err) {
        res.writeHead(500, { 'Content-Type': 'text/plain; charset=utf-8' });
        res.end('index.html の読み込みに失敗しました。ファイルが存在するか確認してください。');
        return;
      }
      res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
      res.end(data);
    });
  } else {
    res.writeHead(404);
    res.end('Not Found');
  }
});

// WebSocketサーバーをHTTPサーバーに統合
const wss = new WebSocket.Server({ server });

const clients = new Set();

wss.on('connection', (ws) => {
  clients.add(ws);
  console.log(`[WebSocket] ブラウザが接続されました。 (接続数: ${clients.size})`);
  
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
    if (oscData.address.endsWith('/gesture')) {
      console.log(`[OSC Gesture] アドレス: ${oscData.address}, - データ: ${JSON.stringify(oscData.args)}`);
    }
    // クライアント(ブラウザ)へブロードキャスト
    broadcast(oscData);
  }
});

udpServer.on('listening', () => {
  const address = udpServer.address();
  console.log(`[UDP/OSC] サーバーが ${address.address}:${address.port} で起動しました。`);
});

udpServer.bind(UDP_PORT);

// HTTP & WebSocketサーバーを起動
server.listen(PORT, () => {
  console.log(`[Server] HTTP & WebSocket Server running on http://localhost:${PORT}`);
  console.log('\n--- 💡 ネットワーク設定用IPアドレス一覧 ---');
  const networkInterfaces = os.networkInterfaces();
  for (const interfaceName in networkInterfaces) {
    for (const net of networkInterfaces[interfaceName]) {
      if (net.family === 'IPv4' && !net.internal) {
        console.log(`  スマホのIPアドレス: ${net.address}`);
      }
    }
  }
  console.log('-------------------------------------------\n');
});
