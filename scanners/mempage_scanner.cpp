#include "mempage_scanner.h"
#include "module_data.h"
#include "artefact_scanner.h"

#include "../utils/path_converter.h"
#include "../utils/workingset_enum.h"

#define PE_NOT_FOUND 0



bool MemPageScanner::isCode(MemPageData &memPageData)
{
	if (memPageData.loadedData == nullptr) {
		if (!memPageData.loadRemote()) return false;
		if (memPageData.loadedData == nullptr) return false;
	}

	BYTE prolog32_pattern[] = { 0x55, 0x8b, 0xEC };
	BYTE prolog64_pattern[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20 };

	size_t prolog32_size = sizeof(prolog32_pattern);
	size_t prolog64_size = sizeof(prolog64_pattern);
	bool is32bit = false;

	BYTE* buffer = memPageData.loadedData;

	bool pattern_found = false;
	for (size_t i = 0; (i + prolog64_size) < memPageData.loadedSize; i++) {
		if (memcmp(buffer + i, prolog32_pattern, prolog32_size) == 0) {
			pattern_found = true;
			is32bit = true;
#ifdef _DEBUG
			std::cout << std::hex << memPage.region_start << ": contains 32bit shellcode" << std::endl;
#endif
			break;
		}
		if (memcmp(buffer + i, prolog64_pattern, prolog64_size) == 0) {
			pattern_found = true;
#ifdef _DEBUG
			std::cout << std::hex << memPage.region_start << " : contains 64bit shellcode" << std::hex << memPage.region_start << std::endl;
#endif
			break;
		}
	}
	return pattern_found;
}

MemPageScanReport* MemPageScanner::scanShellcode(MemPageData &memPageData)
{
	if (memPage.loadedData == nullptr) {
		return nullptr;
	}
	//shellcode found! now examin it with more details:
	ArtefactScanner artefactScanner(this->processHandle, memPage);
	MemPageScanReport *my_report = artefactScanner.scanRemote();
	if (my_report) {
#ifdef _DEBUG
		std::cout << "The detected shellcode is probably a corrupt PE" << std::endl;
#endif
		return my_report;
	}
	//just a regular shellcode...

	if (!this->detectShellcode) {
		// not a PE file, and we are not interested in shellcode, so just finish it here
		return nullptr;
	}
	//report about shellcode:
	ULONGLONG region_start = memPage.region_start;
	const size_t region_size = size_t (memPage.region_end - region_start);
	my_report = new MemPageScanReport(processHandle, (HMODULE)region_start, region_size, SCAN_SUSPICIOUS);
	my_report->is_executable = true;
	my_report->is_manually_loaded = !memPage.is_listed_module;
	my_report->protection = memPage.protection;
	my_report->is_shellcode = true;
	return my_report;
}

MemPageScanReport* MemPageScanner::scanRemote()
{
	if (!memPage.isInfoFilled() && !memPage.fillInfo()) {
		return nullptr;
	}
	if (memPage.mapping_type == MEM_IMAGE) {
		//probably legit
		return nullptr;
	}

	// is the page executable?
	bool is_any_exec = (memPage.initial_protect & PAGE_EXECUTE_READWRITE)
		|| (memPage.initial_protect & PAGE_EXECUTE_READ)
		|| (memPage.initial_protect & PAGE_EXECUTE)
		|| (memPage.protection & PAGE_EXECUTE_READWRITE)
		|| (memPage.protection & PAGE_EXECUTE_READ)
		|| (memPage.initial_protect & PAGE_EXECUTE);

	if (!is_any_exec && memPage.is_listed_module) {
		// the header is not executable + the module was already listed - > probably not interesting
#ifdef _DEBUG
		std::cout << std::hex << memPage.start_va << " : Aleady listed" << std::endl;
#endif
		return nullptr;
	}
	if (is_any_exec && (memPage.mapping_type == MEM_PRIVATE ||
		(memPage.mapping_type == MEM_MAPPED && !memPage.isRealMapping())))
	{
//#ifdef _DEBUG
		std::cout << std::hex << memPage.start_va << " : Checking for shellcode" << std::endl;
//#endif
		if (isCode(memPage)) {
			std::cout << "Code pattern found, scanning.." << std::endl;
			return this->scanShellcode(memPage);
		}
	}
	return nullptr;
}
