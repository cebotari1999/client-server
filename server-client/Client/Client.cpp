#ifndef UNICODE
#define UNICODE
#endif

#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>
#include <Ws2tcpip.h>
#include <stdio.h>
#include <Windows.h>
#include <chrono>

#define BUFSIZE 1024

using namespace std;
using namespace chrono;

// Link with ws2_32.lib
#pragma comment(lib, "Ws2_32.lib")

enum State { CONNECTED, WAITING, DISCONNECTED };

State ClientState;
steady_clock::time_point LastMessage;


int InitializeWinsock(WSADATA &wsaData) {
    
    int Result = WSAStartup(MAKEWORD(2, 2), &wsaData);

    if (Result != NO_ERROR) {
        wprintf(L"WSAStartup failed with error: %d\n", Result);
        WSACleanup();
    }

    return Result;
}

int CreateSocket(SOCKET &SendSocket) {
    SendSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    u_long mode = 1; // 1  to enable non-blocking socket
    ioctlsocket(SendSocket, FIONBIO, &mode);

    if (SendSocket == INVALID_SOCKET) {
        wprintf(L"socket failed with error: %ld\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    return 0;
}

int CompleteSockStruct(SOCKADDR_IN& RecvAddr, unsigned short Port) {
    //---------------------------------------------
    // Set up the RecvAddr structure with the IP address of
    // the receiver (in this example case "192.168.1.1")
    // and the specified port number.
    RecvAddr.sin_family = AF_INET;
    RecvAddr.sin_port = htons(Port); // 34921
    int Ret = inet_pton(AF_INET, "127.0.0.1", &RecvAddr.sin_addr.s_addr); //3.216.94.65

    if (Ret < 0) {
        wprintf(L"inet_pton failed with error: %ld\n", WSAGetLastError());
    }

    return Ret;
}

int ReceiveDatagrams(SOCKET& RecvSocket, SOCKADDR_IN& RecvAddr) {
    //-----------------------------------------------
    // Call the recvfrom function to receive datagrams
    // on the bound socket.
    char RecvBuf[BUFSIZE];
    UINT8 Type = 255;

    int RecvAddrSize = sizeof(RecvAddr);

    memset(RecvBuf, 0, BUFSIZE);
    int Ret = recvfrom(RecvSocket, RecvBuf, sizeof(RecvBuf), 0, (SOCKADDR*)&RecvAddr, &RecvAddrSize);
    sscanf_s(RecvBuf, "%hhu", &Type);

    if (Ret < 1) {
       wprintf(L"recvfrom failed with error %d\n", WSAGetLastError());
    }
    else {
        printf("Recive %s", RecvBuf);
    }

    //-------------------------------------------------
    // If message type is 0, that means client recive
    // server confirmation that is connected.
    if (Type == 0) {
        ClientState = CONNECTED;
    }

    return Ret;
}

void SendDatagram(SOCKET &SendSocket, sockaddr_in &RecvAddr, char* SendBuf) {
    //---------------------------------------
    // Send a message to server using sendto.
    int Ret = sendto(SendSocket, SendBuf, BUFSIZE, 0, (SOCKADDR*)&RecvAddr, sizeof(RecvAddr));

    if (Ret == SOCKET_ERROR) {
        wprintf(L"sendto failed with error: %d\n", WSAGetLastError());
    }
    else {
        wprintf(L"Send\n");
    }
}

void Update(SOCKET& SendSocket, sockaddr_in& RecvAddr, FD_SET& ReadFds, int FdMax) {
    char SendBuf[BUFSIZE];
    int Ret = 0;

    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;


    FD_SET TmpFds = ReadFds;
    Ret = select(FdMax + 1, &TmpFds, NULL, NULL, &timeout);

    //--------------------------------------------
    // Clean the buffer.
    memset(SendBuf, 0, BUFSIZE);

    switch (ClientState) {
        //----------------------------------------------
        // If the client is disconnected, it sends
        // a message to conect to server.
        case DISCONNECTED:
            snprintf(SendBuf, BUFSIZE, "%hhu %s", 0, "Hello!");
            SendDatagram(SendSocket, RecvAddr, SendBuf);
            ClientState = WAITING;
            break;
        //------------------------------------------------
        // After the message to connect is send, the client
        // is waiting for server confirmation.
        case WAITING:
            if (FD_ISSET(SendSocket, &TmpFds) && Ret > 0) {
                Ret = ReceiveDatagrams(SendSocket, RecvAddr);
            }
            break;
        //----------------------------------------------------
        // When the client is connected, it is waiting for PING
        // message from server, when the message is recived,
        // the client send a PONG.
        case CONNECTED:
                

            //---------------------------------------
            // Check if are messages on the socket 
            // and recive them. If client wants to 
            // connect, the server sends a confirmation
            // that client is connected.
            if (FD_ISSET(SendSocket, &TmpFds) && Ret > 0) {
                Ret = ReceiveDatagrams(SendSocket, RecvAddr);
            }

            if (Ret > 0) {
                snprintf(SendBuf, BUFSIZE, "%hhu %s", 1, "PONG!");
                SendDatagram(SendSocket, RecvAddr, SendBuf);
            }
            LastMessage = high_resolution_clock::now();
            break;

        default:
            snprintf(SendBuf, BUFSIZE, "%hhu %s", 2, "Undefine state.\n");
            SendDatagram(SendSocket, RecvAddr, SendBuf);
    }
}

int CloseSocket(SOCKET& SendSocket) {
    wprintf(L"Finished sending. Closing socket.\n");
    int Ret = closesocket(SendSocket);
    if (Ret == SOCKET_ERROR) {
        wprintf(L"closesocket failed with error: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
}

int main()
{
    int iResult;
    WSADATA wsaData;

    SOCKET SendSocket = INVALID_SOCKET;
    sockaddr_in RecvAddr;

    unsigned short Port = 27015;

    fd_set ReadFds;
    int FdMax;
    FD_ZERO(&ReadFds);

    //----------------------
    // Initialize Winsock
    InitializeWinsock(wsaData);

    //---------------------------------------------
    // Create a socket for sending data
    CreateSocket(SendSocket);

    //---------------------------------------------
    // Set up the RecvAddr structure
    CompleteSockStruct(RecvAddr, Port);

    //---------------------------------------------
    // Set the state of client
    ClientState = DISCONNECTED;
    LastMessage = steady_clock::now();

    FD_SET(SendSocket, &ReadFds);
    FdMax = SendSocket;
   
    while (1) {
        Update(SendSocket, RecvAddr, ReadFds, FdMax);
        Sleep(16);
    }

    //---------------------------------------------
    // When the application is finished sending,
    // close the socket.
    CloseSocket(SendSocket);
    
    //-----------------------------------------------
    // Clean up and quit.
    wprintf(L"Exiting.\n");
    WSACleanup();
    
    return 0;
}