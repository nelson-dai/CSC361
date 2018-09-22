/*
*  rdps.c
*  Nelosn Dai-V00815253
*  March 7th, 2017
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
char receivebuf[MAXSEGMENT];
char sendbuf[MAXSEGMENT];
long int lastsegment;
char *filename;
long fileSIZE;
FILE *f;
socklen_t receiver_len;
clock_t begin, end;
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


struct segment
{
    char type[4];
    int seqno;
    int ackno;
    int length;
    int size;
    char data[MAXPAYLOAD + 1]; //mac or linux TODO !!!
};


enum state
{
    connection,
    transfer,
    finish,
} current_state;


struct summary
{
    int totalSIZE;
    int totalsegment;
    int syn;
    int fin;
    int rstsend;
    int ack;
    int rstrecieve;
    double totaltimeduration;
};
struct summary summaryInfo;


// Reference: http://www.cse.yorku.ca/~oz/hash.html djb2 hash.
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



/************************* connection establishment *************************/
int connecting()
{
    char newSegment[MAXSEGMENT];  //new segment
    char segWcheck[MAXSEGMENT];   // segment with checksum
    memset(newSegment, 0, sizeof(char));
    memset(segWcheck, 0, sizeof(char));
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
    sprintf(newSegment, "CSc361 %s %d %d %d %d\n%s", synsegment.type, synsegment.seqno, synsegment.ackno, synsegment.length, synsegment.size, synsegment.data);
    unsigned long checksum = hash(newSegment);
    sprintf(segWcheck, "%lu %s", checksum, newSegment);

    /* no payload in SYN */
    if (sendto(sock, segWcheck, MAXSEGMENT, 0, (struct sockaddr *)&receiverAddr, receiver_len) == -1)
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
    /* Issue: ack damaged! */
    char segmenttype[5]; //TODO mac or linux !!!
    memset(segmenttype, 0, sizeof(segmenttype));
    int space;
    int i;
    int nextline;
    //find the position of the end of checksum
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
    char newSegment[MAXSEGMENT];
    memset(newSegment, 0, sizeof(char));
    strncpy(newSegment, receivebuf + space + 1, sizeof(newSegment));
    //check if segment is damaged
    if (hash(newSegment) != checksum)
    {
        memset(receivebuf, 0, sizeof(char)); //dont send ack. let sender timeout
        return;
    }
    //check magic
    char magicnumber[7];
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
    cumAck = realSegment.ackno;
    receiverwindow = realSegment.size;
    summaryInfo.ack++;

    //we are at the last segment because data length is less than max payload length
    if (realSegment.seqno == 0 && realSegment.ackno == 0 && realSegment.length == 0 && realSegment.size == 0)
    {
        finflag = 1;
    }
    else
    {
        realSegment.ackno--;
    }

    //we should send fin
    if (realSegment.ackno == fileSIZE && numFin == 0)
    {
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
    seqnum = cumAck - 1; //sequence number in the first segment

    for (i = 0; i < receiverwindow / MAXSEGMENT; i++)
    { //how many segment we can send
        memset(receivebuf, 0, sizeof(char));
        intransfer = 1;
        rewind(f);

        char newSegment[MAXSEGMENT];
        char segWcheck[MAXSEGMENT];
        memset(segWcheck, 0, sizeof(char));
        memset(newSegment, 0, sizeof(char));
        struct segment datasegment; //make a new data segment
        memset(datasegment.data, 0, sizeof(char));
        datasegment.seqno = seqnum + 1;  //put sequence number
        strcpy(datasegment.type, "DAT"); //type is data
        datasegment.ackno = 0;
        if ((seqnum + MAXPAYLOAD) > fileSIZE)
        { //the last segment payload<900
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
        sprintf(newSegment, "CSc361 %s %d %d %d %d\n%s", datasegment.type, datasegment.seqno, datasegment.ackno, datasegment.length, datasegment.size, datasegment.data);
        //checksum
        summaryInfo.totalSIZE += datasegment.length;
        summaryInfo.totalsegment++;
        unsigned long checksum = hash(newSegment);
        sprintf(segWcheck, "%lu %s", checksum, newSegment); //after checksum

        memset(receivebuf, 0, sizeof(char));
        //send the segment
        if (sendto(sock, segWcheck, MAXSEGMENT, 0, (struct sockaddr *)&receiverAddr, receiver_len) == -1)
        {
            perror("rdps: error in sendto()");
            exit(-1);
        }
        datasegment.seqno--;

        if (tmp == 0)
        {
            printSummary ('s', datasegment);
        }

        current_state == transfer;   //change state
        seqnum += datasegment.length; //update sequence number

        if (seqnum == fileSIZE)
        { //change state to finish

            intransfer = 0;
            current_state = finish;
            memset(newSegment, 0, sizeof(char));
            memset(datasegment.data, 0, sizeof(char));
            return;
        }

        memset(receivebuf, 0, sizeof(char));
        memset(newSegment, 0, sizeof(char));
        memset(datasegment.data, 0, sizeof(char));
    }
    intransfer = 0;
    reflag = 0;
}




/************************* send fin to end connection *************************/
void sendfin(int seqnum)
{
    tmp++;
    char newSegment[MAXSEGMENT];
    char segWcheck[MAXSEGMENT];
    memset(newSegment, 0, sizeof(char));
    memset(segWcheck, 0, sizeof(char));
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
    sprintf(newSegment, "CSc361 %s %d %d %d %d\n", finsegment.type, finsegment.seqno, finsegment.ackno, finsegment.length, finsegment.size);
    unsigned long checksum = hash(newSegment);
    sprintf(segWcheck, "%lu %s", checksum, newSegment);

    if (sendto(sock, segWcheck, MAXSEGMENT, 0, (struct sockaddr *)&receiverAddr, receiver_len) == -1)
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
    end = clock();
    timeUSE = (double)(end - begin) / CLOCKS_PER_SEC;
    summaryInfo.totaltimeduration = timeUSE;
    //print log statistic
    printf("total data bytes sent: %d\nunique data bytes sent: %lu\ntotal data segments sent: %d\nunique data segments sent: %d\nSYN segments sent: %d\nFIN segments sent: %d\nRST segments sent: %d\nACK segments received: %d\nRST segments received: %d\ntotal time duration (second): %f\n", summaryInfo.totalSIZE, fileSIZE, summaryInfo.totalsegment, (int)fileSIZE / 900 + 1, summaryInfo.syn, summaryInfo.fin, summaryInfo.rstsend, summaryInfo.ack, summaryInfo.rstrecieve, summaryInfo.totaltimeduration);

    fclose(f);
    exit(-1);
}


int main(int argc, char *argv[])
{
    //The timer for the program
    begin = clock();

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
        { //send SYN
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

                memset(receivebuf, 0, sizeof(char));
                if ((recvfrom(sock, receivebuf, MAXSEGMENT - 1, 0, (struct sockaddr *)&receiverAddr, &receiver_len)) == -1)
                {
                    perror("rdps: error on recvfrom()!");
                    return -1;
                }

                parsesegment();
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
