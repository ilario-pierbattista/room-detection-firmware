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

typedef enum AppState_t {
    APP_STATE_INITIAL, APP_STATE_IDLE,
} AppState_t;

/***********************************************************************
 *
 *      TASK SCHEDULING HEADERS
 *
 ***********************************************************************
 */
/**
 * Gestisce gli stati dell'applicazione
 */
static void APP_TaskHandler (void);

/**
 * Inizializza l'applicazione con i parametri di interesse
 */
static void APP_Init (void);

/**
 * Esecutore, temporizzato, del ping
 * @param timer Software timer che esegue il ping
 */
static void ping_timer_handler (SYS_Timer_t *timer);

/***********************************************************************
 *
 *      NETWORKING HEADERS
 *
 ***********************************************************************
 */
/**
 * Invia in broadcast il ping
 */
static void anchor_tx_ping ();

/**
 * Alla conferma della corretta processazione della richiesta, esegue
 * delle operazioni di pulizia su di essa
 * @param req Richiesta di rete
 */
static void anchor_tx_ping_confirmation (NWK_DataReq_t *req);

/**
 * Handler del coordinator per la ricezione del ping da un anchor node
 * @param ind Pacchetto di dati ricevuti
 * @return true se la trasmissione è andata a buon fine
 */
static bool coordinator_rx_ping (NWK_DataInd_t *ind);

/**
 * Handler delle ancore per la ricezione del ping dalle altre ancore
 * @param ind Pacchetto di dati ricevuti
 * @return true se la trasmissione è andata a buon fine
 */
static bool anchor_rx_ping (NWK_DataInd_t *ind);

/***********************************************************************
 *
 *      UTILS HEADERS
 *
 ***********************************************************************
 */

/**
 * Stampa le informazioni sul dongle all'avvio
 */
static void print_info_boot (void);

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

/**
 * Print, append new line, flush
 */
extern "C" void println ();

/***********************************************************************
 *
 *      STATIC GLOBAL VARIABLES
 *
 ***********************************************************************
 */

// Stato dell'applicazione
static AppState_t app_state;
// Contatore del ping
static uint16_t ping_counter = 0;
// Buffer per il messaggio di ping in uscita
static uint8_t broadcast_ping_msg[PING_MSG_SIZE];
// Software timer per lo scheduling del ping
static SYS_Timer_t ping_timer;
// Coda di messaggi in uscita
static QueueArray<NWK_DataReq_t *> outcoming_datareqs;
// Quando è true vuol dire che c'è un NWK_DataReq in uscita che sta
// aspettando conferma
static bool ping_req_busy = false;

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

/***********************************************************************
 *
 *      TASK SCHEDULING DEFINITIONS
 *
 ***********************************************************************
 */

static void APP_TaskHandler (void) {
    switch (app_state) {
        case APP_STATE_INITIAL: {
            APP_Init();
            app_state = APP_STATE_IDLE;
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

    #if DONGLE_ADDRESS == COORDINATOR_ADDRESS

    // Endpoint di ricezione del ping al coordinator
    NWK_OpenEndpoint(PING_ENDPOINT, coordinator_rx_ping);

    #else

    // Endpoint di ricezione del ping all'ancora
    NWK_OpenEndpoint(PING_ENDPOINT, anchor_rx_ping);

    if (!SYS_TimerStarted(&ping_timer)) {
        ping_timer.interval = PING_PERIOD;
        ping_timer.mode = SYS_TIMER_PERIODIC_MODE;
        ping_timer.handler = ping_timer_handler;
        SYS_TimerStart(&ping_timer);
    }
    #endif
}

static void ping_timer_handler (SYS_Timer_t *timer) {
    anchor_tx_ping();
    (void) timer;
}

/***********************************************************************
 *
 *      NETWORKING DEFINITIONS
 *
 ***********************************************************************
 */

static void anchor_tx_ping () {
    if (ping_req_busy) return;

    // Incodamento della richieste
    if (outcoming_datareqs.count() >= NWK_BUFFERS_AMOUNT) {
        free(outcoming_datareqs.dequeue());
    }
    NWK_DataReq_t *outcoming_msg = (NWK_DataReq_t *) malloc(
            sizeof(NWK_DataReq_t));
    outcoming_datareqs.enqueue(outcoming_msg);

    // Creazione del messaggio
    ping_counter++;
    broadcast_ping_msg[0] = 'C';
    uint16_to_bytes(&broadcast_ping_msg[1], ping_counter);

    // Impacchettamento
    outcoming_msg->dstAddr = NWK_BROADCAST_ADDR;
    outcoming_msg->dstEndpoint = PING_ENDPOINT;
    outcoming_msg->srcEndpoint = 1;
    outcoming_msg->options = 0;
    outcoming_msg->confirm = anchor_tx_ping_confirmation;
    outcoming_msg->data = broadcast_ping_msg;
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

static void anchor_tx_ping_confirmation (NWK_DataReq_t *req) {
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
 *      UTILS DEFINITIONS
 *
 ***********************************************************************
 */
static void print_info_boot (void) {
    Serial.println(
            F("\n\n========================================\n#"));
    #if DONGLE_ADDRESS == COORDINATOR_ADDRESS
    Serial.println(F("#  Coordinator Node"));
    #else
    Serial.println(F("#  Anchor Node"));
    #endif
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

void println (char *x) {
    Serial.println(x);
    Serial.flush();
}
/**********************************************************************/