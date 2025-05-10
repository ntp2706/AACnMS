// This is a basic Flutter widget test.
//
// To perform an interaction with a widget in your test, use the WidgetTester
// utility in the flutter_test package. For example, you can send tap and scroll
// gestures. You can also use WidgetTester to find child widgets in the widget
// tree, read text, and verify that the values of widget properties are correct.

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_web_plugins/flutter_web_plugins.dart';

import 'package:ms/main.dart';

void main() {
  setUpAll(() {
    // Cấu hình để test có thể chạy trên web
    setUrlStrategy(PathUrlStrategy());
  });

  group('Login App Tests', () {
    testWidgets('LoginPage shows correct initial UI', (WidgetTester tester) async {
      // Build ứng dụng và kích hoạt một frame
      await tester.pumpWidget(MyApp(
        initialRoute: '/login',
        email: '',
        username: '',
        password: '',
      ));

      // Verify các widget cơ bản có mặt trên màn hình
      expect(find.text('Xin chào'), findsOneWidget);
      expect(find.text('Đăng nhập để tiếp tục'), findsOneWidget);
      expect(find.widgetWithText(TextField, 'Nhập email của bạn'), findsOneWidget);
      expect(find.widgetWithText(ElevatedButton, 'Xác nhận email'), findsOneWidget);
    });

    testWidgets('Can navigate to LoginFormPage', (WidgetTester tester) async {
      await tester.pumpWidget(MyApp(
        initialRoute: '/login',
        email: '',
        username: '',
        password: '',
      ));

      // Nhập email
      await tester.enterText(
        find.byType(TextField),
        'test@example.com'
      );

      // Tap nút xác nhận email
      await tester.tap(find.widgetWithText(ElevatedButton, 'Xác nhận email'));
      await tester.pumpAndSettle();

      // Verify đã chuyển sang màn LoginFormPage
      expect(find.text('Đăng nhập'), findsOneWidget);
      expect(find.byIcon(Icons.arrow_back), findsOneWidget);
    });
  });
}
