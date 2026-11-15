// D3D12 hook + ImGui bootstrap. UNTESTED against v26.63: verify the game really presents via
// D3D12 (RenderDragon can also run D3D11) and that Present/Present1 are the calls it uses.
#include "mod/Overlay.h"
#include "mod/Ui.h"

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <vector>

#include <MinHook.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

using Microsoft::WRL::ComPtr;

namespace bmm::overlay {
namespace {

struct Frame {
    ComPtr<ID3D12CommandAllocator> alloc;
    ComPtr<ID3D12Resource>         buf;
    D3D12_CPU_DESCRIPTOR_HANDLE    rtv{};
};

struct State {
    bool ready = false, failed = false, needFrames = true;
    ID3D12CommandQueue* queue = nullptr;
    ComPtr<ID3D12Device> dev;
    ComPtr<ID3D12DescriptorHeap> rtvHeap, srvHeap;
    ComPtr<ID3D12GraphicsCommandList> cl;
    std::vector<Frame> frames;
    HWND hwnd{};
    WNDPROC oldProc{};
    DXGI_FORMAT fmt{};
    UINT srvInc = 0, srvNext = 1; // slot 0 = font
} S;

constexpr UINT kSrvCount = 128;

using PresentFn  = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(WINAPI*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using ResizeFn   = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ExecFn     = void(WINAPI*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
PresentFn oPresent; Present1Fn oPresent1; ResizeFn oResize; ExecFn oExec;
void *tPresent, *tPresent1, *tResize, *tExec;

LRESULT CALLBACK hkWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (S.ready) {
        ImGui_ImplWin32_WndProcHandler(h, m, w, l);
        ImGuiIO& io = ImGui::GetIO();
        bool mouse = (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST);
        bool key   = (m >= WM_KEYFIRST && m <= WM_KEYLAST);
        if (mouse && io.WantCaptureMouse) return TRUE;
        if (key && (ui::wantsInput() || io.WantTextInput)) return TRUE;
    }
    return CallWindowProcW(S.oldProc, h, m, w, l);
}

bool init(IDXGISwapChain* sc) {
    if (FAILED(sc->GetDevice(IID_PPV_ARGS(&S.dev)))) { S.failed = true; return false; } // D3D11 game -> give up
    DXGI_SWAP_CHAIN_DESC d{}; sc->GetDesc(&d);
    S.hwnd = d.OutputWindow; S.fmt = d.BufferDesc.Format;
    UINT n = d.BufferCount;

    D3D12_DESCRIPTOR_HEAP_DESC rd{}; rd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; rd.NumDescriptors = n;
    D3D12_DESCRIPTOR_HEAP_DESC sd{}; sd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    sd.NumDescriptors = kSrvCount; sd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(S.dev->CreateDescriptorHeap(&rd, IID_PPV_ARGS(&S.rtvHeap)))) return false;
    if (FAILED(S.dev->CreateDescriptorHeap(&sd, IID_PPV_ARGS(&S.srvHeap)))) return false;
    S.srvInc = S.dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    S.frames.resize(n);
    for (auto& f : S.frames)
        if (FAILED(S.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&f.alloc)))) return false;
    if (FAILED(S.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, S.frames[0].alloc.Get(), nullptr,
                                        IID_PPV_ARGS(&S.cl)))) return false;
    S.cl->Close();

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange; // don't fight the game's cursor
    ImGui_ImplWin32_Init(S.hwnd);
    ImGui_ImplDX12_Init(S.dev.Get(), (int)n, S.fmt, S.srvHeap.Get(),
                        S.srvHeap->GetCPUDescriptorHandleForHeapStart(),
                        S.srvHeap->GetGPUDescriptorHandleForHeapStart());
    S.oldProc = (WNDPROC)SetWindowLongPtrW(S.hwnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
    S.ready = true;
    return true;
}

void buildFrames(IDXGISwapChain3* sc) {
    UINT inc = S.dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    auto h = S.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < S.frames.size(); ++i) {
        S.frames[i].buf.Reset();
        sc->GetBuffer(i, IID_PPV_ARGS(&S.frames[i].buf));
        S.frames[i].rtv = h;
        S.dev->CreateRenderTargetView(S.frames[i].buf.Get(), nullptr, h);
        h.ptr += inc;
    }
    S.needFrames = false;
}

void render(IDXGISwapChain* sc) {
    if (S.failed || !S.queue) return;
    if (!S.ready && !init(sc)) return;
    ComPtr<IDXGISwapChain3> sc3;
    if (FAILED(sc->QueryInterface(IID_PPV_ARGS(&sc3)))) return;
    if (S.needFrames) buildFrames(sc3.Get());

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ui::draw();
    ImGui::Render();

    Frame& f = S.frames[sc3->GetCurrentBackBufferIndex()];
    f.alloc->Reset();
    S.cl->Reset(f.alloc.Get(), nullptr);

    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = f.buf.Get();
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    S.cl->ResourceBarrier(1, &b);
    S.cl->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr);
    ID3D12DescriptorHeap* heaps[] = {S.srvHeap.Get()};
    S.cl->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), S.cl.Get());
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    S.cl->ResourceBarrier(1, &b);
    S.cl->Close();
    ID3D12CommandList* lists[] = {S.cl.Get()};
    S.queue->ExecuteCommandLists(1, lists);
}

HRESULT WINAPI hkPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
    render(sc);
    return oPresent(sc, sync, flags);
}
HRESULT WINAPI hkPresent1(IDXGISwapChain1* sc, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* p) {
    render(sc);
    return oPresent1(sc, sync, flags, p);
}
HRESULT WINAPI hkResize(IDXGISwapChain* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT fl) {
    if (S.ready) { S.frames.assign(S.frames.size(), Frame{}); ImGui_ImplDX12_InvalidateDeviceObjects(); }
    HRESULT hr = oResize(sc, n, w, h, f, fl);
    if (S.ready) { ImGui_ImplDX12_CreateDeviceObjects(); S.needFrames = true; }
    return hr;
}
void WINAPI hkExec(ID3D12CommandQueue* q, UINT n, ID3D12CommandList* const* l) {
    if (!S.queue && q->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) S.queue = q;
    oExec(q, n, l);
}

// Throwaway device/queue/swapchain just to read the vtable addresses.
bool findTargets() {
    WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"BMM_Dummy";
    RegisterClassExW(&wc);
    HWND w = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
    bool ok = false;
    do {
        ComPtr<ID3D12Device> dev;
        if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev)))) break;
        D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ComPtr<ID3D12CommandQueue> q;
        if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&q)))) break;
        ComPtr<IDXGIFactory4> fac;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&fac)))) break;
        DXGI_SWAP_CHAIN_DESC1 sd{}; sd.Width = sd.Height = 100; sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> sc;
        if (FAILED(fac->CreateSwapChainForHwnd(q.Get(), w, &sd, nullptr, nullptr, &sc))) break;
        void** sv = *(void***)sc.Get();
        void** qv = *(void***)q.Get();
        tPresent = sv[8]; tResize = sv[13]; tPresent1 = sv[22]; tExec = qv[10];
        ok = true;
    } while (false);
    DestroyWindow(w); UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return ok;
}

} // namespace

bool install() {
    if (!findTargets()) return false;
    if (MH_Initialize() != MH_OK) return false;
    MH_CreateHook(tPresent,  (void*)hkPresent,  (void**)&oPresent);
    MH_CreateHook(tPresent1, (void*)hkPresent1, (void**)&oPresent1);
    MH_CreateHook(tResize,   (void*)hkResize,   (void**)&oResize);
    MH_CreateHook(tExec,     (void*)hkExec,     (void**)&oExec);
    return MH_EnableHook(MH_ALL_HOOKS) == MH_OK;
}

void uninstall() {
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    if (S.ready) {
        SetWindowLongPtrW(S.hwnd, GWLP_WNDPROC, (LONG_PTR)S.oldProc);
        ImGui_ImplDX12_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
        S.ready = false;
    }
}

bool allocSrv(void** cpu, unsigned long long* gpu, unsigned* idx) {
    if (!S.ready || S.srvNext >= kSrvCount) return false;
    auto c = S.srvHeap->GetCPUDescriptorHandleForHeapStart();
    auto g = S.srvHeap->GetGPUDescriptorHandleForHeapStart();
    c.ptr += (SIZE_T)S.srvNext * S.srvInc; g.ptr += (UINT64)S.srvNext * S.srvInc;
    *cpu = (void*)c.ptr; *gpu = g.ptr; *idx = S.srvNext++;
    return true;
}

} // namespace bmm::overlay
