#pragma once
#include <dxgi.h>
#include <dxgi1_5.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#ifndef __D3D12_IMPL_H__
#define __D3D12_IMPL_H__

namespace impl
{
	namespace d3d12
	{
		void init();
	}
}

long __fastcall hkPresent12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags);
void __fastcall hkExecuteCommandLists12(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists);
HRESULT __stdcall hkResizeBuffers12(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);

#endif // __D3D12_IMPL_H__
