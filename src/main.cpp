#include <Arduino.h>
#include <Wire.h>
#include "lwm.h";
#include "lwm/sys/sys.h"
#include "lwm/nwk/nwk.h"
#include "config.h"

/**
 * @TODO Aggiungere una descrizione mooolto dettagliata
 */

// Invio: dongle -> coordinator
// cXX
// Invio: dongle -> dongle
// pXX
// Invio report dati: dongle -> coordinator
// r{<Address>:<Valore RSSI>[, <Address>:<Valore RSSI>]}
// (Invio con ack)


#define ANCHOR_TO_COORDINATOR_ENDPOINT 1
#define ANCHOR_TO_ANCHOR_ENDPOINT 2

#define PING_MSG_SIZE 3

#define DEBUG

extern "C" {
void println (char *x) {
    Serial.println(x);
    Serial.flush();
}
}

/**
 * Stampa le informazioni sul dongle all'avvio
 */
static void print_info_boot (void);

static void ping_coordinator ();

static bool coordinator_receive_ping (NWK_DataInd_t *ind);

static void uint16_to_2bytes (uint8_t *dest, uint16_t src);

static void debug_received_frame (NWK_DataInd_t *ind);

uint16_t coordinator_ping_counter = 0;
uint8_t coordinator_ping_msg[PING_MSG_SIZE];

void setup () {
    Serial.begin(115200);
    print_info_boot();
    SYS_Init();
    NWK_SetAddr(DONGLE_ADDRESS);
    NWK_SetPanId(0x01);
    PHY_SetChannel(0x1a);
    PHY_SetRxState(true);
    NWK_OpenEndpoint(1, coordinator_receive_ping);
}

void loop () {
    SYS_TaskHandler();
    if (DONGLE_ADDRESS != COORDINATOR_ADDRESS) {
        // Sono un dongle
        ping_coordinator();
        delay(300);
    } else {
        // Sono un coordinator
        delay(10);
    }
}

static void ping_coordinator () {
    coordinator_ping_counter++;
    Serial.print("ping ");
    Serial.println(coordinator_ping_counter);

    // Creazione del messaggio
    coordinator_ping_msg[0] = 'C';
    uint16_to_2bytes(&coordinator_ping_msg[1], coordinator_ping_counter);

    // we just leak for now @TODO pocca memoria
    NWK_DataReq_t *message = (NWK_DataReq_t *) malloc(sizeof(NWK_DataReq_t));
    message->dstAddr = COORDINATOR_ADDRESS;
    message->dstEndpoint = 1;
    message->srcEndpoint = 1;
    message->options = 0;
    message->data = coordinator_ping_msg;
    message->size = sizeof(PING_MSG_SIZE);
    NWK_DataReq(message);
}

static bool coordinator_receive_ping (NWK_DataInd_t *ind) {
    #ifdef DEBUG
    debug_received_frame(ind);
    #endif
    coordinator_ping_counter = (byte) *(ind->data);
    Serial.println(coordinator_ping_counter);
    return true;
}

static void print_info_boot (void) {
    Serial.println(F("\n\n========================================\n#"));
    if (DONGLE_ADDRESS == COORDINATOR_ADDRESS) {
        Serial.println(F("#  Coordinator Node"));
    } else {
        Serial.println(F("#  Anchor Node"));
    }
    Serial.print(F("#\n#  Address: "));
    Serial.print(DONGLE_ADDRESS);
    Serial.println(F("\n#"));
    Serial.println(F("========================================"));
}

// @TODO unit testing http://docs.platformio.org/en/latest/plus/unit-testing.html#unit-testing
static void uint16_to_2bytes (uint8_t *dest, uint16_t src) {
    *dest = (src >> 8) & 0xff;
    *(dest + 1) = (src) & 0xff;
}

void debug_received_frame (NWK_DataInd_t *ind) {
    char debug_msg[300];
    sprintf(debug_msg, "Received message - from %d to endpoint #%d\n"
            "lqi: %d rssi: %d size: %d body: ", ind->srcAddr, ind->dstEndpoint, ind->lqi, ind->rssi, ind->size);
    Serial.print(debug_msg);
    for (int i = 0; i < ind->size; i++) {
        Serial.print(ind->data[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
}
