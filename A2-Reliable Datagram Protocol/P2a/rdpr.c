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
socklen_t sender_len;
int segmentinbuffer = Initail_window_size / MAXsegmentSIZE; //num segments in receiver buffer
int cumAck;
FILE *f;
clock_t begin, end;
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

    char type[4];
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



//Reference :http://www.cse.yorku.ca/~oz/hash.html  Hash Function
unsigned long hash(char *str)
{
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
    {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash;
}


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

    char sendsegment[MAXsegmentSIZE]; //after checksum
    char rawsegment[MAXsegmentSIZE];  //before checksum
    memset(rawsegment, 0, sizeof(char));
    memset(sendsegment, 0, sizeof(char));
    struct segment synacksegment; //create segment
    socklen_t sender_len = sizeof(senderAddr);
    int ack = 1;
    synacksegment.seqno = 0;
    synacksegment.ackno = ack;
    strcpy(synacksegment.type, "ACK");
    synacksegment.length = 0;
    synacksegment.size = Initail_window_size; //window size
    sprintf(rawsegment, "CSc361 %s %d %d %d %d\n", synacksegment.type, synacksegment.seqno, synacksegment.ackno, synacksegment.length, synacksegment.size);
    unsigned long checksum = hash(rawsegment); //checksum
    sprintf(sendsegment, "%lu %s", checksum, rawsegment);

    if (sendto(sock, sendsegment, MAXsegmentSIZE, 0, (struct sockaddr *)&senderAddr, sender_len) == -1)
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

    char sendsegment[MAXsegmentSIZE];
    char rawsegment[MAXsegmentSIZE];
    memset(rawsegment, 0, sizeof(char));
    memset(sendsegment, 0, sizeof(char));
    struct segment rstsegment;
    socklen_t sender_len = sizeof(senderAddr);

    rstsegment.seqno = 0;
    rstsegment.ackno = 0;
    strcpy(rstsegment.type, "RST");
    rstsegment.length = 0;
    rstsegment.size = 0;
    sprintf(rawsegment, "CSc361 %s %d %d %d %d\n", rstsegment.type, rstsegment.seqno, rstsegment.ackno, rstsegment.length, rstsegment.size);
    unsigned long checksum = hash(rawsegment);
    sprintf(sendsegment, "%lu %s", checksum, rawsegment);

    if (sendto(sock, sendsegment, MAXsegmentSIZE, 0, (struct sockaddr *)&senderAddr, sender_len) == -1)
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
    char sendsegment[MAXsegmentSIZE];
    char rawsegment[MAXsegmentSIZE];
    memset(rawsegment, 0, sizeof(char));
    memset(sendsegment, 0, sizeof(char));
    struct segment finacksegment;
    socklen_t sender_len = sizeof(senderAddr);
    int ack = 0;
    finacksegment.seqno = 0;
    finacksegment.ackno = ack;
    strcpy(finacksegment.type, "ACK");
    finacksegment.length = 0;
    finacksegment.size = 0;
    sprintf(rawsegment, "CSc361 %s %d %d %d %d\n", finacksegment.type, finacksegment.seqno, finacksegment.ackno, finacksegment.length, finacksegment.size);
    unsigned long checksum = hash(rawsegment);
    sprintf(sendsegment, "%lu %s", checksum, rawsegment);

    if (sendto(sock, sendsegment, MAXsegmentSIZE, 0, (struct sockaddr *)&senderAddr, sender_len) == -1)
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

    char sendsegment[MAXsegmentSIZE];
    char rawsegment[MAXsegmentSIZE];
    struct segment dataacksegment;
    socklen_t sender_len = sizeof(senderAddr);
    int ack = cumAck;
    dataacksegment.seqno = 0;
    dataacksegment.ackno = ack;
    strcpy(dataacksegment.type, "ACK");
    dataacksegment.length = 0;
    dataacksegment.size = receiverwindow;
    sprintf(rawsegment, "CSc361 %s %d %d %d %d\n", dataacksegment.type, dataacksegment.seqno, dataacksegment.ackno, dataacksegment.length, dataacksegment.size);
    unsigned long checksum = hash(rawsegment);
    sprintf(sendsegment, "%lu %s", checksum, rawsegment);

    if (sendto(sock, sendsegment, MAXsegmentSIZE, 0, (struct sockaddr *)&senderAddr, sender_len) == -1)
    {
        perror("rdps: error in sendto()");
        exit(-1);
    }



    summaryInfo.ack++;
    dataacksegment.ackno--;


    printSummary('s', dataacksegment);

}

/************************* proccess the segment *************************/
void parsesegment()
{
    //get the checksum first
    int errorflag;      //error flag
    errorflag = 0;      //0: OK ; 1: damaged
    char segmenttype[4]; //TODO mac or linux !!!
    memset(segmenttype, 0, sizeof(segmenttype));
    int space;
    int i;
    int nextline;
    //get the position of the end of checksum
    for (i = 0; i < strlen(receivebuf); i++)
    {
        if (receivebuf[i] == ' ')
        {
            space = i;
            break;
        }
    }
    long int checksum;
    char *ptr;
    checksum = strtoul(receivebuf, &ptr, 10);
    char rawsegment[MAXsegmentSIZE];
    memset(rawsegment, 0, sizeof(char));
    strncpy(rawsegment, receivebuf + space + 1, sizeof(rawsegment));
    if (hash(rawsegment) != checksum)
    { //checksum fails
        senddataack();
        memset(receivebuf, 0, sizeof(char));
        errorflag = 1; //set error flag
        return;        //dont send ack. let sender timeout
    }
    // check magic
    if (errorflag == 0)
    { //dont need to parse the segment is checksum already failed
        char magicnumber[7];
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
        char tmparray[MAXsegmentSIZE];
        memset(tmparray, 0, sizeof(char));
        memcpy(tmparray, rawsegment, MAXsegmentSIZE);
        int detail[6];
        int j;
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


        if (strcmp(segmenttype, "FIN") == 0)
        { //FIN !! Finally
            intransfer = 1;

            printSummary('r', resultsegment);
            // }
            memset(receivebuf, 0, sizeof(char));
            summaryInfo.fin++;
            sendfinack();
            end = clock();
            timeUse = (double)(end - begin) / CLOCKS_PER_SEC;
            summaryInfo.totaltimeduration = timeUse;
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
            printf("total data bytes received: %d\nunique data bytes received: %d\ntotal data segments received: %d\nunique data segments received: %d\nSYN segments received: %d\nFIN segments received: %d\nRST segments received: %d\nACK segments sent: %d\nRST segments sent: %d\ntotal time duration (second): %f\n", summaryInfo.totalbytesend, summaryInfo.resenddata, summaryInfo.totalsegment, summaryInfo.resenddata / 900 + 1, summaryInfo.syn, summaryInfo.fin, summaryInfo.rstsend, summaryInfo.ack, summaryInfo.rstrecieve, summaryInfo.totaltimeduration);
            fclose(f);
            exit(-1);
        }
        summaryInfo.totalsegment++;
        current_state = transfer;
        lastflag = 0;
        //this is the last segment
        if (resultsegment.length < 900 && segmentinbuffer > 1)
        {
            lastflag = 1;
        }


        receiveflag = 1;
        intransfer = 1;
        cumAck = resultsegment.length + resultsegment.seqno;
        char datacontent[resultsegment.length]; //temp buffer for data
        memset(datacontent, 0, sizeof(char));
        strncpy(datacontent, rawsegment + nextline + 1, resultsegment.length); //copy data into temp buffer

        datacontent[resultsegment.length] = '\0'; //TODO mac or linux?
        strcat(buffer, datacontent);             //put into receiver buffer
        resultsegment.seqno--;
        if (tmp == 0)
        {
            printSummary('r', resultsegment); //print log
        }
        lastsegment = checksum;
        writedata();
        segmentinbuffer--; //more segment in buffer
        memset(buffer, 0, sizeof(char));
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

    }
}



int main(int argc, char *argv[])
{
    begin = clock();

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

            if (segmentinbuffer == 0 || lastflag == 1)
            {

                //writedata();
                if (receiverwindow / MAXsegmentSIZE == 6)
                {
                    receiverwindow = Initail_window_size;
                }
                senddataack();
                memset(buffer, 0, sizeof(char));
                memset(receivebuf, 0, sizeof(char));
                segmentinbuffer = 5;
            }
        }
    }
    return 0;
}
