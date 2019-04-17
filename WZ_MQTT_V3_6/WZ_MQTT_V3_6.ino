//***********************
//名称：基于MQTT的万能AT指令（名称来源于本人名字故取名为WZ指令）
//版本：V3.4
// 2019年4月18日更新sub_topic字段 用于用户自定义Topic
//更新：2019年4月11日修复onenet大于3个数据点内存溢出问题
//       修复数据反馈异常，清除多余打印数据。
//特点：简单到只需要一行代码就可以将数据点推送至MQTT服务器
//可用于个人EMQ服务器，或者树莓派搭建的局域网服务器，已支持onenet等商用平台
//网站：http://wenzheng.club
//Github:https://github.com/az666
//***********************
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <Wire.h>
#include <EEPROM.h>
#include <PubSubClient.h>
//#define server_topic "wz_server"  //默认订阅的主题（已修复）
WiFiClient espClient;
PubSubClient client(espClient);
struct CONFIG {  //结构体存放账号密码主题和消息
  char ssid[32];
  char password[32];
  char server[32];
  char topic[32];
  char sub_topic[32];
  char message[200];
  char onenet_topic_msg[200];
};
String baidu_url = "http://quan.suning.com/getSysTime.do";   //检测网络异常接口
String payload = "";
String inputString = "";
String mqtt_user,sub_topic;
int port;
String mqtt_password;
String mqtt_id;
boolean stringComplete = false;
long lastMsg = 0;
char msg[50];
char msg_buf[200];
int value = 0;
char  c[] = "";
int server_flag = 0;
char dataTemplete[] = "{\"wendu\":\"12\",\"shidu\":\"45\"}";
char msgJson[100];  //存储json数据
char debug_buf[200];  //打印调试数据
int i;
unsigned short json_len = 0;
/*********************************智能配网**************************************/
void smartConfig()
{
  Serial.println("SmartConfig begin");
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
  Serial.print("wifi_OK\r\n");
  Serial.print("please connect the server!\r\n");
  delay(2000);
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
  Serial.print("]");
  Serial.print("\r\n");
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
    CONFIG buf;
    String clientName;
    clientName += "esp8266-";
    uint8_t mac[6];
    WiFi.macAddress(mac);
    clientName += macToStr(mac);
    clientName += "-";
    clientName += String(micros() & 0xff, 16);
    if (mqtt_id == "") mqtt_id = clientName; //判断是否要添加ID
    if  (client.connect((char*)mqtt_id.c_str(), (char*)mqtt_user.c_str(), (char*)mqtt_password.c_str()))  {  //ID与账号和密码
      Serial.println("connected_OK");
      client.subscribe(sub_topic.c_str(),1);//为保证主题不受冲突 (const char*)
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());  //设备重连
      Serial.println(" try_to_connected");
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
          //"strcpy"是和结构体搭配的语法 。
          //strcpy是一种C语言的标准库函数，strcpy把含有'\0'结束符的字符串复制到另一个地址空间，返回值的类型为char*。
          strcpy(buf.server, output.c_str());//服务器域名
          String output_port =  root[String("port")];
          if (output_port == "") output_port = "1883"; //判断是否启用其他端口
          port = output_port.toInt();
          Serial.println(port);
          String output_id =  root[String("id")];
          mqtt_id = output_id; //设备ID
          String output_user =  root[String("user")];
          mqtt_user = output_user; //设备用户名
          String output_password =  root[String("password")];
          mqtt_password = output_password; //设备密码
          String mqtt_sub_topic =  root[String("sub_topic")];
          if(mqtt_sub_topic !="") sub_topic =  mqtt_sub_topic;//订阅的主题
          else   sub_topic =output;//订阅的主题
          Serial.println("setServer_OK");
          delay(100);
          client.setServer(buf.server, port);
          client.setCallback(callback);
          server_flag = 1;
        }
        String output_topic =  root[String("topic")];
        CONFIG buf;
        if ((output_topic != "") || (buf.onenet_topic_msg != ""))
        {
          CONFIG buf;
          String output_message =  root[String("message")];
          //Serial.printf("topic:%s\r\n", output_topic.c_str());
          //Serial.printf("message:%s\r\n", output_message.c_str());
          strcpy(buf.topic, output_topic.c_str());
          strcpy(buf.message, output_message.c_str());
          String output_onenet =  root[String("topic&msg")];
          strcpy(buf.onenet_topic_msg, output_onenet.c_str());//onenet专属协议
          //下面进行onenet封包
          if (port == 6002) { //如果连接的是onenet服务器
            snprintf(msgJson, 100, buf.onenet_topic_msg); //数据包最长少于100字节
            json_len = strlen(msgJson); //packet length count the end char '\0'
            msg_buf[0] = char(0x03); //palyLoad packet byte 1, one_net mqtt Publish packet payload byte 1, type3 , json type2
            msg_buf[1] = char(json_len >> 8); //high 8 bits of json_len (16bits as short int type)
            msg_buf[2] = char(json_len & 0xff); //low 8 bits of json_len (16bits as short int type)
            memcpy(msg_buf + 3, msgJson, strlen(msgJson));
            msg_buf[3 + strlen(msgJson)] = 0;
            // Serial.print("Publish message: ");
            // Serial.println(msgJson);
            client.publish("$dp", (uint8_t*)msg_buf, strlen(msgJson) + 3, false);   //入口参数类型和自己的PubSubClient库有关
            Serial.print("publish_OK");
            Serial.print("\r\n");
          }
          else {  //如果是其他服务器（百度云服务器待做）
            client.publish(buf.topic, buf.message, true); //数据发送至服务器
            Serial.print("publish_OK");
            Serial.print("\r\n");
          }
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
    while (!get_network())  //wifi断网重连
    {
      setup_wifi();
    }
  }

}
