#pragma once

#include <Windows.h>
#include <map>
#include <d3d9types.h>

#include "driver.h"
#include "structs.h"
#include <vector>
#include "offsets.h"


using namespace UE4Structs;


namespace Globals
{
	DWORD_PTR
		LocalPlayer,
		PlayerController,
		PlayerCameraManager;

	int MyUniqueID, MyTeamID, BoneCount;

	FVector MyRelativeLocation, closestPawn;

	namespace Camera
	{
		FVector CameraLocation;
		FRotator CameraRotation;
		float FovAngle;
	}
	uint64_t baseadd;
	uintptr_t uworldoffset_g;
	uintptr_t pml4_g;
	uintptr_t level_g;
	uintptr_t worldptr_g;
}

using namespace Globals;
using namespace Camera;

// Forward declarations for pointer decryption
extern uint64_t g_pointer_state[7];
extern bool g_state_initialized;
uint64_t decrypt_xor_keys(const uint32_t key, const uint64_t* state);

// Decrypt pointer function - inline to avoid linking issues
inline uintptr_t decrypt_pointer(uintptr_t encrypted_ptr) {
    // Pointer decryption disabled - pointers are not encrypted in this version
    // The raw pointer values are already valid
    return encrypted_ptr;
}

namespace UE4
{
	struct GWorld
	{
		uintptr_t GameInstance(uintptr_t GameWorld) {
			auto encrypted = driver.readv<uintptr_t>(GameWorld + offsets::Gameinstance);
			return decrypt_pointer(encrypted);
		};

		uintptr_t ULevel(uintptr_t World) {
			auto encrypted = driver.readv<uintptr_t>(World + offsets::Ulevel);
			return decrypt_pointer(encrypted);
		};
	};

	struct GGameInstance {
		uintptr_t ULocalPlayer(uintptr_t UGameInstance) {
			auto ULocalPlayerArray = driver.readv<uintptr_t>(UGameInstance + offsets::LocalPlayers);
			auto decrypted_array = decrypt_pointer(ULocalPlayerArray);
			auto encrypted_player = driver.readv<uintptr_t>(decrypted_array);
			return decrypt_pointer(encrypted_player);
		};
	};

	struct GULevel {
		TArrayDrink<uintptr_t> AActorArray(uintptr_t ULevel) {
			return driver.readv<TArrayDrink<uintptr_t>>(ULevel + offsets::AActorArray);
		};
	};

	struct GPrivatePawn {
		uintptr_t USkeletalMeshComponent(uintptr_t Pawn) {
			auto encrypted = driver.readv<uintptr_t>(Pawn + offsets::MeshComponent);
			return decrypt_pointer(encrypted);
		};
	};

	struct GUSkeletalMeshComponent {
		int BoneCount(uintptr_t Mesh) {
			return driver.readv<uintptr_t>(Mesh + offsets::BoneCount);
		};
	};

	struct GLocalPlayer {
		uintptr_t APlayerController(uintptr_t ULocalPlayer) {
			auto encrypted = driver.readv<uintptr_t>(ULocalPlayer + offsets::PlayerController);
			return decrypt_pointer(encrypted);
		};
	};

	struct GPlayerController {
		uintptr_t APlayerCameraManager(uintptr_t APlayerController) {
			auto encrypted = driver.readv<uintptr_t>(APlayerController + offsets::PlayerCameraManager);
			return decrypt_pointer(encrypted);
		};
		uintptr_t AHUD(uintptr_t APlayerController) {
			auto encrypted = driver.readv<uintptr_t>(APlayerController + offsets::MyHUD);
			return decrypt_pointer(encrypted);
		};
		uintptr_t APawn(uintptr_t APlayerController) {
			auto encrypted = driver.readv<uintptr_t>(APlayerController + offsets::AcknowledgedPawn);
			return decrypt_pointer(encrypted);
		};
	};

	struct GPawn {
		auto TeamID(uintptr_t APawn) -> int {
			auto encrypted_state = driver.readv<uintptr_t>(APawn + offsets::PlayerState);
			auto PlayerState = decrypt_pointer(encrypted_state);
			auto encrypted_team = driver.readv<uintptr_t>(PlayerState + offsets::TeamComponent);
			auto TeamComponent = decrypt_pointer(encrypted_team);
			return driver.readv<int>(TeamComponent + offsets::TeamID);
		};

		auto UniqueID(uintptr_t APawn) -> int {
			return driver.readv<int>(APawn + offsets::UniqueID);
		};

		auto FNameID(uintptr_t APawn) -> int {
			return driver.readv<int>(APawn + offsets::FNameID);
		};

		auto RelativeLocation(uintptr_t APawn) -> FVector {
			auto RootComponent = driver.readv<uintptr_t>(APawn + offsets::RootComponent);
			return driver.readv<FVector>(RootComponent + offsets::RelativeLocation);
		};

		auto bIsDormant(uintptr_t APawn) -> bool {
			// NetDormancy is an enum: 0=DORM_Never, 1=DORM_Awake, 2=DORM_DormantAll, 3=DORM_DormantPartial
			// Actor is dormant if NetDormancy >= 2
			uint8_t netDormancy = driver.readv<uint8_t>(APawn + offsets::bIsDormant);
			return netDormancy >= 2;
		};

		auto Health(uintptr_t APawn) -> float {
			auto DamageHandler = driver.readv<uintptr_t>(APawn + offsets::DamageHandler);
			return driver.readv<float>(DamageHandler + offsets::Health);
		};
	};

	auto GetWorld(uintptr_t Pointer) -> uintptr_t
	{
		std::uintptr_t uworld_addr = driver.readv<uintptr_t>(Pointer + 0x50);

		unsigned long long uworld_offset;

		if (uworld_addr > 0x10000000000)
		{
			uworld_offset = uworld_addr - 0x10000000000;
		}
		else {
			uworld_offset = uworld_addr - 0x8000000000;
		}

		return Pointer + uworld_offset;
	}

	auto VectorToRotation(FVector relativeLocation) -> FVector
	{
		constexpr auto radToUnrRot = 57.2957795f;

		return FVector(
			atan2(relativeLocation.z, sqrt((relativeLocation.x * relativeLocation.x) + (relativeLocation.y * relativeLocation.y))) * radToUnrRot,
			atan2(relativeLocation.y, relativeLocation.x) * radToUnrRot,
			0.f);
	}

	auto AimAtVector(FVector targetLocation, FVector cameraLocation) -> FVector
	{
		return VectorToRotation(targetLocation - cameraLocation);
	}

	namespace SDK
	{
		auto GetEntityBone(DWORD_PTR mesh, int id) -> FVector
		{
			DWORD_PTR array = driver.readv<uintptr_t>(mesh + offsets::BoneArray);
			if (array == NULL)
				array = driver.readv<uintptr_t>(mesh + offsets::BoneArrayCache);

			FTransform bone = driver.readv<FTransform>(array + (id * 0x30));

			FTransform ComponentToWorld = driver.readv<FTransform>(mesh + offsets::ComponentToWorld);
			D3DMATRIX Matrix;

			Matrix = MatrixMultiplication(bone.ToMatrixWithScale(), ComponentToWorld.ToMatrixWithScale());

			return FVector(Matrix._41, Matrix._42, Matrix._43);
		}

		auto ProjectWorldToScreen(FVector WorldLocation) -> FVector
		{
			FVector Screenlocation = FVector(0, 0, 0);

			auto ViewInfo = driver.readv<FMinimalViewInfo>(PlayerCameraManager + 0x17C0);

			CameraLocation = ViewInfo.Location;
			CameraRotation = ViewInfo.Rotation;

			// Use FOV from ViewInfo (offset 0x30 in FMinimalViewInfo)
			FovAngle = ViewInfo.FOV;
			if (FovAngle <= 0.0f || FovAngle > 180.0f) {
				FovAngle = 90.0f; // Fallback to default if invalid
			}

			D3DMATRIX tempMatrix = Matrix(FVector(CameraRotation.pitch, CameraRotation.yaw, CameraRotation.roll), FVector(0, 0, 0));

			FVector vAxisX = FVector(tempMatrix.m[0][0], tempMatrix.m[0][1], tempMatrix.m[0][2]),
				vAxisY = FVector(tempMatrix.m[1][0], tempMatrix.m[1][1], tempMatrix.m[1][2]),
				vAxisZ = FVector(tempMatrix.m[2][0], tempMatrix.m[2][1], tempMatrix.m[2][2]);

			FVector vDelta = WorldLocation - CameraLocation;
			FVector vTransformed = FVector(vDelta.Dot(vAxisY), vDelta.Dot(vAxisZ), vDelta.Dot(vAxisX));

			if (vTransformed.z < 1.f) vTransformed.z = 1.f;

			// Get screen size from ImGui
			ImGuiIO& io = ImGui::GetIO();
			float ScreenWidth = io.DisplaySize.x;
			float ScreenHeight = io.DisplaySize.y;
			float ScreenCenterX = ScreenWidth / 2.0f;
			float ScreenCenterY = ScreenHeight / 2.0f;

			float fov_radians = FovAngle * (float)M_PI / 360.f;
			float scale = ScreenCenterX / tanf(fov_radians);

			Screenlocation.x = ScreenCenterX + vTransformed.x * scale / vTransformed.z;
			Screenlocation.y = ScreenCenterY - vTransformed.y * scale / vTransformed.z;

			return Screenlocation;
		}

		bool IsVec3Valid(FVector vec3)
		{
			return !(vec3.x == 0 && vec3.y == 0 && vec3.z == 0);
		}
	}
}