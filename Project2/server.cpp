#include <stdio.h>
#include <stdlib.h>
#include <WinSock2.h>
#include <process.h>

#pragma comment(lib,"ws2_32")

#define PORT 6100
#define BUFSIZE 128

using namespace std;

/*
무조건 IOCP를 이용해 구현해보기로 하자.

메인서버가 할 일.
1. 클라이언트의 접속을 받는다.
2. 클라이언트에서 매칭을 요청하면 따로 매칭큐를 돌려서 적합한 상대를 찾아준다.
3. 클라이언트가 접속을 끊는걸 확인한다.
*/

typedef struct {// 소켓 정보를 구조체화
	SOCKET hClntSock;
	SOCKADDR_IN clntAddr;
}PER_HANDLE_DATA, *LPPER_HANDLE_DATA;

typedef struct {// 소켓의 버퍼 정보를 구조체화
	OVERLAPPED overlapped;
	char buffer[BUFSIZE];
	WSABUF wsaBuf;
}PER_IO_DATA, *LPPER_IO_DATA;

void AlertError(const char *msg);

//입출력이 완료될 때 처리될 스레드
unsigned WINAPI CompletionThread(LPVOID pComPort);

int main() {
	WSADATA wsaData;
	SOCKET servSock;
	SOCKADDR_IN servAddr;

	HANDLE hCompletionPort;
	SYSTEM_INFO sysInfo;

	LPPER_IO_DATA PerIoData;
	LPPER_HANDLE_DATA PerHandleData;

	DWORD recvBytes;
	DWORD flags;

	int check_res = 0;

	check_res = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (check_res != 0) AlertError("WSAStartup() Error!");

	// 기본적인 설정들
	ZeroMemory(&servAddr, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servAddr.sin_port = htons(PORT);

	servSock = WSASocket(PF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (servSock == INVALID_SOCKET)AlertError("socket() Error!");

	check_res = bind(servSock, (SOCKADDR*)&servAddr, sizeof(servAddr));
	if (check_res == SOCKET_ERROR) AlertError("bind() Error!");

	check_res = listen(servSock, 5);
	if (check_res == SOCKET_ERROR) AlertError("listen() Error!");

	hCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);// 이렇게 입력할 경우 새로운 포트를 할당해준다.
	GetSystemInfo(&sysInfo);// 시스템 정보 받아오기

	for (int i = 0; i < sysInfo.dwNumberOfProcessors; ++i)
		_beginthreadex(
			NULL,
			0,
			CompletionThread,
			(LPVOID)hCompletionPort,
			0,
			NULL);

	while (1) {
		//새로운 클라이언트가 연결될 때마다 새로 소켓을 할당시켜줘야 하기 때문에 계속 만들어서 쓰는 듯하다.
		SOCKET hClntSock;
		SOCKADDR_IN clntAddr;

		int addrLen = sizeof(clntAddr);
		hClntSock = accept(servSock, (SOCKADDR*)&clntAddr, &addrLen);
		if (hClntSock == SOCKET_ERROR) AlertError("accept() Error!");

		PerHandleData = (LPPER_HANDLE_DATA) new PER_HANDLE_DATA;
		PerHandleData->hClntSock = hClntSock;
		memcpy(&(PerHandleData->clntAddr), &clntAddr, addrLen);


		CreateIoCompletionPort(
			(HANDLE)hClntSock,		// 연결하고자 하는 overlapped형태의 소켓
			hCompletionPort,		// 이미 생성했었던 CompletionPort
			(DWORD)PerHandleData,	// 포트에 연결되는 소켓에 관련된 정보
			0);						// 스레드의 개수. 0이면 cpu의 수와 일치하게 됨

		//이 과정을 통해 소켓버퍼의 정보를 가진 구조체를 만들었다.		
		PerIoData = (LPPER_IO_DATA)new PER_IO_DATA;
		ZeroMemory(&(PerIoData->overlapped), sizeof(PerIoData->overlapped));
		PerIoData->wsaBuf.len = BUFSIZE;
		PerIoData->wsaBuf.buf = PerIoData->buffer;

		flags = 0;

		//에코 서버의 경우 여기서 WSARecv를 호출해 데이터를 받는다. 
		WSARecv(
			PerHandleData->hClntSock,	// 데이터 입력 소켓
			&(PerIoData->wsaBuf),		// 데이터 버퍼 포인터
			1,							// 버퍼 개수
			(LPDWORD)&recvBytes,		// 
			(LPDWORD)&flags,
			&(PerIoData->overlapped),
			NULL);
	}
	WSACleanup();
	return 0;
}

// 잘못된 행동이 나올 경우 에러 메시지를 띄운다. 
// 많은 예외처리를 통해 이 함수가 실행되지 않도록 해야 서버가 죽지 않는다.
void AlertError(const char *msg) {
	fputs(msg, stderr);
	fputc('\n', stderr);
	exit(1);
}

// 미리 cpu의 개수에 맞춰 스레드를 생성한다.
// 이후에 입출력이 다 된 소켓이 있는지를 스레드가 확인하는 코드로 구성한다.
// 다수의 클라이언트가 접속해도 스레드들끼리 소켓들을 검사해서 중첩된 입력이 완료될 경우 각 입력에 따라 행동을 취한다.
// 따라서 여럿이 나눠서 일을 하기 때문에 부하가 적다.
unsigned WINAPI CompletionThread(LPVOID pComport) {
	HANDLE hCompletionPort = (HANDLE)pComport;
	DWORD BytesTransferred;
	LPPER_HANDLE_DATA PerHandleData;
	LPPER_IO_DATA PerIoData;
	DWORD flags;

	while (1) {
		// hCompletionPort는 main함수 안에서 중첩소켓으로 생성되었다.
		// 이곳에 연결된 소켓중에 입출력을 완료한 소켓이 있다면 리턴하는 함수.
		GetQueuedCompletionStatus(
			hCompletionPort,			// CompletionPort
			&BytesTransferred,			// 받은 데이터의 크기
			(LPDWORD)&PerHandleData,	// 포트에 등록했던 PerHandleData 구조체
			(LPOVERLAPPED*)&PerIoData,	// Recv나 Send에서 사용했던 overlapped 구조체
			INFINITE);

		if (BytesTransferred == 0) { // EOF 상황
			closesocket(PerHandleData->hClntSock);
			delete PerHandleData;
			delete PerIoData;
			continue;
		}

		// 에코 서버의 경우 먼저 받아오기 때문에 받아온 메시지를 출력
		PerIoData->wsaBuf.buf[BytesTransferred] = '\0';
		printf("Recv [%s]\n", PerIoData->wsaBuf.buf);
		// 이후에 클라이언트로 에코한다.
		PerIoData->wsaBuf.len = BytesTransferred;
		WSASend(
			PerHandleData->hClntSock,
			&(PerIoData->wsaBuf),
			1,
			NULL,
			0,
			NULL,
			NULL);

		// RECEIVE AGAIN 
		// 왜 한번 더 받는걸까
		// 그럼 main함수 안에 있는 WSARecv는 뭘까
		ZeroMemory(&(PerIoData->overlapped), sizeof(OVERLAPPED));
		PerIoData->wsaBuf.len = BUFSIZE - 1;
		PerIoData->wsaBuf.buf = PerIoData->buffer;
		flags = 0;
		WSARecv(
			PerHandleData->hClntSock,
			&(PerIoData->wsaBuf),
			1,
			NULL,
			&flags,
			&(PerIoData->overlapped),
			NULL);
	}
	return 0;
}
