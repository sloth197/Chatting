
// ChatClientDlg.cpp: 구현 파일
//

#include "pch.h"
#include "framework.h"
#include "ChatClient.h"
#include "ChatClientDlg.h"
#include "afxdialogex.h"

// 문자열을 UTF-8로 바꿀 때 std::string을 사용합니다.
#include <string>

// WinSock 함수(socket, connect, send, recv)를 쓰기 위해 필요한 라이브러리입니다.
#pragma comment(lib, "ws2_32.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// 수신 스레드에서 화면에 글을 바로 쓰지 않고, 대화상자에게 로그 추가를 요청할 때 쓰는 메시지 번호입니다.
const UINT WM_ADD_LOG = WM_USER + 100;

// 서버와 연결된 클라이언트 소켓입니다.
SOCKET g_clientSock = INVALID_SOCKET;

// 수신 스레드가 대화상자 창으로 메시지를 보내기 위해 창 핸들을 저장합니다.
HWND g_hClientWnd = NULL;

// 현재 서버에 연결되어 있는지 확인하는 값입니다.
bool g_isConnected = false;

// WSAStartup을 성공했을 때만 WSACleanup을 호출하기 위한 값입니다.
bool g_wsaStarted = false;

// 서버에서 메시지를 계속 받는 스레드 함수입니다.
UINT ReceiveThread(LPVOID pParam);

// CString(MFC 문자열)을 소켓으로 보낼 수 있는 UTF-8 문자열로 바꿉니다.
std::string CStringToUtf8(const CString& text)
{
	// 변환에 필요한 바이트 수를 먼저 계산합니다.
	int byteCount = WideCharToMultiByte(CP_UTF8, 0, text.GetString(), -1, NULL, 0, NULL, NULL);
	if (byteCount <= 1)
	{
		return std::string();
	}

	// 실제 UTF-8 변환 결과를 std::string에 저장합니다.
	std::string result(byteCount, '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.GetString(), -1, &result[0], byteCount, NULL, NULL);
	result.resize(byteCount - 1);
	return result;
}

// 서버에서 받은 UTF-8 데이터를 화면에 출력할 수 있는 CString으로 바꿉니다.
CString Utf8ToCString(const char* data, int length)
{
	CString result;

	// 변환에 필요한 글자 수를 먼저 계산합니다.
	int charCount = MultiByteToWideChar(CP_UTF8, 0, data, length, NULL, 0);
	if (charCount <= 0)
	{
		return result;
	}

	// CString 내부 버퍼를 얻어서 변환 결과를 바로 넣습니다.
	wchar_t* buffer = result.GetBuffer(charCount);
	MultiByteToWideChar(CP_UTF8, 0, data, length, buffer, charCount);
	result.ReleaseBuffer(charCount);
	return result;
}

// 수신 스레드에서 UI 스레드로 로그 추가를 요청합니다.
void PostClientLog(const CString& text)
{
	if (::IsWindow(g_hClientWnd))
	{
		// PostMessage는 비동기 방식이라 수신 스레드가 화면 처리를 기다리지 않아도 됩니다.
		::PostMessage(g_hClientWnd, WM_ADD_LOG, 0, (LPARAM)new CString(text));
	}
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


// CChatClientDlg 대화 상자



CChatClientDlg::CChatClientDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_CHATCLIENT_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CChatClientDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CChatClientDlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	// [연결] 버튼을 누르면 서버 접속 함수를 실행합니다.
	ON_BN_CLICKED(IDC_BUTTON_CONNECT, &CChatClientDlg::OnBnClickedButtonConnect)

	// [전송] 버튼을 누르면 입력한 메시지를 서버로 보냅니다.
	ON_BN_CLICKED(IDC_BUTTON_SEND, &CChatClientDlg::OnBnClickedButtonSend)

	// 창이 닫힐 때 소켓을 정리합니다.
	ON_WM_DESTROY()

	// 수신 스레드가 보낸 로그 추가 메시지를 처리합니다.
	ON_MESSAGE(WM_ADD_LOG, &CChatClientDlg::OnAddLog)
END_MESSAGE_MAP()


// CChatClientDlg 메시지 처리기

BOOL CChatClientDlg::OnInitDialog()
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
	// 수신 스레드에서 이 대화상자에 메시지를 보낼 수 있게 창 핸들을 저장합니다.
	g_hClientWnd = m_hWnd;

	// 처음 실행했을 때 바로 테스트하기 쉽도록 기본 서버 주소를 넣어둡니다.
	SetDlgItemText(IDC_EDIT_IP, _T("127.0.0.1"));
	SetDlgItemText(IDC_EDIT_PORT, _T("4000"));

	// 사용자에게 처음 해야 할 일을 로그창에 보여줍니다.
	AddLog(_T("IP와 PORT를 확인하고 [연결] 버튼을 누르세요."));

	return TRUE;  // 포커스를 컨트롤에 설정하지 않으면 TRUE를 반환합니다.
}

void CChatClientDlg::OnSysCommand(UINT nID, LPARAM lParam)
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

void CChatClientDlg::OnPaint()
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
HCURSOR CChatClientDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

void CChatClientDlg::OnBnClickedButtonConnect()
{
	// 이미 연결되어 있으면 중복으로 connect하지 않습니다.
	if (g_isConnected)
	{
		AddLog(_T("이미 서버에 연결되어 있습니다."));
		return;
	}

	// 화면에 입력된 IP와 PORT 값을 가져옵니다.
	CString ipText;
	CString portText;
	GetDlgItemText(IDC_EDIT_IP, ipText);
	GetDlgItemText(IDC_EDIT_PORT, portText);

	// IP가 비어 있으면 내 컴퓨터 주소를 기본값으로 사용합니다.
	if (ipText.IsEmpty())
	{
		ipText = _T("127.0.0.1");
	}

	// PORT가 비어 있거나 잘못되면 서버 코드와 같은 4000번을 사용합니다.
	int port = _ttoi(portText);
	if (port <= 0)
	{
		port = 4000;
	}

	// Windows에서 소켓을 쓰기 전에 WinSock을 초기화해야 합니다.
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		AddLog(_T("WSAStartup 실패"));
		return;
	}
	g_wsaStarted = true;

	// TCP 통신용 소켓을 만듭니다.
	g_clientSock = socket(AF_INET, SOCK_STREAM, 0);
	if (g_clientSock == INVALID_SOCKET)
	{
		AddLog(_T("소켓 생성 실패"));
		Disconnect();
		return;
	}

	// 접속할 서버의 주소 정보를 준비합니다.
	SOCKADDR_IN serverAddr = {};
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons((u_short)port);

	// 문자열 IP를 컴퓨터가 이해하는 숫자 주소로 바꿉니다.
	if (InetPton(AF_INET, ipText, &serverAddr.sin_addr) != 1)
	{
		AddLog(_T("IP 주소 형식이 올바르지 않습니다."));
		Disconnect();
		return;
	}

	// 서버에 실제로 접속합니다.
	if (connect(g_clientSock, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
	{
		AddLog(_T("서버 연결 실패"));
		Disconnect();
		return;
	}

	// 접속에 성공하면 상태와 버튼 표시를 바꿉니다.
	g_isConnected = true;
	SetDlgItemText(IDC_BUTTON_CONNECT, _T("연결됨"));
	GetDlgItem(IDC_BUTTON_CONNECT)->EnableWindow(FALSE);
	AddLog(_T("서버 연결 성공"));

	// 서버에서 오는 메시지를 받기 위해 별도 스레드를 시작합니다.
	AfxBeginThread(ReceiveThread, NULL);
	GetDlgItem(IDC_EDIT_MESSAGE)->SetFocus();
}

void CChatClientDlg::OnBnClickedButtonSend()
{
	// 연결되지 않은 상태에서는 메시지를 보낼 수 없습니다.
	if (!g_isConnected || g_clientSock == INVALID_SOCKET)
	{
		AddLog(_T("먼저 서버에 연결하세요."));
		return;
	}

	// 입력창에 적힌 메시지를 가져옵니다.
	CString message;
	GetDlgItemText(IDC_EDIT_MESSAGE, message);
	message.Trim();

	// 빈 메시지는 보내지 않습니다.
	if (message.IsEmpty())
	{
		return;
	}

	// CString을 UTF-8로 바꾼 뒤 서버로 보냅니다.
	std::string sendData = CStringToUtf8(message);
	int result = send(g_clientSock, sendData.c_str(), (int)sendData.size(), 0);
	if (result == SOCKET_ERROR)
	{
		AddLog(_T("메시지 전송 실패"));
		return;
	}

	// 내가 보낸 메시지도 내 로그창에 보여주고 입력창을 비웁니다.
	AddLog(_T("나: ") + message);
	SetDlgItemText(IDC_EDIT_MESSAGE, _T(""));
}

void CChatClientDlg::OnOK()
{
	// Enter 키를 눌렀을 때도 [전송] 버튼과 같은 동작을 하게 합니다.
	OnBnClickedButtonSend();
}

void CChatClientDlg::OnCancel()
{
	// 창을 닫기 전에 소켓을 먼저 정리합니다.
	Disconnect();
	CDialogEx::OnCancel();
}

void CChatClientDlg::OnDestroy()
{
	// X 버튼 등으로 창이 사라질 때도 소켓을 정리합니다.
	Disconnect();
	CDialogEx::OnDestroy();
}

LRESULT CChatClientDlg::OnAddLog(WPARAM wParam, LPARAM lParam)
{
	// 수신 스레드가 보낸 CString 포인터를 꺼내 로그창에 추가합니다.
	CString* text = (CString*)lParam;
	if (text != NULL)
	{
		AddLog(*text);

		// new로 만든 문자열이므로 사용 후 delete로 메모리를 해제합니다.
		delete text;
	}
	return 0;
}

void CChatClientDlg::AddLog(const CString& text)
{
	// 로그를 보여줄 Edit Control을 가져옵니다.
	CEdit* logEdit = (CEdit*)GetDlgItem(IDC_EDIT_LOG);
	if (logEdit == NULL)
	{
		return;
	}

	// 기존 로그 뒤에 새 줄을 붙입니다.
	CString oldText;
	logEdit->GetWindowText(oldText);
	oldText += text;
	oldText += _T("\r\n");
	logEdit->SetWindowText(oldText);

	// 커서를 맨 아래로 이동해서 최신 로그가 보이게 합니다.
	int length = logEdit->GetWindowTextLength();
	logEdit->SetSel(length, length);
}

void CChatClientDlg::Disconnect()
{
	// 먼저 연결 상태를 false로 바꿔 수신 스레드가 종료될 수 있게 합니다.
	g_isConnected = false;

	// 열려 있는 소켓이 있으면 닫습니다.
	if (g_clientSock != INVALID_SOCKET)
	{
		closesocket(g_clientSock);
		g_clientSock = INVALID_SOCKET;
	}

	// WSAStartup을 했던 경우에만 WSACleanup을 호출합니다.
	if (g_wsaStarted)
	{
		WSACleanup();
		g_wsaStarted = false;
	}

	// 창이 아직 살아 있으면 연결 버튼을 다시 누를 수 있게 되돌립니다.
	if (::IsWindow(m_hWnd) && GetDlgItem(IDC_BUTTON_CONNECT) != NULL)
	{
		SetDlgItemText(IDC_BUTTON_CONNECT, _T("연결"));
		GetDlgItem(IDC_BUTTON_CONNECT)->EnableWindow(TRUE);
	}
}

UINT ReceiveThread(LPVOID pParam)
{
	// 서버에서 받은 데이터를 임시로 담을 공간입니다.
	char buffer[1024];

	// 연결되어 있는 동안 서버 메시지를 계속 기다립니다.
	while (g_isConnected)
	{
		int length = recv(g_clientSock, buffer, sizeof(buffer), 0);
		if (length <= 0)
		{
			break;
		}

		// 받은 데이터를 CString으로 바꿔 로그창에 표시하도록 요청합니다.
		CString message = Utf8ToCString(buffer, length);
		PostClientLog(_T("받음: ") + message);
	}

	// 원래 연결 중이었는데 recv가 끝났다면 서버 연결이 끊어진 것입니다.
	if (g_isConnected)
	{
		PostClientLog(_T("서버와 연결이 끊어졌습니다."));
	}

	// 스레드가 끝날 때 연결 상태를 false로 맞춥니다.
	g_isConnected = false;
	return 0;
}
