/** MQTT Publish von Sensordaten */
#include "mbed.h"
#include "OLEDDisplay.h"

#if MBED_CONF_IOTKIT_HTS221_SENSOR == true
#include "HTS221Sensor.h"
#endif
#if MBED_CONF_IOTKIT_BMP180_SENSOR == true
#include "BMP180Wrapper.h"
#endif

#ifdef TARGET_K64F
#include "QEI.h"

//Use X2 encoding by default.
QEI wheel (MBED_CONF_IOTKIT_BUTTON2, MBED_CONF_IOTKIT_BUTTON3, NC, 624);
#endif

#include <MQTTClientMbedOs.h>
#include <MQTTNetwork.h>
#include <MQTTClient.h>
#include <MQTTmbed.h> // Countdown

// Sensoren wo Daten fuer Topics produzieren
static DevI2C devI2c( MBED_CONF_IOTKIT_I2C_SDA, MBED_CONF_IOTKIT_I2C_SCL );
#if MBED_CONF_IOTKIT_HTS221_SENSOR == true
static HTS221Sensor hum_temp(&devI2c);
#endif
#if MBED_CONF_IOTKIT_BMP180_SENSOR == true
static BMP180Wrapper hum_temp( &devI2c );
#endif
DigitalIn button( MBED_CONF_IOTKIT_BUTTON1 );

// Topic's publish
char* topicTEMP = (char*) "iotkit/sensor";
char* topicALERT = (char*) "iotkit/alert";
char* topicBUTTON = (char*) "iotkit/button";
char* topicENCODER = (char*) "iotkit/encoder";
// Topic's subscribe
char* topicActors = (char*) "iotkit/actors/#";
// MQTT Brocker
char* hostname = (char*) "cloud.tbz.ch";
int port = 1883;
// MQTT Message
MQTT::Message message;
// I/O Buffer
char buf[100];

// UI
OLEDDisplay oled( MBED_CONF_IOTKIT_OLED_RST, MBED_CONF_IOTKIT_OLED_SDA, MBED_CONF_IOTKIT_OLED_SCL );

// Aktore(n)
PwmOut speaker( MBED_CONF_IOTKIT_BUZZER );

DigitalOut led1( MBED_CONF_IOTKIT_LED1 );
DigitalOut led2( MBED_CONF_IOTKIT_LED2 );
DigitalOut led3( MBED_CONF_IOTKIT_LED3 );
DigitalOut led4( MBED_CONF_IOTKIT_LED4 );


/** Hilfsfunktion zum Publizieren auf MQTT Broker */
void publish( MQTTNetwork &mqttNetwork, MQTT::Client<MQTTNetwork, Countdown> &client, char* topic )
{
    MQTT::Message message;    
    oled.cursor( 2, 0 );
    oled.printf( "Topi: %s\n", topic );
    oled.cursor( 3, 0 );    
    oled.printf( "Push: %s\n", buf );
    message.qos = MQTT::QOS0;
    message.retained = false;
    message.dup = false;
    message.payload = (void*) buf;
    message.payloadlen = strlen(buf)+1;
    client.publish( topic, message);  
}

/** Daten empfangen von MQTT Broker */
void messageArrived( MQTT::MessageData& md )
{
    float value;
    MQTT::Message &message = md.message;
    printf("Message arrived: qos %d, retained %d, dup %d, packetid %d\n", message.qos, message.retained, message.dup, message.id);
    printf("Topic %.*s, ", md.topicName.lenstring.len, (char*) md.topicName.lenstring.data );
    printf("Payload %.*s\n", message.payloadlen, (char*) message.payload);
    
    if  ( strncmp( (char*) md.topicName.lenstring.data + md.topicName.lenstring.len - 3, "led", 3) == 0  
        && message.payloadlen >= 2) {

        DigitalOut * ledN;
        switch(((char*) message.payload)[0]) {
            case '1': ledN = &led1; printf("LED1\n"); break;
            case '2': ledN = &led2; printf("LED2\n"); break;
            case '3': ledN = &led3; printf("LED3\n"); break;
            case '4': ledN = &led4; printf("LED4\n"); break;
            default: break;
        }

        if(((char*) message.payload)[1] == '1') {
            *ledN = 1;
        }else{
            *ledN = 0;
        }
    }

}

/** Hauptprogramm */
int main()
{
    uint8_t id;
    float temp, hum;
    int encoder;
    
    oled.clear();
    oled.printf( "MQTTPublish\r\n" );
    oled.printf( "host: %s:%s\r\n", hostname, port );

    printf("\nConnecting to %s...\n", MBED_CONF_APP_WIFI_SSID);
    oled.printf( "SSID: %s\r\n", MBED_CONF_APP_WIFI_SSID );
    
    // Connect to the network with the default networking interface
    // if you use WiFi: see mbed_app.json for the credentials
    WiFiInterface *wifi = WiFiInterface::get_default_instance();
    if ( !wifi ) 
    {
        printf("ERROR: No WiFiInterface found.\n");
        return -1;
    }
    printf("\nConnecting to %s...\n", MBED_CONF_APP_WIFI_SSID);
    int ret = wifi->connect( MBED_CONF_APP_WIFI_SSID, MBED_CONF_APP_WIFI_PASSWORD, NSAPI_SECURITY_WPA_WPA2 );
    if ( ret != 0 ) 
    {
        printf("\nConnection error: %d\n", ret);
        return -1;
    }    

    // TCP/IP und MQTT initialisieren (muss in main erfolgen)
    MQTTNetwork mqttNetwork( wifi );
    MQTT::Client<MQTTNetwork, Countdown> client(mqttNetwork);

    printf("Connecting to %s:%d\r\n", hostname, port);
    int rc = mqttNetwork.connect(hostname, port);
    if (rc != 0)
        printf("rc from TCP connect is %d\r\n", rc); 

    // Zugangsdaten - der Mosquitto Broker ignoriert diese
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.MQTTVersion = 3;
    data.clientID.cstring = (char*) wifi->get_mac_address(); // muss Eindeutig sein, ansonsten ist nur 1ne Connection moeglich
    data.username.cstring = (char*) wifi->get_mac_address(); // User und Password ohne Funktion
    data.password.cstring = (char*) "password";
    if ((rc = client.connect(data)) != 0)
        printf("rc from MQTT connect is %d\r\n", rc);           

    // MQTT Subscribe!
    client.subscribe( topicActors, MQTT::QOS0, messageArrived );
    printf("MQTT subscribe %s\n", topicActors );
    
    /* Init all sensors with default params */
    hum_temp.init(NULL);
    hum_temp.enable(); 
    led1 = 0;led2 = 0;led3 = 0;led4 = 0;

    while   ( 1 ) 
    {
        // Temperator und Luftfeuchtigkeit
        hum_temp.read_id(&id);
        hum_temp.get_temperature(&temp);
        hum_temp.get_humidity(&hum);    
        sprintf( buf, "0x%X,%2.2f,%2.1f", id, temp, hum ); 

        publish( mqttNetwork, client, topicTEMP );
        
        // Button (nur wenn gedrueckt)
        if  ( button == 0 )
        {
            sprintf( buf, "ON" );
            publish( mqttNetwork, client, topicBUTTON );
        }

#ifdef TARGET_K64F

        // Encoder
        encoder = wheel.getRevolutions();
        // wheel.getRevolutions();
        sprintf( buf, "%d", encoder );
        publish( mqttNetwork, client, topicENCODER );
#endif

        client.yield    ( 1000 );                   // MQTT Client darf empfangen
        thread_sleep_for( 500 );
    }

    // Verbindung beenden
    if ((rc = client.disconnect()) != 0)
        printf("rc from disconnect was %d\r\n", rc);

    mqttNetwork.disconnect();    
}
