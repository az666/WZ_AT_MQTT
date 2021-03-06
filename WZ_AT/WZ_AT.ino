//***********************
//名称：基于MQTT的万能AT指令（名称来源于本人名字故取名为WZ指令）
//版本：V2.0
//特点：简单到只需要一行代码就可以将数据点推送至MQTT服务器
//可用于个人EMQ服务器，或者树莓派搭建的局域网服务器（暂不支持onenet等商用平台）
//网站：http://wenzheng.club
//Github:https://github.com/az666
//***********************
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <Wire.h>
#include <EEPROM.h>
#include <PubSubClient.h>
#define server_topic "wz_server"  //默认订阅的主题
WiFiClient espClient;
PubSubClient client(espClient);
struct CONFIG {  //结构体存放账号密码主题和消息
  char ssid[32];
  char password[32];
  char server[32];
  char topic[32];
  char message[200];
};
String baidu_url = "http://quan.suning.com/getSysTime.do";   //检测网络异常接口
String payload = "";
String inputString = "";
String mqtt_user;
String mqtt_password;
boolean stringComplete = false;
long lastMsg = 0;
char msg[50];
int value = 0;
char  c[] = "";
int server_flag = 0;
/*********************************智能配网**************************************/
void smartConfig()
{
  WiFi.mode(WIFI_STA);
  WiFi.beginSmartConfig();
  while (1)
  {
    Serial.print(".");
    digitalWrite(2, 0);
    delay(200);
    digitalWrite(2, 1);
    delay(200);
    if (WiFi.smartConfigDone())
    {
      EEPROM.begin(512);
      CONFIG buf;
      Serial.println("SmartConfig Success");
      Serial.printf("SSID:%s\r\n", WiFi.SSID().c_str());
      Serial.printf("PSW:%s\r\n", WiFi.psk().c_str());
      strcpy(buf.ssid,  WiFi.SSID().c_str());
      strcpy(buf.password, WiFi.psk().c_str());
      EEPROM.put<CONFIG>(0, buf);
      EEPROM.commit();
      Serial.println(buf.ssid);
      Serial.println(buf.password);
      break;
    }
  }
}
void setup() {
  Serial.begin(9600);
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);
  setup_wifi();
  delay(1000);
  Serial.println("OK");
  Serial.println("please connect the server!");
  inputString.reserve(200);
}
/*********************************测试网络**************************************/
int get_network()
{
  HTTPClient http;
  http.begin(baidu_url);
  int httpCode = http.GET();
  if (httpCode > 0) {
    digitalWrite(2, 0);
    delay(100);
    digitalWrite(2, 1);
    delay(100);
    return 1;
  }
  else return 0;
  http.end();
}
/*********************************处理消息**************************************/
void callback(char* topic, byte* payload, unsigned int length)  //接收消息
{
  Serial.print("WZ:[");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println("]");
}
String macToStr(const uint8_t* mac)
{
  String result;
  for (int i = 0; i < 6; ++i) {
    result += String(mac[i], 16);
    if (i < 5)
      result += ':';
  }
  return result;
}
/*********************************连接wifi**************************************/
void setup_wifi() {
  EEPROM.begin(512);
  CONFIG buf;
  EEPROM.get<CONFIG>(0, buf);
  Serial.println(buf.ssid);
  Serial.println(buf.password);
  EEPROM.commit();
  WiFi.begin(buf.ssid, buf.password);
  long lastMsg = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    long now = millis();
    if (now - lastMsg > 10000) {
      smartConfig();  //微信智能配网
      break;
    }
  }
}
/*********************************服务器连接**************************************/
void reconnect() {
  while (!client.connected()) {
    String clientName;
    clientName += "esp8266-";
    uint8_t mac[6];
    WiFi.macAddress(mac);
    clientName += macToStr(mac);
    clientName += "-";
    clientName += String(micros() & 0xff, 16);
    if  (client.connect((char*) clientName.c_str(),(char*)mqtt_user.c_str(), (char*)mqtt_password.c_str()))  {   //ID与账号和密码
      Serial.println("connected");
      client.subscribe(server_topic);//订阅默认主题
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());  //设备重连
      Serial.println(" try again in 5 seconds");
    }
  }
}
/*********************************串口事件**************************************/
void serialEvent() {
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    inputString += inChar;
    if (inChar == '\n') {
      stringComplete = true;
    }
  }
}
void loop() {
  if (server_flag) {
    if (!client.connected()) {  //服务器掉线重连
      reconnect();
    }
    client.loop();
  }
  ////////////////////////下面开始处理WZ指令/////////////////
  serialEvent();
  if (stringComplete) {
    Serial.println(inputString);
    if (inputString.indexOf("wz")) {
      /************************************json 数据处理********************************************/
      DynamicJsonBuffer jsonBuffer;
      String  input =   inputString;
      JsonObject & root = jsonBuffer.parseObject(input);
      String output =  root[String("wz")];
      if (output == "wenzheng.club") {
        if (!server_flag) {
          CONFIG buf;
          String output =  root[String("server")];
          Serial.printf("Server:%s\r\n", output.c_str());
          strcpy(buf.server, output.c_str());
          String output_user =  root[String("user")];
          mqtt_user = output_user;
          String output_password =  root[String("password")];
          mqtt_password = output_password;
          Serial.println("OK");
          delay(100);
          client.setServer(buf.server,1883);
          client.setCallback(callback); 
          server_flag = 1;
        }
        String output_topic =  root[String("topic")];
        if (output_topic!="")
        {
          CONFIG buf;
          String output_message =  root[String("message")];
           Serial.printf("topic:%s\r\n", output_topic.c_str());
           Serial.printf("message:%s\r\n", output_message.c_str());
          strcpy(buf.topic, output_topic.c_str());
          strcpy(buf.message, output_message.c_str());
          client.publish(buf.topic, buf.message, true); //数据发送至服务器   
          }
      }
    }
    //清除标志位
    inputString = "";
    stringComplete = false;
  }
  long now = millis();  //每两秒检测一下网络状态，联网异常则重连wifi
  if (now - lastMsg > 2000) {
    lastMsg = now;
    if (!get_network())  //wifi断网重连
  {
    setup_wifi();
  }
  }

}

