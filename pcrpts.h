extern char PCRAvailable(char *Packet);
extern unsigned short GetPid(char *Packet);
extern char GetPTSFromPacket(unsigned char *Packet, unsigned long long *pts, unsigned long long *dts, int *PacketOffsetPTS, int *PacketOffsetDTS);
extern unsigned long long GetPCRFromPacket(unsigned char *Packet);
extern void set_timedts_pts(unsigned long long ts, unsigned char *buff);
extern void SetPacketPCR(unsigned char *Packet, unsigned long long pcr);