#undef UNICODE

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <iomanip>
#include "CInitSock.h"
#include "sha256.h"

using namespace std;

char* join(char *s1, char *s2);
char* MakeAPacket(int flag, int totalLen, int remainsLen, string digest, char *buff);
void PrintPercentage(vector<string> strings);
void SplitString(const string& s, vector<std::string>& v, const string& c);
bool SendFileName(char *msg, sockaddr_in addrRemote);

CInitSock theSock;
SOCKET s, sUDP;

int main(int argc, char **argv)
{
	// Validate the params
	if (argc != 3)
	{
		cout << "Usage: " << argv[0] << " <HOST> <LOCAL_UDP_PORT>." << endl;
		return 1;
	}

	s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sUDP = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (s==INVALID_SOCKET)
	{
		cout << "Failed TCP socket()" << endl;
		return 0;
	}
	if (sUDP == INVALID_SOCKET)
	{
		cout << "Failed UDP socket()" << endl;
		return 0;
	}
	sockaddr_in udpsin;
	udpsin.sin_family = AF_INET;
	udpsin.sin_port = htons(atoi(argv[2]));
	udpsin.sin_addr.S_un.S_addr = INADDR_ANY;
	// bind UDP socket to local
	if (::bind(sUDP, (sockaddr*)&udpsin, sizeof(udpsin)) == SOCKET_ERROR)
	{
		cout << "Failed bind() UDP Socket" << endl;
		return -1;
	}
	
	// set not blocking
	//unsigned long ul = 1;
	//ioctlsocket(sUDP, FIONBIO, &ul);
	sockaddr_in servAddr, servAddrUDP;
	servAddr.sin_family = AF_INET;
	servAddr.sin_port = htons(2121);
	servAddr.sin_addr.S_un.S_addr = inet_addr(argv[1]);
	servAddrUDP.sin_family = AF_INET;
	servAddrUDP.sin_port = htons(2020);
	servAddrUDP.sin_addr.S_un.S_addr = inet_addr(argv[1]);
	if (::connect(s, (sockaddr*)&servAddr, sizeof(servAddr)) == -1)
	{
		cout << "Failed connect()" << endl;
		return 0;
	}
	// Receive server file list and Print
	char listBuff[512];
	int recv = ::recv(s, listBuff, strlen(listBuff), 0);
	listBuff[recv] = '\0';
	cout << "Server File List" << endl;
	cout << listBuff << endl;

	// UDP client send filename first
	char filenameText[128];
	cout << "Please Input Filename: ";
	cin.getline(filenameText, strlen(filenameText));

	// Send filename using UDP
   	// while (!SendFileName(filenameText, servAddrUDP)){}
	// cout << "Server Received File request" << endl;

	// Send filename using TCP (with Client UDP Port)
	sockaddr_in addrLocal;
	int addrLen = sizeof(addrLocal);
	getsockname(sUDP, (struct sockaddr*)&addrLocal, &addrLen);
	int pUDP = htons(addrLocal.sin_port);
	char pBuff[20];
	_itoa(pUDP, pBuff, 10);
	string filenameStr(filenameText);
	string portStr(pBuff);
	string toSendStr = filenameStr + "|" + portStr;
	char *toSend = (char *)toSendStr.c_str();
	send(s, toSend, strlen(toSend), 0);

	// prepare to restore file
	char buff[5200] = { 0 };
	string newFilename = "restored_" + (string)filenameText;
	fstream fs;
	fs.open(newFilename, ios::out | ios::binary);
	// Receive data
	for (;;)
	{
		int nRecv = recvfrom(sUDP, buff, sizeof(buff), 0, NULL, NULL);
		if (nRecv > 0)
		{
			buff[nRecv] = '\0';
			// Handle data
			string data = string(buff);
			vector<string> strings;
			SplitString(data, strings, "-|-");

			// is Not Found?
			if (strings[0].compare("-1") == 0)
			{
				// Received message, Tell server
				char *packet = MakeAPacket(0, 0, 0, "", (char*)strings[0].c_str());
				::sendto(sUDP, packet, strlen(packet), 0, (struct sockaddr *)&servAddrUDP, sizeof(servAddrUDP));
				cout << "Server File Not Found" << endl;
				return 0;
			}

			char *toWrite = (char *)"";
			// check digest
			if (strings.size() == 5)
			{
				toWrite = (char *)strings[4].c_str();
				if (strings[3].compare(sha256(string(toWrite))) != 0)
				{
					cout << "Received UDP packet, but wrong packet" << endl;
					continue;
				}
			}
			
			// Received Correct data, Tell server index
			char *packet = MakeAPacket(0, 0, 0, "", (char *)strings[0].c_str());
			::sendto(sUDP, packet, strlen(packet), 0, (struct sockaddr *)&servAddrUDP, sizeof(servAddrUDP));

			cout << "Received UDP packet #" << strings[0] << " Transport Complete ";
			
			// Write file
			fs.write(toWrite, strlen(toWrite));
			// Print Persentage
			 PrintPercentage(strings);
			// File End?
			if (strings[2].compare("0") == 0)
			{
				fs.close();
				
				break;
			}
		}
		
		
	}
	cout << "File '" << filenameText << "' has been restored as '" << newFilename << "'" << endl;

	::closesocket(sUDP);
	::closesocket(s);
	return 0;


}

bool SendFileName(char *msg, sockaddr_in addrRemote)
{
	::sendto(sUDP, msg, strlen(msg), 0, (sockaddr *)&addrRemote, sizeof(addrRemote));
	char buff[128] = { 0 };
	int nRecv = recvfrom(sUDP, buff, sizeof(buff), 0, NULL, NULL);
	if (nRecv <= 0)
	{
		cout << "Failed Send() filename" << endl;
		return false;
	}
	else
	{
		// Handle server result
		if (strcmp(buff, "RECEIVED FILENAME") == 0)
		{
			return true;
		}
		else
		{
			return false;
		}
	}
}

void SplitString(const string& s, vector<string>& v, const string& c)
{
	string::size_type pos1, pos2;
	pos2 = s.find(c);
	pos1 = 0;
	while (string::npos != pos2)
	{
		v.push_back(s.substr(pos1, pos2 - pos1));

		pos1 = pos2 + c.size();
		pos2 = s.find(c, pos1);
	}
	if (pos1 != s.length())
		v.push_back(s.substr(pos1));
}

void PrintPercentage(vector<string> strings)
{
	// Calculate task percent
	stringstream ss0(strings[0]);
	int index = 0;
	ss0 >> index;

	stringstream ss1(strings[1]);
	int total = 0;
	ss1 >> total;

	stringstream ss2(strings[2]);
	int remains = 0;
	ss2 >> remains;

	float x = (total - remains)*1.0 / total*1.0;
	x *= 100;
	cout << fixed << setprecision(1) << x << '%' << endl;
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