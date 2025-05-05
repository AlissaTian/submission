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
#define SEQSPACE 12     /* the sequence space for SR must be at least 2*windowsize */
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

/* called from layer 5 (application layer), passed the message to be sent to other side */
void A_output(struct msg message)
{
  struct pkt sendpkt;
  int i;

  /* if not blocked waiting on ACK */
  if (windowcount < WINDOWSIZE) {
    if (TRACE > 1)
      printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

    /* create packet */
    sendpkt.seqnum = A_nextseqnum;
    sendpkt.acknum = NOTINUSE;
    for (i=0; i<20; i++) 
      sendpkt.payload[i] = message.data[i];
    sendpkt.checksum = ComputeChecksum(sendpkt); 

    /* put packet in window buffer */
    windowlast = (windowlast + 1) % WINDOWSIZE; 
    buffer[windowlast] = sendpkt;
    acked[windowlast] = FALSE;
    windowcount++;

    /* send out packet */
    if (TRACE > 0)
      printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
    tolayer3(A, sendpkt);

    /* start timer for this packet if no timer is running */
    if (timer_for_pkt == -1) {
      if (TRACE > 1)
        // printf("---A: resending packet %d\n", buffer[timer_for_pkt].seqnum);
      starttimer(A, RTT);
      timer_for_pkt = windowlast;
    }

    /* get next sequence number, wrap back to 0 */
    A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;  
  }
  /* if blocked, window is full */
  else {
    if (TRACE > 0)
      printf("----A: New message arrives, send window is full\n");
    window_full++;
  }
}

/* called from layer 3, when a packet arrives for layer 4 */
void A_input(struct pkt packet)
{
  int i, idx;
  int next_to_time = -1;

  /* if received ACK is not corrupted */ 
  if (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----A: uncorrupted ACK %d is received\n", packet.acknum);
    total_ACKs_received++;

    /* Ignore NAKs (NOTINUSE) */
    if (packet.acknum == NOTINUSE) {
      return;
    }

    /* find this ACK in our window */
    for (i=0; i<windowcount; i++) {
      idx = (windowfirst + i) % WINDOWSIZE;
      if (buffer[idx].seqnum == packet.acknum) {
        /* Mark as acknowledged if not already done */
        if (!acked[idx]) {
          acked[idx] = TRUE;
          new_ACKs++;
          
          /* If this was the packet we were timing, stop the timer */
          if (timer_for_pkt == idx) {
            stoptimer(A);
            timer_for_pkt = -1;
          }
        }
        break;
      }
    }
    
    /* Slide window if possible */
    while (windowcount > 0 && acked[windowfirst]) {
      windowfirst = (windowfirst + 1) % WINDOWSIZE;
      windowcount--;
    }
    
    /* If timer was stopped, find next unacked packet to time */
    if (timer_for_pkt == -1 && windowcount > 0) {
      for (i=0; i<windowcount; i++) {
        idx = (windowfirst + i) % WINDOWSIZE;
        if (!acked[idx]) {
          next_to_time = idx;
          break;
        }
      }
      
      if (next_to_time != -1) {
        // if (TRACE > 1)
//     printf("----A: Starting timer for packet %d\n", sendpkt.seqnum);
        starttimer(A, RTT);
        timer_for_pkt = next_to_time;
      }
    }
  }
  else {
    if (TRACE > 0)
      printf("----A: corrupted ACK is received, do nothing!\n");
  }
}

/* called when A's timer goes off */
void A_timerinterrupt(void)
{
  int i, idx;
  int next_to_time = -1;
  
  /* Make sure we know which packet's timer expired */
  if (timer_for_pkt != -1) {
    /* Check if this packet is still waiting for ACK (it might have been ACKed just before timeout) */
    if (!acked[timer_for_pkt]) {
      /* Resend this packet */
      if (TRACE > 0)
        printf("----A: time out, resend packet %d\n", buffer[timer_for_pkt].seqnum);
      printf("---A: resending packet %d\n", buffer[timer_for_pkt].seqnum);
      tolayer3(A, buffer[timer_for_pkt]);
      packets_resent++;
      
      /* Restart timer for this packet */
      if (TRACE > 1)
        printf("----A: Starting timer for packet %d\n", buffer[timer_for_pkt].seqnum);
      starttimer(A, RTT);
    } else {
      /* This packet was already ACKed, find next unacked packet */
      timer_for_pkt = -1;
      
      for (i=0; i<windowcount; i++) {
        idx = (windowfirst + i) % WINDOWSIZE;
        if (!acked[idx]) {
          next_to_time = idx;
          break;
        }
      }
      
      if (next_to_time != -1) {
        if (TRACE > 1)
          printf("----A: Starting timer for packet %d\n", buffer[next_to_time].seqnum);
        starttimer(A, RTT);
        timer_for_pkt = next_to_time;
      }
    }
  }
  /* If somehow we don't know which packet's timer expired, just time the first unacked packet */
  else if (windowcount > 0) {
    for (i=0; i<windowcount; i++) {
      idx = (windowfirst + i) % WINDOWSIZE;
      if (!acked[idx]) {
        next_to_time = idx;
        break;
      }
    }
    
    if (next_to_time != -1) {
      if (TRACE > 0)
        printf("----A: time out, resend packet %d\n", buffer[next_to_time].seqnum);
      printf("---A: resending packet %d\n", buffer[next_to_time].seqnum);
      tolayer3(A, buffer[next_to_time]);
      packets_resent++;
      
      if (TRACE > 1)
        printf("----A: Starting timer for packet %d\n", buffer[next_to_time].seqnum);
      starttimer(A, RTT);
      timer_for_pkt = next_to_time;
    }
  }
}

/********* Receiver (B) variables and procedures ************/

static int B_nextseqnum;                 /* the sequence number for the next packets sent by B */
static struct pkt B_buffer[WINDOWSIZE];  /* buffer for out-of-order packets */
static int B_received[WINDOWSIZE];       /* indicates whether packet has been received */
static int B_base;                       /* base sequence number of receive window */

void B_init(void)
{
  int i;
  
  B_nextseqnum = 1;
  B_base = 0;
  
  for (i=0; i<WINDOWSIZE; i++) {
    B_received[i] = FALSE;
  }
}

/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
  struct pkt sendpkt;
  int i, idx;
  int relative_seq;
  
  /* check if packet is corrupted */
  if (IsCorrupted(packet)) {
    if (TRACE > 0) 
      printf("----B: packet corrupted, send NAK!\n");
      
    sendpkt.acknum = NOTINUSE;
  }
  else {
    if (TRACE > 0)
      printf("----B: uncorrupted packet %d is received\n", packet.seqnum);
    
    /* Calculate relative sequence number in window */
    relative_seq = (packet.seqnum - B_base + SEQSPACE) % SEQSPACE;
    
    /* Check if packet is within receive window */
    if (relative_seq < WINDOWSIZE) {
      idx = relative_seq;
      
      /* Store packet if not already received */
      if (!B_received[idx]) {
        B_buffer[idx] = packet;
        B_received[idx] = TRUE;
        packets_received++;
      }
      
      /* Try to deliver in-order packets */
      while (B_received[0]) {
        if (TRACE > 0)
          printf("----B: delivering packet %d to layer5\n", B_base);
        tolayer5(B, B_buffer[0].payload);
        
        /* Slide window */
        for (i=0; i<WINDOWSIZE-1; i++) {
          B_received[i] = B_received[i+1];
          B_buffer[i] = B_buffer[i+1];
        }
        B_received[WINDOWSIZE-1] = FALSE;
        
        /* Update base sequence number */
        B_base = (B_base + 1) % SEQSPACE;
      }
      
      /* ACK this packet */
      sendpkt.acknum = packet.seqnum;
    }
    else if (((packet.seqnum - B_base + SEQSPACE) % SEQSPACE) >= SEQSPACE - WINDOWSIZE) {
      /* It's a duplicate of a packet we already received */
      if (TRACE > 0)
        printf("----B: packet outside receive window, likely old\n");
      sendpkt.acknum = packet.seqnum;
    }
    else {
      /* Packet is outside our window and not a duplicate */
      if (TRACE > 0)
        printf("----B: packet outside receive window\n");
      sendpkt.acknum = NOTINUSE;
    }
  }

  /* create ACK packet */
  sendpkt.seqnum = B_nextseqnum;
  B_nextseqnum = (B_nextseqnum + 1) % SEQSPACE;
    
  /* we don't have any data to send */
  for (i=0; i<20; i++) 
    sendpkt.payload[i] = '0';  

  /* compute checksum */
  sendpkt.checksum = ComputeChecksum(sendpkt); 

  /* send packet */
  tolayer3(B, sendpkt);
}

void B_output(struct msg message)  
{
}

void B_timerinterrupt(void)
{
}

