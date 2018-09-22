CSC361-Assignment 2 Design
Nelson Dai-V00815253
March 3rd, 2017


1. How do you design and implement your RDP header and header fields? Do you use any additional header fields?

	I am using the header as a structure in C, in the header structure for sender I have type(flag), checksum, seq, Ack, payloadLength and data. For the receiver I do not need data but 		window size in the header structure. Since we have learnt about checksum in both lecture and lab I am going to add it as an additional header field.



2. How do you design and implement the connection management using SYN, FIN and RST packets? How to choose the initial sequence number?

	Connection establish: Sender sends a SNY to receiver if receiver receives it them reply an Ack to establish the connection. If not then sender sends a SNY again.

   	Connection finish: Sender sends an FIN to receiver, and if receiver receives it, the receiver replies a FIN repeatedly during a time period to ensure that the sender receives 				   it, then close the connection.

	Connection reset: If the same error occurs 10(maybe more or less) times continuously, then it means I need to reset the connection. The sender or the receiver sends RST to the 			  the otherone after the other one receives RST they both back to the beginning.

	Initial sequence number: To be safe, I am going to use the function rand() to randomly chose an integer as the initial seq number.



3. How do you design and implement the flow control using window size? How to choose the initial window size and adjust the size? How to read and write the file and how to manage the buffer at the sender and receiver side, respectively?


	As in question 1 I meanthioned that I am going to include the window size in the receiver's header structure to let the sender know how much space the receiver have left 		currectly, so that the sender knows how many packets it should send.

	initial window size: I am going to set the initial window size as: 5*1024=5120 which is the size for 5 packets.

	adjust the size: in the beginning it has size of 5120, after receiving couple packets receiver calculates the size left by using 5120-numpackets*1024.
			 read and write: read packet in the buffer only when the receiver window size is 0, write into buffer only when sender buffer is space left. (fopen, 				 fread,fseek,fwrite,fclose)




4. How do you design and implement the error detection, notification and recovery? How to use timer? How many timers do you use? How to respond to the events at the sender and receiver side, respectively? How to ensure reliable data transfer?

	error detection: I am going to use retransmition, which needs seq, ack, timer and checksum if I am going to include it. By using checksum Who ever sends a packet, it should 				 first use hash(or maybe other way) to calculate a checksum value, then add the value into the packet. When the other side receices it, first read the value 				 then calculate the packet using the same technic if the value is same then it is good.

	timer: As for timer, each packet has a timer (which means will need same number of timers as the packets sent), the timer starts the same time as the packet sends. If the one 		       who sends the packet does not get an responds in a perioud of time(timer) then send the packet again.

	respond: Look for the packet flag in the header and act depends on that.

	reliable data transfer: Use Ack and seq number 




5. Any additional design and implementation considerations you want to get feedback from your lab instructor?

	Not for now, but if I do get some questions I will bring it to my lab instructor.
