#undef UNICODE

#define WIN32_LEAN_AND_MEAN
#define FILE_BUFFER_SIZE 4096

#include <windows.h>
#include <Windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include "CInitSock.h"
#include "sha256.h"

using namespace std;

vector<string> get_all_files_names_within_folder(string folder);
void SplitString(const std::string& s, std::vector<std::string>& v, const std::string& c);
char* MakeAPacket(int flag, int totalLen, int remainsLen, string digest, char *buff);
bool Send(char *msg, sockaddr_in addrRemote, int packetIndex);
DWORD WINAPI SendFileFunc(LPVOID n);


CInitSock theSock;
SOCKET sListen, sListenUDP;

struct FileRequest
{
	char filename[128];
	sockaddr_in addrRemote;
};


int main()
{
	USHORT nPort = 2121;
	USHORT nPortUDP = 2020;
	sListen = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sListenUDP = ::socket(AF_INET, SOCK_DGRAM, 0);
	sockaddr_in sin, sinUDP;
	sin.sin_family = AF_INET;
	sin.sin_port = htons(nPort);
	sin.sin_addr.S_un.S_addr = INADDR_ANY;
	sinUDP.sin_family = AF_INET;
	sinUDP.sin_port = htons(nPortUDP);
	sinUDP.sin_addr.S_un.S_addr = INADDR_ANY;
	// bind TCP socket to local
	if (::bind(sListen, (sockaddr*)&sin, sizeof(sin))==SOCKET_ERROR)
	{
		cout << "Failed bind() TCP Socket" << endl;
		return -1;
	}
	// bind UDP socket to local
	if (::bind(sListenUDP, (sockaddr*)&sinUDP, sizeof(sinUDP)) == SOCKET_ERROR)
	{
		cout << "Failed bind() UDP Socket" << endl;
		return -1;
	}

	// TCP socket to listen, backlog is 5
	::listen(sListen, 5);
	/*
		select i/o
	*/
	// init a fdSocket set(maintains all avalible socket)
	// add listen socket handle to this set
	fd_set fdSocket;
	FD_ZERO(&fdSocket);
	FD_SET(sListen, &fdSocket);
	for (;;)
	{
		// send fdRead(a copy of fdSocket) to select()
		fd_set fdRead = fdSocket;
		int nRet = ::select(0, &fdRead, NULL, NULL, NULL);
		if (nRet > 0)
		{
			for (int i = 0;i < (int)fdSocket.fd_count;i++)
			{
				if (FD_ISSET(fdSocket.fd_array[i], &fdRead))
				{
					if (fdSocket.fd_array[i] == sListen)
					{
						if (fdSocket.fd_count < FD_SETSIZE)
						{
							sockaddr_in addrRemote;
							int nAddrLen = sizeof(addrRemote);
							SOCKET sNew = ::accept(sListen, (SOCKADDR*)&addrRemote, &nAddrLen);
							if (sNew == INVALID_SOCKET)
							{
								::cout << "Failed to accept(), stop server..." << endl;
								// close all socket
								for (int j = 0; j < fdSocket.fd_count; j++)
								{
									closesocket(fdSocket.fd_array[j]);
									WSACleanup();
									return 0;
								}
							}
							FD_SET(sNew, &fdSocket);
							cout << "Accept connection " << ::inet_ntoa(addrRemote.sin_addr) << ":" << htons(addrRemote.sin_port) << endl;
							// send file list
							vector<string> files = get_all_files_names_within_folder(".");
							string list;
							for (i = 0; i < files.size(); i++)
							{
								list.append(files[i]);
								list.append("\n");
							}
							char *szText = (char *)list.c_str();
							int nSend = ::send(sNew, szText, strlen(szText), 0);
							
						}
						else
						{
							cout << "Too much connections" << endl;
							continue;
						}
					}
					else
					{
						// Recieve filename from client
						char filenameAndPort[256];
						int nRecv = ::recv(fdSocket.fd_array[i], filenameAndPort, strlen(filenameAndPort), 0);
						if (nRecv > 0)
						{
							filenameAndPort[nRecv] = '\0';
							vector<string> strings;
							SplitString(string(filenameAndPort), strings, "|");
							if (strings.size() != 2)
							{
								cout << "Server Error, please try again :"<< filenameAndPort << endl;
								return -1;
							}
							string filename = strings[0];
							string clientUDPPort = strings[1];
							cout << "Receive file request '" << filename << "' " << endl;

							// Send File using UDP
							sockaddr_in addrRemote, addrRemoteTCP;
							int addrTCPLen = sizeof(addrRemoteTCP);
							getsockname(fdSocket.fd_array[i], (struct sockaddr *)&addrRemoteTCP, &addrTCPLen);
							addrRemote.sin_family = AF_INET;
							addrRemote.sin_port = htons(stoi(clientUDPPort));
							addrRemote.sin_addr = addrRemoteTCP.sin_addr;

							// Start a thread to send that file
							HANDLE hThread;
							DWORD threadID;
							FileRequest req;
							strcpy(req.filename, filename.c_str());
							
							req.addrRemote = addrRemote;
							hThread = CreateThread(NULL, 0, SendFileFunc, (LPVOID)&req, 0, &threadID);
					
						}
						else
						{
							::closesocket(fdSocket.fd_array[i]);
							FD_CLR(fdSocket.fd_array[i], &fdSocket);
						}
					}
				}
			}
		}
		else
		{
			cout << "Failed select()" << endl;
			break;
		}
	}
	return 0;

}

DWORD WINAPI SendFileFunc(LPVOID n)
{
	FileRequest *req = (FileRequest *)n;
	sockaddr_in addrRemote;
	char *filename;
	filename = req->filename;
	addrRemote = req->addrRemote;
	// open file
	ifstream inFile((char *)filename, ios::in | ios::binary);
	int fileLen;
	inFile.seekg(0, ios::end);
	fileLen = inFile.tellg();
	inFile.seekg(0, ios::beg);
	if (!inFile)
	{
		//Tell client error
		char message[] = "File Not Found";
		char *packet = MakeAPacket(-1, 0, 0, "", message);
		while (!Send(packet, addrRemote, -1)){}
		cout << "Cannot open " << filename << endl;
	}
	else
	{
		cout << "File '" << filename << "' Total Length " << fileLen << endl;
		BYTE fileBuffer[4097] = { 0 };
		int packetIndex = 0;
		int totalLen = fileLen;
		while (fileLen >= 4096)
		{
			// cout << "Loop send, " << fileLen << " to be sent" << endl;
			inFile.read((char *)fileBuffer, 4096);
			fileBuffer[4096] = '\0';
			fileLen -= 4096;
			// Make a Packet with my protocol
			string digest = sha256(string((char *)fileBuffer));
			char *packet = MakeAPacket(++packetIndex, totalLen, fileLen, digest, (char *)fileBuffer);
			// Do Send
			while (!Send((char *)packet, addrRemote, packetIndex)){}
			
		}
		// cout << "Out of loop send, " << fileLen << " to be sent" << endl;
		inFile.read((char *)fileBuffer, fileLen);
		fileBuffer[fileLen] = '\0';
		fileLen = 0;
		// Make a Packet with my protocol
		string digest = sha256(string((char *)fileBuffer));
		char *packet = MakeAPacket(++packetIndex, totalLen, fileLen, digest, (char *)fileBuffer);
		// Do Send
		while (!Send((char *)packet, addrRemote, packetIndex)) {}

		
	}

}

/*
	Send file package and NOT FOUND Msg
*/
bool Send(char *msg, sockaddr_in addrRemote, int packetIndex)
{
	::sendto(sListenUDP, msg, strlen(msg), 0, (sockaddr *)&addrRemote, sizeof(addrRemote));
	char buff[128] = { 0 };
	int size = sizeof(addrRemote);
	int nRecv = recvfrom(sListenUDP, buff, sizeof(buff), 0, (sockaddr *)&addrRemote, &size);
	if (nRecv <= 0)
	{
		cout << "Failed Send()" << endl;
		return false;
	}
	else
	{
		// Handle data
		string data = string(buff);
		vector<string> strings;
		SplitString(data, strings, "-|-");
		// is ACK of Not Found(index==-1)
		// Or ACK of packet data
		
		if (strings[0].compare("0") == 0 && strings[4].compare(to_string(packetIndex)) == 0)
		{
			cout << "Successful Send packet #" << packetIndex << " and Received ACK" << endl;
			return true;
		}
		else
		{
			return false;
		}
	}
}

char* join(char *s1, char *s2)
{
	char *result = (char *)malloc(strlen(s1) + strlen(s2) + 1);//+1 for the zero-terminator
	if (result == NULL) exit(1);

	strcpy(result, s1);
	strcat(result, s2);

	return result;
}

char* MakeAPacket(int flag, int totalLen, int remainsLen, string digest, char *buff)
{
	
	string head;
	head.append(to_string(flag));
	head.append("-|-");
	head.append(to_string(totalLen));
	head.append("-|-");
	head.append(to_string(remainsLen));
	head.append("-|-");
	head.append(digest);
	head.append("-|-");
	return join((char *)head.c_str(), buff);
	

}

void SplitString(const std::string& s, std::vector<std::string>& v, const std::string& c)
{
	std::string::size_type pos1, pos2;
	pos2 = s.find(c);
	pos1 = 0;
	while (std::string::npos != pos2)
	{
		v.push_back(s.substr(pos1, pos2 - pos1));

		pos1 = pos2 + c.size();
		pos2 = s.find(c, pos1);
	}
	if (pos1 != s.length())
		v.push_back(s.substr(pos1));
}


vector<string> get_all_files_names_within_folder(string folder)
{
	vector<string> names;
	string search_path = folder + "/*.*";
	WIN32_FIND_DATA fd;
	HANDLE hFind = ::FindFirstFile(search_path.c_str(), &fd);
	if (hFind != INVALID_HANDLE_VALUE) {
		do {
			// read all (real) files in current folder
			// , delete '!' read other 2 default folder . and ..
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
				names.push_back(fd.cFileName);
			}
		} while (::FindNextFile(hFind, &fd));
		::FindClose(hFind);
	}
	return names;
}

