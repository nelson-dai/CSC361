/*
   CSC361 Assignment-1 A-Simple-Web-server
   Author: Nelson Dai-V00815253
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
#include <errno.h>


#define TRUE 1
#define FALSE 0
#define BUFFERSIZE 1024	//1k buffer size

int isnum(char *);
void getFilename(char []);
void client(struct sockaddr_in);
void currentTime();
void runTheRequest(struct sockaddr_in *, socklen_t, char [], char []);
void *terminator(void *);


char finaloutput[BUFFERSIZE]; //stores HTTP responds: 200 -> OK; 400 -> Bad Request; 404 -> Not Found
char sendrequest[BUFFERSIZE];    //this is the buffer used by sendto().
int udpSocket;
pthread_t quit;
int stop = FALSE;

/*
    check if the number of arguement is correct
*/
int numArg (int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <port> <directory>\n", argv[0]);
        printf("ERROR, sorry no port and targeted directory provided !\n");
        return -1;
    }
    if (argc < 3) {
        printf("Usage: %s <port> <directory>\n", argv[0]);
        if (atoi(argv[1]) == 0) {
            printf("ERROR, no port provided !\n");
            return -1;
        } else {
            printf("ERROR, no targeted directory provided !\n");
            return -1;
        }
    }
    return 0;
}

/*
    Check if the port number is valid
*/
int isnum(char *num) {
    while (*num){
        if (isdigit(*num++) == 0) 
        return 0;
    }
    return 1;
}

/*
    get the IP and port number from client and record it for trans file
*/
void client(struct sockaddr_in clientAddr) {   
    char portn[6];
    char c[2] = ":";
    strcat(finaloutput, " ");
    int port = ntohs(clientAddr.sin_port);  
    sprintf(portn, "%d", port);          
    strcat(finaloutput, inet_ntoa(clientAddr.sin_addr));  //ip
    strcat(finaloutput, c);         //:
    strcat(finaloutput, portn);            //port number
    strcat(finaloutput, " ");         //" "
    
}

/*
    get the current time
*/
void currentTime() {
    time_t now;
    struct tm * timeinfo;
    time ( &now );
    timeinfo = localtime ( &now );
    strftime(finaloutput, BUFFERSIZE, "%b %d %H:%M:%S", timeinfo);

 }

/*
	checks to see if the file path contains /../ anywwhere
*/
int containsBackLinks(char * path) {
	char * token = strtok(path, "/");
	while (token != NULL) {
		if (strcmp(token, "..") == 0) return 1;
		token = strtok(NULL, "/");
	}
	return 0;
}

/*
    process the request and try to read the file
*/
void runTheRequest(struct sockaddr_in * clientAddr, socklen_t client_addr_size, char * userRequest, char * dir) {
	
	// The file handler and buffer, for when the request is successful.
	strcpy(dir,"www");
	FILE *fpr;
	char * fbuf;
	size_t fBufSize = BUFFERSIZE;
	
	// strings to hold the GET, the request object, and the HTTP/1.0 input text.
	char method[5];
	char object[255];
	char version[10];
	
	// string used to build the path to the file.
	char path[255];

	// separate the method, object and version parts of 
	// the request into separate char arrays.
	char * token = strtok(userRequest, " ");
	strcpy(method, token);

	token = strtok(NULL, " ");
	strcpy(object, token);

	token = strtok(NULL, "\r\n");
	strcpy(version, token);

	// Update the server output string with the request information.
	strcat(finaloutput, method);
	strcat(finaloutput, " ");
	strcat(finaloutput, object);
	strcat(finaloutput, " ");
	strcat(finaloutput, version);
	strcat(finaloutput, "; ");

	// Check the format of the method: "GET"
	int TF=strcmp(method,"GET");
	int tf=strcmp(method,"get");
	if (TF !=0 && tf !=0){
		strcat(finaloutput, "HTTP/1.0 400 Bad Request; ");
		strcat(sendrequest, "HTTP/1.0 400 Bad Request\n\n");
		sendto(udpSocket, sendrequest, strlen(sendrequest), 0, (struct sockaddr *) clientAddr, client_addr_size);
		return;
	}

	char lalala[255]="";
	strcpy(lalala,object);
	// Check the format of the object doesn't contain /../ anywhere.
	 if (containsBackLinks(object)) {
	 	strcat(finaloutput, "HTTP/1.0 404 Not Found; ");
	 	strcat(sendrequest, "HTTP/1.0 404 Not Found\n\n");
		sendto(udpSocket, sendrequest, strlen(sendrequest), 0, (struct sockaddr *) clientAddr, client_addr_size);
	 	return;
	 }
	strcpy(object,lalala);
	//request version should be "HTTP/1.0" 
	if(strcmp(version,"HTTP/1.0")!=0){
		strcat(finaloutput, "HTTP/1.0 400 Bad Request; ");
		strcat(sendrequest, "HTTP/1.0 400 Bad Request\n\n");
		sendto(udpSocket, sendrequest, strlen(sendrequest), 0, (struct sockaddr *) clientAddr, client_addr_size);
		return;
	}

	// The request is good, attempt to open the file. Start with the
	// document root, as specified on the command-line.
	strcpy(path, dir);
	
	
	// Remove the '/' character at the end, if it was supplied. (We'll add
	// it with the request object string.)
	if (path[strlen(dir)-1] == '/') path[strlen(dir)-1] = '\0';
	
	// Add the request object to the base dir; if the request object is
	// just the root (i.e. '/'), then get /index.html.
	if (strcmp(object, "/") == 0) {
		strcat(path, "/index.html");
		strcat(dir, "/index.html");

	} else {
		strcat(path, object);
		strcat(dir,object);
	}
	

	// Now attempt to open the file for reading
	fpr = fopen(dir,"rb"); //      www/web/home.html
	if( fpr == NULL ){
		
		// Problems opening file, report 404 error.
		strcat(finaloutput, "HTTP/1.0 404 Not Found; ");   //add to server output
		strcat(finaloutput, path);
		strcat(sendrequest, "HTTP/1.0 404 Not Found\n\n");   //add to http response
		sendto(udpSocket, sendrequest, strlen(sendrequest), 0, (struct sockaddr *) clientAddr, client_addr_size);
		return;
	} else {
		size_t bytesRead;

		// The file is open, send the 200 response, open the file and send the contents.
		strcat(finaloutput, "HTTP/1.0 200 OK; "); //add to server output
		strcat(finaloutput,path);
		strcpy(sendrequest, "HTTP/1.0 200 OK\n\n"); //add to http response

		//printf("Sending: %s\n", sendrequest);

		sendto(udpSocket, sendrequest, strlen(sendrequest), 0, (struct sockaddr *) clientAddr, client_addr_size);
		
		// prepare the file buffer. We have to free this later.
		fbuf = (char *) malloc(BUFFERSIZE * sizeof(char));
		
		// loop to read in the file. Use getline() to read in one line at a time,
		// adding the result to the http response buffer for each (successful) line.
		while (! feof(fpr)) {
			size_t bytesRead = fread(fbuf, sizeof(char), BUFFERSIZE, fpr);
			if (bytesRead > 0) {
				sendto(udpSocket, fbuf, bytesRead, 0, (struct sockaddr *) clientAddr, client_addr_size);
			}
		}
		
		// file reading is done. Close the file and free the buffer's memory.
		fclose(fpr);
		free(fbuf);
	}
}




/*
  terminate the program when  user press 'q' the code is from a template online at http://stackoverflow.com/questions/1798511/how-to-avoid-press-enter-with-any-getchar 
*/

void *terminator(void *quit) {
    int c;
    static struct termios oldt, newt;  //Derived code
    tcgetattr( STDIN_FILENO, &oldt);  //get terminal attributes. Derived code
    newt = oldt;		      //Derived code
    newt.c_lflag &= ~(ICANON | ECHO); //Slightly changed
    tcsetattr( STDIN_FILENO, TCSANOW, &newt);//Derived code
    while((c=getchar())!= 'q'){         //do nothing
    }
    tcsetattr( STDIN_FILENO, TCSANOW, &oldt);//Derived code
    close(udpSocket);        //close socket
    exit(1);
    return NULL;
    //stop = TRUE;

	//return NULL;
}


int main(int argc, char *argv[]) { //    ./sws <port> <directory>

	int tmp;
	int portnumber;
	int nBytes;


/*For the socket and UDP part belwo I am using a template online and modified for it to work. The website is http://www.programminglogic.com/sockets-programming-in-c-using-udp-datagrams/  */

	// socket structures.	
	char buffer[1024];
	struct sockaddr_in serverAddr, clientAddr;
	socklen_t addr_size, client_addr_size;
	

	//check port
	if(numArg(argc,argv) == -1) return -1;
	tmp = isnum(argv[1]);
	if (tmp == 0) {
		printf("ERROR, invaild port !\n");
		return -1;
	}

	portnumber = atoi(argv[1]);
	printf("sws is running on UDP port %d and serving %s\n", portnumber,argv[2]);
    	printf("Press 'q' to quit ...\n");
    	printf("\n");

	// Create UDP socket
	udpSocket = socket(PF_INET, SOCK_DGRAM, 0);
	if (udpSocket < 0) { //error
        perror("Error on socket");
        return -1;
	}


	// Configure settings in address struct
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(portnumber);
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	memset(serverAddr.sin_zero, '\0', sizeof serverAddr.sin_zero);  

	bind(udpSocket, (struct sockaddr *) &serverAddr, sizeof(serverAddr));


	// Initialize size variable to be used later on
	addr_size = sizeof clientAddr;

	



	int rc;
	rc = pthread_create(&quit, NULL, terminator, (void *) quit);    //create a thread for monitoring keypress
	

	while(1){
			

	// Nothing on the keyboard, so must be a client connecting on the socket.
	nBytes = recvfrom(udpSocket,buffer,1024,0,(struct sockaddr *)&clientAddr, &addr_size);
			

	// prepare to build the server output and client response data.
	finaloutput[0]='\0';
	sendrequest[0]='\0';
			
	// add time stamp and client address info to the server output.
	currentTime();
	client(clientAddr);

	// now process the request.
	runTheRequest(&clientAddr, addr_size, buffer, argv[2]);

	// output the server result.
	printf("%s\n", finaloutput);
	printf("sws is running on UDP port %d and serving %s\n", portnumber, argv[2]);
        printf("Press 'q' to quit ...\n");
        printf("\n");

	
		
	}

	return 0;
}
