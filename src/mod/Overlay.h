#pragma once
namespace bmm::overlay {
bool install();   // hooks D3D12 Present/ExecuteCommandLists, starts ImGui on first frame
void uninstall();
// Descriptor slot allocator for textures (mod icons). Slot 0 is the ImGui font.
bool allocSrv(void** outCpuPtr, unsigned long long* outGpuPtr, unsigned* outIndex);
}
