#include "CommProtocol.h"
#include "configs.h"
#include <Arduino.h>

#include <EEPROM.h>

#ifdef SERVER
CtrlPacket packet;
#else
#include "SensorDriver.h"
#include <MotorDriver.h>
#endif


unsigned long last_publish_time = 0;
unsigned long last_summary_time = 0;
unsigned long last_servo_time = 0;
unsigned long last_addr_time = 0;


class Core{
public:
#ifdef SERVER
  static CommServer comm;
#else
  static CommClient comm;
#endif

#ifndef SERVER
  static Sensors sensor;
  static ServoMotorDriver x_servo;
  static ServoMotorDriver y_servo;
  static ESCMotorDriver esc_p1;
  static ESCMotorDriver esc_p2;
#endif

  Core();

  static void init();

  static void Wifi_connection_setup();

  static int get_agent_id();

private:
  static uint8_t _agent_id;

  static TaskHandle_t websocket_task_handle;
  static void websocket_loop(void *parameter) {
    while (true) {
      comm.update();
      // To allow other threads have chances to join
      delay(1);
    }
  }

#ifndef SERVER
  static TaskHandle_t state_feedback_handle;
  static void state_feedback(void *parameter) {
    while (true) {
      if (millis() - last_summary_time >= 500) {
        const StatePacket *s_packet_ptr = sensor.state_packet_gen();
        if (comm.send(CommProtocol::PACKET_TYPE::STATE_AGN, s_packet_ptr))
          log_i("State feedback sent!");
        last_summary_time = millis();
      }
      delay(10);
    }
  }
#endif
};

#ifdef SERVER
CommServer Core::comm(COMM_PORT);
#else
CommClient Core::comm(COMM_PORT, server_IP);

ServoMotorDriver Core::x_servo(ServoMotorConfigs(MotorConfigs(18, 1100, 1900, 1500, 180, 4096), 0));
ServoMotorDriver Core::y_servo(ServoMotorConfigs(MotorConfigs(19, 1100, 1900, 1500, 180, 4096), 0));
ESCMotorDriver Core::esc_p1(ESCMotorConfigs(MotorConfigs(32, 1000, 2000, 1500, 100, 4096), 95));
ESCMotorDriver Core::esc_p2(ESCMotorConfigs(MotorConfigs(33, 1000, 2000, 1500, 100, 4096), 95));
#endif 


Core::Core(){}

void Core::init(){
  get_agent_id();

  Wifi_connection_setup();

#ifndef SERVER
  if (_agent_id == 2)
    sensor.init(14, 15);
  else
    sensor.init();

  x_servo.init();
  y_servo.init();
  esc_p1.init();
  esc_p2.init();
#endif
  
  comm.init();

  //TODO: check stack usages
  xTaskCreatePinnedToCore(websocket_loop, /* Function to implement the task */
                          "websocket_updating",   /* Name of the task */
                          15000,                  /* Stack size in words */
                          NULL,                   /* Task input parameter */
                          1,                      /* Priority of the task */
                          &websocket_task_handle, /* Task handle. */
                          1); /* Core where the task should run */
  
#ifndef SERVER
  xTaskCreatePinnedToCore(state_feedback,   /* Function to implement the task
                                             */
                          "state_feedback", /* Name of the task */
                          5000,             /* Stack size in words */
                          NULL,             /* Task input parameter */
                          10,               /* Priority of the task */
                          &state_feedback_handle, /* Task handle. */
                          1); /* Core where the task should run */
#endif
}

void Core::Wifi_connection_setup() {
  const IPAddress local_IP(gateway[0], gateway[1], gateway[2], _agent_id + 1);
#ifdef SERVER
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(server_IP, gateway, subnet);
  const int maximum_connection = 4;
  WiFi.softAP(WIFI_SSID, WIFI_PSWD, 1, false, maximum_connection);
  log_i("AP started with IP address: %s\n", WiFi.softAPIP().toString());
#else
  log_i("Connecting to %s\n", (String)WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.config(local_IP, gateway, subnet);
  WiFi.begin(WIFI_SSID, WIFI_PSWD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  log_i("\nWiFi connected with IP address:  %s\n", WiFi.softAPIP().toString());
#endif
}

int Core::get_agent_id(){
  static bool agent_id_init = false;
  if (!agent_id_init){
    // not read from memory yet
    EEPROM.begin(EEPROM_SIZE);
#ifdef WRITE_AGENT_ID
    EEPROM.write(EEPROM_AGENT_ID_ADDR, AGENT_ID);
    EEPROM.commit();
#endif
    _agent_id = EEPROM.read(EEPROM_AGENT_ID_ADDR);
    log_i("Agent id: %d, \n", _agent_id);
  }
  return _agent_id;
}

#ifdef SERVER
#include "esp_wifi.h"
void display_connected_devices() {
  wifi_sta_list_t wifi_sta_list;
  tcpip_adapter_sta_list_t adapter_sta_list;
  esp_wifi_ap_get_sta_list(&wifi_sta_list);
  tcpip_adapter_get_sta_list(&wifi_sta_list, &adapter_sta_list);

  if (adapter_sta_list.num > 0)
    Serial.println("-----------");
  for (uint8_t i = 0; i < adapter_sta_list.num; i++) {
    tcpip_adapter_sta_info_t station = adapter_sta_list.sta[i];
    Serial.print((String) "[+] Device " + i + " | MAC : ");
    Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X", station.mac[0],
                  station.mac[1], station.mac[2], station.mac[3],
                  station.mac[4], station.mac[5]);
    // Serial.println((String) " | IP " + ip4addr_ntoa(&(station.ip)));

    Serial.printf("   IP: %s\n", IPAddress(station.ip.addr).toString());
  }
}
#endif


void setup() {
  Serial.begin(115200);

#ifdef SERVER
  packet.time = 0;
  packet.id = 0;
#else
  Core::x_servo.set_armed(true);
  Core::y_servo.set_armed(true);
#endif
}

void loop() {

#ifdef SERVER
  if (millis() - last_addr_time >= 500) {
    display_connected_devices();
    last_addr_time = millis();
  }

  if (millis() - last_publish_time >= 20) {
    packet.id += 1;
    packet.time = micros();

    comm.send(CommProtocol::PACKET_TYPE::CTRL_CMDS, &packet);

    last_publish_time = millis();
    // display_connected_devices();
  }

  if (millis() - last_summary_time >= 5000) {
    Serial.printf("fps: %f, latency: %f, packet_lost: %d\n", comm.ben.get_fps(),
                  comm.ben.get_latency(), comm.ben.packet_lost());
    last_summary_time = millis();
  }
#else
#define SERVO_TEST
#ifdef SERVO_TEST
  if (millis() - last_servo_time >= 10) {
    float t = millis() / 1000.0;
    Core::x_servo.write(60.0f * sin(t));
    Core::y_servo.write(60.0f * cos(t));
    last_servo_time = millis();
  }
#endif
#endif
};

#ifdef UDP_PING_PONG
#include <Arduino.h>
#include <AsyncUDP.h>
#include <WiFi.h>

#include "benchmark.h"
#define SERVER

const char *ssid = "esp_udp";
const char *password = "esp_ros_pingpong";
const uint16_t udp_port = 11411;
AsyncUDP udp;
Benchmark ben(100);

typedef struct PACKET {
  uint32_t time;
  int32_t id;
  char data[400];
} Packet;
Packet t_packet;
Packet ping_packet;
Packet pong_packet;

#ifdef SERVER
void startWiFiAP() {
  WiFi.softAP(ssid, password);
  Serial.println("AP started");
  Serial.println("IP address: " + WiFi.softAPIP().toString());
}
#else
void startWiFiClient() {
  Serial.println("Connecting to " + (String)ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");

  Serial.println("WiFi connected");
  Serial.println("IP address: " + WiFi.localIP().toString());
}

#endif

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println();

  t_packet.time = micros();
  t_packet.id = 0;

#ifdef SERVER
  // Or start the ESP as AP
  startWiFiAP();
  Serial.println("Starting UDP server");
#else
  // Connect to a WiFi network
  startWiFiClient();
#endif

#ifdef SERVER
  // mqtt.subscribe("esp_comm/pong", [](const char * topic, const char *
  // payload) {
  //     // payload might be binary, but PicoMQTT guarantees that it's
  //     zero-terminated Packet pong_packet; memcpy((void*)&pong_packet,
  //     payload, sizeof(pong_packet)); Serial.printf("Received pong in '%s':
  //     id=%ld, time=%ld, at %ld\n", topic, pong_packet.id, pong_packet.time,
  //     micros());
  // });

  if (udp.listen(udp_port)) {
    Serial.print("UDP Listening on IP: ");
    Serial.println(WiFi.localIP());
    udp.onPacket([](AsyncUDPPacket packet) {
      // Serial.print("UDP Packet Type: ");
      // Serial.print(packet.isBroadcast()?"Broadcast":packet.isMulticast()?"Multicast":"Unicast");
      // Serial.print(", From: ");
      // Serial.print(packet.remoteIP());
      // Serial.print(":");
      // Serial.print(packet.remotePort());
      // Serial.print(", To: ");
      // Serial.print(packet.localIP());
      // Serial.print(":");
      // Serial.print(packet.localPort());
      // Serial.print(", Length: ");
      // Serial.print(packet.length());
      // Serial.print(", Data: ");
      // Serial.write(packet.data(), packet.length());
      // Serial.println();

      memcpy((void *)&pong_packet, packet.data(), packet.length());
      // Serial.printf("Received pong from '%s': id=%ld, time=%ld, at %ld\n",
      //     packet.remoteIP().toString(), pong_packet.id, pong_packet.time,
      //     micros());
      ben.feed_data(pong_packet.id, pong_packet.time, micros());
      // reply to the client
      // packet.printf("Got %u bytes of data", packet.length());
    });
  }

#else
  //   mqtt.subscribe("esp_comm/ping", [](const char * topic, const char *
  //   payload) {
  //     // payload might be binary, but PicoMQTT guarantees that it's
  //     zero-terminated memcpy((void*)&packet, payload, sizeof(packet));
  //     Serial.printf("Received ping in '%s': id=%ld, time=%ld\n", topic,
  //     packet.id, packet.time);

  //     // Pong
  //     mqtt.publish(topic_pong, (char*)&packet, sizeof(packet), 0, false, 0);
  //   });
  // if(udp.connect(IPAddress(0,0,0,0), udp_port)) {
  // if(udp.connect(IPAddress(192,168,4,1), udp_port)) {
  if (udp.listen(udp_port)) {
    Serial.println("UDP connected");
    udp.onPacket([](AsyncUDPPacket packet) {
      // Serial.print("UDP Packet Type: ");
      // Serial.print(packet.isBroadcast()?"Broadcast":packet.isMulticast()?"Multicast":"Unicast");
      // Serial.print(", From: ");
      // Serial.print(packet.remoteIP());
      // Serial.print(":");
      // Serial.print(packet.remotePort());
      // Serial.print(", To: ");
      // Serial.print(packet.localIP());
      // Serial.print(":");
      // Serial.print(packet.localPort());
      // Serial.print(", Length: ");
      // Serial.print(packet.length());
      // Serial.print(", Data: ");
      // Serial.write(packet.data(), packet.length());
      // Serial.println();
      // //reply to the client
      // packet.printf("Got %u bytes of data", packet.length());
      memcpy((void *)&ping_packet, packet.data(), packet.length());
      Serial.printf("Received ping from '%s': id=%ld, time=%ld\n",
                    packet.remoteIP().toString(), ping_packet.id,
                    ping_packet.time);
      udp.broadcastTo((uint8_t *)&ping_packet, sizeof(ping_packet), udp_port);
    });
    // Send unicast
    t_packet.id = 1234;
    t_packet.time = micros();
    udp.broadcastTo((uint8_t *)&t_packet, sizeof(t_packet), udp_port);
  }
#endif
}

unsigned long last_publish_time = 0;
unsigned long last_summary_time = 0;
int greeting_number = 1;

void loop() {
// Send broadcast
#ifdef SERVER
  if (millis() - last_publish_time >= 20) {
    // We're publishing to a topic, which we're subscribed too, but these
    // message will *not* be delivered locally. String topic = "picomqtt/esp-" +
    // WiFi.macAddress();

    t_packet.id += 1;
    t_packet.time = micros();
    // mqtt.publish(topic_ping, (char*)&packet, sizeof(packet), 0, false, 0);
    udp.broadcastTo((uint8_t *)&t_packet, sizeof(t_packet), udp_port);
    // Serial.printf("Publishing ping from %s: id=%ld, time=%ld, at %ld\n",
    // WiFi.localIP().toString(), t_packet.id, t_packet.time, micros());
    last_publish_time = millis();
  }

  if (millis() - last_summary_time >= 5000) {
    Serial.printf("fps: %f, latency: %f, packet_lost: %d\n", ben.get_fps(),
                  ben.get_latency(), ben.packet_lost());
    last_summary_time = millis();
  }
#endif
}

#endif