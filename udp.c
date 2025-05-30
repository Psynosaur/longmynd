/* -------------------------------------------------------------------------------------------------- */
/* The LongMynd receiver: udp.c                                                                       */
/*    - an implementation of the Serit NIM controlling software for the MiniTiouner Hardware          */
/*    - linux udp handler to send the TS data to a remote display                                     */
/* Copyright 2019 Heather Lomond                                                                      */
/* -------------------------------------------------------------------------------------------------- */
/*
    This file is part of longmynd.

    Longmynd is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Longmynd is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with longmynd.  If not, see <https://www.gnu.org/licenses/>.
*/

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- INCLUDES ----------------------------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------- */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "errors.h"
#include "udp.h"
#include <mutex>
#include <unordered_set>
#include <CivetServer.h>
#include "pcrpts.h"

using namespace std;
/* -------------------------------------------------------------------------------------------------- */
/* ----------------- GLOBALS ------------------------------------------------------------------------ */
/* -------------------------------------------------------------------------------------------------- */

struct sockaddr_in servaddr_status;
struct sockaddr_in servaddr_ts;
int sockfd_status;
int sockfd_ts;

/* Dual-tuner UDP globals */
struct sockaddr_in servaddr_ts1;  /* Dedicated socket for tuner 1 in dual mode */
int sockfd_ts1;                   /* Dedicated socket for tuner 1 in dual mode */
struct sockaddr_in servaddr_ts2;
int sockfd_ts2;
bool dual_udp_initialized = false;

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- DEFINES ------------------------------------------------------------------------ */
/* -------------------------------------------------------------------------------------------------- */

#define WS_PORT "7682"
#define DOCUMENT_ROOT "."

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- ROUTINES ----------------------------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------- */
// ================= CIVETWEB ================================

class WsStartHandler : public CivetHandler
{
public:
    bool
    handleGet(CivetServer *server, struct mg_connection *conn)
    {

        mg_printf(conn,
                  "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: "
                  "close\r\n\r\n");

        mg_printf(conn, "<!DOCTYPE html>\n");
        mg_printf(conn, "<html>\n<head>\n");
        mg_printf(conn, "<meta charset=\"UTF-8\">\n");
        mg_printf(conn, "<title>Embedded websocket example</title>\n");

        /* mg_printf(conn, "<script type=\"text/javascript\"><![CDATA[\n"); ...
         * xhtml style */
        mg_printf(conn, "<script>\n");
        mg_printf(
            conn,
            "var i=0\n"
            "function load() {\n"
            "  var wsproto = (location.protocol === 'https:') ? 'wss:' : 'ws:';\n"
            "  connection = new WebSocket(wsproto + '//' + window.location.host + "
            "'/websocket');\n"
            "  websock_text_field = "
            "document.getElementById('websock_text_field');\n"
            "  connection.onmessage = function (e) {\n"
            "    websock_text_field.innerHTML=e.data;\n"
            "    i=i+1;"
            "    connection.send(i);\n"
            "  }\n"
            "  connection.onerror = function (error) {\n"
            "    alert('WebSocket error');\n"
            "    connection.close();\n"
            "  }\n"
            "}\n");
        /* mg_printf(conn, "]]></script>\n"); ... xhtml style */
        mg_printf(conn, "</script>\n");
        mg_printf(conn, "</head>\n<body onload=\"load()\">\n");
        mg_printf(
            conn,
            "<div id='websock_text_field'>No websocket connection yet</div>\n");
        mg_printf(conn, "</body>\n</html>\n");

        return 1;
    }
};

void extract_between_quotes(char *s, char *dest)
{
    int in_quotes = 0;
    *dest = 0;
    while (*s != 0)
    {
        if (in_quotes)
        {
            if (*s == '"')
                return;
            dest[0] = *s;
            dest[1] = 0;
            dest++;
        }
        else if (*s == '"')
            in_quotes = 1;
        s++;
    }
}

class WebSocketHandler : public CivetWebSocketHandler
{

private:
    /// Lock protecting \c connections_.
    std::mutex connectionsLock_;

    /// Set of connected clients to the entry point.
    std::unordered_set<mg_connection *> connections_;

public:
    virtual bool handleConnection(CivetServer *server,
                                  const struct mg_connection *conn)
    {
        fprintf(stderr, "WS connected\n");

        return true;
    }

    virtual void handleReadyState(CivetServer *server,
                                  struct mg_connection *conn)
    {
        // printf("WS ready\n");
        {
            std::lock_guard<std::mutex> l(connectionsLock_);
            connections_.insert(conn);
        }
        // update_web_param();
        //  process_param(span);
        //   process_param(centrefreq);
        /*
         send(centrefreq, strlen(centrefreq),MG_WEBSOCKET_OPCODE_TEXT);
        send(span, strlen(centrefreq),MG_WEBSOCKET_OPCODE_TEXT);
        */
    }

    virtual bool handleData(CivetServer *server,
                            struct mg_connection *conn,
                            int bits,
                            char *data,
                            size_t data_len)
    {
        // printf("WS got %lu bytes: ", (long unsigned)data_len);
        // fwrite(data, 1, data_len, stdout);
        // printf("\n");
        char message[255];
        strncpy(message, data, data_len);
        char name[255];
        extract_between_quotes(message, name);
        fprintf(stderr, "Message received : %s : name %s ", message, name);
        if (strcmp(name, "freq") == 0)
        {
            char *pch;
            pch = strstr(message, ":");
            char value[255];
            strncpy(value, pch + 1, strlen(pch - 2));
            // set_frequency_span(atof(value), span);
        }

        return (data_len < 4);
    }

    virtual void handleClose(CivetServer *server,
                             const struct mg_connection *conn)
    {

        std::lock_guard<std::mutex> l(connectionsLock_);

        auto it = connections_.find(const_cast<struct mg_connection *>(conn));
        if (it != connections_.end())
            connections_.erase(it);
        // printf("WS closed\n");
    }

    void process(uint8_t *tsbuff, int tssize)
    {

        send((char *)tsbuff, tssize, MG_WEBSOCKET_OPCODE_BINARY);
    }

    void process_param(char *param)
    {
        fprintf(stderr, "Param : %s\n", param);
        send(param, strlen(param), MG_WEBSOCKET_OPCODE_TEXT);
    }

    void send(char *data, int len, int MessageType)
    {
        std::lock_guard<std::mutex> l(connectionsLock_);
        std::vector<struct mg_connection *> errors;
        for (auto it : connections_)
        {
            if (0 >= mg_websocket_write(it, MessageType, data, len))
                errors.push_back(it);
        }

        for (auto it : errors)
        for (auto it : errors)
            connections_.erase(it);
    }
};






size_t video_pcrpts = 0;
size_t audio_pcrpts = 0;
long transmission_delay=0;

void udp_send_normalize(u_int8_t *b, int len)
{
#define BUFF_MAX_SIZE (7 * 188)
    static u_int8_t *Buffer = NULL;
    if (Buffer == NULL)
        Buffer = (u_int8_t *)malloc(BUFF_MAX_SIZE * 2);

    static int Size = 0;
    // Align to Sync Packet
    static bool IsSync = false;

    // fprintf(stderr,"len %d Size %d\n",len,Size);

    if ((IsSync == false) && (len >= 2 * 188))
    {
        int start_packet = 0;
        bool sync_found = false;
        for (start_packet = 0; start_packet < 188; start_packet++)
        {
            if ((b[start_packet] == 0x47) && (b[start_packet + 188] == 0x47))
            {
                b = b + start_packet;
                len = len - start_packet;
                IsSync = true;
                sync_found = true;
                fprintf(stderr, "Recover Sync %d\n", start_packet);
                break;
            }
        }

        if (!sync_found) {
            fprintf(stderr, "Not Sync!\n");
        }
    }

    if (Size > 0 && Buffer[0] != 0x47)
    {
        if (Size >= 188)
        {
            IsSync = false;
            Size = 0;
            fprintf(stderr, "Lost Sync\n");
            return;
        }
    }

    if ((Size + len) >= BUFF_MAX_SIZE)
    {
        memcpy(Buffer + Size, b, len);

        // fprintf(stderr,"len %d Size %d %x\n",len,Size,Buffer[0]);
        if (IsSync)
        {
            //ProcessCorectPCR(Buffer, BUFF_MAX_SIZE);
            ProcessTSTiming(Buffer, BUFF_MAX_SIZE,&video_pcrpts,&audio_pcrpts,&transmission_delay);
        }
        if (sendto(sockfd_ts, Buffer, BUFF_MAX_SIZE, 0, (const struct sockaddr *)&servaddr_ts, sizeof(struct sockaddr)) < 0)
        {
            fprintf(stderr, "UDP send failed\n");
        }
        // h_websocket.process(Buffer, BUFF_MAX_SIZE);
        memmove(Buffer, Buffer + BUFF_MAX_SIZE, Size - BUFF_MAX_SIZE + len);
        Size += len;
        Size = Size - BUFF_MAX_SIZE;
    }
    else
    {
        memcpy(Buffer + Size, b, len);
        Size += len;
    }
}

#define CRC_POLY 0xAB
// Reversed
#define CRC_POLYR 0xD5
uint8_t m_crc_tab[256];
void build_crc8_table(void)
{
    int r, crc;
    fprintf(stderr, "Init crc8\n");
    for (int i = 0; i < 256; i++)
    {
        r = i;
        crc = 0;
        for (int j = 7; j >= 0; j--)
        {
            if ((r & (1 << j) ? 1 : 0) ^ ((crc & 0x80) ? 1 : 0))
                crc = (crc << 1) ^ CRC_POLYR;
            else
                crc <<= 1;
        }
        m_crc_tab[i] = crc;
    }
}

uint8_t calc_crc8(uint8_t *b, int len)
{
    uint8_t crc = 0;

    for (int i = 0; i < len; i++)
    {
        crc = m_crc_tab[b[i] ^ crc];
    }
    return crc;
}

#define BBFRAME_MAX_LEN 7274
void udp_bb_defrag(u_int8_t *b, int len, bool withheader)
{
    static unsigned char BBFrame[BBFRAME_MAX_LEN];
    static int offset = 0;
    static int dfl = 0;
    static int count = 0;
    int end = false;
    int idx_b = 0;

    if (offset + len > BBFRAME_MAX_LEN)
    {
        fprintf(stderr, "bbframe overflow! %d/%d\n", offset, len);
        offset = 0;
        return;
    }

    // if(((b[0]&0xC0)==0x70)&&(b[1]==0x0)) // FixeMe : SHould be better to compute crc to be sure it is a header
    if ((offset == 0) && (b[0] != 0x72))
    {
        fprintf(stderr, "BBFRAME padding ? %x\n", b[0]);
        return;
    }
    if ((offset == 0) && (len >= 10) && (calc_crc8(b, 9) == b[9]))
    {
        // offset=0; //Start of bbframe header
        dfl = (((int)b[4] << 8) + (int)b[5]) / 8 + 10;
    }
    if (dfl == 0)
    {
        fprintf(stderr, "wrong dfl size %d\n", len);

        /*
        for(int i=0;i<len;i++)
        {
            fprintf(stderr,"%x ",b[i]);
        }
        fprintf(stderr,"\n");
        */
        return;
    }
    if (offset + len < dfl)
    {
        // fprintf(stderr,"bbframe # %d : %d/%d\n",count,offset+len,dfl);
        memcpy(BBFrame + offset, b, len);
        offset += len;
        return;
    }

    if (offset + len == dfl)
    {
        memcpy(BBFrame + offset, b, len);
        fprintf(stderr, "Complete bbframe # %d : %d/%d\n", count, offset + len, dfl);
        sendto(sockfd_ts, BBFrame, dfl, 0, (const struct sockaddr *)&servaddr_ts, sizeof(struct sockaddr));
        offset = 0;
        count++;
        return;
    }

    if (offset + len > dfl)
    {
        // fprintf(stderr,"------------------------ Partial bbframe # %d : %d/%d\n",count,offset+len,dfl);
        memcpy(BBFrame + offset, b, dfl - offset);
        sendto(sockfd_ts, BBFrame, dfl, 0, (const struct sockaddr *)&servaddr_ts, sizeof(struct sockaddr));
        fprintf(stderr, "First Complete bbframe # %d : %d/%d\n", count, offset + dfl - offset, dfl);

        int size = len - (dfl - offset);
        /*
          for(int i=0;i<len;i++)
        {
            if(i==dfl-offset) fprintf(stderr,"/////// \n");
            fprintf(stderr,"%x ",b[i]);
        }
        fprintf(stderr,"\n");
        */
        int oldoffset = offset;
        int olddfl = dfl;
        offset = 0;
        dfl = 0;

        fprintf(stderr, "Recursive with size %d\n", size);
        for (int i = 0; i < size; i++)
        {
            fprintf(stderr, "%x ", b[i + olddfl - oldoffset]);
        }
        fprintf(stderr, "\n");

        udp_bb_defrag(b + olddfl - oldoffset, size, true);
    }
}

uint8_t udp_ts_write(uint8_t *buffer, uint32_t len, bool *output_ready)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* takes a buffer and writes out the contents to udp socket                                           */
    /* *buffer: the buffer that contains the data to be sent                                              */
    /*     len: the length (number of bytes) of data to be sent                                           */
    /*  return: error code                                                                                */
    /* -------------------------------------------------------------------------------------------------- */
    (void)output_ready;
    uint8_t err = ERROR_NONE;
    int32_t remaining_len; /* note it is signed so can go negative */
    uint32_t write_size;

    remaining_len = len;

    /* we need to loop round sending 510 byte chunks so that we can skip the 2 extra bytes put in by */
    /* the FTDI chip every 512 bytes of USB message */
    while (remaining_len > 0)
    {
        if (remaining_len > 510)
        {
            /* calculate where to start in the buffer and how many bytes to send */
            write_size = 510;
            // fprintf(stderr,"1Usb len %d remainin %d\n",len,remaining_len);
            udp_send_normalize(&buffer[len - remaining_len], write_size);
            // sendto(sockfd_ts, &buffer[len-remaining_len], write_size, 0,(const struct sockaddr *) &servaddr_ts,  sizeof(struct sockaddr));
            /* note we skip over the 2 bytes inserted by the FTDI */
            remaining_len -= 512;
        }
        else
        {
            write_size = remaining_len;
            /// fprintf(stderr,"2 Usb len %d remainin %d\n",len,remaining_len);
            udp_send_normalize(&buffer[len - remaining_len], write_size);
            // sendto(sockfd_ts, &buffer[len-remaining_len], write_size, 0,(const struct sockaddr *) &servaddr_ts,  sizeof(struct sockaddr));
            remaining_len -= write_size; /* should be 0 if all went well */
        }
    }

    /* if someting went bad with our calcs, remaining will not be 0 */
    if ((err == ERROR_NONE) && (remaining_len != 0))
    {
        printf("ERROR: UDP socket write incorrect number of bytes\n");
        err = ERROR_UDP_WRITE;
    }

    if (err != ERROR_NONE)
        printf("ERROR: UDP socket ts write\n");

    return err;
}

uint8_t udp_bb_write(uint8_t *buffer, uint32_t len, bool *output_ready)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* takes a buffer and writes out the contents to udp socket                                           */
    /* *buffer: the buffer that contains the data to be sent                                              */
    /*     len: the length (number of bytes) of data to be sent                                           */
    /*  return: error code                                                                                */
    /* -------------------------------------------------------------------------------------------------- */
    (void)output_ready;
    uint8_t err = ERROR_NONE;
    int32_t remaining_len; /* note it is signed so can go negative */
    uint32_t write_size;

    remaining_len = len;

    /* we need to loop round sending 510 byte chunks so that we can skip the 2 extra bytes put in by */
    /* the FTDI chip every 512 bytes of USB message */
    // fprintf(stderr,"bbframe %d\n",len);
    while (remaining_len > 0)
    {
        if (remaining_len > 510)
        {
            /* calculate where to start in the buffer and how many bytes to send */
            write_size = 510;

            udp_bb_defrag(&buffer[len - remaining_len], write_size, true);
            // sendto(sockfd_ts, &buffer[len-remaining_len], write_size, 0,(const struct sockaddr *) &servaddr_ts,  sizeof(struct sockaddr));
            remaining_len -= 512;
        }
        else
        {
            write_size = remaining_len;
            udp_bb_defrag(&buffer[len - remaining_len], write_size, true);
            // sendto(sockfd_ts, &buffer[len-remaining_len], write_size, 0,(const struct sockaddr *) &servaddr_ts,  sizeof(struct sockaddr));
            remaining_len -= write_size; /* should be 0 if all went well */
        }
    }

    /* if someting went bad with our calcs, remaining will not be 0 */
    if ((err == ERROR_NONE) && (remaining_len != 0))
    {
        printf("ERROR: UDP socket write incorrect number of bytes\n");
        err = ERROR_UDP_WRITE;
    }

    if (err != ERROR_NONE)
        printf("ERROR: UDP socket ts write\n");

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t udp_status_write(uint8_t message, uint32_t data, bool *output_ready)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* takes a buffer and writes out the contents to udp socket                                           */
    /* *buffer: the buffer that contains the data to be sent                                              */
    /*     len: the length (number of bytes) of data to be sent                                           */
    /*  return: error code                                                                                */
    /* -------------------------------------------------------------------------------------------------- */
    (void)output_ready;
    uint8_t err = ERROR_NONE;
    char status_message[30];

    /* WARNING: This currently prints as signed integer (int32_t), even though function appears to expect unsigned (uint32_t) */
    sprintf(status_message, "$%i,%i\n", message, data);

    sendto(sockfd_status, status_message, strlen(status_message), 0, (const struct sockaddr *)&servaddr_status, sizeof(struct sockaddr));

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t udp_status_string_write(uint8_t message, char *data, bool *output_ready)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* takes a buffer and writes out the contents to udp socket                                           */
    /* *buffer: the buffer that contains the data to be sent                                              */
    /*     len: the length (number of bytes) of data to be sent                                           */
    /*  return: error code                                                                                */
    /* -------------------------------------------------------------------------------------------------- */
    (void)output_ready;
    uint8_t err = ERROR_NONE;
    char status_message[5 + 128];

    sprintf(status_message, "$%i,%s\n", message, data);

    sendto(sockfd_status, status_message, strlen(status_message), 0, (const struct sockaddr *)&servaddr_status, sizeof(struct sockaddr));

    return err;
}

CivetServer *server; // <-- C++ style start
/* -------------------------------------------------------------------------------------------------- */
static uint8_t udp_init(struct sockaddr_in *servaddr_ptr, int *sockfd_ptr, char *udp_ip, int udp_port)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* initialises the udp socket                                                                         */
    /*    udp_ip: the ip address (as a string) of the socket to open                                      */
    /*  udp_port: the UDP port to be opened at the given IP address                                       */
    /*    return: error code                                                                              */
    /* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;

    printf("Flow: UDP Init\n");
    build_crc8_table();
    /* Creat the socket  for IPv4 and UDP */
    if ((*sockfd_ptr = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
    {
        printf("ERROR: socket creation failed\n");
        err = ERROR_UDP_SOCKET_OPEN;
    }
    else
    {
        /* setup all the destination fields */
        memset(servaddr_ptr, 0, sizeof(struct sockaddr_in));
        servaddr_ptr->sin_family = AF_INET;
        servaddr_ptr->sin_port = htons(udp_port);
        servaddr_ptr->sin_addr.s_addr = inet_addr(udp_ip); // INADDR_ANY;
    }
    if (err != ERROR_NONE)
        printf("ERROR: UDP init\n");

    return err;
}

uint8_t udp_status_init(char *udp_ip, int udp_port)
{
    return udp_init(&servaddr_status, &sockfd_status, udp_ip, udp_port);
}

uint8_t udp_ts_init(char *udp_ip, int udp_port)
{

    uint8_t err = udp_init(&servaddr_ts, &sockfd_ts, udp_ip, udp_port);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t udp_close(void)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* closes the udp socket                                                                              */
    /* return: error code                                                                                 */
    /* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;
    int ret;

    printf("Flow: UDP Close\n");

    ret = close(sockfd_ts);
    if (ret != 0)
    {
        err = ERROR_UDP_CLOSE;
        printf("ERROR: TS UDP close\n");
    }
    ret = close(sockfd_status);
    if (ret != 0)
    {
        err = ERROR_UDP_CLOSE;
        printf("ERROR: Status UDP close\n");
    }

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
/* Dual-tuner UDP functions                                                                           */
/* -------------------------------------------------------------------------------------------------- */

uint8_t udp_ts_init_dual(char *udp_ip1, int udp_port1, char *udp_ip2, int udp_port2)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Initialize dual UDP sockets for dual-tuner TS streaming                                           */
    /*   udp_ip1: IP address for tuner 1 (TOP demodulator)                                               */
    /*   udp_port1: Port for tuner 1                                                                     */
    /*   udp_ip2: IP address for tuner 2 (BOTTOM demodulator)                                            */
    /*   udp_port2: Port for tuner 2                                                                     */
    /*   return: error code                                                                              */
    /* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;

    printf("Flow: UDP dual init - Tuner1: %s:%d, Tuner2: %s:%d\n",
           udp_ip1, udp_port1, udp_ip2, udp_port2);

    /* Initialize dedicated tuner 1 UDP socket */
    if (err == ERROR_NONE) {
        err = udp_init(&servaddr_ts1, &sockfd_ts1, udp_ip1, udp_port1);
    }

    /* Initialize tuner 2 UDP socket */
    if (err == ERROR_NONE) {
        err = udp_init(&servaddr_ts2, &sockfd_ts2, udp_ip2, udp_port2);
    }

    if (err == ERROR_NONE) {
        dual_udp_initialized = true;
        printf("Flow: UDP dual init successful\n");
        printf("      Tuner 1 socket: fd=%d, IP=%s:%d\n", sockfd_ts1, udp_ip1, udp_port1);
        printf("      Tuner 2 socket: fd=%d, IP=%s:%d\n", sockfd_ts2, udp_ip2, udp_port2);

        /* Validate socket addresses */
        struct sockaddr_in addr1, addr2;
        socklen_t addr_len = sizeof(struct sockaddr_in);
        if (getsockname(sockfd_ts1, (struct sockaddr*)&addr1, &addr_len) == 0) {
            printf("      Tuner 1 socket bound successfully\n");
        } else {
            printf("WARNING: Tuner 1 socket binding issue\n");
        }
        if (getsockname(sockfd_ts2, (struct sockaddr*)&addr2, &addr_len) == 0) {
            printf("      Tuner 2 socket bound successfully\n");
        } else {
            printf("WARNING: Tuner 2 socket binding issue\n");
        }
    } else {
        printf("ERROR: UDP dual init failed\n");
    }

    return err;
}

uint8_t udp_ts_write_tuner1(uint8_t *buffer, uint32_t len, bool *output_ready)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Write TS data for tuner 1 (TOP demodulator) - uses dedicated UDP socket                          */
    /*   buffer: data buffer to send                                                                     */
    /*   len: length of data                                                                             */
    /*   output_ready: output ready flag                                                                 */
    /*   return: error code                                                                              */
    /* -------------------------------------------------------------------------------------------------- */
    (void)output_ready;
    uint8_t err = ERROR_NONE;
    int32_t remaining_len;
    uint32_t write_size;
    static uint32_t call_count = 0;

    if (!dual_udp_initialized) {
        printf("ERROR: Dual UDP not initialized\n");
        return ERROR_UDP_WRITE;
    }

    call_count++;
    if (call_count % 100 == 1) {  /* Log every 100th call to avoid spam */
        printf("DEBUG: Tuner1 UDP write called #%u: len=%u, first_byte=0x%02x\n",
               call_count, len, len > 0 ? buffer[0] : 0);
    }

    remaining_len = len;

    /* Process data in 510 byte chunks (same as tuner 2) */
    while (remaining_len > 0) {
        if (remaining_len > 510) {
            write_size = 510;
            udp_send_normalize_tuner1(&buffer[len - remaining_len], write_size);
            remaining_len -= 512;
        } else {
            write_size = remaining_len;
            udp_send_normalize_tuner1(&buffer[len - remaining_len], write_size);
            remaining_len -= write_size;
        }
    }

    if ((err == ERROR_NONE) && (remaining_len != 0)) {
        printf("ERROR: UDP tuner1 write incorrect number of bytes\n");
        err = ERROR_UDP_WRITE;
    }

    if (err != ERROR_NONE) {
        printf("ERROR: UDP tuner1 TS write\n");
    }

    return err;
}

uint8_t udp_ts_write_tuner2(uint8_t *buffer, uint32_t len, bool *output_ready)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Write TS data for tuner 2 (BOTTOM demodulator) - uses second UDP socket                          */
    /*   buffer: data buffer to send                                                                     */
    /*   len: length of data                                                                             */
    /*   output_ready: output ready flag                                                                 */
    /*   return: error code                                                                              */
    /* -------------------------------------------------------------------------------------------------- */
    (void)output_ready;
    uint8_t err = ERROR_NONE;
    int32_t remaining_len;
    uint32_t write_size;
    static uint32_t call_count = 0;

    if (!dual_udp_initialized) {
        printf("ERROR: Dual UDP not initialized\n");
        return ERROR_UDP_WRITE;
    }

    call_count++;
    if (call_count % 100 == 1) {  /* Log every 100th call to avoid spam */
        printf("DEBUG: Tuner2 UDP write called #%u: len=%u, first_byte=0x%02x\n",
               call_count, len, len > 0 ? buffer[0] : 0);
    }

    remaining_len = len;

    /* Process data in 510 byte chunks (same as tuner 1) */
    while (remaining_len > 0) {
        if (remaining_len > 510) {
            write_size = 510;
            udp_send_normalize_tuner2(&buffer[len - remaining_len], write_size);
            remaining_len -= 512;
        } else {
            write_size = remaining_len;
            udp_send_normalize_tuner2(&buffer[len - remaining_len], write_size);
            remaining_len -= write_size;
        }
    }

    if ((err == ERROR_NONE) && (remaining_len != 0)) {
        printf("ERROR: UDP tuner2 write incorrect number of bytes\n");
        err = ERROR_UDP_WRITE;
    }

    if (err != ERROR_NONE) {
        printf("ERROR: UDP tuner2 TS write\n");
    }

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
void udp_send_normalize_tuner2(uint8_t *b, int len)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Normalize and send TS data for tuner 2 (similar to udp_send_normalize but for second socket)     */
    /*   b: data buffer                                                                                  */
    /*   len: length of data                                                                             */
    /* -------------------------------------------------------------------------------------------------- */
#define BUFF_MAX_SIZE_T2 (7 * 188)
    static uint8_t *Buffer_t2 = NULL;
    if (Buffer_t2 == NULL)
        Buffer_t2 = (uint8_t *)malloc(BUFF_MAX_SIZE_T2 * 2);

    static int Size_t2 = 0;
    static bool IsSync_t2 = false;

    /* Align to Sync Packet */
    if ((IsSync_t2 == false) && (len >= 2 * 188)) {
        int start_packet = 0;
        for (start_packet = 0; start_packet < 188; start_packet++) {
            if ((b[start_packet] == 0x47) && (b[start_packet + 188] == 0x47)) {
                b = b + start_packet;
                len = len - start_packet;
                IsSync_t2 = true;
                fprintf(stderr, "Tuner2: Recover Sync %d\n", start_packet);
                break;
            }
        }
        if (!IsSync_t2) {
            fprintf(stderr, "Tuner2: Not Sync!\n");
        }
    }

    if (Size_t2 > 0 && Buffer_t2[0] != 0x47) {
        if (Size_t2 >= 188) {
            IsSync_t2 = false;
            Size_t2 = 0;
            fprintf(stderr, "Tuner2: Lost Sync\n");
            return;
        }
    }

    if ((Size_t2 + len) >= BUFF_MAX_SIZE_T2) {
        memcpy(Buffer_t2 + Size_t2, b, len);

        if (IsSync_t2) {
            /* Process TS timing for tuner 2 if needed */
            /* ProcessTSTiming(Buffer_t2, BUFF_MAX_SIZE_T2, &video_pcrpts, &audio_pcrpts, &transmission_delay); */
        }

        static uint32_t send_count_t2 = 0;
        send_count_t2++;

        ssize_t sent_bytes = sendto(sockfd_ts2, Buffer_t2, BUFF_MAX_SIZE_T2, 0,
                                   (const struct sockaddr *)&servaddr_ts2, sizeof(struct sockaddr));
        if (sent_bytes < 0) {
            fprintf(stderr, "Tuner2: UDP send failed\n");
        } else if (send_count_t2 % 1000 == 1) {  /* Log every 1000th send */
            printf("DEBUG: Tuner2 UDP sent #%u: %zd bytes to 230.0.0.3:1234\n",
                   send_count_t2, sent_bytes);
        }

        memmove(Buffer_t2, Buffer_t2 + BUFF_MAX_SIZE_T2, Size_t2 - BUFF_MAX_SIZE_T2 + len);
        Size_t2 += len;
        Size_t2 = Size_t2 - BUFF_MAX_SIZE_T2;
    } else {
        memcpy(Buffer_t2 + Size_t2, b, len);
        Size_t2 += len;
    }
}

/* -------------------------------------------------------------------------------------------------- */
void udp_send_normalize_tuner1(uint8_t *b, int len)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Normalize and send TS data for tuner 1 (similar to udp_send_normalize but for dedicated socket)  */
    /*   b: data buffer                                                                                  */
    /*   len: length of data                                                                             */
    /* -------------------------------------------------------------------------------------------------- */
#define BUFF_MAX_SIZE_T1 (7 * 188)
    static uint8_t *Buffer_t1 = NULL;
    if (Buffer_t1 == NULL)
        Buffer_t1 = (uint8_t *)malloc(BUFF_MAX_SIZE_T1 * 2);

    static int Size_t1 = 0;
    static bool IsSync_t1 = false;

    /* Align to Sync Packet */
    if ((IsSync_t1 == false) && (len >= 2 * 188)) {
        int start_packet = 0;
        for (start_packet = 0; start_packet < 188; start_packet++) {
            if ((b[start_packet] == 0x47) && (b[start_packet + 188] == 0x47)) {
                b = b + start_packet;
                len = len - start_packet;
                IsSync_t1 = true;
                fprintf(stderr, "Tuner1: Recover Sync %d\n", start_packet);
                break;
            }
        }
        if (!IsSync_t1) {
            fprintf(stderr, "Tuner1: Not Sync!\n");
        }
    }

    if (Size_t1 > 0 && Buffer_t1[0] != 0x47) {
        if (Size_t1 >= 188) {
            IsSync_t1 = false;
            Size_t1 = 0;
            fprintf(stderr, "Tuner1: Lost Sync\n");
            return;
        }
    }

    if ((Size_t1 + len) >= BUFF_MAX_SIZE_T1) {
        memcpy(Buffer_t1 + Size_t1, b, len);

        if (IsSync_t1) {
            /* Process TS timing for tuner 1 if needed */
            /* ProcessTSTiming(Buffer_t1, BUFF_MAX_SIZE_T1, &video_pcrpts, &audio_pcrpts, &transmission_delay); */
        }

        static uint32_t send_count_t1 = 0;
        send_count_t1++;

        ssize_t sent_bytes = sendto(sockfd_ts1, Buffer_t1, BUFF_MAX_SIZE_T1, 0,
                                   (const struct sockaddr *)&servaddr_ts1, sizeof(struct sockaddr));
        if (sent_bytes < 0) {
            fprintf(stderr, "Tuner1: UDP send failed\n");
        } else if (send_count_t1 % 1000 == 1) {  /* Log every 1000th send */
            printf("DEBUG: Tuner1 UDP sent #%u: %zd bytes to 230.0.0.2:1234\n",
                    send_count_t1, sent_bytes);
        }

        memmove(Buffer_t1, Buffer_t1 + BUFF_MAX_SIZE_T1, Size_t1 - BUFF_MAX_SIZE_T1 + len);
        Size_t1 += len;
        Size_t1 = Size_t1 - BUFF_MAX_SIZE_T1;
    } else {
        memcpy(Buffer_t1 + Size_t1, b, len);
        Size_t1 += len;
    }
}

uint8_t udp_bb_write_tuner1(uint8_t *buffer, uint32_t len, bool *output_ready)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Write BB frame data for tuner 1 (TOP demodulator) - uses dedicated UDP socket                    */
    /*   buffer: data buffer to send                                                                     */
    /*   len: length of data                                                                             */
    /*   output_ready: output ready flag                                                                 */
    /*   return: error code                                                                              */
    /* -------------------------------------------------------------------------------------------------- */
    (void)output_ready;
    uint8_t err = ERROR_NONE;
    int32_t remaining_len;
    uint32_t write_size;

    if (!dual_udp_initialized) {
        printf("ERROR: Dual UDP not initialized\n");
        return ERROR_UDP_WRITE;
    }

    remaining_len = len;

    /* Process data in 510 byte chunks (same as tuner 2) */
    while (remaining_len > 0) {
        if (remaining_len > 510) {
            write_size = 510;
            udp_bb_defrag_tuner1(&buffer[len - remaining_len], write_size, true);
            remaining_len -= 512;
        } else {
            write_size = remaining_len;
            udp_bb_defrag_tuner1(&buffer[len - remaining_len], write_size, true);
            remaining_len -= write_size;
        }
    }

    if ((err == ERROR_NONE) && (remaining_len != 0)) {
        printf("ERROR: UDP tuner1 BB write incorrect number of bytes\n");
        err = ERROR_UDP_WRITE;
    }

    if (err != ERROR_NONE) {
        printf("ERROR: UDP tuner1 BB write\n");
    }

    return err;
}

uint8_t udp_bb_write_tuner2(uint8_t *buffer, uint32_t len, bool *output_ready)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Write BB frame data for tuner 2 (BOTTOM demodulator) - uses second UDP socket                    */
    /*   buffer: data buffer to send                                                                     */
    /*   len: length of data                                                                             */
    /*   output_ready: output ready flag                                                                 */
    /*   return: error code                                                                              */
    /* -------------------------------------------------------------------------------------------------- */
    (void)output_ready;
    uint8_t err = ERROR_NONE;
    int32_t remaining_len;
    uint32_t write_size;

    if (!dual_udp_initialized) {
        printf("ERROR: Dual UDP not initialized\n");
        return ERROR_UDP_WRITE;
    }

    remaining_len = len;

    /* Process data in 510 byte chunks (same as tuner 1) */
    while (remaining_len > 0) {
        if (remaining_len > 510) {
            write_size = 510;
            udp_bb_defrag_tuner2(&buffer[len - remaining_len], write_size, true);
            remaining_len -= 512;
        } else {
            write_size = remaining_len;
            udp_bb_defrag_tuner2(&buffer[len - remaining_len], write_size, true);
            remaining_len -= write_size;
        }
    }

    if ((err == ERROR_NONE) && (remaining_len != 0)) {
        printf("ERROR: UDP tuner2 BB write incorrect number of bytes\n");
        err = ERROR_UDP_WRITE;
    }

    if (err != ERROR_NONE) {
        printf("ERROR: UDP tuner2 BB write\n");
    }

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
void udp_bb_defrag_tuner2(uint8_t *b, int len, bool withheader)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* BB frame defragmentation for tuner 2 (similar to udp_bb_defrag but for second socket)            */
    /*   b: data buffer                                                                                  */
    /*   len: length of data                                                                             */
    /*   withheader: whether header is included                                                          */
    /* -------------------------------------------------------------------------------------------------- */
    static unsigned char BBFrame_t2[BBFRAME_MAX_LEN];
    static int offset_t2 = 0;
    static int dfl_t2 = 0;
    static int count_t2 = 0;
    int idx_b = 0;
    (void)withheader;

    if (offset_t2 + len > BBFRAME_MAX_LEN) {
        fprintf(stderr, "Tuner2: bbframe overflow! %d/%d\n", offset_t2, len);
        offset_t2 = 0;
        return;
    }

    if ((offset_t2 == 0) && (b[0] != 0x72)) {
        fprintf(stderr, "Tuner2: BBFRAME padding ? %x\n", b[0]);
        return;
    }

    if ((offset_t2 == 0) && (len >= 10) && (calc_crc8(b, 9) == b[9])) {
        dfl_t2 = (((int)b[4] << 8) + (int)b[5]) / 8 + 10;
    }

    if (dfl_t2 == 0) {
        fprintf(stderr, "Tuner2: wrong dfl size %d\n", len);
        return;
    }

    if (offset_t2 + len < dfl_t2) {
        memcpy(BBFrame_t2 + offset_t2, b, len);
        offset_t2 += len;
        return;
    }

    if (offset_t2 + len == dfl_t2) {
        memcpy(BBFrame_t2 + offset_t2, b, len);
        fprintf(stderr, "Tuner2: Complete bbframe # %d : %d/%d\n", count_t2, offset_t2 + len, dfl_t2);
        sendto(sockfd_ts2, BBFrame_t2, dfl_t2, 0,
               (const struct sockaddr *)&servaddr_ts2, sizeof(struct sockaddr));
        offset_t2 = 0;
        count_t2++;
        return;
    }

    if (offset_t2 + len > dfl_t2) {
        memcpy(BBFrame_t2 + offset_t2, b, dfl_t2 - offset_t2);
        sendto(sockfd_ts2, BBFrame_t2, dfl_t2, 0,
               (const struct sockaddr *)&servaddr_ts2, sizeof(struct sockaddr));
        fprintf(stderr, "Tuner2: First Complete bbframe # %d : %d/%d\n", count_t2, offset_t2 + dfl_t2 - offset_t2, dfl_t2);

        int size = len - (dfl_t2 - offset_t2);
        int oldoffset = offset_t2;
        int olddfl = dfl_t2;
        offset_t2 = 0;
        dfl_t2 = 0;

        fprintf(stderr, "Tuner2: Recursive with size %d\n", size);
        udp_bb_defrag_tuner2(b + olddfl - oldoffset, size, true);
    }
}

/* -------------------------------------------------------------------------------------------------- */
void udp_bb_defrag_tuner1(uint8_t *b, int len, bool withheader)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* BB frame defragmentation for tuner 1 (similar to udp_bb_defrag but for dedicated socket)        */
    /*   b: data buffer                                                                                  */
    /*   len: length of data                                                                             */
    /*   withheader: whether header is included                                                          */
    /* -------------------------------------------------------------------------------------------------- */
    static unsigned char BBFrame_t1[BBFRAME_MAX_LEN];
    static int offset_t1 = 0;
    static int dfl_t1 = 0;
    static int count_t1 = 0;
    int idx_b = 0;
    (void)withheader;

    if (offset_t1 + len > BBFRAME_MAX_LEN) {
        fprintf(stderr, "Tuner1: bbframe overflow! %d/%d\n", offset_t1, len);
        offset_t1 = 0;
        return;
    }

    if ((offset_t1 == 0) && (b[0] != 0x72)) {
        fprintf(stderr, "Tuner1: BBFRAME padding ? %x\n", b[0]);
        return;
    }

    if ((offset_t1 == 0) && (len >= 10) && (calc_crc8(b, 9) == b[9])) {
        dfl_t1 = (((int)b[4] << 8) + (int)b[5]) / 8 + 10;
    }

    if (dfl_t1 == 0) {
        fprintf(stderr, "Tuner1: wrong dfl size %d\n", len);
        return;
    }

    if (offset_t1 + len < dfl_t1) {
        memcpy(BBFrame_t1 + offset_t1, b, len);
        offset_t1 += len;
        return;
    }

    if (offset_t1 + len == dfl_t1) {
        memcpy(BBFrame_t1 + offset_t1, b, len);
        fprintf(stderr, "Tuner1: Complete bbframe # %d : %d/%d\n", count_t1, offset_t1 + len, dfl_t1);
        sendto(sockfd_ts1, BBFrame_t1, dfl_t1, 0,
               (const struct sockaddr *)&servaddr_ts1, sizeof(struct sockaddr));
        offset_t1 = 0;
        count_t1++;
        return;
    }

    if (offset_t1 + len > dfl_t1) {
        memcpy(BBFrame_t1 + offset_t1, b, dfl_t1 - offset_t1);
        sendto(sockfd_ts1, BBFrame_t1, dfl_t1, 0,
               (const struct sockaddr *)&servaddr_ts1, sizeof(struct sockaddr));
        fprintf(stderr, "Tuner1: First Complete bbframe # %d : %d/%d\n", count_t1, offset_t1 + dfl_t1 - offset_t1, dfl_t1);

        int size = len - (dfl_t1 - offset_t1);
        int oldoffset = offset_t1;
        int olddfl = dfl_t1;
        offset_t1 = 0;
        dfl_t1 = 0;

        fprintf(stderr, "Tuner1: Recursive with size %d\n", size);
        udp_bb_defrag_tuner1(b + olddfl - oldoffset, size, true);
    }
}

uint8_t udp_close_dual(void)
{
    /* -------------------------------------------------------------------------------------------------- */
    /* Close dual UDP sockets                                                                             */
    /*   return: error code                                                                              */
    /* -------------------------------------------------------------------------------------------------- */
    uint8_t err = ERROR_NONE;
    int ret;

    printf("Flow: UDP dual close\n");

    /* Close dedicated tuner sockets */
    if (dual_udp_initialized) {
        /* Close tuner 1 socket */
        ret = close(sockfd_ts1);
        if (ret != 0) {
            err = ERROR_UDP_CLOSE;
            printf("ERROR: Tuner1 UDP close\n");
        }

        /* Close tuner 2 socket */
        ret = close(sockfd_ts2);
        if (ret != 0) {
            err = ERROR_UDP_CLOSE;
            printf("ERROR: Tuner2 UDP close\n");
        }

        dual_udp_initialized = false;
    }

    if (err != ERROR_NONE) {
        printf("ERROR: UDP dual close\n");
    }

    return err;
}