// dllmain.cpp : Definiuje punkt wejścia dla aplikacji DLL.
#include "pch.h"
#include <Windows.h>
#include <cstdio>
#include <cstdint>

//=======================================
//==============Shared Memory============
//=======================================

#define MAX_ENTITIES 1024

struct SharedEntity
{
    uintptr_t ptr;
    int type;
    int variant;
    int id;
};

struct SharedBuffer
{
    volatile LONG writeIndex;
    volatile LONG publishIndex;
    SharedEntity entities[MAX_ENTITIES];
};

SharedBuffer* g_shared = nullptr;
HANDLE g_hMap = nullptr;

//=======================================
//=================hooks================
//=======================================
uintptr_t returnAddressHookEntityPointer;
uintptr_t returnAddressHookEntityValues;

bool hookEntityPointerTriggered = false;
bool hookEntityValuesTriggered = false;

void HookEntityPointer();
void HookEntityValues();
void InstallHook(uintptr_t hookAddr, void* hookFunc, int len, uintptr_t* returnAddr);
void InitSharedMemory();
void __stdcall PushEntity(uintptr_t ptr, int type, int variant, int id);
inline bool IsInteresting(int type, int variant);

// ================= ENTITY CACHE =================
struct EntityState
{
    uintptr_t ptr;   // entity base

    int type;        // +0x28
    int variant;     // +0x2C
    int id;          // +0x30
};

EntityState g_entity;

DWORD WINAPI MainThread(LPVOID lpParam)
{
    Sleep(2000);
    InitSharedMemory();

    g_shared->writeIndex += 1;

    if (g_shared->writeIndex == -1)
    {
        MessageBoxA(0, "Memory NOT WORKING", "SHM", 0);
    }
    else
    {
        MessageBoxA(0, "Memory IS WORKING", "SHM", 0);
    }

    uintptr_t base = (uintptr_t)GetModuleHandle(NULL);

    char buf[100];
    sprintf_s(buf, sizeof(buf), "Base: %p", (void*)base);
    MessageBoxA(0, buf, "INFO", 0);

    uintptr_t hookAddr1 = base + 0x2A52A9;
    InstallHook(hookAddr1, HookEntityPointer, 6, &returnAddressHookEntityPointer);

    uintptr_t hookAddr2 = base + 0x2A52B4;
    InstallHook(hookAddr2, HookEntityValues, 5, &returnAddressHookEntityValues);
    /*
    while (true)
    {
        if (hookEntityPointerTriggered)
        {
            char buf[128];
            sprintf_s(
                buf,
                "ENTITY -> ESI: %p | Type: %d | Variant: %d | ID: %d",
                (void*)g_entity.ptr,
                g_entity.type,
                g_entity.variant,
                g_entity.id
            );
            MessageBoxA(0, buf, "EntityPointer", 0);

            hookEntityPointerTriggered = false;
        }

        if (hookEntityValuesTriggered)
        {
            char buf[128];
            sprintf_s(
                buf,
                "ENTITY -> ESI: %p | Type: %d | Variant: %d | ID: %d",
                (void*)g_entity.ptr,
                g_entity.type,
                g_entity.variant,
                g_entity.id
            );
            MessageBoxA(0, buf, "Entity Values", 0);

            hookEntityValuesTriggered = false;
        }

        Sleep(100); // krótszy sleep → responsywnie
    }*/

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved
)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        MessageBoxA(0, "DLL ATTACHED", "OK", 0);
        CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

void InstallHook(uintptr_t hookAddr, void* hookFunc, int len, uintptr_t* returnAddr)
{
    DWORD oldProtect;

    // ustaw RWX
    VirtualProtect((LPVOID)hookAddr, len, PAGE_EXECUTE_READWRITE, &oldProtect);

    // zapis powrotu
    *returnAddr = hookAddr + len;

    // wyliczenie JMP (relatywny)
    DWORD relAddr = (DWORD)((uintptr_t)hookFunc - hookAddr - 5);

    // wpisanie JMP
    *(BYTE*)hookAddr = 0xE9;
    *(DWORD*)(hookAddr + 1) = relAddr;

    // NOPy na resztę
    for (int i = 5; i < len; i++) {
        *(BYTE*)(hookAddr + i) = 0x90;
    }

    // przywrócenie ochrony
    VirtualProtect((LPVOID)hookAddr, len, oldProtect, &oldProtect);
}

void InitSharedMemory()
{
    g_hMap = CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        sizeof(SharedBuffer),
        "Local\\MyEntitySharedMemory"
    );

    bool isNew = (GetLastError() != ERROR_ALREADY_EXISTS);

    if (!g_hMap)
    {
        MessageBoxA(0, "CreateFileMapping FAILED", "SHM", 0);
        return;
    }

    g_shared = (SharedBuffer*)MapViewOfFile(
        g_hMap,
        FILE_MAP_ALL_ACCESS,
        0, 0,
        sizeof(SharedBuffer)
    );

    if (!g_shared)
    {
        MessageBoxA(0, "MapViewOfFile FAILED", "SHM", 0);
        return;
    }

    // init tylko raz

    if (isNew)
    {
        g_shared->writeIndex = -1;
        g_shared->publishIndex = -1;
    }

    MessageBoxA(0, "SHARED MEMORY OK", "SHM", 0);
}

void __stdcall PushEntity(uintptr_t ptr, int type, int variant, int id)
{
    if (!g_shared)
        return;

    if (!IsInteresting(type, variant))
        return;

    LONG index = InterlockedIncrement(&g_shared->writeIndex);

    LONG slot = index % MAX_ENTITIES;

    g_shared->entities[slot].ptr = ptr;
    g_shared->entities[slot].type = type;
    g_shared->entities[slot].variant = variant;
    g_shared->entities[slot].id = id;

    InterlockedExchange(&g_shared->publishIndex, index);
}

inline bool IsInteresting(int type, int variant)
{
    if (type != 5)
        return false;

    return (variant == 100 || variant == 300 || variant == 350);
}

__declspec(naked) void HookEntityPointer()
{
    __asm {
        pushfd
        pushad

        // tu debug/log
        mov eax, esi
        mov [g_entity.ptr], eax

        mov ecx, eax

        mov edx, [ecx + 0x28]
        mov [g_entity.type], edx

        mov edx, [ecx + 0x2C]
        mov [g_entity.variant], edx

        mov edx, [ecx + 0x30]
        mov [g_entity.id], edx

        //mov byte ptr[hookEntityPointerTriggered], 1

        popad
        popfd

        mov[esi + 0x2B4], eax

        jmp dword ptr[returnAddressHookEntityPointer]
    }
}

__declspec(naked) void HookEntityValues()
{
    __asm {
        pushfd
        pushad

        mov eax, [g_entity.ptr]

        mov ecx, eax

        mov edx, [ecx + 0x30]
        //mov[g_entity.id], edx
        push edx                // id

        mov edx, [ecx + 0x2C]
        //mov[g_entity.variant], edx
        push edx                // variant

        mov edx, [ecx + 0x28]
        //mov [g_entity.type], edx
        push edx                // type

        //mov byte ptr[hookEntityValuesTriggered], 1

        push eax                // ptr
         
        call PushEntity

        popad
        popfd

        // odtworzenie oryginalnych instrukcji
        mov ebx, dword ptr ss : [esp + 0xC]
        push esi

        // powrót
        jmp dword ptr[returnAddressHookEntityValues]
    }
}