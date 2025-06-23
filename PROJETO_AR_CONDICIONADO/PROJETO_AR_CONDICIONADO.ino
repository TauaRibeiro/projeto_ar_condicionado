#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include "DHT.h"
#include <PubSubClient.h>
#include <time.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <IRsend.h>

const int pinLDR {17};
const int pinPIR {19};
const int configLed {2};

#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

const int pinRecvIR {5};
const int pinSendIR {18};

IRrecv recvIR(pinRecvIR, 1024, 50, true, 25);
IRsend sendIR(pinSendIR);
decode_results resultado;

const char* ssid = "#Packtus";
const char* senha = "Sucesso$8";
// IPAddress localIP(192, 168, 2, 4);
// IPAddress gateway(192, 168, 2, 1);
// IPAddress subnet(255, 255, 255, 0);

WiFiClient espClient;
PubSubClient client(espClient);
String id = String(random(0xffff), HEX);
const char* host = "test.mosquitto.org";
const int numPorta{ 1883 };

const char* topicoEnvioTemperatura = "temperatura-ar";
const char* topicoEnvioPresenca = "presenca-local";
const char* topicoEnvioLuminosidade = "luminosidade-local";
const char* topicoRecebimento = "controle-ar";
const char* topicoEnvioStatus = "status-esp";
const char* topicoEnvioConfig = "config-ar";

const long intervaloEnvioDHT = 5 * 1000;
const long intervaloEnvioPIR = 30 * 1000;
const long intervaloEnvioLDR = 30 * 1000;

const long intervaloUpdatePIR = 60 * 1000;
const long intervaloUpdateLDR = 60 * 1000; 

long ultimoEnvioPIR {0};
long ultimoEnvioLDR {0};
long ultimoEnvioDHT {0};
long ultimaDeteccaoLDR {0};
long ultimaDeteccaoPIR {0};

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -3 * 3600;
const long dayLightOffset_sec = 0;

bool estadoPIR = false;
bool estadoLDR = false;

void setupWiFi() {
  //WiFi.config(localIP, gateway, subnet);
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

void MQTTSubscribe(){
  client.subscribe(topicoRecebimento);
}

void connectMQTT() {
  Serial.println("Conectando ao servidor MQTT");

  if(!WiFi.isConnected()){
    conectarWiFi();
  }
  
  while(!client.connected()) {
    id = String(random(0xffff), HEX);
    if(client.connect(id.c_str())) {
      Serial.println();
      Serial.println("Conectado ao servidor MQTT");
      MQTTSubscribe();
    }else{
      Serial.print("Falha ao conectar: ");
      Serial.println(client.state());
      delay(5000);
    }
  }
}

/*
ORDEM DE CONFIG
DESLIGAR
LIGAR
AUMENTAR TEMPERATURA
DIMINUIR TEMPERATURA
*/
void configurarControle(){
  int qtd_configurados {0};
  String ligar, desligar, aumentar, diminuir, envio;

  while(qtd_configurados < 4){
    if(recvIR.decode(&resultado)){
      String temp = "[";
      int len_comando = resultado.rawlen - 1;

      for(int i = 0; i < len_comando; i++){
        temp += String(resultado.rawbuf[i+1] * kRawTick);
        temp += (i < len_comando-1) ? "," : "]";
      }

      switch(++qtd_configurados){
        case 1:
          ligar = temp;
        break;
        case 2:
          desligar = temp;
        break;
        case 3:
          aumentar = temp;
        break;
        case 4:
          diminuir = temp;
        break;
      }

    }
    
    recvIR.resume();
  }

  envio = String("{\"desligar\":") + desligar +
  String(",\"ligar\":") + ligar +
  String(",\"aumentar\":") + aumentar +
  String(",\"diminuir\":") + diminuir +
  String("}");

  client.publish(topicoEnvioConfig, envio.c_str());
}

void emitirComando(String comando){
  Serial.print("O comando recebido foi: ");
  Serial.println(comando);
  int qtd_virgulas = 0;
  
  for(int i = 0; i < comando.length(); i++){
    if(comando[i] == ',') qtd_virgulas++;
  }

  uint16_t dadoCru[comando.length()];
  int index = 0;
  String temp = "";

  Serial.print("dadoCru[");
  Serial.print(comando.length() - qtd_virgulas);
  Serial.print("] = {");
  for(int i = 0; i < comando.length() - qtd_virgulas; i++){
    dadoCru[i] = resultado.rawbuf[i+1] * kRawTick;
    Serial.print(dadoCru[i]);
    
    if(i == resultado.rawlen-2){
      Serial.println("}");
    }else if(i % 10 == 5){
      Serial.println(",");
    }else{
      Serial.print(", ");
    }
  }

  for(int i = 0; i < comando.length()-qtd_virgulas; i++){
    
  }
  sendIR.sendRaw(dadoCru, qtd_virgulas+1, 38);
}

/*
COMANDOS PARA CONTROLAR O AR
FORMA DE RECEBIMENTO: "{comando:<número_comando>,dados:<dados>}"
1- Configurar comandos (desligar, ligar, aumentar temperatura, diminuir temperatura)
2- Enviar comando
OBS: Os dados devem ser passados somente no comando 2

FORMA DE ENVIO DAS CONFIGURAÇÕES
{desligar:<array_dados>, ligar:<array_dados>, diminuir:<array_dados>, aumentar:<array_dados>}
*/
void callback(String topico, byte* payload, unsigned int length) {
  String dados = "";
  String recebimento = (char *)payload;
  int comando;

  if(topico == topicoRecebimento){    
    int indiceComando = recebimento.indexOf("comando:");
    int indiceDados = recebimento.indexOf("dados:");

    if(indiceComando != -1){
      dados = recebimento[recebimento.indexOf(":")+1];
      comando = dados.toInt();
      dados = "";
    }
    
    if(indiceDados != -1){
      for(int i = recebimento.indexOf(":", indiceDados)+1; i < length-1; i++){
        dados += recebimento[i];
      }
    }

    switch(comando){
      case 1:
        configurarControle();
      break;
      case 2:
        emitirComando(dados);
      break;
    }
  }
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

  recvIR.enableIRIn();
  sendIR.begin();

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
    float temperatura = dht.readTemperature();
    float humidade    = dht.readHumidity();

    String envio =  String("{\"dispositivo\":\"sensorDHT\",") +
    String("\"temperatura\":") + String(temperatura) +
    String(",\"humidade\":") + String(humidade) +
    String("\"timestamp\":\"") + pegarHorario() + String("\"}");

    client.publish(topicoEnvioTemperatura, envio.c_str()); 
    ultimoEnvioDHT = millis();   
  }

  if(tempoAtual - ultimoEnvioPIR >= intervaloEnvioPIR) {
    String envio = String("{\"dispositivo\":\"sensorPIR\",") +
    String("\"presenca\":") + String(estadoPIR) + String(",") +
    String("\"timestamp\":\"") + pegarHorario() + String("\"}");;

    client.publish(topicoEnvioPresenca, envio.c_str());
    ultimoEnvioPIR = millis();
  }

  if(tempoAtual - ultimoEnvioLDR >= intervaloEnvioLDR) {
    String envio = String("{\"dispositivo\":\"sensorLDR\",") +
    String("\"luminosidade\":") + String(estadoLDR) + String(",") +
    String("\"timestamp\":\"") + pegarHorario() + String("\"}");;

    client.publish(topicoEnvioLuminosidade, envio.c_str());
    ultimoEnvioLDR = millis();
  }
}
