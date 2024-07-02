#include <Arduino.h>
#include <ezTime.h>



#include <ESP8266WiFi.h>

//NodeMCU 1.0 (ESP-12E module)
//define MY_SSID, MY_WIFI_PASSWORD
#include "passwd.h"

//i flipped the wiring on these, so flip the pins to match what should actually happen - left relay is on the left and right relay is one the right
#define RELAY_PIN_1 4
#define RELAY_PIN_2 5
#define SENSOR_PIN_1 14
#define SENSOR_PIN_2 12

//#define ENABLE_DEBUGGING
#ifdef ENABLE_DEBUGGING

//socket debugging
#define DEBUG_ADDRESS "192.168.1.206"
#define DEBUG_PORT 4565
WiFiClient DebugSocket;
#endif

void DebugPrint(String str)
{
  Serial.print(str);
#ifdef ENABLE_DEBUGGING
  //reconnect if we're not connected
  if (!DebugSocket.connected())
  {
    if (!DebugSocket.connect(DEBUG_ADDRESS, DEBUG_PORT))
      return; //failed to reconnect - give up
    DebugSocket.setNoDelay(true);
  }

  DebugSocket.print(str);
  DebugSocket.flush();
#endif
}
  Timezone myTZ;

int AllowToStayOpenTimeMS = 10 * 60 * 1000;

#define HMSToTime(hour, minute, second) (hour*60*60 + minute*60 + second)

struct Schedule
{
  int m_NumSchedules;
  int m_OpenTime[2];
  int m_CloseTime[2];  
};


Schedule WeekendSchedule =
{
  1,
  { HMSToTime(6, 0, 0), -1},
  { HMSToTime(22, 0, 0), -1}
};

Schedule WeekdaySchedule =
{
  2,
  { HMSToTime(6, 0, 0),  HMSToTime(15, 0, 0) },
  { HMSToTime(11, 0, 0), HMSToTime(22, 0, 0) }
};


String TimeToString(Timezone& tz, time_t thetime = TIME_NOW)
{
  return String(tz.day(thetime)) + "/" + String(tz.month(thetime)) + "/" + String(tz.year(thetime)) + " " + String(tz.hour(thetime)) + ":" + String(tz.minute(thetime)) + ":" + String(tz.second(thetime));
}

String TTS(int thetime)
{
  int hours = thetime / (60 * 60);
  int remainder = thetime - hours *60*60;
  int minutes = remainder / 60;
  remainder -= minutes * 60;
  return String(hours) + ":" + String(minutes) + ":" + String(remainder);
}

void setup()
{
  pinMode(RELAY_PIN_1, OUTPUT);
  digitalWrite(RELAY_PIN_1, LOW);
  pinMode(RELAY_PIN_2, OUTPUT);
  digitalWrite(RELAY_PIN_2, LOW);
  pinMode(SENSOR_PIN_1, INPUT_PULLUP);
  pinMode(SENSOR_PIN_2, INPUT_PULLUP);

  Serial.begin(115200);

  //connect to WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(MY_SSID, MY_WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(100);
    Serial.println("Waiting for wifi to connect...");
  }
  WiFi.setSleepMode(WIFI_LIGHT_SLEEP); //??
  Serial.println("Connected to WiFi!");
  DebugPrint("Setup complete\n");

  waitForSync();
  myTZ.setLocation("America/New_York");
}

void loop()
{
  waitForSync();
  DebugPrint("My time: " + String(myTZ.dateTime()) + "\n");
  DebugPrint("Current time 1: " + TimeToString(myTZ) + "\n");

  //time_t now = myTZ.now();
  time_t now = myTZ.tzTime(TIME_NOW, LOCAL_TIME);
  DebugPrint("NOW = " + TimeToString(myTZ, now) + "\n");
  //nowTime is in local time
  int nowTime = HMSToTime(myTZ.hour(), myTZ.minute(), myTZ.second());
  DebugPrint("NOW2 = " + TTS(nowTime) + "\n");

  //determine which schedule we should use
  Schedule* schedule = myTZ.weekday() >= MONDAY && myTZ.weekday() <= FRIDAY ? &WeekdaySchedule : &WeekendSchedule;
  
  bool leaveOpen = false;
  int nextCloseTime = -1;
  for (int i = 0; i < schedule->m_NumSchedules; i++)
  {
    if ((schedule->m_OpenTime[i] < schedule->m_CloseTime[i] && schedule->m_OpenTime[i]  < nowTime && nowTime < schedule->m_CloseTime[i]) ||
        (schedule->m_OpenTime[i] > schedule->m_CloseTime[i] && schedule->m_CloseTime[i] < nowTime && nowTime < schedule->m_OpenTime[i] ))
    {
      leaveOpen = true;
      nextCloseTime = schedule->m_CloseTime[i];
      break;
    }
  }
  
  if (leaveOpen)
  {
    //should be open - just sleep until next close time
    DebugPrint("Doors can remain open right now\n");
    //how long until the next close time???
    int sleepTime = -1;
    if (nextCloseTime > nowTime)
      sleepTime = nextCloseTime - nowTime;
    else
      sleepTime = HMSToTime(24, 0, 0) - nowTime + nextCloseTime;
    sleepTime += 5000; //just so we will definitely be over the threshold for the close time
    DebugPrint("Sleeping for " + String(sleepTime) + " seconds (until)" + TTS(nextCloseTime));
    //TODO: deep sleep?
    //ESP.deepSleep(sleepTime * 1000);
  }
  else
  {
    //should be closed - close the door (if need be)
    DebugPrint("Doors should be closed now!!\n");
    int sensor1 = digitalRead(SENSOR_PIN_1);
    int sensor2 = digitalRead(SENSOR_PIN_2);
    if (sensor1 == HIGH && sensor2 == HIGH)
    {
      //both doors are closed - nothing to do
      //TODO: deep sleep....forever? or until next close time?
      delay(60 * 5 * 1000);
      return;
    }
    //allow the doors to stay open for a bit
    delay(AllowToStayOpenTimeMS);

    //make sure they weren't closed in the mean time
    sensor1 = digitalRead(SENSOR_PIN_1);
    sensor2 = digitalRead(SENSOR_PIN_2);
    if (sensor1 == LOW)
    {
      DebugPrint("YOU LEFT DOOR 1 OPEN!!\n");
      digitalWrite(RELAY_PIN_1, HIGH);
    }
    if (sensor2 == LOW)
    {
      DebugPrint("YOU LEFT DOOR 2 OPEN!!\n");
      digitalWrite(RELAY_PIN_2, HIGH);
    }
    delay(200);
    digitalWrite(RELAY_PIN_1, LOW);
    digitalWrite(RELAY_PIN_2, LOW);
    //now sleep for a bit
  }
  
  //digitalWrite(RELAY_PIN_1, HIGH);
  //delay(1000);
  //digitalWrite(RELAY_PIN_1, LOW);
  delay(60 * 5 * 1000);
  //ESP.deepSleep(2 * 1000000);
}
