#pragma once

#include <Windows.h>
#include <Psapi.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

// Safe PE32 signature resolver for the 32-bit Source-engine modules used by
// Portal 2's July 2009 build. Signatures are searched only in executable
// sections; matching bytes in .rdata/.data are not valid function targets.
// A signature is accepted only when exactly one executable-section match exists.
class SigScanner
{
public:
    static bool VerifyOffset(const std::string& moduleName,
                             int currentOffset,
                             const std::string& signature,
                             int sigOffset,
                             int& resolvedOffset,
                             std::string& failure)
    {
        resolvedOffset = 0;
        failure.clear();

        HMODULE module = GetModuleHandleA(moduleName.c_str());
        if (!module)
        {
            failure = "module is not loaded";
            return false;
        }

        MODULEINFO moduleInfo{};
        if (!GetModuleInformation(GetCurrentProcess(), module, &moduleInfo, sizeof(moduleInfo)) ||
            !moduleInfo.lpBaseOfDll || moduleInfo.SizeOfImage < sizeof(IMAGE_DOS_HEADER))
        {
            failure = "could not read module image information";
            return false;
        }

        const auto* base = static_cast<const std::uint8_t*>(moduleInfo.lpBaseOfDll);
        const std::size_t imageSize = static_cast<std::size_t>(moduleInfo.SizeOfImage);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
            static_cast<std::size_t>(dos->e_lfanew) > imageSize - sizeof(IMAGE_NT_HEADERS32))
        {
            failure = "invalid DOS/NT header in loaded module";
            return false;
        }

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        {
            failure = "module is not a valid PE32 image";
            return false;
        }

        const std::size_t sectionTableOffset = static_cast<std::size_t>(dos->e_lfanew) +
            offsetof(IMAGE_NT_HEADERS32, OptionalHeader) + nt->FileHeader.SizeOfOptionalHeader;
        const std::size_t sectionTableBytes =
            static_cast<std::size_t>(nt->FileHeader.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);
        if (sectionTableOffset > imageSize || sectionTableBytes > imageSize - sectionTableOffset)
        {
            failure = "invalid PE section table";
            return false;
        }

        std::vector<int> pattern;
        std::stringstream stream(signature);
        std::string token;
        while (stream >> token)
        {
            if (token == "?" || token == "??")
            {
                pattern.push_back(-1);
                continue;
            }

            char* end = nullptr;
            const unsigned long value = std::strtoul(token.c_str(), &end, 16);
            if (!end || end == token.c_str() || *end != '\0' || value > 0xFF)
            {
                failure = "signature contains an invalid byte token";
                return false;
            }
            pattern.push_back(static_cast<int>(value));
        }

        if (pattern.empty())
        {
            failure = "signature is empty";
            return false;
        }

        // Select the longest fixed-byte run as the search anchor.
        std::size_t anchorStart = 0;
        std::size_t anchorLength = 0;
        for (std::size_t i = 0; i < pattern.size();)
        {
            if (pattern[i] < 0)
            {
                ++i;
                continue;
            }

            const std::size_t start = i;
            while (i < pattern.size() && pattern[i] >= 0)
                ++i;

            if (i - start > anchorLength)
            {
                anchorStart = start;
                anchorLength = i - start;
            }
        }

        if (anchorLength == 0)
        {
            failure = "signature contains no fixed bytes";
            return false;
        }

        std::vector<std::uint8_t> anchor;
        anchor.reserve(anchorLength);
        for (std::size_t i = 0; i < anchorLength; ++i)
            anchor.push_back(static_cast<std::uint8_t>(pattern[anchorStart + i]));

        const auto matchesAt = [&](const std::uint8_t* bytes, std::size_t sectionSize,
                                   std::size_t position) -> bool
        {
            if (position > sectionSize || pattern.size() > sectionSize - position)
                return false;
            for (std::size_t i = 0; i < pattern.size(); ++i)
            {
                if (pattern[i] >= 0 && bytes[position + i] != static_cast<std::uint8_t>(pattern[i]))
                    return false;
            }
            return true;
        };

        const auto* firstSection = reinterpret_cast<const IMAGE_SECTION_HEADER*>(
            base + sectionTableOffset);

        // Verify the supplied RVA first, but only if it points into executable code.
        if (currentOffset >= sigOffset)
        {
            const std::size_t signatureRva = static_cast<std::size_t>(currentOffset - sigOffset);
            for (unsigned int i = 0; i < nt->FileHeader.NumberOfSections; ++i)
            {
                const IMAGE_SECTION_HEADER& section = firstSection[i];
                if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                    continue;

                const std::size_t sectionRva = section.VirtualAddress;
                std::size_t sectionSize = section.Misc.VirtualSize;
                if (sectionSize == 0)
                    sectionSize = section.SizeOfRawData;
                if (sectionRva > imageSize)
                    continue;
                if (sectionSize > imageSize - sectionRva)
                    sectionSize = imageSize - sectionRva;

                if (signatureRva >= sectionRva &&
                    signatureRva - sectionRva < sectionSize &&
                    matchesAt(base + sectionRva, sectionSize, signatureRva - sectionRva))
                {
                    resolvedOffset = currentOffset;
                    return true;
                }
            }
        }

        std::size_t matchRva = 0;
        unsigned int matchCount = 0;
        for (unsigned int i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        {
            const IMAGE_SECTION_HEADER& section = firstSection[i];
            if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                continue;

            const std::size_t sectionRva = section.VirtualAddress;
            std::size_t sectionSize = section.Misc.VirtualSize;
            if (sectionSize == 0)
                sectionSize = section.SizeOfRawData;
            if (sectionRva > imageSize)
                continue;
            if (sectionSize > imageSize - sectionRva)
                sectionSize = imageSize - sectionRva;
            if (sectionSize < pattern.size())
                continue;

            const std::uint8_t* sectionBytes = base + sectionRva;
            const std::uint8_t* searchBegin = sectionBytes;
            const std::uint8_t* searchEnd = sectionBytes + sectionSize;
            while (searchBegin + anchorLength <= searchEnd)
            {
                const std::uint8_t* found = std::search(
                    searchBegin, searchEnd, anchor.begin(), anchor.end());
                if (found == searchEnd || found + anchorLength > searchEnd)
                    break;

                const std::ptrdiff_t anchorPosition = found - sectionBytes;
                const std::ptrdiff_t candidatePosition =
                    anchorPosition - static_cast<std::ptrdiff_t>(anchorStart);
                if (candidatePosition >= 0 && matchesAt(sectionBytes, sectionSize,
                    static_cast<std::size_t>(candidatePosition)))
                {
                    matchRva = sectionRva + static_cast<std::size_t>(candidatePosition);
                    ++matchCount;
                    if (matchCount > 1)
                        break;
                }
                searchBegin = found + 1; // allow overlapping matches
            }
            if (matchCount > 1)
                break;
        }

        if (matchCount == 0)
        {
            failure = "signature not found in executable sections";
            return false;
        }
        if (matchCount != 1)
        {
            failure = "signature is ambiguous in executable sections";
            return false;
        }

        const long long adjusted = static_cast<long long>(matchRva) + sigOffset;
        if (adjusted < 0 || adjusted > 0x7FFFFFFFLL)
        {
            failure = "resolved RVA is out of range";
            return false;
        }

        resolvedOffset = static_cast<int>(adjusted);
        return true;
    }
};
