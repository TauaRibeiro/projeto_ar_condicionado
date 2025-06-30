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

const int pinLDR {15};
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

const char* ssid = "WifiA35";
const char* senha = "Kilo021b#";
// IPAddress localIP(192, 168, 2, 4);
// IPAddress gateway(192, 168, 2, 1);
// IPAddress subnet(255, 255, 255, 0);

WiFiClient espClient;
PubSubClient client(espClient);
String id = String(random(0xffff), HEX);
const char* host = "test.mosquitto.org";
const int numPorta{ 1883 };

#define MQTT_MAX_PACKET_SIZE 2048

const char* topicoEnvioTemperatura = "temperatura-ar";
const char* topicoEnvioPresenca = "presenca-local";
const char* topicoEnvioLuminosidade = "luminosidade-local";
const char* topicoRecebimento = "controle-ar";
const char* topicoEnvioStatus = "status-esp";
const char* topicoEnvioConfig = "config-ar";

int num_partes {10};
int num_partes_recebidas {0};
String comando[10];

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
  String temp, envio;
  Serial.println("Preparando para configurar");

  while(qtd_configurados < 4){
    if(recvIR.decode(&resultado)){
      int len_comando = resultado.rawlen - 1;
      int len_parte = len_comando/num_partes;
      int indiceParte = 0;
      int numElementos = 0;
      
      for(int i = 0; i < len_comando; i++){
        temp += String(resultado.rawbuf[i+1] * kRawTick);

        numElementos++;
        temp += (numElementos >= len_parte && indiceParte < num_partes) ? "" : ",";
        if(numElementos >= len_parte && indiceParte < num_partes) {
          comando[indiceParte++] = temp;
          temp = "";
          numElementos = 0;
        }
      }

      for(int i = 0; i < num_partes; i++){
        Serial.print("Parte ");
        Serial.print(i+1);
        Serial.print(": ");
        Serial.println(comando[i]);
      }

      switch(++qtd_configurados){
        case 1:
          for(int i = 0; i < num_partes; i++){
            envio = String("{\"desligar") + String("\":[") + comando[i] + String("]}");
            delay(1000); 
            client.publish(topicoEnvioConfig, envio.c_str());
            comando[i] = "";
          }
        break;
        case 2:
          for(int i = 0; i < num_partes; i++){
            envio = String("{\"ligar") + String("\":[") + comando[i] + String("]}");
            delay(1000);
            client.publish(topicoEnvioConfig, envio.c_str());
            comando[i] = "";
          }
        break;
        case 3:
          for(int i = 0; i < num_partes; i++){
            envio = String("{\"aumentar") + String("\":[") + comando[i] + String("]}");
            delay(1000);
            client.publish(topicoEnvioConfig, envio.c_str());
            comando[i] = "";
          }
        break;
        case 4:
          for(int i = 0; i < num_partes; i++){
            envio = String("{\"diminuir") + String("\":[") + comando[i] + String("]}");
            client.publish(topicoEnvioConfig, envio.c_str());
            comando[i] = "";
          }
        break;
      }

      Serial.print(qtd_configurados);
      Serial.println("/4 Configurados");

      recvIR.resume();
    }
  }
}

void emitirComando(String payload){
  payload.replace("[", "");
  payload.replace("]", "");
  
  if(num_partes_recebidas < num_partes-1){
    comando[num_partes_recebidas++] = payload;
    Serial.print(num_partes_recebidas);
    Serial.print("/");
    Serial.print(num_partes);
    Serial.println(" Partes recebidas");

    return;
  }

  uint16_t dadoCru[1024];
  int index = 0;
  int qtd_virgulas = 1;
  String temp = "";
  String comandoInteiro = comando[0];
  
  for(int i = 1; i < num_partes-1; i++){
    comandoInteiro += "," + comando[i];
  }

  comandoInteiro += "," + payload;

  Serial.print("Comando inteiro: ");
  Serial.println(comandoInteiro);

  for(int i = 0; i < comandoInteiro.length(); i++){
    if(comandoInteiro[i] == ',') qtd_virgulas++;
  }

  for(int i = 0; i < comandoInteiro.length(); i++){
    if(comandoInteiro[i] == ','){
      dadoCru[index++] = temp.toInt();
      temp = "";
      continue;                        
    }  

    temp += comandoInteiro[i];
  }

  dadoCru[qtd_virgulas-1] = temp.toInt();

  Serial.print("dadoCru[");
  Serial.print(qtd_virgulas);
  Serial.print("] = {");
  for(int i = 0; i < qtd_virgulas; i++){
    Serial.print(dadoCru[i]);
    if(i == qtd_virgulas-1){
      Serial.println("}");
    }else if(i % 10 == 5){
      Serial.println(",");
    }else{
      Serial.print(", ");
    }
  }

  sendIR.sendRaw(dadoCru, qtd_virgulas, 38);

  for(int i = 0; i < num_partes; i++){
    comando[i] = "";
    num_partes_recebidas = 0;
  }
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
    Serial.print("Comando: ");
    Serial.println(comando);
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
    return "0/0/0 - 0:0:0";
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
