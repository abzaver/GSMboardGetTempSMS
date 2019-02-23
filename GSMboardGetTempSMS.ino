/*
SMS
+CMT: "+79263653824","","12/12/12,00:00:00+3"
set temp 12
*/
//#define DEBUG          1
//#define SHOW_OW_TEMP   1
//#define WITHOUT_SIM900 1
#define WITHOUT_LCD    1

#include <avr/wdt.h>

#include <timeouter.h>
timeouter waitDefaultTimeout;     //Таймаут для общей ошибки serial
timeouter waitIntercharTimeout;   //Таймаут для передачи символа
timeouter waitTempSensorUpdate;   //Таймаут для обновления температурных датчиков
timeouter waitSigStrengthUpdate;  //Таймаут для обновления мощности сигнала
timeouter waitBalanceUpdate;      //Таймаут для обновления остатка на счете
timeouter waitBlinkTimeout;       //Таймаут для моргания светодиодом
timeouter waitWDogSIM900Tmout;    //Таймаут для Watchdog SIM900
timeouter waitForRebootTimeout;   //Таймаут для перезагрузки устройства
timeouter waitHeartbeatTimeout;   //Таймаут для автоматического оповещения

// Define pins
#define PIN_BUTTON       A2 //pin where locate PushButton
#define PIN_ONE_WIRE_BUS  2 //Data wire is plugged into port 2 on the Arduino
#define PIN_LIGHT        13 //signal LED
#define PIN_FAILURE       5 //for failure registration
#define PIN_GSM_RX        7 //pin for GSM receive Software Serial
#define PIN_GSM_TX        8 //pin for GSM transmit Software Serial

#ifdef WITHOUT_LCD

#else
#define PIN_LCD_LIGHT     3 //LCD backlight
#define PIN_LCD_SCK      13 //yellow wire
#define PIN_LCD_MOSI     11 //green wire
#define PIN_LCD_DC       10 //orange wire
#define PIN_LCD_RST      12 //red wire
#define PIN_LCD_CS        6 //brown wire
#endif //WITHOUT_LCD

#ifndef WITHOUT_SIM900
#include "sim900.h"
SoftwareSerial gprsSerial(PIN_GSM_RX, PIN_GSM_TX);// RX, TX
#endif //WITHOUT_SIM900

#ifndef WITHOUT_LCD
timeouter waitDisplayUpdateTmout; //Таймаут для обновления дисплея

#include <LCD5110_Basic.h>
LCD5110 LCD(PIN_LCD_SCK, PIN_LCD_MOSI, PIN_LCD_DC, PIN_LCD_RST, PIN_LCD_CS);

extern uint8_t SmallFont[];
extern uint8_t MediumNumbers[];
//extern uint8_t BigNumbers[];

bool lcdInverse = false;
#endif //WITHOUT_LCD

//Push Button on PIN_BUTTON
bool     button_state = false;
uint32_t ms_button    = 0;

// Include the libraries we need
#include <OneWire.h>
#include <DallasTemperature.h>

#include <EEPROM.h>

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(PIN_ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensorsOW(&oneWire);

//error handling
int error       = 0; //error number
int errBlinkCnt = 0; //display error number by LED blinks

/*
 * Функция отправки SMS-сообщения
 */
bool sendSMS(char *number, char *data){
  #ifdef WITHOUT_SIM900
  Serial.println("-------startSMS-------");
  Serial.println(data);
  Serial.println("--------endSMS--------");
  return true;
  #else
  sim900_flush_serial();
  sim900_send_cmd("AT+CMGS=\"");
  sim900_send_cmd(number);
  if(!sim900_check_with_cmd("\"\r\n",">",CMD)) {
        return false;
    }
  delay(100);
  sim900_send_cmd(data);
  delay(100);
  sim900_send_End_Mark();
  return sim900_wait_for_resp("OK\r\n", CMD);
  #endif //WITHOUT_SIM900
}

/*
 * Функция отправки USSD-запроса
 */
bool sendUSSDSynchronous(char *ussdCommand, char *response){
  #ifdef WITHOUT_SIM900
  Serial.println("-------sendUSSD-------");
  Serial.println(ussdCommand);
  Serial.println("--------endUSSD--------");
  return true;
  #else
  //AT+CUSD=1,"{command}"
  //OK
  //
  //+CUSD:1,"{response}",{int}

  byte i = 0;
  char resultcode[2]; 
  char gprsBuffer[200];
  char *p,*s;
  sim900_clean_buffer(response, sizeof(response));
  
  sim900_flush_serial();
  sim900_send_cmd("AT+CUSD=1,\"");
  sim900_send_cmd(ussdCommand);
  sim900_send_cmd("\"\r");
  if(!sim900_wait_for_resp("OK\r\n", CMD)) {
    return false;
  }
  sim900_clean_buffer(gprsBuffer, 200);
  //delay(3000);
  sim900_read_buffer(gprsBuffer, 200, DEFAULT_TIMEOUT);
  if(NULL != ( s = strstr(gprsBuffer,"+CUSD: "))) {
    *resultcode = *(s+7);
    resultcode[1] = '\0';
    if(!('0' <= *resultcode && *resultcode <= '2')) {
      return false;
    }
    s = strstr(s,"\""); //s is first character """
        s = s + 1;  //We are in the first response character
        p = strstr(s,"\""); //p is last character """
        if (NULL != s) {
            i = 0;
            while (s < p) {
              response[i++] = *(s++);
            }
            response[i] = '\0';
        }
    return true;
  }
  return false;
  #endif //WITHOUT_SIM900
}

/*
 * Функция получения уровня сигнала в сети
 */
short int gsmNetStatus(){
  #ifdef WITHOUT_SIM900
  return -53;
  #else  
  byte i = 0;
  char *p,*s;
  char tempchar[7]; // временная переменная 
  int sgr; // уровень сигнала в сети в dBi
  char gprsBuffer[200];
  
  sim900_flush_serial();
  sim900_send_cmd("AT+CREG?\r");  // выполним запрос состояния сети
  sim900_clean_buffer(gprsBuffer, 200);
  sim900_read_buffer(gprsBuffer, 200, DEFAULT_TIMEOUT);
  if (strstr(gprsBuffer, "0,1")) {//если сеть есть  -  модуль вернул "+CREG: 0,1"
    sim900_flush_serial();
    sim900_send_cmd("AT+CSQ\r"); //Запросим уровень сигнала
    sim900_clean_buffer(gprsBuffer, 200);
    sim900_read_buffer(gprsBuffer, 200, DEFAULT_TIMEOUT);
    if(NULL != ( s = strstr(gprsBuffer,"+CSQ: "))) { //  Модуль вернет строку типа +CSQ: 17,0, нужное нам значение между : и ,
      s = strstr(s,":"); //s is character ":"
      s = s + 1;  //We are in the first response character
      p = strstr(s,","); //p is character ","
      if (NULL != s) {
          i = 0;
          while (s < p) {
            tempchar[i++] = *(s++);
          }
          tempchar[i] = '\0';
      }
      sgr = atoi(tempchar); // преобразуем строку в число
      sgr = -115 + (sgr * 2); // переведем число в dBi 
      return sgr; // вернем значение          
    }
  }
  return 0;
  #endif //WITHOUT_SIM900
}

/*
 * Функция получения из строки str, с началом на start символе и длиной length
 */
char *getSubString(char *str, int start, int length = 0) {
  char *s;
  // Определить длину исходной строки
  int len = 0;
  for (int i = 0; str[i] != '\0'; i++)
    len++;
  // Определить позицию последнего символа подстроки
  if (length > 0) {
    if (start + length < len)
      len = start + length;
  }
  else // length < 0
    len = len + length;
  int newlen = len - start + 1; // длина подстроки
  s = new char[newlen];
  // Копирование символов подстроки
  int j = 0;
  for (int i = start; i<len; i++) {
    s[j] = str[i]; j++;
  }
  s[j] = '\0';
  return(s);
}

/*
 * Функция получения из строки src_str, подстроку, ограниченную символами beg_str и end_str
 */
char *extractFromString (char *src_str, char *beg_str, char *end_str)  {
// *src_str = исходная строка
// *beg_str = символы перед нужной нам частью строки
// *end_str = символы после нужной нам части строки  
  char *tempchar;
  if (src_str != NULL) {// проверяем указатель на исходную строку
    tempchar = strstr(src_str, beg_str); // ищем beg_str
    tempchar = tempchar + strlen(beg_str); 
    *(strstr (tempchar, end_str)) = 0; // ставим  NULL символ перед началом end_str
    return tempchar;
   }
}

// Значение текущей температуры
float currentTemperature = DEVICE_DISCONNECTED_C;
// Значение пороговой температуры
int   warningTemp   = 12;
float hysteresis    = 0.5;
bool  warningSended = false;

void setup()
{
    #ifndef WITHOUT_LCD
    LCD.InitLCD();
    #endif

    pinMode(PIN_FAILURE,   INPUT);
    pinMode(PIN_LIGHT,     OUTPUT);
    #ifndef WITHOUT_LCD
    pinMode(PIN_LCD_LIGHT, OUTPUT);
    #endif
    pinMode(PIN_BUTTON,    INPUT_PULLUP);
    
    // Start up the DallasTemperature library
    sensorsOW.begin();
    
    #ifdef DEBUG
    Serial.begin(9600);
    Serial.print("DEBUG: ");
    Serial.println("Start"); 
    #endif

    // Читаем сохраненное значение проговой температуры
    EEPROM.get(0, warningTemp);
    //EEPROM.get(sizeof(int), hysteresis);
    
    #ifdef DEBUG
    //warningTemp = 30;
    Serial.print("DEBUG: ");
    Serial.print("warningTemp: ");
    Serial.println(warningTemp);
    //hysteresis = 0.5;
    Serial.print("DEBUG: ");
    Serial.print("hysteresis: ");
    Serial.println(hysteresis);    
    #endif

    // задержка на
    int i = 0;
    while(i < 10){ // 10 секунд
      digitalWrite(PIN_LIGHT, HIGH);
      delay(500);
      digitalWrite(PIN_LIGHT, LOW);
      delay(500);
      i++;
    }
    
	  #ifndef WITHOUT_SIM900
	  sim900_init(&gprsSerial, 9600);
    delay(500);

    // Информация о состоянии модуля
    if (sim900_check_with_cmd("AT\r\n","OK\r\n",CMD)){
      #ifdef DEBUG
      Serial.print("DEBUG: ");
      Serial.println("checkGeneral OK");
      #endif
    } else {
      #ifdef DEBUG
      Serial.print("DEBUG: ");
      Serial.println("checkGeneral ERR");
      #endif
      error = 1;
      return;
    }

    // Информация о статусе модуля
    if (sim900_check_with_cmd("AT+CPAS\r\n","+CPAS: 0\r\n",CMD)){
      #ifdef DEBUG
      Serial.print("DEBUG: ");
      Serial.println("checkPAS OK");
      #endif
    } else {
      #ifdef DEBUG
      Serial.print("DEBUG: ");
      Serial.println("checkPAS ERR");
      #endif
      error = 2;
      return;
    }    
    
    // Настраиваем приём сообщений с других устройств
    // Проверяем их на правильность выполнения
 
    // Запрет входящих звонков
    if (sim900_check_with_cmd("AT+GSMBUSY=1\r", "OK\r", CMD)){
      #ifdef DEBUG
      Serial.print("DEBUG: ");
      Serial.println("GSMBUSY OK");
      #endif
    } else {
      #ifdef DEBUG
      Serial.print("DEBUG: ");
      Serial.println("GSMBUSY ERR");
      #endif
      error = 3;
      return;
    }
    // Текстовый режим
    if (sim900_check_with_cmd("AT+CMGF=1\r", "OK\r", CMD)){
      #ifdef DEBUG
      Serial.print("DEBUG: ");
      Serial.println("CMGF OK");
      #endif
    } else {
      #ifdef DEBUG
      Serial.print("DEBUG: ");
      Serial.println("CMGF ERR");
      #endif
      error = 4;
      return;
    }
    // Контроль передачи данных
    if (sim900_check_with_cmd("AT+IFC=1, 1\r", "OK\r", CMD)){
      #ifdef DEBUG
      Serial.print("DEBUG: ");
      Serial.println("IFC OK");
      #endif
    } else {
      #ifdef DEBUG
      Serial.print("DEBUG: ");
      Serial.println("IFC ERR");
      #endif
      error = 5;
      return;
    }
    if (sim900_check_with_cmd("AT+CPBS=\"SM\"\r", "OK\r", CMD)){
      #ifdef DEBUG
      Serial.print("DEBUG: ");
      Serial.println("CPBS=\"SM\" OK");
      #endif
    } else {
      #ifdef DEBUG
      Serial.print("DEBUG: ");
      Serial.println("CPBS=\"SM\" ERR");
      #endif
      error = 6;
      return;
    }
    //Обработка СМС в реальном времени
    if (sim900_check_with_cmd("AT+CNMI=1,2,2,1,0\r", "OK\r", CMD)){
      #ifdef DEBUG
      Serial.print("DEBUG: ");
      Serial.println("CNMI OK");
      #endif
    } else {
      #ifdef DEBUG
      Serial.print("DEBUG: ");
      Serial.println("CNMI ERR");
      #endif
      error = 7;
      return;
    }
    #endif //WITHOUT_SIM900
	
	#ifdef WITHOUT_SIM900
    error = 0;
    #endif //WITHOUT_SIM900

    waitBlinkTimeout.setDelay(2000);
    waitBlinkTimeout.start();

    waitTempSensorUpdate.setDelay(5000);
    waitTempSensorUpdate.start();

    waitSigStrengthUpdate.setDelay(5000);
    waitSigStrengthUpdate.start();

    waitBalanceUpdate.setDelay(300000); //5 минут
    waitBalanceUpdate.start();

    waitWDogSIM900Tmout.setDelay(60000); //1 минута
    waitWDogSIM900Tmout.start();

    waitHeartbeatTimeout.setDelay(1000*3600*24UL); //1 сутки
    waitHeartbeatTimeout.start();

    #ifndef WITHOUT_LCD
    waitDisplayUpdateTmout.setDelay(1000);
    waitDisplayUpdateTmout.start();
    #endif
	
  	wdt_enable (WDTO_8S); //enable watchdog
  	#ifdef DEBUG
  	Serial.print("DEBUG: ");
  	Serial.println("Watchdog enabled.");
  	#endif
    waitForRebootTimeout.setDelay(60000); //1 минута
    waitForRebootTimeout.start();	
}
 
String currStr = "";
// Переменная принимает значение True, если текущая строка является сообщением
boolean isStringMessage = false;
// Статус команды включения светодиода
boolean lightOnCmd = false;
// Статус светодиода
boolean lightOn = false;
// Статус тревоги
boolean alarm = false;
int alarmSensorVal = LOW;
// Номер абонента для отправки СМС, по умолчанию
char *senderNumber ="+79263653824";
// Переменная со значением силы сигнала
int signalStrength = 0;

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
      digitalWrite(PIN_LIGHT, HIGH);
      delay(500);               
      digitalWrite(PIN_LIGHT, LOW);
      delay(blinkDelay);
      
	  if (waitForRebootTimeout.isOver()) {
    #ifdef DEBUG
    Serial.print("DEBUG: ");
    Serial.println("rebooting device");
    #endif
		wdt_enable(WDTO_15MS);
		while (1) { }
	  }
	  wdt_reset();
	  return;
    } else if (!lightOnCmd) {
        if (waitBlinkTimeout.isOver()) {
          lightOn = !lightOn;
          if (lightOn){
            digitalWrite(PIN_LIGHT, HIGH);
          } else {
            digitalWrite(PIN_LIGHT, LOW);
          }
          waitBlinkTimeout.start();
        }
    }
	  waitForRebootTimeout.start();

    // Запрашиваем силу сигнала и выводим если она изменилась с прошлого раза
    /*
    if (waitSigStrengthUpdate.isOver()) {
      int signalStrengthCurrent = gsmNetStatus();
      if (signalStrengthCurrent != signalStrength) {
        signalStrength = signalStrengthCurrent;
        Serial.print("signal strength: ");
        Serial.print(signalStrength);
        Serial.println("dBi");
      }
      waitSigStrengthUpdate.start();
    }
    */

    // Раз в минуту проверяем состояние GSM модуля
    #ifdef DEBUG
    /*
    if (waitWDogSIM900Tmout.isOver()) {
      if (sim900_check_with_cmd("AT+CPAS\r\n","+CPAS: 0\r\n",CMD)){
        Serial.println("checkPAS OK");
      } else {
        Serial.println("checkPAS ERR");
      }
      waitWDogSIM900Tmout.start();
    }
    */
    #endif
    
    // Раз в 5 минут запрашиваем баланс через USSD
    #ifdef DEBUG
    /*
    if (waitBalanceUpdate.isOver()) {
      char strResponse[50];
      if (sendUSSDSynchronous("*100#", strResponse)) {
        Serial.print("have balance response: ");
        Serial.println(getSubString(strResponse, 0, strcspn(strResponse, "\r\n")));
      } else {
        Serial.println("error check balance");
      }
      waitBalanceUpdate.start();
    }
    */
    #endif
        
    //Работа с тревогой
    /*
    int tempSensorVal = digitalRead(PIN_FAILURE);
    if (tempSensorVal != alarmSensorVal) {
      #ifdef DEBUG
      Serial.println(tempSensorVal);
      #endif

      if (alarmSensorVal == HIGH) { 
        alarm = true;
        sendSMS(senderNumber, "boiler is failure!");
        #ifdef DEBUG
        Serial.println("alarm!!!");
        #endif
      } else {
        alarm = false;
        sendSMS(senderNumber, "boiler is operational");
        #ifdef DEBUG
        Serial.println("all OK");
        #endif
      }
    }
    alarmSensorVal = tempSensorVal;
    */ 

    uint32_t ms = millis();
    // Фиксируем нажатие кнопки
    if( digitalRead(PIN_BUTTON) == LOW && !button_state && ( ms - ms_button ) > 50 ){
      button_state = true;
      ms_button    = ms;
    }
    // Фиксируем отпускание кнопки
    if( digitalRead(PIN_BUTTON) == HIGH && button_state && ( ms - ms_button ) > 50  ){
      button_state = false;     
      ms_button    = ms;
      if (lightOnCmd) {
        #ifdef WITHOUT_LCD
        digitalWrite(PIN_LIGHT, HIGH);
        #else
        digitalWrite(PIN_LCD_LIGHT, HIGH);
        #endif //WITHOUT_LCD        
      } else {
        #ifdef WITHOUT_LCD
        digitalWrite(PIN_LIGHT, LOW);
        #else
        digitalWrite(PIN_LCD_LIGHT, LOW);
        #endif //WITHOUT_LCD 
      }
      lightOnCmd = !lightOnCmd;
    }


    #ifndef WITHOUT_LCD
    if (waitDisplayUpdateTmout.isOver()) {
      lcdInverse = !lcdInverse;
      if (warningSended) {
        LCD.invert(lcdInverse);
      } else {
        LCD.invert(false);
      }

      LCD.clrScr();
      LCD.setFont(SmallFont);
      LCD.print("current temp:", CENTER, 16);
      LCD.setFont(MediumNumbers);
      LCD.printNumF(currentTemperature,2,CENTER,24);
      LCD.setFont(SmallFont);
      LCD.print("warn temp:", LEFT, 40);
      LCD.printNumI(warningTemp, RIGHT, 40);

      waitDisplayUpdateTmout.start();
    }
    #endif
    
    //Работа с датчиком температуры
    if (waitTempSensorUpdate.isOver()) {
      sensorsOW.requestTemperatures();
      currentTemperature = sensorsOW.getTempCByIndex(0);
      if (currentTemperature == DEVICE_DISCONNECTED_C) {
        #ifdef DEBUG
        Serial.print("DEBUG: ");
        Serial.println("DEVICE DISCONNECTED");
        #endif
        error = 8;
      } else {
        #ifdef SHOW_OW_TEMP
        Serial.print("DEBUG: ");
        Serial.println(currentTemperature);
        #endif
        if (currentTemperature < warningTemp) {
          if (!warningSended) {
            char strTemp[6];
            char strMessage[50];
            // 4 is mininum width, 2 is precision; float value is copied onto str_temp
            dtostrf(currentTemperature, 4, 2, strTemp);
            /*
            snprintf здесь урезанная, и не умеет работать с double
            поэтому используем dtostrf
            */
            snprintf(strMessage, 50, "!!!WARNING!!!\ntemp is: %sC", strTemp);
            #if defined(DEBUG) && !defined(WITHOUT_SIM900)
            Serial.println("-------startDBG-------");
            Serial.println(strMessage);
            Serial.println("--------endDBG--------");
            #endif
            sendSMS(senderNumber, strMessage);
            warningSended = true;
          }
        } else {
          if (warningSended && currentTemperature > (warningTemp + hysteresis)) {
            char strTemp[6];
            char strMessage[25];
            // 4 is mininum width, 2 is precision; float value is copied onto str_temp
            dtostrf(currentTemperature, 4, 2, strTemp);
            /*
            snprintf здесь урезанная, и не умеет работать с double
            поэтому используем dtostrf
            */
            snprintf(strMessage, 25, "temp is normal: %sC", strTemp);
            #if defined(DEBUG) && !defined(WITHOUT_SIM900)
            Serial.println("-------startDBG-------");
            Serial.println(strMessage);
            Serial.println("--------endDBG--------");            
            #endif
            sendSMS(senderNumber, strMessage);
            warningSended = false;
          }
        }
      }
      waitTempSensorUpdate.start();
    }    

    //Раз в сутки отправляем температуру автоматически
    if (waitHeartbeatTimeout.isOver()) {
      char strTemp[6];
      char strWarnTemp[6];
      char strMessage[160];
      
      // 4 is mininum width, 2 is precision; float value is copied onto str_temp
      dtostrf(currentTemperature, 4, 2, strTemp);
      dtostrf(warningTemp, 4, 2, strWarnTemp);
      /*
      snprintf здесь урезанная, и не умеет работать с double
      поэтому используем dtostrf
      */
      snprintf(strMessage, 160, "heartbeat\nCURR temp: %sC\nWARN temp: %sC", strTemp, strWarnTemp);
      sendSMS(senderNumber, strMessage);
      waitHeartbeatTimeout.start();
    }                      
    
    // Принимаем СМС разбираем и исполняем команды
    #ifdef WITHOUT_SIM900
    if (Serial.available()){
 
      char currSymb = Serial.read();
    #else
    if (gprsSerial.available()){
   
      char currSymb = gprsSerial.read();
    #endif
      if ('\r' == currSymb) {
          #ifdef DEBUG 
          Serial.print("DEBUG: ");
          Serial.println(currStr);
          #endif
          if (isStringMessage) {
              //если текущая строка - SMS-сообщение,
              //отреагируем на него соответствующим образом
              if (currStr.equalsIgnoreCase("light on")) {
                  #ifdef WITHOUT_LCD
                  digitalWrite(PIN_LIGHT, HIGH);
                  #else
                  digitalWrite(PIN_LCD_LIGHT, HIGH);
                  #endif //WITHOUT_LCD
                  sendSMS(senderNumber, "Light is on");
                  lightOnCmd = true;
              } else if (currStr.equalsIgnoreCase("light off")) {
                  #ifdef WITHOUT_LCD
                  digitalWrite(PIN_LIGHT, LOW);
                  #else
                  digitalWrite(PIN_LCD_LIGHT, LOW);
                  #endif //WITHOUT_LCD
                  sendSMS(senderNumber, "Light is off");
                  lightOnCmd = false;
              } else if (currStr.equalsIgnoreCase("get temp") || currStr.equalsIgnoreCase("gt")) {
                  char strTemp[6];
                  char strWarnTemp[6];
                  char strMessage[160];
                  
                  // 4 is mininum width, 2 is precision; float value is copied onto str_temp
                  dtostrf(currentTemperature, 4, 2, strTemp);
                  dtostrf(warningTemp, 4, 2, strWarnTemp);
                  /*
                  snprintf здесь урезанная, и не умеет работать с double
                  поэтому используем dtostrf
                  */
                  snprintf(strMessage, 160, "CURR temp: %sC\nWARN temp: %sC", strTemp, strWarnTemp);
                  sendSMS(senderNumber, strMessage);
  
              } else if (currStr.startsWith("set temp")) {
                  char command[20];
                  currStr.toCharArray(command, 20);
                  sscanf(command, "set temp %d", &warningTemp);
                  // Сохраняем значение проговой температуры
                  EEPROM.put(0, warningTemp);
                  #ifdef DEBUG
                  Serial.print("DEBUG: ");
                  Serial.print("warningTemp: ");
                  Serial.println(warningTemp);
                  #endif
              } else if (currStr.startsWith("set hyst")) {
                  // sscanf не работает с float
                  /*
                  char command[20];
                  currStr.toCharArray(command, 20);
                  sscanf(command, "set hyst %d", &hysteresis);
                  // Сохраняем значение проговой температуры
                  EEPROM.put(0, warningTemp);
                  #ifdef DEBUG
                  Serial.print("warningTemp: ");
                  Serial.println(warningTemp);
                  #endif
                  */
              } else if (currStr.startsWith("reboot")) {                  
                  wdt_enable(WDTO_15MS);
                  while (1) { }                  
              } else if (currStr.equalsIgnoreCase("balance") || currStr.equalsIgnoreCase("bl")) {
                  char strResponse[50];
                  if (sendUSSDSynchronous("*100#", strResponse)) {
                    sendSMS(senderNumber, getSubString(strResponse, 0, strcspn(strResponse, "\r\n")));
                  }
              }
              isStringMessage = false;
          } else {
              if (currStr.startsWith("+CMT")) {
                  //если текущая строка начинается с "+CMT",
                  //то следующая строка является сообщением
                  isStringMessage = true;
                  //экстрагируем номер отправителя
                  currStr.substring(7,19).toCharArray(senderNumber, 13);
                  #ifdef DEBUG
                  Serial.print("DEBUG: ");
                  Serial.println(senderNumber);
                  #endif
              }
          }
          currStr = "";
      } else if ('\n' != currSymb) {
          currStr += String(currSymb);
      }
    }
	wdt_reset();
}

