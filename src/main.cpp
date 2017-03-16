#include <Arduino.h>
#include <Wire.h>
#include "lwm.h";
#include "lwm/sys/sys.h"
#include "lwm/nwk/nwk.h"
#include "config.h"

extern "C" {
void println (char *x) {
    Serial.println(x);
    Serial.flush();
}
}

// Invio: dongle -> coordinator
// cXXXX
// Invio: dongle -> dongle
// pXXXX
// Invio report dati: dongle -> coordinator
// r{<Address>:<Valore RSSI>[, <Address>:<Valore RSSI>]}
// (Invio con ack)

static void send_msg (uint16_t address);

static bool receive_msg (NWK_DataInd_t *ind);

byte pingCounter = 0;

void setup () {
    Serial.begin(115200);
    Serial.println("LWP Ping Demo");
    SYS_Init();
    NWK_SetAddr(DONGLE_ADDRESS);
    NWK_SetPanId(0x01);
    PHY_SetChannel(0x1a);
    PHY_SetRxState(true);
    NWK_OpenEndpoint(1, receive_msg);
}

void loop () {
    SYS_TaskHandler();
    if (DONGLE_ADDRESS != COORDINATOR_ADDRESS) {
        // Sono un dongle
        send_msg(COORDINATOR_ADDRESS);
        delay(300);
    } else {
        // Sono un coordinator
        delay(10);
    }
}

static void send_msg (uint16_t address) {
    pingCounter++;
    Serial.print("ping ");
    Serial.println(pingCounter);

    // we just leak for now @TODO pocca memoria
    NWK_DataReq_t *message = (NWK_DataReq_t *) malloc(sizeof(NWK_DataReq_t));
    message->dstAddr = address;
    message->dstEndpoint = 1;
    message->srcEndpoint = 1;
    message->options = 0;
    message->data = &pingCounter;
    message->size = sizeof(pingCounter);
    NWK_DataReq(message);
}

static bool receive_msg (NWK_DataInd_t *ind) {
    Serial.print("Received message - ");
    Serial.print("from: ");
    Serial.print(ind->srcAddr, DEC);

    Serial.print("  ");

    Serial.print("rssi: ");
    Serial.print(ind->rssi, DEC);
    Serial.print("  ");

    Serial.print("ping: ");

    pingCounter = (byte) *(ind->data);
    Serial.println(pingCounter);
    return true;
}
