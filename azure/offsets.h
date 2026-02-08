#pragma once
#include <Windows.h>

HWND Entryhwnd = NULL;
int processid = 0;



namespace offsets
{
	uintptr_t PointerState = 0xBDE72D0;
	uintptr_t PointerKey = 0xBDE7308 ;
    DWORD
        FnameState = 0xC153880,
        fnamekey = 0xC1538B8,
        VgkShadowStructPtr = 0xA8190,
        VgkStateIndex = 0xA81A8,
        FnamePool = 0xB4C0200,
        persistent_level = 0x38, 
        local_player_array = 0x40,
        fnamepool = 0xBF6E0C0,
        camera_fov = 0x11f8,
        local_player_pawn = 0x40,
        Gameinstance = 0x1D8,  
        Ulevel = 0x38, 
        LocalPlayers = 0x40, 
        PlayerController = 0x38, 
        PlayerCameraManager = 0x520, 
        MyHUD = 0x518,                  // SDK: APlayerController::MyHUD == 0x0518
        AcknowledgedPawn = 0x510, 
        PlayerState = 0x478, 
        TeamComponent = 0x6A8,
        TeamID = 0xf8, 
        UniqueID = 0x38, 
        FNameID = 0x18, 
        AActorArray = 0xA0, 
        RootComponent = 0x288, 
		RelativeLocation = 0x170,
		MeshComponent = 0x0F40,  // AShooterCharacter::MeshCosmetic3P offset
		DamageHandler = 0xC68,          // SDK: AShooterCharacter::DamageHandler == 0x0C68
		bIsDormant = 0x21D,             // SDK: AActor::NetDormancy == 0x021D (ENetDormancy enum)
		Health = 0x200,  
		ComponentToWorld = 0x2D0, 
		BoneArray = 0x730, 
		bone_array = 0x730,
		BoneArrayCache = 0x740,
		BoneCount = 0x5D0, 
		GWorld = 0xBDE72D0,  
		is_visible = 0x501,
		portrait_map = 0x11c8,
		component_to_world = 0x250,
		mesh = 0x4E8,
        last_submit_time = 0x478,
        last_render_time = 0x47C,
        SpikeTimer = 0x4cc,
        CurrentDefuseSection = 0x4fc,
        ControlRotation = 0x4E0;  

}


namespace Settings
{
     bool bHVCI = true;
     bool bMenu = true;

    namespace Visuals
    {
         bool bSnaplines = true;
         bool bDistance = false;
         bool bBox = true;
         bool bBoxOutlined = false;
         bool bHealth = true;

         float BoxWidth = 1.0f;
    }
}