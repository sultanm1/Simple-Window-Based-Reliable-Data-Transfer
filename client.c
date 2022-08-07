#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <fcntl.h>
#include <netdb.h>

// =====================================

#define RTO 50000 /* timeout in microseconds */
#define HDR_SIZE 12 /* header size*/
#define PKT_SIZE 524 /* total packet size */
#define PAYLOAD_SIZE 512 /* PKT_SIZE - HDR_SIZE */
#define WND_SIZE 10 /* window size*/
#define MAX_SEQN 25601 /* number of sequence numbers [0-25600] */
#define FIN_WAIT 2 /* seconds to wait after receiving FIN*/

// Packet Structure: Described in Section 2.1.1 of the spec. DO NOT CHANGE!
struct packet {
    unsigned short seqnum;
    unsigned short acknum;
    char syn;
    char fin;
    char ack;
    char dupack;
    unsigned int length;
    char payload[PAYLOAD_SIZE];
};




/*
USED
https://www.geeksforgeeks.org/queue-set-1introduction-and-array-implementation/
to have implementation of tthe Queue

*/




/*End implementation of queue*/
///////


// Printing Functions: Call them on receiving/sending/packet timeout according
// Section 2.6 of the spec. The content is already conformant with the spec,
// no need to change. Only call them at correct times.
void printRecv(struct packet* pkt) {
    printf("RECV %d %d%s%s%s\n", pkt->seqnum, pkt->acknum, pkt->syn ? " SYN": "", pkt->fin ? " FIN": "", (pkt->ack || pkt->dupack) ? " ACK": "");
}

void printSend(struct packet* pkt, int resend) {
    if (resend)
        printf("RESEND %d %d%s%s%s\n", pkt->seqnum, pkt->acknum, pkt->syn ? " SYN": "", pkt->fin ? " FIN": "", pkt->ack ? " ACK": "");
    else
        printf("SEND %d %d%s%s%s%s\n", pkt->seqnum, pkt->acknum, pkt->syn ? " SYN": "", pkt->fin ? " FIN": "", pkt->ack ? " ACK": "", pkt->dupack ? " DUP-ACK": "");
}

void printTimeout(struct packet* pkt) {
    printf("TIMEOUT %d\n", pkt->seqnum);
}

// Building a packet by filling the header and contents.
// This function is provided to you and you can use it directly
void buildPkt(struct packet* pkt, unsigned short seqnum, unsigned short acknum, char syn, char fin, char ack, char dupack, unsigned int length, const char* payload) {
    pkt->seqnum = seqnum;
    pkt->acknum = acknum;
    pkt->syn = syn;
    pkt->fin = fin;
    pkt->ack = ack;
    pkt->dupack = dupack;
    pkt->length = length;
    memcpy(pkt->payload, payload, length);
}


double setTimer() {
    struct timeval e;
    gettimeofday(&e, NULL);
    return (double) e.tv_sec + (double) e.tv_usec/1000000 + (double) RTO/1000000;
}

double setFinTimer() {
    struct timeval e;
    gettimeofday(&e, NULL);
    return (double) e.tv_sec + (double) e.tv_usec/1000000 + (double) FIN_WAIT;
}

int isTimeout(double end) {
    struct timeval s;
    gettimeofday(&s, NULL);
    double start = (double) s.tv_sec + (double) s.tv_usec/1000000;
    return ((end - start) < 0.0);
}


int main (int argc, char *argv[])
{
    if (argc != 4) {
        perror("ERROR: incorrect number of arguments\n");
        exit(1);
    }

    struct in_addr servIP;
    if (inet_aton(argv[1], &servIP) == 0) {
        struct hostent* host_entry;
        host_entry = gethostbyname(argv[1]);
        if (host_entry == NULL) {
            perror("ERROR: IP address not in standard dot notation\n");
            exit(1);
        }
        servIP = *((struct in_addr*) host_entry->h_addr_list[0]);
    }

    unsigned int servPort = atoi(argv[2]);

    FILE* fp = fopen(argv[3], "r");
    if (fp == NULL) {
        perror("ERROR: File not found\n");
        exit(1);
    }

    // =====================================
    // Socket Setup

    int sockfd;
    struct sockaddr_in servaddr;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr = servIP;
    servaddr.sin_port = htons(servPort);
    memset(servaddr.sin_zero, '\0', sizeof(servaddr.sin_zero));

    int servaddrlen = sizeof(servaddr);

    //Set the socket as non-blocking.
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    // Establish Connection

    struct packet synpkt, synackpkt;

    unsigned short seqNum = rand() % MAX_SEQN;
    buildPkt(&synpkt, seqNum, 0, 1, 0, 0, 0, 0, NULL);

    printSend(&synpkt, 0);
    sendto(sockfd, &synpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
    double timer = setTimer();
    int n;

    while (1) {
        while (1) {
            n = recvfrom(sockfd, &synackpkt, PKT_SIZE, 0, (struct sockaddr *) &servaddr, (socklen_t *) &servaddrlen);

            if (n > 0)
                break;
            else if (isTimeout(timer)) {
                printTimeout(&synpkt);
                printSend(&synpkt, 1);
                sendto(sockfd, &synpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                timer = setTimer();
            }
        }

        printRecv(&synackpkt);
        if ((synackpkt.ack || synackpkt.dupack) && synackpkt.syn && synackpkt.acknum == (seqNum + 1) % MAX_SEQN) {
            seqNum = synackpkt.acknum;
            break;
        }
    }

    // File reading vars.
    char buf[PAYLOAD_SIZE];
    size_t m;

    // Circular reading vars.

    struct packet ackpkt;
    struct packet pkts[WND_SIZE];
    int s = 0;
    int e = 0;
    // int full = 0;

    // Send First Packet

    m = fread(buf, 1, PAYLOAD_SIZE, fp);

    buildPkt(&pkts[0], seqNum, (synackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 1, 0, m, buf);
    printSend(&pkts[0], 0);
    sendto(sockfd, &pkts[0], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
    timer = setTimer();
    buildPkt(&pkts[0], seqNum, (synackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 0, 1, m, buf);

    e = 1;

    int smallFileTransmission = 0;



    // Small file transmission implementation
    if(m < 512){
      smallFileTransmission = 1;
      while (1) {
          n = recvfrom(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr *) &servaddr, (socklen_t *) &servaddrlen);

          if (n > 0) {
              printRecv(&ackpkt);
              m = fread(buf, 1, PAYLOAD_SIZE, fp);
              if(m <= 0){
                break;
              }
              buildPkt(&pkts[1], ackpkt.acknum, (ackpkt.seqnum) % MAX_SEQN, 0, 0, 0, 0, m, buf);
              //If you want the ACK NUM TO GO BIGGER GET RID OF %MAX SEQN
              printSend(&pkts[1], 0);
              sendto(sockfd, &pkts[1], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
              // break;
          }
      }
        n = recvfrom(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr *) &servaddr, (socklen_t *) &servaddrlen);
    }


    //implementation of GBN
    //First have an initial loop that sends first couple packets
    //Then, have a second loop that for each ACK'd packet slides the window
    /*
    Put packets in queue,
    [1 2 3 ... 10]
    If queue size is equal to window size
      Wait for acknum

    Dequeue and add last new packet
    when recieve an ack we dequeue,
    */

    //Upon timeout --> what we need to do is set timer if
    //if timeout occurs we resend
    //0  512
    else{

      s = 1; //We sent 1 packet

      seqNum += (int)m;
      int lessThanWindowSize = 1;
      for(int j = 1; j < WND_SIZE; j++){

        m = fread(buf, 1, PAYLOAD_SIZE, fp);
        if(m <= 0){
          break;
        }

        buildPkt(&pkts[j], seqNum % MAX_SEQN, (synackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 0, 0, m, buf);
        printSend(&pkts[j], 0);
        sendto(sockfd, &pkts[j], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);

        seqNum += (int)m;
        lessThanWindowSize++;
        s++; //THis is our counter for where we are in the array of packets
      }
      // printf("s variable is %d\n", s);

    //[1 2 3 ... 10]
    //As soon as we get the ack for the packet 1 , we dequeue packet 1
    //and enqueue packet 11
    //Here we do if less than lessThanWindowSize is bigger than 10 then add our old implementation

    double mytimer = setTimer();

    if(lessThanWindowSize == 10){
      while (1) {
        if(isTimeout(mytimer)){
          printTimeout(&pkts[(s) % WND_SIZE]);
          //call timeout on oldest packet should be s + 1

          //resend all packets in window
          int news = (s) % WND_SIZE;
          int counter = 0;
          for(; counter != 10; news++, counter++){
            printSend(&pkts[news % WND_SIZE], 1);
            sendto(sockfd, &pkts[news % WND_SIZE], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
          }

          mytimer = setTimer(); //resetting timer
        }
        n = recvfrom(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr *) &servaddr, (socklen_t *) &servaddrlen);
          if (n > 0) {

            printRecv(&ackpkt);
            if((pkts[s % WND_SIZE].seqnum + pkts[s % WND_SIZE].length) % MAX_SEQN  == ackpkt.acknum ||
            (pkts[s % WND_SIZE].seqnum + pkts[s % WND_SIZE].length) % MAX_SEQN  < ackpkt.acknum){
              mytimer = setTimer(); //resetting timer
              m = fread(buf, 1, PAYLOAD_SIZE, fp);
              if(m <= 0){
                break;
              }


              buildPkt(&pkts[s % WND_SIZE], seqNum, (synackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 0, 0, m, buf);
              printSend(&pkts[s % WND_SIZE], 0);
              sendto(sockfd, &pkts[s % WND_SIZE], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);

              seqNum = (seqNum + (int)m) % MAX_SEQN;
              s++;

            }
          }
      }
    }
      //

    while (1) {
      n = recvfrom(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr *) &servaddr, (socklen_t *) &servaddrlen);
        if (n > 0) {
            printRecv(&ackpkt);
            break;
            lessThanWindowSize--;
            if(lessThanWindowSize == 1 || (pkts[s % WND_SIZE].seqnum + pkts[s % WND_SIZE].length) % MAX_SEQN  == ackpkt.acknum){
              // printf("%s\n", );  //-1 to take into account first packet
              break;
            }

        }
    }
    n = recvfrom(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr *) &servaddr, (socklen_t *) &servaddrlen);
  }


    fclose(fp);

    // Connection Teardown

    struct packet finpkt, recvpkt;

    if(smallFileTransmission == 1){//Tweaked seq num of this
      buildPkt(&finpkt, ackpkt.acknum, 0, 0, 1, 0, 0, 0, NULL);  //Tweaked seq num of this
      buildPkt(&ackpkt, (ackpkt.acknum + 1) % MAX_SEQN, (ackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 1, 0, 0, NULL);
    }else{
      buildPkt(&finpkt, seqNum, 0, 0, 1, 0, 0, 0, NULL);  //Tweaked seq num of this
      buildPkt(&ackpkt, (seqNum + 1) % MAX_SEQN, (ackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 1, 0, 0, NULL);
    }


    printSend(&finpkt, 0);
    sendto(sockfd, &finpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen); //This is sending FIN packet with 0 data
    timer = setTimer();
    int timerOn = 1;

    double finTimer;
    int finTimerOn = 0;

    while (1) {
        while (1) {
            n = recvfrom(sockfd, &recvpkt, PKT_SIZE, 0, (struct sockaddr *) &servaddr, (socklen_t *) &servaddrlen);

            if (n > 0)
                break;
            if (timerOn && isTimeout(timer)) {
                printTimeout(&finpkt);
                printSend(&finpkt, 1);
                if (finTimerOn)
                    timerOn = 0;
                else
                    sendto(sockfd, &finpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                timer = setTimer();
            }
            if (finTimerOn && isTimeout(finTimer)) {
                close(sockfd);
                if (! timerOn)
                    exit(0);
            }
        }
        printRecv(&recvpkt);
        if ((recvpkt.ack || recvpkt.dupack) && recvpkt.acknum == (finpkt.seqnum + 1) % MAX_SEQN) {
            timerOn = 0;
        }
        else if (recvpkt.fin && (recvpkt.seqnum + 1) % MAX_SEQN == ackpkt.acknum) {
            printSend(&ackpkt, 0);
            sendto(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
            finTimer = setFinTimer();
            finTimerOn = 1;
            buildPkt(&ackpkt, ackpkt.seqnum, ackpkt.acknum, 0, 0, 0, 1, 0, NULL);
        }
    }
}


