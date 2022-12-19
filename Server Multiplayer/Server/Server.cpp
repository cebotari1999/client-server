#ifndef UNICODE
#define UNICODE
#endif

#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <winsock2.h>
#include <Ws2tcpip.h>
#include <stdio.h>
#include <fcntl.h>
#include <iterator>
#include <map>
#include <string>
#include <chrono>

#define BUFSIZE 1024

using namespace std;
using namespace chrono;

// Link with ws2_32.lib
#pragma comment(lib, "Ws2_32.lib")

class Client {
    public:
        sockaddr_in ClientAddr;
        time_point<system_clock>  LastMessage;
};


int InitializeWinsock(int& iResult, WSADATA& wsaData) {

    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != NO_ERROR) {
        wprintf(L"WSAStartup failed with error: %d\n", iResult);
        return 1;
    }
    return 0;
}

int CreateSocket(SOCKET& RecvSocket) {
    RecvSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    
    // 1  to enable non-blocking socket
    u_long mode = 1;
    ioctlsocket(RecvSocket, FIONBIO, &mode);

    if (RecvSocket == INVALID_SOCKET) {
        wprintf(L"socket failed with error %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    return 0;
}

int BindSocket(SOCKET& RecvSocket, sockaddr_in& RecvAddr, int Port) {
    //-------------------------------------------
    //Complete sockaddr structure and bind socket.
    RecvAddr.sin_family = AF_INET;
    RecvAddr.sin_port = htons(Port);
    RecvAddr.sin_addr.s_addr = INADDR_ANY;

    int Ret = bind(RecvSocket, (SOCKADDR*)&RecvAddr, sizeof(RecvAddr));
    if (Ret != 0) {
        wprintf(L"bind failed with error %d\n", WSAGetLastError());
        return 1;
    }

}

UINT8 ReceiveDatagrams(SOCKET& RecvSocket, SOCKADDR_IN& SenderAddr, int SenderAddrSize, map<pair<string, unsigned short>, Client>& clients) {
    char RecvBuf[BUFSIZE];
    char IP[16];
    UINT8 Type = 255;

    //-----------------------------------------------
    // Call the recvfrom function to receive datagrams
    // on the bound socket.
    memset(RecvBuf, 0, BUFSIZE);
    int Ret = recvfrom(RecvSocket, RecvBuf, BUFSIZE, 0, (struct sockaddr*)&SenderAddr, &SenderAddrSize);
    auto T2 = system_clock::now();

    if (Ret < 1) {
        wprintf(L"recvfrom failed with error %d", WSAGetLastError());
        return -1;
    }

    printf("Recive %s\n", RecvBuf);

    //-------------------------------------------------
    // Store the type of message and sender IP
    sscanf_s(RecvBuf, "%hhu", &Type);
    inet_ntop(SenderAddr.sin_family, &SenderAddr.sin_addr, IP, sizeof(IP));


    //-------------------------------------------------
    // If a new client connects to server, it is store
    // in clients list, else a client send PONG message
    // and server display in which time client answered.
    if (Type == 0) {
        inet_ntop(SenderAddr.sin_family, &SenderAddr.sin_addr, IP, sizeof(IP));
        
        Client client;
        client.ClientAddr.sin_family = SenderAddr.sin_family;
        client.ClientAddr.sin_port = SenderAddr.sin_port;
        Ret = inet_pton(AF_INET, IP, &client.ClientAddr.sin_addr.s_addr);

        if (Ret < 0) {
            wprintf(L"inet_pton failed with error: %ld\n", WSAGetLastError());
        }

        clients.insert({make_pair(IP, SenderAddr.sin_port), client});
    }
    else {
        auto T1 = clients.find(make_pair(IP, SenderAddr.sin_port))->second.LastMessage;
        

        auto Time = duration_cast<milliseconds>(T2 - T1);
        cout << IP << "  " << SenderAddr.sin_port << "  Client answer in " << Time.count() << " milliseconds." << endl;

    }
    return Type;
}

void BuildMessage(UINT8 Type, char* SendBuf) {
    //---------------------------------------
    // Build a message for specific type.
    switch (Type) {
        case 0:
            snprintf(SendBuf, BUFSIZE, "%hhu %s", 0, "Ok, you are connected!\n");
            break;
        case 1:
            snprintf(SendBuf, BUFSIZE, "%hhu %s", 1, "PING\n");
            break;
        default:
            snprintf(SendBuf, BUFSIZE, "%hhu %s", 2, "I recived your message.\n");
    }
}


int SendDatagrams(SOCKET& RecvSocket, SOCKADDR_IN& SenderAddr, char* SendBuf, map<pair<string, unsigned short>, Client>& clients) {
    //-----------------------------------------------
    // Call the sendto function to send datagrams
    // on the bound socket.
    char IP[16];
    UINT8 Type = 255;
    int Ret = sendto(RecvSocket, SendBuf, BUFSIZE, 0, (SOCKADDR*)&SenderAddr, sizeof(SenderAddr));

    if (Ret == -1) {
        wprintf(L"sendto failed with error: %d\n", WSAGetLastError());   
    }

    //----------------------------------------------
    // Find the client in list and set the time when
    // server send the message.
    inet_ntop(SenderAddr.sin_family, &SenderAddr.sin_addr, IP, sizeof(IP));
    clients[make_pair(IP, SenderAddr.sin_port)].LastMessage = system_clock::now();

    printf("Send\n");
    return Ret;
}



void Update(SOCKET& RecvSocket, fd_set& ReadFds, int FdMax, map<pair<string, unsigned short>, Client>& clients) {
    char SendBuf[BUFSIZE];
    UINT8 type = 255;

    struct sockaddr_in SenderAddr;
    int SenderAddrSize = sizeof(SenderAddr);

    //-----------------------------------
    // Clean the buffer.
    memset(SendBuf, 0, BUFSIZE);

    fd_set TmpFds; 
    FD_ZERO(&TmpFds);

    //---------------------------------------
    // Set maximum time for select waiting 
    // for messages.
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    TmpFds = ReadFds;
    int Ret = select(FdMax + 1, &TmpFds, NULL, NULL, &timeout);

    //---------------------------------------
    // Check if are messages on the socket 
    // and recive them. If client wants to 
    // connect, the server sends a confirmation
    // that client is connected.
    if (FD_ISSET(RecvSocket, &TmpFds) && Ret > 0) {
        type = ReceiveDatagrams(RecvSocket, SenderAddr, SenderAddrSize, clients);
       
        if (type == 0) {
            BuildMessage(type, SendBuf);
            SendDatagrams(RecvSocket, SenderAddr, SendBuf, clients);
        }
    }

   /* for (auto i = clients.begin(); i != clients.end(); i++) {
        cout << i->first.first << "  " << i->first.second  << "   " << i->second.ClientAddr.sin_port << "  "
            << duration_cast<seconds>(steady_clock::now() - i->second.LastMessage).count() << endl;

    }*/
   
    //--------------------------------------------
    // The server sends a PING message to the 
    // clients every 3 seconds
    for (auto i : clients) {
        if (duration_cast<seconds>(system_clock::now() - i.second.LastMessage).count() >= 3) {
            BuildMessage(1, SendBuf);
            int Ret = SendDatagrams(RecvSocket, i.second.ClientAddr, SendBuf, clients);
            
        }
    }
}

int main()
{
    int Ret = 0;

    WSADATA wsaData;

    SOCKET RecvSocket;
    struct sockaddr_in RecvAddr;

    unsigned short Port = 27015;

    fd_set ReadFds;
    int FdMax;
    FD_ZERO(&ReadFds);

    map<pair<string, unsigned short>, Client> Clients;
    
    //-----------------------------------------------
    // Initialize Winsock
    InitializeWinsock(Ret, wsaData);

    //-----------------------------------------------
    // Create a receiver socket to receive datagrams
    CreateSocket(RecvSocket);
  
    //-----------------------------------------------
    // Bind the socket to any address and the specified port.
    BindSocket(RecvSocket, RecvAddr, Port);

    //-----------------------------------------------
    // Add socket in file descritors set.
    FD_SET(RecvSocket, &ReadFds);
    FdMax = RecvSocket;

    //-----------------------------------------------
    //Server is ready to recive datagrams.
    wprintf(L"Waiting for datagrams...\n");

    while (1) {
        Update(RecvSocket, ReadFds, FdMax, Clients);
        Sleep(16);
    }

    //-----------------------------------------------
    // Close the socket when finished receiving datagrams
    wprintf(L"Finished receiving. Closing socket.\n");
    Ret = closesocket(RecvSocket);
    if (Ret == SOCKET_ERROR) {
        wprintf(L"closesocket failed with error %d\n", WSAGetLastError());
        return 1;
    }

    //-----------------------------------------------
    // Clean up and exit.
    wprintf(L"Exiting.\n");
    WSACleanup();
    return 0;
}
