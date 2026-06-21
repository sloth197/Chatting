
// ChatServerDlg.cpp: 구현 파일
//

#include "pch.h"
#include "framework.h"
#include "ChatServer.h"
#include "ChatServerDlg.h"
#include "afxdialogex.h"

#include <string>

#pragma comment(lib, "ws2_32.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

const UINT WM_ADD_LOG = WM_USER + 100;

//서버 및 클라이언트 소켓
SOCKET g_serverSock = INVALID_SOCKET;
SOCKET g_client1 = INVALID_SOCKET;
SOCKET g_client2 = INVALID_SOCKET;
HWND g_hServerWnd = NULL;
bool g_serverStarted = false;
bool g_wsaStarted = false;

//Thread 함수 선언
UINT ServerThread(LPVOID pParam);
UINT Client1ToClient2Thread(LPVOID pParam);
UINT Client2ToClient1Thread(LPVOID pParam);

std::string CStringToUtf8(const CString& text)
{
	int byteCount = WideCharToMultiByte(CP_UTF8, 0, text.GetString(), -1, NULL, 0, NULL, NULL);
	if (byteCount <= 1)
	{
		return std::string();
	}

	std::string result(byteCount, '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.GetString(), -1, &result[0], byteCount, NULL, NULL);
	result.resize(byteCount - 1);
	return result;
}

CString Utf8ToCString(const char* data, int length)
{
	CString result;
	int charCount = MultiByteToWideChar(CP_UTF8, 0, data, length, NULL, 0);
	if (charCount <= 0)
	{
		return result;
	}

	wchar_t* buffer = result.GetBuffer(charCount);
	MultiByteToWideChar(CP_UTF8, 0, data, length, buffer, charCount);
	result.ReleaseBuffer(charCount);
	return result;
}

void PostServerLog(const CString& text)
{
	if (::IsWindow(g_hServerWnd))
	{
		::PostMessage(g_hServerWnd, WM_ADD_LOG, 0, (LPARAM)new CString(text));
	}
}

bool SendToClient(SOCKET clientSock, const CString& message)
{
	if (clientSock == INVALID_SOCKET)
	{
		return false;
	}

	std::string sendData = CStringToUtf8(message);
	if (sendData.empty())
	{
		return false;
	}

	return send(clientSock, sendData.c_str(), (int)sendData.size(), 0) != SOCKET_ERROR;
}

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
	ON_BN_CLICKED(IDC_BUTTON_START, &CChatServerDlg::OnBnClickedButtonStart)
	ON_BN_CLICKED(IDC_BUTTON_SEND, &CChatServerDlg::OnBnClickedButtonSend)
	ON_WM_DESTROY()
	ON_MESSAGE(WM_ADD_LOG, &CChatServerDlg::OnAddLog)
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
	g_hServerWnd = m_hWnd;
	AddLog(_T("[서버 시작] 버튼을 누르면 4000번 포트에서 대기합니다."));
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
UINT ServerThread(LPVOID pParam)
{
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		PostServerLog(_T("WSAStartup 실패"));
		g_serverStarted = false;
		return 0;
	}
	g_wsaStarted = true;

	g_serverSock = socket(AF_INET, SOCK_STREAM, 0);
	if (g_serverSock == INVALID_SOCKET)
	{
		PostServerLog(_T("서버 소켓 생성 실패"));
		g_serverStarted = false;
		return 0;
	}

	BOOL reuseAddr = TRUE;
	setsockopt(g_serverSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuseAddr, sizeof(reuseAddr));

	//서버 주소 설정
	SOCKADDR_IN serverAddr = {};
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(4000);
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

	//IP,PORT -> 서버 소켓 연결
	if (bind(g_serverSock, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
	{
		PostServerLog(_T("bind 실패: 4000번 포트를 사용할 수 없습니다."));
		g_serverStarted = false;
		return 0;
	}

	if (listen(g_serverSock, 2) == SOCKET_ERROR)
	{
		PostServerLog(_T("listen 실패"));
		g_serverStarted = false;
		return 0;
	}

	PostServerLog(_T("Client1 접속 대기 중..."));
	g_client1 = accept(g_serverSock, NULL, NULL);
	if (g_client1 == INVALID_SOCKET)
	{
		PostServerLog(_T("Client1 accept 실패"));
		g_serverStarted = false;
		return 0;
	}
	PostServerLog(_T("Client1 접속 성공"));

	PostServerLog(_T("Client2 접속 대기 중..."));
	g_client2 = accept(g_serverSock, NULL, NULL);
	if (g_client2 == INVALID_SOCKET)
	{
		PostServerLog(_T("Client2 accept 실패"));
		g_serverStarted = false;
		return 0;
	}
	PostServerLog(_T("Client2 접속 성공"));

	PostServerLog(_T("채팅 시작"));

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

	while (g_serverStarted)
	{
		int len = recv(g_client1, buffer, sizeof(buffer), 0);
		if (len <= 0)
		{
			break;
		}

		CString message = Utf8ToCString(buffer, len);
		CString log;
		log.Format(_T("Client1 -> Client2: %s"), message.GetString());
		PostServerLog(log);

		SendToClient(g_client2, _T("[Client1] ") + message);
	}
	PostServerLog(_T("Client1 연결 종료"));
	return 0;
}

//Client2 Message -> Client1
UINT Client2ToClient1Thread(LPVOID pParam)
{
	char buffer[1024];

	while (g_serverStarted)
	{
		int len = recv(g_client2, buffer, sizeof(buffer), 0);
		if (len <= 0)
		{
			break;
		}

		CString message = Utf8ToCString(buffer, len);
		CString log;
		log.Format(_T("Client2 -> Client1: %s"), message.GetString());
		PostServerLog(log);

		SendToClient(g_client1, _T("[Client2] ") + message);
	}
	PostServerLog(_T("Client2 연결 종료"));
	return 0;
}

void CChatServerDlg::OnBnClickedButtonStart()
{
	if (g_serverStarted)
	{
		AddLog(_T("서버가 이미 실행 중입니다."));
		return;
	}

	g_serverStarted = true;
	GetDlgItem(IDC_BUTTON_START)->EnableWindow(FALSE);
	AddLog(_T("서버 시작 중..."));
	AfxBeginThread(ServerThread, NULL);
}

void CChatServerDlg::OnBnClickedButtonSend()
{
	CString message;
	GetDlgItemText(IDC_EDIT_MESSAGE, message);
	message.Trim();

	if (message.IsEmpty())
	{
		return;
	}

	CString sendMessage = _T("[Server] ") + message;
	bool sent = false;
	sent = SendToClient(g_client1, sendMessage) || sent;
	sent = SendToClient(g_client2, sendMessage) || sent;

	if (sent)
	{
		AddLog(_T("Server -> Client: ") + message);
		SetDlgItemText(IDC_EDIT_MESSAGE, _T(""));
	}
	else
	{
		AddLog(_T("연결된 클라이언트가 없습니다."));
	}
}

void CChatServerDlg::OnOK()
{
	OnBnClickedButtonSend();
}

void CChatServerDlg::OnCancel()
{
	StopServer();
	CDialogEx::OnCancel();
}

void CChatServerDlg::OnDestroy()
{
	StopServer();
	CDialogEx::OnDestroy();
}

LRESULT CChatServerDlg::OnAddLog(WPARAM wParam, LPARAM lParam)
{
	CString* text = (CString*)lParam;
	if (text != NULL)
	{
		AddLog(*text);
		delete text;
	}
	return 0;
}

void CChatServerDlg::AddLog(const CString& text)
{
	CEdit* logEdit = (CEdit*)GetDlgItem(IDC_EDIT_LOG);
	if (logEdit == NULL)
	{
		return;
	}

	CString oldText;
	logEdit->GetWindowText(oldText);
	oldText += text;
	oldText += _T("\r\n");
	logEdit->SetWindowText(oldText);

	int length = logEdit->GetWindowTextLength();
	logEdit->SetSel(length, length);
}

void CChatServerDlg::StopServer()
{
	g_serverStarted = false;

	if (g_client1 != INVALID_SOCKET)
	{
		closesocket(g_client1);
		g_client1 = INVALID_SOCKET;
	}

	if (g_client2 != INVALID_SOCKET)
	{
		closesocket(g_client2);
		g_client2 = INVALID_SOCKET;
	}

	if (g_serverSock != INVALID_SOCKET)
	{
		closesocket(g_serverSock);
		g_serverSock = INVALID_SOCKET;
	}

	if (g_wsaStarted)
	{
		WSACleanup();
		g_wsaStarted = false;
	}

	if (::IsWindow(m_hWnd) && GetDlgItem(IDC_BUTTON_START) != NULL)
	{
		GetDlgItem(IDC_BUTTON_START)->EnableWindow(TRUE);
	}
}
