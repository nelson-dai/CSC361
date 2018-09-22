/*
*  rdps.c
*  Nelosn Dai-V00815253
*  March 22nd, 2017
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

#define MAXSEGMENT 1024
#define MAXPAYLOAD 900


int sock;
struct sockaddr_in receiverAddr;
struct sockaddr_in senderAddr;
char *senderIP;
int senderPORT;
char *receiverIP;
int receiverPORT;
int receiverwindow;
int cumAck;
int windowNextSeq;
char receivebuf[MAXSEGMENT];
char sendbuf[MAXSEGMENT];
long int lastsegment;
char *filename;
long fileSIZE;
FILE *f;
socklen_t receiver_len;
struct timespec begin, end;
double timeUSE;
int synflag;
int reflag;
int finflag;
int intransfer;
int receiveflag;
fd_set readfds, testfds;
int last;
int needrst;
int needfin;
int numFin;
int tmp;
char fileTransferComplete = 0;
char finSeq = 0;

char dropped;

//header
struct segment
{
    char type[3];
    int seqno;
    int ackno;
    int length;
    int size;
    char data[MAXPAYLOAD];
};

//state during transimission 
enum state
{
    connection,
    transfer,
    finish,
} current_state;

//the log
struct summary
{
    int totalSIZE;
    int totalsegment;
    int uniqueSIZE;
    int uniqueSegment;
    int syn;
    int fin;
    int rstsend;
    int ack;
    int rstrecieve;
    double totaltimeduration;
};
struct summary summaryInfo;

/*function for printing the summary*/
void printSummary (char flag, struct segment segmentinfo)
{
    char message[80];
    time_t timer;
    struct tm *tm_info;
    struct timeval t;
    gettimeofday(&t, NULL); //get current time
    tm_info = localtime(&(t.tv_sec));
    strftime(message, 80, "%H:%M:%S", tm_info);
    //print log info
    printf("%s.%06li %c %s:%d %s:%d %s %d/%d %d/%d\n", message, (long int)t.tv_usec, flag, senderIP, senderPORT, receiverIP, receiverPORT, segmentinfo.type, segmentinfo.seqno, segmentinfo.ackno, segmentinfo.length, segmentinfo.size);
}

void sendfin(int);
void finishconnection();

/************************* connection reset *************************/
void sendrst()
{

    char rawsegment[MAXSEGMENT];
    socklen_t receiver_len = sizeof(receiverAddr);

    memset(rawsegment, 0, sizeof(char));
    struct segment rstsegment;

    rstsegment.seqno = 0;
    rstsegment.ackno = 0;
    strcpy(rstsegment.type, "RST");
    rstsegment.length = 0;
    rstsegment.size = 0;
    sprintf(rawsegment, "%d CSc361 %s %d %d %d %d\n", 0,rstsegment.type, rstsegment.seqno, rstsegment.ackno, rstsegment.length, rstsegment.size);

    if (sendto(sock, rawsegment, MAXSEGMENT, 0, (struct sockaddr *)&receiverAddr, receiver_len) == -1)
    {
        perror("rdps: error in sendto()");
        exit(-1);
    }

    summaryInfo.rstsend++;


    printSummary('s', rstsegment);

}


/************************* connection establishment *************************/
int connecting()
{
    char newSegment[MAXSEGMENT];  //new segment
    memset(newSegment, 0, sizeof(char));
    struct segment synsegment; //make a new segment
    socklen_t receiver_len = sizeof(receiverAddr);
    memset(synsegment.type, 0, sizeof(synsegment.type));

    int seq = rand();

    synsegment.seqno = seq;
    strcpy(synsegment.type, "SYN");
    synsegment.ackno = 0;
    synsegment.length = 0;
    synsegment.size = 0;
    strcpy(synsegment.data, "");
    sprintf(newSegment, "%d CSc361 %s %d %d %d %d\n%s", 0,synsegment.type, synsegment.seqno, synsegment.ackno, synsegment.length, synsegment.size, synsegment.data);

    /* no payload in SYN */
    if (sendto(sock, newSegment, MAXSEGMENT, 0, (struct sockaddr *)&receiverAddr, receiver_len) == -1)
    {
        perror("rdps: error in sendto()");
        exit(-1);
    }
    summaryInfo.syn++; //SYN segments sent

    printSummary ('s', synsegment);
    return 0;
}



/************************* proccess the segment *************************/
void parsesegment()
{
    /* Connection stage */
    char segmenttype[3];
    memset(segmenttype, 0, sizeof(segmenttype));
    int space;
    int i;
    int nextline;
    //find the position of the end of checksum
    long int checksum;
    char *ptr;
    checksum = strtoul(receivebuf, &ptr, 10);
    char newSegment[MAXSEGMENT];
    memset(newSegment, 0, sizeof(char));
    strncpy(newSegment, receivebuf + 1 + 1, sizeof(newSegment));
    //check magic
    char magicnumber[6];
    memset(magicnumber, 0, sizeof(char));
    strncpy(magicnumber, newSegment, 6);
    magicnumber[6] = '\0';

    if (strcmp(magicnumber, "CSc361") != 0)
    {
        memset(receivebuf, 0, sizeof(char));
        return;
    }
    char tmparray[MAXSEGMENT];
    memset(tmparray, 0, sizeof(char));
    memcpy(tmparray, newSegment, MAXSEGMENT);
    int detail[6];
    int j;
    j = 0;
    memset(detail, 0, sizeof(detail));
    char *pch;
    pch = strtok(tmparray, " ,.-");
    //get rid of space in the segment
    while (pch != NULL)
    {
        if (j > 1 && j < 6)
        {
            detail[j] = atoi(pch);
        }
        pch = strtok(NULL, " ,.-");
        j++;
    }
    strncpy(segmenttype, newSegment + 7, 3);
    segmenttype[3] = '\0';

    //if no error. Create a realSegment
    struct segment realSegment;
    realSegment.seqno = detail[2];
    realSegment.ackno = detail[3];
    realSegment.length = detail[4];
    realSegment.size = detail[5];
    strcpy(realSegment.type, "ACK");

    //update ack. This tells sender the next byte to expect
    if (realSegment.ackno > cumAck) cumAck = realSegment.ackno;
    receiverwindow = realSegment.size;
    summaryInfo.ack++;

    //Now at the last segment because data length is less than max payload length
    if (realSegment.seqno == 0 && realSegment.ackno == 0 && realSegment.length == 0 && realSegment.size == 0)
    {
        finflag = 1;
    }

    //send fin
    if (realSegment.ackno == fileSIZE && numFin == 0)
    {
        current_state = finish;
        numFin++;
        needfin = 1;
        printSummary ('r', realSegment);
        sendfin((int)fileSIZE);
        return;
    }

    // end the connection if we recive the fin ack
    if (realSegment.seqno == 0 && realSegment.ackno == 0 && realSegment.length == 0 && realSegment.size == 0 && current_state == finish)
    {
        printSummary ('r', realSegment);
        finishconnection();
    }

    receiveflag = 1;
    reflag = 0;
    if (tmp == 0)
    {
        printSummary ('r', realSegment);
    }
    lastsegment = checksum;
    memset(receivebuf, 0, sizeof(char));
    current_state = transfer;

}



/************************* transfer data *************************/
void transferdata()
{
    memset(receivebuf, 0, sizeof(char));
    int i;
    i = 0;
    int seqnum;
    seqnum = cumAck; //sequence number in the first segment

    for (i = 0; i < receiverwindow / MAXSEGMENT; i++)
    {
        //how many segment we can send
        memset(receivebuf, 0, sizeof(char));
        intransfer = 1;
        rewind(f);

        char newSegment[MAXSEGMENT];
        memset(newSegment, 0, sizeof(char));
        struct segment datasegment; //make a new data segment
        memset(datasegment.data, 0, sizeof(char));
        datasegment.seqno = seqnum;  //put sequence number
        strcpy(datasegment.type, "DAT"); //type is data
        datasegment.ackno = 0;
        if ((seqnum + MAXPAYLOAD) > fileSIZE)
        {
            //the last segment payload<900
            datasegment.length = fileSIZE - seqnum;
            tmp++;
        }
        else
        {
            datasegment.length = MAXPAYLOAD;
        }
        datasegment.size = 0;
        //read the input file

        fseek(f, seqnum, SEEK_SET);
        fread(datasegment.data, sizeof(char), datasegment.length, f);

        datasegment.data[datasegment.length] = '\0'; //put the data content into a temp buffer

        sprintf(newSegment, "%d CSc361 %s %d %d %d %d\n%s", 0,datasegment.type, datasegment.seqno, datasegment.ackno, datasegment.length, datasegment.size, datasegment.data);

        //checksum
        summaryInfo.totalSIZE += datasegment.length;
        summaryInfo.totalsegment++;
        summaryInfo.uniqueSIZE += datasegment.length;
        summaryInfo.uniqueSegment++;

        memset(receivebuf, 0, sizeof(char));
        //send the segment
        if (sendto(sock, newSegment, MAXSEGMENT, 0, (struct sockaddr *)&receiverAddr, receiver_len) == -1)
        {
            perror("rdps: error in sendto()");
            exit(-1);
        }

        if (tmp == 0)
        {
            printSummary ('s', datasegment);
        }

        current_state = transfer;   //change state
        seqnum += datasegment.length; //update sequence number


        if (seqnum == fileSIZE)
        {
            //change state to finish
            intransfer = 0;
            fileTransferComplete = 1;
            finSeq = seqnum;
            //current_state = finish;
            memset(newSegment, 0, sizeof(char));
            memset(datasegment.data, 0, sizeof(char));
            windowNextSeq = seqnum;
            return;
        }

        memset(receivebuf, 0, sizeof(char));
        memset(newSegment, 0, sizeof(char));
        memset(datasegment.data, 0, sizeof(char));
    }
    intransfer = 0;
    reflag = 0;
    windowNextSeq = seqnum;
}

// Send the segments from the buffer starting at the first one past
// cumAck.
void resendData()
{
    int currentSeq = cumAck;
    while (currentSeq < windowNextSeq)
    {
        // Resend the segment.
        char newSegment[MAXSEGMENT];
        memset(newSegment, 0, sizeof(char));
        struct segment datasegment; //make a new data segment
        memset(datasegment.data, 0, sizeof(char));
        datasegment.seqno = currentSeq;  //put sequence number
        if (currentSeq == 1) {
            currentSeq = 0;
            datasegment.seqno = 0;
        }
        strcpy(datasegment.type, "DAT"); //type is data
        datasegment.ackno = 0;
        if ((currentSeq + MAXPAYLOAD) > fileSIZE)
        {
            //the last segment payload<900
            datasegment.length = fileSIZE - currentSeq;
        }
        else
        {
            datasegment.length = MAXPAYLOAD;
        }
        datasegment.size = 0;
        //read the input file
        fseek(f, currentSeq, SEEK_SET);
        fread(datasegment.data, sizeof(char), datasegment.length, f);

        datasegment.data[datasegment.length] = '\0'; //put the data content into a temp buffer
        sprintf(newSegment, "%d CSc361 %s %d %d %d %d\n%s", 0,datasegment.type, datasegment.seqno, datasegment.ackno, datasegment.length, datasegment.size, datasegment.data);
        //checksum
        summaryInfo.totalSIZE += datasegment.length;
        summaryInfo.totalsegment++;

        if (sendto(sock, newSegment, MAXSEGMENT, 0, (struct sockaddr *)&receiverAddr, receiver_len) == -1)
        {
            perror("rdps: error in sendto()");
            exit(-1);
        }
        printSummary ('S', datasegment);
        currentSeq += datasegment.length;
    }
}


/************************* send fin to end connection *************************/
void sendfin(int seqnum)
{
    tmp++;
    char newSegment[MAXSEGMENT];
    memset(newSegment, 0, sizeof(char));
    struct segment finsegment;
    socklen_t receiver_len = sizeof(receiverAddr);
    memset(finsegment.type, 0, sizeof(finsegment.type));
    int seq = seqnum;
    last = seqnum;
    finsegment.seqno = seq;
    strcpy(finsegment.type, "FIN");
    finsegment.ackno = 0;
    finsegment.length = 0;
    finsegment.size = 0;
    sprintf(newSegment, "%d CSc361 %s %d %d %d %d\n", 0,finsegment.type, finsegment.seqno, finsegment.ackno, finsegment.length, finsegment.size);

    if (sendto(sock, newSegment, MAXSEGMENT, 0, (struct sockaddr *)&receiverAddr, receiver_len) == -1)
    {
        perror("rdps: error in sendto()");
        exit(-1);
    }
    summaryInfo.fin++;

    printSummary ('s', finsegment);
    memset(receivebuf, 0, sizeof(char));
    return;
}


/************************* end connection  *************************/
void finishconnection()
{
    //print log info
    uint64_t delta_us;
    clock_gettime(CLOCK_MONOTONIC, &end);
    delta_us = (end.tv_sec - begin.tv_sec) * 1000000 + (end.tv_nsec - begin.tv_nsec) / 1000;
    summaryInfo.totaltimeduration = delta_us / 1000000.0;
    //print log statistic
    printf("total data bytes sent: %d\nunique data bytes sent: %d\ntotal data segments sent: %d\nunique data segments sent: %d\nSYN segments sent: %d\nFIN segments sent: %d\nRST segments sent: %d\nACK segments received: %d\nRST segments received: %d\ntotal time duration (second): %f\n", summaryInfo.totalSIZE, summaryInfo.uniqueSIZE, summaryInfo.totalsegment, summaryInfo.uniqueSegment, summaryInfo.syn, summaryInfo.fin, summaryInfo.rstsend, summaryInfo.ack, summaryInfo.rstrecieve, summaryInfo.totaltimeduration);
    fclose(f);
    exit(-1);
}


int main(int argc, char *argv[])
{

    /* Variables used to detect packet loss */
    fd_set set, set_orig;
    struct timeval timeout;
    int selectResult;




    summaryInfo.totalSIZE = 0;
    summaryInfo.totalsegment = 0;
    summaryInfo.uniqueSIZE = 0;
    summaryInfo.uniqueSegment = 0;
    summaryInfo.syn = 0;
    summaryInfo.fin = 0;
    summaryInfo.rstsend = 0;
    summaryInfo.ack = 0;
    summaryInfo.rstrecieve = 0;
    summaryInfo.totaltimeduration = 0;





    /* check arguments */
    if (argc != 6)
    {
        printf("Usage: %s rdps <senderIP> <senderPORT> <receiverIP> <receiverPORT> <sender_file_name>\n", argv[0]);
        return -1;
    }

    senderPORT = atoi(argv[2]);
    receiverPORT = atoi(argv[4]);
    senderIP = argv[1];
    receiverIP = argv[3];
    filename = argv[5];

    bzero((char *)&senderAddr, sizeof(senderAddr));
    bzero((char *)&receiverAddr, sizeof(receiverAddr));

    senderAddr.sin_family = AF_INET;
    senderAddr.sin_addr.s_addr = inet_addr(senderIP);
    senderAddr.sin_port = htons(senderPORT);

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
    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) == -1)
    {
        perror("Error on setsockopt");
        return -1;
    }

    // bind
    if (bind(sock, (struct sockaddr *)&senderAddr, sizeof(senderAddr)) < 0)
    {
        perror("Error on binding");
        return -1;
    }

    /* Initialize the file descriptor set. */
    FD_ZERO (&set_orig);
    FD_SET (sock, &set_orig);

    //file size
    f = fopen(filename, "rb");
    if (f == NULL)
    {
        printf("No such file\n");
        exit(-1);
    }

    fseek(f, 0, SEEK_END);
    fileSIZE = ftell(f);


    /************************* now do the real work  *************************/
    current_state = connection; //initialize current state
    receiver_len = sizeof(receiverAddr);
    memset(receivebuf, 0, sizeof(char));
    int count;
    count = 0;
    needrst = 0;
    numFin = 0;
    tmp = 0;
    while (1)
    {

        memset(receivebuf, 0, sizeof(char));
        if (current_state == connection)
        {
            //The timer for the program
            clock_gettime(CLOCK_MONOTONIC, &begin);
            //send SYN
            connecting();

            if ((recvfrom(sock, receivebuf, MAXSEGMENT - 1, 0, (struct sockaddr *)&receiverAddr, &receiver_len)) == -1)
            {
                perror("rdps: error on recvfrom()!");
                return -1;
            }
            parsesegment();
        }

        //transfer state
        if (current_state == transfer)
        {
            synflag = 1;
            memset(receivebuf, 0, sizeof(char));
            transferdata(); //transfer data
            memset(receivebuf, 0, sizeof(char));
            if (current_state == transfer)
            {
                // Keep processing ACK responses until we either receive all the ones we
                // are expecting, or the socket times out.
                int resendCount = 0;
                while (cumAck < windowNextSeq)
                {
                    set = set_orig;
                    /* Initialize the timeout data structure. */
                    timeout.tv_sec = 1;
                    timeout.tv_usec = 0;
                    selectResult = select(sock+1, &set, NULL, NULL, &timeout);
                    //selectResult = 1;
                    if (selectResult == 0)
                    {

                        // TIMEOUT! Resend any buffer segments from the last received ACK onward.
                        resendCount++;
                        if (resendCount < 5)
                        {
                            resendData();
                        }
                        else
                        {
                            sendrst();
                            finishconnection();
                        }

                    }
                    else if (selectResult == 1)
                    {

                        // data waiting on socket
                        memset(receivebuf, 0, sizeof(char));
                        if ((recvfrom(sock, receivebuf, MAXSEGMENT - 1, 0, (struct sockaddr *)&receiverAddr, &receiver_len)) == -1)
                        {
                            perror("rdps: error on recvfrom()!");
                            return -1;
                        }

                        parsesegment();
                    }
                }
            }
        }

        //finish state
        if (current_state == finish)
        {

            memset(receivebuf, 0, sizeof(char));
            //should receive fin ack
            if ((recvfrom(sock, receivebuf, MAXSEGMENT - 1, 0, (struct sockaddr *)&receiverAddr, &receiver_len)) == -1)
            {
                perror("rdps: error on recvfrom()!");
                return -1;
            }
            parsesegment();
        }
    }

    return 0;
}
