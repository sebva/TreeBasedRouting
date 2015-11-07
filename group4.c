#include "contiki.h"
#include <stdio.h> /* For printf() */
#include <string.h>
#include "net/rime.h"
#include "dev/button-sensor.h"
#include "dev/leds.h"
#include "dev/light.h"
#include "node-id.h" /* get a pointer to the own node id */
#include "dev/sht11-sensor.h"

/************************************************
 *                  Structs                     *
 ************************************************/
struct discovery_packet
{
    unsigned short parent_node_id;
    uint16_t hop_count;
    uint16_t sequence_number;
};

/************************************************
 *                  Constants                   *
 ************************************************/
// specify the node which initiates the broadcast flood
#define ROOT_ID 1
#define BROADCAST_CHANNEL 128
#define WAIT_BEFORE_BEGINNING_ALGORITHM 5
#define BROADCAST_INTERVAL 10
#define ALIVE_OUTPUT_INTERVAL 30

#define PARENT_STRATEGY HOPCOUNT

/************************************************
 *              Global variables                *
 ************************************************/
static uint16_t sequence_number_heard;
static uint16_t sequence_number_emitted;
static struct etimer et_alive;
static clock_time_t time_now_alive;
static clock_time_t time_now_alive_secs;
static clock_time_t ticks_to_wait;
static struct etimer et0, et1;
static struct ctimer leds_off_timer_send;
static struct broadcast_conn bc;
static unsigned short parent_node_id;
static int best_rssi;
static uint16_t smallest_hopcount;

/************************************************
 *                  Functions                   *
 ************************************************/
void print_temperature_binary_to_float(uint16_t temp);
static void timer_callback_turn_leds_off();
static void recv_bc(struct broadcast_conn *c, rimeaddr_t *from);

static const struct broadcast_callbacks broadcast_call = { recv_bc };

// Timer callback turns off the blue led
static void timer_callback_turn_leds_off()
{
    leds_off(LEDS_BLUE);
}

void print_temperature_binary_to_float(uint16_t temp)
{
    printf("%d.%d", (temp / 10 - 396) / 10, (temp / 10 - 396) % 10);
}

void set_new_parent(uint16_t _parent_node_id)
{
    parent_node_id = _parent_node_id;
    printf("New parent node: %d\n", parent_node_id);

    sequence_number_emitted = sequence_number_heard;

    static struct discovery_packet sent_discovery_message;
    sent_discovery_message.parent_node_id = node_id;
    sent_discovery_message.hop_count = smallest_hopcount + 1;
    sent_discovery_message.sequence_number = sequence_number_emitted;

    packetbuf_copyfrom(&sent_discovery_message, sizeof(sent_discovery_message));
    broadcast_send(&bc);

    printf("Not root: sent discovery bcast message. seq=%u, ", sequence_number_emitted);
    printf("hops=%u\n", smallest_hopcount + 1);
}

// Broadcast callback
static void recv_bc(struct broadcast_conn *c, rimeaddr_t *from)
{
    if (ROOT_ID != node_id)
    {
        static struct discovery_packet received_discovery_message;
        packetbuf_copyto(&received_discovery_message);
        int rssi = (int) packetbuf_attr(PACKETBUF_ATTR_RSSI);

        printf("Not root: received discovery bcast from %d, ", from->u8[0]);
        printf("seq=%u, ", received_discovery_message.sequence_number);
        printf("RSSI=%i\n", rssi);

        if (received_discovery_message.sequence_number > sequence_number_heard)
        {
            sequence_number_heard = received_discovery_message.sequence_number;
            smallest_hopcount = received_discovery_message.hop_count;
            best_rssi = rssi;
            set_new_parent(received_discovery_message.parent_node_id);
        }
        else if (received_discovery_message.sequence_number == sequence_number_heard)
        {
#if PARENT_STRATEGY == HOPCOUNT
            if (received_discovery_message.hop_count < smallest_hopcount)
            {
                smallest_hopcount = received_discovery_message.hop_count;
                set_new_parent(received_discovery_message.parent_node_id);
            }
#elif PARENT_STRATEGY == RSSI
            if (rssi > best_rssi)
            {
                best_rssi = rssi;
                set_new_parent(received_discovery_message.parent_node_id);
            }
#endif
        }
    }
}

/************************************************
 *                  Processes                   *
 ************************************************/
PROCESS(alive_status_process, "alive status");
PROCESS_THREAD(alive_status_process, ev, data)
{
    PROCESS_BEGIN();
    while (1)
    {
        time_now_alive = clock_time();
        time_now_alive_secs = time_now_alive / CLOCK_SECOND;
        ticks_to_wait = ((time_now_alive_secs + 1) * ALIVE_OUTPUT_INTERVAL * CLOCK_SECOND) - time_now_alive;

        etimer_set(&et_alive, ticks_to_wait);
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et_alive));
        printf("alive_status_process: node: %u is alive ", node_id);
        clock_time_t time = clock_time();
        printf("time: %lu s ", time / CLOCK_SECOND);
        printf("ticks: %lu s\n", time);
    }
    PROCESS_END();
}

PROCESS(routing_process, "Establish routing");
PROCESS_THREAD(routing_process, ev, data)
{
    PROCESS_EXITHANDLER(broadcast_close(&bc);)
    PROCESS_BEGIN();

    broadcast_open(&bc, BROADCAST_CHANNEL, &broadcast_call);
    sequence_number_heard = 0;

    if (ROOT_ID == node_id)
    {
        etimer_set(&et1, WAIT_BEFORE_BEGINNING_ALGORITHM * CLOCK_SECOND);
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et1));

        sequence_number_heard = 0;
        sequence_number_emitted = 0;
        while (1)
        {
            sequence_number_emitted++;
            sequence_number_heard = sequence_number_emitted;

            static struct discovery_packet sent_discovery_message;
            sent_discovery_message.parent_node_id = node_id;
            sent_discovery_message.hop_count = 1;
            sent_discovery_message.sequence_number = sequence_number_emitted;

            packetbuf_copyfrom(&sent_discovery_message, sizeof(sent_discovery_message));
            broadcast_send(&bc);

            printf("Root: sent discovery bcast message. seq=%u\n", sequence_number_emitted);

            // blink
            leds_on(LEDS_BLUE);
            ctimer_set(&leds_off_timer_send, CLOCK_SECOND / 8, timer_callback_turn_leds_off, NULL);

            // set timer to repeat loop
            etimer_set(&et1, BROADCAST_INTERVAL * CLOCK_SECOND);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et1));
        }

    }
    PROCESS_END();
}

AUTOSTART_PROCESSES(&routing_process, &alive_status_process);
