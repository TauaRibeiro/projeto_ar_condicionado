#include <WiFi.h>
#include <WiFiClient.h>
#include "DHT.h"
#include <PubSubClient.h>
#include <time.h>

const int pinLDR {15};
const int pinPIR {19};
const int configLed {2};

#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

const char* ssid = "<NOME_REDE>";
const char* senha = "<SENHA_REDE>";
IPAddress localIP(192, 168, 2, 4);
IPAddress gateway(192, 168, 2, 1);
IPAddress subnet(255, 255, 255, 0);

WiFiClient espClient;
PubSubClient client(espClient);
const char* id = "sdfjklhsowe890r23q9uh";
const char* host = "broker.mqtt.cool";
const int numPorta{ 1883 };

const char* topicoEnvioTemperatura = "temperatura-ar";
const char* topicoEnvioPresenca = "presenca-local";
const char* topicoEnvioLuminosidade = "luminosidade-local";
const char* topicoRecebimento = "controle-ar";

const long intervaloEnvioDHT = 5 * 1000;
const long intervaloEnvioPIR = 30 * 1000;
const long intervaloEnvioLDR = 30 * 1000;

const long intervaloUpdatePIR = 60 * 1000;
const long intervaloUpdateLDR = 60 * 1000; 

long ultimoEnvioPIR {0};
long ultimoEnvioLDR {0};
long ultimoEnvioDHT {0};
long ultimaDeteccaoLDR {0};
long ultimaDeteccaoPIR {};

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -3 * 3600;
const long dayLightOffset_sec = 0;

bool estadoPIR = false;
bool estadoLDR = false;

void setupWiFi() {
  WiFi.config(localIP, gateway, subnet);
  WiFi.begin(ssid, senha);
}

void conectarWiFi(){
  Serial.println("Conectando ao WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print('.');
  }

  Serial.println("");
  Serial.println("Conectado com sucesso!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void connectMQTT() {
  Serial.println("Conectando ao servidor MQTT");

  if(!WiFi.isConnected()){
    conectarWiFi();
  }
  
  while(!client.connected()) {
    if(client.connect(id)) {
      Serial.println();
      Serial.println("Conectado ao servidor MQTT");
      MQTTSubscribe();
    }else{
      Serial.print("Falha ao conectar: ");
      Serial.println(client.state());
    }

    delay(1000);
  }
}

void callback(String topico, byte* payload, unsigned int length) {
  Serial.println("Mensagem recebida: " + topico);
}

void MQTTSubscribe(){
  client.subscribe(topicoRecebimento);
}

void setupMQTT() {
  client.setServer(host, numPorta);
  client.setCallback(callback);
}


void setup() {
  Serial.begin(115200);
  delay(2000);

  setupWiFi();
  setupMQTT();
  configTime(gmtOffset_sec, dayLightOffset_sec, ntpServer);
  conectarWiFi();
  pinMode(pinLDR, INPUT);
  pinMode(pinPIR, INPUT);
  pinMode(configLed, OUTPUT);

  dht.begin();
}


String pegarHorario() {
  struct tm infoTempo;

  if(!getLocalTime(&infoTempo)){
    Serial.println("Erro ao pegar o horário atual!");
    return "Erro";
  }

  char buffer[30];

  strftime(buffer, sizeof(buffer), "%d/%m/%y - %H:%M:%S", &infoTempo);

  return String(buffer);

}

void loop() {
  if( !client.connected()) {
    digitalWrite(configLed, LOW);
    connectMQTT();
    digitalWrite(configLed, HIGH);
  }

  client.loop();


  long tempoAtual = millis();
  int leituraPIR = digitalRead(pinPIR);
  int leituraLDR = digitalRead(pinLDR);

  if(leituraPIR){
    estadoPIR = true;
    ultimaDeteccaoPIR = millis();
  }else if(tempoAtual - ultimaDeteccaoPIR >= intervaloUpdatePIR) {
    estadoPIR = false;
  }

  if(!leituraLDR) {
    estadoLDR = true;
    ultimaDeteccaoLDR = millis();
  }else if(tempoAtual - ultimaDeteccaoLDR >= intervaloUpdateLDR) {
    estadoLDR = false;
  }

  if(tempoAtual - ultimoEnvioDHT >= intervaloEnvioDHT) {
    long temperatura = dht.readTemperature();
    long humidade = dht.readHumidity();

    String envio =  String("{\"dispositivo\":\"sensorDHT\",") +
    String("\"temperatura\":") + String(temperatura) +
    String(",\"humidade\":") + String(humidade) +
    String(",\"timestamp\":\"") + pegarHorario() +
    String("\"}");

    client.publish(topicoEnvioTemperatura, envio.c_str()); 
    ultimoEnvioDHT = millis();   
  }

  if(tempoAtual - ultimoEnvioPIR >= intervaloEnvioPIR) {
    String envio = String("{\"dispositivo\":\"sensorPIR\",") +
    String("\"presenca\":") + String(estadoPIR) + String(",") +
    String("\"timestamp\":") + pegarHorario() + String("}");

    client.publish(topicoEnvioPresenca, envio.c_str());
    ultimoEnvioPIR = millis();
  }

  if(tempoAtual - ultimoEnvioLDR >= intervaloEnvioLDR) {
    String envio = String("{\"dispositivo\":\"sensorLDR\",") +
    String("\"luminosidade\":") + String(estadoLDR) + String(",") +
    String("\"timestamp\":") + pegarHorario() + String("}");

    client.publish(topicoEnvioLuminosidade, envio.c_str());
    ultimoEnvioLDR = millis();
  }
}