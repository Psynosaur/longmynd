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


/* -------------------------------------------------------------------------------------------------- */
/* ----------------- GLOBALS ------------------------------------------------------------------------ */
/* -------------------------------------------------------------------------------------------------- */

struct sockaddr_in servaddr_status; 
struct sockaddr_in servaddr_ts;
int sockfd_status; 
int sockfd_ts;

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- DEFINES ------------------------------------------------------------------------ */
/* -------------------------------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- ROUTINES ----------------------------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------------------------------- */

void udp_send_normalize(u_int8_t *b, int len)
    {
#define BUFF_MAX_SIZE (7 * 188)
        static u_int8_t *Buffer=NULL;
        if(Buffer==NULL) Buffer=(u_int8_t*)malloc(BUFF_MAX_SIZE*2);
       
        static int Size = 0;
        //Align to Sync Packet
        static bool IsSync=false;
       
        //fprintf(stderr,"len %d Size %d\n",len,Size);
        
        if((IsSync==false)&&(len>=2*188))
        {
            int start_packet=0;
            for(start_packet=0;start_packet<188;start_packet++)
            {
                if ((b[start_packet]==0x47)&&(b[start_packet+188]==0x47))
                {
                    b=b+start_packet;
                    len=len-start_packet;
                    IsSync=true;
                    fprintf(stderr,"Recover Sync %d\n",start_packet);
                    break;        
                } 
            }
            fprintf(stderr,"Not Sync!\n");
            
        }
        
        if(Buffer[0]!=0x47) 
        {
            if(Size>=188) 
            {
                IsSync=false;
                Size=0;
                fprintf(stderr,"Lost Sync\n");
                return; 
            }    
        }
        
        
        if ((Size + len) >= BUFF_MAX_SIZE)
        {
                memcpy(Buffer + Size, b, len);
                
                //fprintf(stderr,"len %d Size %d %x\n",len,Size,Buffer[0]);
                if (sendto(sockfd_ts, Buffer, BUFF_MAX_SIZE, 0, (const struct sockaddr *) &servaddr_ts,  sizeof(struct sockaddr)) < 0)
                {
                    fprintf(stderr, "UDP send failed\n");
                }
                
                memmove(Buffer,Buffer+BUFF_MAX_SIZE,Size-BUFF_MAX_SIZE+len);
                Size+=len;
                Size = Size-BUFF_MAX_SIZE;
                 
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
void build_crc8_table( void )
{
    int r,crc;
    fprintf(stderr,"Init crc8\n");
    for( int i = 0; i < 256; i++ )
    {
        r = i;
        crc = 0;
        for( int j = 7; j >= 0; j-- )
        {
            if((r&(1<<j)?1:0) ^ ((crc&0x80)?1:0))
                crc = (crc<<1)^CRC_POLYR;
            else
                crc <<= 1;
        }
        m_crc_tab[i] = crc;
    }
}

uint8_t calc_crc8( uint8_t *b, int len )
{
    uint8_t crc = 0;

    for( int i = 0; i < len; i++ )
    {
        crc = m_crc_tab[b[i]^crc];
    }
    return crc;
}

#define BBFRAME_MAX_LEN 7274
void udp_bb_defrag(u_int8_t *b, int len,bool withheader)
{
    static unsigned char BBFrame[BBFRAME_MAX_LEN];
    static int offset=0;
    static int dfl=0;
    static int count=0;
    int end=false;
    int idx_b=0;

     if(offset+len>BBFRAME_MAX_LEN) 
    {
        fprintf(stderr,"bbframe overflow! %d/%d\n",offset,len);
        offset=0;
        return;
    }

    
    // if(((b[0]&0xC0)==0x70)&&(b[1]==0x0)) // FixeMe : SHould be better to compute crc to be sure it is a header
    if((offset==0)&&(len>=10)&&(calc_crc8(b,9)==b[9]))
    {
            //offset=0; //Start of bbframe header
            dfl=(((int)b[4]<<8) + (int)b[5])/8 + 10;
    }
    if(dfl==0) 
    {
        fprintf(stderr,"wrong dfl size %d\n",len);
        /*
        for(int i=0;i<len;i++)
        {
            fprintf(stderr,"%x ",b[i]);
        }
        fprintf(stderr,"\n");
        */
        return; 
    }    
    if(offset+len<dfl)
    {
        //fprintf(stderr,"bbframe # %d : %d/%d\n",count,offset+len,dfl);
        memcpy(BBFrame+offset,b,len);
        offset+=len;
        return;
    }     

    if(offset+len==dfl)
    {
         memcpy(BBFrame+offset,b,len);
         //fprintf(stderr,"Complete bbframe # %d : %d/%d\n",count,offset+len,dfl);
         sendto(sockfd_ts, BBFrame, dfl, 0, (const struct sockaddr *) &servaddr_ts,  sizeof(struct sockaddr));
         offset=0;
         count++;
         return;
           
    }
    
    if(offset+len>dfl)
    {
        //fprintf(stderr,"------------------------ Partial bbframe # %d : %d/%d\n",count,offset+len,dfl);
         memcpy(BBFrame+offset,b,dfl-offset);
         sendto(sockfd_ts, BBFrame, dfl, 0, (const struct sockaddr *) &servaddr_ts,  sizeof(struct sockaddr));
         fprintf(stderr,"First Complete bbframe # %d : %d/%d\n",count,offset+dfl-offset,dfl);

        int size=len - (dfl-offset);
        /*
          for(int i=0;i<len;i++)
        {
            if(i==dfl-offset) fprintf(stderr,"/////// \n");
            fprintf(stderr,"%x ",b[i]);
        }
        fprintf(stderr,"\n");
        */
         int oldoffset=offset;
         int olddfl=dfl;
         offset=0;
         dfl=0;
         /*
          fprintf(stderr,"Recursive with size %d\n",size);
           for(int i=0;i<size;i++)
        {
                    fprintf(stderr,"%x ",b[i+olddfl-oldoffset]);
        }
        fprintf(stderr,"\n");
        */
         udp_bb_defrag(b+olddfl-oldoffset,size,true);
         
           
    }
           
    

}


uint8_t udp_ts_write(uint8_t *buffer, uint32_t len, bool *output_ready) {
/* -------------------------------------------------------------------------------------------------- */
/* takes a buffer and writes out the contents to udp socket                                           */
/* *buffer: the buffer that contains the data to be sent                                              */
/*     len: the length (number of bytes) of data to be sent                                           */
/*  return: error code                                                                                */
/* -------------------------------------------------------------------------------------------------- */
    (void)output_ready;
    uint8_t err=ERROR_NONE;
    int32_t remaining_len; /* note it is signed so can go negative */
    uint32_t write_size;
   
    remaining_len=len;
    
    /* we need to loop round sending 510 byte chunks so that we can skip the 2 extra bytes put in by */
    /* the FTDI chip every 512 bytes of USB message */
    while (remaining_len>0) {
        if (remaining_len>510) {
             /* calculate where to start in the buffer and how many bytes to send */
             write_size=510;
              //fprintf(stderr,"1Usb len %d remainin %d\n",len,remaining_len);
             udp_send_normalize( &buffer[len-remaining_len],write_size);
            //sendto(sockfd_ts, &buffer[len-remaining_len], write_size, 0,(const struct sockaddr *) &servaddr_ts,  sizeof(struct sockaddr)); 
             /* note we skip over the 2 bytes inserted by the FTDI */
             remaining_len-=512;
        } else {
             write_size=remaining_len;
            ///fprintf(stderr,"2 Usb len %d remainin %d\n",len,remaining_len);            
             udp_send_normalize( &buffer[len-remaining_len],write_size);
            //sendto(sockfd_ts, &buffer[len-remaining_len], write_size, 0,(const struct sockaddr *) &servaddr_ts,  sizeof(struct sockaddr)); 
             remaining_len-=write_size; /* should be 0 if all went well */
        }
    }

    /* if someting went bad with our calcs, remaining will not be 0 */
    if ((err==ERROR_NONE) && (remaining_len!=0)) {
        printf("ERROR: UDP socket write incorrect number of bytes\n");
        err=ERROR_UDP_WRITE;
    }

    if (err!=ERROR_NONE) printf("ERROR: UDP socket ts write\n");

    return err;
}

uint8_t udp_bb_write(uint8_t *buffer, uint32_t len, bool *output_ready) {
/* -------------------------------------------------------------------------------------------------- */
/* takes a buffer and writes out the contents to udp socket                                           */
/* *buffer: the buffer that contains the data to be sent                                              */
/*     len: the length (number of bytes) of data to be sent                                           */
/*  return: error code                                                                                */
/* -------------------------------------------------------------------------------------------------- */
    (void)output_ready;
    uint8_t err=ERROR_NONE;
    int32_t remaining_len; /* note it is signed so can go negative */
    uint32_t write_size;
   
    remaining_len=len;
    
    /* we need to loop round sending 510 byte chunks so that we can skip the 2 extra bytes put in by */
    /* the FTDI chip every 512 bytes of USB message */
    //fprintf(stderr,"bbframe %d\n",len);
    while (remaining_len>0) {
        if (remaining_len>510) {
             /* calculate where to start in the buffer and how many bytes to send */
             write_size=510;
         
            udp_bb_defrag( &buffer[len-remaining_len],write_size,true);
            //sendto(sockfd_ts, &buffer[len-remaining_len], write_size, 0,(const struct sockaddr *) &servaddr_ts,  sizeof(struct sockaddr)); 
             remaining_len-=512;
        } else {
             write_size=remaining_len;
             udp_bb_defrag( &buffer[len-remaining_len],write_size,true);
             //sendto(sockfd_ts, &buffer[len-remaining_len], write_size, 0,(const struct sockaddr *) &servaddr_ts,  sizeof(struct sockaddr)); 
             remaining_len-=write_size; /* should be 0 if all went well */
        }
    }

    /* if someting went bad with our calcs, remaining will not be 0 */
    if ((err==ERROR_NONE) && (remaining_len!=0)) {
        printf("ERROR: UDP socket write incorrect number of bytes\n");
        err=ERROR_UDP_WRITE;
    }

    if (err!=ERROR_NONE) printf("ERROR: UDP socket ts write\n");

    return err;
}


/* -------------------------------------------------------------------------------------------------- */
uint8_t udp_status_write(uint8_t message, uint32_t data, bool *output_ready) {
/* -------------------------------------------------------------------------------------------------- */
/* takes a buffer and writes out the contents to udp socket                                           */
/* *buffer: the buffer that contains the data to be sent                                              */
/*     len: the length (number of bytes) of data to be sent                                           */
/*  return: error code                                                                                */
/* -------------------------------------------------------------------------------------------------- */
    (void)output_ready;
    uint8_t err=ERROR_NONE;
    char status_message[30];

    /* WARNING: This currently prints as signed integer (int32_t), even though function appears to expect unsigned (uint32_t) */
    sprintf(status_message, "$%i,%i\n", message, data);

    sendto(sockfd_status, status_message, strlen(status_message), 0, (const struct sockaddr *)&servaddr_status,  sizeof(struct sockaddr)); 

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t udp_status_string_write(uint8_t message, char *data, bool *output_ready) {
/* -------------------------------------------------------------------------------------------------- */
/* takes a buffer and writes out the contents to udp socket                                           */
/* *buffer: the buffer that contains the data to be sent                                              */
/*     len: the length (number of bytes) of data to be sent                                           */
/*  return: error code                                                                                */
/* -------------------------------------------------------------------------------------------------- */
    (void)output_ready;
    uint8_t err=ERROR_NONE;
    char status_message[5+128];

    sprintf(status_message, "$%i,%s\n", message, data);

    sendto(sockfd_status, status_message, strlen(status_message), 0, (const struct sockaddr *)&servaddr_status,  sizeof(struct sockaddr)); 

    return err;
}


/* -------------------------------------------------------------------------------------------------- */
static uint8_t udp_init(struct sockaddr_in *servaddr_ptr, int *sockfd_ptr, char *udp_ip, int udp_port) {
/* -------------------------------------------------------------------------------------------------- */
/* initialises the udp socket                                                                         */
/*    udp_ip: the ip address (as a string) of the socket to open                                      */
/*  udp_port: the UDP port to be opened at the given IP address                                       */
/*    return: error code                                                                              */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;
  
    printf("Flow: UDP Init\n");
    build_crc8_table();
    /* Creat the socket  for IPv4 and UDP */
    if ((*sockfd_ptr = socket(AF_INET, SOCK_DGRAM,IPPROTO_UDP)) < 0 ) { 
        printf("ERROR: socket creation failed\n"); 
        err=ERROR_UDP_SOCKET_OPEN; 
    } else {
        /* setup all the destination fields */
        memset(servaddr_ptr, 0, sizeof(struct sockaddr_in)); 
        servaddr_ptr->sin_family = AF_INET; 
        servaddr_ptr->sin_port = htons(udp_port); 
        servaddr_ptr->sin_addr.s_addr = inet_addr(udp_ip); // INADDR_ANY; 
    }
    if (err!=ERROR_NONE) printf("ERROR: UDP init\n");

    return err;
}

uint8_t udp_status_init(char *udp_ip, int udp_port) {
    return udp_init(&servaddr_status, &sockfd_status, udp_ip, udp_port);
}

uint8_t udp_ts_init(char *udp_ip, int udp_port) {
    return udp_init(&servaddr_ts, &sockfd_ts, udp_ip, udp_port);
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t udp_close(void) {
/* -------------------------------------------------------------------------------------------------- */
/* closes the udp socket                                                                              */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;
    int ret;

    printf("Flow: UDP Close\n");

    ret=close(sockfd_ts); 
    if (ret!=0) {
        err=ERROR_UDP_CLOSE;
        printf("ERROR: TS UDP close\n");
    }
    ret=close(sockfd_status); 
    if (ret!=0) {
        err=ERROR_UDP_CLOSE;
        printf("ERROR: Status UDP close\n");
    }

    return err;
}

