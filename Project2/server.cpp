#include <stdio.h>
#include <stdlib.h>
#include <WinSock2.h>
#include <process.h>

#pragma comment(lib,"ws2_32")

#define PORT 6100
#define BUFSIZE 128

using namespace std;

/*
������ IOCP�� �̿��� �����غ���� ����.

���μ����� �� ��.
1. Ŭ���̾�Ʈ�� ������ �޴´�.
2. Ŭ���̾�Ʈ���� ��Ī�� ��û�ϸ� ���� ��Īť�� ������ ������ ��븦 ã���ش�.
3. Ŭ���̾�Ʈ�� ������ ���°� Ȯ���Ѵ�.
*/

typedef struct {// ���� ������ ����üȭ
	SOCKET hClntSock;
	SOCKADDR_IN clntAddr;
}PER_HANDLE_DATA, *LPPER_HANDLE_DATA;

typedef struct {// ������ ���� ������ ����üȭ
	OVERLAPPED overlapped;
	char buffer[BUFSIZE];
	WSABUF wsaBuf;
}PER_IO_DATA, *LPPER_IO_DATA;

void AlertError(const char *msg);

//������� �Ϸ�� �� ó���� ������
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

	// �⺻���� ������
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

	hCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);// �̷��� �Է��� ��� ���ο� ��Ʈ�� �Ҵ����ش�.
	GetSystemInfo(&sysInfo);// �ý��� ���� �޾ƿ���

	for (int i = 0; i < sysInfo.dwNumberOfProcessors; ++i)
		_beginthreadex(
			NULL,
			0,
			CompletionThread,
			(LPVOID)hCompletionPort,
			0,
			NULL);

	while (1) {
		//���ο� Ŭ���̾�Ʈ�� ����� ������ ���� ������ �Ҵ������� �ϱ� ������ ��� ���� ���� ���ϴ�.
		SOCKET hClntSock;
		SOCKADDR_IN clntAddr;

		int addrLen = sizeof(clntAddr);
		hClntSock = accept(servSock, (SOCKADDR*)&clntAddr, &addrLen);
		if (hClntSock == SOCKET_ERROR) AlertError("accept() Error!");

		PerHandleData = (LPPER_HANDLE_DATA) new PER_HANDLE_DATA;
		PerHandleData->hClntSock = hClntSock;
		memcpy(&(PerHandleData->clntAddr), &clntAddr, addrLen);


		CreateIoCompletionPort(
			(HANDLE)hClntSock,		// �����ϰ��� �ϴ� overlapped������ ����
			hCompletionPort,		// �̹� �����߾��� CompletionPort
			(DWORD)PerHandleData,	// ��Ʈ�� ����Ǵ� ���Ͽ� ���õ� ����
			0);						// �������� ����. 0�̸� cpu�� ���� ��ġ�ϰ� ��

		//�� ������ ���� ���Ϲ����� ������ ���� ����ü�� �������.		
		PerIoData = (LPPER_IO_DATA)new PER_IO_DATA;
		ZeroMemory(&(PerIoData->overlapped), sizeof(PerIoData->overlapped));
		PerIoData->wsaBuf.len = BUFSIZE;
		PerIoData->wsaBuf.buf = PerIoData->buffer;

		flags = 0;

		//���� ������ ��� ���⼭ WSARecv�� ȣ���� �����͸� �޴´�. 
		WSARecv(
			PerHandleData->hClntSock,	// ������ �Է� ����
			&(PerIoData->wsaBuf),		// ������ ���� ������
			1,							// ���� ����
			(LPDWORD)&recvBytes,		// 
			(LPDWORD)&flags,
			&(PerIoData->overlapped),
			NULL);
	}
	WSACleanup();
	return 0;
}

// �߸��� �ൿ�� ���� ��� ���� �޽����� ����. 
// ���� ����ó���� ���� �� �Լ��� ������� �ʵ��� �ؾ� ������ ���� �ʴ´�.
void AlertError(const char *msg) {
	fputs(msg, stderr);
	fputc('\n', stderr);
	exit(1);
}

// �̸� cpu�� ������ ���� �����带 �����Ѵ�.
// ���Ŀ� ������� �� �� ������ �ִ����� �����尡 Ȯ���ϴ� �ڵ�� �����Ѵ�.
// �ټ��� Ŭ���̾�Ʈ�� �����ص� ������鳢�� ���ϵ��� �˻��ؼ� ��ø�� �Է��� �Ϸ�� ��� �� �Է¿� ���� �ൿ�� ���Ѵ�.
// ���� ������ ������ ���� �ϱ� ������ ���ϰ� ����.
unsigned WINAPI CompletionThread(LPVOID pComport) {
	HANDLE hCompletionPort = (HANDLE)pComport;
	DWORD BytesTransferred;
	LPPER_HANDLE_DATA PerHandleData;
	LPPER_IO_DATA PerIoData;
	DWORD flags;

	while (1) {
		// hCompletionPort�� main�Լ� �ȿ��� ��ø�������� �����Ǿ���.
		// �̰��� ����� �����߿� ������� �Ϸ��� ������ �ִٸ� �����ϴ� �Լ�.
		GetQueuedCompletionStatus(
			hCompletionPort,			// CompletionPort
			&BytesTransferred,			// ���� �������� ũ��
			(LPDWORD)&PerHandleData,	// ��Ʈ�� ����ߴ� PerHandleData ����ü
			(LPOVERLAPPED*)&PerIoData,	// Recv�� Send���� ����ߴ� overlapped ����ü
			INFINITE);

		if (BytesTransferred == 0) { // EOF ��Ȳ
			closesocket(PerHandleData->hClntSock);
			delete PerHandleData;
			delete PerIoData;
			continue;
		}

		// ���� ������ ��� ���� �޾ƿ��� ������ �޾ƿ� �޽����� ���
		PerIoData->wsaBuf.buf[BytesTransferred] = '\0';
		printf("Recv [%s]\n", PerIoData->wsaBuf.buf);
		// ���Ŀ� Ŭ���̾�Ʈ�� �����Ѵ�.
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
		// �� �ѹ� �� �޴°ɱ�
		// �׷� main�Լ� �ȿ� �ִ� WSARecv�� ����
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
