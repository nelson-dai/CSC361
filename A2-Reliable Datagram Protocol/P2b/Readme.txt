/*
   Nelson Dai-V00815253
   CSC361 Assignment 2
*/
CSC361-Assignment 2 Design
Nelson Dai-V00815253
March 23rd, 2017
It runs no problem when sending a large txt or html file at 10% drop rate, but it can not deal with the sent.dat file. 
There must be some magic power in the dat file so that the file after tansfer will not be identical to the original one.

Below is my changes to my initial design:

1.I ended up not including the checksum in the header. So now in my sender head it has: type(flag), seqno, ackno, length, size, and data. In the receiver's header it has: type(flag), seqno, ackno, length and size.

2.When sending data it checks for a seqnum of 1 and changes it to a zero before sending. 

3.I did not include in my initial design but I am going to use two-way-handshake. 

4.There also some minor changes I made but they do not infect my original design.
