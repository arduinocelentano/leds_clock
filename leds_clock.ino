#include <DS1302.h> //бібліотека для роботи з DS1302
#include <FastLED.h> //бібліотека для управління "розумними" діодами
#include <DHT.h> //бібліотека для взаємодії з датчиком DHT11
#include <U8g2lib.h> //бібліотека для виводу на OLED дисплей

#define LED_PIN 11 //пін піключення діодного кільця
#define NUM_LEDS 16 //кількість діодів у кільці
#define BTNMINUS_PIN 4 //пін кнопки "-"
#define BTNPLUS_PIN 3 //пін кнопки "+"
#define BTNSET_PIN 2 //пін кнопки "SET"
#define DHT_PIN 8 //пін датчика DHT11
#define INACTIVE_TIME 255 //Максимальна кількість ітерацій головного циклу бездіяльності користувача (якщо під час встановлення часу, користувач довго нічого не натискає, годинник переходить в основний режим)

enum Modes {NORMAL, SETTIME}; //режими: основний та режим встановлення часу
enum Submodes {SET_HOUR, SET_MIN, SET_SEC, SET_DAY, SET_MON, SET_YEAR, SET_DOW}; //підрежимим режиму встановлення часу
const char week[][19] = {"понеділок","вівторок","середа","четвер","п'ятниця","субота","неділя"}; //дні тижня для виводу на екрані

U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0); //об'єкт для управління дисплеєм
DHT dht(DHT_PIN, DHT11); //об'єкт для управління датчиком DHT11
DS1302 rtc(10, 12, 13); //об'єкт для управління годинником реального часу
CRGB leds[NUM_LEDS]; //об'єкт для управління діодним кільцем
// три масиви станів світлодіодів для відображення годин/хвилин/секунд (вони будуть впливати на різні кольорові канали)
byte hour_leds[NUM_LEDS];
byte min_leds[NUM_LEDS]; 
byte sec_leds[NUM_LEDS]; 
byte hour_led, min_led, sec_led; //центри "стрілок"
byte current_hour, current_min, current_sec, current_day, current_mon, current_year, current_dow;//поточні значення, введені користувачем в режимі налаштування
bool btnset_state, btnplus_state, btnminus_state; //стани кнопок
byte inactive_time = INACTIVE_TIME; //кількість ітерацій, протягом яких не була натиснена жодна кнопка
enum Modes mode = NORMAL; //поточний режим
enum Submodes submode = SET_HOUR; //поточний підрежим
Time t; //час
unsigned char animation_frame; //номер поточного "кадру" анімації
unsigned int red, blue, animation_speed; //значення кольорових каналів для створення анімації
float temperature=0, humidity=0; //температура й вологість

/*
 * Оператор інкременту для зручного переключення підрежимів
*/
Submodes& operator++(Submodes& submode)
{
  switch(submode) {
    case SET_HOUR : return submode = SET_MIN;
    case SET_MIN : return submode = SET_SEC;
    case SET_SEC : return submode = SET_DAY;
    case SET_DAY : return submode = SET_MON;
    case SET_MON : return submode = SET_YEAR;
    case SET_YEAR : return submode = SET_DOW;
    case SET_DOW : return submode = SET_HOUR;
  }
}

/*
 * Екран виводу дня тижня
*/
void printDOW ()
{
  if ((t.dow<1)||(t.dow>7))
    return;
  //виведення дати
  u8g2.setFont(u8g2_font_logisoso18_tr);
  u8g2.drawStr(0,18,rtc.getDateStr());
  //і дня тижня
  u8g2.setFont(u8g2_font_unifont_t_cyrillic);  
  u8g2.setCursor(0, 30);
  u8g2.print(week[t.dow-1]);
  u8g2.sendBuffer();
}

/*
 * Екран виводу температури
*/
void printTemp ()
{
  char line [] = "XXC YY%";
  //округлення значень температури й вологості
  int intTemp = (int)round(temperature);
  int intHum = (int)round(humidity);
  //конвериація цифр у ASCII коди
  line[0] = intTemp/10+48;
  line[1] = intTemp%10+48;
  line[4] = intHum/10+48;
  line[5] = intHum%10+48;
  //виведення на екран
  u8g2.setFont(u8g2_font_logisoso28_tr);
  u8g2.drawStr(0,32,line);
  u8g2.sendBuffer();
}

/*
 * Ініціалізація пристрою
 */
void setup() {
  mode = NORMAL; //одразу після запуску переходимо в головний режим
  rtc.halt(false); // запуск годинника реального часу
  pinMode(LED_PIN, OUTPUT); //режим піну діодного кільця - виведення
  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS); //ініціалізація діодного кільця
  animation_frame = 0; // стартовий "кадр" анімації
  red = 0; //стартове значення червогого каналу на початку анімації
  blue = 16; //стартове значення синього каналу на початку анімації
  animation_speed = 1; //швидкість анімації
  //ініціалізація дисплея
  u8g2.begin();
  u8g2.enableUTF8Print();
  //ініціалізація пінів кнопок
  pinMode(BTNPLUS_PIN, INPUT_PULLUP);
  pinMode(BTNMINUS_PIN, INPUT_PULLUP);
  pinMode(BTNSET_PIN, INPUT_PULLUP);
  btnset_state = false;
  inactive_time = INACTIVE_TIME; //кількість ітерацій неактивності користувача
  dht.begin(); //ініціалізація DHT11
}

/*
 * Головний цикл
 */
void loop() {
  //Зчитування часу та обчислення, які світлодіоди слід активувати
  t = rtc.getTime();
  hour_led = (NUM_LEDS*(t.hour%12)+NUM_LEDS*t.min/60)/12;
  min_led = (NUM_LEDS*t.min+NUM_LEDS*t.sec/60)/60;
  sec_led = (NUM_LEDS*t.sec)/60;

  /// -=Оновлення світлодіодного кільця=-
  //Заповнення масивів станів діодів
  //Більші значення відповідають більшій яскравості
  if(mode==NORMAL){
    for (byte i=0; i<NUM_LEDS; i++){
      if (i==hour_led)
        hour_leds[i] = 80;
      else 
        hour_leds[i] = 0;
      if (i==min_led)
        min_leds[i] = 40;
      else
        min_leds[i] = 0;
      if (i==sec_led && (animation_frame<5))
        sec_leds[i] = 20;
      else 
        sec_leds[i] = 0;
    }
    //Годинникова "стрілка" позначається трьома діодами, тому вмикаємо два сусідніх світлодіоди з меншою яскравістю
    if(hour_led==0){
      hour_leds[15] = 10;
      hour_leds[1] = 10;
    }
    else if (hour_led==15){
      hour_leds[14] = 10;
      hour_leds[0] = 10;
    }
    else{
      hour_leds[hour_led - 1] = 10;
      hour_leds[hour_led + 1] = 10;
    }
    //Оновлюємо стан світлодіодного кільця
    for (int i = 0; i<NUM_LEDS; i++)
      if (i<=animation_frame)
          leds[NUM_LEDS-i-1] = CRGB((red>>(animation_frame-i))+min_leds[i], hour_leds[i]+min_leds[i]+sec_leds[i] ,(blue>>(animation_frame-i))+sec_leds[i]);
        else
          leds[NUM_LEDS-i-1] = CRGB(min_leds[i], hour_leds[i]+min_leds[i]+sec_leds[i], sec_leds[i]);
      FastLED.show();

   /// -=Оновлення дисплею=-
   u8g2.clearBuffer(); // очистка внутрішнього буферу
   //Відображення одного з екранів
   switch (t.sec/6){
    case 2: 
    case 6: {
      printDOW();//відображення екрану дня тижня
      break;
    case 4:
    case 8:
      printTemp();//відображення екрану клавіатури
      break;
            }
    default:{
      u8g2.setFont(u8g2_font_logisoso28_tr);
      u8g2.drawStr(0,32,rtc.getTimeStr());//відображення екрану часу
    }
   }
   u8g2.sendBuffer(); // виведення внутрішнього буферу на дисплей
  }
  else{//Якщо активний режим - режим встановлення часу
    //-=Оновлення діодного кільця=-
    for (int i = 0; i<NUM_LEDS; i++)
      if (i<=animation_frame)
          leds[NUM_LEDS-i-1] = CRGB((red>>(animation_frame-i)), 0,(blue>>(animation_frame-i)));
        else
          leds[NUM_LEDS-i-1] = CRGB(0, 0, 0);
    FastLED.show();
    //-=Оновлення дисплею=-
    char line[19] = "HH:MM:SS";
    //три підрежими встановлення часу
    if((submode==SET_HOUR)||(submode==SET_MIN)||(submode==SET_SEC)){
      //конвертація цифр у ASCII коди
      line[0] = current_hour/10+48;
      line[1] = current_hour%10+48;
      line[3] = current_min/10+48;
      line[4] = current_min%10+48;
      line[6] = current_sec/10+48;
      line[7] = current_sec%10+48;
    }
    //підрежими встановлення дати
    else if((submode==SET_DAY)||(submode==SET_MON)||(submode==SET_YEAR))
    {
      //конвертація цифр у ASCII коди
      line[0] = current_day/10+48;
      line[1] = current_day%10+48;
      line[2] = '.';
      line[3] = current_mon/10+48;
      line[4] = current_mon%10+48;
      line[5] = '.';
      line[6] = current_year/10+48;
      line[7] = current_year%10+48;
    }
    else{
      strncpy(line,week[current_dow-1],18);//підрежим встановлення дня тижня
    }
    //-=блимання числа, яке в даний момент встановлюється=-
    switch (submode){
      case SET_HOUR:{
        if(animation_frame%2){
          line[0] = ' ';
          line[1] = ' ';
        }
        break;
      }
      case SET_MIN:{
        if(animation_frame%2){
          line[3] = ' ';
          line[4] = ' ';
        }
        break;
      }
      case SET_SEC:{
        if(animation_frame%2){
          line[6] = ' ';
          line[7] = ' ';
        }
        break;
      }
      case SET_DAY:{
        if(animation_frame%2){
          line[0] = ' ';
          line[1] = ' ';
        }
        break;
      }
      case SET_MON:{
        if(animation_frame%2){
          line[3] = ' ';
          line[4] = ' ';
        }
        break;
      }
      case SET_YEAR:{
        if(animation_frame%2){
          line[6] = ' ';
          line[7] = ' ';
        }
        break;
      }
      case SET_DOW:{
        if(animation_frame%2){
          strcpy(line,"");
        }
        break;
      }
    }
    //виведення на дисплей
    u8g2.clearBuffer();
    if (submode==SET_DOW){
      u8g2.setFont(u8g2_font_unifont_t_cyrillic);
      u8g2.setCursor(0, 24);
      u8g2.print(line);
    }
    else{
      u8g2.setFont(u8g2_font_logisoso28_tr);
      u8g2.drawStr(0,32,line);
    }
    u8g2.sendBuffer();
    //-= Обробка натиснення кнопки "+" =-
    if (digitalRead(BTNPLUS_PIN) == LOW)//кнопка натиснена, але не відпущена
      btnplus_state = true;
    else if (btnplus_state){
      inactive_time = INACTIVE_TIME;//скидання часу неактивності (кнопка щойно була натиснена)
      switch(submode){//Інкремент відповідного значення в залежності від підрежиму
        case SET_HOUR:{
          current_hour++;
          if (current_hour>23)
            current_hour = 0;
          break;
        }
        case SET_MIN:{
          current_min++;
          if (current_min>59)
            current_min = 0;
          break;
        }
        case SET_SEC:{
          current_sec++;
          if (current_sec>59)
            current_sec = 0;
          break;
        }
        case SET_DAY:{
          current_day++;
          if (current_day>31)
            current_day = 1;
          break;
        }
        case SET_MON:{
          current_mon++;
          if (current_mon>12)
            current_mon = 1;
          break;
        }
        case SET_YEAR:{
          current_year++;
          if (current_year>99)
            current_year = 1;
          break;
        }
        case SET_DOW:{
          current_dow++;
          if (current_dow>7)
            current_dow = 1;
          break;
        }
      }
      btnplus_state = false;
    }

    //-= Обробка натиснення кнопки "-" =-
    if (digitalRead(BTNMINUS_PIN) == LOW)//кнопка натиснена, але не відпущена
      btnminus_state = true;
    else if (btnminus_state){
      inactive_time = INACTIVE_TIME;//скидання часу неактивності (кнопка щойно була натиснена)
      switch(submode){//Декремент відповідного значення в залежності від підрежиму
        case SET_HOUR:{
          current_hour--;
          if (current_hour>127)//від'ємні значення (представлення в доповняльному коді)
            current_hour = 23;
          break;
        }
        case SET_MIN:{
          current_min--;
          if (current_min>127)//від'ємні значення (представлення в доповняльному коді)
            current_min = 59;
          break;
        }
        case SET_SEC:{
          current_sec--;
          if (current_sec>127)//від'ємні значення (представлення в доповняльному коді)
            current_sec = 59;
          break;
        }
        case SET_DAY:{
          current_day--;
          if (current_day<1)
            current_day = 31;
          break;
        }
        case SET_MON:{
          current_mon--;
          if (current_mon<1)
            current_mon = 12;
          break;
        }
        case SET_YEAR:{
          current_year--;
          if (current_year>127)//від'ємні значення (представлення в доповняльному коді)
            current_year = 99;
          break;
        }
        case SET_DOW:{
          current_dow--;
          if (current_dow<1)
            current_dow = 7;
          break;
        }
      }
      btnminus_state = false;
    }
    inactive_time--; //зменшення часу неактивності
    if (inactive_time==0){//Якщо час неактивності сплив, перемикання в основний режим 
      inactive_time = INACTIVE_TIME;
      mode = NORMAL;
      submode = SET_HOUR;
    }
  }

  //Перерахунок анімації
  animation_frame++;
  blue-=animation_speed;
  red+=animation_speed;
  if ((!red) || (!blue))
    animation_speed = -animation_speed;
  if (animation_frame>15)
    animation_frame = 0;

  //-= Обробка натиснення кнопки "SET" =-
  if (digitalRead(BTNSET_PIN) == LOW)//кнопка натиснена, але не відпущена
   btnset_state = true;
  else if (btnset_state){
    inactive_time = INACTIVE_TIME;//скидання часу неактивності (кнопка щойно була натиснена)
    if (mode==NORMAL){//Якщо кнопка натиснена в основному режимі, то перехід до режиму налаштування й копіювання поточних значень
      mode = SETTIME;
      submode = SET_HOUR;
      current_hour = t.hour;
      current_min = t.min;
      current_sec = t.sec;
      current_day = t.date;
      current_mon = t.mon;
      current_year = t.year%100;
      current_dow = t.dow;
    }
    else{
      submode++;//інакше перехід до наступного підрежиму
      if (submode==SET_YEAR){//перевірка, що введена користувачем дата не перевищує максимальну дату для обраного місяця
        if ((current_mon==2)&&(current_day>29))
          current_day=29;
        if (((current_mon==4)||(current_mon==6)||(current_mon==9)||(current_mon==11)) && (current_day>30))
          current_day=30;
      }
      if ((submode==SET_DOW)&&(current_year%4)&&(current_mon==2)&&(current_day>28))//перевірка для лютого невисокосного року
        current_day=28;
      if(submode==SET_HOUR){//Збереження дати й часу в годинник реального часу
        rtc.writeProtect(false);
        rtc.setDOW(current_dow); // запис дня тижня
        rtc.setTime(current_hour, current_min, current_sec); // запис часу
        rtc.setDate(current_day, current_mon, current_year+2000); // запис дати
        rtc.writeProtect(true);
        mode = NORMAL; //перехід в основний режим
      }
    }
    btnset_state = false;
  }

  //Зчитування з датчика значень температури й вологості
  if ((t.sec%10)==0){ ///!!!Для відладки раз на 10 секунд. Змініть це значення для економії енергії
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();
  }
    
  delay(15); //Пауза між кадрами анімації. Це значення не впливає на точність годинника, позаяк час зчитується з RTC
}

