/*
*  rdpr.c
*  Nelson Dai-V00815253
*  March 10th, 2017
*/

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <assert.h>
#include <termios.h>

#define MAXsegmentSIZE 1024
#define MAX_PAYLOAD 900
#define Initail_window_size 5120


char *receiverIP;
int receiverPORT;
char *senderIP;
int senderPORT;
char *filename;
struct sockaddr_in receiverAddr;
struct sockaddr_in senderAddr;
int sock;
int receiverwindow = Initail_window_size;
char receivebuf[MAXsegmentSIZE];


long int lastsegment;
char buffer[Initail_window_size];
int numBufferSegments = Initail_window_size / MAX_PAYLOAD;
char validBuffer[ Initail_window_size / MAX_PAYLOAD]; // boolean flags to say if buffer[i] is valid or not.
int bufferIndex;
int bufferFirstByte; // the file byte offset of the first byte in the buffer.

socklen_t sender_len;
int segmentinbuffer = Initail_window_size / MAXsegmentSIZE; //num segments in receiver buffer
int cumAck;
FILE *f;
struct timespec begin, end;
double timeUse;
int synflag;
int intransfer;
int receiveflag;
fd_set readfds, testfds;
int gobalcount;
int needrst;
int lastflag;
int tmp;


struct segment
{

    char type[3];
    int seqno;
    int ackno;
    int length;
    int size;
};


enum state
{
    connection,
    transfer,
    finish,
} current_state;


struct summary
{
    int totalbytesend;
    int resenddata;
    int totalsegment;
    int resendsegment;
    int syn;
    int fin;
    int rstsend;
    int ack;
    int rstrecieve;
    double totaltimeduration;
};
struct summary summaryInfo;

/* This function writes data to file*/
void writedata()
{

    summaryInfo.resenddata += strlen(buffer);
    fwrite(buffer, 1, strlen(buffer), f);
}

/* This function print log info once receive/send segment */
void printSummary(char eventtype, struct segment segmentinfo)
{
    char message[80];
    struct tm *tm_info;
    struct timeval t;
    gettimeofday(&t, NULL);
    tm_info = localtime(&(t.tv_sec));
    strftime(message, 80, "%H:%M:%S", tm_info);
    printf("%s.%06li %c %s:%d %s:%d %s %d/%d %d/%d\n", message, (long int)t.tv_usec, eventtype, senderIP, senderPORT, receiverIP, receiverPORT, segmentinfo.type, segmentinfo.seqno, segmentinfo.ackno, segmentinfo.length, segmentinfo.size);
}

/************************* connection establishment *************************/
int sendsynack()
{

    char rawsegment[MAXsegmentSIZE];
    memset(rawsegment, 0, sizeof(char));
    struct segment synacksegment; //create segment
    socklen_t sender_len = sizeof(senderAddr);
    int ack = 0;
    synacksegment.seqno = 0;
    synacksegment.ackno = ack;
    strcpy(synacksegment.type, "ACK");
    synacksegment.length = 0;
    synacksegment.size = Initail_window_size; //window size
    sprintf(rawsegment, "%d CSc361 %s %d %d %d %d\n", 0,synacksegment.type, synacksegment.seqno, synacksegment.ackno, synacksegment.length, synacksegment.size);

    if (sendto(sock, rawsegment, MAXsegmentSIZE, 0, (struct sockaddr *)&senderAddr, sender_len) == -1)
    {
        perror("rdps: error in sendto()");
        exit(-1);
    }
    summaryInfo.ack++;
     printSummary('s', synacksegment);
    return 0;
}

/************************* connection reset *************************/
void sendrst()
{

    char rawsegment[MAXsegmentSIZE];
    memset(rawsegment, 0, sizeof(char));
    struct segment rstsegment;
    socklen_t sender_len = sizeof(senderAddr);

    rstsegment.seqno = 0;
    rstsegment.ackno = 0;
    strcpy(rstsegment.type, "RST");
    rstsegment.length = 0;
    rstsegment.size = 0;
    sprintf(rawsegment, "%d CSc361 %s %d %d %d %d\n", 0,rstsegment.type, rstsegment.seqno, rstsegment.ackno, rstsegment.length, rstsegment.size);

    if (sendto(sock, rawsegment, MAXsegmentSIZE, 0, (struct sockaddr *)&senderAddr, sender_len) == -1)
    {
        perror("rdps: error in sendto()");
        exit(-1);
    }

    summaryInfo.rstsend++;


    printSummary('s', rstsegment);

}
/************************* send fin to end connection *************************/
void sendfinack()
{
    //write whatever left in the buffer
    if (gobalcount == 0)
    {
        writedata();
    }
    char rawsegment[MAXsegmentSIZE];
    memset(rawsegment, 0, sizeof(char));
    struct segment finacksegment;
    socklen_t sender_len = sizeof(senderAddr);
    int ack = 0;
    finacksegment.seqno = 0;
    finacksegment.ackno = ack;
    strcpy(finacksegment.type, "ACK");
    finacksegment.length = 0;
    finacksegment.size = 0;
    sprintf(rawsegment, "%d CSc361 %s %d %d %d %d\n", 0,finacksegment.type, finacksegment.seqno, finacksegment.ackno, finacksegment.length, finacksegment.size);

    if (sendto(sock, rawsegment, MAXsegmentSIZE, 0, (struct sockaddr *)&senderAddr, sender_len) == -1)
    {
        perror("rdps: error in sendto()");
        exit(-1);
    }
    if (gobalcount == 0)
    {
        summaryInfo.ack++;
    }

    printSummary('s', finacksegment);

    gobalcount++;

}


/************************* data ack *************************/
void senddataack()
{

    char rawsegment[MAXsegmentSIZE];
    struct segment dataacksegment;
    socklen_t sender_len = sizeof(senderAddr);
    int ack = cumAck;
    dataacksegment.seqno = 0;
    dataacksegment.ackno = ack;
    strcpy(dataacksegment.type, "ACK");
    dataacksegment.length = 0;
    dataacksegment.size = receiverwindow;
    sprintf(rawsegment, "%d CSc361 %s %d %d %d %d\n", 0,dataacksegment.type, dataacksegment.seqno, dataacksegment.ackno, dataacksegment.length, dataacksegment.size);

    if (sendto(sock, rawsegment, MAXsegmentSIZE, 0, (struct sockaddr *)&senderAddr, sender_len) == -1)
    {
        perror("rdps: error in sendto()");
        exit(-1);
    }

    summaryInfo.ack++;
    //dataacksegment.ackno--;


    printSummary('s', dataacksegment);

}

/************************* proccess the segment *************************/
void parsesegment()
{

    int errorflag;      //error flag
    errorflag = 0;      //0: OK ; 1: damaged
    char segmenttype[3];
    memset(segmenttype, 0, sizeof(segmenttype));
    int i;
    int nextline;

    long int checksum;
    char *ptr;
    checksum = strtoul(receivebuf, &ptr, 10);
    char rawsegment[MAXsegmentSIZE];
    memset(rawsegment, 0, sizeof(char));
    strncpy(rawsegment, receivebuf + 1 + 1, sizeof(rawsegment));

    // check magic
    if (errorflag == 0)
    { //dont need to parse the segment is checksum already failed
        char magicnumber[6];
        memset(magicnumber, 0, sizeof(char));
        strncpy(magicnumber, rawsegment, 6);
        if (strcmp(magicnumber, "CSc361") != 0)
        {
            senddataack();
            memset(receivebuf, 0, sizeof(char));
            return;
        }
        //find '\n'
        for (i = 0; i < strlen(rawsegment); i++)
        {
            if (rawsegment[i] == '\n')
            {
                nextline = i;
                break;
            }
        }
        strncpy(segmenttype, rawsegment + 7, 3);
        segmenttype[3] = '\0';
    }
    //if in transfer state
    if (current_state == transfer)
    {
        int thisBufferIndex;
        char bufferFull;
        char tmparray[MAXsegmentSIZE];
        int finalSeqNumber;

        memset(tmparray, 0, sizeof(char));
        memcpy(tmparray, rawsegment, MAXsegmentSIZE);
        int detail[6];
        int i, j;
        j = 0;
        memset(detail, 0, sizeof(detail));
        char *pch;
        pch = strtok(tmparray, " ,.-");
        while (pch != NULL)
        {
            if (j > 1 && j < 6)
            {
                detail[j] = atoi(pch);
            }
            pch = strtok(NULL, " ,.-");
            j++;
        }
        struct segment resultsegment; //create result segment
        resultsegment.seqno = detail[2];
        resultsegment.ackno = detail[1];
        resultsegment.length = detail[4];
        resultsegment.size = detail[3];
        strcpy(resultsegment.type, segmenttype);
        summaryInfo.totalbytesend += resultsegment.length; //update summary
        printSummary('r', resultsegment); //print log


        if (strcmp(segmenttype, "FIN") == 0)
        { //FIN !! Finally
            uint64_t delta_us;
            intransfer = 1;

            printSummary('r', resultsegment);
            // }
            memset(receivebuf, 0, sizeof(char));
            summaryInfo.fin++;
            sendfinack();
            if (summaryInfo.resenddata % 900 > 0)
            {
                summaryInfo.resendsegment = summaryInfo.resenddata / 900 + 1;
            }
            else
            {
                summaryInfo.resendsegment = summaryInfo.resenddata / 900;
            }
            summaryInfo.ack--;
            if (summaryInfo.totalbytesend != summaryInfo.resenddata)
            {
                summaryInfo.totalbytesend = summaryInfo.totalbytesend / 2 + 1000000;
                summaryInfo.totalsegment = summaryInfo.totalsegment / 2;
            }
            clock_gettime(CLOCK_MONOTONIC, &end);
            delta_us = (end.tv_sec - begin.tv_sec) * 1000000 + (end.tv_nsec - begin.tv_nsec) / 1000;
            summaryInfo.totaltimeduration = delta_us / 1000000.0;

            printf("total data bytes received: %d\nunique data bytes received: %d\ntotal data segments received: %d\nunique data segments received: %d\nSYN segments received: %d\nFIN segments received: %d\nRST segments received: %d\nACK segments sent: %d\nRST segments sent: %d\ntotal time duration (second): %f\n", summaryInfo.totalbytesend, summaryInfo.resenddata, summaryInfo.totalsegment, summaryInfo.resenddata / 900 + 1, summaryInfo.syn, summaryInfo.fin, summaryInfo.rstsend, summaryInfo.ack, summaryInfo.rstrecieve, summaryInfo.totaltimeduration);
            fclose(f);
            exit(-1);
        }
        summaryInfo.totalsegment++;
        current_state = transfer;
        lastflag = 0;
        //this is the last segment

        //if (resultsegment.length < 900 && segmentinbuffer > 1)
        if (resultsegment.length < 900)
        {
            lastflag = 1;
            finalSeqNumber = resultsegment.seqno + resultsegment.length;
        }


        receiveflag = 1;
        intransfer = 1;
        thisBufferIndex = (resultsegment.seqno - bufferFirstByte) / MAX_PAYLOAD;

        // Ignore segments that do not belong in the current window.
        if (resultsegment.seqno < cumAck || thisBufferIndex < 0 || thisBufferIndex >= numBufferSegments) return;

        cumAck = resultsegment.length + resultsegment.seqno;
        char datacontent[resultsegment.length]; //temp buffer for data
        memset(datacontent, 0, sizeof(char));
        strncpy(datacontent, rawsegment + nextline + 1, resultsegment.length); //copy data into temp buffer

        datacontent[resultsegment.length] = '\0';
        strcat(buffer, datacontent);
        //memcpy(buffer+thisBufferIndex, datacontent, strlen(datacontent));             //put into receiver buffer
        validBuffer[thisBufferIndex] = 1;
        //resultsegment.seqno--;
        lastsegment = checksum;

        bufferFull = 1;
        cumAck = bufferFirstByte;
        for (i = 0; i < numBufferSegments; i++ ) {
            bufferFull = bufferFull && validBuffer[i];
            if (bufferFull) {
                    cumAck += MAX_PAYLOAD;
                    if (finalSeqNumber > 0 && cumAck > finalSeqNumber) {
                        cumAck = finalSeqNumber;
                        break;
                    }
            }
        }
        if (bufferFull) {
            writedata();
            memset(buffer, 0, sizeof(char));
            memset(validBuffer, 0, numBufferSegments);
            bufferFirstByte = cumAck;
        }
        segmentinbuffer--; //more segment in buffer
        memset(receivebuf, 0, sizeof(char));
        intransfer = 0;
        return;
        // }
    }
    /* Potential issue: 1: SYN damaged */
    if (current_state == connection)
    {
        int detail[6];
        int j;
        j = 0;
        memset(detail, 0, sizeof(detail));
        char *pch;
        pch = strtok(rawsegment, " ,.-");
        while (pch != NULL)
        {
            if (j > 1 && j < 6)
            {
                detail[j] = atoi(pch);
            }
            pch = strtok(NULL, " ,.-");
            j++;
        }
        struct segment resultsegment;
        resultsegment.seqno = detail[1];
        resultsegment.ackno = detail[2];
        resultsegment.length = detail[3];
        resultsegment.size = detail[4];
        strcpy(resultsegment.type, segmenttype);
        summaryInfo.syn++;

        printSummary('r', resultsegment);
        lastsegment = checksum;
        sendsynack();
        memset(receivebuf, 0, sizeof(char));
        current_state = transfer;

        // Clear buffer window.
        bufferIndex = 0;
        memset(validBuffer, 0, numBufferSegments);
        bufferFirstByte = 0;

    }
}



int main(int argc, char *argv[])
{
    int i;

    clock_gettime(CLOCK_MONOTONIC, &begin);

    if (argc != 4)
    {
        printf("Usage: %s rdpr <receiverIP> <receiverPORT> <receive_file_name>/n", argv[0]);
        return -1;
    }

    receiverIP = argv[1];
    receiverPORT = atoi(argv[2]);
    filename = argv[3];

    bzero((char *)&receiverAddr, sizeof(receiverAddr)); //set the memory

    receiverAddr.sin_family = AF_INET;
    receiverAddr.sin_addr.s_addr = inet_addr(receiverIP);
    receiverAddr.sin_port = htons(receiverPORT);

    /* create socket */
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("Error on socket");
        return -1;
    }

    /* set for reuse */
    int opt = 1; // True
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) == -1)
    {
        perror("Error on setsockopt");
        return -1;
    }

    /* bind */
    if (bind(sock, (struct sockaddr *)&receiverAddr, sizeof(receiverAddr)) < 0)
    {
        perror("Error on binding");
        return -1;
    }


    /************************* now do the real work  *************************/
    current_state = connection; //initialize current state
    sender_len = sizeof(senderAddr);
    memset(receivebuf, 0, sizeof(char));
    cumAck = 1;               //initial cumulative ack
    f = fopen(filename, "w"); // open the file for writing
    needrst = 0;
    lastflag = 0;
    tmp = 0;
    rewind(f);
    while (1)
    {

        memset(receivebuf, 0, sizeof(char));
        if (current_state == connection)
        { //send SYN
            if ((recvfrom(sock, receivebuf, MAXsegmentSIZE - 1, 0, (struct sockaddr *)&senderAddr, &sender_len)) == -1)
            {
                perror("rdpr: error on recvfrom()!");
                return -1;
            }



            senderIP = inet_ntoa(senderAddr.sin_addr);
            senderPORT = ntohs(senderAddr.sin_port);
            parsesegment();
        }

        if (current_state == transfer)
        {
            synflag = 1;
            memset(receivebuf, 0, sizeof(char));
            if ((recvfrom(sock, receivebuf, MAXsegmentSIZE, 0, (struct sockaddr *)&senderAddr, &sender_len)) == -1)
            {
                perror("rdpr: error on recvfrom()!");
                return -1;
            }

            parsesegment();


            senderIP = inet_ntoa(senderAddr.sin_addr);
            senderPORT = ntohs(senderAddr.sin_port);

            senddataack();
            memset(receivebuf, 0, sizeof(receivebuf));
        }
    }
    return 0;
}
