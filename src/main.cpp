#include <Arduino.h>
#include <lwm/sys/sysTimer.h>
#include <lwm/nwk/nwkTx.h>
#include <lwm/sys/sys.h>
#include <QueueArray.h>

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

#define PING_MSG_SIZE 3

extern "C" {
void println (char *x) {
    Serial.println(x);
    Serial.flush();
}
}

typedef enum AppState_t {
    APP_STATE_INITIAL, APP_STATE_IDLE,
} AppState_t;

static void APP_TaskHandler (void);

static void APP_Init (void);

static void ping_confirmation (NWK_DataReq_t *req);

static void anchor_tx_ping ();

/**
 * Stampa le informazioni sul dongle all'avvio
 */
static void print_info_boot (void);

/**
 * Handler del coordinator per la ricezione del ping da un anchor node
 * @param ind Pacchetto di dati ricevuti
 * @return Stato
 */
static bool coordinator_rx_ping (NWK_DataInd_t *ind);

static bool anchor_rx_ping (NWK_DataInd_t *ind);

static void ping_timer_handler (SYS_Timer_t *timer);

/***********************************************************************
 *
 *      UTILS HEADERS
 *
 ***********************************************************************
 */

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

/***********************************************************************
 *
 *      STATIC GLOBAL VARIABLES
 *
 ***********************************************************************
 */

static AppState_t appState;
static uint16_t coordinator_ping_counter = 0;
static uint8_t coordinator_ping_msg[PING_MSG_SIZE];
static QueueArray<NWK_DataReq_t *> outcoming_stack;
static SYS_Timer_t ping_timer;
static bool ping_req_busy = false;

static void ping_timer_handler (SYS_Timer_t *timer) {
    anchor_tx_ping();
    (void) timer;
}

/***********************************************************************
 *
 *      MAIN LOOP
 *
 ***********************************************************************
 */

void setup () {
    Serial.begin(115200);

    SYS_Init();

    print_info_boot();
}

void loop () {
    SYS_TaskHandler();
    APP_TaskHandler();
}

static void APP_TaskHandler (void) {
    switch (appState) {
        case APP_STATE_INITIAL: {
            APP_Init();
            appState = APP_STATE_IDLE;
        }
            break;

        case APP_STATE_IDLE:
            break;

        default:
            break;
    }
}

static void APP_Init (void) {
    NWK_SetAddr(DONGLE_ADDRESS);
    NWK_SetPanId(0x01);
    PHY_SetChannel(0x1a);
    PHY_SetRxState(true);

    if (DONGLE_ADDRESS == COORDINATOR_ADDRESS) {
        NWK_OpenEndpoint(PING_ENDPOINT, coordinator_rx_ping);
    } else {
        NWK_OpenEndpoint(PING_ENDPOINT, anchor_rx_ping);
    }

    if (!SYS_TimerStarted(&ping_timer)) {
        ping_timer.interval = 300;
        ping_timer.mode = SYS_TIMER_PERIODIC_MODE;
        ping_timer.handler = ping_timer_handler;
        SYS_TimerStart(&ping_timer);
    }
}

static void anchor_tx_ping () {
    if (ping_req_busy) return;

    // Incodamento della richieste
    if (outcoming_stack.count() >= NWK_BUFFERS_AMOUNT) {
        free(outcoming_stack.dequeue());
    }
    NWK_DataReq_t *outcoming_msg = (NWK_DataReq_t *) malloc(
            sizeof(NWK_DataReq_t));
    outcoming_stack.enqueue(outcoming_msg);

    // Creazione del messaggio
    coordinator_ping_counter++;
    coordinator_ping_msg[0] = 'C';
    uint16_to_bytes(&coordinator_ping_msg[1], coordinator_ping_counter);

    // Impacchettamento
    outcoming_msg->dstAddr = NWK_BROADCAST_ADDR;
    outcoming_msg->dstEndpoint = PING_ENDPOINT;
    outcoming_msg->srcEndpoint = 1;
    outcoming_msg->options = 0;
    outcoming_msg->confirm = ping_confirmation;
    outcoming_msg->data = coordinator_ping_msg;
    outcoming_msg->size = PING_MSG_SIZE;

    #ifdef DEBUG_ANCHOR
    char msg_debug[30];
    debug_bytes_to_hex_digest(msg_debug, outcoming_msg->data,
                              outcoming_msg->size);
    Serial.println("Sent msg: " + String(msg_debug));
    #endif

    NWK_DataReq(outcoming_msg);

    ping_req_busy = true;
}

static void ping_confirmation (NWK_DataReq_t *req) {
    ping_req_busy = false;
    (void) req;
}

static bool coordinator_rx_ping (NWK_DataInd_t *ind) {
    #ifdef DEBUG_COORD
    debug_print_dataind_summary(ind);
    #endif
    char out[50];
    // Stampando il letterale di un dizionario riesco a minimizzare
    // il successivo lavoro di parsing
    sprintf(out, "{'anchor':%u,'rssi':%d,'ping':%d}\n", ind->srcAddr,
            ind->rssi, bytes_to_uint16(&ind->data[1]));
    Serial.print(out);
    return true;
}

static bool anchor_rx_ping (NWK_DataInd_t *ind) {
    #ifdef DEBUG_ANCHOR
    debug_print_dataind_summary(ind);
    #endif
    return true;
}

/***********************************************************************
 *
 *      UTILS DEFINITION
 *
 ***********************************************************************
 */
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

void debug_print_dataind_summary (NWK_DataInd_t *ind) {
    char debug_msg[100], msg_body[30];
    debug_bytes_to_hex_digest(msg_body, ind->data, ind->size);
    sprintf(debug_msg,
            "Received message from anchor #%d to endpoint #%d lqi: %d rssi: %d size: %d body: %s",
            ind->srcAddr, ind->dstEndpoint, ind->lqi, ind->rssi,
            ind->size, msg_body);
    Serial.println(debug_msg);
}

void debug_bytes_to_hex_digest (char *dest, uint8_t *msg, size_t size) {
    dest[0] = '\0';
    for (size_t i = 0; i < size; i++) {
        sprintf(dest, "%s 0x%02X", dest, msg[i]);
    }
}

static void uint16_to_bytes (uint8_t *dest, uint16_t src) {
    *dest = (src >> 8) & 0xff;
    *(dest + 1) = (src) & 0xff;
}

uint16_t bytes_to_uint16 (uint8_t *src) {
    uint16_t dest = (src[0] << 8);
    dest |= src[1];
    return dest;
}
/**********************************************************************/