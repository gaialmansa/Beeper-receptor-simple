#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <local.h>
#include <esp_sleep.h> // Necesario para funciones de sleep

/*

En este proyecto haremos todas las funciones que va a necesitar un beeper que no lleva pantalla.
*Prender/apagar un led numerado de 1 a 5
*Activar/desactivar el zumbador
*Detectar 5 segundos de pulsación del pulsador
*Leer el estado de la batería
*Inicializar la wifi
*Comprobar el registro en el servidor
*Preguntar al API si hay mensajes nuevos
*Comprobar el estado de la conexion wifi
*/

// Declaración de variables
volatile unsigned long buttonPressStartTime = 0; // Tiempo en que el botón fue presionado
volatile bool buttonPressed = false; // Flag para saber si el botón está actualmente presionado
volatile bool longPressDetected = false; // Flag para indicar que se detectó una pulsación larga
const unsigned long longPressThreshold = 500; // Umbral de pulsación larga en milisegundos (1/2 segundo)
int user = 0 ;    // identificdor del usuario.
int pcbat = 0;    // estado de la bateria. Se intenta que solo grabe cuando el estado cambie

WiFiUDP ntpUDP;             // instanciamos WiFi
HTTPClient apicall;         // cliente HTTP para llamar al API   
DynamicJsonDocument doc(1024); // calculamos un k para la respuesta del api

//Declaración de funciones
void prenderLed(int);
void apagarLed(int);
void ledTest();
void setupPins();
void drums();
void chas(int);
void silencio(int);
void pulsadoBoton();
String Api(char metodo[], String parametros[], int numparam);
void regSys();
void chkMsg();
void hayMensajeNuevo();
void WiFiStart();
void setupAdcForBattery();
float getBatteryVoltage();
int getBatteryPercentage();
void grabarEstadoBateria();


// Definiciones 
#define BON prenderLed(7);
#define BOFF apagarLed(7);
#define buzzer 7  // definicion de los leds de colores
#define azul 8 
#define amarillo 9
#define verde 10
#define blanco 20
#define rojo 21
#define bateria 0   // entrada de la medicion de carga
#define umbral  40  // umbral estimado para la carga de la batería
#define pulsador 6  // pulsador lateral de OIDOCOCINA
//#define _DEBUG    // si se activa para depuracion, hay que sistituir la linea
                    //  esp_light_sleep_start(); // Inicia el Light Sleep
                    // del bucle loop por un delay()
#ifndef DEFAULT_VREF
#define DEFAULT_VREF 1100 // Default Vref en mV para ADC del ESP32
#endif

const int ADC_PIN = 0; // GPIO0 es el pin ADC que estás usando
const int ADC_RESOLUTION_BITS = 12; // Resolución de 12 bits (0-4095)
// --- CONFIGURACIÓN DEL DIVISOR DE TENSIÓN ---
// ¡¡AJUSTA ESTOS VALORES A TUS RESISTENCIAS REALES!!
const float R1_DIVIDER_OHMS = 100000.0; // Resistencia superior (conectada de Bateria+ al pin ADC)
const float R2_DIVIDER_OHMS = 47000.0;  // Resistencia inferior (conectada del pin ADC a GND)
// Calculamos la relación del divisor: (R1+R2)/R2. Usamos float para precisión.
const float DIVIDER_RATIO = (R1_DIVIDER_OHMS + R2_DIVIDER_OHMS) / R2_DIVIDER_OHMS;
// --- CALIBRACIÓN DE BATERÍA (para una LiPo de 3.7V nominal) ---
const float BATTERY_FULL_VOLTAGE = 4.20; // Voltaje de la batería cuando está 100% cargada
const float BATTERY_EMPTY_VOLTAGE = 3.20; // Voltaje mínimo de la batería (0% de carga útil, ¡no la descargues más!)

void setup() 
{
  #ifdef _DEBUG
    Serial.begin(115200);
    Serial.println("Iniciando ESP32...");
    delay(5000);
    Serial.println("Iniciando ESP32...");
  #endif
  setupPins();  // Definimos los pines de entrada y salida
  attachInterrupt(pulsador,pulsadoBoton,CHANGE);    // asociamos la interrupcion del pulsador
  WiFiStart();    // inicializamos la wifi
  apicall.setTimeout(5000);   // establecemos el tiempo maximo antes de dar un timeout al conectar con el API
  setupAdcForBattery();   // inicializamos el ADC para leer el estado de la bateria
  regSys();     // Chequeamos hasta que esta registrado en el sistema
}
void loop() 
{
  grabarEstadoBateria();
  chkMsg();
  apagarLed(1);
  // Entrar en Light Sleep por 10 segundos (10000 milisegundos)
   #ifdef _DEBUG
      delay(5000);
    #else
      esp_sleep_enable_timer_wakeup(10000000ULL); // 10,000,000 microsegundos = 10 segundos
      esp_light_sleep_start(); // Inicia el Light Sleep
    #endif
  // --- El código se reanuda aquí después de despertar ---
  // WiFi.forceSleepWake(); // No siempre necesario, pero puede ayudar en algunos casos.
  // WiFi.reconnect(); // No se recomienda usar esto con Light Sleep, ya que la conexión debería persistir.
}
void IRAM_ATTR pulsadoBoton() // ISR Para el pulsador
{
  unsigned long currentTime = millis(); // Obtiene el tiempo actual

  // Si el botón ha sido presionado (estado LOW, asumiendo PULLUP)
  if (digitalRead(pulsador) == LOW) 
  {
    if (!buttonPressed) 
    { // Si no estaba presionado antes (flanco de bajada)
      buttonPressStartTime = currentTime; // Registra el tiempo de inicio de la pulsación
      buttonPressed = true; // Marca como presionado
      longPressDetected = false; // Resetea la detección de pulsación larga
    }
  }
  // Si el botón ha sido soltado (estado HIGH)
  else 
  {
    if (buttonPressed) 
    { // Si estaba presionado antes (flanco de subida)
      unsigned long pressDuration = currentTime - buttonPressStartTime; // Calcula la duración de la pulsación
      if (pressDuration >= longPressThreshold) 
      {
        longPressDetected = true; // Marca que fue una pulsación larga
       // #ifdef _DEBUG
       //   Serial.println("interrupcion. Detectada pulsacion larga");
       // #endif
      }
      buttonPressed = false; // Marca como no presionado
    }
  }
}
/*************************** Control de la Batería */
void grabarEstadoBateria()  // graba a través del Api el estado de la bateria
{
  int currentBatPc = getBatteryPercentage();
  String parms[2];
  String salida;
  if (currentBatPc != pcbat)  // o es la primera vez o hay un cambio de valor
  {
    pcbat = currentBatPc;   // actualizamos el valor de la variable externa
    parms[0] = "id_usuario=" + String(user);  // primer parametro
    parms[1] = "pcbat=" + String(pcbat);      // segundo parametro
    salida = Api("exrtime",parms,2);                   // llamada al Api
  }
  if(pcbat <= umbral)   // si alcanzamos el umbral de carga para la batería
  {
    prenderLed(rojo);   // enciende la bombilla roja.
    chas ((umbral-pcbat)*100);  // un pitido cada vez más largo
  }
  #ifdef _DEBUG
    Serial.print("Bateria: ");
    Serial.println(pcbat);
    Serial.print("Salida Api: ");
    Serial.println(salida);
  #endif

}
float getBatteryVoltage() // obtiene el voltaje en la bateria
{
  uint32_t adc_reading = 0;
  // Promediar múltiples lecturas para obtener un valor más estable y reducir el ruido.
  for (int i = 0; i < 64; i++) {
    adc_reading += analogRead(ADC_PIN); // <-- Usamos analogRead()
  }
  adc_reading /= 64; // Calcula el promedio

  // Convertir el valor RAW del ADC a milivoltios (mV).
  // La referencia de 3.3V es el voltaje máximo que el ADC puede leer con esta atenuación.
  float measured_voltage_on_pin = (float)adc_reading * (3.3 / (float)((1 << ADC_RESOLUTION_BITS) - 1)); // (3.3V / 4095)
  // Nota: La calibración interna del ESP32-C3 es compleja. 3.3V es una aproximación.
  // Para mayor precisión, algunos usan un Vref de 1.1V y ajustan por la atenuación.
  // Pero con analogRead, el framework intenta abstraer esto.

  float battery_voltage = measured_voltage_on_pin * DIVIDER_RATIO; // Voltaje real de la batería

  return battery_voltage;
}
int getBatteryPercentage() // Estima el porcentaje de carga de la bateria
{
 float voltage = getBatteryVoltage(); // Obtiene el voltaje actual de la batería

  // Si el voltaje está por encima del máximo, se considera 100%
  if (voltage >= BATTERY_FULL_VOLTAGE) {
    return 100;
  }
  // Si el voltaje está por debajo o igual al mínimo, se considera 0%
  if (voltage <= BATTERY_EMPTY_VOLTAGE) {
    return 0;
  }

  // Mapear linealmente el voltaje a un porcentaje entre 0 y 100.
  // Multiplicamos por 1000 para trabajar con enteros grandes en map(), lo que puede ser más estable.
  int percentage = map(static_cast<long>(voltage * 1000), // Valor a mapear (en mV para map())
                       static_cast<long>(BATTERY_EMPTY_VOLTAGE * 1000), // Inicio del rango de entrada (en mV)
                       static_cast<long>(BATTERY_FULL_VOLTAGE * 1000),  // Fin del rango de entrada (en mV)
                       0, 100); // Rango de salida (0-100%)

  // Asegurar que el porcentaje se mantenga dentro del rango válido [0, 100]
  return constrain(percentage, 0, 100);
}
/************************** WIFI y conexion al API */
void WiFiStart() // Inicializa la conexion
{
  String mensaje;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true); // true = borrar configuración, true = borrar NVS
  delay(100); // Pequeña pausa
  WiFi.begin(ssid, pass);
  mensaje = "Intentando conectar a "+String(ssid);  // los parametros de la conexion se leen en local.h
  delay(500);
  #ifdef _DEBUG
    Serial.println(mensaje);
  #endif
  while (WiFi.status() != WL_CONNECTED) 
   {
    delay(500);
    #ifdef _DEBUG
      Serial.println(mensaje);
      Serial.print("WiFi Status: "); Serial.println(WiFi.status());
    #endif
    }
    mensaje = "Conectado a "+String(ssid) + "Macaddress: "+WiFi.macAddress();
    #ifdef _DEBUG
      Serial.println(mensaje);
    #endif 
    prenderLed(azul);   // eso es que estamos felizmente conectados
}
void chkMsg() // Consulta el API para ver si hay mensajes no vistos para el usuario. Si hay varios mensajes pendientes, los recupera todos.
{
  String User[1];
  User[0] = "id_usuario=" + String(user);
  while (true)
  {
  //Api("exrtime",User,1);
  Api("mnv",User,1);  // llamamos al api para recuperar el primer mensaje sin leer del usuario
  if(doc.containsKey("mensaje") ) // esto es que hay mensaje
   {
    hayMensajeNuevo();
   }
  return;
  }
}
void hayMensajeNuevo() //enciende la pantalla y enseña el mensaje que se acaba de leer. Al terminar lo marca en la API como leido.Se queda en bucle hasta que se pulsa el joystick
{
  int anterior;
  String retApi;
  int ledindex;
  retApi = doc["id"].as<String>();
  ledindex = doc["mensaje"].as<String>()[0] - 40;
  Serial.println(ledindex);
  //if (ledindex != 9 and ledindex != 10 and ledindex != 11)  // error. Admitimos solo estos tres supuestos
  //  return; // ni caso 
  if(ledindex == 11)  // corregimos para el caso del led blanco, que no es correlativo
    ledindex = 20;
  #ifdef _DEBUG
    Serial.print("Haymensajenuevo. Ledindex = ");
    Serial.println(ledindex);
  #endif
  prenderLed(ledindex); // endendemos el led
  while ( not longPressDetected)  // hasta que pulsen el boton un segundo
    drums();        // musica infernal
  String parms[1];
  parms[0] = "id=" + doc["id"].as<String>();
  anterior =0;  // opcion anteriormente seleccionada
  longPressDetected = false;  // dejamos la opcion de nuevo en falso
  apagarLed(ledindex);  // apagamos el led
  String p[1];
  p[0] = "id_mensaje=" + doc["id_mensaje"].as<String>(); // con el doc de la lectura del mensaje, guardamos para la llamada de comprobacion
  String flag = Api("mver",parms,1);    // marca el mensaje como visto
  Serial.print("Flag: ");
  Serial.println(flag);
  while (flag.equals("-1"))
    {    
       WiFiStart();
       flag = Api("mver",parms,1);    // marca el mensaje como visto
    }
  }
void regSys() //Registra al usuario en el sistema.
{
  String nombre,usuario = "null";
  String mac[1];
  mac[0] = "mac=";
  mac[0] += WiFi.macAddress();
  #ifdef _DEBUG
    Serial.print("Registrando el dispositivo ");
    Serial.println(WiFi.macAddress());
  #endif
  while (usuario == "null") // repetiremos hasta que el usuario tenga un valor
  {
    Api("bregister",mac,1);
    usuario = doc["usuario"].as<String>();
    nombre = doc["nombre" ].as<String>();
    user = doc["id_usuario"].as<int>();
    delay(1000);  // esperamos un segundo
  }
  #ifdef _DEBUG
    Serial.println("**");
    Serial.println("Registrado para el usuario " + nombre);
  #endif
} 
String Api(char metodo[], String parametros[],int numparam) //Hace una llamada al metodo del Api indicado con los parametros que van en el array Los parametros van en formato 'paramname=paramvalue'
{
   int f, responsecode;
  String postData, url, payload;
  DeserializationError error;   // por si esta mal formada la respuesta
  postData = parametros[0];
  if(numparam >1)
  {
    for (f = 1; f < numparam; f++) // concatenamos los elementos pasados en parametros en el formato parm1=valor1&param2=valor2....
      {
        postData += "&";
        postData += parametros[f];
      }
  }
  //borrame = postData;
  url = _URL;
  url += metodo; // con esto queda formada la url del API con su metodo al final
  apicall.begin(url);    // iniciamos la llamada al api
  apicall.addHeader("Content-Type", "application/x-www-form-urlencoded"); // Especificamos el header content-type
  #ifdef _DEBUG
    Serial.println(url);
    Serial.println(postData);
  #endif
  responsecode = 0;
  while (responsecode !=200)
  {
    responsecode = apicall.POST(postData);  // llamada a la API
    if(responsecode != 200) // control de error
    {
      #ifdef _DEBUG
        Serial.printf("Ha habido un error %d al comunicar con el api.\n",responsecode);
        Serial.println(url);
        Serial.println(postData);
      #endif
      apicall.end();
      WiFiStart();
      apicall.begin(url);    // iniciamos la llamada al api
      apicall.addHeader("Content-Type", "application/x-www-form-urlencoded"); // Especificamos el header content-type
    }
  }
  payload = apicall.getString();  // obtenemos la respuesta
  apicall.end();  /// fin de la llamada
  delay(200);
  #ifdef _DEBUG
    Serial.println(payload);
  #endif
  error = deserializeJson(doc,payload);   // deserializamos la respuesta y la metemos en el objeto doc
  return(payload);
}
/***************************** Leds y zumbador */
void drums()  // Toca un remedo del tradicional tono de triaje
{
  int n = 400;
  chas(3*n/5);
  silencio(n);
  chas(n/3);
  silencio(n/4);
  chas(n/2);
  silencio(n/4);
  chas(n/2);
  silencio(n/4);
  silencio(n);
  chas(n/2);
  silencio(n/4);
  chas(n/2);
  silencio(n);
  delay (1000);
}
void chas(int n)  // Pita n microsegundos
{

  if(longPressDetected) // si se detectó la pulsacion larga, detenemos la reproduccion inmediatamente
    {
      #ifdef _DEBUG
        Serial.println("se detecto pulsacion. Abortando la musica");
      #endif
      return;
    }
    
  BON
  delay(n);
  BOFF
}
void silencio(int n)  // Es simplemente un delay(n)
{
  delay(n);
}
void prenderLed(int numled) // Enciende un led
{
  if(numled == azul)
    ledcWrite(0,16);
  else
    digitalWrite(numled, HIGH);
}
void apagarLed(int numled)  // Apaga un led
{
  digitalWrite(numled, LOW);
}
void ledTest()  // Chequea las conexiones del buzzer y todos los leds
{
  int f;
  for(f=7;f<11;f++)
    {
      prenderLed(f);
      delay(300);
      apagarLed(f);
    }
  prenderLed(20);
  delay(300);
  apagarLed(20);
  prenderLed(21);
  delay(300);
  apagarLed(21);
}
/******************************** Setup****************/
void setupPins()  // hace el setup de los pines. Usa los numeros a capon (no funcionara si no se respeta la numeracion original)
{
   // Salidas
  pinMode(buzzer, OUTPUT); // Buzzer
  //pinMode(azul, OUTPUT); // led Azul
  ledcSetup(0, 5000, 8);
  ledcAttachPin(azul,0);
  pinMode(amarillo, OUTPUT); // led Amarillo
  pinMode(verde, OUTPUT);// led Verde 
  pinMode(blanco, OUTPUT);//led Blando
  pinMode(rojo, OUTPUT);//led Rojo
  //Entradas
  pinMode(bateria, INPUT); // Batería

  pinMode(pulsador, INPUT_PULLUP); // Pulsador
}
void setupAdcForBattery() // Prepara el ADC para leer el estado de la bateria
{
 analogReadResolution(ADC_RESOLUTION_BITS);
 analogSetAttenuation(static_cast<adc_attenuation_t>(3));
}
