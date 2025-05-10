const express = require('express');
const admin = require('firebase-admin');
const mqtt = require('mqtt');
const http = require('http');
const path = require('path');
const axios = require('axios');
const fs = require('fs-extra');
const { spawn } = require('child_process');
const multer = require('multer');
const bodyParser = require('body-parser');
const socketIO = require('socket.io');

const app = express();
const server = http.createServer(app);
const port = process.env.PORT || 3000;

const DOWNLOAD_DIR = path.join(__dirname, 'database');
const DATA_DIR = path.join(__dirname, 'data');
const LOG_DIR = path.join(__dirname, 'logs');

app.use(express.static(path.join(__dirname, 'public')));
app.use(bodyParser.json());
app.use(bodyParser.urlencoded({ extended: true }));

const io = socketIO(server);

try {
  const serviceAccount = require('./serviceAccountKey.json');
  admin.initializeApp({
    credential: admin.credential.cert(serviceAccount),
    storageBucket: 'test-96fdf.appspot.com'
  });
  const bucket = admin.storage().bucket();
  console.log('Đã kết nối đến Firebase');
} catch (error) {
  console.error('Lỗi khi kết nối đến Firebase:', error);
}

const storage = multer.diskStorage({
  destination: function (req, file, cb) {
    const uploadDir = path.join(__dirname, 'uploads');
    fs.ensureDirSync(uploadDir);
    cb(null, uploadDir);
  },
  filename: function (req, file, cb) {
    cb(null, Date.now() + '-' + file.originalname);
  }
});
const upload = multer({ storage: storage });

const webappURL = 'https://script.google.com/macros/s/AKfycbzvP5cKI78IiGVe2IVkMIJiC-9pwV0infUcoxbhi-ExFT7f7avgoF02Kijnrh_KUZJxcg/exec';

const mqttBroker = 'mqtt://broker.emqx.io:1883';
const mqttOptions = {
  clientId: 'nodejs_server_' + Math.random().toString(16).substring(2, 8),
  username: '',
  password: '',
  clean: true
};

const databaseTopic = 'database/acs/users';
const logTopic = 'database/acs/logs';
const deleteUserTopic = 'feature/acs/delete';
const notifyTopic = 'feature/acs/notify';
const updateTopic = 'feature/acs/update';
const traceTopic = 'feature/acs/trace';
const traceResponseTopic = 'response/acs/trace';

const mqttClient = mqtt.connect(mqttBroker, mqttOptions);

mqttClient.on('connect', () => {
  console.log('Đã kết nối đến MQTT broker');
  mqttClient.subscribe(databaseTopic);
  mqttClient.subscribe(logTopic);
  mqttClient.subscribe(deleteUserTopic);
  mqttClient.subscribe(notifyTopic);
  mqttClient.subscribe(updateTopic);
  mqttClient.subscribe(traceTopic);
  mqttClient.subscribe(traceResponseTopic);
});

mqttClient.on('error', (err) => {
  console.error('Lỗi kết nối MQTT:', err);
});

let databaseData = [];
let logData = [];

mqttClient.on('message', (topic, message) => {
  try {
    const payload = JSON.parse(message.toString());
    console.log('Nhận được tin nhắn MQTT:', topic, payload);
    
    if (topic === databaseTopic) {
      handleDatabaseUpdate(payload);
    } else if (topic === traceTopic) {
        handleTrace(payload);
      } else if (topic === updateTopic) {
          handleUpdate(payload);
        } else if (topic === notifyTopic) {
            handleNotify(payload);
          } else if (topic === deleteUserTopic) {
            if (payload.identification) {
              handleUserDeletion(payload.identification)
                .then(() => {
                  console.log(`Đã xóa thành công người dùng ${payload.identification}`);
                  io.emit('updateNotification', {
                    main: "Xóa người dùng thành công",
                    sub: `Đã xóa người dùng ${payload.identification}`,
                    timeout: "5000",
                    color: "green"
                  });
                })
                .catch(error => {
                  console.error('Lỗi khi xóa người dùng:', error);
                  io.emit('updateNotification', {
                    main: "Lỗi xóa người dùng",
                    sub: "Không thể xóa người dùng",
                    timeout: "5000",
                    color: "red"
                  });
                });
            }
          }
  } catch (error) {
    console.error('Lỗi xử lý tin nhắn MQTT:', error);
  }
});

async function syncDatabaseWithStorage() {
  const message = await downloadAllFiles();
  io.emit('updateNotification', {
    main: "Hoàn tất cập nhật CSDL",
    sub: "Đã cập nhật dữ liệu hình ảnh",
    timeout: "5000",
    color: "green"
  });
  return message;
}

function handleDatabaseUpdate(data) {
  try {
    const existingIndex = databaseData.findIndex(item => item.id === data.id);
    const identification = data.id.toString();

    if (data.name) {
      data.name = data.name.replace(/\\u([a-fA-F0-9]{4})/g, (match, grp) => {
        return String.fromCharCode(parseInt(grp, 16));
      });
    }

    json = {
        "command": "Upload",
        "content1": identification,
        "content2": "",
        "content3": "",
        "content4": ""
    }
    const response = axios.post(webappURL, json, {
      headers: { 'Content-Type': 'application/json' }
    });
    console.log('Phản hồi từ Web App', response.json);
    
    if (existingIndex >= 0) {
      databaseData[existingIndex] = data;
    } else {
      databaseData.push(data);
    }
    
    saveDatabase();
    return true;
  } catch (error) {
    console.error('Lỗi khi cập nhật database:', error);
    return false;
  }
}

function handleLogUpdate(data) {
  try {
    logData.unshift(data);
    
    if (logData.length > 500) {
      logData = logData.slice(0, 500);
    }
    
    saveLog();
    return true;
  } catch (error) {
    console.error('Lỗi khi cập nhật log:', error);
    return false;
  }
}

async function handleUserDeletion(userId) {
  try {
    const userIdStr = userId.toString();
    const bucket = admin.storage().bucket();
    await bucket.deleteFiles({
      prefix: `${userIdStr}/`
    });
    console.log(`Đã xóa thư mục ${userIdStr} trên Firebase Storage`);

    const databasePath = path.join(DATA_DIR, 'database.json');
    
    const fileContent = await fs.readFile(databasePath, 'utf8');
    let currentData = JSON.parse(fileContent || '[]');

    currentData = currentData.filter(item => {
      const itemId = item.id?.toString();
      return itemId !== userIdStr;
    });
    
    databaseData = currentData;
    
    await fs.writeFile(
      databasePath,
      JSON.stringify(currentData, null, 2)
    );
    
    console.log(`Đã xóa người dùng ${userIdStr} trong database`);

    await syncDatabaseWithStorage();

    mqttClient.publish('feature/acs/reload', JSON.stringify({
      action: 'reload',
      timestamp: Date.now()
    }));

    return true;
  } catch (error) {
    console.error('Lỗi tiến trình xóa:', error);
    throw error;
  }
}

async function saveDatabase() {
  try {
    const databasePath = path.join(DATA_DIR, 'database.json');
    await fs.writeFile(
      databasePath,
      JSON.stringify(databaseData || [], null, 2)
    );
    console.log('Đã lưu database thành công');
    mqttClient.publish('feature/acs/reload', JSON.stringify({
      action: 'reload',
      timestamp: Date.now()
    }));
    return true;
  } catch (error) {
    console.error('Lỗi khi lưu database:', error);
    throw error;
  }
}

try {
  fs.ensureDirSync(DATA_DIR);
  fs.ensureDirSync(LOG_DIR);

  const databasePath = path.join(DATA_DIR, 'database.json');
  const logPath = path.join(DATA_DIR, 'logs.json');

  if (!fs.existsSync(databasePath)) {
    fs.writeFileSync(databasePath, '[]');
  } 
  if (!fs.existsSync(logPath)) {
    fs.writeFileSync(logPath, '[]');
  }
} catch (error) {
  console.error('Lỗi khởi tạo dữ liệu:', error);
}

try {
  if (fs.existsSync(path.join(DATA_DIR, 'database.json'))) {
    databaseData = JSON.parse(
      fs.readFileSync(path.join(DATA_DIR, 'database.json'))
    );
  } 
  if (fs.existsSync(path.join(DATA_DIR, 'logs.json'))) {
    logData = JSON.parse(
      fs.readFileSync(path.join(DATA_DIR, 'logs.json'))
    );
  }
} catch (error) {
  console.error('Lỗi khi đọc dữ liệu từ file:', error);
}

async function saveLog() {
  try {
    fs.writeFileSync(
      path.join(DATA_DIR, 'logs.json'),
      JSON.stringify(logData || [], null, 2)
    );
    console.log('Đã lưu logs thành công');

    const latestLog = logData[0];
    if (latestLog && latestLog.id !== 'N/A') {
      const userLogPath = path.join(LOG_DIR, `${latestLog.id}.json`);

      const logKey = `${latestLog.date} - ${latestLog.time}`;
      const logValue = latestLog.image === 'N/A' ? 'thẻ' : 'khuôn mặt';
      const newLogEntry = {
        [logKey]: logValue
      };

      let userLogs = [];
      if (fs.existsSync(userLogPath)) {
        const fileContent = fs.readFileSync(userLogPath, 'utf8');
        userLogs = JSON.parse(fileContent);
      }

      userLogs.unshift(newLogEntry);

      fs.writeFileSync(
        userLogPath,
        JSON.stringify(userLogs, null, 2)
      );
      console.log(`Đã lưu log cho người dùng ${latestLog.id}`);
    }
  } catch (error) {
    console.error('Lỗi khi lưu logs:', error);
  }
}

function getCurrentDateTime() {
  const now = new Date();
  
  const months = [
    'January', 'February', 'March', 'April', 'May', 'June',
    'July', 'August', 'September', 'October', 'November', 'December'
  ];
  const currentDate = `${months[now.getMonth()]} ${now.getDate()}, ${now.getFullYear()}`;
  
  const hours = now.getHours().toString().padStart(2, '0');
  const minutes = now.getMinutes().toString().padStart(2, '0');
  const seconds = now.getSeconds().toString().padStart(2, '0');
  const currentTime = `${hours}:${minutes}:${seconds}`;
  
  return { currentDate, currentTime };
}

function createLogEntry(data) {
  const now = new Date();
  const formattedDate = now.toLocaleDateString('en-GB', { 
    day: 'numeric', month: 'long', year: 'numeric' 
  });
  const formattedTime = now.toLocaleTimeString('en-GB');
  
  return {
    date: formattedDate,
    time: formattedTime,
    ...data
  };
}

async function runFaceRecognition() {
  return new Promise((resolve, reject) => {
    const pythonProcess = spawn('python', ['cache.py']);
    
    pythonProcess.stdout.on('data', (data) => {
      console.log(`Python output: ${data}`);
    });

    pythonProcess.stderr.on('data', (data) => {
      console.error(`Python error: ${data}`);
    });

    pythonProcess.on('close', (code) => {
      if (code === 0) {
        resolve('Hoàn tất nhận diện khuôn mặt và cập nhật dữ liệu');
      } else {
        reject(`Lỗi ${code}`);
      }
    });
  });
}

async function downloadFiles() {
  try {
    const bucket = admin.storage().bucket();
    await fs.ensureDir(DOWNLOAD_DIR);

    const [files] = await bucket.getFiles();
    console.log(`Đã tìm thấy ${files.length} file trên Firebase Storage`);

    const firebaseFolders = new Set(
      files
        .map(file => file.name.split('/')[0])
        .filter(folder => folder && folder !== 'src')
    );

    const localFolders = new Set(
      (await fs.readdir(DOWNLOAD_DIR))
        .filter(item => fs.statSync(path.join(DOWNLOAD_DIR, item)).isDirectory())
    );

    for (const localFolder of localFolders) {
      if (!firebaseFolders.has(localFolder)) {
        console.log(`Đã xóa thư mục: ${localFolder}`);
        await fs.remove(path.join(DOWNLOAD_DIR, localFolder));
      }
    }

    for (const file of files) {
      const [folderName, fileName] = file.name.split('/');

      if (folderName === 'logs' || !fileName || fileName === 'verification.txt' || fileName === 'information.txt') continue;

      if (localFolders.has(folderName)) {
        console.log(`Bỏ qua thư mục đã tồn tại: ${folderName}`);
        continue;
      }

      const destFolder = path.join(DOWNLOAD_DIR, folderName);
      const destPath = path.join(destFolder, fileName);

      await fs.ensureDir(destFolder);

      console.log(`Đang tải ${file.name}...`);
      await file.download({
        destination: destPath
      });
      console.log(`Đã tải về ${file.name}`);
    }

    return 'Tải xuống hoàn tất';
  } catch (error) {
    console.error('Lỗi:', error);
    throw error;
  }
}

async function downloadAllFiles() {
  try {
    const downloadResult = await downloadFiles();
    await runFaceRecognition();
    return 'Tải xuống và nhận diện khuôn mặt hoàn tất';
  } catch (error) {
    console.error('Lỗi:', error);
    return 'Lỗi trong quá trình tải xuống và nhận diện khuôn mặt';
  }
}

async function getInformation(folderName) {
  try {
    const bucket = admin.storage().bucket();
    const file = bucket.file(`${folderName}/information.txt`);
    const [exists] = await file.exists();
    
    if (!exists) {
      return "unidentified";
    }

    const [content] = await file.download();
    return content.toString().trim();
  } catch (error) {
    console.error('Lỗi khi đọc information.txt:', error);
    return "unidentified";
  }
}

function processInformation(information) {
  let id = '', username = '', dob = '', room = '';

    const parts = information.split(':');
    if (parts.length === 2) {
      const info = parts[1].split('$');
      if (info.length === 4) {
        username = info[0];
        dob = info[1];
        room = info[2];
        id = info[3];
      }
  }
  
  return { id, username, dob, room };
}

async function performFaceRecognition(imagePath) {
  return new Promise((resolve, reject) => {
    const pythonProcess = spawn('python', ['predict.py']);
    let result = '';
    
    pythonProcess.stdin.write(imagePath + '\n');
    pythonProcess.stdin.end();

    pythonProcess.stdout.on('data', (data) => {
      result += data.toString();
    });

    pythonProcess.stderr.on('data', (data) => {
      console.error(`Python error: ${data}`);
    });

    pythonProcess.on('close', async (code) => {
      if (code === 0) {
        const folderMatch = result.match(/Matched Folder: (.+)/);
        const confidenceMatch = result.match(/Confidence: (.+)%/);
        
        if (folderMatch && confidenceMatch) {
          const folder = folderMatch[1].trim();
          const confidence = parseFloat(confidenceMatch[1]) / 100;
          
          const information = await getInformation(folder);
          
          resolve({
            status: 'success',
            folder: folder,
            confidence: confidence,
            information: information
          });
        } else {
          resolve({
            status: 'error',
            message: 'Could not process image'
          });
        }
      } else {
        reject(`Lỗi khi nhận diện khuôn mặt ${code}`);
      }
    });
  });
}

app.get('/api/acs/database', (req, res) => {
  try {
    const databasePath = path.join(DATA_DIR, 'database.json');
    if (!fs.existsSync(databasePath)) {
      return res.status(404).json({ error: 'Không tìm thấy database.json' });
    }
    const data = fs.readFileSync(databasePath, 'utf8');
    res.json(JSON.parse(data || '[]'));
  } catch (error) {
    console.error('Lỗi khi đọc database.json:', error);
    res.status(500).json({ error: 'Lỗi server' });
  }
});

app.get('/api/acs/logs', (req, res) => {
  try {
    const logsPath = path.join(DATA_DIR, 'logs.json');
    if (!fs.existsSync(logsPath)) {
      return res.status(404).json({ error: 'Không tìm thấy logs.json' });
    }
    const data = fs.readFileSync(logsPath, 'utf8');
    res.json(JSON.parse(data || '[]'));
  } catch (error) {
    console.error('Lỗi khi đọc logs.json:', error);
    res.status(500).json({ error: 'Lỗi server' });
  }
});

app.post('/api/acs/database', (req, res) => {
  try {
    const userData = req.body;
    
    if (!userData.id || !userData.name) {
      return res.status(400).json({ error: 'Dữ liệu không hợp lệ' });
    }
    
    const success = handleDatabaseUpdate(userData);
    
    if (!success) {
      return res.status(500).json({ error: 'Lỗi khi cập nhật database' });
    }
    
    const logEntry = createLogEntry(userData);
    handleLogUpdate(logEntry);
    
    mqttClient.publish(databaseTopic, JSON.stringify(userData));
    mqttClient.publish(logTopic, JSON.stringify(logEntry));
    
    res.status(201).json({ success: true, message: 'Đã cập nhật dữ liệu' });
  } catch (error) {
    console.error('Lỗi khi thêm/cập nhật người dùng:', error);
    res.status(500).json({ error: 'Lỗi server' });
  }
});

async function handleUpdate(data) {
  if (data.command == 'update') {
    try {
      const message = await syncDatabaseWithStorage();
    } catch (error) {
      console.error('Lỗi:', error);
    }
  }
}

app.post('/recall', async (req, res) => {
  const identification = req.body.identification;
  try {
    const information = await getInformation(identification);
    res.json({information: information });

    const { currentDate, currentTime} = getCurrentDateTime();
    const { id, username, dob, room } = processInformation(information);
    console.log('Nhận diện:', id, username, dob, room);

    if (information === "unidentified") {
      data = {
        "command": "Alert",
        "content1": currentDate,
        "content2": currentTime,
        "content3": "thẻ",
        "content4": "không có"
      }
      const response = await axios.post(webappURL, data, {
        headers: { 'Content-Type': 'application/json' }
      });
      console.log('Phản hồi từ Web App', response.data);
    }
    const logEntry = {
      date: currentDate,
      time: currentTime,
      id: id || 'N/A',
      name: username || 'N/A',
      dob: dob || 'N/A',
      room: room || 'N/A',
      image: 'N/A',
    };
    
    handleLogUpdate(logEntry);
    mqttClient.publish(logTopic, JSON.stringify(logEntry));
  } catch (error) {
    console.error('Lỗi:', error);
    res.status(500).json({ error: 'Lỗi truy xuất thông tin' });
  }
});

async function handleTrace(data) {
  const identification = data.identification;
  try {
    const filePath = path.join(LOG_DIR, `${identification}.json`);
    if (!fs.existsSync(filePath)) {
      console.log('Không tìm thấy file:', filePath);
      return;
    }

    const fileContent = await fs.readFileSync(filePath, 'utf8');
    const logData = JSON.parse(fileContent);

    const formattedData = {
      [identification]: logData
    };

    mqttClient.publish(traceResponseTopic, JSON.stringify(formattedData));
  } catch (error) {
    console.error('Lỗi:', error);
    throw error;
  }
}

app.post('/upload', upload.single('image'), async (req, res) => {
  if (!req.file) {
    return res.status(400).json({ error: 'Không có hình ảnh được tải lên' });
  }

  try {
    const result = await performFaceRecognition(req.file.path);

    res.json(result);
    const status = result.status;

    const imageBuffer = fs.readFileSync(req.file.path);
    const timestamp = Date.now();
    const fileName = `logs/${timestamp}.jpg`;

    const bucket = admin.storage().bucket();
    const file = bucket.file(fileName);
    
    await file.save(imageBuffer, {
      metadata: {
        contentType: 'image/jpeg',
      }
    });

    const [url] = await file.getSignedUrl({
      action: 'read',
      expires: '03-01-2500'
    });

    const { currentDate, currentTime } = getCurrentDateTime();

    if (status === 'error') {
      data = {
        "command": "Alert",
        "content1": currentDate,
        "content2": currentTime,
        "content3": "khuôn mặt",
        "content4": url
      }
      const response = await axios.post(webappURL, data, {
        headers: { 'Content-Type': 'application/json' }
      });
      console.log('Phản hồi từ Web App', response.data);
    }

    let id = '', username = '', dob = '', room = '';
    if (result.status === 'success') {
      ({ id, username, dob, room } = processInformation(result.information));
    }

    const logEntry = {
      date: currentDate,
      time: currentTime,
      id: id || 'N/A',
      name: username || 'N/A',
      dob: dob || 'N/A',
      room: room || 'N/A',
      image: url,
      timestamp: currentDate + ' ' + currentTime,
      confidence: result.confidence
    };
    
    handleLogUpdate(logEntry);
    mqttClient.publish(logTopic, JSON.stringify(logEntry));
    
    if (fs.existsSync(req.file.path)) {
      fs.unlinkSync(req.file.path);
    }
  } catch (error) {
    console.error('Lỗi:', error);
    if (req.file && fs.existsSync(req.file.path)) {
      fs.unlinkSync(req.file.path);
    }
    res.status(500).json({ error: 'Lỗi nhận diện khuôn mặt' });
  }
});

app.get('/notification', (req, res) => {
  const htmlPath = path.join(__dirname, 'views', 'notification.html');
  res.sendFile(htmlPath);
});

async function handleNotify(data) {
  const main = data.main;
  const sub = data.sub;
  const timeout = data.timeout;
  const color = data.color;
  io.emit('updateNotification', { 
    main, 
    sub, 
    timeout: timeout || null,
    color: color || 'green'
  });
}

app.get('/', (req, res) => {
  res.sendFile(path.join(__dirname, 'views', 'management.html'));
});

server.listen(port, () => {
  console.log(`Server đang chạy tại http://localhost:${port}`);
});