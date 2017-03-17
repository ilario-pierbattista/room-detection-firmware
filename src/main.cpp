#include <Arduino.h>
#include <Wire.h>
#include "lwm.h"
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

#define DEBUG_ANCHOR
// #define DEBUG_COORD

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

static void ping_coordinator_ack (NWK_DataReq_t *ind);

/**
 * Handler del coordinator per la ricezione del ping da un anchor node
 * @param ind Pacchetto di dati ricevuti
 * @return Stato
 */
static bool coordinator_receive_ping (NWK_DataInd_t *ind);

/**
 * Converte un uint16 in un array di due bytes
 * @param dest Indirizzo dell'array di byte di destinazione
 * @param src Valore da convertire (unsigned short)
 */
static void uint16_to_bytes (uint8_t *dest, uint16_t src);

/**
 * Converte un array di due byte in un uint16
 * @param src Indirizzo dell'array di byte da convertire
 * @return Valore convertito in unsigned short
 */
static uint16_t bytes_to_uint16 (uint8_t *src);

/**
 * Scrive in seriale (per motivi di debug) le informazioni contenute in
 * un messaggio ricevuto
 * @param ind Dati ricevuti
 */
static void debug_print_dataind_summary (NWK_DataInd_t *ind);

/**
 * Ottiene l'hex digest di un array di byte e lo salva in una stringa
 * @param dest Stringa di destinazione per il digest (va allocata
 * precedentemente)
 * @param msg Array di byte di cui fare il digest
 * @param size Lunghezza dell'array di byte
 */
static void
debug_bytes_to_hex_digest (char *dest, uint8_t *msg, size_t size);

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
    NWK_OpenEndpoint(ANCHOR_TO_COORDINATOR_ENDPOINT,
                     coordinator_receive_ping);
    // @todo Aggiungere endpoint per il ping delle ancore
    // @todo Aggiungere endpoint per invio del report al coordinator
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

    // Creazione del messaggio
    coordinator_ping_msg[0] = 'C';
    uint16_to_bytes(&coordinator_ping_msg[1], coordinator_ping_counter);

    // @todo è un leak di memoria. Da risolvere
    NWK_DataReq_t *message = (NWK_DataReq_t *) malloc(
            sizeof(NWK_DataReq_t));
    message->dstAddr = COORDINATOR_ADDRESS;
    message->dstEndpoint = ANCHOR_TO_COORDINATOR_ENDPOINT;
    message->srcEndpoint = 1;
    message->options = 0;
    message->data = coordinator_ping_msg;
    message->size = PING_MSG_SIZE;
    message->confirm = ping_coordinator_ack;

    #ifdef DEBUG_ANCHOR
    char msg_debug[30];
    debug_bytes_to_hex_digest(msg_debug, message->data, message->size);
    Serial.println("Sent msg: " + String(msg_debug));
    #endif

    NWK_DataReq(message);
}

static bool coordinator_receive_ping (NWK_DataInd_t *ind) {
    #ifdef DEBUG_COORD
    debug_received_frame(ind);
    #endif
    char out[50];
    // Stampando il letterale di un dizionario riesco a minimizzare
    // il successivo lavoro di parsing
    sprintf(out, "{'anchor':%u,'rssi':%d,'ping':%d}\n", ind->srcAddr,
            ind->rssi, bytes_to_uint16(&ind->data[1]));
    Serial.print(out);
    return true;
}

static void print_info_boot (void) {
    Serial.println(
            F("\n\n========================================\n#"));
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
static void uint16_to_bytes (uint8_t *dest, uint16_t src) {
    *dest = (src >> 8) & 0xff;
    *(dest + 1) = (src) & 0xff;
}

void debug_print_dataind_summary (NWK_DataInd_t *ind) {
    char debug_msg[100], msg_body[30];
    debug_bytes_to_hex_digest(msg_body, ind->data, ind->size);
    sprintf(debug_msg,
            "Received message from anchor #%d to endpoint #%d lqi: %d rssi: %d size: %d body: %s",
            ind->srcAddr, ind->dstEndpoint, ind->lqi, ind->rssi,
            ind->size, msg_body);
    Serial.println(debug_msg);
}

void ping_coordinator_ack (NWK_DataReq_t *ind) {
    #ifdef DEBUG_ANCHOR
    Serial.println(
            F("Il coordinator ha ricevuto il ping e ha inviato un ACK"));
    #endif
    free(ind);
}

void debug_bytes_to_hex_digest (char *dest, uint8_t *msg, size_t size) {
    dest[0] = '\0';
    for (size_t i = 0; i < size; i++) {
        sprintf(dest, "%s 0x%02X", dest, msg[i]);
    }
}

uint16_t bytes_to_uint16 (uint8_t *src) {
    uint16_t dest = (src[0] << 8);
    dest |= src[1];
    return dest;
}
