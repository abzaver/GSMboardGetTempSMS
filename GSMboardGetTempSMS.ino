#include <SoftwareSerial.h>
#define DEFAULT_TIMEOUT           5000   
#define DEFAULT_INTERCHAR_TIMEOUT 1500   

#include <timeouter.h>
timeouter waitDefaultTimeout;   //Таймаут для общей ошибки serial
timeouter waitIntercharTimeout; //Таймаут для передачи символа
timeouter waitTempSensorUpdate; //Таймаут для обновления температурных датчиков
timeouter waitNetworkTimeUpdate;//Таймаут для обновления сетевого времени
timeouter waitBlinkTimeout;     //Таймаут для моргания светодиодом

//#define DEBUG 1
//#define SHOW_OW_TEMP 1

// Include the libraries we need
#include <OneWire.h>
#include <DallasTemperature.h>

// Data wire is plugged into port 2 on the Arduino
#define ONE_WIRE_BUS 2

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensorsOW(&oneWire);

#include <SoftwareSerial.h>
SoftwareSerial gprsSerial(7, 8);// RX, TX

//для светодиода будем использовать 13 цифровой вход,
int lightPin = 13;

int error = 0; //номер ошибки
int errBlinkCnt = 0;

void setup()
{
    pinMode(lightPin, OUTPUT);
    // Start up the DallasTemperature library
    sensorsOW.begin();
    
    waitTempSensorUpdate.setDelay(2000);
    waitTempSensorUpdate.start();

    waitNetworkTimeUpdate.setDelay(10000);
    waitNetworkTimeUpdate.start();
      
    #ifdef DEBUG
    Serial.begin(9600);
    Serial.println("Start"); 
    #endif

    delay(10000);
    gprsSerial.begin(19200);
    delay(500);

    // Информация о состояние модуля
    if (sendCmdWithCheck(&gprsSerial, "AT\r\n","OK\r\n")){
      Serial.println("checkPowerUp OK");
    } else {
      Serial.println("checkPowerUp ERR");
      error = 1;
      return;
    }
    
    // Настраиваем приём сообщений с других устройств
    // Проверяем их на правильность выполнения
 
    // Запрет входящих звонков
    if (sendCmdWithCheck(&gprsSerial, "AT+GSMBUSY=1\r", "OK\r")){
      Serial.println("GSMBUSY OK");
    } else {
      Serial.println("GSMBUSY ERR");
      error = 2;
      return;
    }
    // Текстовый режим
    if (sendCmdWithCheck(&gprsSerial, "AT+CMGF=1\r", "OK\r")){
      Serial.println("CMGF OK");
    } else {
      Serial.println("CMGF ERR");
      error = 3;
      return;
    }
    // Контроль передачи данных
    if (sendCmdWithCheck(&gprsSerial, "AT+IFC=1, 1\r", "OK\r")){
      Serial.println("IFC OK");
    } else {
      Serial.println("IFC ERR");
      error = 4;
      return;
    }
    if (sendCmdWithCheck(&gprsSerial, "AT+CPBS=\"SM\"\r", "OK\r")){
      Serial.println("CPBS=\"SM\" OK");
    } else {
      Serial.println("CPBS=\"SM\" ERR");
      error = 5;
      return;
    }
    //Обработка СМС в реальном времени
    if (sendCmdWithCheck(&gprsSerial, "AT+CNMI=1,2,2,1,0\r", "OK\r")){
      Serial.println("CNMI OK");
    } else {
      Serial.println("CNMI ERR");
      error = 6;
      return;
    }

    waitBlinkTimeout.setDelay(2000);
    waitBlinkTimeout.start();
}
 
String currStr = "";
// Переменная принимает значение True, если текущая строка является сообщением
boolean isStringMessage = false;
// Переменная принимает значение True, если текущая строка является ответом USSD
boolean isStringUSSDreply = false;
// Статус команды включения светодиода
boolean lightOnCmd = false;
// Статус светодиода
boolean lightOn = false;
// Номер абонента для отправки СМС, по умолчанию
char *senderNumber ="+79263653824";

void loop()
{
    
    // Выводим код ошибки на светодиод
    if (error){
      int blinkDelay = 500;
      errBlinkCnt++;
      if (errBlinkCnt >= error) {
        errBlinkCnt = 0;
        blinkDelay = 2000;
      }
      digitalWrite(13, HIGH);   // turn the LED on (HIGH is the voltage level)
      delay(500);               // wait for a second
      digitalWrite(13, LOW);    // turn the LED off by making the voltage LOW
      delay(blinkDelay);        // wait for a second
      return;
    } else if (!lightOnCmd) {
        if (waitBlinkTimeout.isOver()) {
          lightOn = !lightOn;
          if (lightOn){
            digitalWrite(13, HIGH);
          } else {
            digitalWrite(13, LOW);
          }
          waitBlinkTimeout.start();
        }
    }
    
    //Работа с датчиком температуры и влажности
    if (waitTempSensorUpdate.isOver()) {
      waitTempSensorUpdate.start();
      sensorsOW.requestTemperatures();
      #ifdef SHOW_OW_TEMP
      Serial.println(sensorsOW.getTempCByIndex(0));
      #endif
    }    
    
    // Принимаем СМС разбираем и исполняем команды
    if (!gprsSerial.available())
        return;
 
    char currSymb = gprsSerial.read();    
    if ('\r' == currSymb) {
        #ifdef DEBUG 
        Serial.println(currStr);
        #endif
        if (isStringMessage) {
            //если текущая строка - SMS-сообщение,
            //отреагируем на него соответствующим образом
            if (currStr.equalsIgnoreCase("Light on")) {
                digitalWrite(lightPin, HIGH);
                sendTextMessage(senderNumber,"Light is on");
                lightOnCmd = true;
            } else if (currStr.equalsIgnoreCase("Light off")) {
                digitalWrite(lightPin, LOW);
                sendTextMessage(senderNumber,"Light is off");
                lightOnCmd = false;
            } else if (currStr.equalsIgnoreCase("Get temp")) {
                char strTemp[6];
                char strMessage[15];
                /* 4 is mininum width, 2 is precision; float value is copied onto str_temp*/
                dtostrf(sensorsOW.getTempCByIndex(0), 4, 2, strTemp);
                snprintf(strMessage, 15, "Temp: %sC", strTemp);
                sendTextMessage(senderNumber,strMessage);
            } /*else if (currStr.equalsIgnoreCase("balance")) {
                if (sendCmdWithCheck(&gprsSerial, "ATD*100#;\r", "OK\r")) {
                  
                }
            }*/
            isStringMessage = false;
        } else {
            if (currStr.startsWith("+CMT")) {
                //если текущая строка начинается с "+CMT",
                //то следующая строка является сообщением
                isStringMessage = true;
                //экстрагируем номер отправителя
                currStr.substring(7,19).toCharArray(senderNumber, 13);
                #ifdef DEBUG
                Serial.println(senderNumber);
                #endif
            }
        }
        currStr = "";
    } else if ('\n' != currSymb) {
        currStr += String(currSymb);
    }
}

/*
 * Функция отправки SMS-сообщения
 */
void sendTextMessage(char *number, char *msg) {
    // Устанавливает текстовый режим для SMS-сообщений
    gprsSerial.print("AT+CMGF=1\r");
    delay(100); // даём время на усваивание команды
    // Устанавливаем адресата: телефонный номер в международном формате
    gprsSerial.print("AT+CMGS=\"");  /// Команда отправить SMS
    gprsSerial.print(number); /// Ввод номера абонента
    delay(100); /// Задержка на запись
    gprsSerial.print("\"\r");
    delay(100);
    // Пишем текст сообщения
    Serial.println(gprsSerial.println(msg), DEC);
    delay(100);
    // Отправляем Ctrl+Z, обозначая, что сообщение готово
    gprsSerial.println((char)26);
}


boolean sendCmdWithCheck(const SoftwareSerial* SS, const char* cmd, const char *resp)
{
    SS->println(cmd);

    int len = strlen(resp);
    int sum = 0;    
    waitDefaultTimeout.setDelay(DEFAULT_TIMEOUT);
    waitIntercharTimeout.setDelay(DEFAULT_INTERCHAR_TIMEOUT);
    
    waitDefaultTimeout.start();
    while(1) {
        if(gprsSerial.available()) {
            char c = SS->read();
            waitIntercharTimeout.start();
            sum = (c==resp[sum]) ? sum+1 : 0;
            if(sum == len)break;
        }
        if (waitDefaultTimeout.isOver()) {
            return false;
        }
        //If interchar Timeout => return FALSE. So we can return sooner from this function.
        if (waitIntercharTimeout.isOver()) {
            return false;
        }
    }
    return true;
}

