#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"

/* ******************************************************************
   Selective Repeat protocol implementation
   
   This implementation will support:
   - Sequence space at least twice the window size
   - Selective retransmission of only timed-out packets
   - Buffering of out-of-order packets at receiver
   - Individual acknowledgment of packets
*********************************************************************/

#define RTT  16.0       /* round trip time. MUST BE SET TO 16.0 when submitting assignment */
#define WINDOWSIZE 6    /* the maximum number of buffered unacked packet */
#define SEQSPACE 14     /* the sequence space for SR must be at least 2*windowsize */
#define NOTINUSE (-1)   /* used to fill header fields that are not being used */
#define FALSE 0
#define TRUE 1

/* generic procedure to compute the checksum of a packet */
int ComputeChecksum(struct pkt packet)
{
  int checksum = 0;
  int i;

  checksum = packet.seqnum;
  checksum += packet.acknum;
  for (i=0; i<20; i++) 
    checksum += (int)(packet.payload[i]);

  return checksum;
}

int IsCorrupted(struct pkt packet)
{
  if (packet.checksum == ComputeChecksum(packet))
    return (FALSE);
  else
    return (TRUE);
}

/********* Sender (A) variables and functions ************/

static struct pkt buffer[WINDOWSIZE];    /* array for storing packets waiting for ACK */
static int acked[WINDOWSIZE];           /* indicates whether packet has been ACKed */
static int timer_for_pkt;              /* which packet currently has the timer running (-1 if none) */
static int windowfirst, windowlast;      /* array indexes of the first/last packet awaiting ACK */
static int windowcount;                  /* the number of packets currently awaiting an ACK */
static int A_nextseqnum;                 /* the next sequence number to be used by the sender */

void A_init(void)
{
  int i;

  A_nextseqnum = 0;
  windowfirst = 0;
  windowlast = -1;
  windowcount = 0;
  timer_for_pkt = -1;
  
  for (i=0; i<WINDOWSIZE; i++) {
    acked[i] = FALSE;
  }
}
