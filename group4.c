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

struct temperature_packet
{
    unsigned short origin_node_id;
    uint16_t hop_count;
    uint16_t temperature;
};

/************************************************
 *                  Constants                   *
 ************************************************/
// specify the node which initiates the broadcast flood
#define ROOT_ID 1
#define UNICAST_CHANNEL 140
#define BROADCAST_CHANNEL 128
#define WAIT_BEFORE_BEGINNING_ALGORITHM 5
#define BROADCAST_INTERVAL 10
#define TEMPERATURE_INTERVAL 20

//#define PARENT_STRATEGY HOPCOUNT
#define PARENT_STRATEGY RSSI

/************************************************
 *              Global variables                *
 ************************************************/
static uint16_t sequence_number_heard;
static uint16_t sequence_number_emitted;
static struct etimer et0, et1;
static struct ctimer leds_off_timer_send;
static struct unicast_conn uc;
static struct broadcast_conn bc;
static unsigned short parent_node_id;
static int best_rssi;
static uint16_t smallest_hopcount;

/************************************************
 *                  Functions                   *
 ************************************************/
void print_temperature_binary_to_float(uint16_t temp);
static void timer_callback_turn_leds_off();
static void recv_uc(struct unicast_conn *c, const rimeaddr_t *from);
static void recv_bc(struct broadcast_conn *c, rimeaddr_t *from);

static const struct unicast_callbacks unicast_callbacks = { recv_uc };
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
    if (parent_node_id != _parent_node_id) {
        parent_node_id = _parent_node_id;
        printf("New parent node: %d\n", _parent_node_id);
    }
}

void print_temperature_packet(struct temperature_packet *tp)
{
    printf("Temperature at node %d received in %u hops: ", tp->origin_node_id, tp->hop_count);
    print_temperature_binary_to_float(tp->temperature);
    printf("\n");
}

void send_temperature_message(struct temperature_packet *tp)
{
    packetbuf_clear();
    packetbuf_copyfrom(tp, sizeof(struct temperature_packet));

    // Prepare the rimeaddr_t structure holding the remote node id
    rimeaddr_t addr;
    addr.u8[0] = parent_node_id; // LSB
    addr.u8[1] = 0; // MSB

    // send the packet using unicast
    unicast_send(&uc, &addr);
}

// Unicast callback
static void recv_uc(struct unicast_conn *c, const rimeaddr_t *from)
{
    static struct temperature_packet received_temperature_message;
    packetbuf_copyto(&received_temperature_message);

    if (ROOT_ID == node_id)
    {
        print_temperature_packet(&received_temperature_message);
    }
    else
    {
        received_temperature_message.hop_count++;
        send_temperature_message(&received_temperature_message);
        printf("Node %u: relayed temperature of node %u\n", node_id, received_temperature_message.origin_node_id);
    }
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
        printf("hops=%u, ", received_discovery_message.hop_count);
        printf("RSSI=%i\n", rssi);


        uint16_t previous_sequence_number = sequence_number_heard;
        uint8_t is_better_packet = 0;
        // Update sequence number
        sequence_number_heard = received_discovery_message.sequence_number;
        // First discovery packet ever
        if (previous_sequence_number == 0)
        {
            smallest_hopcount = received_discovery_message.hop_count;
            best_rssi = rssi;
            set_new_parent(received_discovery_message.parent_node_id);
            is_better_packet = 1;
        }
        else
        {
#if PARENT_STRATEGY == HOPCOUNT
            if (received_discovery_message.hop_count < smallest_hopcount)
            {
                smallest_hopcount = received_discovery_message.hop_count;
                set_new_parent(received_discovery_message.parent_node_id);
                is_better_packet = 1;
            }
#elif PARENT_STRATEGY == RSSI
            if (rssi > best_rssi)
            {
                best_rssi = rssi;
                set_new_parent(received_discovery_message.parent_node_id);
            }
#endif
        }

        // New or better packet, forward it. is_better_packet only used for hop-count.
        if (received_discovery_message.sequence_number > sequence_number_heard || is_better_packet)
        {
            sequence_number_emitted = sequence_number_heard;

            static struct discovery_packet sent_discovery_message;
            sent_discovery_message.parent_node_id = node_id;
            sent_discovery_message.hop_count = received_discovery_message.hop_count + 1;
            sent_discovery_message.sequence_number = sequence_number_emitted;

            packetbuf_copyfrom(&sent_discovery_message, sizeof(sent_discovery_message));
            broadcast_send(&bc);

            printf("Not root: sent discovery bcast message. seq=%u, ", sequence_number_emitted);
            printf("hops=%u\n", smallest_hopcount + 1);
        }
    }
}

/************************************************
 *                  Processes                   *
 ************************************************/
PROCESS(send_temperature_process, "Send temperature");
PROCESS_THREAD(send_temperature_process, ev, data)
{
    PROCESS_EXITHANDLER(SENSORS_DEACTIVATE(sht11_sensor); unicast_close(&uc);)
    PROCESS_BEGIN();

    unicast_open(&uc, UNICAST_CHANNEL, &unicast_callbacks);
    SENSORS_ACTIVATE(sht11_sensor);

    while (1)
    {
        etimer_set(&et0, TEMPERATURE_INTERVAL * CLOCK_SECOND);
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et0));

        if (node_id != ROOT_ID && sequence_number_heard > 0)
        {
            static struct temperature_packet sent_temperature_message;
            sent_temperature_message.hop_count = 1;
            sent_temperature_message.origin_node_id = node_id;
            sent_temperature_message.temperature = sht11_sensor.value(SHT11_SENSOR_TEMP);

            send_temperature_message(&sent_temperature_message);
            printf("Temperature sent\n");
        }
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

AUTOSTART_PROCESSES(&routing_process, &send_temperature_process);
