import 'package:flutter/material.dart';
import 'package:flutter_svg/flutter_svg.dart';
import 'package:flutter/gestures.dart';
import 'package:image_picker/image_picker.dart';
import 'dart:io';
import 'package:dotted_border/dotted_border.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:http/http.dart' as http;
import 'dart:convert';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:firebase_core/firebase_core.dart';
import 'package:firebase_storage/firebase_storage.dart';
import 'firebase_options.dart';
import 'package:path_provider/path_provider.dart';
import 'package:mqtt_client/mqtt_client.dart';
import 'package:mqtt_client/mqtt_server_client.dart';

const String webappUrl = "https://script.google.com/macros/s/AKfycbzvP5cKI78IiGVe2IVkMIJiC-9pwV0infUcoxbhi-ExFT7f7avgoF02Kijnrh_KUZJxcg/exec";
late MqttServerClient client;
_DashboardPageState? currentDashboardState;
final GlobalKey<NavigatorState> navigatorKey = GlobalKey<NavigatorState>();

Future<void> setupMqttClient() async {
  client = MqttServerClient('broker.emqx.io', 'flutter_client${DateTime.now().millisecondsSinceEpoch}');
  client.port = 1883;
  client.logging(on: true);
  client.keepAlivePeriod = 20;
  client.connectTimeoutPeriod = 30000;
  
  client.onConnected = onConnected;
  client.onDisconnected = onDisconnected;
  client.onSubscribed = onSubscribed;
  client.onSubscribeFail = onSubscribeFail;
  client.pongCallback = pong;

  final connMessage = MqttConnectMessage()
      .withClientIdentifier('flutter_client')
      .startClean()
      .withWillQos(MqttQos.atLeastOnce)
      .withWillRetain()
      .withWillMessage('Client disconnected')
      .withWillTopic('willtopic');
  
  client.connectionMessage = connMessage;

  try {
    print('MQTT client connecting....');
    
    int retryCount = 0;
    bool connected = false;
    
    while (!connected && retryCount < 5) {
      try {
        await client.connect();
        if (client.connectionStatus!.state == MqttConnectionState.connected) {
          connected = true;
          print('MQTT client connected on attempt ${retryCount + 1}');
        }
      } catch (e) {
        retryCount++;
        if (retryCount < 5) {
          print('Connection attempt $retryCount failed, retrying in 5 seconds...');
          await Future.delayed(const Duration(seconds: 5));
        }
      }
    }
    
    if (!connected) {
      throw Exception('Failed to connect after $retryCount attempts');
    }
    
    _subscribeTo('database/acs/users');
    _subscribeTo('feature/acs/trace'); 
    _subscribeTo('response/acs/trace');
    
    client.updates!.listen((List<MqttReceivedMessage<MqttMessage>> c) {
      final MqttPublishMessage message = c[0].payload as MqttPublishMessage;
      final payload = MqttPublishPayload.bytesToStringAsString(message.payload.message);
      
      print('Received message: $payload from topic: ${c[0].topic}>');
      
      switch (c[0].topic) {
        case 'response/acs/trace':
          handleResponseMessage(payload);
          break;
      }
    });
  } catch (e) {
    print('Exception: $e');
    client.disconnect();
  }
}

void _subscribeTo(String topic) {
  try {
    client.subscribe(topic, MqttQos.atLeastOnce);
  } catch (e) {
    print('Error subscribing to $topic: $e');
  }
}

void onConnected() {
  print('Connected to MQTT broker');
}

void onDisconnected() {
  print('Disconnected from MQTT broker');
}

void onSubscribed(String topic) {
  print('Subscribed to topic: $topic');
}

void onSubscribeFail(String topic) {
  print('Failed to subscribe to topic: $topic');
}

void pong() {
  print('Ping response received');
}

void handleResponseMessage(String payload) {
  print('Handling response message: $payload');
  try {
    final Map<String, dynamic> jsonData = json.decode(payload);
    
    if (currentDashboardState != null) {
      final username = currentDashboardState!.widget.username;
      final entries = jsonData.entries.where((entry) => entry.value is List).toList();
      
      if (entries.isNotEmpty) {
        final key = entries[0].key;
        if (key == username) {
          final userHistory = entries[0].value as List;

          if (currentDashboardState!.mounted) {
            WidgetsBinding.instance.addPostFrameCallback((_) {
              currentDashboardState!._updateHistoryData(userHistory.map((item) {
                final entry = item as Map<String, dynamic>;
                final dateTime = entry.keys.first;
                final type = entry[dateTime].toString();
                return {dateTime: type};
              }).toList());
            });
          }
        }
      }
    }
  } catch (e) {
    print('Error processing response message: $e');
  }
}

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  final prefs = await SharedPreferences.getInstance();
  final email = prefs.getString('email') ?? '';
  final username = prefs.getString('username') ?? '';
  final password = prefs.getString('password') ?? '';
  await Firebase.initializeApp(
    options: DefaultFirebaseOptions.currentPlatform,
  );
  await setupMqttClient();
  runApp(MyApp(
    initialRoute: email.isNotEmpty ? '/loginForm' : '/login',
    email: email,
    username: username,
    password: password,
  ));
}

class MyApp extends StatelessWidget {
  final String initialRoute;
  final String email;
  final String username;
  final String password;

  const MyApp({
    super.key,
    required this.initialRoute,
    required this.email,
    required this.username,
    required this.password,
  });

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Access Control App',
      theme: ThemeData(
        primarySwatch: Colors.green,
        visualDensity: VisualDensity.adaptivePlatformDensity,
      ),
      localizationsDelegates: const [
        GlobalMaterialLocalizations.delegate,
        GlobalWidgetsLocalizations.delegate,
        GlobalCupertinoLocalizations.delegate,
      ],
      supportedLocales: const [
        Locale('vi', 'VN'),
        Locale('en', 'US'),
      ],
      locale: const Locale('vi', 'VN'),
      initialRoute: initialRoute,
      routes: {
        '/login': (context) => const LoginPage(),
        '/loginForm': (context) => LoginFormPage(
              email: email,
            ),
        '/dashboard': (context) => DashboardPage(
              username: username,
              email: email,
            ),
      },
    );
  }
}

class LoginPage extends StatefulWidget {
  const LoginPage({super.key});

  @override
  State<LoginPage> createState() => _LoginPageState();
}

class _LoginPageState extends State<LoginPage> {
  final TextEditingController _emailController = TextEditingController();
  
  @override
  void initState() {
    super.initState();
    _loadSavedEmail();
  }
  
  Future<void> _loadSavedEmail() async {
    final prefs = await SharedPreferences.getInstance();
    setState(() {
      _emailController.text = prefs.getString('email') ?? '';
    });
  }
  
  Future<void> _saveEmail(String email) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('email', email);
  }
  
  @override
  void dispose() {
    _emailController.dispose();
    super.dispose();
  }

  Future<void> _requestRegistration(BuildContext context) async {
    showDialog(
      context: context,
      barrierDismissible: false,
      builder: (BuildContext context) {
        return const Center(
          child: CircularProgressIndicator(
            valueColor: AlwaysStoppedAnimation<Color>(Color(0xFF4CAF50)),
          ),
        );
      },
    );
    
    try {
      final url = Uri.parse(webappUrl);
      final requestData = {
        "command": "Request",
        "content1": _emailController.text,
        "content2": "",
        "content3": "",
        "content4": ""
      };
      
      print('Sending request data: $requestData');
      
      final response = await http.post(
        url,
        headers: {
          'Content-Type': 'application/json',
        },
        body: json.encode(requestData),
      );
      
      Navigator.pop(context);
      
      if (response.statusCode == 200 || response.statusCode == 201 || response.statusCode == 302) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content: Text('Đã gửi yêu cầu thành công'),
            backgroundColor: Color(0xFF4CAF50),
          ),
        );
      } else {
        throw Exception('Failed to request registration: ${response.statusCode}');
      }
    } catch (e) {
      Navigator.pop(context);
      
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text('Lỗi: $e'),
          backgroundColor: Colors.red,
        ),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFFF4F4F4),
      body: Center(
        child: Container(
          constraints: const BoxConstraints(maxWidth: 400),
          margin: const EdgeInsets.all(20),
          child: Card(
            elevation: 4,
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(12),
            ),
            child: Padding(
              padding: const EdgeInsets.all(24),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Container(
                    width: 80,
                    height: 80,
                    decoration: BoxDecoration(
                      color: const Color(0xFFF4F4F4),
                      shape: BoxShape.circle,
                    ),
                    child: Center(
                      child: SvgPicture.asset(
                        'assets/images/avatar.svg',
                        width: 80,
                        height: 80,
                      ),
                    ),
                  ),
                  const SizedBox(height: 16),
                  const Text(
                    'Xin chào',
                    style: TextStyle(
                      fontSize: 24,
                      fontWeight: FontWeight.bold,
                      color: Color(0xFF1A1A1A),
                    ),
                  ),
                  const SizedBox(height: 8),
                  const Text(
                    'Đăng nhập để tiếp tục',
                    style: TextStyle(color: Color(0xFF6B7280), fontSize: 14),
                  ),
                  const SizedBox(height: 24),
                  
                  TextField(
                    controller: _emailController,
                    keyboardType: TextInputType.emailAddress,
                    decoration: InputDecoration(
                      hintText: 'Nhập email của bạn',
                      contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 12),
                      border: OutlineInputBorder(
                        borderRadius: BorderRadius.circular(8),
                        borderSide: const BorderSide(color: Color(0xFFD1D5DB)),
                      ),
                      focusedBorder: OutlineInputBorder(
                        borderRadius: BorderRadius.circular(8),
                        borderSide: const BorderSide(color: Color(0xFF4CAF50), width: 2),
                      ),
                    ),
                  ),
                  const SizedBox(height: 16),
                  
                  ElevatedButton(
                    onPressed: () async {
                      final email = _emailController.text;
                      await _saveEmail(email);
                      
                      Navigator.pushReplacement(
                        context,
                        MaterialPageRoute(
                          builder: (context) => LoginFormPage(
                            email: email,
                          ),
                        ),
                      );
                    },
                    style: ElevatedButton.styleFrom(
                      backgroundColor: const Color(0xFF4CAF50),
                      foregroundColor: const Color(0xFFF4F4F4),
                      minimumSize: const Size(double.infinity, 48),
                      shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(8),
                      ),
                      elevation: 0,
                    ),
                    child: Row(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        const SizedBox(width: 12),
                        const Text(
                          'Xác nhận email',
                          style: TextStyle(fontSize: 16),
                        ),
                      ],
                    ),
                  ),
                  const SizedBox(height: 12),
                  Center(
                    child: RichText(
                      text: TextSpan(
                        text: 'Bạn chưa có tài khoản? ',
                        style: const TextStyle(
                          fontSize: 14,
                          color: Color(0xFF6B7280),
                        ),
                        children: [
                          TextSpan(
                            text: 'Gửi yêu cầu!',
                            style: const TextStyle(
                              color: Color(0xFF4CAF50),
                              fontWeight: FontWeight.w500,
                            ),
                            recognizer: TapGestureRecognizer()
                              ..onTap = () {
                                if (_emailController.text.isEmpty) {
                                  ScaffoldMessenger.of(context).showSnackBar(
                                    const SnackBar(
                                      content: Text('Hãy nhập email'),
                                      backgroundColor: Colors.red,
                                    ),
                                  );
                                } else {
                                  _requestRegistration(context);
                                }
                              },
                          ),
                        ],
                      ),
                    ),
                  ),
                  const SizedBox(height: 24),
                  Text(
                    '© 2025 NTP HCMUT',
                    style: TextStyle(color: Colors.grey.shade600, fontSize: 12),
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}

class LoginFormPage extends StatefulWidget {
  final String email;
  
  const LoginFormPage({
    super.key,
    this.email = '',
  });

  @override
  State<LoginFormPage> createState() => _LoginFormPageState();
}

class _LoginFormPageState extends State<LoginFormPage> {
  final TextEditingController _usernameController = TextEditingController(text: '');
  final TextEditingController _passwordController = TextEditingController(text: '');
  bool _obscurePassword = true;

  @override
  void initState() {
    super.initState();
    _loadSavedCredentials();
  }

  Future<void> _loadSavedCredentials() async {
    final prefs = await SharedPreferences.getInstance();
    setState(() {
      _usernameController.text = prefs.getString('username') ?? '';
      _passwordController.text = prefs.getString('password') ?? '';
    });
    if (_usernameController.text.isNotEmpty && _passwordController.text.isNotEmpty) {
      _verifyLogin();
    }
  }

  Future<void> _saveCredentials(String username, String password) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('username', username);
    await prefs.setString('password', password);
  }

  @override
  void dispose() {
    _usernameController.dispose();
    _passwordController.dispose();
    super.dispose();
  }

  Future<void> _verifyLogin() async {
    final username = _usernameController.text;
    final password = _passwordController.text;
    
    if (username.isEmpty || password.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Vui lòng nhập đầy đủ thông tin đăng nhập'),
          backgroundColor: Colors.red,
        ),
      );
      return;
    }
    
    showDialog(
      context: context,
      barrierDismissible: false,
      builder: (BuildContext context) {
        return const Center(
          child: CircularProgressIndicator(
            valueColor: AlwaysStoppedAnimation<Color>(Color(0xFF4CAF50)),
          ),
        );
      },
    );
    
    try {
      final Reference ref = FirebaseStorage.instance.ref().child('$username/verification.txt');
      final Directory tempDir = await getTemporaryDirectory();
      final File tempFile = File('${tempDir.path}/verification.txt');
      
      try {
        await ref.writeToFile(tempFile);
        final String content = await tempFile.readAsString();
        final List<String> parts = content.split('\$');
        if (parts.length >= 2) {
          final String emailCheck = parts[0];
          final String passwordCheck = parts[1];
          
          Navigator.pop(context);
          
          if (emailCheck == widget.email && passwordCheck == password) {
            await _saveCredentials(username, password);
            
            Navigator.pushReplacement(
              context,
              MaterialPageRoute(
                builder: (context) => DashboardPage(
                  username: username,
                  email: widget.email,
                ),
              ),
            );
          } else {
            ScaffoldMessenger.of(context).showSnackBar(
              const SnackBar(
                content: Text('Thông tin đăng nhập sai'),
                backgroundColor: Colors.red,
              ),
            );
          }
        } else {
          throw Exception('Định dạng file không hợp lệ');
        }
      } catch (e) {
        Navigator.pop(context);
        
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content: Text('Thông tin đăng nhập không tồn tại'),
            backgroundColor: Colors.red,
          ),
        );
      } finally {
        if (await tempFile.exists()) {
          await tempFile.delete();
        }
      }
    } catch (e) {
      Navigator.pop(context);
      
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text('Lỗi: $e'),
          backgroundColor: Colors.red,
        ),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFFF4F4F4),
      body: Center(
        child: Container(
          constraints: const BoxConstraints(maxWidth: 400),
          margin: const EdgeInsets.all(20),
          child: Card(
            elevation: 4,
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(12),
            ),
            child: Stack(
              children: [
                Padding(
                  padding: const EdgeInsets.all(24),
                  child: SingleChildScrollView(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        const SizedBox(height: 32), 
                        const Text(
                          'Đăng nhập',
                          style: TextStyle(
                            fontSize: 24,
                            fontWeight: FontWeight.w600,
                            color: Color(0xFF1A1A1A),
                          ),
                        ),
                        const SizedBox(height: 4),
                        const Text(
                          'để tiếp tục',
                          style: TextStyle(
                            fontSize: 14,
                            color: Color(0xFF6B7280),
                          ),
                        ),
                        const SizedBox(height: 24),
                        
                        const Text(
                          'Tài khoản',
                          style: TextStyle(
                            fontSize: 14,
                            fontWeight: FontWeight.w500,
                            color: Color(0xFF374151),
                          ),
                        ),
                        const SizedBox(height: 8),
                        TextField(
                          controller: _usernameController,
                          decoration: InputDecoration(
                            contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 12),
                            border: OutlineInputBorder(
                              borderRadius: BorderRadius.circular(6),
                              borderSide: const BorderSide(color: Color(0xFFD1D5DB)),
                            ),
                            focusedBorder: OutlineInputBorder(
                              borderRadius: BorderRadius.circular(6),
                              borderSide: const BorderSide(color: Color(0xFF4CAF50), width: 2),
                            ),
                          ),
                        ),
                        const SizedBox(height: 16),
                        
                        const Text(
                          'Mật khẩu',
                          style: TextStyle(
                            fontSize: 14,
                            fontWeight: FontWeight.w500,
                            color: Color(0xFF374151),
                          ),
                        ),
                        const SizedBox(height: 8),
                        TextField(
                          controller: _passwordController,
                          obscureText: _obscurePassword,
                          decoration: InputDecoration(
                            contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 12),
                            border: OutlineInputBorder(
                              borderRadius: BorderRadius.circular(6),
                              borderSide: const BorderSide(color: Color(0xFFD1D5DB)),
                            ),
                            focusedBorder: OutlineInputBorder(
                              borderRadius: BorderRadius.circular(6),
                              borderSide: const BorderSide(color: Color(0xFF4CAF50), width: 2),
                            ),
                            suffixIcon: IconButton(
                              icon: Icon(
                                _obscurePassword ? Icons.visibility_off : Icons.visibility,
                                color: const Color(0xFF6B7280),
                                size: 20,
                              ),
                              onPressed: () {
                                setState(() {
                                  _obscurePassword = !_obscurePassword;
                                });
                              },
                            ),
                          ),
                        ),
                        const SizedBox(height: 24),
                        
                        ElevatedButton(
                          onPressed: _verifyLogin,
                          style: ElevatedButton.styleFrom(
                            backgroundColor: const Color(0xFF4CAF50),
                            foregroundColor: Colors.white,
                            minimumSize: const Size(double.infinity, 48),
                            shape: RoundedRectangleBorder(
                              borderRadius: BorderRadius.circular(6),
                            ),
                            elevation: 0,
                          ),
                          child: Row(
                            mainAxisAlignment: MainAxisAlignment.center,
                            children: [
                              const Text(
                                'Đăng nhập',
                                style: TextStyle(fontSize: 16),
                              ),
                              const SizedBox(width: 8),
                              Icon(Icons.arrow_forward, size: 16),
                            ],
                          ),
                        ),
                        const SizedBox(height: 16),
                      ],
                    ),
                  ),
                ),
                Positioned(
                  top: 16,
                  left: 16,
                  child: IconButton(
                    icon: const Icon(Icons.arrow_back, color: Color(0xFF4CAF50)),
                    onPressed: () {
                      Navigator.pushReplacement(
                        context,
                        MaterialPageRoute(
                          builder: (context) => const LoginPage(),
                        ),
                      );
                    },
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class DashboardPage extends StatefulWidget {
  final String username;
  final String email;
  
  const DashboardPage({
    super.key, 
    this.username = '',
    this.email = '',
  });

  @override
  _DashboardPageState createState() => _DashboardPageState();
}

class _DashboardPageState extends State<DashboardPage> {
  List<Map<String, String>> _historyData = [];

  @override
  void initState() {
    super.initState();
    currentDashboardState = this;
    _updateHistory();
  }

  void _updateHistoryData(List<Map<String, String>> newData) {
    if (mounted) {
      setState(() {
        _historyData = newData;
      });
    }
  }

  Future<void> _updateHistory() async {
  try {
    final builder = MqttClientPayloadBuilder();
      builder.addString(json.encode({
        "identification": widget.username      
      }));
      
      if (client.connectionStatus?.state == MqttConnectionState.connected) {
        client.publishMessage(
          'feature/acs/trace', 
          MqttQos.atLeastOnce, 
          builder.payload!
        );
        print('Published registration data to MQTT topic');
      } else {
        print('MQTT client not connected');
      }
  } catch (e) {
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text('Lỗi: $e'),
        backgroundColor: Colors.red,
      ),
    );
  }
}

  Future<void> _reportLostCard(BuildContext context) async {
    showDialog(
      context: context,
      barrierDismissible: false,
      builder: (BuildContext context) {
        return const Center(
          child: CircularProgressIndicator(
            valueColor: AlwaysStoppedAnimation<Color>(Color(0xFF4CAF50)),
          ),
        );
      },
    );
    
    try {
      final url = Uri.parse(webappUrl);
      final requestData = {
        "command": "Lost",
        "content1": widget.username,
        "content2": "",
        "content3": "",
        "content4": ""
      };
      
      print('Sending lost card data: $requestData');
      
      final response = await http.post(
        url,
        headers: {
          'Content-Type': 'application/json',
        },
        body: json.encode(requestData),
      );
      
      Navigator.pop(context);
      
      if (response.statusCode == 200 || response.statusCode == 201 || response.statusCode == 302) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content: Text('Đã báo cáo mất thẻ thành công'),
            backgroundColor: Color(0xFF4CAF50),
          ),
        );
      } else {
        throw Exception('Failed to report lost card: ${response.statusCode}');
      }
    } catch (e) {
      Navigator.pop(context);
      
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text('Lỗi: $e'),
          backgroundColor: Colors.red,
        ),
      );
    }
  }

  Future<void> _clearCredentials() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.remove('username');
    await prefs.remove('email');
    await prefs.remove('password');
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFFF4F4F4),
      body: Center(
        child: SafeArea(
          child: Container(
            constraints: const BoxConstraints(maxWidth: 400),
            margin: const EdgeInsets.all(16),
            child: Column(
              children: [
                Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  children: [
                    Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          widget.username,
                          style: const TextStyle(
                            fontSize: 20,
                            fontWeight: FontWeight.bold,
                            color: Color(0xFF1F2937),
                          ),
                        ),
                        Text(
                          widget.email,
                          style: TextStyle(
                            fontSize: 14,
                            color: Color(0xFF6B7280),
                          ),
                        ),
                      ],
                    ),
                    OutlinedButton.icon(
                      onPressed: () async {
                        await _clearCredentials();
                        Navigator.pushReplacement(
                          context,
                          MaterialPageRoute(
                            builder: (context) => const LoginPage(),
                          ),
                        );
                      },
                      icon: const Icon(Icons.logout, size: 16),
                      label: const Text('Đăng xuất'),
                      style: OutlinedButton.styleFrom(
                        foregroundColor: const Color(0xFFFF6060),
                        side: const BorderSide(color: Color(0xFFFF6060)),
                        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 24),
                
                Expanded(
                  child: Column(
                    children: [
                      Expanded(
                        child: Card(
                          shape: RoundedRectangleBorder(
                            borderRadius: BorderRadius.circular(12),
                            side: const BorderSide(color: Color(0xFF60B9FF), width: 2),
                          ),
                          child: Padding(
                            padding: const EdgeInsets.all(20),
                            child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                Row(
                                  children: [
                                    const Icon(
                                      Icons.history,
                                      color: Color(0xFF60B9FF),
                                      size: 24,
                                    ),
                                    const SizedBox(width: 8),
                                    const Text(
                                      'Lịch sử',
                                      style: TextStyle(
                                        fontSize: 20,
                                        fontWeight: FontWeight.bold,
                                        color: Color(0xFF60B9FF),
                                      ),
                                    ),
                                  ],
                                ),
                                const SizedBox(height: 16),
                                Expanded(
                                  child: RefreshIndicator(
                                    onRefresh: _updateHistory,
                                    child: ListView.builder(
                                      itemCount: _historyData.length,
                                      itemBuilder: (context, index) {
                                        final entry = _historyData[index];
                                        final date = entry.keys.first;
                                        final id = entry[date];
                                        return _TransactionItem(
                                          date: date,
                                          id: id ?? '',
                                          isLast: index == _historyData.length - 1,
                                        );
                                      },
                                    ),
                                  ),
                                ),
                              ],
                            ),
                          ),
                        ),
                      ),
                      const SizedBox(height: 16),
                      
                      Row(
                        children: [
                          Expanded(
                            child: InkWell(
                              onTap: () {
                                Navigator.push(
                                  context,
                                  MaterialPageRoute(
                                    builder: (context) => RegistrationFormPage(
                                      username: widget.username,
                                      email: widget.email,
                                    ),
                                  ),
                                );
                              },
                              child: Card(
                                color: const Color(0xFF4CAF50),
                                shape: RoundedRectangleBorder(
                                  borderRadius: BorderRadius.circular(12),
                                ),
                                child: SizedBox(
                                  height: 64,
                                  child: Padding(
                                    padding: const EdgeInsets.all(12),
                                    child: Row(
                                      children: [
                                        const Icon(
                                          Icons.edit_document,
                                          color: Colors.white,
                                          size: 16,
                                        ),
                                        const SizedBox(width: 8),
                                        const Text(
                                          'Cập nhật thông tin',
                                          style: TextStyle(
                                            color: Colors.white,
                                            fontWeight: FontWeight.w500,
                                            fontSize: 12,
                                          ),
                                        ),
                                      ],
                                    ),
                                  ),
                                ),
                              ),
                            ),
                          ),
                          const SizedBox(width: 16),
                          
                          Expanded(
                            child: InkWell(
                              onTap: () => _reportLostCard(context),
                              child: Card(
                                color: const Color(0xFFFF6060),
                                shape: RoundedRectangleBorder(
                                  borderRadius: BorderRadius.circular(12),
                                ),
                                child: SizedBox(
                                  height: 64,
                                  child: Padding(
                                    padding: const EdgeInsets.all(12),
                                    child: Row(
                                      children: [
                                        const Icon(
                                          Icons.warning_amber,
                                          color: Colors.white,
                                          size: 16,
                                        ),
                                        const SizedBox(width: 8),
                                        const Text(
                                          'Báo mất thẻ',
                                          style: TextStyle(
                                            color: Colors.white,
                                            fontWeight: FontWeight.w500,
                                            fontSize: 12,
                                          ),
                                        ),
                                      ],
                                    ),
                                  ),
                                ),
                              ),
                            ),
                          ),
                        ],
                      ),
                    ],
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class RegistrationFormPage extends StatefulWidget {
  final String username;
  final String email;
  
  const RegistrationFormPage({
    super.key,
    required this.username,
    this.email = '',
  });

  @override
  State<RegistrationFormPage> createState() => _RegistrationFormPageState();
}

class _RegistrationFormPageState extends State<RegistrationFormPage> {
  final TextEditingController _fullNameController = TextEditingController();
  final TextEditingController _birthDateController = TextEditingController();
  final TextEditingController _roomController = TextEditingController();
  
  List<File> _imageFiles = [];
  final FirebaseStorage _storage = FirebaseStorage.instance;
  bool _isUploading = false;
  List<String> _uploadedImageUrls = [];
  
String encodeToAscii(String text) {
  return text.runes.map((rune) {
    if (rune > 127) {
      return '\\u${rune.toRadixString(16).padLeft(4, '0')}';
    }
    return String.fromCharCode(rune);
  }).join('');
}

  Future<void> _pickImages() async {
    if (_imageFiles.length >= 5) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Hãy chọn 5 hình ảnh'),
          backgroundColor: Colors.red,
        ),
      );
      return;
    }
    
    final ImagePicker picker = ImagePicker();
    final List<XFile> pickedFiles = await picker.pickMultiImage();
    
    if (pickedFiles.isNotEmpty) {
      final int remainingSlots = 5 - _imageFiles.length;
      final List<XFile> filesToAdd = pickedFiles.length <= remainingSlots 
          ? pickedFiles 
          : pickedFiles.sublist(0, remainingSlots);
      
      if (filesToAdd.length < pickedFiles.length) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('Hãy chọn đúng 5 hình ảnh'),
            backgroundColor: Colors.orange,
          ),
        );
      }
      
      setState(() {
        _imageFiles.addAll(filesToAdd.map((file) => File(file.path)).toList());
      });
    }
  }
  
  void _removeImage(int index) {
    setState(() {
      _imageFiles.removeAt(index);
    });
  }

  Future<void> uploadImages() async {
    if (_imageFiles.isEmpty) {
      print("Không có hình ảnh nào để tải lên");
      return;
    }
    
    setState(() {
      _isUploading = true;
    });
    
    try {
      _uploadedImageUrls = [];
      
      final String folderPath = widget.username;
      
      final String fullName = encodeToAscii(_fullNameController.text);
      final String birthDate = _birthDateController.text;
      final String room = _roomController.text;
      final String username = widget.username;

      final builder = MqttClientPayloadBuilder();
      builder.addString(json.encode({
        "id": username,
        "name": fullName,
        "dob": birthDate,
        "room": room,
        "image": "https://firebasestorage.googleapis.com/v0/b/test-96fdf.appspot.com/o/$username%2Fsample1.jpg?alt=media"
      }));
      
      if (client.connectionStatus?.state == MqttConnectionState.connected) {
        client.publishMessage(
          'database/acs/users', 
          MqttQos.atLeastOnce, 
          builder.payload!
        );
        print('Published registration data to MQTT topic');
      } else {
        print('MQTT client not connected');
      }

      try {
        print("Đang tải lên file information.txt");
        
        final Directory tempDir = await getTemporaryDirectory();
        final File informationFile = File('${tempDir.path}/information.txt');
        final String informationContent = "Database:$fullName\$$birthDate\$$room\$$username";
        await informationFile.writeAsString(informationContent);
        
        final Reference infoRef = _storage.ref().child('$folderPath/information.txt');
        final UploadTask infoUploadTask = infoRef.putFile(
          informationFile,
          SettableMetadata(contentType: 'text/plain'),
        );
        
        await infoUploadTask;
        print("Đã tải lên file information.txt thành công");
      } catch (e) {
        print("Lỗi khi tải lên file information.txt: $e");
      }
      
      for (int i = 0; i < _imageFiles.length; i++) {
        print("Đang tải lên hình ảnh ${i+1}/${_imageFiles.length}");
        
        try {
          final String fileName = '$folderPath/sample${i+1}.jpg';
          final Reference ref = _storage.ref().child(fileName);
          
          final SettableMetadata metadata = SettableMetadata(
            contentType: 'image/jpeg',
          );
          
          final UploadTask uploadTask = ref.putFile(_imageFiles[i], metadata);
          
          uploadTask.snapshotEvents.listen((TaskSnapshot snapshot) {
            final progress = (snapshot.bytesTransferred / snapshot.totalBytes) * 100;
            print('Tiến trình tải lên hình ảnh ${i+1}: ${progress.toStringAsFixed(2)}%');
          });
          
          await uploadTask;
          print("Đã tải lên hình ảnh ${i+1} thành công");
          
          final String downloadUrl = await ref.getDownloadURL();
          _uploadedImageUrls.add(downloadUrl);
        } catch (e) {
          print("Lỗi khi tải lên hình ảnh ${i+1}: $e");
        }
      }

      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text('Đã tải lên thành công'),
          backgroundColor: Color(0xFF4CAF50),
        ),
      );

    } catch (e) {
      print("Lỗi tổng thể khi tải lên: $e");
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text('Lỗi khi tải lên: $e'),
          backgroundColor: Colors.red,
        ),
      );
    } finally {
      setState(() {
        _isUploading = false;
      });
      print("Đã hoàn tất quá trình tải lên");
    }
  }
  @override
  void dispose() {
    if (currentDashboardState == this) {
      currentDashboardState = null;
    }
    _fullNameController.dispose();
    _birthDateController.dispose();
    _roomController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFFF4F4F4),
      body: Center(
        child: Container(
          constraints: const BoxConstraints(maxWidth: 400),
          margin: const EdgeInsets.all(20),
          child: Card(
            elevation: 4,
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(12),
            ),
            child: Padding(
              padding: const EdgeInsets.all(24),
              child: SingleChildScrollView(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const Text(
                      'Đăng ký thông tin',
                      style: TextStyle(
                        fontSize: 24,
                        fontWeight: FontWeight.w600,
                        color: Color(0xFF1A1A1A),
                      ),
                    ),
                    const SizedBox(height: 4),
                    const Text(
                      'vui lòng điền đầy đủ thông tin',
                      style: TextStyle(
                        fontSize: 14,
                        color: Color(0xFF6B7280),
                      ),
                    ),
                    const SizedBox(height: 24),
                    
                    const Text(
                      'Họ và tên',
                      style: TextStyle(
                        fontSize: 14,
                        fontWeight: FontWeight.w500,
                        color: Color(0xFF374151),
                      ),
                    ),
                    const SizedBox(height: 8),
                    TextField(
                      controller: _fullNameController,
                      decoration: InputDecoration(
                        contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 12),
                        border: OutlineInputBorder(
                          borderRadius: BorderRadius.circular(6),
                          borderSide: const BorderSide(color: Color(0xFFD1D5DB)),
                        ),
                        focusedBorder: OutlineInputBorder(
                          borderRadius: BorderRadius.circular(6),
                          borderSide: const BorderSide(color: Color(0xFF4CAF50), width: 2),
                        ),
                      ),
                    ),
                    const SizedBox(height: 16),
                    
                    const Text(
                      'Ngày sinh',
                      style: TextStyle(
                        fontSize: 14,
                        fontWeight: FontWeight.w500,
                        color: Color(0xFF374151),
                      ),
                    ),
                    const SizedBox(height: 8),
                    TextField(
                      controller: _birthDateController,
                      decoration: InputDecoration(
                        contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 12),
                        border: OutlineInputBorder(
                          borderRadius: BorderRadius.circular(6),
                          borderSide: const BorderSide(color: Color(0xFFD1D5DB)),
                        ),
                        focusedBorder: OutlineInputBorder(
                          borderRadius: BorderRadius.circular(6),
                          borderSide: const BorderSide(color: Color(0xFF4CAF50), width: 2),
                        ),
                        suffixIcon: const Icon(Icons.calendar_today, size: 20),
                      ),
                      readOnly: true,
                      onTap: () async {
                        final DateTime? pickedDate = await showDatePicker(
                          context: context,
                          initialDate: DateTime.now(),
                          firstDate: DateTime(1900),
                          lastDate: DateTime.now(),
                          locale: const Locale('vi', 'VN'),
                        );
                        if (pickedDate != null) {
                          setState(() {
                            _birthDateController.text = 
                                "${pickedDate.day.toString().padLeft(2, '0')}/${pickedDate.month.toString().padLeft(2, '0')}/${pickedDate.year}";
                          });
                        }
                      },
                    ),
                    const SizedBox(height: 16),
                    
                    const Text(
                      'Phòng',
                      style: TextStyle(
                        fontSize: 14,
                        fontWeight: FontWeight.w500,
                        color: Color(0xFF374151),
                      ),
                    ),
                    const SizedBox(height: 8),
                    TextField(
                      controller: _roomController,
                      keyboardType: TextInputType.number,
                      decoration: InputDecoration(
                        contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 12),
                        border: OutlineInputBorder(
                          borderRadius: BorderRadius.circular(6),
                          borderSide: const BorderSide(color: Color(0xFFD1D5DB)),
                        ),
                        focusedBorder: OutlineInputBorder(
                          borderRadius: BorderRadius.circular(6),
                          borderSide: const BorderSide(color: Color(0xFF4CAF50), width: 2),
                        ),
                      ),
                    ),
                    const SizedBox(height: 16),
                    
                    const Text(
                      'Hình ảnh',
                      style: TextStyle(
                        fontSize: 14,
                        fontWeight: FontWeight.w500,
                        color: Color(0xFF374151),
                      ),
                    ),
                    const SizedBox(height: 8),
                    if (_imageFiles.isNotEmpty)
                      Column(
                        children: [
                          GridView.builder(
                            shrinkWrap: true,
                            physics: const NeverScrollableScrollPhysics(),
                            gridDelegate: const SliverGridDelegateWithFixedCrossAxisCount(
                              crossAxisCount: 3,
                              crossAxisSpacing: 8,
                              mainAxisSpacing: 8,
                              childAspectRatio: 1,
                            ),
                            itemCount: _imageFiles.length,
                            itemBuilder: (context, index) {
                              return Stack(
                                children: [
                                  Container(
                                    decoration: BoxDecoration(
                                      borderRadius: BorderRadius.circular(6),
                                      image: DecorationImage(
                                        image: FileImage(_imageFiles[index]),
                                        fit: BoxFit.cover,
                                      ),
                                    ),
                                  ),
                                  Positioned(
                                    top: 0,
                                    right: 0,
                                    child: GestureDetector(
                                      onTap: () => _removeImage(index),
                                      child: Container(
                                        padding: const EdgeInsets.all(4),
                                        decoration: const BoxDecoration(
                                          color: Color(0xFFFF6060),
                                          shape: BoxShape.circle,
                                        ),
                                        child: const Icon(
                                          Icons.close,
                                          color: Colors.white,
                                          size: 12,
                                        ),
                                      ),
                                    ),
                                  ),
                                ],
                              );
                            },
                          ),
                          const SizedBox(height: 8),
                        ],
                      ),
                    InkWell(
                      onTap: _pickImages,
                      child: DottedBorder(
                        borderType: BorderType.RRect,
                        radius: const Radius.circular(6),
                        color: const Color(0xFFD1D5DB),
                        strokeWidth: 2,
                        dashPattern: const [6, 3],
                        child: Container(
                          width: double.infinity,
                          padding: const EdgeInsets.symmetric(vertical: 24),
                          child: Column(
                            children: [
                              const Icon(
                                Icons.upload_file,
                                size: 40,
                                color: Color(0xFF9CA3AF),
                              ),
                              const SizedBox(height: 8),
                              const Text(
                                'Nhấn để tải lên hình ảnh',
                                style: TextStyle(
                                  fontSize: 14,
                                  color: Color(0xFF6B7280),
                                ),
                              ),
                            ],
                          ),
                        ),
                      ),
                    ),
                    const SizedBox(height: 24),
                    
                    ElevatedButton(
                      onPressed: _isUploading 
                        ? null 
                        : () async {
                            await uploadImages();
                          
                            Navigator.pop(context);
                          },
                      style: ElevatedButton.styleFrom(
                        backgroundColor: const Color(0xFF4CAF50),
                        foregroundColor: Colors.white,
                        minimumSize: const Size(double.infinity, 48),
                        shape: RoundedRectangleBorder(
                          borderRadius: BorderRadius.circular(6),
                        ),
                        elevation: 0,
                        disabledBackgroundColor: Colors.grey,
                      ),
                      child: _isUploading
                        ? const Row(
                            mainAxisAlignment: MainAxisAlignment.center,
                            children: [
                              SizedBox(
                                width: 20,
                                height: 20,
                                child: CircularProgressIndicator(
                                  color: Colors.white,
                                  strokeWidth: 2,
                                ),
                              ),
                              SizedBox(width: 12),
                              Text('Đang tải lên...', style: TextStyle(fontSize: 16)),
                            ],
                          )
                        : Row(
                            mainAxisAlignment: MainAxisAlignment.center,
                            children: [
                              const Text(
                                'Đăng ký',
                                style: TextStyle(fontSize: 16),
                              ),
                              const SizedBox(width: 8),
                              Icon(Icons.arrow_forward, size: 16),
                            ],
                          ),
                    ),
                  ],
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}
class _TransactionItem extends StatelessWidget {
  final String date;
  final String id;
  final bool isLast;

  const _TransactionItem({
    required this.date,
    required this.id,
    this.isLast = false,
  });

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        Padding(
          padding: const EdgeInsets.symmetric(vertical: 8),
          child: Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Text(
                date,
                style: const TextStyle(
                  fontSize: 12,
                  color: Color(0xFF6B7280),
                ),
              ),
              Text(
                id,
                style: const TextStyle(
                  fontSize: 12,
                  fontWeight: FontWeight.w500,
                  color: Color(0xFF374151),
                ),
              ),
            ],
          ),
        ),
        if (!isLast)
          Container(
            height: 1,
            color: const Color(0xFF60B9FF).withOpacity(0.2),
          ),
      ],
    );
  }
}

