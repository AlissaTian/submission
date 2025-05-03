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
