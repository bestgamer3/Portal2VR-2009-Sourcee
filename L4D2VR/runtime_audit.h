#pragma once

// Read-only runtime diagnostics for the supplied July 2009 Source binaries.
// This header deliberately does not install hooks or call interface methods.

#include <Windows.h>
#include <Psapi.h>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

struct RuntimeAuditModule
{
    std::string name;
    HMODULE handle = nullptr;
    MODULEINFO info{};
};

inline bool RuntimeAuditIsReadable(const void* address, std::size_t bytes = sizeof(void*))
{
    if (!address || bytes == 0)
        return false;

    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(address, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
        return false;

    if ((mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
        return false;

    const auto start = reinterpret_cast<std::uintptr_t>(address);
    const auto regionStart = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
    if (start < regionStart || bytes > mbi.RegionSize ||
        start - regionStart > mbi.RegionSize - bytes)
        return false;

    return true;
}

inline bool RuntimeAuditIsExecutable(const void* address)
{
    if (!address)
        return false;

    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(address, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
        return false;

    if ((mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;

    const DWORD protection = mbi.Protect & 0xFF;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
           protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

inline std::vector<RuntimeAuditModule> RuntimeAuditCollectModules()
{
    static const char* names[] = {
        "client.dll", "engine.dll", "server.dll", "materialsystem.dll",
        "vgui2.dll", "vguimatsurface.dll", "tier0.dll", "vstdlib.dll"
    };

    std::vector<RuntimeAuditModule> modules;
    for (const char* name : names)
    {
        HMODULE module = GetModuleHandleA(name);
        if (!module)
            continue;

        RuntimeAuditModule entry;
        entry.name = name;
        entry.handle = module;
        if (GetModuleInformation(GetCurrentProcess(), module, &entry.info, sizeof(entry.info)))
            modules.push_back(entry);
    }
    return modules;
}

inline std::string RuntimeAuditDescribeAddress(
    const void* address, const std::vector<RuntimeAuditModule>& modules)
{
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    for (const RuntimeAuditModule& module : modules)
    {
        const auto base = reinterpret_cast<std::uintptr_t>(module.info.lpBaseOfDll);
        const auto size = static_cast<std::uintptr_t>(module.info.SizeOfImage);
        if (value >= base && value - base < size)
        {
            std::ostringstream text;
            text << module.name << "+0x" << std::hex << (value - base);
            return text.str();
        }
    }
    return RuntimeAuditIsExecutable(address) ? "executable address (module not in short list)" : "not executable";
}

inline void RuntimeAuditDumpModuleTable(
    std::ostream& out, const std::vector<RuntimeAuditModule>& modules)
{
    out << "Loaded target modules:\n";
    for (const RuntimeAuditModule& module : modules)
    {
        out << "  " << module.name << " base=0x" << std::hex
            << reinterpret_cast<std::uintptr_t>(module.info.lpBaseOfDll)
            << " imageSize=0x" << module.info.SizeOfImage << std::dec << "\n";
    }
}

inline void RuntimeAuditDumpInterface(
    std::ostream& out,
    const char* interfaceName,
    const void* object,
    std::size_t maxSlots,
    const std::vector<RuntimeAuditModule>& modules)
{
    out << "\nInterface " << interfaceName << ": ";
    if (!object)
    {
        out << "not created\n";
        return;
    }

    out << "object=0x" << std::hex << reinterpret_cast<std::uintptr_t>(object) << std::dec;
    if (!RuntimeAuditIsReadable(object))
    {
        out << " (object memory is not readable)\n";
        return;
    }

    const auto* vtable = *reinterpret_cast<void* const* const*>(object);
    if (!vtable || !RuntimeAuditIsReadable(vtable))
    {
        out << " (vtable pointer is not readable)\n";
        return;
    }

    out << " vtable=0x" << std::hex << reinterpret_cast<std::uintptr_t>(vtable) << std::dec << "\n";
    std::size_t emitted = 0;
    for (std::size_t index = 0; index < maxSlots; ++index)
    {
        const void* const* slot = vtable + index;
        if (!RuntimeAuditIsReadable(slot))
            break;

        const void* function = *slot;
        if (!RuntimeAuditIsExecutable(function))
            break;

        out << "  [" << std::setw(3) << std::setfill('0') << index << "] 0x"
            << std::hex << std::setw(8) << std::setfill('0')
            << reinterpret_cast<std::uintptr_t>(function) << std::dec
            << "  " << RuntimeAuditDescribeAddress(function, modules) << "\n";
        ++emitted;
    }

    out << "  slots dumped: " << emitted << " (read-only; no calls made)\n";
}


inline bool RuntimeAuditVerifyInterfaceSlot(
    std::ostream& out,
    const char* interfaceName,
    const void* object,
    std::size_t slotIndex,
    const void* expectedFunction,
    const std::vector<RuntimeAuditModule>& modules)
{
    out << "\nRender hook target verification for " << (interfaceName ? interfaceName : "(unknown)")
        << " slot " << slotIndex << ": ";

    if (!object || !RuntimeAuditIsReadable(object))
    {
        out << "FAIL - interface object is not readable\n";
        return false;
    }

    const auto* vtable = *reinterpret_cast<void* const* const*>(object);
    if (!vtable || !RuntimeAuditIsReadable(vtable))
    {
        out << "FAIL - vtable pointer is not readable\n";
        return false;
    }

    const void* const* slot = vtable + slotIndex;
    if (!RuntimeAuditIsReadable(slot))
    {
        out << "FAIL - vtable slot is not readable\n";
        return false;
    }

    const void* actualFunction = *slot;
    if (!RuntimeAuditIsExecutable(actualFunction))
    {
        out << "FAIL - vtable slot does not point to executable memory\n";
        return false;
    }

    out << (actualFunction == expectedFunction ? "MATCH" : "MISMATCH")
        << " expected=0x" << std::hex
        << reinterpret_cast<std::uintptr_t>(expectedFunction)
        << " actual=0x" << reinterpret_cast<std::uintptr_t>(actualFunction)
        << std::dec
        << " actual-module=" << RuntimeAuditDescribeAddress(actualFunction, modules)
        << " expected-module=" << RuntimeAuditDescribeAddress(expectedFunction, modules)
        << "\n";

    return actualFunction == expectedFunction;
}
