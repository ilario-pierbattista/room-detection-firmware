#include <Arduino.h>
#include <lwm/sys/sysTimer.h>
#include <lwm/nwk/nwkTx.h>
#include <lwm/sys/sys.h>
#include <QueueArray.h>
#include <HashMap.h>

#include "config.h"

/**
 * Le ancore inviano in broadcast dei messaggi di ping.
 *
 * === MESSAGGI DI PING ===
 *
 * +++++++++++++++++++
 * | P |  PingCount  |
 * +++++++++++++++++++
 * | 1 |     2       |    = 3B
 * +++++++++++++++++++
 *
 * P: Carattere di controllo 'P'
 * PingCount: numero del ping inviato
 *
 * === MESSAGGI DI REPORT ===
 *
 * ++++++++++++++++++++++++++++++++++++++
 * | R | AnchorAddress | AnchorMeanRssi |
 * ++++++++++++++++++++++++++++++++++++++
 * | 1 |       2       |       4        |  = 7 B
 * ++++++++++++++++++++++++++++++++++++++
 *
 * R: Carattere di controllo R
 * AnchorAddress: indirizzo dell'ancora di cui si invia il report
 * AnchorMeanRssi: Rssi medio percepito dall'ancora che invia il report
 */

#define PING_MSG_SIZE 3
#define REPORT_MSG_SIZE 7

/***********************************************************************
 *
 *      TYPES DEFINITIONS
 *
 ***********************************************************************
 */
/**
 * Tipo dello stato dell'applicazione
 */
typedef enum AppState_t {
    APP_STATE_INITIAL, APP_STATE_IDLE,
} AppState_t;

/**
 * Float spacchettabile in byte singoli
 */
typedef union {
    float number;
    uint8_t bytes[4];
} FloatUnion_t;

/**
 * Struttura di un report da inviare al coordinator
 */
typedef struct NodeReport {
    uint16_t addr;
    uint16_t received_ping_count;
    FloatUnion_t rssi_mean;
} NodeReport_t;


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
 * Invia un report al coordinator
 */
static void anchor_tx_report ();

/**
 * Alla conferma della corretta processazione della richiesta, esegue
 * delle operazioni di pulizia su di essa
 * @param req Richiesta di rete
 */
static void anchor_tx_ping_confirmation (NWK_DataReq_t *req);

/**
 * Alla conferma del corretto invio di un report, esegue le
 * operazioni du pulizia sull richiesta
 * @param req Richiesta di rete
 */
static void anchor_tx_report_confirmation (NWK_DataReq_t *req);

/**
 * Handler del coordinator per la ricezione del ping da un anchor node
 * @param ind Pacchetto di dati ricevuti
 * @return true se la trasmissione è andata a buon fine
 */
static bool coordinator_rx_ping (NWK_DataInd_t *ind);

/**
 * Handler del coordinatore per la ricezione del report da un anchor
 * node
 * @param ind Pacchetto dati
 * @return true se la trasmissione è andata a buon fine
 */
static bool coordinator_rx_report (NWK_DataInd_t *ind);

/**
 * Handler delle ancore per la ricezione del ping dalle altre ancore
 * @param ind Pacchetto di dati ricevuti
 * @return true se la trasmissione è andata a buon fine
 */
static bool anchor_rx_ping (NWK_DataInd_t *ind);

/***********************************************************************
 *
 *      MEASUREMENT HEADERS
 *
 ***********************************************************************
 */
/**
 * Prepara la struttura di un report ad essere utilizzata per
 * immagazzinare dati
 * @param report Report
 */
static void configure_report (NodeReport_t *report, uint16_t addr);

/**
 * Aggiunge una misura di rssi ad un report
 * @param report Report da aggiornare
 * @param rssi Rssi misurato
 */
static void update_report (NodeReport_t *report, int16_t rssi);

/**
 * Impacchetta il report in un messaggio da poter spedire
 * @param dest Array del messaggo
 * @param report Report da spedire
 */
static void pack_report (uint8_t *dest, NodeReport_t *report);

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
// Buffer per il messaggio di report
static uint8_t report_msg[REPORT_MSG_SIZE];
// Software timer per lo scheduling del ping
static SYS_Timer_t ping_timer;
// Coda di messaggi in uscita
static QueueArray<NWK_DataReq_t *> outcoming_datareqs;
// Quando è true vuol dire che c'è un NWK_DataReq in uscita che sta
// aspettando conferma
static bool request_busy = false;
// Quando è true vuol dire che c'è un report da inviare
static bool report_ready_to_send = false;
// Array grezzo di report
static HashType<uint16_t, NodeReport_t *> report_hash_raw_array[NODES_COUNT];
// Hashmap di report
static HashMap<uint16_t, NodeReport_t *> report_hashmap = HashMap<uint16_t, NodeReport_t *>(
        report_hash_raw_array, NODES_COUNT);
// Stringhe per le funzioni di debug. Dichiarate staticamente si
// evitano le perdite di memoria
static char debug_message_formatted[120], debug_message_body[30];
// Output seriale
static char serial_output_buffer[70];
// Per motivi di performance, sprintf disabilità il placeholder %f.
// Serve un buffer di conversione intermedio
static char float_conversion_buffer[10];

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
    NWK_OpenEndpoint(REPORT_ENDPOINT, coordinator_rx_report);

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
    anchor_tx_report();
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
    if (request_busy) return;

    // Incodamento della richieste
    if (outcoming_datareqs.count() >= NWK_BUFFERS_AMOUNT) {
        free(outcoming_datareqs.dequeue());
    }
    NWK_DataReq_t *outcoming_msg = (NWK_DataReq_t *) malloc(
            sizeof(NWK_DataReq_t));
    outcoming_datareqs.enqueue(outcoming_msg);

    // Creazione del messaggio
    ping_counter++;
    broadcast_ping_msg[0] = 'P';
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
    debug_bytes_to_hex_digest(serial_output_buffer, outcoming_msg->data,
                              outcoming_msg->size);
    Serial.println("Sent msg: " + String(serial_output_buffer));
    #endif

    NWK_DataReq(outcoming_msg);

    request_busy = true;
}

static void anchor_tx_report () {
    if (request_busy) return;
    if (!report_ready_to_send) return;

    report_ready_to_send = false;
    // Incodamento della richieste
    if (outcoming_datareqs.count() >= NWK_BUFFERS_AMOUNT) {
        free(outcoming_datareqs.dequeue());
    }
    NWK_DataReq_t *outcoming_msg = (NWK_DataReq_t *) malloc(
            sizeof(NWK_DataReq_t));
    outcoming_datareqs.enqueue(outcoming_msg);

    // Impacchettamento
    outcoming_msg->dstAddr = COORDINATOR_ADDRESS;
    outcoming_msg->dstEndpoint = REPORT_ENDPOINT;
    outcoming_msg->srcEndpoint = 1;
    outcoming_msg->options = 0;
    outcoming_msg->confirm = anchor_tx_report_confirmation;
    outcoming_msg->data = report_msg;
    outcoming_msg->size = REPORT_MSG_SIZE;

    #ifdef DEBUG_ANCHOR
    debug_bytes_to_hex_digest(serial_output_buffer, outcoming_msg->data,
                              outcoming_msg->size);
    Serial.println("Sent msg: " + String(serial_output_buffer));
    #endif

    NWK_DataReq(outcoming_msg);

    request_busy = true;
}

static void anchor_tx_ping_confirmation (NWK_DataReq_t *req) {
    request_busy = false;
    (void) req;
}

static void anchor_tx_report_confirmation (NWK_DataReq_t *req) {
    request_busy = false;
    (void) req;
}

static bool coordinator_rx_ping (NWK_DataInd_t *ind) {
    #ifdef DEBUG_COORD
    debug_print_dataind_summary(ind);
    #endif
    // Stampando il letterale di un dizionario riesco a minimizzare
    // il successivo lavoro di parsing
    sprintf(serial_output_buffer, "{'anchor':%u,'rssi':%d,'ping':%d}\n",
            ind->srcAddr, ind->rssi, bytes_to_uint16(&ind->data[1]));
    Serial.print(serial_output_buffer);
    return true;
}

static bool coordinator_rx_report (NWK_DataInd_t *ind) {
    FloatUnion_t rssi;

    #ifdef DEBUG_COORD
    debug_print_dataind_summary(ind);
    #endif

    // Stampando il letterale di un dizionario riesco a minimizzare
    // il successivo lavoro di parsing
    memcpy(rssi.bytes, &ind->data[3], sizeof(float));
    dtostrf(rssi.number, 4, 2, float_conversion_buffer);
    sprintf(serial_output_buffer, "{'sender':%u,'report':True,"
                    "'rssi':%s,"
                    "'anchor':%u}\n", ind->srcAddr, float_conversion_buffer,
            bytes_to_uint16(&ind->data[1]));
    Serial.print(serial_output_buffer);

    return true;
}

static bool anchor_rx_ping (NWK_DataInd_t *ind) {
    NodeReport_t *report;

    #ifdef DEBUG_ANCHOR
    debug_print_dataind_summary(ind);
    #endif

    // Prendo il report per l'indirizzo corrispondente
    if (report_hashmap.getIndexOf(ind->srcAddr) < 0) {
        report = (NodeReport_t *) malloc(sizeof(NodeReport_t));
        configure_report(report, ind->srcAddr);
        // Creo il nuovo repo
        report_hashmap.add(ind->srcAddr, report);
    } else {
        report = report_hashmap.getValueOf(ind->srcAddr);
    }

    // Aggiorno il report
    update_report(report, ind->rssi);

    // Se il report è completo, lo preparo per la spedizione
    if (report->received_ping_count >= SEND_EVERY_N_PINGS) {
        pack_report(report_msg, report);
        report_ready_to_send = true;
        // E resetto il report corrente
        configure_report(report, report->addr);
    }

    return true;
}

/***********************************************************************
 *
 *      MEASUREMENT HEADERS
 *
 ***********************************************************************
 */
static void configure_report (NodeReport_t *report, uint16_t addr) {
    report->addr = addr;
    report->received_ping_count = 0;
    report->rssi_mean.number = NAN;
}

static void update_report (NodeReport_t *report, int16_t rssi) {
    if (isnan(report->rssi_mean.number)) {
        report->rssi_mean.number = (float) rssi;
        report->received_ping_count = 1;
    } else {
        report->rssi_mean.number = ((report->rssi_mean.number *
                                     (float) report->received_ping_count) +
                                    (float) rssi) /
                                   (report->received_ping_count + 1);
        report->received_ping_count++;
    }
}

static void pack_report (uint8_t *dest, NodeReport_t *report) {
    dest[0] = 'R';
    uint16_to_bytes(&dest[1], report->addr);
    memcpy(&dest[3], report->rssi_mean.bytes, sizeof(float));
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
    debug_bytes_to_hex_digest(debug_message_body, ind->data, ind->size);
    sprintf(debug_message_formatted,
            "Received message from anchor #%d to endpoint #%d lqi: %d rssi: %d size: %d body: %s",
            ind->srcAddr, ind->dstEndpoint, ind->lqi, ind->rssi,
            ind->size, debug_message_body);
    Serial.println(debug_message_formatted);
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