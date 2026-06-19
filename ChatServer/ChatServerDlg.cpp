
// ChatServerDlg.cpp: 구현 파일
//

#include "pch.h"
#include "framework.h"
#include "ChatServer.h"
#include "ChatServerDlg.h"
#include "afxdialogex.h"

#include <iostream>
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//서버 및 클라이언트 소켓
SOCKET g_serverSock = INVALID_SOCKET;
SOCKET g_client1 = INVALID_SOCKET;
SOCKET g_client2 = INVALID_SOCKET;

//Thread 함수 선언
UINT ServerThread(LPVOID pParam);
UINT Client1ToClient2Thread(LPVOID pParam);
UINT Client2ToClient1Thread(LPVOID pParam);

// 응용 프로그램 정보에 사용되는 CAboutDlg 대화 상자입니다.
class CAboutDlg : public CDialogEx
{
public:
	CAboutDlg();

// 대화 상자 데이터입니다.
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ABOUTBOX };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 지원입니다.

// 구현입니다.
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialogEx(IDD_ABOUTBOX)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()


// CChatServerDlg 대화 상자


CChatServerDlg::CChatServerDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_CHATSERVER_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CChatServerDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CChatServerDlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
END_MESSAGE_MAP()


// CChatServerDlg 메시지 처리기

BOOL CChatServerDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// 시스템 메뉴에 "정보..." 메뉴 항목을 추가합니다.

	// IDM_ABOUTBOX는 시스템 명령 범위에 있어야 합니다.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != nullptr)
	{
		BOOL bNameValid;
		CString strAboutMenu;
		bNameValid = strAboutMenu.LoadString(IDS_ABOUTBOX);
		ASSERT(bNameValid);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// 이 대화 상자의 아이콘을 설정합니다.  응용 프로그램의 주 창이 대화 상자가 아닐 경우에는
	//  프레임워크가 이 작업을 자동으로 수행합니다.
	SetIcon(m_hIcon, TRUE);			// 큰 아이콘을 설정합니다.
	SetIcon(m_hIcon, FALSE);		// 작은 아이콘을 설정합니다.

	// TODO: 여기에 추가 초기화 작업을 추가합니다.
	//콘솔 창 생성
	AllocConsole();
	FILE* fp;
	freopen_s(&fp, "CONTOUT$", "w", stdout);
	freopen_s(&fp, "CONIN$", "r", stdin);
	std::cout << "Server Start" << std::endl;
	AfxBeginThread(ServerThread, NULL);
	return TRUE;  // 포커스를 컨트롤에 설정하지 않으면 TRUE를 반환합니다.
}

void CChatServerDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialogEx::OnSysCommand(nID, lParam);
	}
}

// 대화 상자에 최소화 단추를 추가할 경우 아이콘을 그리려면
//  아래 코드가 필요합니다.  문서/뷰 모델을 사용하는 MFC 애플리케이션의 경우에는
//  프레임워크에서 이 작업을 자동으로 수행합니다.

void CChatServerDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // 그리기를 위한 디바이스 컨텍스트입니다.

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// 클라이언트 사각형에서 아이콘을 가운데에 맞춥니다.
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// 아이콘을 그립니다.
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

// 사용자가 최소화된 창을 끄는 동안에 커서가 표시되도록 시스템에서
//  이 함수를 호출합니다.
HCURSOR CChatServerDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

//서버 실행
UINT ServerThread(LPVOID pParam);
{
	WSDATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
	g_serverSock = socket(AF_INET, SOCK_STREAM, 0);

	//서버 주소 설정
	SOCKADDR_IN serverAddr = {};
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(4000);
	serverAddr.sinaddr.s_addr = htonl(INADDR_ANY);

	//IP,PORT -> 서버 소켓 연결
	bind(g_serverSock, (SOCKADDR*)&serverAddr, sizeof(serverAddr));
	listen(g_serverSock, 2);

	std::cout << "Client1 Connect Waiting" << std::endl;
	g_client1 = accept(g_serverSock, NULL, NULL);
	std::cout << "Client1 Connect Success" << std::endl;

	std::cout << "Client2 Connect Waiting" << std::endl;
	g_client2 = accept(g_serverSock, NULL, NULL);
	std::cout << "Client2 Connect Success" << std::endl;

	std::cout << "Chatting Start" << std::endl;

	//Client1 -> Client2
	AfxBeginThread(Client1ToClient2Thread, NULL);

	//Client2 -> Client1
	AfxBeginThread(Client2ToClient1Thread, NULL);
	return 0;
}

//Client1 Message -> Client2
UINT Client1ToClient2Thread(LPVOID pParam)
{
	char buffer[1024];

	while (true)
	{
		int len = recv(g_client1, buffer, sizeof(buffer), -1, 0);
		if (len < 0)
		{
			break;
		}
		buffer[len] = '\0';
		std::cout << "Client1 -> Client2 : " << buffer << std::endl;
		send(g_client1, buffer, len, 0);
	}
	return 0;
}
