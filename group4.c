#include "contiki.h"
#include <stdio.h> /* For printf() */
#include <string.h>
#include "net/rime.h"
#include "dev/button-sensor.h"
#include "dev/leds.h"
#include "dev/light.h"
#include "node-id.h" /* get a pointer to the own node id */
#include "dev/sht11-sensor.h"

struct levelDiscovery {
	unsigned short root_node_id;
	uint16_t sequenceNumber;
};

static uint16_t sequenceNumberHeard;
static uint16_t sequenceNumberEmitted;


#define BROADCAST_CHANNEL 128

static struct etimer et0, et1;
static struct ctimer leds_off_timer_send;


void print_temperature_binary_to_float(uint16_t temp);
static void timerCallback_turnOffLeds();
static void recv_bc(struct broadcast_conn *c, rimeaddr_t *from);

void print_temperature_binary_to_float(uint16_t temp) {
	printf("%d.%d", (temp / 10 - 396) / 10, (temp / 10 - 396) % 10);
}

static const struct broadcast_callbacks broadcast_call = {recv_bc};
static struct broadcast_conn bc;

/*---------------------------------------------------------------------------*/
/* Timer callback turns off the blue led */
static void timerCallback_turnOffLeds() {
  leds_off(LEDS_BLUE);
}

static void recv_bc(struct broadcast_conn *c, rimeaddr_t *from) {

	static struct levelDiscovery levelBroadcastMessageReceived;
	packetbuf_copyto(&levelBroadcastMessageReceived);
	printf("received level discovery broadcast from %d\n", from->u8[0]);
	printf("seq=%u ", levelBroadcastMessageReceived.sequenceNumber);
	printf("RSSI=%i\n",(int) packetbuf_attr(PACKETBUF_ATTR_RSSI));

	printf("NEW BROADCAST RECEIVED!\n");

}

/*---------------------------------------------------------------------------*/

/* DEFINE HOW LONG THE NODE WAITS BEFORE STARTING THE ALGORITHM */

#define WAIT_BEFORE_BEGINNING_ALGORITHM                  5
#define BROADCAST_INTERVAL								 10

static struct levelDiscovery levelBroadcastMessage;


/*---------------------------------------------------------------------------*/
PROCESS(alive_status_process, "alive status");
/*---------------------------------------------------------------------------*/

#define ALIVE_OUTPUT_INTERVAL		        30
static struct etimer etAlive;
static clock_time_t time_now_alive;
static clock_time_t time_now_alive_secs;
static clock_time_t ticks_to_wait;

PROCESS_THREAD(alive_status_process, ev, data)
{
   PROCESS_BEGIN();
   while(1){
	   time_now_alive = clock_time();
	   time_now_alive_secs =  time_now_alive / CLOCK_SECOND;
	   ticks_to_wait = ((time_now_alive_secs+1)*ALIVE_OUTPUT_INTERVAL*CLOCK_SECOND)-time_now_alive;

	   etimer_set(&etAlive, ticks_to_wait);
	   PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&etAlive));
	   printf("alive_status_process: node: %u is alive ", node_id);
	   clock_time_t time = clock_time();
	   printf("time: %lu s ", time/CLOCK_SECOND);
	   printf("ticks: %lu s\n", time);
   }
   PROCESS_END();
}


/*---------------------------------------------------------------------------*/
PROCESS(broadcast_rssi_process, "Broadcast RSSI");
/*---------------------------------------------------------------------------*/


// specify the node which initiates the broadcast flood
#define ROOT_ID 50

PROCESS_THREAD(broadcast_rssi_process, ev, data)
{
  PROCESS_EXITHANDLER(broadcast_close(&bc);)
  PROCESS_BEGIN();
  broadcast_open(&bc, BROADCAST_CHANNEL, &broadcast_call);

  sequenceNumberHeard   = 0;

  if(ROOT_ID == node_id) {
	  etimer_set(&et1, WAIT_BEFORE_BEGINNING_ALGORITHM*CLOCK_SECOND);
	  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et1));

	  sequenceNumberHeard   = 0;
	  sequenceNumberEmitted = 0;
	  while(1) {
		  sequenceNumberEmitted++;
		  sequenceNumberHeard = sequenceNumberEmitted;

		  levelBroadcastMessage.root_node_id = node_id;
		  levelBroadcastMessage.sequenceNumber = sequenceNumberEmitted;

		  packetbuf_copyfrom(&levelBroadcastMessage, sizeof(levelBroadcastMessage));
		  broadcast_send(&bc);

	      printf("sent discovery broadcast message with sequence number %u\n", sequenceNumberEmitted);

	      // blink
	      leds_on(LEDS_BLUE);
		  ctimer_set(&leds_off_timer_send, CLOCK_SECOND / 8, timerCallback_turnOffLeds, NULL);

		  // set timer to repeat loop
		  etimer_set(&et1, BROADCAST_INTERVAL*CLOCK_SECOND);
		  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et1));
	  }

  }
  PROCESS_END();
}

AUTOSTART_PROCESSES(&broadcast_rssi_process, &alive_status_process);
