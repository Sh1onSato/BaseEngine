#include "ImGuiManager.h"
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#include <cassert>

ImGuiManager* ImGuiManager::instance = nullptr;

ImGuiManager* ImGuiManager::GetInstance() {
    if (instance == nullptr) {
        instance = new ImGuiManager();
    }
    return instance;
}

void ImGuiManager::Initialize(WinApp* winApp, DirectXCommon* dxCommon, SrvManager* srvManager) {
    this->dxCommon = dxCommon;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    if (!ImGui_ImplWin32_Init(winApp->GetHwnd())) {
        assert(false && "ImGui_ImplWin32_Init failed");
    }

    ImGui_ImplDX12_InitInfo init_info = {};
    init_info.Device = dxCommon->GetDevice();
    init_info.CommandQueue = dxCommon->GetCommandQueue();
    init_info.NumFramesInFlight = (int)dxCommon->GetBackBufferCount();
    init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    init_info.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    init_info.SrvDescriptorHeap = srvManager->GetDescriptorHeap();
    
    // コールバック関数の設定
    init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle) {
        SrvManager* mgr = (SrvManager*)info->UserData;
        uint32_t index = mgr->Allocate();
        *out_cpu_desc_handle = mgr->GetCPUDescriptorHandle(index);
        *out_gpu_desc_handle = mgr->GetGPUDescriptorHandle(index);
    };
    init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_desc_handle) {
        // 解放処理（現状は空）
    };
    init_info.UserData = srvManager;

    if (!ImGui_ImplDX12_Init(&init_info)) {
        assert(false && "ImGui_ImplDX12_Init failed");
    }
}

void ImGuiManager::Begin() {
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGuiManager::End() {
    ImGui::Render();
}

void ImGuiManager::Draw() {
    ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
}

void ImGuiManager::Finalize() {
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    delete instance;
    instance = nullptr;
}
