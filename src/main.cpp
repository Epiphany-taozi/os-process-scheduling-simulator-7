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
#include <utility>
#include <vector>

#pragma comment(lib, "comctl32.lib")

// ============================= 数据结构与调度算法区域 =============================

struct Process {
    std::wstring name;                 // 进程名
    int arrivalTime = 0;               // 到达时间
    int burstTime = 0;                 // 服务时间/运行时间
    int remainingTime = 0;             // 剩余运行时间，抢占式算法和 RR 使用
    int priority = 0;                  // 优先级：数字越小优先级越高
    int startTime = -1;                // 第一次获得 CPU 的时间
    int finishTime = 0;                // 完成时间
    int turnaroundTime = 0;            // 周转时间 = 完成时间 - 到达时间
    double weightedTurnaroundTime = 0; // 带权周转时间 = 周转时间 / 服务时间
    int inputOrder = 0;                // 输入顺序，用于稳定同分排序
};

struct GanttBlock {
    std::wstring processName;          // 运行进程名
    int startTime = 0;                 // 片段开始时间
    int endTime = 0;                   // 片段结束时间
};

struct ScheduleResult {
    std::vector<Process> processes;
    std::vector<GanttBlock> ganttBlocks;
    double averageTurnaroundTime = 0;
    double averageWeightedTurnaroundTime = 0;
    std::wstring scheduleOrder;
};

static void addGanttBlock(std::vector<GanttBlock>& blocks, const std::wstring& name, int start, int end) {
    if (start >= end) return;
    // 连续执行同一个进程的相邻时间片合并，避免抢占式算法甘特图过碎。
    if (!blocks.empty() && blocks.back().processName == name && blocks.back().endTime == start) {
        blocks.back().endTime = end;
    } else {
        blocks.push_back({ name, start, end });
    }
}

static void finishStatistics(ScheduleResult& result) {
    double totalTurnaround = 0;
    double totalWeighted = 0;
    for (auto& p : result.processes) {
        p.turnaroundTime = p.finishTime - p.arrivalTime;
        p.weightedTurnaroundTime = static_cast<double>(p.turnaroundTime) / p.burstTime;
        totalTurnaround += p.turnaroundTime;
        totalWeighted += p.weightedTurnaroundTime;
    }
    std::stable_sort(result.processes.begin(), result.processes.end(), [](const Process& a, const Process& b) {
        return a.inputOrder < b.inputOrder;
    });
    if (!result.processes.empty()) {
        result.averageTurnaroundTime = totalTurnaround / result.processes.size();
        result.averageWeightedTurnaroundTime = totalWeighted / result.processes.size();
    }

    result.scheduleOrder.clear();
    for (size_t i = 0; i < result.ganttBlocks.size(); ++i) {
        if (i) result.scheduleOrder += L" -> ";
        result.scheduleOrder += result.ganttBlocks[i].processName;
    }
}

static int earliestArrival(const std::vector<Process>& processes) {
    int earliest = INT_MAX;
    for (const auto& p : processes) earliest = std::min(earliest, p.arrivalTime);
    return earliest == INT_MAX ? 0 : earliest;
}

// 1. FCFS：先来先服务，按到达时间排序，到达时间相同则按输入顺序。
static ScheduleResult scheduleFCFS(std::vector<Process> processes) {
    ScheduleResult result;
    result.processes = std::move(processes);
    std::vector<int> order(result.processes.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const auto& pa = result.processes[a];
        const auto& pb = result.processes[b];
        if (pa.arrivalTime != pb.arrivalTime) return pa.arrivalTime < pb.arrivalTime;
        return pa.inputOrder < pb.inputOrder;
    });

    int currentTime = earliestArrival(result.processes);
    for (int idx : order) {
        auto& p = result.processes[idx];
        currentTime = std::max(currentTime, p.arrivalTime);
        p.startTime = currentTime;
        p.finishTime = currentTime + p.burstTime;
        p.remainingTime = 0;
        addGanttBlock(result.ganttBlocks, p.name, p.startTime, p.finishTime);
        currentTime = p.finishTime;
    }
    finishStatistics(result);
    return result;
}

// 2. SJF 非抢占式：CPU 空闲时选择已到达且服务时间最短的进程，执行后不被打断。
static ScheduleResult scheduleSJFNonPreemptive(std::vector<Process> processes) {
    ScheduleResult result;
    result.processes = std::move(processes);
    const int n = static_cast<int>(result.processes.size());
    std::vector<bool> done(n, false);
    int finished = 0;
    int currentTime = earliestArrival(result.processes);

    while (finished < n) {
        int best = -1;
        for (int i = 0; i < n; ++i) {
            if (done[i] || result.processes[i].arrivalTime > currentTime) continue;
            if (best == -1 ||
                result.processes[i].burstTime < result.processes[best].burstTime ||
                (result.processes[i].burstTime == result.processes[best].burstTime && result.processes[i].arrivalTime < result.processes[best].arrivalTime) ||
                (result.processes[i].burstTime == result.processes[best].burstTime && result.processes[i].arrivalTime == result.processes[best].arrivalTime && result.processes[i].inputOrder < result.processes[best].inputOrder)) {
                best = i;
            }
        }
        if (best == -1) {
            int nextArrival = INT_MAX;
            for (int i = 0; i < n; ++i) if (!done[i]) nextArrival = std::min(nextArrival, result.processes[i].arrivalTime);
            currentTime = nextArrival;
            continue;
        }
        auto& p = result.processes[best];
        p.startTime = currentTime;
        p.finishTime = currentTime + p.burstTime;
        p.remainingTime = 0;
        addGanttBlock(result.ganttBlocks, p.name, p.startTime, p.finishTime);
        currentTime = p.finishTime;
        done[best] = true;
        ++finished;
    }
    finishStatistics(result);
    return result;
}

// 3. SRTF 抢占式短作业优先：每个时间单位选择剩余时间最短的进程。
static ScheduleResult scheduleSRTFPreemptive(std::vector<Process> processes) {
    ScheduleResult result;
    result.processes = std::move(processes);
    const int n = static_cast<int>(result.processes.size());
    for (auto& p : result.processes) p.remainingTime = p.burstTime;
    int finished = 0;
    int currentTime = earliestArrival(result.processes);

    while (finished < n) {
        int best = -1;
        for (int i = 0; i < n; ++i) {
            const auto& p = result.processes[i];
            if (p.arrivalTime > currentTime || p.remainingTime <= 0) continue;
            if (best == -1 ||
                p.remainingTime < result.processes[best].remainingTime ||
                (p.remainingTime == result.processes[best].remainingTime && p.arrivalTime < result.processes[best].arrivalTime) ||
                (p.remainingTime == result.processes[best].remainingTime && p.arrivalTime == result.processes[best].arrivalTime && p.inputOrder < result.processes[best].inputOrder)) {
                best = i;
            }
        }
        if (best == -1) { ++currentTime; continue; }
        auto& p = result.processes[best];
        if (p.startTime == -1) p.startTime = currentTime;
        addGanttBlock(result.ganttBlocks, p.name, currentTime, currentTime + 1);
        --p.remainingTime;
        ++currentTime;
        if (p.remainingTime == 0) {
            p.finishTime = currentTime;
            ++finished;
        }
    }
    finishStatistics(result);
    return result;
}

// 4. Priority 非抢占式：CPU 空闲时选择优先级最高（数字最小）的进程，执行后不被打断。
static ScheduleResult schedulePriorityNonPreemptive(std::vector<Process> processes) {
    ScheduleResult result;
    result.processes = std::move(processes);
    const int n = static_cast<int>(result.processes.size());
    std::vector<bool> done(n, false);
    int finished = 0;
    int currentTime = earliestArrival(result.processes);

    while (finished < n) {
        int best = -1;
        for (int i = 0; i < n; ++i) {
            if (done[i] || result.processes[i].arrivalTime > currentTime) continue;
            if (best == -1 ||
                result.processes[i].priority < result.processes[best].priority ||
                (result.processes[i].priority == result.processes[best].priority && result.processes[i].arrivalTime < result.processes[best].arrivalTime) ||
                (result.processes[i].priority == result.processes[best].priority && result.processes[i].arrivalTime == result.processes[best].arrivalTime && result.processes[i].inputOrder < result.processes[best].inputOrder)) {
                best = i;
            }
        }
        if (best == -1) {
            int nextArrival = INT_MAX;
            for (int i = 0; i < n; ++i) if (!done[i]) nextArrival = std::min(nextArrival, result.processes[i].arrivalTime);
            currentTime = nextArrival;
            continue;
        }
        auto& p = result.processes[best];
        p.startTime = currentTime;
        p.finishTime = currentTime + p.burstTime;
        p.remainingTime = 0;
        addGanttBlock(result.ganttBlocks, p.name, p.startTime, p.finishTime);
        currentTime = p.finishTime;
        done[best] = true;
        ++finished;
    }
    finishStatistics(result);
    return result;
}

// 5. Preemptive Priority：每个时间单位选择优先级最高的进程，高优先级到达时可抢占。
static ScheduleResult schedulePriorityPreemptive(std::vector<Process> processes) {
    ScheduleResult result;
    result.processes = std::move(processes);
    const int n = static_cast<int>(result.processes.size());
    for (auto& p : result.processes) p.remainingTime = p.burstTime;
    int finished = 0;
    int currentTime = earliestArrival(result.processes);

    while (finished < n) {
        int best = -1;
        for (int i = 0; i < n; ++i) {
            const auto& p = result.processes[i];
            if (p.arrivalTime > currentTime || p.remainingTime <= 0) continue;
            if (best == -1 ||
                p.priority < result.processes[best].priority ||
                (p.priority == result.processes[best].priority && p.remainingTime < result.processes[best].remainingTime) ||
                (p.priority == result.processes[best].priority && p.remainingTime == result.processes[best].remainingTime && p.arrivalTime < result.processes[best].arrivalTime) ||
                (p.priority == result.processes[best].priority && p.remainingTime == result.processes[best].remainingTime && p.arrivalTime == result.processes[best].arrivalTime && p.inputOrder < result.processes[best].inputOrder)) {
                best = i;
            }
        }
        if (best == -1) { ++currentTime; continue; }
        auto& p = result.processes[best];
        if (p.startTime == -1) p.startTime = currentTime;
        addGanttBlock(result.ganttBlocks, p.name, currentTime, currentTime + 1);
        --p.remainingTime;
        ++currentTime;
        if (p.remainingTime == 0) {
            p.finishTime = currentTime;
            ++finished;
        }
    }
    finishStatistics(result);
    return result;
}

// 6. RR：时间片轮转，进程按到达时间和输入顺序进入就绪队列。
static ScheduleResult scheduleRR(std::vector<Process> processes, int timeQuantum) {
    ScheduleResult result;
    result.processes = std::move(processes);
    const int n = static_cast<int>(result.processes.size());
    for (auto& p : result.processes) p.remainingTime = p.burstTime;

    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const auto& pa = result.processes[a];
        const auto& pb = result.processes[b];
        if (pa.arrivalTime != pb.arrivalTime) return pa.arrivalTime < pb.arrivalTime;
        return pa.inputOrder < pb.inputOrder;
    });

    std::queue<int> ready;
    int currentTime = earliestArrival(result.processes);
    int finished = 0;
    int next = 0;
    auto pushArrived = [&]() {
        while (next < n && result.processes[order[next]].arrivalTime <= currentTime) {
            ready.push(order[next++]);
        }
    };

    while (finished < n) {
        pushArrived();
        if (ready.empty()) {
            currentTime = std::max(currentTime, result.processes[order[next]].arrivalTime);
            pushArrived();
        }
        int idx = ready.front();
        ready.pop();
        auto& p = result.processes[idx];
        if (p.startTime == -1) p.startTime = currentTime;
        int runTime = std::min(timeQuantum, p.remainingTime);
        addGanttBlock(result.ganttBlocks, p.name, currentTime, currentTime + runTime);
        currentTime += runTime;
        p.remainingTime -= runTime;
        pushArrived();
        if (p.remainingTime > 0) {
            ready.push(idx);
        } else {
            p.finishTime = currentTime;
            ++finished;
        }
    }
    finishStatistics(result);
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
#define IDC_ALGO_DESC 1013

static HINSTANCE g_hInst;
static HWND g_nameEdit, g_arrivalEdit, g_serviceEdit, g_priorityEdit, g_quantumEdit;
static HWND g_algoCombo, g_processList, g_resultList, g_algoDesc;
static std::vector<Process> g_processes;
static int g_nextInputOrder = 0;
static ScheduleResult g_lastResult;
static bool g_hasResult = false;

static const wchar_t* GetAlgorithmDescription(int algo) {
    switch (algo) {
    case 0: return L"FCFS：按照进程到达时间先后顺序依次执行。";
    case 1: return L"SJF 非抢占式：CPU 空闲时选择已到达进程中服务时间最短者执行，执行后不被打断。";
    case 2: return L"SRTF 抢占式：每个时间单位选择剩余时间最短的进程，新短作业到达时可能抢占。";
    case 3: return L"Priority 非抢占式：CPU 空闲时选择优先级最高的进程，执行后不被打断。";
    case 4: return L"Preemptive Priority：高优先级进程到达时可以抢占当前进程。";
    case 5: return L"RR：按照时间片轮转执行，就绪队列中的进程轮流获得 CPU。";
    default: return L"请选择调度算法。";
    }
}

static void UpdateAlgorithmDescription() {
    if (g_algoDesc) SetWindowTextW(g_algoDesc, GetAlgorithmDescription(ComboBox_GetCurSel(g_algoCombo)));
}


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
        ListView_SetItemText(g_processList, i, 1, const_cast<LPWSTR>(std::to_wstring(p.arrivalTime).c_str()));
        ListView_SetItemText(g_processList, i, 2, const_cast<LPWSTR>(std::to_wstring(p.burstTime).c_str()));
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
        ListView_SetItemText(g_resultList, i, 1, const_cast<LPWSTR>(std::to_wstring(p.arrivalTime).c_str()));
        ListView_SetItemText(g_resultList, i, 2, const_cast<LPWSTR>(std::to_wstring(p.burstTime).c_str()));
        ListView_SetItemText(g_resultList, i, 3, const_cast<LPWSTR>(std::to_wstring(p.priority).c_str()));
        ListView_SetItemText(g_resultList, i, 4, const_cast<LPWSTR>(std::to_wstring(p.startTime).c_str()));
        ListView_SetItemText(g_resultList, i, 5, const_cast<LPWSTR>(std::to_wstring(p.finishTime).c_str()));
        ListView_SetItemText(g_resultList, i, 6, const_cast<LPWSTR>(std::to_wstring(p.turnaroundTime).c_str()));
        std::wstring w = FormatDouble(p.weightedTurnaroundTime);
        ListView_SetItemText(g_resultList, i, 7, const_cast<LPWSTR>(w.c_str()));
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
    if (!g_hasResult || g_lastResult.ganttBlocks.empty()) {
        TextOutW(hdc, rc.left + 10, rc.top + 40, L"请添加进程并点击“开始调度”。", lstrlenW(L"请添加进程并点击“开始调度”。"));
        return;
    }

    int minTime = g_lastResult.ganttBlocks.front().startTime;
    int maxTime = g_lastResult.ganttBlocks.back().endTime;
    int total = std::max(1, maxTime - minTime);
    int x0 = rc.left + 30;
    int y0 = rc.top + 60;
    int width = std::max(100, rc.right - rc.left - 60);
    int height = 50;
    COLORREF colors[] = { RGB(135,206,250), RGB(144,238,144), RGB(255,218,185), RGB(221,160,221), RGB(255,182,193), RGB(240,230,140) };
    std::map<std::wstring, COLORREF> colorMap;
    int colorIndex = 0;

    for (const auto& s : g_lastResult.ganttBlocks) {
        if (!colorMap.count(s.processName)) colorMap[s.processName] = colors[colorIndex++ % 6];
        int x1 = x0 + (s.startTime - minTime) * width / total;
        int x2 = x0 + (s.endTime - minTime) * width / total;
        HBRUSH brush = CreateSolidBrush(colorMap[s.processName]);
        RECT block{ x1, y0, x2, y0 + height };
        FillRect(hdc, &block, brush);
        DeleteObject(brush);
        Rectangle(hdc, block.left, block.top, block.right, block.bottom);
        DrawTextW(hdc, s.processName.c_str(), -1, &block, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        std::wstring startText = std::to_wstring(s.startTime);
        TextOutW(hdc, x1 - 4, y0 + height + 8, startText.c_str(), static_cast<int>(startText.size()));
        std::wstring endText = std::to_wstring(s.endTime);
        TextOutW(hdc, x2 - 4, y0 + height + 8, endText.c_str(), static_cast<int>(endText.size()));
    }

    std::wstring order = L"调度顺序：" + g_lastResult.scheduleOrder;
    TextOutW(hdc, rc.left + 10, y0 + height + 40, order.c_str(), static_cast<int>(order.size()));

    std::wstring avg = L"平均周转时间：" + FormatDouble(g_lastResult.averageTurnaroundTime) + L"    平均带权周转时间：" + FormatDouble(g_lastResult.averageWeightedTurnaroundTime);
    TextOutW(hdc, rc.left + 10, y0 + height + 68, avg.c_str(), static_cast<int>(avg.size()));
}

static void AddProcessFromInput(HWND hwnd) {
    Process p;
    p.name = GetWindowTextString(g_nameEdit);
    if (p.name.empty()) { ShowError(L"进程名不能为空。 "); return; }
    if (!ParseInt(GetWindowTextString(g_arrivalEdit), p.arrivalTime) || p.arrivalTime < 0) { ShowError(L"到达时间必须是不小于 0 的整数。 "); return; }
    if (!ParseInt(GetWindowTextString(g_serviceEdit), p.burstTime) || p.burstTime <= 0) { ShowError(L"服务时间必须是大于 0 的整数。 "); return; }
    if (!ParseInt(GetWindowTextString(g_priorityEdit), p.priority)) { ShowError(L"优先级必须是整数，数字越小优先级越高。 "); return; }
    p.remainingTime = p.burstTime;
    p.inputOrder = g_nextInputOrder++;
    g_processes.push_back(p);
    g_hasResult = false;
    RefreshProcessList();
    RefreshResultList();
    InvalidateRect(hwnd, nullptr, TRUE);
}

static void StartSchedule(HWND hwnd) {
    if (g_processes.empty()) { ShowError(L"请至少添加一个进程。 "); return; }
    int algo = ComboBox_GetCurSel(g_algoCombo);
    if (algo == 0) g_lastResult = scheduleFCFS(g_processes);
    else if (algo == 1) g_lastResult = scheduleSJFNonPreemptive(g_processes);
    else if (algo == 2) g_lastResult = scheduleSRTFPreemptive(g_processes);
    else if (algo == 3) g_lastResult = schedulePriorityNonPreemptive(g_processes);
    else if (algo == 4) g_lastResult = schedulePriorityPreemptive(g_processes);
    else {
        int quantum = 0;
        if (!ParseInt(GetWindowTextString(g_quantumEdit), quantum) || quantum <= 0) { ShowError(L"RR 时间片必须是大于 0 的整数。 "); return; }
        g_lastResult = scheduleRR(g_processes, quantum);
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
    g_algoCombo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 90, 52, 300, 200, hwnd, reinterpret_cast<HMENU>(IDC_ALGO), g_hInst, nullptr);
    SendMessageW(g_algoCombo, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    ComboBox_AddString(g_algoCombo, L"FCFS 先来先服务");
    ComboBox_AddString(g_algoCombo, L"SJF 非抢占式短作业优先");
    ComboBox_AddString(g_algoCombo, L"SRTF 抢占式短作业优先");
    ComboBox_AddString(g_algoCombo, L"Priority 非抢占式优先级调度");
    ComboBox_AddString(g_algoCombo, L"Preemptive Priority 抢占式优先级调度");
    ComboBox_AddString(g_algoCombo, L"RR 时间片轮转");
    ComboBox_SetCurSel(g_algoCombo, 0);
    label(405, 55, L"时间片");     g_quantumEdit = edit(IDC_QUANTUM, 465, 52, 60, L"2");
    button(IDC_START, 540, 50, 100, L"开始调度");

    CreateWindowW(L"STATIC", L"算法说明", WS_CHILD | WS_VISIBLE, 650, 55, 80, 24, hwnd, nullptr, g_hInst, nullptr);
    g_algoDesc = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | SS_LEFT,
                               730, 50, 430, 44, hwnd, reinterpret_cast<HMENU>(IDC_ALGO_DESC), g_hInst, nullptr);
    SendMessageW(g_algoDesc, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    UpdateAlgorithmDescription();

    CreateWindowW(L"STATIC", L"输入进程信息列表", WS_CHILD | WS_VISIBLE, 15, 92, 160, 24, hwnd, nullptr, g_hInst, nullptr);
    g_processList = CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
                                  15, 120, 430, 210, hwnd, reinterpret_cast<HMENU>(IDC_PROCESS_LIST), g_hInst, nullptr);
    ListView_SetExtendedListViewStyle(g_processList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    AddListColumn(g_processList, 0, 120, L"进程名");
    AddListColumn(g_processList, 1, 100, L"到达时间");
    AddListColumn(g_processList, 2, 100, L"服务时间");
    AddListColumn(g_processList, 3, 100, L"优先级");

    CreateWindowW(L"STATIC", L"调度结果列表", WS_CHILD | WS_VISIBLE, 465, 92, 160, 24, hwnd, nullptr, g_hInst, nullptr);
    g_resultList = CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT,
                                 465, 120, 695, 210, hwnd, reinterpret_cast<HMENU>(IDC_RESULT_LIST), g_hInst, nullptr);
    ListView_SetExtendedListViewStyle(g_resultList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    AddListColumn(g_resultList, 0, 80, L"进程名");
    AddListColumn(g_resultList, 1, 80, L"到达时间");
    AddListColumn(g_resultList, 2, 80, L"服务时间");
    AddListColumn(g_resultList, 3, 70, L"优先级");
    AddListColumn(g_resultList, 4, 80, L"开始时间");
    AddListColumn(g_resultList, 5, 80, L"完成时间");
    AddListColumn(g_resultList, 6, 80, L"周转时间");
    AddListColumn(g_resultList, 7, 110, L"带权周转");
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        CreateControls(hwnd);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_ALGO && HIWORD(wParam) == CBN_SELCHANGE) {
            UpdateAlgorithmDescription();
            return 0;
        }
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
            g_nextInputOrder = 0;
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
                                CW_USEDEFAULT, CW_USEDEFAULT, 1200, 760,
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
