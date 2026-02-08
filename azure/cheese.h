#pragma once
#include "sdk.h"
#include <iostream>
#include "offsets.h"
using namespace Globals;
using namespace Camera;
using namespace UE4;

GWorld* UWorld;
GGameInstance* UGameInstance;
GLocalPlayer* ULocalPlayer;
GPlayerController* APlayerController;
GPawn* APawn;
GPrivatePawn* APrivatePawn;
GULevel* ULevel;
GUSkeletalMeshComponent* USkeletalMeshComponent;

// Pointer decryption state
uint64_t g_pointer_state[7] = {0};
bool g_state_initialized = false;

bool cached = false;
uintptr_t WorldPtr;
uintptr_t PML4Base = 0;
#define uint unsigned int
#define ushort unsigned short
#define ulong unsigned long
struct FNameEntryHandle {
	uint16_t bIsWide : 1;
	uint16_t Len : 15;
};
struct FNameEntry {

	int32_t ComparisonId;
	FNameEntryHandle Header;
	union
	{
		char    AnsiName[1024]; 
		wchar_t    WideName[1024];
	};


	wchar_t const* GetWideName() const { return WideName; }
	char const* GetAnsiName() const { return AnsiName; }
	bool IsWide() const { return Header.bIsWide; }
};


bool IsCoreIsolationEnabled()
{
	
	HKEY hKey;
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity", 0, KEY_READ, &hKey) != ERROR_SUCCESS)
	{
		return false;
	}

	
	DWORD enabled = 0;
	DWORD size = sizeof(enabled);
	if (RegQueryValueExA(hKey, "Enabled", nullptr, nullptr, reinterpret_cast<LPBYTE>(&enabled), &size) != ERROR_SUCCESS)
	{
		RegCloseKey(hKey);
		return false;
	}


	RegCloseKey(hKey);


	return (enabled != 0);
}
typedef struct ShadowRegionsDataStructure
{
	uintptr_t OriginalPML4_t;
	uintptr_t ClonedPML4_t;
	uintptr_t GameCr3;
	uintptr_t ClonedCr3;
	uintptr_t FreeIndex;
} ShadowRegionsDataStructure;
typedef struct _gr {
	ULONGLONG* guarded;
} gr, * pgr;
inline uint64_t ROL8(uint64_t value, unsigned int shift) {
	const unsigned int mask = 63; 
	shift &= mask;             
	return (value << shift) | (value >> (64 - shift));
}
__forceinline uint64_t decrypt_xor_keys(const uint32_t key, const uint64_t* state)
{
    uint64_t hash = 0x2545F4914F6CDD1Dui64
        * (key ^ ((key ^ (key >> 15)) >> 12) ^ (key << 25));
 
    uint64_t idx = hash % 7;
    uint64_t val = state[idx];
    uint32_t hi = (uint32_t)(hash >> 32);
    uint32_t mod7 = (uint32_t)idx;
 
    if (mod7 == 0)
    {
        // ROR then subtract hi
        uint8_t q = (uint8_t)(((int)hi - 1) / 0x3F);
        uint8_t rshift = (uint8_t)hi - 63 * q;
        uint8_t lshift = 63 * q - ((uint8_t)hi - 1) + 63;
        val = ((val >> rshift) | (val << lshift)) - hi;
    }
    else if (mod7 == 1)
    {
        // ROL then add (hi + idx)
        uint32_t rot = hi + 2 * (uint32_t)idx;
        uint8_t lshift = (uint8_t)(rot % 0x3F) + 1;
        uint8_t rshift = 63 * (uint8_t)(rot / 0x3F) - (uint8_t)hi - 2 * (uint8_t)idx + 63;
        val = ((val << lshift) | (val >> rshift)) + (uint32_t)(hi + idx);
    }
    else if (mod7 == 3)
    {
        // NOT(ROR)
        uint32_t rot = hi + 2 * (uint32_t)idx;
        uint8_t rshift = (uint8_t)(rot % 0x3F) + 1;
        uint8_t lshift = 63 * (uint8_t)(rot / 0x3F) - (uint8_t)hi - 2 * (uint8_t)idx + 63;
        val = ~((val >> rshift) | (val << lshift));
    }
    else if (mod7 == 4)
    {
        // XOR with (hi + idx)
        val = val ^ (uint32_t)(hi + idx);
    }
    else if (mod7 == 5)
    {
        // Single bit-swap then XOR with ~(hi + idx)
        val = (val >> 1) ^ ~(uint64_t)(uint32_t)(hi + idx) ^ (((val >> 1) ^ (2 * val)) & 0xAAAAAAAAAAAAAAAAui64);
    }
 
    return val ^ key;
}

// Initialize pointer decryption state
void init_pointer_state(uintptr_t base_address, uintptr_t gworld) {
    if (!g_state_initialized) {
        printf("[*] Initializing pointer decryption state...\n");
        printf("[*] Base address: %p\n", (void*)base_address);
        printf("[*] GWorld: %p\n", (void*)gworld);
        printf("[*] PointerState offset: 0x%X\n", offsets::PointerState);
        printf("[*] PointerKey offset: 0x%X\n", offsets::PointerKey);
        
        // Read the XOR key from the fixed offset
        uintptr_t key_location = base_address + offsets::PointerKey;
        uint64_t xor_key = driver.readv<uint64_t>(key_location);
        printf("[*] XOR Key (encrypted): 0x%llX\n", xor_key);
        
        // Decrypt the XOR key with GWorld
        uint64_t decrypted_xor_key = xor_key ^ gworld;
        printf("[*] XOR Key (decrypted with GWorld): 0x%llX\n", decrypted_xor_key);
        
        // Read the encryption state array directly (7 uint64_t values)
        // The state is stored at PointerState offset, encrypted with GWorld
        uintptr_t state_location = base_address + offsets::PointerState;
        printf("[*] Reading state array from: 0x%p\n", (void*)state_location);
        
        bool success = true;
        uint64_t decrypted_state[7] = {0};
        
        for (int i = 0; i < 7; i++) {
            uintptr_t read_addr = state_location + (i * 8);
            try {
                uint64_t encrypted_value = driver.readv<uint64_t>(read_addr);
                printf("[*] State[%d] (encrypted) from 0x%p: 0x%llX\n", i, (void*)read_addr, encrypted_value);
                
                // Decrypt with GWorld (XOR)
                decrypted_state[i] = encrypted_value ^ gworld;
                g_pointer_state[i] = decrypted_state[i];
                printf("[*] State[%d] (decrypted): 0x%llX\n", i, decrypted_state[i]);
                
                // Check if all values are the same (likely error condition)
                if (i > 0 && decrypted_state[i] == decrypted_state[i-1]) {
                    printf("[!] WARNING: Decrypted state values are identical! This may indicate memory read failure.\n");
                    success = false;
                    break;
                }
            }
            catch (...) {
                printf("[-] ERROR: Failed to read state[%d] from 0x%p\n", i, (void*)read_addr);
                success = false;
                break;
            }
        }
        
        if (success) {
            g_state_initialized = true;
            printf("[+] Pointer decryption state initialized successfully\n");
        } else {
            printf("[-] Failed to initialize pointer decryption state\n");
            g_state_initialized = false;
        }
    } else {
        printf("[*] State already initialized\n");
    }
}


uintptr_t DecryptClonedCr3(ShadowRegionsDataStructure SR)
{
	uintptr_t m_VgkAddr = (uintptr_t)driver.get_module_base("vgk.sys");
	const uint8_t b = driver.readv<BYTE>(m_VgkAddr + 0x71C10);
	const uint64_t key = driver.readv<uint64_t>(m_VgkAddr + 0x71CD0);
	const uintptr_t cr3 = SR.ClonedCr3;

	const uint64_t inv_b = ~static_cast<uint64_t>(b);
	const uint64_t masked_and = static_cast<uint64_t>(b & cr3);
	const uint64_t masked_or = cr3 | inv_b;

	const int64_t term1 = (masked_and - masked_or);
	const int64_t term2 = (cr3 ^ b);
	const int64_t term3 = -2 * (b ^ (cr3 | b));
	const int64_t term4 = 2 * (((b | 0xE8ull) ^ 0xFFFFFFFFFFFFFF17ull) + (~cr3 | (b ^ 0xE8ull)));
	const int64_t term5 = -static_cast<int64_t>(b ^ (~cr3 ^ 0xE8ull));
	const int64_t term6 = -3 * ~cr3;
	const int64_t term7 = -232;

	const int64_t multiplierA = term2 + term3 + term4 + term5 + term6 + term7;
	const uintptr_t v5 = (0x49B74B6480000000ull * cr3 + 0xC2D8B464B4418C6Cull * inv_b + 0x66B8CDC1FFFFFFFFull * b + 0x5C1FE6A2B4418C6Dull * term1) * multiplierA;
	const uint64_t innerMul = 0x13D0F34E00000000ull * cr3 + 0x483C4F8900000000ull;
	const uint64_t cr3MulInner = cr3 * innerMul;
	const uint64_t v6_part1 = ((cr3 ^ cr3MulInner) << 63);
	const uint64_t v6_part2 = cr3 * (0x7D90DC33C620C593ull * innerMul + 0x8000000000000000ull);
	const uint64_t term_cr3_qword = 0x55494E5B80000000ull * key + 0xC83B18136241A38Dull * ~key + 0xCE3CE5E180000000ull * ~cr3 + 0x72F1C9B7E241A38Dull * ((key | 0xE8ull) - (231ull - (key & 0xE8ull)));
	const uint64_t term_key_masked = 0x99BF7D2380CF6EC3ull * key + 0x664082DC7F30913Eull * (cr3 | b) + 0x19BF7D2380CF6EC2ull * ~key + 0xE64082DC7F30913Eull * (~cr3 & ~static_cast<uint64_t>(b)) + ((cr3 + (b & (key ^ cr3)) + (key | b)) << 63);
	const uintptr_t v6 = v6_part1 + v6_part2 + (cr3 * term_cr3_qword + 0x71C31A1E80000000ull) * term_key_masked;
	const uint64_t common_expr = 0x8000000000000001ull * key + 0x2FDBF65F8A4AC9C9ull * cr3 + ((key ^ cr3) << 63) + 0x502409A075B53637ull * cr3;
	const uint64_t inner_expr = 0xFD90DC33C620C592ull * ~cr3MulInner + v6 + 0x2183995CC620C592ull;
	const uintptr_t result = 0x137FEEF6AB38CFB4ull * v5 + ((~v5 ^ ~(common_expr * inner_expr)) << 63) + 0x6C80110954C7304Dull * ((v5 & key) - (~key & ~v5) - key) - 0x7FFFFFFFFFFFFFFFull * common_expr * inner_expr - 0x4F167C5CD4C7304Eull;
	return result;
}
uintptr_t context_cr3;
struct ShadowData
{
	uintptr_t DecryptedClonedCr3;
	uintptr_t PML4Base;
};


ShadowData GetVgkShadowData(uintptr_t VgkAddress)
{
	ShadowRegionsDataStructure Data = driver.readv<ShadowRegionsDataStructure>(VgkAddress + 0x71B48);
	printf("ClonedCr3: %p\n", Data.ClonedCr3);
	printf("FreeIndex: %p\n", Data.FreeIndex);

	uintptr_t DecryptedCr3 = DecryptClonedCr3(Data);

	auto dirbase = (PVOID)DecryptedCr3;

	printf("DecryptedCr3: %p\n", DecryptedCr3);
	return ShadowData{ DecryptedCr3, Data.FreeIndex << 0x27 };
}
uintptr_t GetPML4Base()
{
	uintptr_t m_VgkAddr = (uintptr_t)driver.get_module_base("vgk.sys");
	printf("m_VgkAddr: %p\n", m_VgkAddr);

	ShadowData Data = GetVgkShadowData(m_VgkAddr);

	driver._dirbase = Data.DecryptedClonedCr3;


	return Data.PML4Base;
}




class PrecisePatternUWorldFinder {
private:
	uintptr_t pml4_base;
	uintptr_t pattern_prefix;
	std::vector<uintptr_t> priority_offsets;

	void initializeOffsets() {
		priority_offsets.clear();

		priority_offsets.push_back(0x170);
		priority_offsets.push_back(0x100);
		priority_offsets.push_back(0x130);
		priority_offsets.push_back(0x160);
		priority_offsets.push_back(0x190);
		priority_offsets.push_back(0x40);
		priority_offsets.push_back(0xC0);
		priority_offsets.push_back(0xE0);
		priority_offsets.push_back(0xA0);
		priority_offsets.push_back(0xB0);
		priority_offsets.push_back(0xD0);
		priority_offsets.push_back(0xF0);
		priority_offsets.push_back(0x1a0);

		for (uintptr_t offset = 0x10; offset <= 0x300; offset += 0x10) {
			if (std::find(priority_offsets.begin(), priority_offsets.end(), offset) == priority_offsets.end()) {
				priority_offsets.push_back(offset);
			}
		}
	}

	bool matchesExactPattern(uintptr_t addr) {
		if (addr == 0) return false;
		if ((addr & 0x7) != 0) return false;

		uintptr_t high_pattern = (addr >> 40) & 0xFFFFF;

		if (high_pattern == (pattern_prefix >> 40)) {
			return true;
		}

		return false;
	}

	bool isValidPointer(uintptr_t addr) {
		return (
			addr > (pml4_base + 0x1000) &&
			addr < 0x7FFFFFFFFFFF &&
			(addr & 0x7) == 0
			);
	}

	bool verifyUWorld(uintptr_t candidate) {
		try {
			uintptr_t first_value = driver.readv<uintptr_t>(candidate);
			return isValidPointer(first_value);
		}
		catch (...) {
			return false;
		}
	}


public:
	PrecisePatternUWorldFinder(uintptr_t pml4) : pml4_base(pml4) {

		pattern_prefix = pml4_base & 0xFFFFF00000000;

		initializeOffsets();

		printf("[*] PML4 Base: 0x%llX\n", pml4_base);
		printf("[*] Looking for UWorld pattern: 0x%llXXXXXXX\n", pattern_prefix >> 40);
	}

	bool isFromSameRegion(uintptr_t addr) {
		if (!isValidPointer(addr)) return false;

		return ((addr & 0xFFFFF00000000) == (pml4_base & 0xFFFFF00000000));
	}

	uintptr_t findUWorld() {
		printf("[*] Searching for UWorld from PML4 region: 0x%llX...\n", pml4_base);

		for (uintptr_t offset : priority_offsets) {
			try {
				uintptr_t value = driver.readv<uintptr_t>(pml4_base + offset);
				if (!isValidPointer(value)) continue;

				if (isFromSameRegion(value)) {
					if (verifyUWorld(value)) {
						printf("[+] Found UWorld at PML4+0x%X: 0x%llX\n",
							(unsigned int)offset, value);
						return value;
					}
				}
			}
			catch (...) {}
		}

		printf("[-] No valid UWorld found from PML4 region\n");
		return 0;
	}

	void debugScan() {
		printf("\n[DEBUG] Scanning offsets for pattern 0x%llXXXXXXX:\n", pattern_prefix >> 40);

		for (size_t i = 0; i < 10 && i < priority_offsets.size(); i++) {
			uintptr_t offset = priority_offsets[i];
			try {
				uintptr_t value = driver.readv<uintptr_t>(pml4_base + offset);
				if (value != 0) {
					uintptr_t high_pattern = (value >> 40) & 0xFFFFF;
					printf("  Offset 0x%X: 0x%llX (pattern: 0x%X)\n",
						(unsigned int)offset, value, (unsigned int)high_pattern);
				}
			}
			catch (...) {}
		}
	}
};






std::vector<std::thread> threads;





static uintptr_t cached_world = 0;
static DWORD64 last_search = 0;
bool negritoeee = true;
bool printed_once = false;


uintptr_t getfnamekey(uintptr_t base_address) {
	try {
		// Read the key from fnamekey offset (FnameState + 0x38)
		const uint32_t key = driver.readv<uint32_t>(base_address + offsets::fnamekey);
		
		// Read the state array (7 uint64_t values) from FnameState
		uint64_t state[7];
		for (int i = 0; i < 7; i++) {
			state[i] = driver.readv<uint64_t>(base_address + offsets::FnameState + (i * 8));
		}
		
		// Decrypt to get the pointer
		const uintptr_t ptr = decrypt_xor_keys(key, state);
		
		// Read the actual FName key from the decrypted pointer
		uintptr_t result = driver.readv<uintptr_t>(ptr);
		
		// Validate result - a valid key shouldn't be too large or zero
		if (result == 0 || result > 0xFFFFFFFF) {
			printf("[WARNING] getfnamekey decryption returned suspicious value: 0x%llX, using fallback\n", result);
			// Try reading key directly as fallback
			return driver.readv<uintptr_t>(base_address + offsets::fnamekey);
		}
		
		printf("[DEBUG] getfnamekey: key=0x%X, ptr=0x%llX, result=0x%llX\n", key, ptr, result);
		return result;
	}
	catch (...) {
		printf("[ERROR] getfnamekey failed, using fallback\n");
		try {
			return driver.readv<uintptr_t>(base_address + offsets::fnamekey);
		}
		catch (...) {
			return 0;
		}
	}
}
std::string SLIGHTSTY(int key)
{
	try {
		static std::uintptr_t cachedNameKey = 0;
		if (!cachedNameKey)
			cachedNameKey = getfnamekey(baseadd);

		UINT chunkOffset = key >> 16;
		USHORT nameOffset = key & 0xFFFF;

		std::uintptr_t chunkAddr = baseadd + offsets::fnamepool + ((chunkOffset + 2) * 8);
		std::uint64_t namePoolChunk = driver.readv<std::uint64_t>(chunkAddr);
		std::uintptr_t entryOffset = namePoolChunk + (4 * nameOffset);

		FNameEntry nameEntry = driver.readv<FNameEntry>(entryOffset);
		auto name = nameEntry.AnsiName;

		const BYTE* keyBytes = reinterpret_cast<BYTE*>(&cachedNameKey);
		for (std::uint16_t i = 0; i < nameEntry.Header.Len && i < 1024; i++)
		{
			name[i] ^= nameEntry.Header.Len ^ keyBytes[i & 3];
		}

		if (nameEntry.Header.Len < 1024) {
			name[nameEntry.Header.Len] = '\0';
		} else {
			name[1023] = '\0';
		}

		std::string result(name);
		if (result.length() > 0 && (unsigned char)result[0] < 32) {
			return "Unknown";
		}
		return result;
	}
	catch (...) {
		return "Error";
	}
}


auto CacheGame() -> void
{


	if (!Settings::bHVCI)
	{
		auto pml4maw = GetPML4Base();
		PrecisePatternUWorldFinder finder(pml4maw);
		finder.debugScan();
	
		static bool pointer_state_init = false;
	
		while (true) {
			//if (Settings::bHVCI) {
			uintptr_t GWORLD;
			DWORD64 current_time = GetTickCount64();

			if (cached_world == 0 || (current_time - last_search) > 10000) {
				uintptr_t found_world = finder.findUWorld();

				if (found_world != 0) {
					cached_world = found_world;
					last_search = current_time;
				}
				else {
					printf("[-] No UWorld found\n");
					cached_world = 0;
				}
			}

			if (cached_world != 0) {
				GWORLD = cached_world;

				// Initialize pointer decryption state once with the actual GWorld value
				if (!pointer_state_init) {
					printf("[*] About to initialize pointer state with baseadd: %p, GWorld: %p\n", (void*)baseadd, (void*)GWORLD);
					init_pointer_state(baseadd, GWORLD);
					printf("[*] After init_pointer_state call\n");
					pointer_state_init = true;
				}

				static DWORD64 last_print = 0;
				if (current_time - last_print > 2000) {
					printf((const char*)"[*] World: 0x%p\n", GWORLD);
					last_print = current_time;
				}
			}
			else {
				Sleep(1000);
				continue;
			}

			uintptr_t World = GWORLD;
			std::vector<ValEntity> CachedList;

			printf("[DEBUG] World: %p\n", (void*)World);

			auto ULevelPtr = UWorld->ULevel(World);
			printf("[DEBUG] Ulevelptr: %p\n", (void*)ULevelPtr);
			auto UGameInstancePtr = UWorld->GameInstance(World);
			printf("[DEBUG] Gameinstance: %p\n", (void*)UGameInstancePtr);
			auto ULocalPlayerPtr = UGameInstance->ULocalPlayer(UGameInstancePtr);
			printf("[DEBUG]ULocalPlayerPtr: %p\n", (void*)ULocalPlayerPtr);
			auto APlayerControllerPtr = ULocalPlayer->APlayerController(ULocalPlayerPtr);
			printf("[DEBUG]APlayerControllerPtr: %p\n", (void*)APlayerControllerPtr);
			PlayerCameraManager = APlayerController->APlayerCameraManager(APlayerControllerPtr);
			printf("[DEBUG]PlayerCameraManager: %p\n", (void*)PlayerCameraManager);
			auto MyHUD = APlayerController->AHUD(APlayerControllerPtr);
			printf("[DEBUG] MyHUD : %p\n", (void*)MyHUD);
			auto APawnPtr = APlayerController->APawn(APlayerControllerPtr);
			printf("[DEBUG] APawnPtr : %p\n", (void*)APawnPtr);
			if (APawnPtr != 0)
			{
				MyUniqueID = APawn->UniqueID(APawnPtr);
				printf("[DEBUG] MyUniqueID : %p\n", (void*)MyUniqueID);
				MyRelativeLocation = APawn->RelativeLocation(APawnPtr);

			}

		// Get actors directly from ULevel, don't depend on MyHUD
			{
				auto PlayerArray = ULevel->AActorArray(ULevelPtr);
				printf("[DEBUG] count : %d\n", PlayerArray.Count);
				auto lolcountf = driver.readv<float>(ULevelPtr + 0xA8);
				auto lolcounti = driver.readv<int>(ULevelPtr + 0xA8);
				printf("[DEBUG] countf : %d\n", lolcountf);
				printf("[DEBUG] counti : %d\n", lolcounti);
				auto ViewInfo = driver.readv<FMinimalViewInfo>(PlayerCameraManager + 0x17C0);
				printf("[DEBUG] FOV : %d\n", ViewInfo.FOV);
				printf("[DEBUG] FOV2 : %d\n", ViewInfo.Rotation);
			printf("[DEBUG] loc : %d\n", ViewInfo.Location);
			
			// FOV offset detection - test multiple candidate offsets
			float fov_test_20 = driver.readv<float>(PlayerCameraManager + 0x17C0 + 0x20);
			float fov_test_24 = driver.readv<float>(PlayerCameraManager + 0x17C0 + 0x24);
			float fov_test_28 = driver.readv<float>(PlayerCameraManager + 0x17C0 + 0x28);
			float fov_test_30 = driver.readv<float>(PlayerCameraManager + 0x17C0 + 0x30);
			float fov_test_38 = driver.readv<float>(PlayerCameraManager + 0x17C0 + 0x38);
			printf("[DEBUG] FOV tests: 0x20=%.2f, 0x24=%.2f, 0x28=%.2f, 0x30=%.2f, 0x38=%.2f\n", 
				   fov_test_20, fov_test_24, fov_test_28, fov_test_30, fov_test_38);
			
			for (uint32_t i = 0; i < PlayerArray.Count; ++i)
				{

					auto Pawns = PlayerArray[i];

					std::string agent_name = SLIGHTSTY(driver.readv<int>(Pawns + 0x18));
					
					if (Pawns != APawnPtr && agent_name.find("_PC_C") != std::string::npos)
					{
						printf("actor: %s\n", agent_name.c_str());

						ValEntity Entities{ Pawns };
						CachedList.push_back(Entities);

					}

				}

				ValList.clear();
				ValList = CachedList;
				Sleep(1000);
			}
		}


	}
	else
	{
		static bool pointer_state_init = false;
		
		while (true) {
		
			uintptr_t World;

			uintptr_t uworld = driver.readv<uintptr_t>(baseadd + offsets::GWorld);
			World = driver.readv<uintptr_t>(uworld);
			
			// Initialize pointer decryption state once with the actual GWorld value
			if (!pointer_state_init) {
				printf("[*] About to initialize pointer state with baseadd: %p, GWorld: %p\n", (void*)baseadd, (void*)World);
				init_pointer_state(baseadd, World);
				printf("[*] After init_pointer_state call\n");
				pointer_state_init = true;
			}
			
			std::vector<ValEntity> CachedList;

			printf("[DEBUG] World: %p\n", (void*)World);

			auto ULevelPtr = UWorld->ULevel(World);
			printf("[DEBUG] Ulevelptr: %p\n", (void*)ULevelPtr);
			auto UGameInstancePtr = UWorld->GameInstance(World);
			printf("[DEBUG] Gameinstance: %p\n", (void*)UGameInstancePtr);
			auto ULocalPlayerPtr = UGameInstance->ULocalPlayer(UGameInstancePtr);
			printf("[DEBUG]ULocalPlayerPtr: %p\n", (void*)ULocalPlayerPtr);
			auto APlayerControllerPtr = ULocalPlayer->APlayerController(ULocalPlayerPtr);
			printf("[DEBUG]APlayerControllerPtr: %p\n", (void*)APlayerControllerPtr);
			PlayerCameraManager = APlayerController->APlayerCameraManager(APlayerControllerPtr);
			printf("[DEBUG]PlayerCameraManager: %p\n", (void*)PlayerCameraManager);
			auto MyHUD = APlayerController->AHUD(APlayerControllerPtr);
			printf("[DEBUG] MyHUD : %p\n", (void*)MyHUD);
			auto APawnPtr = APlayerController->APawn(APlayerControllerPtr);
			printf("[DEBUG] APawnPtr : %p\n", (void*)APawnPtr);
			if (APawnPtr != 0)
			{
				MyUniqueID = APawn->UniqueID(APawnPtr);
				printf("[DEBUG] MyUniqueID : %p\n", (void*)MyUniqueID);
				MyRelativeLocation = APawn->RelativeLocation(APawnPtr);

			}

			// Get actors directly from ULevel, don't depend on MyHUD
			{
				auto PlayerArray = ULevel->AActorArray(ULevelPtr);
				printf("[DEBUG] count : %d\n", PlayerArray.Count);
				auto lolcountf = driver.readv<float>(ULevelPtr + 0xA8);
				auto lolcounti = driver.readv<int>(ULevelPtr + 0xA8);
				printf("[DEBUG] countf : %d\n", lolcountf);
				printf("[DEBUG] counti : %d\n", lolcounti);
				// Test reading with proper struct offsets based on SDK
				// CameraCachePrivate at 0x17B0, POV at offset 0x10 within it = 0x17C0
				FMinimalViewInfo ViewInfo = driver.readv<FMinimalViewInfo>(PlayerCameraManager + 0x17C0);
				printf("[DEBUG] Camera Location=(%.2f, %.2f, %.2f), Rotation=(%.2f, %.2f, %.2f), FOV=%.2f\n",
					   ViewInfo.Location.x, ViewInfo.Location.y, ViewInfo.Location.z,
					   ViewInfo.Rotation.pitch, ViewInfo.Rotation.yaw, ViewInfo.Rotation.roll,
					   ViewInfo.FOV);

				// Removed redundant FOV testing since we fixed the struct
				for (uint32_t i = 0; i < PlayerArray.Count; ++i)
				{

					auto Pawns = PlayerArray[i];

					std::string agent_name = SLIGHTSTY(driver.readv<int>(Pawns + 0x18));
					printf("actor: %s\n", agent_name.c_str());

				// Only add valid player characters (TrainingBot_PC_C, Reyna_PC_C, etc.)
				// Skip objects, particles, and visual effects
				if (Pawns != APawnPtr && agent_name.find("PC_C") != std::string::npos)
				{
					ValEntity Entities{ Pawns };
					CachedList.push_back(Entities);
				}

			}

			ValList.clear();
			ValList = CachedList;
			Sleep(1000);
		}
	}
}
}

auto CheatLoop() -> void
{
	printf("[ESP] ValList size: %zu\n", ValList.size());
	
	for (ValEntity ValEntityList : ValList)
	{
		printf("[ESP] Processing actor: 0x%llx\n", (unsigned long long)ValEntityList.Actor);
		
		// Skip null or invalid actors
		if (ValEntityList.Actor == 0) {
			printf("[ESP] Actor is null, skipping\n");
			continue;
		}

		auto SkeletalMesh = APrivatePawn->USkeletalMeshComponent(ValEntityList.Actor);
		printf("[ESP] SkeletalMesh: 0x%llx\n", (unsigned long long)SkeletalMesh);
		
		// Skip if no valid skeletal mesh
		if (SkeletalMesh == 0) {
			printf("[ESP] SkeletalMesh is null, skipping\n");
			continue;
		}

		auto RelativeLocation = APawn->RelativeLocation(ValEntityList.Actor);
		auto RelativeLocationProjected = UE4::SDK::ProjectWorldToScreen(RelativeLocation);
		
		printf("[ESP] RelativeLocation: (%f, %f, %f) -> Screen: (%f, %f)\n", 
			RelativeLocation.x, RelativeLocation.y, RelativeLocation.z,
			RelativeLocationProjected.x, RelativeLocationProjected.y);

		auto RelativePosition = RelativeLocation - CameraLocation;
		auto RelativeDistance = RelativePosition.Length() / 10000 * 2;

		auto HeadBone = UE4::SDK::GetEntityBone(SkeletalMesh, 8);
		auto HeadBoneProjected = UE4::SDK::ProjectWorldToScreen(HeadBone);

		auto RootBone = UE4::SDK::GetEntityBone(SkeletalMesh, 0);
		auto RootBoneProjected = UE4::SDK::ProjectWorldToScreen(RootBone);
		auto RootBoneProjected2 = UE4::SDK::ProjectWorldToScreen(FVector(RootBone.x, RootBone.y, RootBone.z - 15));

		auto Distance = MyRelativeLocation.Distance(RelativeLocation);

		float BoxHeight = abs(HeadBoneProjected.y - RootBoneProjected.y);
		float BoxWidth = BoxHeight * 0.40;

		auto ESPColor = ImColor(255, 255, 255);

		auto Health = APawn->Health(ValEntityList.Actor);
		uint8_t netDormancy = driver.readv<uint8_t>(ValEntityList.Actor + offsets::bIsDormant);
		bool isDormant = APawn->bIsDormant(ValEntityList.Actor);
		printf("[ESP] Health: %.2f, NetDormancy: %d (Dormant: %s)\n", Health, netDormancy, isDormant ? "YES" : "NO");
		
		if (Health <= 0) continue;

		if (!APawn->bIsDormant(ValEntityList.Actor))
		{
			printf("[ESP] Drawing ESP! bBox=%d bSnaplines=%d bBoxOutlined=%d\n", 
				Settings::Visuals::bBox, Settings::Visuals::bSnaplines, Settings::Visuals::bBoxOutlined);
			if (Settings::Visuals::bSnaplines)
				DrawTracers(RootBoneProjected, ESPColor);

			if (Settings::Visuals::bBox)
				Draw2DBox(RelativeLocationProjected, BoxWidth, BoxHeight, ESPColor);

			if (Settings::Visuals::bBoxOutlined)
				DrawOutlinedBox(RelativeLocationProjected, BoxWidth, BoxHeight, ESPColor);

			if (Settings::Visuals::bHealth)
				DrawHealthBar(RelativeLocationProjected, BoxWidth, BoxHeight, Health, RelativeDistance);

			if (Settings::Visuals::bDistance)
				DrawDistance(RootBoneProjected2, Distance);
		}
	}
}
