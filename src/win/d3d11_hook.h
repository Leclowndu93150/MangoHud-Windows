#pragma once
#ifndef __D3D11_IMPL_H__
#define __D3D11_IMPL_H__
#include <d3d11.h>
namespace impl
{
	namespace d3d11
	{
		void init();
	}
}
long __stdcall hkPresent11(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
HRESULT __stdcall hkResizeBuffers11(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);

#endif // __D3D11_IMPL_H__
