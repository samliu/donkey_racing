#if !defined(SerialControl_h)
#define SerialControl_h

#include <stdint.h>

class Stream;

/*
 * The protocol for the serial port connection is a number of free-standing messages.
 * Zero or more messages are framed inside a packet.
 * Packets are constructed as:
 * 
 * 0x55 0xAA <frameid> <lastseen> <length> <payload> <CRC16-L> <CRC16-H>
 * 
 * <frameid> is a byte serial number generated by the sending side.
 * 
 * <lastseen> is the byte serial number identifier of the last successfully 
 *    received packet that the other side saw us send, or 0 if none such.
 * 
 * <length> is one byte length of <payload> without the CRC.
 * 
 * CRC16 is CRC16 of <length> and <payload>.
 * 
 * The packet header bytes are chosen to filter out many common payloads as false 
 * positive, and also to filter out bad serial port baud rates / line noise as 
 * possible start bytes.
 * 
 * The <payload> is zero or more packets, marshaled as:
 * <type> <data>
 * <type> is a single byte ID.
 * 
 * Note that the size of <data> is implicit by <type>. If the receiving end doesn't 
 * know the length of a given <type> it will have to skip all data to the end of the 
 * packet.
 * 
 * The receiving end should be careful not to decode past the end of the length of 
 * the payload of a packet as indicated by the length field.
 * 
 */
class SerialControl {
  
  public:
  
    SerialControl(/*Stream &serial,*/ bool waitForFirstPacket = true);

    /*  Bind a given struct to the given ID. The struct layout in 
     *  memory will be replicated verbatim on the wire. This is 
     *  dangerous if you're talking to something with another 
     *  byte order or different alignment rules. Sort your struct 
     *  members from big to small and use little-endian and you 
     *  will, in practice, be fine.
     *  
     *  The effective maximum size of a packet is about 56 bytes of 
     *  payload. Don't make your struct bigger than that.
     *  
     *  autoSend means that the payload will be sent "once in a while" 
     *  which may depend on how much other data is being sent. Expect 
     *  it to be sent somewhere between every 500 ms and every 8000 ms 
     *  depending on load on the link.
     *  
     *  Call bind() again with NULL pointer to un-bind the struct or call
     *  it with the appropriate data pointer and autoSend flag and size 
     *  to update the in-memory struct represented by the id.
     */
    void bind(uint8_t id, void *data, uint8_t size, bool autoSend = false);

    /*  initiate talking the protocol
     */
    void begin();
    /*  Update the protocol/send/receive. Call this each time 
     *  through the main loop.
     */
    void step(uint32_t now);

    /*  Return the last serial number received from the remote side.
     *  Return 0 if no such serial received. The remote should not 
     *  send a serial of 0 to avoid the ambiguity.
     */
    uint8_t remoteSerial() const;

    /*  Check whether the 'received' flag is set for the given id.
     */
    bool isRecived(uint8_t id) const;
    /*  Remove (clear) the "received" flag for the given id.
     */
    void clearRecived(uint8_t id);

    /*  If ID is received and not yet flag-cleared, flag-clear 
     *  it and return true. Else return false.
     */
    bool getFresh(uint8_t id);
    /*  If the data pointed at by 'data' is received and not 
     *  yet flag-cleared, flag-clear it and return true. Else 
     *  return false.
     */
    bool getFresh(void const *data);

    /*  Enqueue a given item for immediate sending. Depending on 
     *  buffer space available, it may not actually be sent until 
     *  some number of step() calls later, but it's scheduled as 
     *  "send ASAP." sendNow() returns true if it found the packet  
     *  that you indicate bound. Even if sendNow() returns true, 
     *  you will have to call step() at least once to push out the
     *  data.
     */
    bool sendNow(uint8_t id);
    /*  Like sendNow(uint8_t) except pass a pointer to the actually 
     *  bound data to be sent. Again, returns true if the bound 
     *  data was found.
     */
    bool sendNow(void const *data);

    /*  Attempt to enqueue the given payload for sending with the 
     *  given ID. If there is not enough buffer space, return FALSE.
     */
    bool enqueuePayload(uint8_t id, void const *data, uint8_t length);

    enum {
      /*  The number of packets recognized (input and output together) 
       *  by the automatic management is set through MAX_ITEMS. Too large
       *  a number will cause higher CPU usage each time the serial port
       *  attempts to dispatch/recognize a packet.
       *  If there are packets you only use seldom, you can use the 
       *  enqueuePayload() and unknownPacketId() functions to handle them.
       */
      MAX_ITEMS = 8,
      /* These must be the size of RawHID packets */
      MAX_INBUF_SIZE = 64,
      MAX_OUTBUF_SIZE = 64
    };
    
  protected:

    /* Information about registered items */
    struct ItemInfo {
      uint8_t id;
      uint8_t flags;
      uint8_t size;
    };

    /*  For ItemInfo.flags */
    enum {
      FlagAutoSend = 0x02,
      FlagReceived = 0x04,
      FlagToSend = 0x08,
      FlagWasAutoSent = 0x10
    };

    /*  Framing has been detected, and CRC checks out. Look at the 
     *  buffer and pick it apart into separate packets, calling parsePacket()
     *  for each. You only need to override this for special processing.
     */
    virtual void parseFrame(uint8_t const *data, uint8_t length);
    
    /*  Given a packet in the frame, decode it, and return how much data was 
     *  consumed. If you don't know, return maxsize. Decoding includes moving 
     *  data to the bound output pointer, if available.
     */
    virtual uint8_t parsePacket(uint8_t type, uint8_t const *data, uint8_t maxsize);

    /*  If the ID of a packet is not known, this callback is called so that you 
     *  can do something useful with if if you want. Default does nothing.
     */
    virtual void unknownPacketId(uint8_t type, uint8_t const *data, uint8_t maxsize);

    /*  When discarding data because framing/length/CRC is bad, this hook is called.
     *  Not called when scanning for a start-of-packet sequence, though.
     */
    virtual void discardingData(uint8_t const *base, uint8_t length);

    void readInput(uint32_t now);
    void writeOutput(uint32_t now);
    
    /*Stream &serial_;*/

    /*  The implementation does a lot of linear look-ups in these arrays.
     *  Because the Teensy ARM chip actually has a cache, those look-ups 
     *  are reasonably fast as long as the maximum number of items is not
     *  too large.
     */
    ItemInfo infos_[MAX_ITEMS];
    void *datas_[MAX_ITEMS];

    uint32_t lastAutoSendTime_;
    uint8_t inBuf_[MAX_INBUF_SIZE];
    uint8_t outBuf_[MAX_INBUF_SIZE];
    uint8_t inPtr_;
    uint8_t outPtr_;
    bool waitForFirstPacket_;
    uint8_t lastRemoteSerial_;
    uint8_t mySerial_;
};

#endif  //  SerialControl_h

