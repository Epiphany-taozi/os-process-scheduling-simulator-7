#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <climits>
#include <iomanip>
#include <map>
#include <numeric>
#include <queue>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

// ============================= 数据结构与调度算法区域 =============================

struct Process {
    std::wstring name;      // 进程名
    int arrival = 0;        // 到达时间
    int service = 0;        // 服务时间
    int priority = 0;       // 优先级：数字越小优先级越高
    int start = -1;         // 开始时间
    int finish = 0;         // 完成时间
    int turnaround = 0;     // 周转时间
    double weighted = 0.0;  // 带权周转时间
};

struct GanttSegment {
    std::wstring name;      // 运行进程名
    int start = 0;          // 片段开始时间
    int end = 0;            // 片段结束时间
};

struct ScheduleResult {
    std::vector<Process> processes;
    std::vector<GanttSegment> segments;
    double avgTurnaround = 0.0;
    double avgWeighted = 0.0;
};

static void FinishStatistics(ScheduleResult& result) {
    double totalTurnaround = 0.0;
    double totalWeighted = 0.0;
    for (auto& p : result.processes) {
        p.turnaround = p.finish - p.arrival;
        p.weighted = static_cast<double>(p.turnaround) / p.service;
        totalTurnaround += p.turnaround;
        totalWeighted += p.weighted;
    }
    if (!result.processes.empty()) {
        result.avgTurnaround = totalTurnaround / result.processes.size();
        result.avgWeighted = totalWeighted / result.processes.size();
    }
}

static void AddSegment(std::vector<GanttSegment>& segments, const std::wstring& name, int start, int end, bool mergeAdjacent = true) {
    if (start >= end) return;
    // 非 RR 算法可合并相邻且进程相同的运行片段；RR 保留每个时间片，便于观察轮转。
    if (mergeAdjacent && !segments.empty() && segments.back().name == name && segments.back().end == start) {
        segments.back().end = end;
    } else {
        segments.push_back({name, start, end});
    }
}

// FCFS：先来先服务，按到达时间排序，到达时间相同则保持输入顺序。
static ScheduleResult ScheduleFCFS(const std::vector<Process>& input) {
    ScheduleResult result;
    result.processes = input;
    std::vector<int> order(input.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return input[a].arrival < input[b].arrival;
    });

    int current = 0;
    for (int idx : order) {
        auto& p = result.processes[idx];
        current = std::max(current, p.arrival);
        p.start = current;
        p.finish = current + p.service;
        AddSegment(result.segments, p.name, p.start, p.finish);
        current = p.finish;
    }
    FinishStatistics(result);
    return result;
}

// SJF：非抢占式短作业优先，每次选择当前已到达且服务时间最短的进程。
static ScheduleResult ScheduleSJF(const std::vector<Process>& input) {
    ScheduleResult result;
    result.processes = input;
    const int n = static_cast<int>(input.size());
    std::vector<bool> done(n, false);
    int finished = 0;
    int current = 0;

    while (finished < n) {
        int best = -1;
        for (int i = 0; i < n; ++i) {
            if (done[i] || result.processes[i].arrival > current) continue;
            if (best == -1 || result.processes[i].service < result.processes[best].service ||
                (result.processes[i].service == result.processes[best].service && result.processes[i].arrival < result.processes[best].arrival)) {
                best = i;
            }
        }
        if (best == -1) {
            int nextArrival = INT_MAX;
            for (int i = 0; i < n; ++i) if (!done[i]) nextArrival = std::min(nextArrival, result.processes[i].arrival);
            current = nextArrival;
            continue;
        }
        auto& p = result.processes[best];
        p.start = current;
        p.finish = current + p.service;
        AddSegment(result.segments, p.name, p.start, p.finish);
        current = p.finish;
        done[best] = true;
        ++finished;
    }
    FinishStatistics(result);
    return result;
}

// Priority：非抢占式优先级调度，数字越小优先级越高。
static ScheduleResult SchedulePriority(const std::vector<Process>& input) {
    ScheduleResult result;
    result.processes = input;
    const int n = static_cast<int>(input.size());
    std::vector<bool> done(n, false);
    int finished = 0;
    int current = 0;

    while (finished < n) {
        int best = -1;
        for (int i = 0; i < n; ++i) {
            if (done[i] || result.processes[i].arrival > current) continue;
            if (best == -1 || result.processes[i].priority < result.processes[best].priority ||
                (result.processes[i].priority == result.processes[best].priority && result.processes[i].arrival < result.processes[best].arrival)) {
                best = i;
            }
        }
        if (best == -1) {
            int nextArrival = INT_MAX;
            for (int i = 0; i < n; ++i) if (!done[i]) nextArrival = std::min(nextArrival, result.processes[i].arrival);
            current = nextArrival;
            continue;
        }
        auto& p = result.processes[best];
        p.start = current;
        p.finish = current + p.service;
        AddSegment(result.segments, p.name, p.start, p.finish);
        current = p.finish;
        done[best] = true;
        ++finished;
    }
    FinishStatistics(result);
    return result;
}

// RR：时间片轮转，同一进程可多次进入 CPU，因此甘特图保留多个片段。
static ScheduleResult ScheduleRR(const std::vector<Process>& input, int quantum) {
    ScheduleResult result;
    result.processes = input;
    const int n = static_cast<int>(input.size());
    std::vector<int> remain(n);
    for (int i = 0; i < n; ++i) remain[i] = input[i].service;

    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return input[a].arrival < input[b].arrival; });

    std::queue<int> ready;
    int current = 0, finished = 0, next = 0;
    auto pushArrived = [&]() {
        while (next < n && input[order[next]].arrival <= current) ready.push(order[next++]);
    };

    while (finished < n) {
        pushArrived();
        if (ready.empty()) {
            current = std::max(current, input[order[next]].arrival);
            pushArrived();
        }
        int idx = ready.front();
        ready.pop();
        auto& p = result.processes[idx];
        if (p.start == -1) p.start = current;
        int run = std::min(quantum, remain[idx]);
        AddSegment(result.segments, p.name, current, current + run, false);
        current += run;
        remain[idx] -= run;
        pushArrived();
        if (remain[idx] > 0) {
            ready.push(idx);
        } else {
            p.finish = current;
            ++finished;
        }
    }
    FinishStatistics(result);
    return result;
}

// ============================= Win32 图形界面区域 =============================

#define IDC_NAME 1001
#define IDC_ARRIVAL 1002
#define IDC_SERVICE 1003
#define IDC_PRIORITY 1004
#define IDC_ADD 1005
#define IDC_DELETE 1006
#define IDC_CLEAR 1007
#define IDC_ALGO 1008
#define IDC_QUANTUM 1009
#define IDC_START 1010
#define IDC_PROCESS_LIST 1011
#define IDC_RESULT_LIST 1012

static HINSTANCE g_hInst;
static HWND g_nameEdit, g_arrivalEdit, g_serviceEdit, g_priorityEdit, g_quantumEdit;
static HWND g_algoCombo, g_processList, g_resultList;
static std::vector<Process> g_processes;
static ScheduleResult g_lastResult;
static bool g_hasResult = false;

static std::wstring GetWindowTextString(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    std::wstring text(len, L'\0');
    if (len > 0) {
        std::vector<wchar_t> buffer(static_cast<size_t>(len) + 1);
        GetWindowTextW(hwnd, buffer.data(), len + 1);
        text.assign(buffer.data());
    }
    return text;
}

static bool ParseInt(const std::wstring& text, int& value) {
    try {
        size_t pos = 0;
        value = std::stoi(text, &pos);
        return pos == text.size();
    } catch (...) {
        return false;
    }
}

static void ShowError(const std::wstring& msg) {
    MessageBoxW(nullptr, msg.c_str(), L"输入错误", MB_OK | MB_ICONWARNING);
}

static void RefreshProcessList() {
    ListView_DeleteAllItems(g_processList);
    for (int i = 0; i < static_cast<int>(g_processes.size()); ++i) {
        const auto& p = g_processes[i];
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = const_cast<LPWSTR>(p.name.c_str());
        ListView_InsertItem(g_processList, &item);
        ListView_SetItemText(g_processList, i, 1, const_cast<LPWSTR>(std::to_wstring(p.arrival).c_str()));
        ListView_SetItemText(g_processList, i, 2, const_cast<LPWSTR>(std::to_wstring(p.service).c_str()));
        ListView_SetItemText(g_processList, i, 3, const_cast<LPWSTR>(std::to_wstring(p.priority).c_str()));
    }
}

static std::wstring FormatDouble(double v) {
    std::wstringstream ss;
    ss << std::fixed << std::setprecision(2) << v;
    return ss.str();
}

static void RefreshResultList() {
    ListView_DeleteAllItems(g_resultList);
    if (!g_hasResult) return;
    for (int i = 0; i < static_cast<int>(g_lastResult.processes.size()); ++i) {
        const auto& p = g_lastResult.processes[i];
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = const_cast<LPWSTR>(p.name.c_str());
        ListView_InsertItem(g_resultList, &item);
        ListView_SetItemText(g_resultList, i, 1, const_cast<LPWSTR>(std::to_wstring(p.start).c_str()));
        ListView_SetItemText(g_resultList, i, 2, const_cast<LPWSTR>(std::to_wstring(p.finish).c_str()));
        ListView_SetItemText(g_resultList, i, 3, const_cast<LPWSTR>(std::to_wstring(p.turnaround).c_str()));
        std::wstring w = FormatDouble(p.weighted);
        ListView_SetItemText(g_resultList, i, 4, const_cast<LPWSTR>(w.c_str()));
    }
}

static void AddListColumn(HWND list, int index, int width, const wchar_t* text) {
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.cx = width;
    col.iSubItem = index;
    col.pszText = const_cast<LPWSTR>(text);
    ListView_InsertColumn(list, index, &col);
}

static void DrawGanttChart(HDC hdc, RECT rc) {
    HBRUSH bg = CreateSolidBrush(RGB(250, 250, 250));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);

    SetBkMode(hdc, TRANSPARENT);
    TextOutW(hdc, rc.left + 10, rc.top + 8, L"甘特图", 3);
    if (!g_hasResult || g_lastResult.segments.empty()) {
        TextOutW(hdc, rc.left + 10, rc.top + 40, L"请添加进程并点击“开始调度”。", lstrlenW(L"请添加进程并点击“开始调度”。"));
        return;
    }

    int minTime = g_lastResult.segments.front().start;
    int maxTime = g_lastResult.segments.back().end;
    int total = std::max(1, maxTime - minTime);
    int x0 = rc.left + 30;
    int y0 = rc.top + 60;
    int width = std::max(100, rc.right - rc.left - 60);
    int height = 50;
    COLORREF colors[] = { RGB(135,206,250), RGB(144,238,144), RGB(255,218,185), RGB(221,160,221), RGB(255,182,193), RGB(240,230,140) };
    std::map<std::wstring, COLORREF> colorMap;
    int colorIndex = 0;

    for (const auto& s : g_lastResult.segments) {
        if (!colorMap.count(s.name)) colorMap[s.name] = colors[colorIndex++ % 6];
        int x1 = x0 + (s.start - minTime) * width / total;
        int x2 = x0 + (s.end - minTime) * width / total;
        HBRUSH brush = CreateSolidBrush(colorMap[s.name]);
        RECT block{ x1, y0, x2, y0 + height };
        FillRect(hdc, &block, brush);
        DeleteObject(brush);
        Rectangle(hdc, block.left, block.top, block.right, block.bottom);
        DrawTextW(hdc, s.name.c_str(), -1, &block, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        std::wstring startText = std::to_wstring(s.start);
        TextOutW(hdc, x1 - 4, y0 + height + 8, startText.c_str(), static_cast<int>(startText.size()));
        std::wstring endText = std::to_wstring(s.end);
        TextOutW(hdc, x2 - 4, y0 + height + 8, endText.c_str(), static_cast<int>(endText.size()));
    }

    std::wstring order = L"调度顺序：";
    for (size_t i = 0; i < g_lastResult.segments.size(); ++i) {
        if (i) order += L" -> ";
        order += g_lastResult.segments[i].name;
    }
    TextOutW(hdc, rc.left + 10, y0 + height + 40, order.c_str(), static_cast<int>(order.size()));

    std::wstring avg = L"平均周转时间：" + FormatDouble(g_lastResult.avgTurnaround) + L"    平均带权周转时间：" + FormatDouble(g_lastResult.avgWeighted);
    TextOutW(hdc, rc.left + 10, y0 + height + 68, avg.c_str(), static_cast<int>(avg.size()));
}

static void AddProcessFromInput(HWND hwnd) {
    Process p;
    p.name = GetWindowTextString(g_nameEdit);
    if (p.name.empty()) { ShowError(L"进程名不能为空。 "); return; }
    if (!ParseInt(GetWindowTextString(g_arrivalEdit), p.arrival) || p.arrival < 0) { ShowError(L"到达时间必须是不小于 0 的整数。 "); return; }
    if (!ParseInt(GetWindowTextString(g_serviceEdit), p.service) || p.service <= 0) { ShowError(L"服务时间必须是大于 0 的整数。 "); return; }
    if (!ParseInt(GetWindowTextString(g_priorityEdit), p.priority)) { ShowError(L"优先级必须是整数，数字越小优先级越高。 "); return; }
    g_processes.push_back(p);
    g_hasResult = false;
    RefreshProcessList();
    RefreshResultList();
    InvalidateRect(hwnd, nullptr, TRUE);
}

static void StartSchedule(HWND hwnd) {
    if (g_processes.empty()) { ShowError(L"请至少添加一个进程。 "); return; }
    int algo = ComboBox_GetCurSel(g_algoCombo);
    if (algo == 0) g_lastResult = ScheduleFCFS(g_processes);
    else if (algo == 1) g_lastResult = ScheduleSJF(g_processes);
    else if (algo == 2) g_lastResult = SchedulePriority(g_processes);
    else {
        int quantum = 0;
        if (!ParseInt(GetWindowTextString(g_quantumEdit), quantum) || quantum <= 0) { ShowError(L"RR 时间片必须是大于 0 的整数。 "); return; }
        g_lastResult = ScheduleRR(g_processes, quantum);
    }
    g_hasResult = true;
    RefreshResultList();
    InvalidateRect(hwnd, nullptr, TRUE);
}

static void CreateControls(HWND hwnd) {
    INITCOMMONCONTROLSEX icc{ sizeof(INITCOMMONCONTROLSEX), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);
    HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    auto label = [&](int x, int y, const wchar_t* text) {
        HWND h = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, 80, 24, hwnd, nullptr, g_hInst, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    };
    auto edit = [&](int id, int x, int y, int w, const wchar_t* text = L"") {
        HWND h = CreateWindowW(L"EDIT", text, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, x, y, w, 24, hwnd, reinterpret_cast<HMENU>(id), g_hInst, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return h;
    };
    auto button = [&](int id, int x, int y, int w, const wchar_t* text) {
        HWND h = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE, x, y, w, 28, hwnd, reinterpret_cast<HMENU>(id), g_hInst, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    };

    label(15, 15, L"进程名");      g_nameEdit = edit(IDC_NAME, 90, 12, 90);
    label(190, 15, L"到达时间");   g_arrivalEdit = edit(IDC_ARRIVAL, 265, 12, 70, L"0");
    label(345, 15, L"服务时间");   g_serviceEdit = edit(IDC_SERVICE, 420, 12, 70, L"1");
    label(500, 15, L"优先级");     g_priorityEdit = edit(IDC_PRIORITY, 560, 12, 70, L"1");
    button(IDC_ADD, 650, 10, 100, L"添加进程");
    button(IDC_DELETE, 760, 10, 120, L"删除选中进程");
    button(IDC_CLEAR, 890, 10, 90, L"清空进程");

    label(15, 55, L"调度算法");
    g_algoCombo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 90, 52, 220, 200, hwnd, reinterpret_cast<HMENU>(IDC_ALGO), g_hInst, nullptr);
    SendMessageW(g_algoCombo, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    ComboBox_AddString(g_algoCombo, L"FCFS 先来先服务");
    ComboBox_AddString(g_algoCombo, L"SJF 短作业优先");
    ComboBox_AddString(g_algoCombo, L"Priority 优先级调度");
    ComboBox_AddString(g_algoCombo, L"RR 时间片轮转");
    ComboBox_SetCurSel(g_algoCombo, 0);
    label(330, 55, L"时间片");     g_quantumEdit = edit(IDC_QUANTUM, 390, 52, 70, L"2");
    button(IDC_START, 480, 50, 110, L"开始调度");

    CreateWindowW(L"STATIC", L"输入进程信息列表", WS_CHILD | WS_VISIBLE, 15, 92, 160, 24, hwnd, nullptr, g_hInst, nullptr);
    g_processList = CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
                                  15, 120, 470, 210, hwnd, reinterpret_cast<HMENU>(IDC_PROCESS_LIST), g_hInst, nullptr);
    ListView_SetExtendedListViewStyle(g_processList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    AddListColumn(g_processList, 0, 120, L"进程名");
    AddListColumn(g_processList, 1, 100, L"到达时间");
    AddListColumn(g_processList, 2, 100, L"服务时间");
    AddListColumn(g_processList, 3, 100, L"优先级");

    CreateWindowW(L"STATIC", L"调度结果列表", WS_CHILD | WS_VISIBLE, 510, 92, 160, 24, hwnd, nullptr, g_hInst, nullptr);
    g_resultList = CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT,
                                 510, 120, 520, 210, hwnd, reinterpret_cast<HMENU>(IDC_RESULT_LIST), g_hInst, nullptr);
    ListView_SetExtendedListViewStyle(g_resultList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    AddListColumn(g_resultList, 0, 100, L"进程名");
    AddListColumn(g_resultList, 1, 90, L"开始时间");
    AddListColumn(g_resultList, 2, 90, L"完成时间");
    AddListColumn(g_resultList, 3, 90, L"周转时间");
    AddListColumn(g_resultList, 4, 120, L"带权周转时间");
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        CreateControls(hwnd);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_ADD:
            AddProcessFromInput(hwnd);
            break;
        case IDC_DELETE: {
            int sel = ListView_GetNextItem(g_processList, -1, LVNI_SELECTED);
            if (sel >= 0 && sel < static_cast<int>(g_processes.size())) {
                g_processes.erase(g_processes.begin() + sel);
                g_hasResult = false;
                RefreshProcessList();
                RefreshResultList();
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            break;
        }
        case IDC_CLEAR:
            g_processes.clear();
            g_hasResult = false;
            RefreshProcessList();
            RefreshResultList();
            InvalidateRect(hwnd, nullptr, TRUE);
            break;
        case IDC_START:
            StartSchedule(hwnd);
            break;
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        RECT chart{ 15, 350, rc.right - 15, rc.bottom - 20 };
        DrawGanttChart(hdc, chart);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    g_hInst = hInstance;
    const wchar_t CLASS_NAME[] = L"ProcessSchedulingSimulatorWindow";

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"进程调度模拟系统", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1080, 720,
                                nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) return 0;
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
